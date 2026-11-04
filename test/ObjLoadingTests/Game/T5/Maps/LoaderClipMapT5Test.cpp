#include "Game/T5/Maps/LoaderClipMapT5.h"

#include "Game/T5/Bsp/BspT5.h"
#include "Game/T5/Maps/MapEntsLoaderT5.h"
#include "SearchPath/MockSearchPath.h"

#include <catch2/catch_test_macros.hpp>
#include <sstream>

using namespace T5;

namespace
{
    constexpr auto MAP_NAME = "maps/mp/mp_test.d3dbsp";

    std::string CreateBsp(const std::string& entities)
    {
        const std::vector<dleaf_t> leafs{{}};
        std::vector<dmodel_t> models(1u);
        for (auto axis = 0; axis < 3; axis++)
        {
            models[0].mins[axis] = -64.0f;
            models[0].maxs[axis] = 64.0f;
        }

        BspFile bsp;
        bsp.SetLump(LUMP_LEAFS, leafs);
        bsp.SetLump(LUMP_MODELS, models);
        bsp.SetLump(LUMP_ENTITIES, std::vector<uint8_t>(entities.begin(), entities.end()));

        std::stringstream stream;
        bsp.Write(stream);
        return stream.str();
    }

    const clipMap_t* LoadClipMap(Zone& zone, MockSearchPath& searchPath)
    {
        AssetCreatorCollection creatorCollection(zone);
        creatorCollection.AddAssetCreator(map_ents::CreateLoaderT5(zone.Memory(), searchPath));
        IgnoredAssetLookup ignoredAssetLookup;
        AssetCreationContext context(zone, &creatorCollection, &ignoredAssetLookup);

        const auto loader = clip_map::CreatePvsLoaderT5(zone.Memory(), searchPath, zone);
        const auto result = loader->CreateAsset(MAP_NAME, context);
        if (!result.HasBeenSuccessful())
            return nullptr;

        return reinterpret_cast<XAssetInfo<clipMap_t>*>(result.GetAssetInfo())->Asset();
    }

    TEST_CASE("ClipMap loader builds ropes from entities (T5)", "[t5][clipmap]")
    {
        constexpr auto ENTITIES = "{\n\"classname\" \"worldspawn\"\n}\n"
                                  "{\n\"classname\" \"rope\"\n\"targetname\" \"wire\"\n\"target\" \"wire_start\"\n\"target2\" \"wire_end\"\n"
                                  "\"length_scale\" \"2\"\n\"width\" \"1.5\"\n\"timeout\" \"30\"\n}\n"
                                  "{\n\"classname\" \"info_null\"\n\"targetname\" \"wire_start\"\n\"origin\" \"0 0 0\"\n}\n"
                                  "{\n\"classname\" \"info_null\"\n\"targetname\" \"wire_end\"\n\"origin\" \"3 4 0\"\n}\n";

        MockSearchPath searchPath;
        searchPath.AddFileData(MAP_NAME, CreateBsp(ENTITIES));
        searchPath.AddFileData(std::string(MAP_NAME) + ".ents", ENTITIES);

        Zone zone("LoadingZone", 0, GameId::T5, GamePlatform::PC);
        const auto* clipMap = LoadClipMap(zone, searchPath);
        REQUIRE(clipMap);

        REQUIRE(clipMap->numLeafs == 1u);
        REQUIRE(clipMap->numSubModels == 1u);
        REQUIRE(clipMap->numClusters == 1);
        REQUIRE(clipMap->clusterBytes == 8);
        REQUIRE(clipMap->numStaticModels == 0u);
        REQUIRE(clipMap->originalDynEntCount == 0u);
        REQUIRE(clipMap->dynEntCount[0] == 256u);
        REQUIRE(clipMap->dynEntCount[1] == 0u);
        REQUIRE(clipMap->dynEntDefList[0][0].physConstraints[0] == 0x1FF);

        REQUIRE(clipMap->num_constraints == 1);
        const auto& rope = clipMap->constraints[0];
        REQUIRE(rope.type == CONSTRAINT_ROPE);
        REQUIRE(zone.m_script_strings[rope.targetname] == "wire");
        REQUIRE(rope.attach_point_type1 == ATTACH_POINT_WORLD);
        REQUIRE(rope.target_index1 == -1);
        REQUIRE(rope.pos.x == 0.0f);
        REQUIRE(rope.pos2.x == 3.0f);
        REQUIRE(rope.pos2.y == 4.0f);
        REQUIRE(rope.distance == 10.0f);
        REQUIRE(rope.minAngle == 1.5f);
        REQUIRE(rope.timeout == 30000);
        REQUIRE(rope.rope_index == -1);
        REQUIRE(clipMap->max_ropes == 33);
    }

    TEST_CASE("ClipMap loader takes no action without a bsp (T5)", "[t5][clipmap]")
    {
        MockSearchPath searchPath;
        Zone zone("LoadingZone", 0, GameId::T5, GamePlatform::PC);

        AssetCreatorCollection creatorCollection(zone);
        IgnoredAssetLookup ignoredAssetLookup;
        AssetCreationContext context(zone, &creatorCollection, &ignoredAssetLookup);
        const auto loader = clip_map::CreatePvsLoaderT5(zone.Memory(), searchPath, zone);
        const auto result = loader->CreateAsset(MAP_NAME, context);

        REQUIRE(!result.HasBeenSuccessful());
        REQUIRE(!result.HasTakenAction());
    }
} // namespace
