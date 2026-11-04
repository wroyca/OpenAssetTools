#include "Game/T5/Maps/LoaderComWorldT5.h"

#include "Game/T5/Bsp/BspT5.h"
#include "SearchPath/MockSearchPath.h"

#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <sstream>

using namespace T5;

namespace
{
    constexpr auto MAP_NAME = "maps/mp/mp_test.d3dbsp";

    std::string WriteBsp(const BspFile& bsp)
    {
        std::stringstream stream;
        bsp.Write(stream);
        return stream.str();
    }

    std::vector<DiskPrimaryLight> CreateLights()
    {
        std::vector<DiskPrimaryLight> lights(4u);

        auto& sun = lights[1];
        sun.type = 1u;
        sun.cosHalfFovOuter = 0.5f;
        sun.cosHalfFovInner = 0.25f;
        std::strcpy(sun.defName, "ignored");

        auto& spot = lights[2];
        spot.type = 2u;
        spot.canUseShadowMap = 1u;
        spot.cullDist = 512u;
        spot.priority = 3u;
        spot.exponent = 0x102;
        spot.color[0] = 0.5f;
        spot.color[1] = 0.25f;
        spot.color[2] = 1.0f;
        spot.falloffStart = 10.0f;
        spot.radius = 110.0f;
        spot.falloffLerp[0] = 0.5f;
        spot.falloffLerp[1] = 0.25f;
        spot.cosHalfFovOuter = 0.5f;
        spot.cosHalfFovInner = 0.25f;
        spot.rotationLimit = 0.5f;
        spot.angle[0] = 1.0f;
        spot.angleW = 4.0f;
        std::strcpy(spot.defName, "light_dynamic");

        auto& omni = lights[3];
        omni.type = 3u;
        omni.cosHalfFovOuter = 0.5f;
        omni.cosHalfFovInner = 0.75f;
        omni.rotationLimit = -0.75f;
        std::strcpy(omni.defName, "light_dynamic");

        return lights;
    }

    const ComWorld* LoadComWorld(Zone& zone, MockSearchPath& searchPath, AssetCreationResult* result = nullptr)
    {
        AssetCreatorCollection creatorCollection(zone);
        IgnoredAssetLookup ignoredAssetLookup;
        AssetCreationContext context(zone, &creatorCollection, &ignoredAssetLookup);

        const auto loader = com_world::CreateLoaderT5(zone.Memory(), searchPath);
        const auto creationResult = loader->CreateAsset(MAP_NAME, context);
        if (result)
            *result = creationResult;
        if (!creationResult.HasBeenSuccessful())
            return nullptr;

        return reinterpret_cast<XAssetInfo<ComWorld>*>(creationResult.GetAssetInfo())->Asset();
    }

    TEST_CASE("ComWorld loader derives primary light fields (T5)", "[t5][comworld]")
    {
        BspFile bsp;
        bsp.SetLump(LUMP_PRIMARY_LIGHTS, CreateLights());

        MockSearchPath searchPath;
        searchPath.AddFileData(MAP_NAME, WriteBsp(bsp));

        Zone zone("LoadingZone", 0, GameId::T5, GamePlatform::PC);
        const auto* comWorld = LoadComWorld(zone, searchPath);
        REQUIRE(comWorld);

        REQUIRE(std::string(comWorld->name) == MAP_NAME);
        REQUIRE(comWorld->isInUse == 1);
        REQUIRE(comWorld->primaryLightCount == 4u);
        REQUIRE(comWorld->numWaterCells == 0u);
        REQUIRE(comWorld->numBurnableCells == 0u);

        const auto& sun = comWorld->primaryLights[1];
        REQUIRE(sun.defName == nullptr);
        REQUIRE(sun.cosHalfFovInner == 0.25f);
        REQUIRE(sun.cosHalfFovExpanded == 0.5f);

        const auto& spot = comWorld->primaryLights[2];
        REQUIRE(spot.type == 2);
        REQUIRE(spot.canUseShadowMap == 1);
        REQUIRE(spot.cullDist == 512);
        REQUIRE(spot.priority == 3);
        REQUIRE(spot.exponent == 2);
        REQUIRE(spot.color[1] == 0.25f);
        REQUIRE(spot.diffuseColor[2] == 1.0f);
        REQUIRE(spot.diffuseColor[3] == 0.0f);
        REQUIRE(spot.radius == 110.0f);
        REQUIRE(spot.falloff[0] == 10.0f);
        REQUIRE(spot.falloff[1] == 110.0f);
        REQUIRE(spot.falloff[2] == 60.0f);
        REQUIRE(spot.falloff[3] == 35.0f);
        REQUIRE(spot.cosHalfFovInner == 0.625f);
        REQUIRE(spot.cosHalfFovExpanded == -0.5f);
        REQUIRE(spot.angle[0] == 1.0f);
        REQUIRE(spot.angle[3] == 4.0f);
        REQUIRE(std::string(spot.defName) == "light_dynamic");

        const auto& omni = comWorld->primaryLights[3];
        REQUIRE(omni.cosHalfFovInner == 0.75f);
        REQUIRE(omni.cosHalfFovExpanded == -1.0f);
        REQUIRE(omni.defName == spot.defName);
    }

