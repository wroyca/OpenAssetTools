#include "Game/T5/Maps/ComWorldDumperT5.h"

#include "Game/T5/Bsp/BspT5.h"
#include "Game/T5/Maps/BspWriterT5.h"
#include "SearchPath/MockOutputPath.h"
#include "SearchPath/MockSearchPath.h"

#include <catch2/catch_test_macros.hpp>
#include <sstream>

using namespace T5;

namespace
{
    constexpr auto MAP_NAME = "maps/mp/mp_test.d3dbsp";

    TEST_CASE("ComWorld dumper writes lights, water and burnable lumps (T5)", "[t5][comworld]")
    {
        ComPrimaryLight lights[2]{};
        auto& spot = lights[1];
        spot.type = 2;
        spot.exponent = 4;
        spot.cullDist = 300;
        spot.radius = 110.0f;
        spot.falloff[0] = 10.0f;
        spot.falloff[1] = 110.0f;
        spot.falloff[2] = 60.0f;
        spot.falloff[3] = 35.0f;
        spot.diffuseColor[0] = 0.5f;
        spot.angle[3] = 2.0f;
        spot.defName = "light_dynamic";

        ComWaterCell waterCell{};
        waterCell.waterheight = 12;
        waterCell.color[1] = 5;

        ComBurnableSample samples[32]{};
        samples[3].state = 6;
        ComBurnableCell burnableCell{4, 5, samples};

        ComWorld comWorld{};
        comWorld.name = MAP_NAME;
        comWorld.isInUse = 1;
        comWorld.primaryLightCount = 2u;
        comWorld.primaryLights = lights;
        comWorld.waterHeader = ComWaterHeader{1, 2, 3, 4};
        comWorld.numWaterCells = 1u;
        comWorld.waterCells = &waterCell;
        comWorld.numBurnableCells = 1u;
        comWorld.burnableCells = &burnableCell;

        Zone zone("DumpingZone", 0, GameId::T5, GamePlatform::PC);
        zone.m_pools.AddAsset(std::make_unique<XAssetInfo<ComWorld>>(ASSET_TYPE_COMWORLD,
                                                                     MAP_NAME,
                                                                     &comWorld,
                                                                     std::vector<XAssetInfoGeneric*>(),
                                                                     std::vector<scr_string_t>(),
                                                                     std::vector<IndirectAssetReference>(),
                                                                     &zone));

        MockSearchPath objPath;
        MockOutputPath output;
        AssetDumpingContext context(zone, "", output, objPath, std::nullopt);
        com_world::DumperT5 dumper;
        dumper.Dump(context);
        bsp::BspWriterT5 writer;
        writer.Dump(context);

        const auto* file = output.GetMockedFile(MAP_NAME);
        REQUIRE(file != nullptr);

        std::istringstream stream(file->AsString());
        BspFile bsp;
        REQUIRE(bsp.Read(stream));

        const auto diskLights = bsp.GetLump<DiskPrimaryLight>(LUMP_PRIMARY_LIGHTS);
        REQUIRE(diskLights);
        REQUIRE(diskLights->size() == 2u);
        const auto& diskSpot = (*diskLights)[1];
        REQUIRE(diskSpot.type == 2u);
        REQUIRE(diskSpot.exponent == 4);
        REQUIRE(diskSpot.cullDist == 300u);
        REQUIRE(diskSpot.color[0] == 0.5f);
        REQUIRE(diskSpot.falloffStart == 10.0f);
        REQUIRE(diskSpot.radius == 110.0f);
        REQUIRE(diskSpot.falloffLerp[0] == 0.5f);
        REQUIRE(diskSpot.falloffLerp[1] == 0.25f);
        REQUIRE(diskSpot.angleW == 2.0f);
        REQUIRE(std::string(diskSpot.defName) == "light_dynamic");

        const auto waterHeaders = bsp.GetLump<DiskWaterHeader>(LUMP_WATERHEADER);
        REQUIRE(waterHeaders);
        REQUIRE(waterHeaders->size() == 1u);
        REQUIRE((*waterHeaders)[0].maxy == 4);
        const auto waterCells = bsp.GetLump<DiskWaterCell>(LUMP_WATERCELLS);
        REQUIRE(waterCells);
        REQUIRE(waterCells->size() == 1u);
        REQUIRE((*waterCells)[0].waterheight == 12);
        REQUIRE((*waterCells)[0].color[1] == 5u);

        REQUIRE(bsp.HasLump(LUMP_BURNABLEHEADER));
        const auto burnableCells = bsp.GetLump<DiskBurnableCell>(LUMP_BURNABLECELLS);
        REQUIRE(burnableCells);
        REQUIRE(burnableCells->size() == 1u);
        REQUIRE((*burnableCells)[0].x == 4);
        REQUIRE((*burnableCells)[0].dataIndex == 0);
        const auto blocks = bsp.GetLump<DiskBurnableBlock>(LUMP_BURNABLECELLDATA);
        REQUIRE(blocks);
        REQUIRE((*blocks)[0].states[3] == 6u);
    }

    TEST_CASE("ComWorld dumper skips empty water and burnable data (T5)", "[t5][comworld]")
    {
        ComPrimaryLight lights[2]{};

        ComWorld comWorld{};
        comWorld.name = MAP_NAME;
        comWorld.primaryLightCount = 2u;
        comWorld.primaryLights = lights;

        Zone zone("DumpingZone", 0, GameId::T5, GamePlatform::PC);
        zone.m_pools.AddAsset(std::make_unique<XAssetInfo<ComWorld>>(ASSET_TYPE_COMWORLD,
                                                                     MAP_NAME,
                                                                     &comWorld,
                                                                     std::vector<XAssetInfoGeneric*>(),
                                                                     std::vector<scr_string_t>(),
                                                                     std::vector<IndirectAssetReference>(),
                                                                     &zone));

        MockSearchPath objPath;
        MockOutputPath output;
        AssetDumpingContext context(zone, "", output, objPath, std::nullopt);
        com_world::DumperT5 dumper;
        dumper.Dump(context);
        bsp::BspWriterT5 writer;
        writer.Dump(context);

        const auto* file = output.GetMockedFile(MAP_NAME);
        REQUIRE(file != nullptr);

        std::istringstream stream(file->AsString());
        BspFile bsp;
        REQUIRE(bsp.Read(stream));
        REQUIRE(bsp.HasLump(LUMP_PRIMARY_LIGHTS));
        REQUIRE(!bsp.HasLump(LUMP_WATERHEADER));
        REQUIRE(!bsp.HasLump(LUMP_BURNABLEHEADER));
    }
} // namespace
