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

#include "plugin-editor.h"

#include "sst/plugininfra/version_information.h"
#include "sst/clap_juce_shim/menu_helper.h"
#include "sst/plugininfra/misc_platform.h"

#include "sst/jucegui/components/Label.h"

#include "presets/preset-manager.h"
#include "preset-data-binding.h"
#include "patch-data-bindings.h"

#include "main-panel.h"
#include "filter-panel.h"
#include "routing-panel.h"

namespace baconpaul::twofilters::ui
{
struct IdleTimer : juce::Timer
{
    PluginEditor &editor;
    IdleTimer(PluginEditor &e) : editor(e) {}
    void timerCallback() override { editor.idle(); }
};

namespace jstl = sst::jucegui::style;
using sheet_t = jstl::StyleSheet;
static constexpr sheet_t::Class PatchMenu("twofilters.patch-menu");

PluginEditor::PluginEditor(Engine::audioToUIQueue_t &atou, Engine::mainToAudioQueue_T &utoa,
                           const clap_host_t *h)
    : jcmp::WindowPanel(true), audioToUI(atou), mainToAudio(utoa), clapHost(h)
{
    setTitle("Two Filters");
    setAccessible(true);
    sst::jucegui::style::StyleSheet::initializeStyleSheets([]() {});

    sheet_t::addClass(PatchMenu).withBaseClass(jcmp::JogUpDownButton::Styles::styleClass);

    setStyle(sst::jucegui::style::StyleSheet::getBuiltInStyleSheet(
        sst::jucegui::style::StyleSheet::DARK));

    style()->setFont(
        PatchMenu, jcmp::MenuButton::Styles::labelfont,
        style()
            ->getFont(jcmp::MenuButton::Styles::styleClass, jcmp::MenuButton::Styles::labelfont)
            .withHeight(18));

    mainPanel = std::make_unique<MainPanel>(*this);
    mainPanel->hasHamburger = false;
    addAndMakeVisible(*mainPanel);

    for (int i = 0; i < numFilters; ++i)
    {
        filterPanel[i] = std::make_unique<FilterPanel>(*this, i);
        addAndMakeVisible(*filterPanel[i]);
    }

    routingPanel = std::make_unique<RoutingPanel>(*this);
    addAndMakeVisible(*routingPanel);

    auto startMsg = Engine::MainToAudioMsg{Engine::MainToAudioMsg::REQUEST_REFRESH};
    mainToAudio.push(startMsg);
    requestParamsFlush();

    idleTimer = std::make_unique<IdleTimer>(*this);
    idleTimer->startTimer(1000. / 60.);

    toolTip = std::make_unique<jcmp::ToolTip>();
    addChildComponent(*toolTip);

    presetManager = std::make_unique<presets::PresetManager>(clapHost);
    presetManager->onPresetLoaded = [this](auto s)
    {
        for (auto &f : filterPanel)
            f->onModelChanged();

        this->postPatchChange(s);
        repaint();
    };

    presetDataBinding = std::make_unique<PresetDataBinding>(*presetManager, patchCopy, mainToAudio);
    presetDataBinding->setStateForDisplayName(patchCopy.name);

    presetButton = std::make_unique<jcmp::JogUpDownButton>();
    presetButton->setCustomClass(PatchMenu);
    presetButton->setSource(presetDataBinding.get());
    presetButton->onPopupMenu = [this]() { showPresetPopup(); };
    addAndMakeVisible(*presetButton);
    setPatchNameDisplay();
    sst::jucegui::component_adapters::setTraversalId(presetButton.get(), 174);

    // this needs a cleanup
    defaultsProvider = std::make_unique<defaultsProvder_t>(
        presetManager->userPath, "TwoFilters", defaultName,
        [](auto e, auto b) { SQLOG("[ERROR]" << e << " " << b); });
    setSkinFromDefaults();

    lnf = std::make_unique<sst::jucegui::style::LookAndFeelManager>(this);
    lnf->setStyle(style());

    vuMeter = std::make_unique<jcmp::VUMeter>(jcmp::VUMeter::HORIZONTAL);
    addAndMakeVisible(*vuMeter);

    mainToAudio.push({Engine::MainToAudioMsg::EDITOR_ATTACH_DETATCH, true});
    mainToAudio.push({Engine::MainToAudioMsg::REQUEST_REFRESH, true});
    requestParamsFlush();

    auto pzf = defaultsProvider->getUserDefaultValue(Defaults::zoomLevel, 100);
    zoomFactor = pzf * 0.01;
    setTransform(juce::AffineTransform().scaled(zoomFactor));

    setSize(edWidth, edHeight);
#define DEBUG_FOCUS 0
#if DEBUG_FOCUS
    focusDebugger = std::make_unique<sst::jucegui::accessibility::FocusDebugger>(*this);
    focusDebugger->setDoFocusDebug(true);
#endif
}
PluginEditor::~PluginEditor()
{
    mainToAudio.push({Engine::MainToAudioMsg::EDITOR_ATTACH_DETATCH, false});
    idleTimer->stopTimer();
    setLookAndFeel(nullptr);
}

void PluginEditor::idle()
{
    auto aum = audioToUI.pop();
    while (aum.has_value())
    {
        if (aum->action == Engine::AudioToUIMsg::UPDATE_PARAM)
        {
            setAndSendParamValue(aum->paramId, aum->value, false);
        }
        else if (aum->action == Engine::AudioToUIMsg::UPDATE_VU)
        {
            vuMeter->setLevels(aum->value, aum->value2);
        }
        else if (aum->action == Engine::AudioToUIMsg::SET_PATCH_NAME)
        {
            memset(patchCopy.name, 0, sizeof(patchCopy.name));
            strncpy(patchCopy.name, aum->patchNamePointer, 255);
            setPatchNameDisplay();
        }
        else if (aum->action == Engine::AudioToUIMsg::SET_PATCH_DIRTY_STATE)
        {
            patchCopy.dirty = (bool)aum->paramId;
            presetDataBinding->setDirtyState(patchCopy.dirty);
            presetButton->repaint();
        }
        else if (aum->action == Engine::AudioToUIMsg::DO_PARAM_RESCAN)
        {
            if (!clapParamsExtension)
                clapParamsExtension = static_cast<const clap_host_params_t *>(
                    clapHost->get_extension(clapHost, CLAP_EXT_PARAMS));
            if (clapParamsExtension)
            {
                clapParamsExtension->rescan(clapHost,
                                            CLAP_PARAM_RESCAN_VALUES | CLAP_PARAM_RESCAN_TEXT);
                clapParamsExtension->request_flush(clapHost);
            }
        }
        else if (aum->action == Engine::AudioToUIMsg::SEND_SAMPLE_RATE)
        {
            sampleRate = aum->value;
            repaint();
        }
        else if (aum->action == Engine::AudioToUIMsg::SEND_FILTER_CONFIG)
        {
            namespace sfpp = sst::filtersplusplus;
            auto fid = aum->paramId;
            patchCopy.filterNodes[fid].model = (sfpp::FilterModel)aum->uintValues[0];
            patchCopy.filterNodes[fid].config.pt = (sfpp::Passband)aum->uintValues[1];
            patchCopy.filterNodes[fid].config.st = (sfpp::Slope)aum->uintValues[2];
            patchCopy.filterNodes[fid].config.dt = (sfpp::DriveMode)aum->uintValues[3];
            patchCopy.filterNodes[fid].config.mt = (sfpp::FilterSubModel)aum->uintValues[4];
            filterPanel[fid]->onModelChanged();
        }
        else
        {
            SQLOG("Ignored patch message " << aum->action);
        }
        aum = audioToUI.pop();
    }

    for (auto &f : filterPanel)
        f->onIdle();
}

void PluginEditor::paint(juce::Graphics &g)
{
    jcmp::WindowPanel::paint(g);
    auto ft = style()->getFont(jcmp::Label::Styles::styleClass, jcmp::Label::Styles::labelfont);

    bool isLight = defaultsProvider->getUserDefaultValue(Defaults::useLightSkin, false);

    g.setColour(juce::Colours::white.withAlpha(0.9f));
    auto q = ft.withHeight(30);
    g.setFont(q);
    auto xp = 3;
    auto ht = 30;

    int np{110};

    if (isLight)
        g.setColour(juce::Colours::navy);
    else
        g.setColour(juce::Colours::white.withAlpha(0.5f));
    q = ft.withHeight(12);
    g.setFont(q);

    g.drawText(PRODUCT_NAME, getLocalBounds().reduced(3, 3), juce::Justification::bottomLeft);

    std::string os = "";
#if JUCE_MAC
    os = "macOS";
#endif
#if JUCE_WINDOWS
    os = "Windows";
#endif
#if JUCE_LINUX
    os = "Linux";
#endif

    auto bi = os + " " + sst::plugininfra::VersionInformation::git_commit_hash;
    bi += fmt::format(" @ {:.1f}k", sampleRate / 1000.0);
    g.drawText(bi, getLocalBounds().reduced(3, 3), juce::Justification::bottomRight);

    g.drawText(sst::plugininfra::VersionInformation::git_implied_display_version,
               getLocalBounds().reduced(3, 3), juce::Justification::centredBottom);

    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(25));
    auto dr = juce::Rectangle<int>(0, 0, np, ht);
    g.drawText(PRODUCT_NAME, dr.reduced(2), juce::Justification::centredLeft);

#if !defined(NDEBUG) || !NDEBUG
    g.setFont(juce::FontOptions(30));

