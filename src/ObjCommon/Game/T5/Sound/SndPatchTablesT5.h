#pragma once

#include "Game/T5/Sound/SndAliasFieldsT5.h"
#include "Game/T5/SoundConstantsT5.h"

#include <cstddef>
#include <type_traits>

namespace T5
{
    inline constexpr const char* SND_GROUP_FIELD_NAMES[]{"name", "name", "attenuationSp", "attenuationMp", "category", "parent"};

    inline constexpr const char* SND_CURVE_FIELD_NAMES[]{
        "name",
        "name",
        "x0",
        "y0",
        "x1",
        "y1",
        "x2",
        "y2",
        "x3",
        "y3",
        "x4",
        "y4",
        "x5",
        "y5",
        "x6",
        "y6",
        "x7",
        "y7",
    };

    inline constexpr const char* SND_PAN_FIELD_NAMES[]{"name", "name", "front", "back", "center", "lfe", "left", "right"};

    inline constexpr const char* SND_SNAPSHOT_GROUP_FIELD_NAMES[]{"name"};

    inline constexpr const char* SND_SNAPSHOT_FIELD_NAMES[]{
        "name",
        "name",
        "occlusion",
        "occlusion",
        "fadeIn",
        "fadeInCurve",
        "fadeOut",
        "fadeOutCurve",
        "distance",
    };

    inline constexpr const char* SND_CONTEXT_FIELD_NAMES[]{"type", "value0", "value1", "value2", "value3", "value4", "value5", "value6", "value7"};

    inline constexpr const char* SND_RADVERB_FIELD_NAMES[]{
        "name",
        "name",
        "smoothing",
        "earlyTime",
        "lateTime",
        "earlyGain",
        "lateGain",
        "earlyLpf",
        "lateLpf",
        "inputLpf",
        "dampLpf",
        "wallReflect",
        "dryGain",
        "earlySize",
        "lateSize",
        "diffusion",
        "returnGain",
    };

    inline constexpr const char* SND_MASTER_FIELD_NAMES[]{
        "name",   "name",   "notchE", "notchG", "notchF", "notchQ",  "lowE",    "lowG",   "lowF",   "lowQ",    "peak1E",  "peak1G", "peak1F",
        "peak1Q", "peak2E", "peak2G", "peak2F", "peak2Q", "hiE",     "hiG",     "hiF",    "hiQ",    "eqG",     "compE",   "compPG", "compMG",
        "compT",  "compR",  "compTA", "compTR", "limitE", "limitPG", "limitMG", "limitT", "limitR", "limitTA", "limitTR",
    };

    struct SndPatchTable
    {
        const char* name;
        const char* const* fieldNames;
        size_t fieldNameCount;
        const char* const* extraFieldNames;
        size_t extraFieldNameCount;
        const char* const* rowNames;
        size_t rowNameCount;
    };

