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

#ifndef BACONPAUL_TWOFILTERS_NEUI_UI_NEUI_STEPLFO_PANEL_H
#define BACONPAUL_TWOFILTERS_NEUI_UI_NEUI_STEPLFO_PANEL_H

#include <sst/neuigui/components/NamedPanel.h>
#include <sst/neuigui/components/Knob.h>
#include <sst/neuigui/components/JogUpDownButton.h>
#include <sst/neuigui/components/RuledLabel.h>
#include "neui-plugin-editor.h"

namespace baconpaul::twofilters::ui
{

struct NStepEditor;

struct NStepLFOPanel : sngc::NamedPanelBase<NStepLFOPanel>
{
    NStepLFOPanel(npp::Parent p, NeuiPluginEditor &editor, int instance);
    ~NStepLFOPanel();
    void resized() override;

    NeuiPluginEditor &editor;

    void beginEdit() {}
    void endEdit(int id) {}

    void setCurrentStep(int cs);
    void setCurrentPhase(float ph);
    void setCurrentLevel(float ph);

    int currentStep{-1};
    float currentPhase{0}, currentLevel{0};

    NStepEditor *stepEditor{nullptr};
    std::array<std::unique_ptr<PatchContinuous>, maxSteps> stepDs;
    sngc::JogUpDownButton *stepCount{nullptr};
    std::unique_ptr<PatchDiscrete> stepCountD;

    static constexpr int numRoutes{2 * 4 + 6};
    std::array<std::unique_ptr<PatchContinuous>, numRoutes> routeD;
    std::array<sngc::Knob *, numRoutes> routeK{};
    sngc::RuledLabel *toF1{nullptr}, *toF2{nullptr}, *toRt{nullptr};

    sngc::Knob *rate{nullptr}, *smooth{nullptr};
    std::unique_ptr<PatchContinuous> rateD, smoothD;

    void randomize();
    void randomizeSteps();
    void randomizeRoutes();
    void resetRoutes();
    void resetSteps();

    void onModelChanged();
    int instance;
};
} // namespace baconpaul::twofilters::ui
#endif // BACONPAUL_TWOFILTERS_NEUI_UI_NEUI_STEPLFO_PANEL_H
