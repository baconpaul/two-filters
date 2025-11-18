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

#include "steplfo-panel.h"

#include <map>

#include "sst/filters/FilterPlotter.h"
#include "sst/jucegui/components/BaseStyles.h"
#include "sst/jucegui/component-adapters/ComponentTags.h"

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
        auto lastWait = std::chrono::high_resolution_clock::now();

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
            auto postLastWait = std::chrono::high_resolution_clock::now();
            auto dur =
                std::chrono::duration_cast<std::chrono::microseconds>(postLastWait - lastWait);
#if JUCE_WINDOWS
            auto atLeast = 1000000 / 30.0;
#else
            auto atLeast = 1000000 / 60.0;
#endif

            if (dur.count() < atLeast)
            {
                std::this_thread::sleep_for(
                    std::chrono::microseconds((int)(atLeast - dur.count())));
            }
            if (running && nur != lur)
            {
                float lco, lre, lmo;

                {
                    std::unique_lock<std::mutex> l(dataM);

                    nur = updateRequest;
                    lur = nur;

                    lco = co;
                    lre = res;
                    lmo = morph;
                }
                // TODO - really we should snap these values on audio thread in lock
                auto &fn = panel.editor.patchCopy.filterNodes[panel.instance];
                if (fn.model == sst::filtersplusplus::FilterModel::None)
                {
                    {
                        std::unique_lock<std::mutex> l(dataM);
                        cX.clear();
                        cY.clear();
                        cX.push_back(0.5);
                        cX.push_back(5.0);
                        cY.push_back(0.0);
                        cY.push_back(0.0);

                        repaintReq++;
                    }
                }
                else
                {
                    if (sst::filtersplusplus::Filter::coefficientsExtraIsBipolar(fn.model,
                                                                                 fn.config, 0))
                        lmo = lmo * 2 - 1;

                    auto par = sst::filters::FilterPlotParameters();
                    par.freqSmoothOctaves = 1.0 / 36.0;
                    auto crv = plotter.plotFilterMagnitudeResponse(fn.model, fn.config, lco, lre,
                                                                   lmo, 0, 0, par);
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
            lastWait = std::chrono::high_resolution_clock::now();
        }
    }

    bool showDragEdit{false};
    void mouseEnter(const juce::MouseEvent &event) override
    {
        showDragEdit = true;
        repaint();
    }
    void mouseExit(const juce::MouseEvent &event) override
    {
        showDragEdit = false;
        repaint();
    }

    void positionToCoRes(juce::Point<float> p)
    {
        auto res = 1 - std::clamp(p.y / getHeight(), 0.f, 1.f);

        // x px = (xoff + x) * xsc * getWidth();
        // x = xpx / getWidth / xsc - xoff
        auto lfr = std::clamp(p.x / getWidth(), 0.f, 1.f) / xsc - xoff;
        auto fr = pow(10.0, lfr);
        // fr = 440 pow(2, key/12)
        // key = 12*log2(fr / 440)
        auto key = 12 * log2(fr / 440.0);

        panel.cutoffD->setValueFromGUI(key);
        panel.resonanceD->setValueFromGUI(res);

        panel.cutoffK->repaint();
        panel.resonanceK->repaint();
    }
    void mouseDown(const juce::MouseEvent &event) override
    {
        if (event.mods.isPopupMenu())
        {
            showPopup();
            return;
        }
        panel.cutoffK->onBeginEdit();
        panel.resonanceK->onBeginEdit();
        positionToCoRes(event.position.toFloat());
        repaint();
    }

    void mouseDrag(const juce::MouseEvent &event) override
    {
        if (event.mods.isPopupMenu())
        {
            return;
        }
        positionToCoRes(event.position.toFloat());
        repaint();
    }

    void mouseUp(const juce::MouseEvent &event) override
    {
        if (event.mods.isPopupMenu())
        {
            return;
        }
        panel.resonanceK->onEndEdit();
        panel.cutoffK->onEndEdit();
    }

    void showPopup()
    {
        auto m = juce::PopupMenu();
        m.addSectionHeader("Filter " + std::to_string(panel.instance + 1));
        m.addSeparator();
        m.addItem("Reset",
                  [w = juce::Component::SafePointer(this)]()
                  {
                      if (w)
                          w->panel.resetFilter();
                  });
        m.addItem("Randomize",
                  [w = juce::Component::SafePointer(this)]()
                  {
                      if (w)
                          w->panel.randomize();
                  });
        m.addSeparator();
        m.addItem("Swap Filters",
                  [w = juce::Component::SafePointer(this)]()
                  {
                      if (w)
                          w->panel.editor.swapFilters(false);
                  });
        m.addItem("Swap Filters and Modulation",
                  [w = juce::Component::SafePointer(this)]()
                  {
                      if (w)
                          w->panel.editor.swapFilters(true);
                  });
        m.showMenuAsync(juce::PopupMenu::Options().withParentComponent(&panel.editor));
    }

    float xsc = 1.0 / (log10(20000) - log10(6));
    float xoff = -log10(6);

    float dbMax{24}, dbMin{-48 - 12};
    float ysc = 1.0 / (dbMax - dbMin);
    float yoff = dbMax / (dbMax - dbMin);

    auto tx(float x) { return (xoff + x) * xsc * getWidth(); };
    auto ty(float y) { return (yoff - y * ysc) * getHeight(); };

    juce::Image renderCache;
    void paint(juce::Graphics &gReal) override
    {
        /*
         * The updated setp sequence causes a repaint here which on some windows boxes is
         * painful so just cache the image
         */
        float sc =
            juce::Desktop::getInstance().getDisplays().getDisplayForRect(getLocalBounds())->scale *
            panel.editor.zoomFactor;
        int tW = std::round(sc * getWidth());
        int tH = std::round(sc * getHeight());

        if (renderCache.getWidth() != tW || renderCache.getHeight() != tH)
        {
            invalidateImage = true;
        }

        if (!invalidateImage && renderCache.getWidth() > 0)
        {
            gReal.drawImage(renderCache, getLocalBounds().toFloat());

            if (showDragEdit)
            {
                drawCrosshairs(gReal);
            }

            if (!isEnabled())
            {
                namespace bst = sst::jucegui::components::base_styles;
                gReal.fillAll(
                    panel.style()
                        ->getColour(bst::ValueGutter::styleClass, bst::ValueGutter::gutter)
                        .withAlpha(0.5f));
            }
            return;
        }

        invalidateImage = false;
        if (renderCache.getWidth() != tW || renderCache.getHeight() != tH)
        {
            renderCache = juce::Image(juce::Image::ARGB, tW, tH, true);
        }

        juce::Graphics g(renderCache);
        g.fillAll(juce::Colours::black);
        g.addTransform(juce::AffineTransform::scale(sc, sc));
        std::unique_lock<std::mutex> l(dataM);

        namespace bst = sst::jucegui::components::base_styles;
        g.fillAll(panel.style()->getColour(bst::ValueGutter::styleClass, bst::ValueGutter::gutter));
        auto olc = panel.style()->getColour(bst::Outlined::styleClass, bst::Outlined::outline);
        g.setColour(olc);
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
        {
            gReal.drawImage(renderCache, getLocalBounds().toFloat());
            return;
        }

        auto p = juce::Path();
        p.startNewSubPath(tx(cX[0]), ty(cY[0]));
        auto lX = tx(cX[0]);
        auto my = std::min(cY[0], dbMin);
        for (int i = 1; i < cX.size(); i++)
        {
            if (tx(cX[i]) - tx(lX) > 0.5)
            {
                lX = cX[i];

                p.lineTo(tx(cX[i]), ty(cY[i]));
                if (cY[i] < my)
                    my = cY[i];
            }
        }
        auto fillpath = p;

        auto vlc =
            panel.style()->getColour(bst::ValueBearing::styleClass, bst::ValueBearing::value);

        auto c = vlc;

        fillpath.lineTo(tx(cX.back()), ty(my));
        fillpath.lineTo(tx(cX[0]), ty(my));
        fillpath.closeSubPath();

        if (cX.size() > 10 && panel.editor.cpuGraphicsMode != PluginEditor::MINIMAL)
        {
            if (panel.editor.cpuGraphicsMode != PluginEditor::FULL)
            {
                g.setColour(c.withAlpha(0.2f));
            }
            else
            {
                auto gr = juce::ColourGradient::vertical(c.withAlpha(0.6f), ty(6),
                                                         c.withAlpha(0.1f), ty(-48));
                g.setGradientFill(gr);
            }
            g.fillPath(fillpath);
        }

        auto bolc =
            panel.style()->getColour(bst::BaseLabel::styleClass, bst::BaseLabel::labelcolor);

        g.setColour(bolc);
        g.strokePath(p, juce::PathStrokeType(1.5));

        gReal.drawImage(renderCache, getLocalBounds().toFloat());
        if (showDragEdit)
        {
            drawCrosshairs(gReal);
        }
        if (!isEnabled())
        {
            namespace bst = sst::jucegui::components::base_styles;
            gReal.fillAll(panel.style()
                              ->getColour(bst::ValueGutter::styleClass, bst::ValueGutter::gutter)
                              .withAlpha(0.5f));
        }
    }
    void resized() override {}

    void drawCrosshairs(juce::Graphics &gReal)
    {
        auto &fn = panel.editor.patchCopy.filterNodes[panel.instance];
        co = fn.cutoff;
        res = fn.resonance;

        auto freq = 440 * pow(2, co / 12.0);
        auto lfre = log10(freq);
        auto cx = tx(lfre);
        auto cy = (1.0 - res) * getHeight();

        namespace bst = sst::jucegui::components::base_styles;

        auto vlc = panel.style()->getColour(bst::BaseLabel::styleClass, bst::BaseLabel::labelcolor);

        gReal.setColour(vlc);
        gReal.drawVerticalLine(cx, 0, getHeight());
        gReal.drawHorizontalLine(cy, 0, getWidth());

        gReal.fillEllipse(cx - 3, cy - 3, 6, 6);
    }

    int idleCount{0};
    void onIdle()
    {
        if (idleCount == 0)
        {
            bool rp{false};
            {
                std::unique_lock<std::mutex> l(dataM);
                rp = lastRepaintReq != repaintReq;
                lastRepaintReq = repaintReq;
            }
            if (rp)
            {
                invalidateImage = true;
                repaint();
            }
        }
        idleCount++;
        if (idleCount > ((panel.editor.cpuGraphicsMode != PluginEditor::FULL) ? 3 : 0))
            idleCount = 0;
    }
    bool invalidateImage{true};

    void rebuild()
    {
        std::unique_lock<std::mutex> l(sendM);
        updateRequest++;
        auto &fn = panel.editor.patchCopy.filterNodes[panel.instance];
        co = fn.cutoff;
        res = fn.resonance;
        morph = fn.morph;
        sendCV.notify_one();
    }

    float co, res, morph;

    FilterPanel &panel;

    std::vector<float> cX, cY;

    sst::filters::FilterPlotter plotter{14};
};

