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

#include "neui-steplfo-panel.h"
#include "neui-patch-bindings.h"
#include "neui-menus.h"

#include <fmt/core.h>

#include <sst/neuigui/components/BaseStyles.h>
#include <sst/basic-blocks/tables/EqualTuningProvider.h>
#include <sst/basic-blocks/modulators/Transport.h>

namespace baconpaul::twofilters::ui
{

struct NStepEditor : npp::Component<NStepEditor, npp::Paints, npp::MouseEvents>
{
    NStepEditor(npp::Parent p, NStepLFOPanel &pan) : Component(p), panel(pan), lfo(tp)
    {
        tp.init();
        for (int i = 0; i < maxSteps; i++)
            panel.editor.componentRefreshByID[panel.stepDs[i]->pid] = [this]() { repaint(); };
    }

    void paint(npp::Canvas &g) override
    {
        auto b = g.bounds();
        auto W = b.getWidth();
        auto H = b.getHeight();
        float bw = W * 1.0 / maxSteps;
        namespace bst = sst::neuigui::components::base_styles;
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

        auto &sn = panel.editor.patchMainRef.stepLfoNodes[panel.instance];
        auto fullArea = b.withWidth(sn.stepCount * bw);
        auto restArea = b.withTrimmedLeft(sn.stepCount * bw);
        g.fillRect(fullArea, gCol);
        g.fillRect(restArea, gCol.withAlpha(0.3f));

        float mg{1};
        for (int i = 0; i < maxSteps; i++)
        {
            float val = panel.stepDs[i]->getValue();

            auto sCol = vCol;
            if (i >= (int)sn.stepCount)
                sCol = vCol.withAlpha(0.3f);

            if (val < 0)
            {
                g.fillRect({i * bw + mg, H / 2, bw - 2 * mg, -H / 2 * val}, sCol);
            }
            else
            {
                g.fillRect({i * bw + mg, H / 2 * (1 - val), bw - 2 * mg, H / 2 * val}, sCol);
            }
        }
        g.drawRect(b, 1, oCol);
        for (int i = 1; i <= (int)sn.stepCount; i++)
            g.drawLine({i * bw, 0}, {i * bw, H}, 1, oCol);
        g.drawLine({0, H / 2}, {W, H / 2}, 1, oCol);

        if (paintStep >= 0)
        {
            float val = panel.stepDs[paintStep]->getValue();
            auto vf = fmt::format("{:.2f}", val);
            auto sf = lFt.withSize(8);
            if (val > 0)
            {
                g.drawText(vf, {paintStep * bw, H / 2 + 2, bw, 14}, sf, lCol, npp::HAlign::centre,
                           npp::VAlign::top);
            }
            else
            {
                g.drawText(vf, {paintStep * bw, H / 2 - 16, bw, 14}, sf, lCol, npp::HAlign::centre,
                           npp::VAlign::bottom);
            }
        }
        if (panel.editor.cpuGraphicsMode != NeuiPluginEditor::MINIMAL)
        {
            auto cs = panel.currentStep;
            if (cs >= 0 && cs < maxSteps)
            {
                g.drawRect({cs * bw, 0, bw, H}, 1, hCol);
            }
        }

        if (panel.editor.cpuGraphicsMode == NeuiPluginEditor::FULL)
        {
            auto cs = panel.currentStep;
            auto xC = (cs + panel.currentPhase) * bw;
            auto yC = (1 - panel.currentLevel) * H / 2;
            g.fillEllipse({xC - 2, yC - 2, 5, 5}, lCol);
        }
        rebuildLfoPath();
        if (!lfoPath.empty())
        {
            auto bolc =
                panel.style()->getColour(bst::BaseLabel::styleClass, bst::BaseLabel::labelcolor);

            g.beginPath();
            g.moveTo(lfoPath[0]);
            for (std::size_t i = 1; i < lfoPath.size(); ++i)
                g.lineTo(lfoPath[i]);
            g.strokePath(1, bolc);
        }
    }

    std::vector<npp::Point> lfoPath;
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

        auto W = localBounds().getWidth();
        auto H = localBounds().getHeight();

        transport.tempo = 120;
        auto rate = 7;
        auto sr = 48000;
        auto steps = panel.editor.patchMainRef.stepLfoNodes[panel.instance].stepCount;

        rng.reseed(8675309);
        lfo.setSampleRate(sr, 1.0 / sr);
        Engine::updateLfoStorageFromTo(panel.editor.patchMainRef, panel.instance, lfoStorage);
        lfo.assign(&lfoStorage, rate, &transport, rng, true);

        auto stepTime = 1.0 / (1 << 7);
        auto stepSamples = sr * stepTime;
        auto stepBlocks = stepSamples / blockSize;

