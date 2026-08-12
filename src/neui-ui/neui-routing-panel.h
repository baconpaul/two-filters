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

#ifndef BACONPAUL_TWOFILTERS_NEUI_UI_NEUI_ROUTING_PANEL_H
#define BACONPAUL_TWOFILTERS_NEUI_UI_NEUI_ROUTING_PANEL_H

#include <sst/neuigui/components/NamedPanel.h>
#include <sst/neuigui/components/Knob.h>
#include <sst/neuigui/components/MultiSwitch.h>
#include <sst/neuigui/components/ToggleButton.h>
#include <sst/neuigui/components/JogUpDownButton.h>
#include <sst/neuigui/components/Label.h>
#include "neui-plugin-editor.h"

namespace baconpaul::twofilters::ui
{
struct NRoutingPanel : sngc::NamedPanelBase<NRoutingPanel>
{
    NRoutingPanel(npp::Parent p, NeuiPluginEditor &editor);
    void resized() override;

    NeuiPluginEditor &editor;

    std::unique_ptr<PatchDiscrete> routingModeD, fbPowerD, noisePowerD, retriggerModeD, oversampleD;
    std::unique_ptr<PatchContinuous> feedbackD, mixD, igD, ogD, noiseLevelD, filterBlendSerialD,
        filterBlendParallelD;

    sngc::Knob *feedbackK{nullptr}, *mixK{nullptr}, *igK{nullptr}, *ogK{nullptr},
        *noiseLevelK{nullptr}, *filterBlendSerialK{nullptr}, *filterBlendParallelK{nullptr};
    sngc::MultiSwitch *routingModeS{nullptr};
    sngc::JogUpDownButton *retriggerModeS{nullptr};
    sngc::Label *retriggerModeL{nullptr};
    sngc::ToggleButton *fbPowerT{nullptr}, *noisePowerT{nullptr}, *oversampleT{nullptr};

    void enableFB();

    void randomize();

    void beginEdit() {}
    void endEdit(int id) {}
};
} // namespace baconpaul::twofilters::ui
#endif // BACONPAUL_TWOFILTERS_NEUI_UI_NEUI_ROUTING_PANEL_H
