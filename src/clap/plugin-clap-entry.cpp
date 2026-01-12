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

#include <clap/clap.h>
#include "plugin-clap-entry-impl.h"

extern "C"
{

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wattributes"
#endif

    // clang-format off
    const CLAP_EXPORT struct clap_plugin_entry clap_entry = {
        CLAP_VERSION,
        baconpaul::twofilters::clap_init,
        baconpaul::twofilters::clap_deinit,
        baconpaul::twofilters::get_factory
    };
    // clang-format on
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
}