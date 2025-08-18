/*
 * SideQuest Starting Point
 *
 * Basically lets paul bootstrap his projects.
 *
 * Copyright 2024-2025, Paul Walker and Various authors, as described in the github
 * transaction log.
 *
 * This source repo is released under the MIT license, but has
 * GPL3 dependencies, as such the combined work will be
 * released under GPL3.
 *
 * The source code and license are at https://github.com/baconpaul/sidequest-startingpoint
 */

#ifndef BACONPAUL_SIDEQUEST_CONFIGURATION_H
#define BACONPAUL_SIDEQUEST_CONFIGURATION_H

#include <stddef.h>
#include <cstdint>
#include <string>
#include <cstring>
#include <iostream>
#include "sst/plugininfra/version_information.h"

namespace baconpaul::sidequest_ns
{

static constexpr size_t blockSize{8};
static constexpr size_t maxVoices{128};

extern int debugLevel;

} // namespace baconpaul::sidequest_ns

inline std::string fileTrunc(const std::string &f)
{
    auto p = f.find(sst::plugininfra::VersionInformation::cmake_source_dir);
    if (p != std::string::npos)
    {
        return f.substr(p + strlen(sst::plugininfra::VersionInformation::cmake_source_dir) + 1);
    }
    return f;
}

#define SQLOG(...)                                                                                 \
    std::cout << fileTrunc(__FILE__) << ":" << __LINE__ << " " << __VA_ARGS__ << std::endl;
#define SQLOG_UNIMPL                                                                               \
    std::cout << fileTrunc(__FILE__) << ":" << __LINE__ << " Unimplemented " << __func__           \
              << std::endl;

#define SQLOG_ONCE(...)                                                                            \
    {                                                                                              \
        static bool x842132{false};                                                                \
        if (!x842132)                                                                              \
        {                                                                                          \
            SQLOG(__VA_ARGS__);                                                                    \
        }                                                                                          \
        x842132 = true;                                                                            \
    }
#define SQD(x) " " << #x << "=" << x

#endif // CONFIGURATION_H