    TEST_CASE("ComWorld loader reads water and sorts burnable cells (T5)", "[t5][comworld]")
    {
        const DiskWaterHeader waterHeader{-2, -3, 4, 5, 0};
        const std::vector<DiskWaterCell> waterCells{
            {16, 1u, 2u, {10u, 20u, 30u, 40u}}
        };
        const DiskBurnableHeader burnableHeader{0, 0, 8, 8};
        const std::vector<DiskBurnableCell> burnableCells{
            {5, 2, 0, 0},
            {1, 2, 0, 1},
            {9, 1, 0, 0}
        };
        std::vector<DiskBurnableBlock> blocks(2u);
        blocks[0].states[0] = 7u;
        blocks[1].states[31] = 9u;

        BspFile bsp;
        bsp.SetLump(LUMP_PRIMARY_LIGHTS, CreateLights());
        bsp.SetLump(LUMP_WATERHEADER, &waterHeader, 1u);
        bsp.SetLump(LUMP_WATERCELLS, waterCells);
        bsp.SetLump(LUMP_BURNABLEHEADER, &burnableHeader, 1u);
        bsp.SetLump(LUMP_BURNABLECELLS, burnableCells);
        bsp.SetLump(LUMP_BURNABLECELLDATA, blocks);

        MockSearchPath searchPath;
        searchPath.AddFileData(MAP_NAME, WriteBsp(bsp));

        Zone zone("LoadingZone", 0, GameId::T5, GamePlatform::PC);
        const auto* comWorld = LoadComWorld(zone, searchPath);
        REQUIRE(comWorld);

        REQUIRE(comWorld->waterHeader.minx == -2);
        REQUIRE(comWorld->waterHeader.maxy == 5);
        REQUIRE(comWorld->numWaterCells == 1u);
        REQUIRE(comWorld->waterCells[0].waterheight == 16);
        REQUIRE(comWorld->waterCells[0].shoredist == 2);
        REQUIRE(comWorld->waterCells[0].color[3] == 40);

        REQUIRE(comWorld->burnableHeader.maxx == 8);
        REQUIRE(comWorld->numBurnableCells == 3u);
        REQUIRE(comWorld->burnableCells[0].x == 9);
        REQUIRE(comWorld->burnableCells[0].data[0].state == 7);
        REQUIRE(comWorld->burnableCells[1].x == 1);
        REQUIRE(comWorld->burnableCells[1].data[31].state == 9);
        REQUIRE(comWorld->burnableCells[2].x == 5);
        REQUIRE(comWorld->burnableCells[2].data != comWorld->burnableCells[0].data);
    }

    TEST_CASE("ComWorld loader fails without primary lights (T5)", "[t5][comworld]")
    {
        BspFile bsp;
        bsp.SetLump(LUMP_PRIMARY_LIGHTS, std::vector<DiskPrimaryLight>(1u));

        MockSearchPath searchPath;
        searchPath.AddFileData(MAP_NAME, WriteBsp(bsp));

        Zone zone("LoadingZone", 0, GameId::T5, GamePlatform::PC);
        auto result = AssetCreationResult::NoAction();
        REQUIRE(!LoadComWorld(zone, searchPath, &result));
        REQUIRE(result.HasTakenAction());
    }

    TEST_CASE("ComWorld loader takes no action without a bsp (T5)", "[t5][comworld]")
    {
        MockSearchPath searchPath;
        Zone zone("LoadingZone", 0, GameId::T5, GamePlatform::PC);
        auto result = AssetCreationResult::Success(nullptr);

        REQUIRE(!LoadComWorld(zone, searchPath, &result));
        REQUIRE(!result.HasTakenAction());
    }
} // namespace
