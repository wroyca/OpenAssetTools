#include "Game/T5/Maps/GameWorldDumperT5.h"

#include "Game/T5/Bsp/BspT5.h"
#include "Game/T5/Bsp/PathConnectionsT5.h"
#include "Game/T5/Maps/BspWriterT5.h"
#include "SearchPath/MockOutputPath.h"
#include "SearchPath/MockSearchPath.h"

#include <catch2/catch_test_macros.hpp>
#include <sstream>

using namespace T5;

namespace
{
    constexpr auto MAP_NAME = "maps/test.d3dbsp";

    TEST_CASE("GameWorld dumper writes path connections (T5)", "[t5][gameworld]")
    {
        pathlink_s links[2]{};
        links[0].nodeNum = 2u;
        links[0].fDist = 32.0f;
        links[1].nodeNum = 1u;
        links[1].fDist = 16.0f;

        pathnode_t nodes[3 + 128]{};
        nodes[0].constant.totalLinkCount = 2u;
        nodes[0].constant.Links = links;

        char vis[1]{0x05};

        GameWorldSp gameWorld{};
        gameWorld.name = MAP_NAME;
        gameWorld.path.nodeCount = 3u;
        gameWorld.path.nodes = nodes;
        gameWorld.path.visBytes = 1;
        gameWorld.path.pathVis = vis;

        Zone zone("DumpingZone", 0, GameId::T5, GamePlatform::PC);
        zone.m_pools.AddAsset(std::make_unique<XAssetInfo<GameWorldSp>>(ASSET_TYPE_GAMEWORLD_SP,
                                                                        MAP_NAME,
                                                                        &gameWorld,
                                                                        std::vector<XAssetInfoGeneric*>(),
                                                                        std::vector<scr_string_t>(),
                                                                        std::vector<IndirectAssetReference>(),
                                                                        &zone));

        MockSearchPath objPath;
        MockOutputPath output;
        AssetDumpingContext context(zone, "", output, objPath, std::nullopt);
        game_world::DumperSpT5 dumper;
        dumper.Dump(context);
        bsp::BspWriterT5 writer;
        writer.Dump(context);

        const auto* file = output.GetMockedFile(MAP_NAME);
        REQUIRE(file != nullptr);

        std::istringstream stream(file->AsString());
        BspFile bsp;
        REQUIRE(bsp.Read(stream));

        const auto* lump = bsp.GetLumpData(LUMP_PATHCONNECTIONS);
        REQUIRE(lump != nullptr);

        PathConnections connections;
        REQUIRE(connections.Read(*lump));
        REQUIRE(connections.m_node_links.size() == 3u);
        REQUIRE(connections.m_node_links[0].size() == 2u);
        REQUIRE(connections.m_node_links[0][0].nodeNum == 2u);
        REQUIRE(connections.m_node_links[0][1].dist == 16.0f);
        REQUIRE(connections.m_node_links[1].empty());
        REQUIRE(connections.m_vis == std::vector<uint8_t>{0x05u});
    }
} // namespace
