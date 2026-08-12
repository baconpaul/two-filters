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

#ifndef BACONPAUL_TWOFILTERS_NEUI_UI_NEUI_FILTER_PANEL_H
#define BACONPAUL_TWOFILTERS_NEUI_UI_NEUI_FILTER_PANEL_H

#include <sst/neuigui/components/NamedPanel.h>
#include <sst/neuigui/components/Knob.h>
#include "neui-plugin-editor.h"

namespace baconpaul::twofilters::ui
{
/*
 * The first slice of src/ui/filter-panel.cpp: the four knobs, bound to
 * patchMain. The curve display, model/config menus and power toggle follow
 * as their widgets land.
 */
struct NFilterPanel : sngc::NamedPanelBase<NFilterPanel>
{
    NFilterPanel(npp::Parent p, NeuiPluginEditor &editor, int instance);
    ~NFilterPanel();

    void resized() override;

    NeuiPluginEditor &editor;

    void beginEdit() {}
    void endEdit(int) {}

    std::unique_ptr<PatchContinuous> cutoffD, resonanceD, morphD, panD;
    sngc::Knob *cutoffK{nullptr}, *resonanceK{nullptr}, *morphK{nullptr}, *panK{nullptr};

    int instance;
};
} // namespace baconpaul::twofilters::ui
#endif // BACONPAUL_TWOFILTERS_NEUI_UI_NEUI_FILTER_PANEL_H
