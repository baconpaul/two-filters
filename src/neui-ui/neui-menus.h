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

#ifndef BACONPAUL_TWOFILTERS_NEUI_UI_NEUI_MENUS_H
#define BACONPAUL_TWOFILTERS_NEUI_UI_NEUI_MENUS_H

#include <neuiplusplus/neuiplusplus.h>
#include <neuiplusplus/components/PopupMenu.h>

/*
 * Menus come from neuiplusplus' client-drawn PopupMenu (bold headers plus a
 * real type-in row, which the native tree popup can't do). The controller
 * lives on the frame - NeuiEditor::Impl owns it and hands the editor a
 * pointer - and menus open in-frame, editor-local coordinates.
 */
namespace baconpaul::twofilters::ui
{
namespace npp = neuiplusplus;

inline npp::MenuStyle makeMenuStyle()
{
    npp::MenuStyle s;
    s.background = npp::Color::rgb(0x25, 0x25, 0x28).brighter(0.05f);
    s.border = npp::Color::rgb(0x70, 0x70, 0x70);
    s.text = npp::Color::rgb(220, 220, 220);
    s.textDisabled = npp::Color::rgb(220, 220, 220).withAlpha(0.4f);
    s.headerText = npp::Color::rgb(0xFF, 0x90, 0x00);
    s.highlight = npp::Color::rgb(0x35, 0x30, 0x25);
    s.separator = npp::Color::rgb(0x50, 0x50, 0x50);
    return s;
}

/*
 * A component's origin in editor coordinates - menus anchor under widgets,
 * and npp bounds are parent-relative.
 */
inline npp::Point editorLocalOrigin(const npp::ComponentCore *c, const npp::ComponentCore *editor)
{
    npp::Point p{0, 0};
    while (c && c != editor)
    {
        auto b = c->bounds();
        p = p + b.getTopLeft();
        c = c->parent();
    }
    return p;
}

} // namespace baconpaul::twofilters::ui
#endif // BACONPAUL_TWOFILTERS_NEUI_UI_NEUI_MENUS_H
