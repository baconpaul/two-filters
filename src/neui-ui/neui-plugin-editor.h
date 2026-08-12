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

#ifndef BACONPAUL_TWOFILTERS_NEUI_UI_NEUI_PLUGIN_EDITOR_H
#define BACONPAUL_TWOFILTERS_NEUI_UI_NEUI_PLUGIN_EDITOR_H

#include <array>
#include <functional>
#include <memory>
#include <unordered_map>

#include <neuiplusplus/neuiplusplus.h>

#include <sst/neuigui/components/WindowPanel.h>
#include <sst/neuigui/components/NamedPanel.h>
#include <sst/neuigui/components/Knob.h>
#include <sst/neuigui/components/Label.h>
#include <sst/neuigui/components/VUMeter.h>

#include "engine/engine.h"
#include "engine/patch.h"

/*
 * The neui rebuild of src/ui/plugin-editor.h, growing panel by panel as the
 * widget set lands in sst-neuigui. The editor is the root child of the
 * embedded PLUGWINDOW; NeuiEditor (neui-editor.h) owns the session, frame
 * and idle timer and calls idle() here.
 */
namespace baconpaul::twofilters::ui
{
namespace sngc = sst::neuigui::components;
namespace npp = neuiplusplus;

struct PatchContinuous;
struct PatchDiscrete;
struct NFilterPanel;

struct NeuiPluginEditor : sngc::WindowPanelBase<NeuiPluginEditor>
{
    Patch &patchMainRef;

    Engine::audioToMainQueue_t &audioToMain;
    Engine::mainToAudioQueue_T &mainToAudio;
    std::atomic<bool> &editorActive;
    std::atomic<uint32_t> &uiForceRebuild;
    uint32_t lastForceRebuild{0};
    const clap_host_t *clapHost{nullptr};

    NeuiPluginEditor(npp::Parent p, Patch &patchMain, Engine::audioToMainQueue_t &atou,
                     Engine::mainToAudioQueue_T &utoa, std::atomic<bool> &editorActive,
                     std::atomic<uint32_t> &uiForceRebuild, const clap_host_t *ch);
    virtual ~NeuiPluginEditor();

    void resized() override;
    void paint(npp::Canvas &g) override;

    // Called at ~60Hz off the neui session timer NeuiEditor registers.
    void idle();

    std::array<NFilterPanel *, numFilters> filterPanel{};
    sngc::VUMeter *vuMeter{nullptr};

    void markPatchDirty();
    void requestParamsFlush();

    // Param context menus arrive with the popup-menu port; a stub keeps the
    // binding layer identical to the juce one.
    void popupMenuForContinuous(void *) {}

    // Rebuild every widget from patchMainRef after an out-of-band load.
    void rebuildFromPatchMain();

    std::unordered_map<uint32_t, npp::ComponentCore *> componentByID;
    std::unordered_map<uint32_t, std::function<void()>> componentRepaintByID;

    float sampleRate{0};

    const clap_host_params_t *clapParamsExtension{nullptr};
};
} // namespace baconpaul::twofilters::ui
#endif // BACONPAUL_TWOFILTERS_NEUI_UI_NEUI_PLUGIN_EDITOR_H