FilterPanel::FilterPanel(PluginEditor &ed, int ins)
    : instance(ins), sst::jucegui::components::NamedPanel("Filter " + std::to_string(ins + 1)),
      editor(ed)
{
    auto &fn = editor.patchCopy.filterNodes[instance];

    setTogglable(true);

    activeD = std::make_unique<PatchDiscrete>(editor, fn.active.meta.id);
    setToggleDataSource(activeD.get());
    toggleButton->onBeginEdit = [this, &fn]()
    { editor.mainToAudio.push({Engine::MainToAudioMsg::Action::BEGIN_EDIT, fn.active.meta.id}); };

    toggleButton->onEndEdit = [this, &fn]()
    { editor.mainToAudio.push({Engine::MainToAudioMsg::Action::END_EDIT, fn.active.meta.id}); };
    editor.componentRefreshByID[fn.active.meta.id] = [this]() { onModelChanged(); };

    activeD->onGuiSetValue = [this]() { onModelChanged(); };

    curve = std::make_unique<FilterCurve>(*this);
    addAndMakeVisible(*curve);

    createComponent(editor, *this, fn.cutoff, cutoffK, cutoffD);
    cutoffD->onGuiSetValue = [this]() { curve->rebuild(); };
    cutoffD->labelOverride = "Cutoff";
    editor.componentRefreshByID[fn.cutoff.meta.id] = [this]() { curve->rebuild(); };
    addAndMakeVisible(*cutoffK);

    createComponent(editor, *this, fn.resonance, resonanceK, resonanceD);
    addAndMakeVisible(*resonanceK);
    resonanceD->labelOverride = "Res";
    editor.componentRefreshByID[fn.resonance.meta.id] = [this]() { curve->rebuild(); };
    resonanceD->onGuiSetValue = [this]() { curve->rebuild(); };

    createComponent(editor, *this, fn.morph, morphK, morphD);
    addAndMakeVisible(*morphK);
    editor.componentRefreshByID[fn.morph.meta.id] = [this]() { curve->rebuild(); };
    morphD->onGuiSetValue = [this]() { curve->rebuild(); };
    morphD->labelOverride = "Morph";

    createComponent(editor, *this, fn.pan, panK, panD);
    addAndMakeVisible(*panK);
    panD->labelOverride = "Pan";

    modelMenu = std::make_unique<sst::jucegui::components::MenuButton>();
    addAndMakeVisible(*modelMenu);
    modelMenu->setOnCallback([this]() { showModelMenu(); });
    modelMenu->setOnJogCallback([this](auto i) { jogModel(i); });

    configMenu = std::make_unique<sst::jucegui::components::MenuButton>();
    addAndMakeVisible(*configMenu);
    configMenu->setOnCallback([this]() { showConfigMenu(); });
    configMenu->setOnJogCallback([this](auto i) { jogConfig(i); });

    pbMenu = std::make_unique<sst::jucegui::components::MenuButton>();
    pbMenu->setOnCallback([this]() { showConfigStructuredMenu(0); });
    addChildComponent(*pbMenu);

    slpMenu = std::make_unique<sst::jucegui::components::MenuButton>();
    slpMenu->setOnCallback([this]() { showConfigStructuredMenu(1); });
    addChildComponent(*slpMenu);

    drvMenu = std::make_unique<sst::jucegui::components::MenuButton>();
    drvMenu->setOnCallback([this]() { showConfigStructuredMenu(2); });
    addChildComponent(*drvMenu);

    fsmMenu = std::make_unique<sst::jucegui::components::MenuButton>();
    fsmMenu->setOnCallback([this]() { showConfigStructuredMenu(3); });
    addChildComponent(*fsmMenu);

    namespace jcad = sst::jucegui::component_adapters;

    auto bi = (instance + 1) * 1000;
    jcad::setTraversalId(toggleButton.get(), bi++);
    jcad::setTraversalId(cutoffK.get(), bi++);
    jcad::setTraversalId(resonanceK.get(), bi++);
    jcad::setTraversalId(morphK.get(), bi++);
    jcad::setTraversalId(panK.get(), bi++);
    jcad::setTraversalId(modelMenu.get(), bi++);
    jcad::setTraversalId(configMenu.get(), bi++);
    jcad::setTraversalId(pbMenu.get(), bi++);
    jcad::setTraversalId(slpMenu.get(), bi++);
    jcad::setTraversalId(drvMenu.get(), bi++);
    jcad::setTraversalId(fsmMenu.get(), bi++);
}