    g.setColour(juce::Colours::white.withAlpha(0.6f));
    g.drawText("DEBUG", dr.translated(1, 1), juce::Justification::centred);
    g.setColour(juce::Colours::red.withAlpha(0.6f));
    g.drawText("DEBUG", dr, juce::Justification::centred);
#endif
}

void PluginEditor::resized()
{
    int presetHeight{33}, footerHeight{15};

    auto lb = getLocalBounds();
    auto presetArea = lb.withHeight(presetHeight);
    auto panelArea = lb.withTrimmedTop(presetHeight).withTrimmedBottom(footerHeight);

    auto panelMargin{3};
    auto uicMargin{4};
    // Preset button
    auto but = presetArea.reduced(110, 0).withTrimmedTop(uicMargin);
    presetButton->setBounds(but);
    but = but.withLeft(presetButton->getRight() + uicMargin).withRight(getWidth() - uicMargin);

    vuMeter->setBounds(but);

    auto ta = panelArea.withHeight(300).withWidth(panelArea.getWidth() / 2);
    auto fa = ta.withTrimmedRight(60);
    filterPanel[0]->setBounds(fa.reduced(panelMargin));
    filterPanel[1]->setBounds(fa.translated(fa.getWidth(), 0).reduced(panelMargin));
    auto ra = fa.translated(fa.getWidth() * 2, 0).withWidth(120);
    routingPanel->setBounds(ra.reduced(panelMargin));
    auto ma = panelArea.withTrimmedTop(300);
    mainPanel->setBounds(ma.reduced(panelMargin));
}

