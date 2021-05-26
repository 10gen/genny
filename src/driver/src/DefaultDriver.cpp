// Copyright 2019-present MongoDB Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <algorithm>
#include <sstream>
#include <thread>
#include <vector>

#include <boost/algorithm/string/join.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/exception/exception.hpp>
#include <boost/filesystem.hpp>
#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/trivial.hpp>
#include <boost/program_options.hpp>

#include <yaml-cpp/yaml.h>

#include <loki/ScopeGuard.h>

#include <gennylib/Cast.hpp>
#include <gennylib/context.hpp>

#include <metrics/MetricsReporter.hpp>
#include <metrics/metrics.hpp>

#include <driver/v1/DefaultDriver.hpp>

namespace genny::driver {
namespace {


YAML::Node loadFile(const std::string& source) {
    try {
        return YAML::LoadFile(source);
    } catch (const std::exception& ex) {
        BOOST_LOG_TRIVIAL(error) << "Error loading yaml from " << source << ": " << ex.what();
        throw;
    }
}

namespace fs = boost::filesystem;

template <typename Actor>
void runActor(Actor&& actor,
              std::atomic<driver::DefaultDriver::OutcomeCode>& outcomeCode,
              Orchestrator& orchestrator) {
    auto guard = Loki::MakeGuard([&]() { orchestrator.abort(); });

    try {
        actor->run();
    } catch (const boost::exception& x) {
        BOOST_LOG_TRIVIAL(error) << "Unexpected boost::exception: "
                                 << boost::diagnostic_information(x, true);
        outcomeCode = driver::DefaultDriver::OutcomeCode::kBoostException;
    } catch (const std::exception& x) {
        BOOST_LOG_TRIVIAL(error) << "Unexpected std::exception: " << x.what();
        outcomeCode = driver::DefaultDriver::OutcomeCode::kStandardException;
    } catch (...) {
        BOOST_LOG_TRIVIAL(error) << "Unknown error";
        // Don't try to handle unknown errors, let us crash ungracefully
        throw;
    }
}

void reportMetrics(genny::metrics::Registry& metrics,
                   const std::string& workloadName,
                   bool success,
                   metrics::clock::time_point startTime) {
    auto finishTime = metrics::clock::now();
    // The "Setup" operation is a genny internal operation. We want the trend graph to be hidden by
    // default to not confuse users, so we prefix it with "canary_" to hit the
    // CANARY_EXCLUSION_REGEX in https://git.io/Jtjdr
    auto actorSetup = metrics.operation("canary_" + workloadName, "Setup", 0u);
    auto outcome = success ? metrics::OutcomeType::kSuccess : metrics::OutcomeType::kFailure;
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(finishTime - startTime);
    actorSetup.report(std::move(finishTime), std::move(duration), std::move(outcome));
}

// Ugh. This is basically just implementing the following python:
//     ignored = set("foo","bar")
//     return [u for u in unused if not any(ignore in u for ignore in ignored)]
//
// The 'unused' param is pass by value because we need to modify it via remove_if.
UnusedNodes removeFalsePositives(UnusedNodes unused) {
    // The "Clients" key being looks bad but is actually okay for dry-run workloads since
    // connection pools are lazily constructed on first access.

    static const std::vector<std::string> ignored{".yml/Description", ".yml/Owner", ".yml/AutoRun", ".yml/Clients"};
    UnusedNodes out{};
    std::copy_if(
        unused.begin(), unused.end(), std::back_inserter(out), [&](const std::string& path) {
            // not any of the ignored values are in our path value.
            return !std::any_of(ignored.begin(), ignored.end(), [&](const std::string& ignore) {
                return path.find(ignore) != std::string::npos;
            });
        });
    return out;
}

void reportUnused(const NodeSource& nodeSource) {
    auto raw = nodeSource.unused();

    auto unused = removeFalsePositives(raw);
    auto many = std::distance(unused.begin(), unused.end());
    auto verb = many == 1 ? "was" : "were";
    auto plural = many == 1 ? "" : "s";
    BOOST_LOG_TRIVIAL(info) << "<BETA FEATURE>";
    BOOST_LOG_TRIVIAL(info) << "There " << verb << " " << many << " YAML structure" << plural
                            << " not used when constructing the workload.";
    if (!unused.empty()) {
        BOOST_LOG_TRIVIAL(info) << "\n\t" << boost::algorithm::join(unused, "\n\t");
        BOOST_LOG_TRIVIAL(info) << "False-positives are possible. "
                                   "Please let us know in #workload-generation slack "
                                   "if this looks wrong.";
    }
    BOOST_LOG_TRIVIAL(info) << "</BETA FEATURE>";
}

DefaultDriver::OutcomeCode doRunLogic(const DefaultDriver::ProgramOptions& options) {
    // setup logging as the first thing we do.
    boost::log::core::get()->set_filter(boost::log::trivial::severity >= options.logVerbosity);

    const auto workloadName = fs::path(options.workloadSource).stem().string();
    auto startTime = genny::metrics::Registry::clock::now();

    if (options.runMode == DefaultDriver::RunMode::kListActors) {
        globalCast().streamProducersTo(std::cout);
        genny::metrics::Registry metrics;
        reportMetrics(metrics, workloadName, true, startTime);
        return DefaultDriver::OutcomeCode::kSuccess;
    }

    if (options.workloadSource.empty()) {
        std::cerr << "Must specify a workload YAML file" << std::endl;
        genny::metrics::Registry metrics;
        reportMetrics(metrics, workloadName, false, startTime);
        return DefaultDriver::OutcomeCode::kUserException;
    }

    fs::path phaseConfigSource;
    if (options.workloadSourceType == DefaultDriver::ProgramOptions::YamlSource::kString) {
        phaseConfigSource = fs::current_path();
    } else {
        /*
         * The directory structure for workloads and phase configs is as follows:
         * /etc (or /src if building without installing)
         *     /workloads
         *         /[name-of-workload-theme]
         *             /[my-workload.yml]
         * The path to the phase config snippet is obtained as a relative path from the workload
         * file.
         */
        phaseConfigSource = fs::path(options.workloadSource).parent_path();
    }

    YAML::Node yaml;
    if (options.workloadSourceType == DefaultDriver::ProgramOptions::YamlSource::kFile) {
        yaml = loadFile(options.workloadSource);
    } else if (options.workloadSourceType == DefaultDriver::ProgramOptions::YamlSource::kString) {
        yaml = YAML::Load(options.workloadSource);
    } else {
        throw std::invalid_argument("Unrecognized workload source type.");
    }

    auto orchestrator = Orchestrator{};

    NodeSource nodeSource{YAML::Dump(yaml),
                          options.workloadSourceType ==
                                  DefaultDriver::ProgramOptions::YamlSource::kFile
                              ? options.workloadSource
                              : "inline-yaml"};


    auto workloadContext =
        WorkloadContext{nodeSource.root(), orchestrator, options.mongoUri, globalCast()};

    genny::metrics::Registry& metrics = workloadContext.getMetrics();

    if (options.runMode == DefaultDriver::RunMode::kDryRun) {
        std::cout << "Workload context constructed without errors." << std::endl;
        reportMetrics(metrics, workloadName, true, startTime);
        reportUnused(nodeSource);
        return DefaultDriver::OutcomeCode::kSuccess;
    }

    orchestrator.addRequiredTokens(
        int(std::distance(workloadContext.actors().begin(), workloadContext.actors().end())));

    reportMetrics(metrics, workloadName, true, startTime);

    // The "ActorStarted" and "ActorFinished" operations are genny internal operations. We want the
    // trend graph to be hidden by default to not confuse users, so we prefix them with "canary_"
    // to hit the CANARY_EXCLUSION_REGEX in https://git.io/Jtjdr
    auto startedActors = metrics.operation("canary_" + workloadName, "ActorStarted", 0u);
    auto finishedActors = metrics.operation("canary_" + workloadName, "ActorFinished", 0u);

    std::atomic<DefaultDriver::OutcomeCode> outcomeCode = DefaultDriver::OutcomeCode::kSuccess;

    std::mutex reporting;
    std::vector<std::thread> threads;
    std::transform(cbegin(workloadContext.actors()),
                   cend(workloadContext.actors()),
                   std::back_inserter(threads),
                   [&](const auto& actor) {
                       return std::thread{[&]() {
                           {
                               auto ctx = startedActors.start();
                               ctx.addDocuments(1);

                               std::lock_guard<std::mutex> lk{reporting};
                               ctx.success();
                           }

                           runActor(actor, outcomeCode, orchestrator);

                           {
                               auto ctx = finishedActors.start();
                               ctx.addDocuments(1);

                               std::lock_guard<std::mutex> lk{reporting};
                               ctx.success();
                           }
                       }};
                   });

    for (auto& thread : threads)
        thread.join();

    if (metrics.getFormat().useCsv()) {
        const auto reporter = genny::metrics::Reporter{metrics};

        {
            std::ofstream metricsOutput;
            metricsOutput.open(metrics.getPathPrefix().string() + ".csv",
                               std::ofstream::out | std::ofstream::trunc);
            reporter.report(metricsOutput, metrics.getFormat());
        }
    }

    reportUnused(nodeSource);
    return outcomeCode;
}

}  // namespace


DefaultDriver::OutcomeCode DefaultDriver::run(const DefaultDriver::ProgramOptions& options) const {
    try {
        // Wrap doRunLogic in another catch block in case it throws an exception of its own e.g.
        // file not found or io errors etc - exceptions not thrown by ActorProducers.
        return doRunLogic(options);
    } catch (const boost::exception& x) {
        BOOST_LOG_TRIVIAL(error) << "Caught boost::exception "
                                 << boost::diagnostic_information(x, true);
    } catch (const std::exception& x) {
        BOOST_LOG_TRIVIAL(error) << "Caught std::exception " << x.what();
    }
    return DefaultDriver::OutcomeCode::kInternalException;
}


namespace {

/**
 * Normalize the metrics output file command-line option.
 *
 * @param str the input option value from the command-lien
 * @return the file-path that should be used to open the output stream.
 */
// There may be a more conventional way to define conversion/normalization
// functions for use with boost::program_options. The tutorial isn't the
// clearest thing. If we need to do more than 1-2, look into that further.
std::string normalizeOutputFile(const std::string& str) {
    if (str == "-") {
        return std::string("/dev/stdout");
    }
    return str;
}

boost::log::trivial::severity_level parseVerbosity(const std::string& level) {

    if (level == "trace") {
        return boost::log::trivial::trace;
    }
    if (level == "debug") {
        return boost::log::trivial::debug;
    }
    if (level == "info") {
        return boost::log::trivial::info;
    }
    if (level == "warning") {
        return boost::log::trivial::warning;
    }
    if (level == "error") {
        return boost::log::trivial::error;
    }
    if (level == "fatal") {
        return boost::log::trivial::fatal;
    }

    throw std::invalid_argument("Invalid verbosity level '" + level +
                                "'. Need one of trace/debug/info/warning/error/fatal");
}

}  // namespace

const std::string RUNNER_NAME = "genny";

DefaultDriver::ProgramOptions::ProgramOptions(int argc, char** argv) {
    namespace po = boost::program_options;

    std::ostringstream progDescStream;
    // Section headers are prefaced with new lines.
    progDescStream << u8"\n🧞 Usage:\n";
    progDescStream << "    " << RUNNER_NAME << " <subcommand> [options] <workload-file>\n";
    progDescStream << u8"\n🧞 Subcommands:‍";
    progDescStream << u8R"(
    run          Run the workload normally
    dry-run      Exit before the run step -- this may still make network
                 connections during workload initialization
    list-actors  List all actors available for use
    )" << "\n";