FilterPanel::~FilterPanel() = default;

void FilterPanel::resized()
{
    sst::jucegui::components::NamedPanel::resized();
    if (!curve)
        return;

    auto plotH = 190 - (300 - getHeight());

    auto b = getContentArea().withHeight(plotH);
    curve->setBounds(b);

    auto rs = getContentArea().withTrimmedTop(plotH + 5);
    auto q = 62;
    auto pad = 3;

    auto bk = rs.withWidth(q).withTrimmedLeft(5).withTrimmedRight(5);
    cutoffK->setBounds(bk);
    resonanceK->setBounds(bk.translated(q + pad, 0));
    morphK->setBounds(bk.translated(2 * q + 2 * pad, 0));
    panK->setBounds(bk.translated(3 * q + 3 * pad, 0));

    auto rest = rs.withTrimmedLeft(4 * q + 4 * pad);
    modelMenu->setBounds(rest.withHeight(20));
    configMenu->setBounds(rest.withHeight(20).translated(0, 22));
    auto bx = rest.translated(0, 22).withHeight(20);
    auto bxw = bx.getWidth() / 2;
    pbMenu->setBounds(bx.withTrimmedRight(bxw - 2));
    slpMenu->setBounds(bx.withTrimmedLeft(bxw + 2));
    bx = bx.translated(0, 22);
    drvMenu->setBounds(bx.withTrimmedRight(bxw - 2));
    fsmMenu->setBounds(bx.withTrimmedLeft(bxw + 2));
}

