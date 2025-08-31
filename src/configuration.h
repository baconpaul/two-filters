/*
 * Two Filters
 *
 * Two Filters, and some controls thereof
 *
 * Copyright 2024-2025, Paul Walker and Various authors, as described in the github
 * transaction log.
 *
 * This source repo is released under the MIT license, but has
 * GPL3 dependencies, as such the combined work will be
 * released under GPL3.
 *
 * The source code and license are at https://github.com/baconpaul/two-filters
 */

#ifndef BACONPAUL_TWOFILTERS_CONFIGURATION_H
#define BACONPAUL_TWOFILTERS_CONFIGURATION_H

#include <stddef.h>
#include <cstdint>
#include <string>
#include <cstring>
#include <iostream>
#include "sst/plugininfra/version_information.h"
#include <fmt/core.h>

namespace baconpaul::twofilters
{

static constexpr size_t blockSize{8};
static constexpr size_t numFilters{2};
static constexpr size_t numStepLFOs{2};
static constexpr size_t maxSteps{32};

constexpr uint32_t edWidth{920}, edHeight{750};

extern int debugLevel;

} // namespace baconpaul::twofilters

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

#define SQLOGFMT(...)                                                                              \
    std::cout << fileTrunc(__FILE__) << ":" << __LINE__ << " " << fmt::format(__VA_ARGS__)         \
              << std::endl;

#define SQLOG_ERR(...)                                                                             \
    std::cout << fileTrunc(__FILE__) << ":" << __LINE__ << " [ERROR] " << __VA_ARGS__ << std::endl;

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
