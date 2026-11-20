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

#ifndef BACONPAUL_TWOFILTERS_CLAP_PRESET_DISCOVERY_IMPL_H
#define BACONPAUL_TWOFILTERS_CLAP_PRESET_DISCOVERY_IMPL_H

#include <clap/clap.h>

namespace baconpaul::twofilters
{
const clap_preset_discovery_factory_t *twoFiltersPresetDiscoveryFactory();
}
#endif