template <typename E> void setSub(auto &a, auto &m, E val)
{
    namespace sfpp = sst::filtersplusplus;

    if (sfpp::supportsChoice<E>(m))
    {
        if (val == E::UNSUPPORTED)
        {
            a->setLabel("-");
        }
        else
        {
            a->setLabel(sfpp::toString(val));
        }
        a->setEnabled(true);
    }
    else
    {
        a->setLabel("");
        a->setEnabled(false);
    }
}

void FilterPanel::onModelChanged()
{
    displayMode = (PluginEditor::ConfigDisplayMode)editor.defaultsProvider->getUserDefaultValue(
        Defaults::modelConfigMode, PluginEditor::ConfigDisplayMode::SINGLE_LIST);

    namespace sfpp = sst::filtersplusplus;

    auto &fn = editor.patchCopy.filterNodes[instance];
    auto mn = sfpp::toString(fn.model);
    auto cn = fn.config.toString();

    auto xtra = sst::filtersplusplus::Filter::coefficientsExtraCount(fn.model, fn.config);
    morphK->setEnabled(xtra > 0);

    auto tl = mn + " " + cn;
    setName("Filter " + std::to_string(instance + 1) + ": " + tl);
    curve->rebuild();

    modelMenu->setLabel(mn);
    configMenu->setLabel(cn);

    setSub(pbMenu, fn.model, fn.config.pt);
    setSub(slpMenu, fn.model, fn.config.st);
    setSub(drvMenu, fn.model, fn.config.dt);
    setSub(fsmMenu, fn.model, fn.config.mt);

    switch (displayMode)
    {
    case PluginEditor::SINGLE_LIST:
        pbMenu->setVisible(false);
        slpMenu->setVisible(false);
        drvMenu->setVisible(false);
        fsmMenu->setVisible(false);
        configMenu->setVisible(true);
        break;
    case PluginEditor::FOUR_ALL:
        pbMenu->setVisible(true);
        slpMenu->setVisible(true);
        drvMenu->setVisible(true);
        fsmMenu->setVisible(true);
        configMenu->setVisible(false);
        break;
    case PluginEditor::FOUR_HIDE:
        updateFourHideMenuVisibility();
        configMenu->setVisible(false);
        break;
    }

    auto act = fn.active > 0.5;
    cutoffK->setEnabled(act);
    resonanceK->setEnabled(act);
    morphK->setEnabled(act && (xtra > 0));
    modelMenu->setEnabled(act);
    configMenu->setEnabled(act);
    curve->setEnabled(act);
    repaint();
}

