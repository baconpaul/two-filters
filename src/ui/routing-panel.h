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

#ifndef BACONPAUL_TWOFILTERS_UI_ROUTING_PANEL_H
#define BACONPAUL_TWOFILTERS_UI_ROUTING_PANEL_H

#include "sst/jucegui/components/NamedPanel.h"
#include "patch-data-bindings.h"
#include "plugin-editor.h"
#include "sst/jucegui/components/MultiSwitch.h"
#include "sst/jucegui/components/ToggleButton.h"
#include "sst/jucegui/components/JogUpDownButton.h"
#include "sst/jucegui/components/Label.h"

namespace baconpaul::twofilters::ui
{
struct RoutingPanel : sst::jucegui::components::NamedPanel
{
    RoutingPanel(PluginEditor &editor);
    void resized() override;

    PluginEditor &editor;

    std::unique_ptr<PatchDiscrete> routingModeD, fbPowerD, noisePowerD, retriggerModeD, oversampleD;
    std::unique_ptr<PatchContinuous> feedbackD, mixD, igD, ogD, noiseLevelD, filterBlendSerialD,
        filterBlendParallelD;

    std::unique_ptr<sst::jucegui::components::Knob> feedbackK, mixK, igK, ogK, noiseLevelK,
        filterBlendSerialK, filterBlendParallelK;
    std::unique_ptr<sst::jucegui::components::MultiSwitch> routingModeS;
    std::unique_ptr<sst::jucegui::components::JogUpDownButton> retriggerModeS;
    std::unique_ptr<sst::jucegui::components::Label> retriggerModeL;
    std::unique_ptr<sst::jucegui::components::ToggleButton> fbPowerT, noisePowerT, oversampleT;

    void enableFB();

    void randomize();

    void beginEdit() {}
    void endEdit(int id) {}
};
} // namespace baconpaul::twofilters::ui
#endif // ROUTING_PANEL_H
