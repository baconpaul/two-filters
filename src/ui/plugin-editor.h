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

#ifndef BACONPAUL_TWOFILTERS_UI_PLUGIN_EDITOR_H
#define BACONPAUL_TWOFILTERS_UI_PLUGIN_EDITOR_H

#include <functional>
#include <utility>
#include <juce_gui_basics/juce_gui_basics.h>
#include "sst/jucegui/style/JUCELookAndFeelAdapter.h"

#include <sst/jucegui/components/NamedPanel.h>
#include <sst/jucegui/components/WindowPanel.h>
#include <sst/jucegui/components/Knob.h>
#include <sst/jucegui/components/ToolTip.h>
#include <sst/jucegui/components/ToggleButton.h>
#include <sst/jucegui/components/MenuButton.h>
#include <sst/jucegui/components/JogUpDownButton.h>
#include <sst/jucegui/components/VUMeter.h>
#include <sst/jucegui/accessibility/FocusDebugger.h>
#include <sst/jucegui/data/Continuous.h>

#include "engine/engine.h"
#include "engine/patch.h"
#include "presets/preset-manager.h"
#include "preset-data-binding.h"
#include "ui-defaults.h"

namespace jcmp = sst::jucegui::components;
namespace jdat = sst::jucegui::data;

namespace baconpaul::twofilters::ui
{

struct DebugPanel;
struct FilterPanel;
struct RoutingPanel;
struct StepLFOPanel;

struct PluginEditor : jcmp::WindowPanel
{
    Patch patchCopy;

    Engine::audioToUIQueue_t &audioToUI;
    Engine::mainToAudioQueue_T &mainToAudio;
    const clap_host_t *clapHost{nullptr};

    PluginEditor(Engine::audioToUIQueue_t &atou, Engine::mainToAudioQueue_T &utoa,
                 const clap_host_t *ch);
    virtual ~PluginEditor();

    std::unique_ptr<sst::jucegui::style::LookAndFeelManager> lnf;
    void onStyleChanged() override;

    void paint(juce::Graphics &g) override;
    void resized() override;

    void idle();
    std::unique_ptr<juce::Timer> idleTimer;

#if INCLUDE_DEBUG_PANEL
    std::unique_ptr<DebugPanel> debugPanel;
#endif

    std::array<std::unique_ptr<FilterPanel>, numFilters> filterPanel;
    std::array<std::unique_ptr<StepLFOPanel>, numStepLFOs> stepLFOPanel;
    std::unique_ptr<RoutingPanel> routingPanel;

    std::unique_ptr<presets::PresetManager> presetManager;
    std::unique_ptr<PresetDataBinding> presetDataBinding;
    std::unique_ptr<jcmp::JogUpDownButton> presetButton;
    void showPresetPopup();
    void doLoadPatch();
    void doSavePatch();
    void postPatchChange(const std::string &displayName);
    void resetToDefault();
    void setPatchNameDisplay();
    void setPatchNameTo(const std::string &);
    void pushFilterSetup(int instance);
    std::unique_ptr<juce::FileChooser> fileChooser;

    std::unique_ptr<defaultsProvder_t> defaultsProvider;

    void setSkinFromDefaults();

    std::unique_ptr<jcmp::ToolTip> toolTip;
    void showTooltipOn(juce::Component *c);
    void updateTooltip(jdat::Continuous *c);
    void updateTooltip(jdat::Discrete *d);
    void hideTooltip();

    void popupMenuForContinuous(jcmp::ContinuousParamEditor *e);

    void hideAllSubPanels();
    std::unordered_map<uint32_t, juce::Component::SafePointer<juce::Component>> componentByID;
    std::unordered_map<uint32_t, std::function<void()>> componentRefreshByID;

    void setAndSendParamValue(uint32_t id, float value, bool notifyAudio = true,
                              bool includeBeginEnd = true);
    void setAndSendParamValue(const Param &p, float value, bool notifyAudio = true,
                              bool includeBeginEnd = true)
    {
        setAndSendParamValue(p.meta.id, value, notifyAudio, includeBeginEnd);
    }

    bool keyPressed(const juce::KeyPress &key) override;

    void setZoomFactor(float zf);
    float zoomFactor{1.0f};
    std::function<void(float)> onZoomChanged{nullptr};
    bool toggleDebug();

    std::unique_ptr<jcmp::VUMeter> vuMeter;

    void visibilityChanged() override;
    void parentHierarchyChanged() override;
    std::unique_ptr<sst::jucegui::accessibility::FocusDebugger> focusDebugger;

    float sampleRate{0};

    void requestParamsFlush();
    const clap_host_params_t *clapParamsExtension{nullptr};

    // the name tells you about the intent. It just makes startup faster
    void sneakyStartupGrabFrom(Patch &other);
};

struct HasEditor
{
    PluginEditor &editor;
    HasEditor(PluginEditor &e) : editor(e) {}
};
} // namespace baconpaul::twofilters::ui
#endif