void FilterPanel::updateFourHideMenuVisibility()
{
    if (displayMode != PluginEditor::FOUR_HIDE)
        return;

    namespace sfpp = sst::filtersplusplus;
    auto &fn = editor.patchCopy.filterNodes[instance];

    pbMenu->setVisible(sfpp::supportsChoice<sfpp::Passband>(fn.model));
    slpMenu->setVisible(sfpp::supportsChoice<sfpp::Slope>(fn.model) &&
                        !sfpp::noChoicesOrOnlyUnsupported<sfpp::Slope>(fn.model, fn.config.pt));
    drvMenu->setVisible(
        sfpp::supportsChoice<sfpp::DriveMode>(fn.model) &&
        !sfpp::noChoicesOrOnlyUnsupported<sfpp::DriveMode>(fn.model, fn.config.pt, fn.config.st));
    fsmMenu->setVisible(sfpp::supportsChoice<sfpp::FilterSubModel>(fn.model) &&
                        !sfpp::noChoicesOrOnlyUnsupported<sfpp::FilterSubModel>(
                            fn.model, fn.config.pt, fn.config.st, fn.config.dt));
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
                      for (auto &s : editor.stepLFOPanel)
                          s->onModelChanged();
                  });
    }

    p.addSeparator();
    p.addSubMenu("Configuration UI", editor.configDisplayMenu());

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
        countByBand[c.pt]++;
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
                      for (auto &s : editor.stepLFOPanel)
                          s->onModelChanged();
                  });
    }

    p.showMenuAsync(juce::PopupMenu::Options().withParentComponent(&editor));
}

