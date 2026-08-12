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
#include <sst/neuigui/components/MenuButton.h>
#include "neui-plugin-editor.h"

namespace baconpaul::twofilters::ui
{
struct NFilterCurve;

struct NFilterPanel : sngc::NamedPanelBase<NFilterPanel>
{
    NFilterPanel(npp::Parent p, NeuiPluginEditor &editor, int instance);
    ~NFilterPanel();

    void resized() override;

    NeuiPluginEditor &editor;

    void onModelChanged();

    void beginEdit() {}
    void endEdit(int id);

    void jogModel(int dir);
    void jogConfig(int dir);

    void onIdle();

    void randomize();
    void resetFilter();

    NFilterCurve *curve{nullptr};

    std::unique_ptr<PatchContinuous> cutoffD, resonanceD, morphD, panD;
    std::unique_ptr<PatchDiscrete> activeD;

    sngc::Knob *cutoffK{nullptr}, *resonanceK{nullptr}, *morphK{nullptr}, *panK{nullptr};

    sngc::MenuButton *modelMenu{nullptr}, *configMenu{nullptr};
    sngc::MenuButton *pbMenu{nullptr}, *slpMenu{nullptr}, *drvMenu{nullptr}, *fsmMenu{nullptr};

    void showModelMenu();
    void showConfigMenu();
    void showConfigStructuredMenu(int component);

    template <typename E>
    void addConfigStructuredMenu(std::vector<npp::MenuItem> &, const std::string &);

    NeuiPluginEditor::ConfigDisplayMode displayMode{NeuiPluginEditor::SINGLE_LIST};
    void updateFourHideMenuVisibility();

    int instance;
};
} // namespace baconpaul::twofilters::ui
#endif // BACONPAUL_TWOFILTERS_NEUI_UI_NEUI_FILTER_PANEL_H
