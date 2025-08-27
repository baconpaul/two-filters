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

#ifndef BACONPAUL_TWOFILTERS_PRESETS_PRESET_MANAGER_H
#define BACONPAUL_TWOFILTERS_PRESETS_PRESET_MANAGER_H

#include <clap/clap.h>
#include "filesystem/import.h"
#include "sst/jucegui/data/Discrete.h"
#include "engine/patch.h"
#include "engine/engine.h"
#include <map>
#include <unordered_map>
#include <functional>
#include <set>
#include <string>

namespace baconpaul::twofilters::presets
{
struct PresetManager
{
    fs::path userPath;
    fs::path userPatchesPath;
    const clap_host_t *clapHost{nullptr};

    // Call with a null host to be read-only
    PresetManager(const clap_host_t *host);
    ~PresetManager();

    void rescanUserPresets();

    void loadInit(Patch &p, Engine::mainToAudioQueue_T &);
    void loadUserPresetDirect(Patch &, Engine::mainToAudioQueue_T &, const fs::path &p);
    void loadFactoryPreset(Patch &, Engine::mainToAudioQueue_T &, const std::string &cat,
                           const std::string &pat);

#if USE_WCHAR_PRESET
    void saveUserPresetDirect(Patch &, const wchar_t *utf8path);
#else
    void saveUserPresetDirect(Patch &, const fs::path &p);
#endif

    std::function<void(const std::string &)> onPresetLoaded{nullptr};

    static constexpr const char *factoryPath{"resources/factory_patches"};
    std::map<std::string, std::vector<std::string>> factoryPatchNames;
    std::vector<std::pair<std::string, std::string>> factoryPatchVector;
    std::vector<fs::path> userPatches;

    const clap_host_params_t *clapHostParams{nullptr};
    void sendEntirePatchToAudio(Patch &, Engine::mainToAudioQueue_T &, const std::string &name);
    static void sendEntirePatchToAudio(Patch &, Engine::mainToAudioQueue_T &,
                                       const std::string &name, const clap_host_t *,
                                       const clap_host_params_t *p = nullptr);
};
} // namespace baconpaul::twofilters::presets
#endif // PRESET_MANAGER_H
