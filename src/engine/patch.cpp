/*
 * SideQuest Starting Point
 *
 * Basically lets paul bootstrap his projects.
 *
 * Copyright 2024-2025, Paul Walker and Various authors, as described in the github
 * transaction log.
 *
 * This source repo is released under the MIT license, but has
 * GPL3 dependencies, as such the combined work will be
 * released under GPL3.
 *
 * The source code and license are at https://github.com/baconpaul/sidequest-startingpoint
 */

#include "patch.h"
namespace baconpaul::sidequest_ns
{

float Patch::migrateParamValueFromVersion(Param *p, float value, uint32_t version) { return value; }

void Patch::migratePatchFromVersion(uint32_t version) {}

} // namespace baconpaul::sidequest_ns