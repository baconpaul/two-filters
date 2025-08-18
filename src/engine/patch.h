/*
 * Six Sines
 *
 * A synth with audio rate modulation.
 *
 * Copyright 2024-2025, Paul Walker and Various authors, as described in the github
 * transaction log.
 *
 * This source repo is released under the MIT license, but has
 * GPL3 dependencies, as such the combined work will be
 * released under GPL3.
 *
 * The source code and license are at https://github.com/baconpaul/six-sines
 */

#ifndef BACONPAUL_SIX_SINES_SYNTH_PATCH_H
#define BACONPAUL_SIX_SINES_SYNTH_PATCH_H

#include <vector>
#include <array>
#include <unordered_map>
#include <algorithm>
#include <clap/clap.h>
#include "configuration.h"
#include "sst/cpputils/constructors.h"
#include "sst/cpputils/active_set_overlay.h"
#include "sst/basic-blocks/params/ParamMetadata.h"
#include "sst/basic-blocks/dsp/Lag.h"
#include "sst/basic-blocks/modulators/DAHDSREnvelope.h"
#include "sst/plugininfra/patch-support/patch_base.h"


namespace baconpaul::sidequest_ns
{
namespace scpu = sst::cpputils;
namespace pats = sst::plugininfra::patch_support;
using md_t = sst::basic_blocks::params::ParamMetaData;
struct Param : pats::ParamBase, sst::cpputils::active_set_overlay<Param>::participant
{
    Param(const md_t &m) : pats::ParamBase(m) {}

    Param &operator=(const float &val)
    {
        value = val;
        return *this;
    }

    uint64_t adhocFeatures{0};
    enum AdHocFeatureValues : uint64_t
    {
    };

    bool isTemposynced() const
    {
        if (tempoSyncPartner)
            return tempoSyncPartner->value;

        return false;
    }

    Param *tempoSyncPartner{nullptr};

    sst::basic_blocks::dsp::LinearLag<float, false> lag;
};

struct Patch : pats::PatchBase<Patch, Param>
{
    static constexpr uint32_t patchVersion{9};
    static constexpr const char *id{"org.baconpaul.six-sines"};

    static constexpr uint32_t floatFlags{CLAP_PARAM_IS_AUTOMATABLE};
    static constexpr uint32_t boolFlags{CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED};

    static md_t floatMd() { return md_t().asFloat().withFlags(floatFlags); }
    static md_t floatEnvRateMd()
    {
        return md_t().asFloat().withFlags(floatFlags).as25SecondExpTime();
    }
    static md_t boolMd() { return md_t().asBool().withFlags(boolFlags); }
    static md_t intMd() { return md_t().asInt().withFlags(boolFlags); }

    Patch()
        : pats::PatchBase<Patch, Param>()

    {
        auto pushParams = [this](auto &from) { this->pushMultipleParams(from.params()); };

        pushParams(sqParams);

        std::sort(params.begin(), params.end(),
                  [](const Param *a, const Param *b)
                  {
                      auto ga = a->meta.groupName;
                      auto gb = b->meta.groupName;
                      if (ga != gb)
                      {
                          if (ga == "Main")
                              return true;
                          if (gb == "Main")
                              return false;

                          return ga < gb;
                      }

                      auto an = a->meta.name;
                      auto bn = b->meta.name;
                      auto ane = an.find("Env ") != std::string::npos;
                      auto bne = bn.find("Env ") != std::string::npos;

                      if (ane != bne)
                      {
                          if (ane)
                              return false;

                          return true;
                      }
                      if (ane && bne)
                          return a->meta.id < b->meta.id;

                      return a->meta.name < b->meta.name;
                  });
    }

    struct SidequestNode
    {
        static constexpr uint32_t idBase{500};

        SidequestNode()
            :
              pitch(floatMd()
                        .asMIDIPitch().withDefault(0).withName("Pitch Offset").withID(id(0))),
              level(floatMd()
                           .asCubicDecibelAttenuation()
                           .withDefault(0.f)
                           .withName("Level").withID(id(1)))

        {
        }

        std::string name() const { return "SideQuest"; }
        uint32_t id(int f) const { return idBase + f; }

        Param pitch, level;

        std::vector<Param *> params()
        {
            std::vector<Param *> res{&pitch,
                                     &level};
            return res;
        }
    };

    SidequestNode sqParams;

    char name[256]{"Init"};

    float migrateParamValueFromVersion(Param *p, float value, uint32_t version);
    void migratePatchFromVersion(uint32_t version);
};
} // namespace baconpaul::six_sines
#endif // PATCH_H
