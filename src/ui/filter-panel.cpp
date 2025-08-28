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
        auto crv = plotter.plotFilterMagnitudeResponse(fn.model, fn.config, fn.cutoff, fn.resonance,
                                                       0, 0, 0);
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

    auto &fn = editor.patchCopy.filterNodes[instance];
    createComponent(editor, *this, fn.cutoff, cutoffK, cutoffD);
    cutoffD->onGuiSetValue = [this]() { curve->rebuild(); };
    addAndMakeVisible(*cutoffK);

    createComponent(editor, *this, fn.resonance, resonanceK, resonanceD);
    addAndMakeVisible(*resonanceK);
    resonanceD->onGuiSetValue = [this]() { curve->rebuild(); };

    modelMenu = std::make_unique<sst::jucegui::components::MenuButton>();
    addAndMakeVisible(*modelMenu);
    modelMenu->setOnCallback([this]() { showModelMenu(); });
    configMenu = std::make_unique<sst::jucegui::components::MenuButton>();
    addAndMakeVisible(*configMenu);
    configMenu->setOnCallback([this]() { showConfigMenu(); });
}

FilterPanel::~FilterPanel() = default;

void FilterPanel::resized()
{
    auto b = getContentArea().withHeight(150);
    curve->setBounds(b);

    auto rs = getContentArea().withTrimmedTop(160);
    auto q = rs.getHeight() - 30;

    auto bk = rs.withWidth(q);
    cutoffK->setBounds(bk);
    resonanceK->setBounds(bk.translated(q, 0));

    auto rest = rs.withTrimmedLeft(2 * q + 10);
    modelMenu->setBounds(rest.withHeight(20));
    configMenu->setBounds(rest.withHeight(20).translated(0, 22));
}

void FilterPanel::onModelChanged()
{
    auto mn = sst::filtersplusplus::toString(editor.patchCopy.filterNodes[instance].model);
    auto cn = editor.patchCopy.filterNodes[instance].config.toString();

    auto tl = mn + " " + cn;
    SQLOG("FilterPanel[" << instance << "] -> " << tl);
    setName("Filter " + std::to_string(instance + 1) + ": " + tl);
    curve->rebuild();

    modelMenu->setLabel(mn);
    configMenu->setLabel(cn);
    repaint();
}

void FilterPanel::showModelMenu()
{
    auto p = juce::PopupMenu();
    p.addSectionHeader("Filter Models");
    p.addSeparator();

    namespace sfpp = sst::filtersplusplus;

    for (auto &m : sfpp::Filter::availableModels())
    {
        p.addItem(sfpp::toString(m),
                  [this, m]()
                  {
                      auto configs = sfpp::Filter::availableModelConfigurations(m, true);
                      editor.patchCopy.filterNodes[instance].model = m;
                      if (configs.empty())
                          editor.patchCopy.filterNodes[instance].config = {};
                      else
                          editor.patchCopy.filterNodes[instance].config = configs.front();
                      editor.pushFilterSetup(instance);
                      onModelChanged();
                  });
    }

    p.showMenuAsync(juce::PopupMenu::Options().withParentComponent(&editor));
}

void FilterPanel::showConfigMenu()
{
    auto p = juce::PopupMenu();
    p.addSectionHeader("Filter Models");
    p.addSeparator();

    namespace sfpp = sst::filtersplusplus;

    auto cfgs = sfpp::Filter::availableModelConfigurations(
        editor.patchCopy.filterNodes[instance].model, true);
    for (auto &c : cfgs)
    {
        p.addItem(c.toString(),
                  [this, c]()
                  {
                      editor.patchCopy.filterNodes[instance].config = c;
                      editor.pushFilterSetup(instance);
                      onModelChanged();
                  });
    }

    p.showMenuAsync(juce::PopupMenu::Options().withParentComponent(&editor));
}

} // namespace baconpaul::twofilters::ui