void PluginEditor::showTooltipOn(juce::Component *c)
{
    int x = 0;
    int y = 0;
    juce::Component *component = c;
    while (component != this)
    {
        auto bounds = component->getBoundsInParent();
        x += bounds.getX();
        y += bounds.getY();

        component = component->getParentComponent();
    }
    y += c->getHeight();
    toolTip->resetSizeFromData();
    if (y + toolTip->getHeight() > getHeight() - 40)
    {
        y -= c->getHeight() + 3 + toolTip->getHeight();
    }

    if (x + toolTip->getWidth() > getWidth())
    {
        x -= toolTip->getWidth();
        x += c->getWidth() - 3;
    }

    toolTip->setTopLeftPosition(x, y);
    toolTip->setVisible(true);
}
void PluginEditor::updateTooltip(jdat::Continuous *c)
{
    toolTip->setTooltipTitleAndData(c->getLabel(), {c->getValueAsString()});
    toolTip->resetSizeFromData();
}
void PluginEditor::updateTooltip(jdat::Discrete *d)
{
    toolTip->setTooltipTitleAndData(d->getLabel(), {d->getValueAsString()});
    toolTip->resetSizeFromData();
}
void PluginEditor::hideTooltip() { toolTip->setVisible(false); }

struct MenuValueTypein : HasEditor, juce::PopupMenu::CustomComponent, juce::TextEditor::Listener
{
    std::unique_ptr<juce::TextEditor> textEditor;
    juce::Component::SafePointer<jcmp::ContinuousParamEditor> underComp;

