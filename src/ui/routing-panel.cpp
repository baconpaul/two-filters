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

RoutingPanel::RoutingPanel(PluginEditor &editor)
    : sst::jucegui::components::NamedPanel("Main"), editor(editor)
{
    auto rn = editor.patchCopy.routingNode;
    createComponent(editor, *this, rn.routingMode, routingModeS, routingModeD);
    addAndMakeVisible(*routingModeS);

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

    createComponent(editor, *this, rn.feedbackPower, fbPowerT, fbPowerD);
    fbPowerT->setDrawMode(sst::jucegui::components::ToggleButton::DrawMode::GLYPH);
    fbPowerT->setGlyph(sst::jucegui::components::GlyphPainter::POWER);
    addAndMakeVisible(*fbPowerT);
    fbPowerD->onGuiSetValue = [this]() { enableFB(); };
    editor.componentRefreshByID[rn.feedback.meta.id] = [this]() { enableFB(); };

    createComponent(editor, *this, rn.noisePower, noisePowerT, noisePowerD);
    noisePowerT->setDrawMode(sst::jucegui::components::ToggleButton::DrawMode::GLYPH);
    noisePowerT->setGlyph(sst::jucegui::components::GlyphPainter::POWER);
    addAndMakeVisible(*noisePowerT);
    noisePowerD->onGuiSetValue = [this]() { enableFB(); };
    editor.componentRefreshByID[rn.feedback.meta.id] = [this]() { enableFB(); };

    enableFB();
}
void RoutingPanel::resized()
{
    auto ca = getContentArea().reduced(2, 0);

    routingModeS->setBounds(ca.withHeight(100));
    ca = ca.withTrimmedTop(110);
    auto kr = ca.withHeight(75).reduced(15, 0);
    feedbackK->setBounds(kr);
    mixK->setBounds(kr.translated(0, 80));
    igK->setBounds(kr.translated(0, 2 * 80));
    ogK->setBounds(kr.translated(0, 3 * 80));
    noiseLevelK->setBounds(kr.translated(0, 4 * 80));
    auto tr = kr.withWidth(15).withHeight(15).translated(-10, -4);
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
}

} // namespace baconpaul::twofilters::ui