    inline constexpr const char* SND_ALIAS_FIELD_NAMES[]{
        SND_ALIAS_FIELDS[0].name,  SND_ALIAS_FIELDS[1].name,  SND_ALIAS_FIELDS[2].name,  SND_ALIAS_FIELDS[3].name,  SND_ALIAS_FIELDS[4].name,
        SND_ALIAS_FIELDS[5].name,  SND_ALIAS_FIELDS[6].name,  SND_ALIAS_FIELDS[7].name,  SND_ALIAS_FIELDS[8].name,  SND_ALIAS_FIELDS[9].name,
        SND_ALIAS_FIELDS[10].name, SND_ALIAS_FIELDS[11].name, SND_ALIAS_FIELDS[12].name, SND_ALIAS_FIELDS[13].name, SND_ALIAS_FIELDS[14].name,
        SND_ALIAS_FIELDS[15].name, SND_ALIAS_FIELDS[16].name, SND_ALIAS_FIELDS[17].name, SND_ALIAS_FIELDS[18].name, SND_ALIAS_FIELDS[19].name,
        SND_ALIAS_FIELDS[20].name, SND_ALIAS_FIELDS[21].name, SND_ALIAS_FIELDS[22].name, SND_ALIAS_FIELDS[23].name, SND_ALIAS_FIELDS[24].name,
        SND_ALIAS_FIELDS[25].name, SND_ALIAS_FIELDS[26].name, SND_ALIAS_FIELDS[27].name, SND_ALIAS_FIELDS[28].name, SND_ALIAS_FIELDS[29].name,
        SND_ALIAS_FIELDS[30].name, SND_ALIAS_FIELDS[31].name, SND_ALIAS_FIELDS[32].name, SND_ALIAS_FIELDS[33].name, SND_ALIAS_FIELDS[34].name,
        SND_ALIAS_FIELDS[35].name, SND_ALIAS_FIELDS[36].name, SND_ALIAS_FIELDS[37].name, SND_ALIAS_FIELDS[38].name, SND_ALIAS_FIELDS[39].name,
        SND_ALIAS_FIELDS[40].name, SND_ALIAS_FIELDS[41].name, SND_ALIAS_FIELDS[42].name, SND_ALIAS_FIELDS[43].name, SND_ALIAS_FIELDS[44].name,
        SND_ALIAS_FIELDS[45].name, SND_ALIAS_FIELDS[46].name, SND_ALIAS_FIELDS[47].name, SND_ALIAS_FIELDS[48].name, SND_ALIAS_FIELDS[49].name,
        SND_ALIAS_FIELDS[50].name, SND_ALIAS_FIELDS[51].name, SND_ALIAS_FIELDS[52].name,
    };

    static_assert(std::extent_v<decltype(SND_ALIAS_FIELD_NAMES)> == std::extent_v<decltype(SND_ALIAS_FIELDS)>);

    inline constexpr SndPatchTable SND_PATCH_TABLES[]{
        {"alias",         SND_ALIAS_FIELD_NAMES,          std::extent_v<decltype(SND_ALIAS_FIELD_NAMES)>,          nullptr, 0,                                              nullptr,       0                                           },
        {"group",         SND_GROUP_FIELD_NAMES,          std::extent_v<decltype(SND_GROUP_FIELD_NAMES)>,          nullptr, 0,                                              SOUND_GROUPS,  std::extent_v<decltype(SOUND_GROUPS)>       },
        {"curve",         SND_CURVE_FIELD_NAMES,          std::extent_v<decltype(SND_CURVE_FIELD_NAMES)>,          nullptr, 0,                                              SOUND_CURVES,  std::extent_v<decltype(SOUND_CURVES)>       },
        {"pan",           SND_PAN_FIELD_NAMES,            std::extent_v<decltype(SND_PAN_FIELD_NAMES)>,            nullptr, 0,                                              SOUND_PANS,    std::extent_v<decltype(SOUND_PANS)>         },
        {"snapshotgroup", SND_SNAPSHOT_GROUP_FIELD_NAMES, std::extent_v<decltype(SND_SNAPSHOT_GROUP_FIELD_NAMES)>, nullptr, 0,                                              nullptr,       0                                           },
        {"snapshot",
         SND_SNAPSHOT_FIELD_NAMES,                        std::extent_v<decltype(SND_SNAPSHOT_FIELD_NAMES)>,
         SOUND_SNAPSHOT_GROUPS,                                                                                             std::extent_v<decltype(SOUND_SNAPSHOT_GROUPS)>,
         SOUND_SNAPSHOTS,                                                                                                                                                                  std::extent_v<decltype(SOUND_SNAPSHOTS)>    },
        {"context",
         SND_CONTEXT_FIELD_NAMES,                         std::extent_v<decltype(SND_CONTEXT_FIELD_NAMES)>,
         nullptr,                                                                                                           0,
         SOUND_CONTEXT_TYPES,                                                                                                                                                              std::extent_v<decltype(SOUND_CONTEXT_TYPES)>},
        {"radverb",       SND_RADVERB_FIELD_NAMES,        std::extent_v<decltype(SND_RADVERB_FIELD_NAMES)>,        nullptr, 0,                                              nullptr,       0                                           },
        {"master",        SND_MASTER_FIELD_NAMES,         std::extent_v<decltype(SND_MASTER_FIELD_NAMES)>,         nullptr, 0,                                              SOUND_MASTERS, std::extent_v<decltype(SOUND_MASTERS)>      },
    };
} // namespace T5