    MenuValueTypein(PluginEditor &editor,
                    juce::Component::SafePointer<jcmp::ContinuousParamEditor> under)
        : juce::PopupMenu::CustomComponent(false), HasEditor(editor), underComp(under)
    {
        textEditor = std::make_unique<juce::TextEditor>();
        textEditor->setWantsKeyboardFocus(true);
        textEditor->addListener(this);
        textEditor->setIndents(2, 0);

        addAndMakeVisible(*textEditor);
    }

    void getIdealSize(int &w, int &h) override
    {
        w = 180;
        h = 22;
    }
    void resized() override { textEditor->setBounds(getLocalBounds().reduced(3, 1)); }

    void visibilityChanged() override
    {
        juce::Timer::callAfterDelay(
            2,
            [this]()
            {
                if (textEditor->isVisible())
                {
                    textEditor->setText(getInitialText(),
                                        juce::NotificationType::dontSendNotification);
                    auto valCol = juce::Colour(0xFF, 0x90, 0x00);
                    textEditor->setColour(juce::TextEditor::ColourIds::backgroundColourId,
                                          valCol.withAlpha(0.1f));
                    textEditor->setColour(juce::TextEditor::ColourIds::highlightColourId,
                                          valCol.withAlpha(0.15f));
                    textEditor->setJustification(juce::Justification::centredLeft);
                    textEditor->setColour(juce::TextEditor::ColourIds::outlineColourId,
                                          juce::Colours::black.withAlpha(0.f));
                    textEditor->setColour(juce::TextEditor::ColourIds::focusedOutlineColourId,
                                          juce::Colours::black.withAlpha(0.f));
                    textEditor->setBorder(juce::BorderSize<int>(3));
                    textEditor->applyColourToAllText(valCol, true);
                    textEditor->grabKeyboardFocus();
                    textEditor->selectAll();
                }
            });
    }

    std::string getInitialText() const { return underComp->continuous()->getValueAsString(); }

    void setValueString(const std::string &s)
    {
        if (underComp && underComp->continuous())
        {
            underComp->onBeginEdit();

            if (s.empty())
            {
                underComp->continuous()->setValueFromGUI(
                    underComp->continuous()->getDefaultValue());
            }
            else
            {
                underComp->continuous()->setValueAsString(s);
            }
            underComp->onEndEdit();
            underComp->repaint();
            underComp->grabKeyboardFocus();
            underComp->notifyAccessibleChange();
        }
    }

    void textEditorReturnKeyPressed(juce::TextEditor &ed) override
    {
        auto s = ed.getText().toStdString();
        setValueString(s);
        triggerMenuItem();
    }
    void textEditorEscapeKeyPressed(juce::TextEditor &) override { triggerMenuItem(); }
};

void PluginEditor::popupMenuForContinuous(jcmp::ContinuousParamEditor *e)
{
    auto data = e->continuous();
    if (!data)
    {
        return;
    }

    if (!e->isEnabled())
    {
        return;
    }

    auto p = juce::PopupMenu();
    p.addSectionHeader(data->getLabel());
    p.addSeparator();
    p.addCustomItem(-1, std::make_unique<MenuValueTypein>(*this, juce::Component::SafePointer(e)));
    p.addSeparator();
    p.addItem("Set to Default",
              [w = juce::Component::SafePointer(e)]()
              {
                  if (!w)
                      return;
                  w->continuous()->setValueFromGUI(w->continuous()->getDefaultValue());
                  w->repaint();
              });

    // I could also stick the param id onto the component properties I guess
    auto pid = sst::jucegui::component_adapters::getClapParamId(e);
    if (pid.has_value())
    {
        sst::clap_juce_shim::populateMenuForClapParam(p, *pid, clapHost);
    }

    p.showMenuAsync(juce::PopupMenu::Options().withParentComponent(this));
}

