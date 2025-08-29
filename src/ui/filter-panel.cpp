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
#include <map>

#include "sst/filters/FilterPlotter.h"

namespace baconpaul::twofilters::ui
{

struct FilterCurve : juce::Component
{
    // put the thread here waiting and have rebuild trigger it and onilde poll it
    std::unique_ptr<std::thread> thread;
    std::mutex sendM, dataM;
    std::condition_variable sendCV;
    std::atomic<bool> running{true};
    int updateRequest{0};
    int repaintReq{0}, lastRepaintReq{0};
    FilterCurve(FilterPanel &p) : panel(p)
    {
        thread = std::make_unique<std::thread>([this]() { run(); });
    }
    ~FilterCurve()
    {
        running = false;
        {
            std::unique_lock<std::mutex> l(sendM);
            sendCV.notify_one();
        }
        thread->join();
    }

    void run()
    {
        int lur{0};
        while (running)
        {
            int nur{lur};
            {
                std::unique_lock<std::mutex> l(sendM);
                if (nur != updateRequest)
                {
                    nur = updateRequest;
                }
                else
                {
                    sendCV.wait(l);
                }
            }
            if (running && nur != lur)
            {
                lur = nur;

                // TODO - really we should snap these values on audio thread in lock
                auto &fn = panel.editor.patchCopy.filterNodes[panel.instance];
                auto crv = plotter.plotFilterMagnitudeResponse(fn.model, fn.config, fn.cutoff,
                                                               fn.resonance, fn.morph, 0, 0);
                auto tcX = crv.first;
                for (auto &x : tcX)
                    x = (x > 0 ? log10(x) : 0);

                {
                    std::unique_lock<std::mutex> l(dataM);
                    cX = tcX;
                    cY = crv.second;
                    repaintReq++;
                }
            }
        }
    }
    void paint(juce::Graphics &g) override
    {
        std::unique_lock<std::mutex> l(dataM);
        auto xsc = 1.0 / (log10(20000) - log10(6)) * getWidth();
        auto xoff = -log10(6);

        float dbMax{24}, dbMin{-48 - 12};
        auto ysc = 1.0 / (dbMax - dbMin) * getHeight();
        int yoff = dbMax / (dbMax - dbMin) * getHeight();

        auto tx = [=](float x) { return (xoff + x) * xsc; };
        auto ty = [=](float y) { return yoff - y * ysc; };

        g.setColour(juce::Colour(100, 100, 100));
        g.setFont(juce::FontOptions(10));
        for (int i = 1; i < 5; ++i)
        {
            g.drawVerticalLine(tx(i), 0, getHeight());
            auto hz = pow(10.0, i);
            auto txt = fmt::format("{:.0f} Hz", hz);
            if (hz >= 1000)
                txt = fmt::format("{:.0f} kHz", hz / 1000);

            g.drawText(txt, tx(i) + 2, getHeight() - 22, 100, 20, juce::Justification::bottomLeft);
        }

        for (int db = dbMax; db >= dbMin; db -= 24)
        {
            g.drawHorizontalLine(ty(db), 0, getWidth());
            auto txt = fmt::format("{:.0f} dB", (float)db);
            g.drawText(txt, 2, ty(db) + 2, 100, 20, juce::Justification::topLeft);
        }
        g.drawRect(getLocalBounds(), 1.0f);

        if (cX.empty() || cY.empty())
            return;

        auto p = juce::Path();
        p.startNewSubPath(tx(cX[0]), ty(cY[0]));
        auto my = std::min(cY[0], dbMin);
        for (int i = 1; i < cX.size(); i++)
        {
            p.lineTo(tx(cX[i]), ty(cY[i]));
            if (cY[i] < my)
                my = cY[i];
        }
        auto fillpath = p;

        auto c = juce::Colour(0xFF, 0x90, 0x00);
        auto gr = juce::ColourGradient::vertical(c.withAlpha(0.6f), ty(6),
            c.withAlpha(0.1f), ty(-48));

        fillpath.lineTo(tx(cX.back()), ty(my));
        fillpath.lineTo(tx(cX[0]), ty(my));
        fillpath.closeSubPath();
        g.setGradientFill(gr);
        g.fillPath(fillpath);

        g.setColour(juce::Colours::white);
        g.strokePath(p, juce::PathStrokeType(1.5));
    }
    void resized() override {}

    void onIdle()
    {
        bool rp{false};
        {
            std::unique_lock<std::mutex> l(dataM);
            rp = lastRepaintReq == repaintReq;
            lastRepaintReq = repaintReq;
        }
        if (rp)
            repaint();
    }

    void rebuild()
    {
        std::unique_lock<std::mutex> l(sendM);
        updateRequest++;
        sendCV.notify_one();
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
    cutoffD->labelOverride = "Cutoff";
    addAndMakeVisible(*cutoffK);

    createComponent(editor, *this, fn.resonance, resonanceK, resonanceD);
    addAndMakeVisible(*resonanceK);
    resonanceD->labelOverride = "Resonance";
    resonanceD->onGuiSetValue = [this]() { curve->rebuild(); };

    createComponent(editor, *this, fn.morph, morphK, morphD);
    addAndMakeVisible(*morphK);
    morphD->onGuiSetValue = [this]() { curve->rebuild(); };
    morphD->labelOverride = "Morph";

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
    auto b = getContentArea().withHeight(170);
    curve->setBounds(b);

    auto rs = getContentArea().withTrimmedTop(180);
    auto q = 70;
    auto pad = 5;

    auto bk = rs.withWidth(q).withTrimmedLeft(5).withTrimmedRight(5);
    cutoffK->setBounds(bk);
    resonanceK->setBounds(bk.translated(q + pad, 0));
    morphK->setBounds(bk.translated(2 * q + 2 * pad, 0));

    auto rest = rs.withTrimmedLeft(3 * q + 3 * pad);
    modelMenu->setBounds(rest.withHeight(20));
    configMenu->setBounds(rest.withHeight(20).translated(0, 22));
}

void FilterPanel::onModelChanged()
{
    auto &fn = editor.patchCopy.filterNodes[instance];
    auto mn = sst::filtersplusplus::toString(fn.model);
    auto cn = fn.config.toString();

    auto xtra = sst::filtersplusplus::Filter::coefficientsExtraCount(fn.model, fn.config);
    morphK->setEnabled(xtra > 0);

    auto tl = mn + " " + cn;
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

    if (cfgs.empty() || (cfgs.size() == 1 && cfgs[0] == sst::filtersplusplus::ModelConfig()))
    {
        p.addSectionHeader("No Sub-Configurations");
        p.showMenuAsync(juce::PopupMenu::Options().withParentComponent(&editor));
        return;
    }

    std::map<sst::filtersplusplus::Passband, int> countByBand;
    for (const auto &c : cfgs)
    {
        countByBand[c.pt] ++;
    }

    auto priorPassType = cfgs[0].pt;
    for (auto &c : cfgs)
    {
        if (c.pt != priorPassType && (countByBand[c.pt] > 1 || countByBand[priorPassType] > 1))
        {
            priorPassType = c.pt;
            p.addSeparator();
        }
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

void FilterPanel::onIdle() { curve->onIdle(); }

} // namespace baconpaul::twofilters::ui