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

#include "patch.h"
namespace baconpaul::twofilters
{

float Patch::migrateParamValueFromVersion(Param *p, float value, uint32_t version) { return value; }

void Patch::migratePatchFromVersion(uint32_t version)
{
    if (version == 1)
    {
        // version 1-> had serial mode split and no blend, so bring the
        // mode to serial with blend set for the old types.
        auto m = (int)routingNode.routingMode;
        switch (m)
        {
        case 0:
            routingNode.filterBlendSerial = 1.f;
            routingNode.routingMode = 0;
            break;

        case 1:
            routingNode.filterBlendSerial = 0.f;
            routingNode.routingMode = 0;
            break;

        default:
            routingNode.filterBlendParallel = 0.f;
            routingNode.routingMode = m - 1;
            break;
        }
    }
}

void Patch::additionalToStateImpl(TiXmlElement &root)
{
    auto fn = TiXmlElement("filter_models");
    for (int i = 0; i < numFilters; ++i)
    {
        auto &nd = filterNodes[i];
        auto tf = TiXmlElement("filter");
        tf.SetAttribute("idx", i);
        tf.SetAttribute("model", (int)nd.model);
        tf.SetAttribute("pt", (int)nd.config.pt);
        tf.SetAttribute("st", (int)nd.config.st);
        tf.SetAttribute("dt", (int)nd.config.dt);
        tf.SetAttribute("mt", (int)nd.config.mt);
        fn.InsertEndChild(tf);
    }
    root.InsertEndChild(fn);
}

void Patch::additionalFromStateImpl(TiXmlElement *root, uint32_t version)
{
    auto el = root->FirstChildElement("filter_models");
    if (el)
    {
        auto k = el->FirstChildElement("filter");
        while (k)
        {
            int idx;
            if (k->QueryIntAttribute("idx", &idx) == TIXML_SUCCESS)
            {
                auto &nd = filterNodes[idx];

                int tmp;
                k->QueryIntAttribute("model", &tmp);
                nd.model = (sst::filtersplusplus::FilterModel)tmp;

                k->QueryIntAttribute("pt", &tmp);
                nd.config.pt = (sst::filtersplusplus::Passband)tmp;

                k->QueryIntAttribute("st", &tmp);
                nd.config.st = (sst::filtersplusplus::Slope)tmp;

                k->QueryIntAttribute("dt", &tmp);
                nd.config.dt = (sst::filtersplusplus::DriveMode)tmp;

                k->QueryIntAttribute("mt", &tmp);
                nd.config.mt = (sst::filtersplusplus::FilterSubModel)tmp;
            }
            k = k->NextSiblingElement("filter");
        }
    }
}

} // namespace baconpaul::twofilters