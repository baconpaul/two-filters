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

#include "steplfo-panel.h"
#include "sst/jucegui/components/VSlider.h"
#include "sst/jucegui/components/BaseStyles.h"

namespace baconpaul::twofilters::ui
{

struct StepEditor : juce::Component
{
    StepEditor(StepLFOPanel &p) : panel(p), lfo(tp)
    {
        tp.init();
        for (int i = 0; i < maxSteps; i++)
            panel.editor.componentRefreshByID[panel.stepDs[i]->pid] = [this]() { repaint(); };
    }

    void paint(juce::Graphics &g) override
    {
        float bw = getWidth() * 1.0 / maxSteps;
        namespace bst = sst::jucegui::components::base_styles;
        auto gCol =
            panel.style()->getColour(bst::ValueGutter::styleClass, bst::ValueGutter::gutter);
        auto oCol = panel.style()->getColour(bst::Outlined::styleClass, bst::Outlined::outline);
        auto vCol =
            panel.style()->getColour(bst::ValueBearing::styleClass, bst::ValueBearing::value);
        auto hCol =
            panel.style()->getColour(bst::ValueBearing::styleClass, bst::ValueBearing::value_hover);
        auto lCol =
            panel.style()->getColour(bst::BaseLabel::styleClass, bst::BaseLabel::labelcolor);
        auto lFt = panel.style()->getFont(bst::BaseLabel::styleClass, bst::BaseLabel::labelfont);
        hCol = hCol.withAlpha(0.5f);

        auto &sn = panel.editor.patchCopy.stepLfoNodes[panel.instance];
        auto fullArea = getLocalBounds().withWidth(sn.stepCount * bw);
        auto restArea = getLocalBounds().withTrimmedLeft(sn.stepCount * bw);
        g.setColour(gCol);
        g.fillRect(fullArea);
        g.setColour(gCol.withAlpha(0.3f));
        g.fillRect(restArea);

        int mg{1};
        for (int i = 0; i < maxSteps; i++)
        {
            float val = panel.stepDs[i]->getValue();

            g.setColour(vCol);
            if (i >= (int)sn.stepCount)
                g.setColour(vCol.withAlpha(0.3f));

            if (val < 0)
            {
                g.fillRect(i * bw + mg, getHeight() / 2., bw - 2 * mg, -getHeight() / 2 * val);
            }
            else
            {
                // 1 -> y = 0, 0 -> y = h/2. so h/2 * (1-y)
                g.fillRect(i * bw + mg, getHeight() / 2 * (1 - val), bw - 2 * mg,
                           getHeight() / 2 * val);
            }
        }
        g.setColour(oCol);
        g.drawRect(0, 0, getWidth(), getHeight(), 1);
        for (int i = 1; i <= (int)sn.stepCount; i++)
            g.drawVerticalLine(i * bw, 0, getHeight());
        g.drawHorizontalLine(getHeight() / 2, 0, getWidth());

        if (paintStep >= 0)
        {
            float val = panel.stepDs[paintStep]->getValue();
            g.setFont(lFt.withHeight(8));
            g.setColour(lCol);
            auto vf = fmt::format("{:.2f}", val);
            if (val > 0)
            {
                g.drawText(vf, paintStep * bw, getHeight() / 2 + 2, bw, 14,
                           juce::Justification::centredTop, false);
            }
            else
            {
                g.drawText(vf, paintStep * bw, getHeight() / 2 - 16, bw, 14,
                           juce::Justification::centredBottom, false);
            }
        }
        if (panel.editor.cpuGraphicsMode != PluginEditor::MINIMAL)
        {
            auto cs = panel.currentStep;
            if (cs >= 0 && cs < maxSteps)
            {
                g.setColour(hCol);
                g.drawRect(cs * bw, 0., bw, getHeight() * 1., 1.);
            }
        }

        if (panel.editor.cpuGraphicsMode == PluginEditor::FULL)
        {
            auto cs = panel.currentStep;
            auto xC = (cs + panel.currentPhase) * bw;
            auto yC = (1 - panel.currentLevel) * getHeight() / 2;
            g.setColour(lCol);
            g.fillEllipse(xC - 2, yC - 2, 5., 5.);
        }
        rebuildLfoPath();
        if (!lfoPath.isEmpty())
        {
            auto bolc =
                panel.style()->getColour(bst::BaseLabel::styleClass, bst::BaseLabel::labelcolor);

            g.setColour(bolc);
            if (!lfoPath.isEmpty())
                g.strokePath(lfoPath, juce::PathStrokeType(1));
        }
    }

