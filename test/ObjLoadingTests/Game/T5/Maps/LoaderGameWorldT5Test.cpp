#include "Game/T5/Maps/LoaderGameWorldT5.h"

#include "Game/T5/Bsp/BspT5.h"
#include "Game/T5/Bsp/PathConnectionsT5.h"
#include "SearchPath/MockSearchPath.h"

#include <catch2/catch_test_macros.hpp>
#include <sstream>

using namespace T5;

namespace
{
    constexpr auto MAP_NAME = "maps/mp/mp_test.d3dbsp";

    constexpr auto ENTITIES =
        "{\n\"classname\" \"worldspawn\"\n}\n"
        "{\n\"classname\" \"node_pathnode\"\n\"targetname\" \"First\"\n\"target\" \"Second\"\n\"origin\" \"0 0 0\"\n\"angles\" \"0 90 0\"\n}\n"
        "{\n\"classname\" \"node_pathnode\"\n\"targetname\" \"Second\"\n\"origin\" \"10 0 0\"\n\"radius\" \"64\"\n}\n"
        "{\n\"classname\" \"node_negotiation_begin\"\n\"target\" \"jump\"\n\"origin\" \"500 0 0\"\n\"spawnflags\" \"1\"\n}\n"
        "{\n\"classname\" \"node_negotiation_end\"\n\"targetname\" \"jump\"\n\"origin\" \"1000 0 0\"\n}\n"
        "{\n\"classname\" \"node_cover_crouch\"\n\"origin\" \"1500 0 0\"\n\"spawnflags\" \"12\"\n}\n";

    std::string CreateBsp(const bool withPaths)
    {
        const std::string entities(ENTITIES);

        BspFile bsp;
        bsp.SetLump(LUMP_ENTITIES, std::vector<uint8_t>(entities.begin(), entities.end()));

        if (withPaths)
        {
            PathConnections connections;
            connections.m_node_links.resize(5u);
            connections.m_node_links[0].emplace_back(PathConnectionLink{1u, 10.0f});
            connections.m_node_links[2].emplace_back(PathConnectionLink{3u, 500.0f});
            connections.m_vis = {0x01u, 0x00u, 0x80u};
            bsp.SetLump(LUMP_PATHCONNECTIONS, connections.Write());
        }

        std::stringstream stream;
        bsp.Write(stream);
        return stream.str();
    }

    AssetCreationResult LoadGameWorld(Zone& zone, MockSearchPath& searchPath)
    {
        AssetCreatorCollection creatorCollection(zone);
        IgnoredAssetLookup ignoredAssetLookup;
        AssetCreationContext context(zone, &creatorCollection, &ignoredAssetLookup);

        const auto loader = game_world::CreateMpLoaderT5(zone.Memory(), searchPath, zone);
        return loader->CreateAsset(MAP_NAME, context);
    }