void PluginEditor::showPresetPopup()
{
    auto p = juce::PopupMenu();
    p.addSectionHeader("Main Menu");

    auto f = juce::PopupMenu();
    for (auto &[c, ent] : presetManager->factoryPatchNames)
    {
        auto em = juce::PopupMenu();
        for (auto &e : ent)
        {
            auto noExt = e;
            auto ps = noExt.find(PATCH_EXTENSION);
            if (ps != std::string::npos)
            {
                noExt = noExt.substr(0, ps);
            }
            em.addItem(noExt,
                       [cat = c, pat = e, this]() {
                           this->presetManager->loadFactoryPreset(patchCopy, mainToAudio, cat, pat);
                       });
        }
        f.addSubMenu(c, em);
    }

    auto u = juce::PopupMenu();
    auto cat = fs::path();
    auto s = juce::PopupMenu();
    for (const auto &up : presetManager->userPatches)
    {
        auto pp = up.parent_path();
        auto dn = up.filename().replace_extension("").u8string();
        if (pp.empty())
        {
            u.addItem(dn,
                      [this, pth = up, dn]()
                      {
                          presetManager->loadUserPresetDirect(patchCopy, mainToAudio,
                                                              presetManager->userPatchesPath / pth);
                      });
        }
        else
        {
            if (pp != cat)
            {
                if (cat.empty())
                {
                    u.addSeparator();
                }
                if (s.getNumItems() > 0)
                {
                    u.addSubMenu(cat.u8string(), s);
                    s = juce::PopupMenu();
                }
                cat = pp;
            }
            s.addItem(dn,
                      [this, pth = up, dn]()
                      {
                          presetManager->loadUserPresetDirect(patchCopy, mainToAudio,
                                                              presetManager->userPatchesPath / pth);
                      });
        }
    }
    if (s.getNumItems() > 0 && !cat.empty())
    {
        u.addSubMenu(cat.u8string(), s);
    }
    p.addSeparator();
    p.addSubMenu("Factory Presets", f);
    p.addSubMenu("User Presets", u);
    p.addSeparator();
    p.addItem("Load Patch",
              [w = juce::Component::SafePointer(this)]()
              {
                  if (w)
                      w->doLoadPatch();
              });
    p.addItem("Save Patch",
              [w = juce::Component::SafePointer(this)]()
              {
                  if (w)
                      w->doSavePatch();
              });
    p.addSeparator();
    p.addItem("Reset to Init",
              [w = juce::Component::SafePointer(this)]()
              {
                  if (w)
                  {
                      w->resetToDefault();
                  }
              });
    p.addSeparator();

    auto uim = juce::PopupMenu();
    auto isLight = defaultsProvider->getUserDefaultValue(Defaults::useLightSkin, 0);

    for (auto scale : {75, 90, 100, 110, 125, 150})
    {
        uim.addItem("Zoom " + std::to_string(scale) + "%", true,
                    std::fabs(zoomFactor * 100 - scale) < 2,
                    [s = scale, w = juce::Component::SafePointer(this)]()
                    {
                        if (!w)
                            return;
                        w->setZoomFactor(s * 0.01);
                    });
    }

    uim.addSeparator();
    uim.addItem("Dark Mode", true, !isLight,
                [w = juce::Component::SafePointer(this)]()
                {
                    if (!w)
                        return;
                    w->defaultsProvider->updateUserDefaultValue(Defaults::useLightSkin, false);
                    w->setSkinFromDefaults();
                });

    uim.addItem("Light Mode", true, isLight,
                [w = juce::Component::SafePointer(this)]()
                {
                    if (!w)
                        return;
                    w->defaultsProvider->updateUserDefaultValue(Defaults::useLightSkin, true);
                    w->setSkinFromDefaults();
                });
    uim.addSeparator();

    uim.addItem("Activate Debug Log", true, debugLevel > 0,
                [w = juce::Component::SafePointer(this)]()
                {
                    if (w)
                        w->toggleDebug();
                });

#if JUCE_WINDOWS
    auto swr = defaultsProvider->getUserDefaultValue(Defaults::useSoftwareRenderer, false);

    uim.addItem(
        "Use Software Renderer", true, swr,
        [w = juce::Component::SafePointer(this), swr]()
        {
            if (!w)
                return;
            w->defaultsProvider->updateUserDefaultValue(Defaults::useSoftwareRenderer, !swr);
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon, "Software Renderer Change",
                "A software renderer change is only active once you restart/reload the plugin.");
        });
#endif

    p.addSubMenu("User Interface", uim);

    p.addSeparator();
    p.addItem("Read the Manual", false, false, []() {});
    p.addItem("Get the Source", []()
              { juce::URL("https://github.com/baconpaul/two-filters/").launchInDefaultBrowser(); });
    p.addItem("Acknowledgements", false, false, []() {});
    p.showMenuAsync(juce::PopupMenu::Options().withParentComponent(this));
}

