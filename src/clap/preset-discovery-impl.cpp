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

#include <algorithm>
#include <cctype>
#include <string>

#include "preset-discovery-impl.h"
#include "configuration.h"
#include "presets/preset-manager.h"

namespace baconpaul::twofilters
{

namespace prov
{
static constexpr const char *pluginId{"org.baconpaul.twofilters"};
static constexpr const char *providerId{"org.baconpaul.twofilters.preset-discovery"};

struct Provider
{
    clap_preset_discovery_provider provider{};
    presets::PresetManager pm{nullptr};
    const clap_preset_discovery_indexer_t *indexer{nullptr};
};

bool matchesBlockedHost(const char *s)
{
    if (!s)
        return false;

    std::string v{s};
    std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return std::tolower(c); });
    return v.find("fender") != std::string::npos || v.find("presonus") != std::string::npos;
}

bool init(const struct clap_preset_discovery_provider *provider)
{
    auto pm = static_cast<Provider *>(provider->provider_data);

    // some fender and presonus indexers crash with our provider
    if (matchesBlockedHost(pm->indexer->vendor) || matchesBlockedHost(pm->indexer->name))
    {
        auto vendor = pm->indexer->vendor ? pm->indexer->vendor : "(null)";
        auto name = pm->indexer->name ? pm->indexer->name : "(null)";
        SQLOG("Skipping preset discovery for indexer '" << name << "' / '" << vendor << "'");
        return false;
    }

    clap_preset_discovery_location fac{CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT,
                                       "Two Filters Factory Content",
                                       CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr};
    pm->indexer->declare_location(pm->indexer, &fac);

    clap_preset_discovery_filetype twofl{"Two Filters Preset", "For Two Filters!",
                                         &PATCH_EXTENSION[1]};
    pm->indexer->declare_filetype(pm->indexer, &twofl);

    auto loc = pm->pm.userPatchesPath.u8string();
    clap_preset_discovery_location userLoc{CLAP_PRESET_DISCOVERY_IS_USER_CONTENT,
                                           "Two Filters User Content",
                                           CLAP_PRESET_DISCOVERY_LOCATION_FILE, loc.c_str()};
    pm->indexer->declare_location(pm->indexer, &userLoc);
    return true;
}

void destroy(const struct clap_preset_discovery_provider *provider)
{
    auto pm = static_cast<Provider *>(provider->provider_data);
    delete pm;
}

bool get_metadata(const struct clap_preset_discovery_provider *provider, uint32_t location_kind,
                  const char *location, const clap_preset_discovery_metadata_receiver_t *mdr)
{
    auto pm = static_cast<Provider *>(provider->provider_data);
    clap_universal_plugin_id_t clp{"clap", pluginId};

    if (location_kind == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN)
    {
        for (auto &[cat, pat] : pm->pm.factoryPatchVector)
        {
            auto dName = cat + "/" + presets::PresetManager::stripExtension(pat);
            auto loadKey = presets::PresetManager::factoryLoadKey(cat, pat);

            if (!mdr->begin_preset(mdr, dName.c_str(), loadKey.c_str()))
            {
                SQLOG("Begin Preset failed");
                return false;
            }
            auto desc = "A Two Filters '" + cat + "' Factory Preset";
            mdr->add_plugin_id(mdr, &clp);
            mdr->set_description(mdr, desc.c_str());
        }
        return true;
    }

    auto p = fs::path{fs::u8path(location)};
    p = p.replace_extension("").filename();

    if (!mdr->begin_preset(mdr, p.u8string().c_str(), ""))
    {
        SQLOG("Begin Preset failed");
        return false;
    }
    mdr->add_plugin_id(mdr, &clp);
    mdr->set_description(mdr, "A Two Filters User Preset");

    return true;
}

const void *get_extension(const struct clap_preset_discovery_provider *provider,
                          const char *extension_id)
{
    return nullptr;
}
} // namespace prov

namespace fac
{
uint32_t count(const clap_preset_discovery_factory_t *factory) { return 1; };
const clap_preset_discovery_provider_descriptor_t *
get_descriptor(const clap_preset_discovery_factory_t *factory, uint32_t index)
{
    static clap_preset_discovery_provider_descriptor desc = {
        CLAP_VERSION, prov::providerId, "Two Filters Preset Discovery", "BaconPaul"};
    return &desc;
}
const clap_preset_discovery_provider_t *create(const clap_preset_discovery_factory_t *factory,
                                               const clap_preset_discovery_indexer_t *indexer,
                                               const char *provider_id)
{
    if (strcmp(provider_id, prov::providerId) == 0)
    {
        // owned by the host until destroy
        auto pm = new prov::Provider{};
        pm->indexer = indexer;
        pm->provider = {
            get_descriptor(nullptr, 0), pm, prov::init, prov::destroy, prov::get_metadata,
            prov::get_extension};
        return &pm->provider;
    }
    return nullptr;
}
} // namespace fac

const clap_preset_discovery_factory_t *twoFiltersPresetDiscoveryFactory()
{
    static clap_preset_discovery_factory_t factory = {fac::count, fac::get_descriptor, fac::create};
    return &factory;
}
} // namespace baconpaul::twofilters