    progDescStream << "🧞 Options";
    po::options_description progDescription{progDescStream.str()};
    po::positional_options_description positional;

    // clang-format off
    progDescription.add_options()
            ("subcommand", po::value<std::string>(), "1st positional argument")
            ("help,h",
             "Show help message")

            ("workload-file,w",
             po::value<std::string>(),
             "Path to workload configuration yaml file. "
             "Paths are relative to the program's cwd. "
             "Can also specify as the last positional argument.")
            ("mongo-uri,u",
             po::value<std::string>()->default_value("mongodb://localhost:27017"),
             "Mongo URI to use for the default connection-pool.")
            ("verbosity,v",
              po::value<std::string>()->default_value("info"),
              "Log severity for boost logging. Valid values are trace/debug/info/warning/error/fatal.");

    positional.add("subcommand", 1);
    positional.add("workload-file", -1);

    auto run = po::command_line_parser(argc, argv)
            .options(progDescription)
            .positional(positional)
            .run();
    // clang-format on

    {
        auto stream = std::ostringstream();
        stream << progDescription;
        this->description = stream.str();
    }

    po::variables_map vm;
    po::store(run, vm);
    po::notify(vm);

    if (!vm.count("subcommand")) {
        std::cerr << "ERROR: missing subcommand" << std::endl;
        this->runMode = RunMode::kHelp;
        return;
    }
    const auto subcommand = vm["subcommand"].as<std::string>();

    if (subcommand == "list-actors")
        this->runMode = RunMode::kListActors;
    else if (subcommand == "dry-run")
        this->runMode = RunMode::kDryRun;
    else if (subcommand == "run")
        this->runMode = RunMode::kNormal;
    else if (subcommand == "help")
        this->runMode = RunMode::kHelp;
    else {
        std::cerr << "ERROR: Unexpected subcommand " << subcommand << std::endl;
        this->runMode = RunMode::kHelp;
        return;
    }

    if (vm.count("help") >= 1)
        this->runMode = RunMode::kHelp;

    this->logVerbosity = parseVerbosity(vm["verbosity"].as<std::string>());
    this->mongoUri = vm["mongo-uri"].as<std::string>();

    if (vm.count("workload-file") > 0) {
        this->workloadSource = vm["workload-file"].as<std::string>();
        this->workloadSourceType = YamlSource::kFile;
    } else {
        this->workloadSourceType = YamlSource::kString;
    }
}
}  // namespace genny::driver