void FilterPanel::jogConfig(int dir)
{
    namespace sfpp = sst::filtersplusplus;
    auto &fn = editor.patchCopy.filterNodes[instance];

    auto am = sfpp::Filter::availableModelConfigurations(fn.model, true);
    int cm{-1};
    auto currConf = editor.patchCopy.filterNodes[instance].config;
    for (int i = 0; i < am.size(); ++i)
    {
        if (am[i] == currConf)
        {
            cm = i;
            break;
        }
    }

    cm = cm + dir;
    if (cm < 0)
        cm = am.size() - 1;
    if (cm >= am.size())
        cm = 0;
    auto newConf = am[cm];
    editor.patchCopy.filterNodes[instance].config = newConf;
    editor.pushFilterSetup(instance);
    onModelChanged();
}

void FilterPanel::jogModel(int dir)
{
    namespace sfpp = sst::filtersplusplus;
    auto am = sfpp::Filter::availableModels();
    int cm{-1};
    auto currMod = editor.patchCopy.filterNodes[instance].model;
    for (int i = 0; i < am.size(); ++i)
    {
        if (am[i] == currMod)
        {
            cm = i;
            break;
        }
    }

    cm = cm + dir;
    if (cm < 0)
        cm = am.size() - 1;
    if (cm >= am.size())
        cm = 0;
    auto newMod = am[cm];
    editor.patchCopy.filterNodes[instance].model = newMod;

    auto configs = sfpp::Filter::availableModelConfigurations(newMod, true);
    if (configs.empty())
        editor.patchCopy.filterNodes[instance].config = {};
    else
        editor.patchCopy.filterNodes[instance].config = configs.front();
    editor.pushFilterSetup(instance);
    onModelChanged();
}

void FilterPanel::onIdle() { curve->onIdle(); }

void FilterPanel::endEdit(int id) { curve->rebuild(); }

void FilterPanel::randomize()
{
    auto &fn = editor.patchCopy.filterNodes[instance];
    auto wr = [&, this](auto &par, auto &cont, auto &wid)
    {
        auto range = par.meta.maxVal - par.meta.minVal;
        auto nv = editor.rng.unif01() * range + par.meta.minVal;
        wid->onBeginEdit();
        cont->setValueFromGUI(nv);
        wid->onEndEdit();
    };

    namespace sfpp = sst::filtersplusplus;
    auto mods = sfpp::Filter::availableModels();
    // 1 since we dont want off
    auto mod = mods[editor.rng.unifInt(1, mods.size() - 1)];
    fn.model = mod;

    auto am = sfpp::Filter::availableModelConfigurations(fn.model, true);
    auto cfg = am[editor.rng.unifInt(0, am.size() - 1)];
    fn.config = cfg;

    wr(fn.cutoff, cutoffD, cutoffK);
    wr(fn.resonance, resonanceD, resonanceK);
    wr(fn.morph, morphD, morphK);
    wr(fn.pan, panD, panK);

    editor.pushFilterSetup(instance);
    onModelChanged();
    repaint();
}

