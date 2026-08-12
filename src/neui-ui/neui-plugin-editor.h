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
#include <neuiplusplus/components/PopupMenu.h>

#include <sst/neuigui/components/WindowPanel.h>
#include <sst/neuigui/components/NamedPanel.h>
#include <sst/neuigui/components/Knob.h>
#include <sst/neuigui/components/Label.h>
#include <sst/neuigui/components/VUMeter.h>
#include <sst/neuigui/components/JogUpDownButton.h>

#include <sst/basic-blocks/dsp/RNG.h>

#include "engine/engine.h"
#include "engine/patch.h"
#include "presets/preset-manager.h"
#include "ui/ui-defaults.h"

/*
 * The neui rebuild of src/ui/plugin-editor.h, growing panel by panel as the
 * widget set lands in sst-neuigui. The editor is the root child of the
 * embedded PLUGWINDOW; NeuiEditor (neui-editor.h) owns the session, frame,
 * idle timer and the popup menu controller and calls idle() here.
 */
namespace baconpaul::twofilters::ui
{
namespace sngc = sst::neuigui::components;
namespace npp = neuiplusplus;

struct PatchContinuous;
struct PatchDiscrete;
struct PresetDataBinding;
struct NFilterPanel;
struct NRoutingPanel;
struct NStepLFOPanel;

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
    std::array<NStepLFOPanel *, numStepLFOs> stepLFOPanel{};
    NRoutingPanel *routingPanel{nullptr};
    sngc::VUMeter *vuMeter{nullptr};
    sngc::JogUpDownButton *presetButton{nullptr};

    std::unique_ptr<presets::PresetManager> presetManager;
    std::unique_ptr<PresetDataBinding> presetDataBinding;
    std::unique_ptr<defaultsProvider_t> defaultsProvider;

    sst::basic_blocks::dsp::RNG rng;

    enum ConfigDisplayMode
    {
        SINGLE_LIST = 0,
        FOUR_ALL = 1,
        FOUR_HIDE = 2
    };

    enum GraphicsMode
    {
        FULL = 0,
        REDUCES = 1,
        MINIMAL = 2
    } cpuGraphicsMode{FULL};

    void setSkinFromDefaults();
    void doLoadPatch();
    void doSavePatch();
    void setPatchNameDisplay();

    /*
     * Menus. The PopupMenu controller lives on the frame (NeuiEditor::Impl
     * owns it); at is in editor coordinates, which are frame client
     * coordinates since the editor fills the frame at origin.
     */
    npp::PopupMenu *menu{nullptr};
    void showMenu(std::vector<npp::MenuItem> items, npp::Point at);
    std::vector<npp::MenuItem> configDisplayMenuItems();
    void popupMenuForContinuous(PatchContinuous *c, npp::Point at);

    void showPresetPopup();
    void postPatchChange(const std::string &displayName);
    void resetToDefault();
    void markPatchDirty();
    void setPatchNameTo(const std::string &);
    void pushFilterSetup(int instance);
    void swapFilters(bool alsoSwapMod);
    void resetEnablement();

    void requestParamsFlush();

    // Rebuild every widget from patchMainRef after an out-of-band load.
    void rebuildFromPatchMain();

    ConfigDisplayMode cpuConfigMode() const;

    std::unordered_map<uint32_t, npp::ComponentCore *> componentByID;
    std::unordered_map<uint32_t, std::function<void()>> componentRepaintByID;
    std::unordered_map<uint32_t, std::function<void()>> componentRefreshByID;

    float sampleRate{0};

    const clap_host_params_t *clapParamsExtension{nullptr};
};
} // namespace baconpaul::twofilters::ui
#endif // BACONPAUL_TWOFILTERS_NEUI_UI_NEUI_PLUGIN_EDITOR_H
