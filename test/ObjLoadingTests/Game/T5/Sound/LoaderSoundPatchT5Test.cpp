#include "Game/T5/Sound/LoaderSoundPatchT5.h"

#include "Game/T5/CommonT5.h"
#include "SearchPath/MockSearchPath.h"

#include <catch2/catch_test_macros.hpp>

using namespace T5;

namespace
{
    TEST_CASE("SoundPatch loader groups rows into patch records (T5)", "[t5][soundpatch][system]")
    {
        MockSearchPath searchPath;
        searchPath.AddFileData("soundpatch/test_patch.csv",
                               "table,name,field,value\n"
                               "alias,test_beep,vol_min,91\n"
                               "alias,test_beep,spatialized,1\n"
                               "alias,@1234abcd,dist_max,750\n"
                               "group,ui,attenuationMp,80\n"
                               "snapshot,default,ambience,40\n"
                               "master,tv_loud,70,5\n");

        Zone zone("LoadingZone", 0, GameId::T5, GamePlatform::PC);
        AssetCreatorCollection creatorCollection(zone);
        IgnoredAssetLookup ignoredAssetLookup;
        AssetCreationContext context(zone, &creatorCollection, &ignoredAssetLookup);
        const auto loader = sound::CreateSoundPatchLoaderT5(zone.Memory(), searchPath);
        const auto result = loader->CreateAsset("test_patch", context);

        REQUIRE(result.HasBeenSuccessful());
        const auto* patch = reinterpret_cast<XAssetInfo<SndPatch>*>(result.GetAssetInfo())->Asset();

        const unsigned expected[]{
            2u,
            Common::SND_HashName("test_beep"),
            (0u << 16) | 91u,
            (45u << 16) | 1u,
            1u,
            0x1234abcdu,
            (5u << 16) | 750u,
            (1u << 16) | 1u,
            Common::SND_HashName("ui"),
            (3u << 16) | 80u,
            (5u << 16) | 1u,
            Common::SND_HashName("default"),
            (36u << 16) | 40u,
            (8u << 16) | 1u,
            Common::SND_HashName("tv_loud"),
            (70u << 16) | 5u,
        };

        REQUIRE(std::string(patch->name) == "test_patch");
        REQUIRE(patch->elementCount == std::extent_v<decltype(expected)>);
        for (auto i = 0u; i < patch->elementCount; i++)
            REQUIRE(patch->elements[i] == expected[i]);
        REQUIRE(patch->fileCount == 0u);
    }

    TEST_CASE("SoundPatch loader rejects an unknown table (T5)", "[t5][soundpatch]")
    {
        MockSearchPath searchPath;
        searchPath.AddFileData("soundpatch/bad_patch.csv", "table,name,field,value\nweapon,test,damage,5\n");

        Zone zone("LoadingZone", 0, GameId::T5, GamePlatform::PC);
        AssetCreatorCollection creatorCollection(zone);
        IgnoredAssetLookup ignoredAssetLookup;
        AssetCreationContext context(zone, &creatorCollection, &ignoredAssetLookup);
        const auto loader = sound::CreateSoundPatchLoaderT5(zone.Memory(), searchPath);

        REQUIRE(!loader->CreateAsset("bad_patch", context).HasBeenSuccessful());
    }
} // namespace