void PluginEditor::doSavePatch()
{
    auto svP = presetManager->userPatchesPath;
    if (strcmp(patchCopy.name, "Init") != 0)
    {
        svP = (svP / patchCopy.name).replace_extension(PATCH_EXTENSION);
    }
    fileChooser = std::make_unique<juce::FileChooser>("Save Patch", juce::File(svP.u8string()),
                                                      juce::String("*") + PATCH_EXTENSION);
    fileChooser->launchAsync(juce::FileBrowserComponent::canSelectFiles |
                                 juce::FileBrowserComponent::saveMode |
                                 juce::FileBrowserComponent::warnAboutOverwriting,
                             [w = juce::Component::SafePointer(this)](const juce::FileChooser &c)
                             {
                                 if (!w)
                                     return;
                                 auto result = c.getResults();
                                 if (result.isEmpty() || result.size() > 1)
                                 {
                                     return;
                                 }
                                 auto pn = fs::path{result[0].getFullPathName().toStdString()};
                                 w->setPatchNameTo(pn.filename().replace_extension("").u8string());

#if USE_WCHAR_PRESET
                                 w->presetManager->saveUserPresetDirect(
                                     w->patchCopy, result[0].getFullPathName().toUTF16());
#else
                                 w->presetManager->saveUserPresetDirect(w->patchCopy, pn);
#endif

                                 w->presetDataBinding->setDirtyState(false);
                                 w->repaint();
                             });
}

void PluginEditor::setPatchNameTo(const std::string &s)
{
    memset(patchCopy.name, 0, sizeof(patchCopy.name));
    strncpy(patchCopy.name, s.c_str(), 255);
    mainToAudio.push({Engine::MainToAudioMsg::SEND_PATCH_NAME, 0, 0, patchCopy.name});
    setPatchNameDisplay();
}

void PluginEditor::doLoadPatch()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Load Patch", juce::File(presetManager->userPatchesPath.u8string()),
        juce::String("*") + PATCH_EXTENSION);
    fileChooser->launchAsync(
        juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::openMode,
        [w = juce::Component::SafePointer(this)](const juce::FileChooser &c)
        {
            if (!w)
                return;
            auto result = c.getResults();
            if (result.isEmpty() || result.size() > 1)
            {
                return;
            }
            auto loadPath = fs::path{result[0].getFullPathName().toStdString()};
            w->presetManager->loadUserPresetDirect(w->patchCopy, w->mainToAudio, loadPath);
        });
}

void PluginEditor::resetToDefault() { presetManager->loadInit(patchCopy, mainToAudio); }

void PluginEditor::setAndSendParamValue(uint32_t paramId, float value, bool notifyAudio,
                                        bool sendBeginEnd)
{
    patchCopy.paramMap[paramId]->value = value;

    auto rit = componentRefreshByID.find(paramId);
    if (rit != componentRefreshByID.end())
    {
        rit->second();
    }

    auto pit = componentByID.find(paramId);
    if (pit != componentByID.end() && pit->second)
        pit->second->repaint();

    if (notifyAudio)
    {
        if (sendBeginEnd)
            mainToAudio.push({Engine::MainToAudioMsg::Action::BEGIN_EDIT, paramId});
        mainToAudio.push({Engine::MainToAudioMsg::Action::SET_PARAM, paramId, value});
        if (sendBeginEnd)
            mainToAudio.push({Engine::MainToAudioMsg::Action::END_EDIT, paramId});
        requestParamsFlush();
    }
}

void PluginEditor::setPatchNameDisplay()
{
    if (!presetButton)
        return;
    presetDataBinding->setStateForDisplayName(patchCopy.name);
    presetButton->repaint();
}

void PluginEditor::postPatchChange(const std::string &s)
{
    presetDataBinding->setStateForDisplayName(s);
    for (auto [id, f] : componentRefreshByID)
        f();

    repaint();
}

bool PluginEditor::keyPressed(const juce::KeyPress &key) { return false; }

void PluginEditor::visibilityChanged()
{
    if (isVisible() && isShowing())
    {
        presetButton->setWantsKeyboardFocus(true);
        presetButton->grabKeyboardFocus();
    }
}