    juce::Path lfoPath;
    Engine::stepLfo_t lfo;
    Engine::stepLfo_t::Storage lfoStorage;
    sst::basic_blocks::dsp::RNG rng;
    sst::basic_blocks::tables::EqualTuningProvider tp;
    sst::basic_blocks::modulators::Transport transport;
    bool pathValid{false};
    void invalidatePath() { pathValid = false; }
    void rebuildLfoPath()
    {
        if (pathValid)
            return;

        transport.tempo = 120;
        auto rate = 7;
        auto sr = 48000;
        auto steps = panel.editor.patchCopy.stepLfoNodes[panel.instance].stepCount;

        rng.reseed(8675309);
        lfo.setSampleRate(sr, 1.0 / sr);
        Engine::updateLfoStorageFromTo(panel.editor.patchCopy, panel.instance, lfoStorage);
        lfo.assign(&lfoStorage, rate, &transport, rng, true);

        auto stepTime = 1.0 / (1 << 7);
        auto stepSamples = sr * stepTime;
        auto stepBlocks = stepSamples / blockSize;

        lfoPath.clear();

        auto tx = [=, this](auto x) { return x / (stepBlocks * 16) * getWidth(); };
        auto ty = [=, this](auto y) { return (1 - (y + 1) / 2.) * getHeight(); };
        for (int i = 0; i < steps * stepBlocks; ++i)
        {
            lfo.process(rate, 0, true, false, blockSize);
            if (i == 0)
            {
                lfoPath.startNewSubPath(tx(i), ty(lfo.output));
            }
            else
            {
                lfoPath.lineTo(tx(i), ty(lfo.output));
            }
        }

        pathValid = true;
    }

    int lastEditedStep{-1};
    int paintStep{-1};
    void adjustValue(const juce::Point<float> &e, bool endAlways)
    {
        auto x = std::clamp(e.x, 0.f, 1.f * getWidth());
        auto y = std::clamp(e.y, 0.f, 1.f * getHeight());
        auto bw = getWidth() * 1.0 / maxSteps;
        auto step = std::clamp(int(x / bw), 0, (int)maxSteps - 1);
        auto val = std::clamp(1 - y / getHeight(), 0.f, 1.f) * 2 - 1;

        if (step != lastEditedStep)
        {
            if (lastEditedStep >= 0)
            {
                panel.editor.mainToAudio.push(
                    {Engine::MainToAudioMsg::Action::END_EDIT, panel.stepDs[lastEditedStep]->pid});
            }
            panel.editor.mainToAudio.push(
                {Engine::MainToAudioMsg::Action::BEGIN_EDIT, panel.stepDs[step]->pid});
            lastEditedStep = step;
        }

        panel.stepDs[step]->setValueFromGUI(val);
        paintStep = step;
        if (endAlways)
        {
            panel.editor.mainToAudio.push(
                {Engine::MainToAudioMsg::Action::END_EDIT, panel.stepDs[step]->pid});
            paintStep = -1;
        }
        repaint();
    }

