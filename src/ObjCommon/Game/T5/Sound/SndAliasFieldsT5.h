#pragma once

#include "Game/T5/SoundConstantsT5.h"
#include "Game/T5/T5.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace T5
{
    enum class SndAliasFieldType : uint8_t
    {
        DBSPL,
        CENTS,
        HASH,
        ENUM_BITS,
        FLAG_BITS,
        BYTE,
        ENUM_BYTE,
        USHORT,
        NORM_BYTE,
    };

    struct SndAliasField
    {
        const char* name;
        SndAliasFieldType type;
        size_t offset;
        unsigned bitCount;
        unsigned bitShift;
        const char* const* enumValues;
        size_t enumValueCount;
    };

    inline constexpr SndAliasField SND_ALIAS_FIELDS[]{
        {"vol_min",                  SndAliasFieldType::DBSPL,     offsetof(snd_alias_t, volMin),               0, 0,  nullptr,             0                                               },
        {"vol_max",                  SndAliasFieldType::DBSPL,     offsetof(snd_alias_t, volMax),               0, 0,  nullptr,             0                                               },
        {"pitch_min",                SndAliasFieldType::CENTS,     offsetof(snd_alias_t, pitchMin),             0, 0,  nullptr,             0                                               },
        {"pitch_max",                SndAliasFieldType::CENTS,     offsetof(snd_alias_t, pitchMax),             0, 0,  nullptr,             0                                               },
        {"dist_min",                 SndAliasFieldType::USHORT,    offsetof(snd_alias_t, distMin),              0, 0,  nullptr,             0                                               },
        {"dist_max",                 SndAliasFieldType::USHORT,    offsetof(snd_alias_t, distMax),              0, 0,  nullptr,             0                                               },
        {"dist_reverb_max",          SndAliasFieldType::USHORT,    offsetof(snd_alias_t, distReverbMax),        0, 0,  nullptr,             0                                               },
        {"probability",              SndAliasFieldType::NORM_BYTE, offsetof(snd_alias_t, probability),          0, 0,  nullptr,             0                                               },
        {"envelop_min",              SndAliasFieldType::USHORT,    offsetof(snd_alias_t, envelopMin),           0, 0,  nullptr,             0                                               },
        {"envelop_max",              SndAliasFieldType::USHORT,    offsetof(snd_alias_t, envelopMax),           0, 0,  nullptr,             0                                               },
        {"envelop_percentage",       SndAliasFieldType::DBSPL,     offsetof(snd_alias_t, envelopPercentage),    0, 0,  nullptr,             0                                               },
        {"center_send",              SndAliasFieldType::DBSPL,     offsetof(snd_alias_t, centerSend),           0, 0,  nullptr,             0                                               },
        {"reverb_send",              SndAliasFieldType::DBSPL,     offsetof(snd_alias_t, reverbSend),           0, 0,  nullptr,             0                                               },
        {"start_delay",              SndAliasFieldType::USHORT,    offsetof(snd_alias_t, startDelay),           0, 0,  nullptr,             0                                               },
        {"min_priority_threshold",   SndAliasFieldType::NORM_BYTE, offsetof(snd_alias_t, minPriorityThreshold), 0, 0,  nullptr,             0                                               },
        {"max_priority_threshold",   SndAliasFieldType::NORM_BYTE, offsetof(snd_alias_t, maxPriorityThreshold), 0, 0,  nullptr,             0                                               },
        {"team_vol_mod",             SndAliasFieldType::DBSPL,     offsetof(snd_alias_t, teamVolMod),           0, 0,  nullptr,             0                                               },
        {"team_pitch_mod",           SndAliasFieldType::CENTS,     offsetof(snd_alias_t, teamPitchMod),         0, 0,  nullptr,             0                                               },
        {"occlusion_level",          SndAliasFieldType::NORM_BYTE, offsetof(snd_alias_t, occlusionLevel),       0, 0,  nullptr,             0                                               },
        {"occlusion_wet_dry",        SndAliasFieldType::NORM_BYTE, offsetof(snd_alias_t, occlusionWetDry),      0, 0,  nullptr,             0                                               },
        {"move_time",                SndAliasFieldType::USHORT,    offsetof(snd_alias_t, fluxTime),             0, 0,  nullptr,             0                                               },
        {"duck",                     SndAliasFieldType::HASH,      offsetof(snd_alias_t, duck),                 0, 0,  SOUND_SNAPSHOTS,     std::extent_v<decltype(SOUND_SNAPSHOTS)>        },
        {"min_priority",             SndAliasFieldType::BYTE,      offsetof(snd_alias_t, minPriority),          0, 0,  nullptr,             0                                               },
        {"max_priority",             SndAliasFieldType::BYTE,      offsetof(snd_alias_t, maxPriority),          0, 0,  nullptr,             0                                               },
        {"limit_count",              SndAliasFieldType::BYTE,      offsetof(snd_alias_t, limitCount),           0, 0,  nullptr,             0                                               },
        {"entity_limit_count",       SndAliasFieldType::BYTE,      offsetof(snd_alias_t, entityLimitCount),     0, 0,  nullptr,             0                                               },
        {"volume_falloff_curve",     SndAliasFieldType::ENUM_BYTE, offsetof(snd_alias_t, dryCurve),             0, 0,  SOUND_CURVES,        std::extent_v<decltype(SOUND_CURVES)>           },
        {"reverb_falloff_curve",     SndAliasFieldType::ENUM_BYTE, offsetof(snd_alias_t, wetCurve),             0, 0,  SOUND_CURVES,        std::extent_v<decltype(SOUND_CURVES)>           },
        {"volume_min_falloff_curve",
         SndAliasFieldType::ENUM_BYTE,
         offsetof(snd_alias_t,                                                           dryMinCurve),
         0,                                                                                                        0,
         SOUND_CURVES,                                                                                                                      std::extent_v<decltype(SOUND_CURVES)>           },
        {"reverb_min_falloff_curve",
         SndAliasFieldType::ENUM_BYTE,
         offsetof(snd_alias_t,                                                           wetMinCurve),
         0,                                                                                                        0,
         SOUND_CURVES,                                                                                                                      std::extent_v<decltype(SOUND_CURVES)>           },
        {"pan",                      SndAliasFieldType::ENUM_BYTE, offsetof(snd_alias_t, pan),                  0, 0,  SOUND_PANS,          std::extent_v<decltype(SOUND_PANS)>             },
        {"snapshot",
         SndAliasFieldType::ENUM_BYTE,
         offsetof(snd_alias_t,                                                           snapshotGroup),
         0,                                                                                                        0,
         SOUND_SNAPSHOT_GROUPS,                                                                                                             std::extent_v<decltype(SOUND_SNAPSHOT_GROUPS)>  },
        {"context_type",             SndAliasFieldType::HASH,      offsetof(snd_alias_t, contextType),          0, 0,  SOUND_CONTEXT_TYPES, std::extent_v<decltype(SOUND_CONTEXT_TYPES)>    },
        {"context_value",
         SndAliasFieldType::HASH,
         offsetof(snd_alias_t,                                                           contextValue),
         0,                                                                                                        0,
         SOUND_CONTEXT_VALUES,                                                                                                              std::extent_v<decltype(SOUND_CONTEXT_VALUES)>   },
        {"limit_type",               SndAliasFieldType::ENUM_BITS, offsetof(snd_alias_t, flags),                2, 25, SOUND_LIMIT_TYPES,   std::extent_v<decltype(SOUND_LIMIT_TYPES)>      },
        {"entity_limit_type",        SndAliasFieldType::ENUM_BITS, offsetof(snd_alias_t, flags),                2, 27, SOUND_LIMIT_TYPES,   std::extent_v<decltype(SOUND_LIMIT_TYPES)>      },
        {"randomize_type",
         SndAliasFieldType::FLAG_BITS,
         offsetof(snd_alias_t,                                                           flags),
         3,                                                                                                        29,
         SOUND_RANDOMIZE_TYPES,                                                                                                             std::extent_v<decltype(SOUND_RANDOMIZE_TYPES)>  },
        {"move_type",                SndAliasFieldType::ENUM_BITS, offsetof(snd_alias_t, flags),                3, 22, SOUND_MOVE_TYPES,    std::extent_v<decltype(SOUND_MOVE_TYPES)>       },
        {"type",                     SndAliasFieldType::ENUM_BITS, offsetof(snd_alias_t, flags),                2, 14, SOUND_LOAD_TYPES,    std::extent_v<decltype(SOUND_LOAD_TYPES)>       },
        {"group",                    SndAliasFieldType::ENUM_BITS, offsetof(snd_alias_t, flags),                6, 16, SOUND_GROUPS,        std::extent_v<decltype(SOUND_GROUPS)>           },
        {"real_delay",               SndAliasFieldType::ENUM_BITS, offsetof(snd_alias_t, flags),                1, 2,  SOUND_NO_YES,        std::extent_v<decltype(SOUND_NO_YES)>           },
        {"distance_lpf",             SndAliasFieldType::ENUM_BITS, offsetof(snd_alias_t, flags),                1, 3,  SOUND_NO_YES,        std::extent_v<decltype(SOUND_NO_YES)>           },
        {"doppler",                  SndAliasFieldType::ENUM_BITS, offsetof(snd_alias_t, flags),                1, 4,  SOUND_NO_YES,        std::extent_v<decltype(SOUND_NO_YES)>           },
        {"is_big",                   SndAliasFieldType::ENUM_BITS, offsetof(snd_alias_t, flags),                1, 5,  SOUND_NO_YES,        std::extent_v<decltype(SOUND_NO_YES)>           },
        {"loop",                     SndAliasFieldType::ENUM_BITS, offsetof(snd_alias_t, flags),                1, 0,  SOUND_LOOP_TYPES,    std::extent_v<decltype(SOUND_LOOP_TYPES)>       },
        {"spatialized",
         SndAliasFieldType::ENUM_BITS,
         offsetof(snd_alias_t,                                                           flags),
         1,                                                                                                        1,
         SOUND_SPATIALIZED_TYPES,                                                                                                           std::extent_v<decltype(SOUND_SPATIALIZED_TYPES)>},
        {"futz",                     SndAliasFieldType::ENUM_BITS, offsetof(snd_alias_t, flags),                1, 6,  SOUND_NO_YES,        std::extent_v<decltype(SOUND_NO_YES)>           },
        {"music",                    SndAliasFieldType::ENUM_BITS, offsetof(snd_alias_t, flags),                1, 8,  SOUND_NO_YES,        std::extent_v<decltype(SOUND_NO_YES)>           },
        {"timescale",                SndAliasFieldType::ENUM_BITS, offsetof(snd_alias_t, flags),                1, 10, SOUND_NO_YES,        std::extent_v<decltype(SOUND_NO_YES)>           },
        {"pause",                    SndAliasFieldType::ENUM_BITS, offsetof(snd_alias_t, flags),                1, 7,  SOUND_NO_YES,        std::extent_v<decltype(SOUND_NO_YES)>           },
        {"stop_on_death",            SndAliasFieldType::ENUM_BITS, offsetof(snd_alias_t, flags),                1, 9,  SOUND_NO_YES,        std::extent_v<decltype(SOUND_NO_YES)>           },
        {"bus",                      SndAliasFieldType::ENUM_BITS, offsetof(snd_alias_t, flags),                2, 12, SOUND_BUS_IDS,       std::extent_v<decltype(SOUND_BUS_IDS)>          },
        {"voice_limit",              SndAliasFieldType::ENUM_BITS, offsetof(snd_alias_t, flags),                1, 11, SOUND_NO_YES,        std::extent_v<decltype(SOUND_NO_YES)>           },
    };
} // namespace T5