        lfoPath.clear();

        auto tx = [=](auto x) { return x / (stepBlocks * 16) * W; };
        auto ty = [=](auto y) { return (1 - (y + 1) / 2.) * H; };
        for (int i = 0; i < steps * stepBlocks; ++i)
        {
            lfo.process(rate, 0, true, false, blockSize);
            lfoPath.push_back({(float)tx(i), (float)ty(lfo.output)});
        }

        pathValid = true;
    }

    int lastEditedStep{-1};
    int paintStep{-1};
    void adjustValue(const npp::Point &e, bool endAlways, bool resetValue = false)
    {
        auto W = localBounds().getWidth();
        auto H = localBounds().getHeight();
        auto x = std::clamp(e.x, 0.f, W);
        auto y = std::clamp(e.y, 0.f, H);
        auto bw = W * 1.0 / maxSteps;
        auto step = std::clamp(int(x / bw), 0, (int)maxSteps - 1);
        auto val = std::clamp(1 - y / H, 0.f, 1.f) * 2 - 1;
        if (resetValue)
            val = 0.f;

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

    void mouseDown(const npp::MouseEvent &e) override
    {
        lastEditedStep = -1;
        adjustValue(e.position, false);
    }

    void mouseDoubleClick(const npp::MouseEvent &e) override
    {
        lastEditedStep = -1;
        adjustValue(e.position, false, true);
    }

    void mouseDrag(const npp::MouseEvent &e) override { adjustValue(e.position, false); }
    void mouseUp(const npp::MouseEvent &e) override { adjustValue(e.position, true); }

    void mouseRightButtonDown(const npp::MouseEvent &e) override
    {
        auto W = localBounds().getWidth();
        auto x = std::clamp(e.position.x, 0.f, W);
        auto bw = W * 1.0 / maxSteps;
        auto step = std::clamp(int(x / bw), 0, (int)maxSteps - 1);

        showPopup(step, e.position);
    }

    void showPopup(int step, npp::Point at)
    {
        std::vector<npp::MenuItem> items;
        items.push_back(npp::MenuItem::makeHeader("Step LFO " + std::to_string(panel.instance + 1) +
                                                  ", Step " + std::to_string(step + 1)));
        items.push_back(npp::MenuItem::makeSeparator());
        items.push_back(npp::MenuItem::makeTypeIn(
            "Value", panel.stepDs[step]->getValueAsString(),
            [this, step](const std::string &s)
            {
                panel.editor.mainToAudio.push(
                    {Engine::MainToAudioMsg::Action::BEGIN_EDIT, panel.stepDs[step]->pid});
                panel.stepDs[step]->setValueAsString(s);
                panel.editor.mainToAudio.push(
                    {Engine::MainToAudioMsg::Action::END_EDIT, panel.stepDs[step]->pid});
                repaint();
            }));
        items.push_back(npp::MenuItem::makeSeparator());
        items.push_back(npp::MenuItem::makeEntry("Reset Steps", [this]() { panel.resetSteps(); }));
        items.push_back(
            npp::MenuItem::makeEntry("Reset Mod Routes", [this]() { panel.resetRoutes(); }));
        items.push_back(npp::MenuItem::makeSeparator());
        items.push_back(
            npp::MenuItem::makeEntry("Randomize Steps", [this]() { panel.randomizeSteps(); }));
        items.push_back(npp::MenuItem::makeEntry("Randomize Mod Routes",
                                                 [this]() { panel.randomizeRoutes(); }));

        auto o = editorLocalOrigin(this, &panel.editor);
        panel.editor.showMenu(std::move(items), o + at);
    }
    NStepLFOPanel &panel;
};

NStepLFOPanel::NStepLFOPanel(npp::Parent p, NeuiPluginEditor &ed, int inst)
    : sngc::NamedPanelBase<NStepLFOPanel>(p, "StepLFO " + std::to_string(inst + 1)), editor(ed),
      instance(inst)
{
    auto &sn = editor.patchMainRef.stepLfoNodes[instance];
    for (int i = 0; i < maxSteps; i++)
    {
        stepDs[i] = std::make_unique<PatchContinuous>(editor, sn.steps[i].meta.id);
        stepDs[i]->onGuiSetValue = [this]()
        {
            stepEditor->invalidatePath();
            stepEditor->repaint();
        };
    }
    stepEditor = &add<NStepEditor>(*this);

    createComponent(editor, *this, sn.stepCount, stepCount, stepCountD);
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
    idx++;

    createComponent(editor, *this, sn.toPreG, routeK[idx], routeD[idx]);
    routeD[idx]->labelOverride = "Pre";
    idx++;
    createComponent(editor, *this, sn.toPostG, routeK[idx], routeD[idx]);
    routeD[idx]->labelOverride = "Post";
    idx++;
    createComponent(editor, *this, sn.toFiltBlend, routeK[idx], routeD[idx]);
    routeD[idx]->labelOverride = "Blend";
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

    toF1 = &add<sngc::RuledLabel>();
    toF1->setText("To Filter 1");
    toF2 = &add<sngc::RuledLabel>();
    toF2->setText("To Filter 2");
    toRt = &add<sngc::RuledLabel>();
    toRt->setText("To Main");

    createComponent(editor, *this, sn.rate, rate, rateD);
    rateD->tempoSynced = true;
    createComponent(editor, *this, sn.smooth, smooth, smoothD);
    smoothD->onGuiSetValue = [this]() { stepEditor->invalidatePath(); };
}

NStepLFOPanel::~NStepLFOPanel() = default;

void NStepLFOPanel::resized()
{
    layoutHeaderControls();

    auto rPad{80.0f}, bPad{160.0f};
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
    auto kw = 45.0f;
    auto kmarg = 10.0f;

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
    placeTop(10);
    placeBot(11);
    placeBot(12);
    placeBot(13);
}

void NStepLFOPanel::onModelChanged()
{
    for (int i = 0; i < numFilters; ++i)
    {
        auto &fn = editor.patchMainRef.filterNodes[i];
        auto xtra = sst::filtersplusplus::Filter::coefficientsExtraCount(fn.model, fn.config);
        routeK[i * 4 + 2]->setEnabled(xtra > 0);
        routeK[i * 4 + 2]->repaint();
    }

    auto fbP = editor.patchMainRef.routingNode.feedbackPower > 0.5;
    auto nsP = editor.patchMainRef.routingNode.noisePower > 0.5;
    routeK[12]->setEnabled(fbP);
    routeK[13]->setEnabled(nsP);
    routeK[12]->repaint();
    routeK[13]->repaint();
    stepEditor->invalidatePath();
    repaint();
}

void NStepLFOPanel::setCurrentStep(int cs)
{
    if (editor.cpuGraphicsMode != NeuiPluginEditor::MINIMAL)
    {
        if (cs != currentStep)
        {
            currentStep = cs;
            stepEditor->repaint();
        }
    }
}

void NStepLFOPanel::setCurrentPhase(float ph)
{
    if (editor.cpuGraphicsMode == NeuiPluginEditor::FULL)
    {
        currentPhase = ph;
        stepEditor->repaint();
    }
}

void NStepLFOPanel::setCurrentLevel(float ph)
{
    if (editor.cpuGraphicsMode == NeuiPluginEditor::FULL)
    {
        currentLevel = ph;
        stepEditor->repaint();
    }
}

void NStepLFOPanel::randomize()
{
    randomizeSteps();
    randomizeRoutes();
}

void NStepLFOPanel::randomizeSteps()
{
    for (int s = 0; s < maxSteps; ++s)
    {
        editor.mainToAudio.push({Engine::MainToAudioMsg::Action::BEGIN_EDIT, stepDs[s]->pid});
        stepDs[s]->setValueFromGUI(editor.rng.unifPM1());
        editor.mainToAudio.push({Engine::MainToAudioMsg::Action::END_EDIT, stepDs[s]->pid});
    }
}
void NStepLFOPanel::randomizeRoutes()
{
    auto rst = [&, this](auto &D, auto &K)
    {
        auto mx = D->getMax();
        auto mn = D->getMin();
        auto nv = editor.rng.unif(mn, mx);
        K->onBeginEdit();
        D->setValueFromGUI(nv);
        K->onEndEdit();
    };

    rst(smoothD, smooth);
    rst(rateD, rate);
    rst(stepCountD, stepCount);

    for (int s = 0; s < numRoutes; ++s)
    {
        rst(routeD[s], routeK[s]);
    }

    repaint();
}

void NStepLFOPanel::resetSteps()
{
    for (int s = 0; s < maxSteps; ++s)
    {
        editor.mainToAudio.push({Engine::MainToAudioMsg::Action::BEGIN_EDIT, stepDs[s]->pid});
        stepDs[s]->setValueFromGUI(0.f);
        editor.mainToAudio.push({Engine::MainToAudioMsg::Action::END_EDIT, stepDs[s]->pid});
    }
    repaint();
}
void NStepLFOPanel::resetRoutes()
{
    for (int s = 0; s < numRoutes; ++s)
    {
        routeK[s]->onBeginEdit();
        routeD[s]->setValueFromGUI(routeD[s]->getDefaultValue());
        routeK[s]->onEndEdit();
    }
    repaint();
}
} // namespace baconpaul::twofilters::ui
