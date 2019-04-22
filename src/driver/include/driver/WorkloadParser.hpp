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

#ifndef HEADER_0369D27D_9A68_4981_B344_4BAB3EE09A80_INCLUDED
#define HEADER_0369D27D_9A68_4981_B344_4BAB3EE09A80_INCLUDED

#include <boost/filesystem.hpp>
#include <yaml-cpp/yaml.h>

#include <driver/v1/DefaultDriver.hpp>

namespace genny::driver::v1 {

using YamlParameters = std::map<std::string, YAML::Node>;
namespace fs = boost::filesystem;

/**
 * Parse user-defined workload files into shapes suitable for Genny.
 */
class WorkloadParser {
public:
    explicit WorkloadParser(fs::path& phaseConfigPath, bool isSmokeTest = false)
        : _isSmokeTest{isSmokeTest}, _phaseConfigPath{phaseConfigPath} {};

    YAML::Node parse(const std::string& source,
                     const DefaultDriver::ProgramOptions::YamlSource =
                         DefaultDriver::ProgramOptions::YamlSource::kFile);

private:
    enum class ParseMode;
    YamlParameters _params;
    const fs::path _phaseConfigPath;

    const bool _isSmokeTest;

    YAML::Node recursiveParse(YAML::Node, ParseMode parseMode);

    // Handle workload files containing external configuration files.
    void convertExternal(std::string key, YAML::Node value, YAML::Node& out);
    YAML::Node parseExternal(YAML::Node);
    YAML::Node replaceParam(YAML::Node);

    // Handle converting workload files into smoke test versions.
    void convertToSmokeTest(std::string key, YAML::Node value, YAML::Node& out);
};

}  // namespace genny::driver::v1


#endif  // HEADER_0369D27D_9A68_4981_B344_4BAB3EE09A80_INCLUDED
