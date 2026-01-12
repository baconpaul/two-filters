/*
 * Two Filters
 *
 * Two Filters, and some controls thereof
 *
 * Copyright 2024-2026, Paul Walker and Various authors, as described in the github
 * transaction log.
 *
 * This source repo is released under the MIT license, but has
 * GPL3 dependencies, as such the combined work will be
 * released under GPL3.
 *
 * The source code and license are at https://github.com/baconpaul/two-filters
 */

#ifndef BACONPAUL_TWOFILTERS_UI_UI_DEFAULTS_H
#define BACONPAUL_TWOFILTERS_UI_UI_DEFAULTS_H

#include "configuration.h"
#include <sst/plugininfra/userdefaults.h>

namespace baconpaul::twofilters::ui
{
enum Defaults
{
    useLightSkin,
    zoomLevel,
    useSoftwareRenderer, // only used on windows
    useLowCpuGraphics,
    modelConfigMode,
    numDefaults
};

inline std::string defaultName(Defaults d)
{
    switch (d)
    {
    case useLightSkin:
        return "useLightSkin";
    case zoomLevel:
        return "zoomLevel";
    case useSoftwareRenderer:
        return "useSoftwareRenderer";
    case useLowCpuGraphics:
        return "useLowCpuGraphics";
    case modelConfigMode:
        return "modelConfigMode";
    case numDefaults:
    {
        SQLOG("Software Error - defaults found");
        return "";
    }
    }
    return "";
}

using defaultsProvider_t = sst::plugininfra::defaults::Provider<Defaults, Defaults::numDefaults>;
} // namespace baconpaul::twofilters::ui

#endif // UI_DEFAULTS_H
