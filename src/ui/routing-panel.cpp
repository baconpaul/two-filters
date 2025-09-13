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

#include "routing-panel.h"

namespace baconpaul::twofilters::ui
{

RoutingPanel::RoutingPanel(PluginEditor &ed)
    : sst::jucegui::components::NamedPanel("Main"), editor(ed)
{
    auto rn = editor.patchCopy.routingNode;
    createComponent(editor, *this, rn.routingMode, routingModeS, routingModeD);
    addAndMakeVisible(*routingModeS);
    routingModeD->onGuiSetValue = [this]() { editor.resetEnablement(); };
    editor.componentRefreshByID[rn.routingMode.meta.id] = [this]() { editor.resetEnablement(); };

    retriggerModeL = std::make_unique<sst::jucegui::components::Label>();
    retriggerModeL->setText("Retrigger");
    addAndMakeVisible(*retriggerModeL);

    createComponent(editor, *this, rn.retriggerMode, retriggerModeS, retriggerModeD);
    addAndMakeVisible(*retriggerModeS);

    createComponent(editor, *this, rn.feedback, feedbackK, feedbackD);
    addAndMakeVisible(*feedbackK);

    createComponent(editor, *this, rn.inputGain, igK, igD);
    addAndMakeVisible(*igK);

    createComponent(editor, *this, rn.outputGain, ogK, ogD);
    addAndMakeVisible(*ogK);

    createComponent(editor, *this, rn.mix, mixK, mixD);
    addAndMakeVisible(*mixK);

    createComponent(editor, *this, rn.noiseLevel, noiseLevelK, noiseLevelD);
    addAndMakeVisible(*noiseLevelK);
    noiseLevelD->labelOverride = "Noise";

    createComponent(editor, *this, rn.filterBlendSerial, filterBlendSerialK, filterBlendSerialD);
    addAndMakeVisible(*filterBlendSerialK);
    filterBlendSerialD->labelOverride = "Blend";

    createComponent(editor, *this, rn.filterBlendParallel, filterBlendParallelK,
                    filterBlendParallelD);
    addChildComponent(*filterBlendParallelK);
    filterBlendParallelD->labelOverride = "Blend";

    createComponent(editor, *this, rn.feedbackPower, fbPowerT, fbPowerD);
    fbPowerT->setDrawMode(sst::jucegui::components::ToggleButton::DrawMode::GLYPH);
    fbPowerT->setGlyph(sst::jucegui::components::GlyphPainter::POWER);
    addAndMakeVisible(*fbPowerT);
    fbPowerD->onGuiSetValue = [this]() { editor.resetEnablement(); };
    editor.componentRefreshByID[rn.feedbackPower.meta.id] = [this]() { editor.resetEnablement(); };

    createComponent(editor, *this, rn.noisePower, noisePowerT, noisePowerD);
    noisePowerT->setDrawMode(sst::jucegui::components::ToggleButton::DrawMode::GLYPH);
    noisePowerT->setGlyph(sst::jucegui::components::GlyphPainter::POWER);
    addAndMakeVisible(*noisePowerT);
    noisePowerD->onGuiSetValue = [this]() { editor.resetEnablement(); };
    editor.componentRefreshByID[rn.noisePower.meta.id] = [this]() { editor.resetEnablement(); };

    createComponent(editor, *this, rn.oversample, oversampleT, oversampleD);
    oversampleT->setDrawMode(sst::jucegui::components::ToggleButton::DrawMode::LABELED);
    oversampleT->setLabel("Oversample");
    oversampleT->setEnabled(false);
    addAndMakeVisible(*oversampleT);

    enableFB();
}
void RoutingPanel::resized()
{
    auto ca = getContentArea().reduced(2, 0);

    routingModeS->setBounds(ca.withHeight(70));
    ca = ca.withTrimmedTop(73);

    oversampleT->setBounds(ca.withHeight(20));

    retriggerModeL->setBounds(ca.withHeight(18).translated(0, 22));
    retriggerModeS->setBounds(ca.withHeight(20).translated(0, 42));

    ca = ca.withTrimmedTop(76);
    auto kr = ca.withHeight(75).reduced(15, 0);

    auto kH = 77;
    igK->setBounds(kr.translated(0, 0 * kH));
    ogK->setBounds(kr.translated(0, 1 * kH));
    filterBlendSerialK->setBounds(kr.translated(0, 2 * kH));
    filterBlendParallelK->setBounds(kr.translated(0, 2 * kH));
    mixK->setBounds(kr.translated(0, 3 * kH));
    feedbackK->setBounds(kr.translated(0, 4 * kH));
    noiseLevelK->setBounds(kr.translated(0, 5 * kH));

    auto tr = feedbackK->getBounds().withHeight(15).translated(-10, -4);
    fbPowerT->setBounds(tr);
    auto nr = noiseLevelK->getBounds().withWidth(15).withHeight(15).translated(-10, -4);
    noisePowerT->setBounds(nr);
}

void RoutingPanel::enableFB()
{
    feedbackK->setEnabled(editor.patchCopy.routingNode.feedbackPower.value > 0.5f);
    feedbackK->repaint();

    noiseLevelK->setEnabled(editor.patchCopy.routingNode.noisePower.value > 0.5f);
    noiseLevelK->repaint();

    auto m = (int)editor.patchCopy.routingNode.routingMode;
    filterBlendSerialK->setVisible(m == 0);
    filterBlendParallelK->setVisible(m != 0);
}

void RoutingPanel::randomize() { SQLOG("RoutingPanel::randomize"); }
} // namespace baconpaul::twofilters::ui