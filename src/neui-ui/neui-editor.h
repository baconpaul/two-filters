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

#ifndef BACONPAUL_TWOFILTERS_NEUI_UI_NEUI_EDITOR_H
#define BACONPAUL_TWOFILTERS_NEUI_UI_NEUI_EDITOR_H

#include <cstdint>
#include <memory>

#include <clap/clap.h>

/*
 * The neui editor. The clap gui extension in plugin-clap.cpp drives this
 * directly - no shim layer. Lifecycle follows the neui embed contract:
 * construct (session + unshown PLUGWINDOW), setParent with the host native
 * handle BEFORE show, then show. neui owns no run loop in embedded mode; on
 * mac/win the host pump services the frame, on linux the clap posix-fd and
 * timer extensions drive eventFd/pumpAndTick. The editor content itself is
 * NeuiPluginEditor, the frame's root child.
 */
namespace baconpaul::twofilters
{
struct Engine;
}

namespace baconpaul::twofilters::ui
{
struct NeuiEditor
{
    NeuiEditor(Engine &engine, const clap_host_t *clapHost);
    ~NeuiEditor();

    // False if the neui session could not be created; the editor is unusable.
    bool valid() const;

    bool setParent(void *nativeParent);
    void show();
    void hide();

    uint32_t width() const;
    uint32_t height() const;

    // Linux embed servicing. eventFd is -1 where the host pump suffices.
    int eventFd() const;
    void pumpAndTick();

    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace baconpaul::twofilters::ui

#endif // BACONPAUL_TWOFILTERS_NEUI_UI_NEUI_EDITOR_H
