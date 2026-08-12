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

#include "neui-editor.h"

#include <neuiplusplus/neuiplusplus.h>

#include "configuration.h"
#include "engine/engine.h"
#include "neui-plugin-editor.h"
#include "neui-menus.h"

namespace baconpaul::twofilters::ui
{
namespace npp = neuiplusplus;

struct NeuiEditor::Impl
{
    neui_api_t *host{nullptr};
    neui_embed_api_t *embed{nullptr};
    neui_timer_api_t *timers{nullptr};
    uint32_t idleTimerId{0};
    std::unique_ptr<npp::Session> session;
    std::unique_ptr<npp::Frame> frame;
    NeuiPluginEditor *editor{nullptr};
    npp::MenuStyle menuStyle;
    std::unique_ptr<npp::PopupMenu> menu;

    Impl(Engine &engine, const clap_host_t *clapHost)
    {
        static bool neuiInitialized{false};
        if (!neuiInitialized)
        {
            neui_init();
            neuiInitialized = true;
        }

        // The crossplatform host is the only one implementing embed (and
        // timers, cursors, a11y); pixel-identical on every platform.
        host = neui_get_api("neui.host.crossplatform");
        session = npp::Session::create(host);
        if (!session)
            return;

        embed =
            static_cast<neui_embed_api_t *>(host->get_interface(session->raw(), NEUI_API_EMBED));
        timers =
            static_cast<neui_timer_api_t *>(host->get_interface(session->raw(), NEUI_API_TIMER));

        frame = std::make_unique<npp::Frame>(*session, NEUI_W_PLUGWINDOW,
                                             npp::Rect{0, 0, float(edWidth), float(edHeight)},
                                             PRODUCT_NAME);
        editor =
            &frame->add<NeuiPluginEditor>(engine.patchMain, engine.audioToMain, engine.mainToAudio,
                                          engine.editorActive, engine.uiForceRebuild, clapHost);
        editor->setBounds(npp::Rect{0, 0, float(edWidth), float(edHeight)});
        frame->onResize = [this](npp::Rect client)
        {
            if (editor)
                editor->setBounds(client.atOrigin());
        };

        // The popup menu controller hangs its scrim and panels off the frame,
        // created after the editor so they paint above it.
        menuStyle = makeMenuStyle();
        menu = std::make_unique<npp::PopupMenu>(*frame, menuStyle, npp::MenuPlacement::inFrame);
        editor->menu = menu.get();

        // The ~60Hz idle: drain the audio->ui queue, animate, poll rebuilds.
        if (timers)
            idleTimerId = timers->add_timer(session->raw(), 16);
        session->onRawEvent = [this](neui_event_t *ev)
        {
            if (ev->type == NEUI_EVENT_TIMER && ev->data.timer.timer_id == idleTimerId)
            {
                if (editor)
                    editor->idle();
                return true;
            }
            return false;
        };
    }

    ~Impl()
    {
        if (session)
            session->onRawEvent = nullptr;
        if (timers && idleTimerId)
            timers->remove_timer(session->raw(), idleTimerId);
        // The menu references frame children, so it goes first; then the
        // frame (which owns the tree), then the session.
        menu.reset();
        editor = nullptr;
        frame.reset();
        session.reset();
    }
};

NeuiEditor::NeuiEditor(Engine &engine, const clap_host_t *clapHost)
    : impl(std::make_unique<Impl>(engine, clapHost))
{
}
NeuiEditor::~NeuiEditor() = default;

bool NeuiEditor::valid() const { return impl->session && impl->frame && impl->embed; }

bool NeuiEditor::setParent(void *nativeParent)
{
    if (!valid())
        return false;
    return impl->embed->set_parent(impl->session->raw(), impl->frame->widget(), nativeParent);
}

void NeuiEditor::show()
{
    if (valid())
        impl->frame->show();
}

void NeuiEditor::hide()
{
    // neui has no unrealize-and-keep; hosts hide by unmapping our parent.
}

uint32_t NeuiEditor::width() const { return edWidth; }
uint32_t NeuiEditor::height() const { return edHeight; }

int NeuiEditor::eventFd() const
{
    if (!valid())
        return -1;
    return impl->embed->event_fd(impl->session->raw(), impl->frame->widget());
}

void NeuiEditor::pumpAndTick()
{
    if (valid())
        impl->embed->pump_and_tick(impl->session->raw(), impl->frame->widget());
}

} // namespace baconpaul::twofilters::ui