void FilterPanel::resetFilter()
{
    namespace sfpp = sst::filtersplusplus;
    auto &fn = editor.patchCopy.filterNodes[instance];

    fn.model = sfpp::FilterModel::None;
    fn.config = {};

    auto wr = [&, this](auto &dat, auto &wid)
    {
        wid->onBeginEdit();
        dat->setValueFromGUI(dat->getDefaultValue());
        wid->onEndEdit();
    };

    wr(cutoffD, cutoffK);
    wr(resonanceD, resonanceK);
    wr(morphD, morphK);
    wr(panD, panK);

    editor.pushFilterSetup(instance);
    onModelChanged();
    repaint();
}

void FilterPanel::showConfigStructuredMenu(int component)
{
    namespace sfpp = sst::filtersplusplus;
    auto &fn = editor.patchCopy.filterNodes[instance];

    auto p = juce::PopupMenu();

    switch (component)
    {
    case 0:
        addConfigStructuredMenu<sfpp::Passband>(p, "Passband");
        break;

    case 1:
        addConfigStructuredMenu<sfpp::Slope>(p, "Slope");
        break;

    case 2:
        addConfigStructuredMenu<sfpp::DriveMode>(p, "Drive Mode");
        break;

    case 3:
        addConfigStructuredMenu<sfpp::FilterSubModel>(p, "Sub Model");
        break;
    }
    p.showMenuAsync(juce::PopupMenu::Options().withParentComponent(&editor));
}

template <sst::filtersplusplus::is_modelconfig_enum E>
void FilterPanel::addConfigStructuredMenu(juce::PopupMenu &p, const std::string &sh)
{
    namespace sfpp = sst::filtersplusplus;
    auto &fn = editor.patchCopy.filterNodes[instance];

    auto applyCf = [fn, this](const auto &cfc)
    {
        auto nc = sfpp::closestValidModelTo(fn.model, cfc);

        editor.patchCopy.filterNodes[instance].config = nc;
        editor.pushFilterSetup(instance);
        onModelChanged();
        for (auto &s : editor.stepLFOPanel)
            s->onModelChanged();
        updateFourHideMenuVisibility();
    };

    p.addSectionHeader(sh);
    p.addSeparator();

    std::vector<std::pair<E, bool>> opts;
    if constexpr (std::is_same_v<E, sfpp::Passband>)
    {
        opts = sfpp::valuesAndValidityForPartialConfig<E>(fn.model);
    }
    if constexpr (std::is_same_v<E, sfpp::Slope>)
    {
        opts = sfpp::valuesAndValidityForPartialConfig<E>(fn.model, fn.config.pt);
    }
    if constexpr (std::is_same_v<E, sfpp::DriveMode>)
    {
        opts = sfpp::valuesAndValidityForPartialConfig<E>(fn.model, fn.config.pt, fn.config.st);
    }
    if constexpr (std::is_same_v<E, sfpp::FilterSubModel>)
    {
        opts = sfpp::valuesAndValidityForPartialConfig<E>(fn.model, fn.config.pt, fn.config.st,
                                                          fn.config.dt);
    }
    for (auto &[o, active] : opts)
    {
        if (displayMode == PluginEditor::FOUR_HIDE && !active)
            continue;
        auto m = sfpp::toString(o);
        if (o == E::UNSUPPORTED)
            m = "None";
        p.addItem(m, active, o == sfpp::get<E>(fn.config),
                  [applyCf, fn, val = o, this]()
                  {
                      auto cfc = fn.config;
                      sfpp::set<E>(cfc, val);
                      applyCf(cfc);
                  });
    }
}

} // namespace baconpaul::twofilters::ui