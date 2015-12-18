#include <iostream>
#include <fstream>
#include <string>
#include "yaml-cpp/yaml.h"
#include <bsoncxx/builder/stream/document.hpp>
#include <bsoncxx/builder/stream/array_context.hpp>
#include <bsoncxx/builder/stream/single_context.hpp>
#include <bsoncxx/json.hpp>
#include <bson.h>
#include <vector>
#include <getopt.h>

#include <mongocxx/client.hpp>
#include <mongocxx/instance.hpp>
#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/expressions.hpp>

#include "workload.hpp"
#include "main.h"

using namespace std;
using namespace mwg;
namespace logging = boost::log;

static struct option poptions[] = {{"help", no_argument, 0, 'h'},
                                   {"loglevel", required_argument, 0, 'l'},
                                   {"dotfile", required_argument, 0, 'd'},
                                   {"resultsfile", required_argument, 0, 'r'},
                                   {"host", required_argument, 0, 0},
                                   {0, 0, 0, 0}};

void print_help(const char* process_name) {
    fprintf(stderr,
            "Usage: %s [-h] /path/to/workload \n"
            "Execution Options:\n"
            "\t--help|-h             Display this help and exit\n"
            "\t--host Host           Host/Connection string for mongo server to test--must be a\n"
            "\t                      full URI,\n"
            "\t--loglevel|-l LEVEL   Set the logging level. Valid options are trace,\n"
            "\t                      debug, info, warning, error, and fatal.\n"
            "\t--dotfile|-d FILE     Generate dotfile to FILE from workload and exit.\n"
            "\t                      WARNING: names with spaces or other special characters\n"
            "\t                      will break the dot file\n\n"
            "\t--resultfile|-r FILE  FILE to store results to. defaults to results.json\n",
            process_name);
}

int main(int argc, char* argv[]) {
    string filename = "sample.yml";
    string dotFile;
    string resultsFile = "results.json";
    string uri = mongocxx::uri::k_default_uri;
    int arg_count = 0;
    int idx = 0;

    // default logging level to info
    logging::core::get()->set_filter(logging::trivial::severity >= logging::trivial::info);

    while (1) {
        int arg = getopt_long(argc, argv, "hl:d:", poptions, &idx);
        arg_count++;
        if (arg == -1) {
            // all arguments have been processed
            break;
        }
        ++arg_count;

        switch (arg) {
            case 0:
                switch (idx) {
                    case 4:
                        uri = optarg;
                        break;
                    default:
                        fprintf(stderr, "unknown command line option with optarg index %d\n", idx);
                        return EXIT_FAILURE;
                }
                break;
            case 'h':
                print_help(argv[0]);
                return EXIT_SUCCESS;
            case 'l':
                if (strcmp("info", optarg) == 0)
                    logging::core::get()->set_filter(logging::trivial::severity >=
                                                     logging::trivial::info);
                else if (strcmp("trace", optarg) == 0)
                    logging::core::get()->set_filter(logging::trivial::severity >=
                                                     logging::trivial::trace);
                else if (strcmp("debug", optarg) == 0)
                    logging::core::get()->set_filter(logging::trivial::severity >=
                                                     logging::trivial::debug);
                else if (strcmp("warning", optarg) == 0)
                    logging::core::get()->set_filter(logging::trivial::severity >=
                                                     logging::trivial::warning);
                else if (strcmp("error", optarg) == 0)
                    logging::core::get()->set_filter(logging::trivial::severity >=
                                                     logging::trivial::error);
                else if (strcmp("fatal", optarg) == 0)
                    logging::core::get()->set_filter(logging::trivial::severity >=
                                                     logging::trivial::fatal);
                break;
            case 'd':
                dotFile = optarg;
                break;
            case 'r':
                resultsFile = optarg;
                break;
            default:
                fprintf(stderr, "unknown command line option: %s\n", poptions[idx].name);
                print_help(argv[0]);
                return EXIT_FAILURE;
        }
    }

    if (argc > optind)
        filename = argv[optind];
    BOOST_LOG_TRIVIAL(info) << filename;

    // put try catch here with error message
    YAML::Node nodes = YAML::LoadFile(filename);

    // Look for main. And start building from there.
    if (auto main = nodes["main"]) {
        workload myworkload(main);
        if (dotFile.length() > 0) {
            // save the dotgraph
            ofstream dotout;
            dotout.open(dotFile);
            dotout << myworkload.generateDotGraph();
            dotout.close();
            return EXIT_SUCCESS;
        }

        BOOST_LOG_TRIVIAL(trace) << "After workload constructor. Before execute";
        // set the uri
        myworkload.uri = uri;
        myworkload.execute();
        myworkload.logStats();
        if (resultsFile.length() > 0) {
            // save the results
            ofstream out;
            out.open(resultsFile);
            out << bsoncxx::to_json(myworkload.getStats(false));
            out.close();
        }
    }

    else
        BOOST_LOG_TRIVIAL(fatal) << "There was no main";
}