void PluginEditor::parentHierarchyChanged()
{
    if (isVisible() && isShowing())
    {
        presetButton->setWantsKeyboardFocus(true);
        presetButton->grabKeyboardFocus();
    }

#if JUCE_WINDOWS
    auto swr = defaultsProvider->getUserDefaultValue(Defaults::useSoftwareRenderer, false);

    if (swr)
    {
        if (auto peer = getPeer())
        {
            SQLOG("Enabling software rendering engine");
            peer->setCurrentRenderingEngine(0); // 0 for software mode, 1 for Direct2D mode
        }
    }
#endif
}

void PluginEditor::setSkinFromDefaults()
{
    auto b = defaultsProvider->getUserDefaultValue(Defaults::useLightSkin, 0);
    if (b)
    {
        setStyle(sst::jucegui::style::StyleSheet::getBuiltInStyleSheet(
            sst::jucegui::style::StyleSheet::LIGHT));
        style()->setColour(PatchMenu, jcmp::MenuButton::Styles::fill,
                           style()
                               ->getColour(jcmp::base_styles::Base::styleClass,
                                           jcmp::base_styles::Base::background)
                               .darker(0.3f));
    }
    else
    {
        setStyle(sst::jucegui::style::StyleSheet::getBuiltInStyleSheet(
            sst::jucegui::style::StyleSheet::DARK));
    }

    style()->setFont(
        PatchMenu, jcmp::MenuButton::Styles::labelfont,
        style()
            ->getFont(jcmp::MenuButton::Styles::styleClass, jcmp::MenuButton::Styles::labelfont)
            .withHeight(18));
}

void PluginEditor::setZoomFactor(float zf)
{
    // SCLOG("Setting zoom factor to " << zf);
    zoomFactor = zf;
    setTransform(juce::AffineTransform().scaled(zoomFactor));
    defaultsProvider->updateUserDefaultValue(Defaults::zoomLevel, zoomFactor * 100);
    if (onZoomChanged)
        onZoomChanged(zoomFactor);
}

void PluginEditor::doSinglePanelHamburger()
{
    juce::Component *vis;
    for (auto c : mainPanel->getChildren())
    {
        if (c->isVisible())
        {
            vis = c;
        }
    }
    if (!vis)
        return;
}

void PluginEditor::activateHamburger(bool b)
{
    mainPanel->hasHamburger = b;
    mainPanel->repaint();
}

void PluginEditor::requestParamsFlush()
{
    if (!clapParamsExtension)
        clapParamsExtension = static_cast<const clap_host_params_t *>(
            clapHost->get_extension(clapHost, CLAP_EXT_PARAMS));
    if (clapParamsExtension)
    {
        clapParamsExtension->request_flush(clapHost);
    }
}

void PluginEditor::sneakyStartupGrabFrom(Patch &other)
{
    for (auto &p : other.params)
    {
        patchCopy.paramMap.at(p->meta.id)->value = p->value;
    }
    strncpy(patchCopy.name, other.name, 255);
    postPatchChange(other.name);
}

bool PluginEditor::toggleDebug()
{
    if (debugLevel == 0)
    {
        sst::plugininfra::misc_platform::allocateConsole();
    }
    if (debugLevel <= 0)
        debugLevel = 1;
    else
        debugLevel = -1;
    SQLOG("Started debug session");
    SQLOG("If you are on windows and you close this window it may end your entire session");
    return debugLevel > 0;
}

void PluginEditor::onStyleChanged()
{
    jcmp::WindowPanel::onStyleChanged();
    if (lnf)
        lnf->setStyle(style());
}

void PluginEditor::pushFilterSetup(int instance)
{
    auto &fn = patchCopy.filterNodes[instance];

    Engine::MainToAudioMsg msg;
    msg.action = Engine::MainToAudioMsg::SET_FILTER_MODEL;
    msg.paramId = instance;
    msg.uintValues[0] = (uint32_t)fn.model;
    msg.uintValues[1] = (uint32_t)fn.config.pt;
    msg.uintValues[2] = (uint32_t)fn.config.st;
    msg.uintValues[3] = (uint32_t)fn.config.dt;
    msg.uintValues[4] = (uint32_t)fn.config.mt;
    mainToAudio.push(msg);
}

} // namespace baconpaul::twofilters::ui