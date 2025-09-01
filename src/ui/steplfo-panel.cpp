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
    StepEditor(StepLFOPanel &p) : panel(p)
    {
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
        hCol = hCol.withAlpha(0.5f);

        g.fillAll(gCol);

        int mg{2};
        for (int i = 0; i < maxSteps; i++)
        {
            float val = panel.stepDs[i]->getValue();

            g.setColour(vCol);

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
        for (int i = 1; i < maxSteps; i++)
            g.drawVerticalLine(i * bw, 0, getHeight());
        g.drawHorizontalLine(getHeight() / 2, 0, getWidth());

        auto cs = panel.currentStep;
        if (cs >= 0 && cs < maxSteps)
        {
            g.setColour(hCol);
            g.drawRect(cs * bw, 0., bw, getHeight() * 1., 1.);
        }
    }

    int lastEditedStep{-1};
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

        if (endAlways)
        {
            panel.editor.mainToAudio.push(
                {Engine::MainToAudioMsg::Action::END_EDIT, panel.stepDs[step]->pid});
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
    }
    stepEditor = std::make_unique<StepEditor>(*this);
    addAndMakeVisible(*stepEditor);
}
StepLFOPanel::~StepLFOPanel() = default;
void StepLFOPanel::resized()
{
    auto q = getContentArea().withTrimmedRight(90).withTrimmedBottom(90);

    stepEditor->setBounds(q);
}

} // namespace baconpaul::twofilters::ui