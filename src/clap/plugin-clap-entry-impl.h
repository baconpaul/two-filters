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

#ifndef BACONPAUL_TWOFILTERS_CLAP_PLUGIN_CLAP_ENTRY_IMPL_H
#define BACONPAUL_TWOFILTERS_CLAP_PLUGIN_CLAP_ENTRY_IMPL_H

namespace baconpaul::twofilters
{
const void *get_factory(const char *factory_id);
bool clap_init(const char *p);
void clap_deinit();
} // namespace baconpaul::twofilters

#endif
