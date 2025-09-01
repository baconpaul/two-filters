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

#ifndef BACONPAUL_TWOFILTERS_UI_STEPLFO_PANEL_H
#define BACONPAUL_TWOFILTERS_UI_STEPLFO_PANEL_H

#include "sst/jucegui/components/NamedPanel.h"
#include "sst/jucegui/components/Knob.h"
#include "sst/jucegui/components/JogUpDownButton.h"
#include "sst/jucegui/components/MenuButton.h"
#include "sst/jucegui/components/RuledLabel.h"
#include "patch-data-bindings.h"
#include "plugin-editor.h"

namespace baconpaul::twofilters::ui
{

struct StepEditor;

struct StepLFOPanel : sst::jucegui::components::NamedPanel
{
    StepLFOPanel(PluginEditor &editor, int instance);
    ~StepLFOPanel();
    void resized() override;

    PluginEditor &editor;

    void beginEdit() {}
    void endEdit() {}

    void onIdle();

    void setCurrentStep(int cs);
    void setCurrentPhase(float ph);
    void setCurrentLevel(float ph);

    int currentStep{-1};
    float currentPhase{0}, currentLevel{0};

    std::unique_ptr<StepEditor> stepEditor;
    std::array<std::unique_ptr<PatchContinuous>, maxSteps> stepDs;
    std::unique_ptr<sst::jucegui::components::JogUpDownButton> stepCount;
    std::unique_ptr<PatchDiscrete> stepCountD;

    static constexpr int numRoutes{8};
    std::array<std::unique_ptr<PatchContinuous>, numRoutes> routeD;
    std::array<std::unique_ptr<sst::jucegui::components::Knob>, numRoutes> routeK;
    std::unique_ptr<sst::jucegui::components::RuledLabel> toF1, toF2, toRt;

    std::unique_ptr<sst::jucegui::components::Knob> rate, smooth;
    std::unique_ptr<PatchContinuous> rateD, smoothD;

    void onModelChanged();
    int instance;
};
} // namespace baconpaul::twofilters::ui
#endif // STEPLFO_PANEL_H
