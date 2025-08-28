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

#include "filter-panel.h"

#include "sst/filters/FilterPlotter.h"

namespace baconpaul::twofilters::ui
{

struct FilterCurve : juce::Component
{
    FilterCurve(FilterPanel &p) : panel(p) {}
    void paint(juce::Graphics &g) override
    {
        auto xsc = 1.0 / log10(20000) * getWidth();
        auto ysc = 3.0;
        int yoff = getHeight() * .3;
        g.setColour(juce::Colours::white);
        for (int i = 1; i < cX.size(); i++)
        {
            g.drawLine(cX[i - 1] * xsc, yoff - ysc * cY[i - 1], cX[i] * xsc, yoff - ysc * cY[i], 1);
        }
    }
    void resized() override {}

    void rebuild()
    {
        auto &fn = panel.editor.patchCopy.filterNodes[panel.instance];
        auto crv = plotter.plotFilterMagnitudeResponse(fn.model, fn.config, 0, 0.99, 0, 0, 0);
        cX = crv.first;
        for (auto &x : cX)
            x = log10(x);
        cY = crv.second;
        repaint();
    }

    FilterPanel &panel;

    std::vector<float> cX, cY;

    sst::filters::FilterPlotter plotter;
};

FilterPanel::FilterPanel(PluginEditor &editor, int ins)
    : instance(ins), sst::jucegui::components::NamedPanel("Filter " + std::to_string(ins + 1)),
      editor(editor)
{
    curve = std::make_unique<FilterCurve>(*this);
    addAndMakeVisible(*curve);
}

FilterPanel::~FilterPanel() = default;

void FilterPanel::resized()
{
    auto b = getContentArea().withHeight(150);
    curve->setBounds(b);
}

void FilterPanel::onModelChanged()
{
    SQLOG("FilterPanel[" << instance << "]::onModelChanged ");
    auto mn = sst::filtersplusplus::toString(editor.patchCopy.filterNodes[instance].model);
    auto cn = editor.patchCopy.filterNodes[instance].config.toString();
    SQLOG("  model  : " << mn);
    SQLOG("  config : " << cn);

    auto tl = mn + " " + cn;
    setName("Filter " + std::to_string(instance + 1) + ": " + tl);
    curve->rebuild();
    repaint();
}

} // namespace baconpaul::twofilters::ui