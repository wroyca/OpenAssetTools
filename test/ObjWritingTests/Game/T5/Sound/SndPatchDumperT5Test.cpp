#include "Game/T5/Sound/SndPatchDumperT5.h"

#include "Game/T5/CommonT5.h"
#include "SearchPath/MockOutputPath.h"
#include "SearchPath/MockSearchPath.h"

#include <catch2/catch_test_macros.hpp>

using namespace T5;

namespace
{
    TEST_CASE("SndPatch dumper writes one row per patched field (T5)", "[t5][soundpatch][system]")
    {
        snd_alias_t alias{};
        alias.name = "test_beep";
        alias.id = Common::SND_HashName("test_beep");

        snd_alias_list_t aliasList{};
        aliasList.name = alias.name;
        aliasList.id = alias.id;
        aliasList.head = &alias;
        aliasList.count = 1;

        SndBank bank{};
        bank.name = "test_bank.all";
        bank.aliasCount = 1;
        bank.alias = &aliasList;

        unsigned elements[]{
            2u,
            Common::SND_HashName("test_beep"),
            (0u << 16) | 91u,
            (45u << 16) | 1u,
            1u,
            0x1234abcdu,
            (5u << 16) | 750u,
            (5u << 16) | 1u,
            Common::SND_HashName("default"),
            (36u << 16) | 40u,
            (8u << 16) | 1u,
            Common::SND_HashName("tv_loud"),
            (70u << 16) | 5u,
        };

        SndPatch patch{};
        patch.name = "test_patch";
        patch.elementCount = std::extent_v<decltype(elements)>;
        patch.elements = elements;

        Zone zone("DumpingZone", 0, GameId::T5, GamePlatform::PC);
        zone.m_pools.AddAsset(std::make_unique<XAssetInfo<SndBank>>(
            ASSET_TYPE_SOUND, bank.name, &bank, std::vector<XAssetInfoGeneric*>(), std::vector<scr_string_t>(), std::vector<IndirectAssetReference>(), &zone));
        zone.m_pools.AddAsset(std::make_unique<XAssetInfo<SndPatch>>(ASSET_TYPE_SOUND_PATCH,
                                                                     patch.name,
                                                                     &patch,
                                                                     std::vector<XAssetInfoGeneric*>(),
                                                                     std::vector<scr_string_t>(),
                                                                     std::vector<IndirectAssetReference>(),
                                                                     &zone));

        MockSearchPath objPath;
        MockOutputPath output;
        AssetDumpingContext context(zone, "", output, objPath, std::nullopt);
        sound::SndPatchDumperT5 dumper;
        dumper.Dump(context);

        const auto* file = output.GetMockedFile("soundpatch/test_patch.csv");
        REQUIRE(file != nullptr);
        REQUIRE(file->AsString()
                == "table,name,field,value\n"
                   "alias,test_beep,vol_min,91\n"
                   "alias,test_beep,spatialized,1\n"
                   "alias,@1234abcd,dist_max,750\n"
                   "snapshot,default,ambience,40\n"
                   "master,tv_loud,70,5\n");
    }
} // namespace
