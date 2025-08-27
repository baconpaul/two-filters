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

#include "configuration.h"
#include "sst/plugininfra/misc_platform.h"
#include "sst/plugininfra/version_information.h"
#include "clap/plugin-clap-entry-impl.h"
#include "clap/plugin.h"
#include "clapwrapper/vst3.h"
#include "clapwrapper/auv2.h"

#include <iostream>
#include <cstring>
#include <string.h>
#include <clap/clap.h>

namespace baconpaul::twofilters
{

extern const clap_plugin *makePlugin(const clap_host *);

/*
 * Clap Factory API
 */
const clap_plugin_descriptor *getDescriptor()
{
    static const char *features[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_FILTER,
                                     "Free and Open Source", nullptr};

    static char versionNum[1024];

    static clap_plugin_descriptor desc = {
        CLAP_VERSION,
        "org.baconpaul.twofilters",
        PRODUCT_NAME,
        "BaconPaul",
        "https://baconpaul.org",
        "",
        "",
        sst::plugininfra::VersionInformation::project_version_and_hash,
        "Two Filters. Fun!",
        &features[0]};
    return &desc;
}

uint32_t clap_get_plugin_count(const clap_plugin_factory *) { return 1; };
const clap_plugin_descriptor *clap_get_plugin_descriptor(const clap_plugin_factory *f, uint32_t w)
{
    if (w == 0)
    {
        return getDescriptor();
    }

    return nullptr;
}

const clap_plugin *clap_create_plugin(const clap_plugin_factory *f, const clap_host *host,
                                      const char *plugin_id)
{
    if (strcmp(plugin_id, getDescriptor()->id) == 0)
    {
        return makePlugin(host);
    }

    return nullptr;
}

/*
 * Clap Wrapper AUV2 Factory API
 */

static bool clap_get_auv2_info(const clap_plugin_factory_as_auv2 *factory, uint32_t index,
                               clap_plugin_info_as_auv2_t *info)
{
    if (index > 1)
        return false;

    if (index == 0)
    {
        strncpy(info->au_type, "aufx", 5); // use the features to determine the type
        strncpy(info->au_subt, "twFl", 5);
    }

    return true;
}

/*
 * Clap Wrapper VST3 Factory API
 */
static const clap_plugin_info_as_vst3 *clap_get_vst3_info(const clap_plugin_factory_as_vst3 *f,
                                                          uint32_t index)
{
    return nullptr;
}

const void *get_factory(const char *factory_id)
{
    if (strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID) == 0)
    {
        static const struct clap_plugin_factory twofilters_clap_factory = {
            clap_get_plugin_count,
            clap_get_plugin_descriptor,
            clap_create_plugin,
        };
        return &twofilters_clap_factory;
    }
    if (strcmp(factory_id, CLAP_PLUGIN_FACTORY_INFO_AUV2) == 0)
    {
        static const struct clap_plugin_factory_as_auv2 twofilters_auv2_factory = {
            "BcPL",      // manu
            "BaconPaul", // manu name
            clap_get_auv2_info};
        return &twofilters_auv2_factory;
    }
    if (strcmp(factory_id, CLAP_PLUGIN_FACTORY_INFO_VST3) == 0)
    {
        static const struct clap_plugin_factory_as_vst3 twofilters_vst3_factory = {
            "BaconPaul", "https://baconpaul.org", "", clap_get_vst3_info};

        return &twofilters_vst3_factory;
    }

    return nullptr;
}
bool clap_init(const char *p)
{
    // sst::plugininfra::misc_platform::allocateConsole();
    SQLOG("Initializing Two Filters "
          << sst::plugininfra::VersionInformation::project_version_and_hash << " / "
          << sst::plugininfra::VersionInformation::git_implied_display_version);
    return true;
}
void clap_deinit() {}
} // namespace baconpaul::twofilters