#include "Game/T5/Maps/MapEntsLoaderT5.h"

#include "SearchPath/MockSearchPath.h"

#include <catch2/catch_test_macros.hpp>
#include <cstring>

using namespace T5;

namespace
{
    TEST_CASE("MapEnts loader reads the entity string (T5)", "[t5][mapents]")
    {
        constexpr auto ENTITIES = "{\n\"classname\" \"worldspawn\"\n}\n";

        MockSearchPath searchPath;
        searchPath.AddFileData("maps/mp/mp_test.d3dbsp.ents", ENTITIES);

        Zone zone("LoadingZone", 0, GameId::T5, GamePlatform::PC);
        AssetCreatorCollection creatorCollection(zone);
        IgnoredAssetLookup ignoredAssetLookup;
        AssetCreationContext context(zone, &creatorCollection, &ignoredAssetLookup);
        const auto loader = map_ents::CreateLoaderT5(zone.Memory(), searchPath);
        const auto result = loader->CreateAsset("maps/mp/mp_test.d3dbsp", context);

        REQUIRE(result.HasBeenSuccessful());
        const auto* mapEnts = reinterpret_cast<XAssetInfo<MapEnts>*>(result.GetAssetInfo())->Asset();

        REQUIRE(std::string(mapEnts->name) == "maps/mp/mp_test.d3dbsp");
        REQUIRE(mapEnts->numEntityChars == static_cast<int>(std::strlen(ENTITIES) + 1));
        REQUIRE(std::string(mapEnts->entityString) == ENTITIES);
    }

    TEST_CASE("MapEnts loader takes no action without an entity file (T5)", "[t5][mapents]")
    {
        MockSearchPath searchPath;

        Zone zone("LoadingZone", 0, GameId::T5, GamePlatform::PC);
        AssetCreatorCollection creatorCollection(zone);
        IgnoredAssetLookup ignoredAssetLookup;
        AssetCreationContext context(zone, &creatorCollection, &ignoredAssetLookup);
        const auto loader = map_ents::CreateLoaderT5(zone.Memory(), searchPath);
        const auto result = loader->CreateAsset("maps/mp/mp_missing.d3dbsp", context);

        REQUIRE(!result.HasBeenSuccessful());
        REQUIRE(!result.HasTakenAction());
    }
} // namespace