    TEST_CASE("GameWorld loader builds path nodes from entities (T5)", "[t5][gameworld]")
    {
        MockSearchPath searchPath;
        searchPath.AddFileData(MAP_NAME, CreateBsp(true));

        Zone zone("LoadingZone", 0, GameId::T5, GamePlatform::PC);
        const auto result = LoadGameWorld(zone, searchPath);
        REQUIRE(result.HasBeenSuccessful());

        const auto* gameWorld = reinterpret_cast<XAssetInfo<GameWorldMp>*>(result.GetAssetInfo())->Asset();
        REQUIRE(std::string(gameWorld->name) == MAP_NAME);

        const auto& path = gameWorld->path;
        REQUIRE(path.nodeCount == 5u);

        const auto& first = path.nodes[0].constant;
        REQUIRE(first.type == NODE_PATHNODE);
        REQUIRE(zone.m_script_strings[first.targetname] == "First");
        REQUIRE(first.fAngle == 90.0f);
        REQUIRE(first.spawnflags == 0x8000);
        REQUIRE(first.forward[1] == 1.0f);
        REQUIRE(first.wOverlapNode[0] == 1);
        REQUIRE(first.wOverlapNode[1] == -1);
        REQUIRE(first.wChainId == 1);
        REQUIRE(first.wChainDepth == 1);
        REQUIRE(first.wChainParent == -1);
        REQUIRE(first.totalLinkCount == 1u);
        REQUIRE(first.Links[0].nodeNum == 1u);
        REQUIRE(first.Links[0].fDist == 10.0f);
        REQUIRE(first.Links[0].negotiationLink == 0);

        const auto& second = path.nodes[1].constant;
        REQUIRE(second.fAngle == 0.0f);
        REQUIRE(second.spawnflags == 0);
        REQUIRE(second.forward[0] == 1.0f);
        REQUIRE(second.fRadius == 64.0f);
        REQUIRE(second.vOrigin[0] == 10.0f);
        REQUIRE(second.wOverlapNode[0] == 0);
        REQUIRE(second.wChainId == 1);
        REQUIRE(second.wChainDepth == 2);
        REQUIRE(second.wChainParent == 0);

        const auto& begin = path.nodes[2].constant;
        REQUIRE(begin.type == NODE_NEGOTIATION_BEGIN);
        REQUIRE(begin.spawnflags == 0x2);
        REQUIRE(begin.Links[0].negotiationLink == 1);
        REQUIRE(path.nodes[3].constant.spawnflags == 0x2);
        REQUIRE(path.nodes[4].constant.spawnflags == 0x4);

        REQUIRE(path.chainNodeCount == 2u);
        const uint16_t expectedNodeForChainNode[]{0u, 1u, 3u, 2u, 4u};
        const uint16_t expectedChainNodeForNode[]{0u, 1u, 3u, 2u, 4u};
        for (auto i = 0u; i < 5u; i++)
        {
            REQUIRE(path.nodeForChainNode[i] == expectedNodeForChainNode[i]);
            REQUIRE(path.chainNodeForNode[i] == expectedChainNodeForNode[i]);
        }

        REQUIRE(path.visBytes == 3);
        REQUIRE(static_cast<uint8_t>(path.pathVis[2]) == 0x80u);

        REQUIRE(path.nodeTreeCount == 3);
        const auto& root = path.nodeTree[0];
        REQUIRE(root.axis == 0);
        REQUIRE(root.dist == 750.0f);
        REQUIRE(root.u.child[0] == &path.nodeTree[2]);
        REQUIRE(root.u.child[1] == &path.nodeTree[1]);
        REQUIRE(path.nodeTree[2].axis == -1);
        REQUIRE(path.nodeTree[2].u.s.nodeCount == 3);
        REQUIRE(path.nodeTree[2].u.s.nodes[2] == 2u);
        REQUIRE(path.nodeTree[1].u.s.nodeCount == 2);
        REQUIRE(path.nodeTree[1].u.s.nodes[0] == 3u);

        REQUIRE(path.nodes[5].constant.type == NODE_BADNODE);
        REQUIRE(path.basenodes);
    }

    TEST_CASE("GameWorld loader fails without path connections (T5)", "[t5][gameworld]")
    {
        MockSearchPath searchPath;
        searchPath.AddFileData(MAP_NAME, CreateBsp(false));

        Zone zone("LoadingZone", 0, GameId::T5, GamePlatform::PC);
        const auto result = LoadGameWorld(zone, searchPath);

        REQUIRE(!result.HasBeenSuccessful());
        REQUIRE(result.HasTakenAction());
    }

    TEST_CASE("GameWorld loader takes no action without a bsp (T5)", "[t5][gameworld]")
    {
        MockSearchPath searchPath;
        Zone zone("LoadingZone", 0, GameId::T5, GamePlatform::PC);
        const auto result = LoadGameWorld(zone, searchPath);

        REQUIRE(!result.HasBeenSuccessful());
        REQUIRE(!result.HasTakenAction());
    }
} // namespace
