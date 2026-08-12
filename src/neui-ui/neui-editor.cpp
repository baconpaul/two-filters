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

namespace baconpaul::twofilters::ui
{
namespace npp = neuiplusplus;

namespace
{
struct HelloPanel : npp::Component<HelloPanel, npp::Paints>
{
    HelloPanel(npp::Parent p) : Component(p) {}

    void paint(npp::Canvas &g) override
    {
        auto b = g.bounds();
        g.fillAll(npp::Color::rgb(0x25, 0x25, 0x28));
        g.drawText("hello neui", b, npp::Font(28.0f), npp::Color::rgb(0xFF, 0x90, 0x00),
                   npp::HAlign::centre, npp::VAlign::middle);
    }
};
} // namespace

struct NeuiEditor::Impl
{
    neui_api_t *host{nullptr};
    neui_embed_api_t *embed{nullptr};
    std::unique_ptr<npp::Session> session;
    std::unique_ptr<npp::Frame> frame;

    Impl()
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

        frame = std::make_unique<npp::Frame>(*session, NEUI_W_PLUGWINDOW,
                                             npp::Rect{0, 0, float(edWidth), float(edHeight)},
                                             PRODUCT_NAME);
        auto &hello = frame->add<HelloPanel>();
        hello.setBounds(npp::Rect{0, 0, float(edWidth), float(edHeight)});
        frame->onResize = [&hello](npp::Rect client) { hello.setBounds(client.atOrigin()); };
    }

    ~Impl()
    {
        // Components before the session; Frame holds the tree.
        frame.reset();
        session.reset();
    }
};

NeuiEditor::NeuiEditor() : impl(std::make_unique<Impl>()) {}
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
