#include "GameWorldDumperT5.h"

#include "BspDumperStateT5.h"
#include "Game/T5/Bsp/BspT5.h"
#include "Game/T5/Bsp/PathConnectionsT5.h"
#include "Utils/Logging/Log.h"

using namespace T5;

namespace
{
    void DumpPathData(AssetDumpingContext& context, const char* name, const PathData& path)
    {
        PathConnections connections;
        connections.m_node_links.resize(path.nodeCount);
        for (auto i = 0u; i < path.nodeCount; i++)
        {
            const auto& node = path.nodes[i].constant;
            auto& links = connections.m_node_links[i];
            links.resize(node.totalLinkCount);
            for (auto j = 0u; j < node.totalLinkCount; j++)
                links[j] = PathConnectionLink{node.Links[j].nodeNum, node.Links[j].fDist};
        }

        const auto visBytes = PathConnections::GetVisBytes(path.nodeCount);
        if (path.pathVis && static_cast<size_t>(path.visBytes) == visBytes)
            connections.m_vis.assign(path.pathVis, path.pathVis + visBytes);
        else
        {
            con::warn("GameWorld \"{}\" has {} vis bytes instead of {}", name, path.visBytes, visBytes);
            connections.m_vis.resize(visBytes);
        }

        auto& bsp = context.GetZoneAssetDumperState<bsp::BspDumperStateT5>()->GetBsp(name);
        bsp.SetLump(LUMP_PATHCONNECTIONS, connections.Write());
    }
} // namespace

namespace game_world
{
    void DumperSpT5::DumpAsset(AssetDumpingContext& context, const XAssetInfo<AssetGameWorldSp::Type>& asset)
    {
        DumpPathData(context, asset.Asset()->name, asset.Asset()->path);
    }

    void DumperMpT5::DumpAsset(AssetDumpingContext& context, const XAssetInfo<AssetGameWorldMp::Type>& asset)
    {
        DumpPathData(context, asset.Asset()->name, asset.Asset()->path);
    }
} // namespace game_world