    void mouseDown(const juce::MouseEvent &e) override
    {
        lastEditedStep = -1;
        adjustValue(e.position, false);
    }
    void mouseDrag(const juce::MouseEvent &e) override { adjustValue(e.position, false); }
    void mouseUp(const juce::MouseEvent &e) override { adjustValue(e.position, true); }
    StepLFOPanel &panel;
};

StepLFOPanel::StepLFOPanel(PluginEditor &editor, int instance)
    : editor(editor), instance(instance),
      sst::jucegui::components::NamedPanel("StepLFO " + std::to_string(instance + 1))
{
    auto &sn = editor.patchCopy.stepLfoNodes[instance];
    for (int i = 0; i < maxSteps; i++)
    {
        stepDs[i] = std::make_unique<PatchContinuous>(editor, sn.steps[i].meta.id);
        stepDs[i]->onGuiSetValue = [this]()
        {
            stepEditor->invalidatePath();
            stepEditor->repaint();
        };
    }
    stepEditor = std::make_unique<StepEditor>(*this);
    addAndMakeVisible(*stepEditor);

    createComponent(editor, *this, sn.stepCount, stepCount, stepCountD);
    addAndMakeVisible(*stepCount);
    stepCountD->onGuiSetValue = [this]()
    {
        stepEditor->invalidatePath();
        stepEditor->repaint();
    };

    int idx{0};
    createComponent(editor, *this, sn.toCO[0], routeK[idx], routeD[idx]);
    routeD[idx]->labelOverride = "Cutoff";
    idx++;
    createComponent(editor, *this, sn.toRes[0], routeK[idx], routeD[idx]);
    routeD[idx]->labelOverride = "Res";
    idx++;
    createComponent(editor, *this, sn.toMorph[0], routeK[idx], routeD[idx]);
    routeD[idx]->labelOverride = "Morph";
    idx++;
    createComponent(editor, *this, sn.toPan[0], routeK[idx], routeD[idx]);
    routeD[idx]->labelOverride = "Pan";
    routeK[idx]->setEnabled(false);
    idx++;

    createComponent(editor, *this, sn.toCO[1], routeK[idx], routeD[idx]);
    routeD[idx]->labelOverride = "Cutoff";
    idx++;
    createComponent(editor, *this, sn.toRes[1], routeK[idx], routeD[idx]);
    routeD[idx]->labelOverride = "Res";
    idx++;
    createComponent(editor, *this, sn.toMorph[1], routeK[idx], routeD[idx]);
    routeD[idx]->labelOverride = "Morph";
    idx++;
    createComponent(editor, *this, sn.toPan[1], routeK[idx], routeD[idx]);
    routeD[idx]->labelOverride = "Pan";
    routeK[idx]->setEnabled(false);
    idx++;

    createComponent(editor, *this, sn.toPreG, routeK[idx], routeD[idx]);
    routeD[idx]->labelOverride = "Pre";
    idx++;
    createComponent(editor, *this, sn.toPostG, routeK[idx], routeD[idx]);
    routeD[idx]->labelOverride = "Post";
    idx++;
    createComponent(editor, *this, sn.toMix, routeK[idx], routeD[idx]);
    routeD[idx]->labelOverride = "Mix";
    idx++;

    createComponent(editor, *this, sn.toFB, routeK[idx], routeD[idx]);
    routeD[idx]->labelOverride = "F/Back";
    idx++;
    createComponent(editor, *this, sn.toNoise, routeK[idx], routeD[idx]);
    routeD[idx]->labelOverride = "Noise";
    idx++;
    createComponent(editor, *this, sn.toFiltBlend, routeK[idx], routeD[idx]);
    routeD[idx]->labelOverride = "Blend";
    idx++;

    for (int i = 0; i < numRoutes; i++)
    {
        addAndMakeVisible(*routeK[i]);
    }

    toF1 = std::make_unique<sst::jucegui::components::RuledLabel>();
    toF1->setText("To Filter 1");
    addAndMakeVisible(*toF1);
    toF2 = std::make_unique<sst::jucegui::components::RuledLabel>();
    toF2->setText("To Filter 2");
    addAndMakeVisible(*toF2);
    toRt = std::make_unique<sst::jucegui::components::RuledLabel>();
    toRt->setText("To Main");
    addAndMakeVisible(*toRt);

    createComponent(editor, *this, sn.rate, rate, rateD);
    rateD->tempoSynced = true;
    addAndMakeVisible(*rate);
    createComponent(editor, *this, sn.smooth, smooth, smoothD);
    smoothD->onGuiSetValue = [this]() { stepEditor->invalidatePath(); };
    addAndMakeVisible(*smooth);
}
StepLFOPanel::~StepLFOPanel() = default;
void StepLFOPanel::resized()
{
    auto rPad{80}, bPad{160};
    auto q = getContentArea().withTrimmedRight(rPad).withTrimmedBottom(bPad);
    auto rA = getContentArea().withWidth(rPad).translated(q.getWidth(), 0).withTrimmedBottom(bPad);
    stepCount->setBounds(rA.withHeight(20));
    rA = rA.withTrimmedTop(30);
    auto ka = rA.withHeight(66).reduced((rA.getWidth() - 49) / 2, 0);
    rate->setBounds(ka);
    ka = ka.translated(0, 68);
    smooth->setBounds(ka);

    stepEditor->setBounds(q);

    auto bA = getContentArea().withTrimmedTop(getContentArea().getHeight() - bPad);
    auto kw = 45;
    auto kmarg = 10;

    auto lA = bA.withHeight(25).reduced(0, 2);
    toF1->setBounds(lA.withWidth(2 * kw + 3 * kmarg).reduced(4, 0));
    toF2->setBounds(
        lA.withWidth(2 * kw + 3 * kmarg).translated(2 * kw + 3 * kmarg, 0).reduced(4, 0));
    toRt->setBounds(
        lA.withWidth(3 * kw + 4 * kmarg).translated(4 * kw + 6 * kmarg, 0).reduced(4, 0));

    auto kA = bA.withWidth(kw).withTrimmedTop(25).withTrimmedBottom(2);
    auto kT = kA.withHeight(kA.getHeight() / 2);
    auto kB = kA.withTrimmedTop(kA.getHeight() / 2);

    // we want the filters as
    // 0 1   4 5
    // 2 3   6 6
    auto placeTop = [&, this](int idx)
    {
        routeK[idx]->setBounds(kT);
        kT = kT.translated(kw + kmarg, 0);
    };
    auto placeBot = [&, this](int idx)
    {
        routeK[idx]->setBounds(kB);
        kB = kB.translated(kw + kmarg, 0);
    };
    for (auto F = 0; F < numFilters; ++F)
    {
        kT = kT.translated(kmarg, 0);
        kB = kB.translated(kmarg, 0);
        auto i0 = F * 4;
        placeTop(i0);
        placeTop(i0 + 1);
        placeBot(i0 + 2);
        placeBot(i0 + 3);
    }

    kT = kT.translated(kmarg, 0);
    kB = kB.translated(kmarg, 0);
    placeTop(8);
    placeTop(9);
    placeTop(13);
    placeBot(10);
    placeBot(11);
    placeBot(12);
}

void StepLFOPanel::onModelChanged()
{
    for (int i = 0; i < numFilters; ++i)
    {
        auto &fn = editor.patchCopy.filterNodes[i];
        auto xtra = sst::filtersplusplus::Filter::coefficientsExtraCount(fn.model, fn.config);
        routeK[i * 4 + 2]->setEnabled(xtra > 0);
        routeK[i * 4 + 2]->repaint();
    }

    auto fbP = editor.patchCopy.routingNode.feedbackPower > 0.5;
    auto nsP = editor.patchCopy.routingNode.noisePower > 0.5;
    routeK[11]->setEnabled(fbP);
    routeK[12]->setEnabled(nsP);
    routeK[11]->repaint();
    routeK[12]->repaint();
    stepEditor->invalidatePath();
    repaint();
}

void StepLFOPanel::setCurrentStep(int cs)
{
    if (editor.cpuGraphicsMode != PluginEditor::MINIMAL)
    {
        if (cs != currentStep)
        {
            currentStep = cs;
            stepEditor->repaint();
        }
    }
}

void StepLFOPanel::setCurrentPhase(float ph)
{
    if (editor.cpuGraphicsMode == PluginEditor::FULL)
    {
        currentPhase = ph;
        stepEditor->repaint();
    }
}

void StepLFOPanel::setCurrentLevel(float ph)
{
    if (editor.cpuGraphicsMode == PluginEditor::FULL)
    {
        currentLevel = ph;
        stepEditor->repaint();
    }
}

} // namespace baconpaul::twofilters::ui