#include "ClipMapDumperT5.h"

#include "BspDumperStateT5.h"
#include "ClipMapEntityDumperT5.h"
#include "Game/T5/Bsp/BspT5.h"
#include "Game/T5/Bsp/LeafBrushPartitionT5.h"
#include "Utils/Logging/Log.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <map>
#include <optional>
#include <queue>
#include <ranges>
#include <set>
#include <utility>
#include <vector>

using namespace T5;

namespace
{
    constexpr auto AXIAL_SIDE_COUNT = 6;
    constexpr auto MAX_ORDER_SEARCH_STEPS = 100000u;

    class LeafBrushOrder
    {
    public:
        explicit LeafBrushOrder(const clipMap_t& clipMap)
            : m_clip_map(clipMap)
        {
        }

        std::optional<std::vector<uint16_t>> Recover(const uint16_t* zoneOrder, const size_t count, const unsigned rootNode)
        {
            float mins[3]{FLT_MAX, FLT_MAX, FLT_MAX};
            float maxs[3]{-FLT_MAX, -FLT_MAX, -FLT_MAX};
            for (auto i = 0u; i < count; i++)
            {
                const auto& brush = m_clip_map.brushes[zoneOrder[i]];
                for (auto axis = 0; axis < 3; axis++)
                {
                    if (mins[axis] > brush.mins[axis])
                        mins[axis] = brush.mins[axis];
                    if (brush.maxs[axis] > maxs[axis])
                        maxs[axis] = brush.maxs[axis];
                }
            }

            AddLeafChains(rootNode);
            if (!CollectChoices(rootNode, mins, maxs) || !Choose(0u))
                return std::nullopt;

            std::map<uint16_t, size_t> position;
            std::map<uint16_t, size_t> incoming;
            for (auto i = 0u; i < count; i++)
            {
                position[zoneOrder[i]] = i;
                incoming[zoneOrder[i]];
            }
            for (const auto& successors : m_successors | std::views::values)
            {
                for (const auto successor : successors)
                    incoming[successor]++;
            }

            using Entry = std::pair<size_t, uint16_t>;
            std::priority_queue<Entry, std::vector<Entry>, std::greater<>> ready;
            for (const auto& [brush, incomingCount] : incoming)
            {
                if (incomingCount == 0)
                    ready.emplace(position[brush], brush);
            }

            std::vector<uint16_t> order;
            order.reserve(count);
            while (!ready.empty())
            {
                const auto brush = ready.top().second;
                ready.pop();
                order.emplace_back(brush);

                for (const auto successor : m_successors[brush])
                {
                    if (--incoming[successor] == 0)
                        ready.emplace(position[successor], successor);
                }
            }

            if (order.size() != count)
                return std::nullopt;

            return order;
        }

    private:
        struct Choice
        {
            std::vector<uint16_t> winners;
            std::vector<uint16_t> losers;
        };

        void CollectBrushes(const unsigned nodeIndex, std::vector<uint16_t>& brushes) const
        {
            const auto& node = m_clip_map.leafbrushNodes[nodeIndex];
            if (node.leafBrushCount > 0)
            {
                brushes.insert(brushes.end(), node.data.leaf.brushes, node.data.leaf.brushes + node.leafBrushCount);
                return;
            }

            if (node.leafBrushCount < 0)
                CollectBrushes(nodeIndex + 1u, brushes);
            for (const auto childOffset : node.data.children.childOffset)
                CollectBrushes(nodeIndex + childOffset, brushes);
        }

        void AddLeafChains(const unsigned nodeIndex)
        {
            const auto& node = m_clip_map.leafbrushNodes[nodeIndex];
            if (node.leafBrushCount > 0)
            {
                for (auto i = 1; i < node.leafBrushCount; i++)
                    m_successors[node.data.leaf.brushes[i - 1]].emplace(node.data.leaf.brushes[i]);
                return;
            }

            if (node.leafBrushCount < 0)
                AddLeafChains(nodeIndex + 1u);
            for (const auto childOffset : node.data.children.childOffset)
                AddLeafChains(nodeIndex + childOffset);
        }

        bool CollectChoices(const unsigned nodeIndex, const float* mins, const float* maxs)
        {
            const auto& node = m_clip_map.leafbrushNodes[nodeIndex];
            if (node.leafBrushCount > 0)
                return true;

            std::vector<uint16_t> brushes;
            CollectBrushes(nodeIndex, brushes);

            const auto axis = static_cast<int>(node.axis);
            const auto dist = node.data.children.dist;

            if (node.leafBrushCount < 0 && !CollectChoices(nodeIndex + 1u, mins, maxs))
                return false;

            auto range = FLT_MAX;
            for (auto side = 0u; side < 2u; side++)
            {
                for (const auto brushIndex : brushes)
                {
                    const auto& brush = m_clip_map.brushes[brushIndex];
                    if (side == 0u)
                    {
                        if (dist > brush.mins[axis])
                            continue;
                        range = UpdateLeafBrushPartitionRange(range, brush.mins[axis] - dist);
                    }
                    else
                    {
                        if (brush.maxs[axis] > dist)
                            continue;
                        range = UpdateLeafBrushPartitionRange(range, dist - brush.maxs[axis]);
                    }
                }

                float childMins[3]{mins[0], mins[1], mins[2]};
                float childMaxs[3]{maxs[0], maxs[1], maxs[2]};
                if (side == 0u)
                    childMins[axis] = dist + range;
                else
                    childMaxs[axis] = dist - range;

                if (!CollectChoices(nodeIndex + node.data.children.childOffset[side], childMins, childMaxs))
                    return false;
            }

            const auto brushCount = static_cast<int>(brushes.size());
            auto bestScore = 0.0f;
            for (auto testAxis = 0; testAxis < 3; testAxis++)
            {
                for (const auto brushIndex : brushes)
                {
                    const auto& brush = m_clip_map.brushes[brushIndex];
                    for (const auto candidate : {brush.mins[testAxis], brush.maxs[testAxis]})
                    {
                        auto testDist = candidate;
                        const auto score = GetLeafBrushPartitionScore(m_clip_map.brushes, brushes.data(), brushCount, testAxis, mins, maxs, testDist);
                        if (score > bestScore)
                            bestScore = score;
                    }
                }
            }

            for (auto testAxis = 0; testAxis < axis; testAxis++)
            {
                for (const auto brushIndex : brushes)
                {
                    const auto& brush = m_clip_map.brushes[brushIndex];
                    for (const auto candidate : {brush.mins[testAxis], brush.maxs[testAxis]})
                    {
                        auto testDist = candidate;
                        if (GetLeafBrushPartitionScore(m_clip_map.brushes, brushes.data(), brushCount, testAxis, mins, maxs, testDist) == bestScore)
                            return false;
                    }
                }
            }

            Choice choice;
            for (const auto brushIndex : brushes)
            {
                const auto& brush = m_clip_map.brushes[brushIndex];
                for (const auto candidate : {brush.mins[axis], brush.maxs[axis]})
                {
                    auto testDist = candidate;
                    if (GetLeafBrushPartitionScore(m_clip_map.brushes, brushes.data(), brushCount, axis, mins, maxs, testDist) != bestScore)
                        continue;

                    if (std::memcmp(&testDist, &dist, sizeof(float)) == 0)
                        choice.winners.emplace_back(brushIndex);
                    else
                        choice.losers.emplace_back(brushIndex);
                    break;
                }
            }

            if (choice.winners.empty())
                return false;
            if (!choice.losers.empty())
                m_choices.emplace_back(std::move(choice));

            return true;
        }

        bool Reaches(const uint16_t from, const uint16_t to)
        {
            std::vector<uint16_t> pending{from};
            std::set<uint16_t> visited{from};
            while (!pending.empty())
            {
                const auto brush = pending.back();
                pending.pop_back();
                if (brush == to)
                    return true;

                for (const auto successor : m_successors[brush])
                {
                    if (visited.emplace(successor).second)
                        pending.emplace_back(successor);
                }
            }

            return false;
        }

        bool Choose(const size_t choiceIndex)
        {
            if (choiceIndex == m_choices.size())
                return true;
            if (++m_steps > MAX_ORDER_SEARCH_STEPS)
                return false;

            const auto& choice = m_choices[choiceIndex];
            for (const auto winner : choice.winners)
            {
                const auto blocked = std::ranges::any_of(choice.losers,
                                                         [this, winner](const uint16_t loser)
                                                         {
                                                             return loser == winner || Reaches(loser, winner);
                                                         });
                if (blocked)
                    continue;

                std::vector<uint16_t> added;
                for (const auto loser : choice.losers)
                {
                    if (m_successors[winner].emplace(loser).second)
                        added.emplace_back(loser);
                }

                if (Choose(choiceIndex + 1u))
                    return true;

                for (const auto loser : added)
                    m_successors[winner].erase(loser);
            }

            return false;
        }

        const clipMap_t& m_clip_map;
        std::map<uint16_t, std::set<uint16_t>> m_successors;
        std::vector<Choice> m_choices;
        size_t m_steps = 0u;
    };

    class ClipMapBspDumper
    {
    public:
        explicit ClipMapBspDumper(const clipMap_t& clipMap)
            : m_clip_map(clipMap)
        {
        }

        void Dump(BspFile& bsp)
        {
            DumpMaterials(bsp);
            DumpPlanes(bsp);
            DumpBrushes(bsp);
            DumpNodes(bsp);
            DumpLeafs(bsp);
            DumpSubmodels(bsp);
            DumpCollision(bsp);
            DumpVisibility(bsp);
        }

    private:
        int FindMaterial(const int contentFlags, const std::optional<int> surfaceFlags) const
        {
            for (auto i = 0u; i < m_clip_map.numMaterials; i++)
            {
                const auto& material = m_clip_map.materials[i];
                if (material.contentFlags == contentFlags && (!surfaceFlags || material.surfaceFlags == *surfaceFlags))
                    return static_cast<int>(i);
            }

            con::warn("clipMap \"{}\" has no material with contents {:#x} and surface flags {:#x}",
                      m_clip_map.name,
                      static_cast<unsigned>(contentFlags),
                      static_cast<unsigned>(surfaceFlags.value_or(0)));
            return 0;
        }

        void DumpMaterials(BspFile& bsp) const
        {
            bsp.SetLump(LUMP_MATERIALS, m_clip_map.materials, m_clip_map.numMaterials);
        }

        void DumpPlanes(BspFile& bsp) const
        {
            std::vector<dplane_t> planes(m_clip_map.planeCount);
            for (auto i = 0; i < m_clip_map.planeCount; i++)
            {
                const auto& plane = m_clip_map.planes[i];
                planes[i] = dplane_t{
                    {plane.normal.x, plane.normal.y, plane.normal.z},
                    plane.dist
                };
            }

            bsp.SetLump(LUMP_PLANES, planes);
        }

        void DumpBrushes(BspFile& bsp) const
        {
            std::vector<dbrush_t> brushes(m_clip_map.numBrushes);
            std::vector<dbrushside_t> sides;
            std::vector<uint8_t> vertCounts(m_clip_map.numBrushes);

            auto sideOffset = 0u;
            auto vertOffset = 0u;
            for (auto i = 0u; i < m_clip_map.numBrushes; i++)
            {
                const auto& brush = m_clip_map.brushes[i];
                brushes[i].numSides = static_cast<int16_t>(brush.numsides + AXIAL_SIDE_COUNT);
                brushes[i].materialNum = static_cast<int16_t>(FindMaterial(brush.contents, std::nullopt));

                for (auto axis = 0u; axis < 3u; axis++)
                {
                    for (auto side = 0u; side < 2u; side++)
                    {
                        dbrushside_t diskSide{};
                        diskSide.u.bound = side == 0u ? brush.mins[axis] : brush.maxs[axis];
                        diskSide.materialNum = FindMaterial(brush.axial_cflags[side][axis], brush.axial_sflags[side][axis]);
                        sides.emplace_back(diskSide);
                    }
                }

                if (brush.numsides > 0 && brush.sides != m_clip_map.brushsides + sideOffset)
                    con::warn("clipMap \"{}\" brush {} does not use the next brush sides", m_clip_map.name, i);

                for (auto side = 0u; side < brush.numsides; side++)
                {
                    const auto& brushSide = brush.sides[side];
                    dbrushside_t diskSide{};
                    diskSide.u.planeNum = static_cast<int>(brushSide.plane - m_clip_map.planes);
                    diskSide.materialNum = FindMaterial(brushSide.cflags, brushSide.sflags);
                    sides.emplace_back(diskSide);
                }
                sideOffset += brush.numsides;

                if (brush.verts != m_clip_map.brushVerts + vertOffset)
                    con::warn("clipMap \"{}\" brush {} does not use the next brush verts", m_clip_map.name, i);

                vertCounts[i] = static_cast<uint8_t>(brush.numverts);
                vertOffset += brush.numverts;
            }

            bsp.SetLump(LUMP_BRUSHES, brushes);
            bsp.SetLump(LUMP_BRUSHSIDES, sides);
            bsp.SetLump(LUMP_BRUSHVERTSCOUNTS, vertCounts);
            bsp.SetLump(LUMP_BRUSHVERTS, m_clip_map.brushVerts, m_clip_map.numBrushVerts);
        }

        void DumpNodes(BspFile& bsp) const
        {
            std::vector<dnode_t> nodes(m_clip_map.numNodes);
            for (auto i = 0u; i < m_clip_map.numNodes; i++)
            {
                const auto& node = m_clip_map.nodes[i];
                nodes[i].planeNum = static_cast<int>(node.plane - m_clip_map.planes);
                nodes[i].children[0] = node.children[0];
                nodes[i].children[1] = node.children[1];
            }

            bsp.SetLump(LUMP_NODES, nodes);
        }

        void CollectLeafBrushRange(const unsigned nodeIndex, const uint16_t*& first, const uint16_t*& end) const
        {
            const auto& node = m_clip_map.leafbrushNodes[nodeIndex];
            if (node.leafBrushCount > 0)
            {
                first = std::min<const uint16_t*>(first, node.data.leaf.brushes);
                end = std::max<const uint16_t*>(end, node.data.leaf.brushes + node.leafBrushCount);
                return;
            }

            if (node.leafBrushCount < 0)
                CollectLeafBrushRange(nodeIndex + 1u, first, end);
            for (const auto childOffset : node.data.children.childOffset)
                CollectLeafBrushRange(nodeIndex + childOffset, first, end);
        }

        void DumpLeafs(BspFile& bsp) const
        {
            std::vector<dleaf_t> leafs(m_clip_map.numLeafs);
            std::vector<uint32_t> leafBrushes(m_clip_map.leafbrushes, m_clip_map.leafbrushes + m_clip_map.numLeafBrushes);

            for (auto i = 0u; i < m_clip_map.numLeafs; i++)
            {
                const auto& leaf = m_clip_map.leafs[i];
                auto& diskLeaf = leafs[i];
                diskLeaf.cluster = leaf.cluster;
                diskLeaf.firstCollAabbIndex = leaf.firstCollAabbIndex;
                diskLeaf.collAabbCount = leaf.collAabbCount;

                if (leaf.leafBrushNode == 0)
                    continue;

                const auto* first = m_clip_map.leafbrushes + m_clip_map.numLeafBrushes;
                const auto* end = m_clip_map.leafbrushes;
                CollectLeafBrushRange(static_cast<unsigned>(leaf.leafBrushNode), first, end);

                diskLeaf.firstLeafBrush = static_cast<int>(first - m_clip_map.leafbrushes);
                diskLeaf.numLeafBrushes = static_cast<int>(end - first);

                LeafBrushOrder order(m_clip_map);
                const auto recovered = order.Recover(first, static_cast<size_t>(end - first), static_cast<unsigned>(leaf.leafBrushNode));
                if (!recovered)
                {
                    con::warn("clipMap \"{}\" leaf {} brush order could not be recovered", m_clip_map.name, i);
                    continue;
                }

                std::ranges::copy(*recovered, leafBrushes.begin() + diskLeaf.firstLeafBrush);
            }

            bsp.SetLump(LUMP_LEAFS, leafs);
            bsp.SetLump(LUMP_LEAFBRUSHES, leafBrushes);
        }

        static float FindAddend(const float value, const float offset)
        {
            auto source = value - offset;
            for (auto step = 0u; step < 8u && source + offset != value; step++)
                source = std::nextafter(source, source + offset < value ? FLT_MAX : -FLT_MAX);

            return source;
        }

        void DumpSubmodels(BspFile& bsp) const
        {
            std::vector<dmodel_t> models(m_clip_map.numSubModels);
            for (auto i = 0u; i < m_clip_map.numSubModels; i++)
            {
                const auto& model = m_clip_map.cmodels[i];
                auto& diskModel = models[i];
                for (auto axis = 0u; axis < 3u; axis++)
                {
                    diskModel.mins[axis] = FindAddend(model.mins[axis], -1.0f);
                    diskModel.maxs[axis] = FindAddend(model.maxs[axis], 1.0f);
                }

                if (i == 0u)
                    continue;

                diskModel.firstCollAabbIndex = model.leaf.firstCollAabbIndex;
                diskModel.collAabbCount = model.leaf.collAabbCount;
                if (model.leaf.leafBrushNode == 0)
                    continue;

                std::vector<uint16_t> brushes;
                CollectModelBrushes(static_cast<unsigned>(model.leaf.leafBrushNode), brushes);
                std::ranges::sort(brushes);
                if (brushes.empty() || brushes.back() - brushes.front() + 1u != brushes.size())
                    con::warn("clipMap \"{}\" submodel {} brushes are not contiguous", m_clip_map.name, i);

                if (!brushes.empty())
                {
                    diskModel.firstBrush = brushes.front();
                    diskModel.numBrushes = static_cast<unsigned>(brushes.size());
                }
            }

            bsp.SetLump(LUMP_MODELS, models);
        }

        void CollectModelBrushes(const unsigned nodeIndex, std::vector<uint16_t>& brushes) const
        {
            const auto& node = m_clip_map.leafbrushNodes[nodeIndex];
            if (node.leafBrushCount > 0)
            {
                brushes.insert(brushes.end(), node.data.leaf.brushes, node.data.leaf.brushes + node.leafBrushCount);
                return;
            }

            if (node.leafBrushCount < 0)
                CollectModelBrushes(nodeIndex + 1u, brushes);
            for (const auto childOffset : node.data.children.childOffset)
                CollectModelBrushes(nodeIndex + childOffset, brushes);
        }

        void DumpCollision(BspFile& bsp) const
        {
            bsp.SetLump(LUMP_LEAFSURFACES, m_clip_map.leafsurfaces, m_clip_map.numLeafSurfaces);
            bsp.SetLump(LUMP_COLLISIONVERTS, m_clip_map.verts, m_clip_map.vertCount);
            bsp.SetLump(LUMP_COLLISIONTRIS, m_clip_map.triIndices, static_cast<size_t>(m_clip_map.triCount) * 3u);
            bsp.SetLump(
                LUMP_COLLISIONEDGEWALKABLE, reinterpret_cast<const uint8_t*>(m_clip_map.triEdgeIsWalkable), ((3u * m_clip_map.triCount + 31u) / 32u) * 4u);
            bsp.SetLump(LUMP_UINDS, m_clip_map.uinds, m_clip_map.nuinds);

            std::vector<DiskCollBorder> borders(m_clip_map.borderCount);
            for (auto i = 0; i < m_clip_map.borderCount; i++)
            {
                const auto& border = m_clip_map.borders[i];
                borders[i] = DiskCollBorder{
                    {border.distEq[0], border.distEq[1], border.distEq[2]},
                    border.zBase, border.zSlope, border.start, border.length
                };
            }
            bsp.SetLump(LUMP_COLLISIONBORDERS, borders);

            std::vector<DiskCollPartition> partitions(m_clip_map.partitionCount);
            for (auto i = 0; i < m_clip_map.partitionCount; i++)
            {
                const auto& partition = m_clip_map.partitions[i];
                auto& diskPartition = partitions[i];
                diskPartition.triCount = static_cast<uint8_t>(partition.triCount);
                diskPartition.borderCount = static_cast<uint8_t>(partition.borderCount);
                diskPartition.firstTriIndex = partition.firstTri;
                diskPartition.firstBorderIndex = static_cast<int>(partition.borders - m_clip_map.borders);
                diskPartition.nuinds = partition.nuinds;
                diskPartition.fuind = partition.fuind;
            }
            bsp.SetLump(LUMP_COLLISIONPARTITIONS, partitions);

            std::vector<DiskCollAabbTree> aabbTrees(m_clip_map.aabbTreeCount);
            for (auto i = 0; i < m_clip_map.aabbTreeCount; i++)
            {
                const auto& aabbTree = m_clip_map.aabbTrees[i];
                aabbTrees[i] = DiskCollAabbTree{
                    {aabbTree.origin[0],   aabbTree.origin[1],   aabbTree.origin[2]  },
                    {aabbTree.halfSize[0], aabbTree.halfSize[1], aabbTree.halfSize[2]},
                    aabbTree.materialIndex,
                    aabbTree.childCount,
                    aabbTree.u.firstChildIndex
                };
            }
            bsp.SetLump(LUMP_COLLISIONAABBS, aabbTrees);
        }

        void DumpVisibility(BspFile& bsp) const
        {
            if (!m_clip_map.vised)
                return;

            const auto visibilitySize = static_cast<size_t>(m_clip_map.numClusters) * static_cast<size_t>(m_clip_map.clusterBytes);
            std::vector<uint8_t> visibility(8u + visibilitySize);
            const int32_t header[2]{m_clip_map.numClusters, m_clip_map.clusterBytes};
            std::memcpy(visibility.data(), header, sizeof(header));
            if (visibilitySize > 0)
                std::memcpy(visibility.data() + sizeof(header), m_clip_map.visibility, visibilitySize);

            bsp.SetLump(LUMP_VISIBILITY, std::move(visibility));
        }

        const clipMap_t& m_clip_map;
    };

    void DumpClipMap(AssetDumpingContext& context, const clipMap_t& clipMap)
    {
        auto& bsp = context.GetZoneAssetDumperState<bsp::BspDumperStateT5>()->GetBsp(clipMap.name);

        ClipMapBspDumper dumper(clipMap);
        dumper.Dump(bsp);

        const auto entities = clip_map::DumpEntitiesT5(clipMap, context.m_zone);
        std::vector<uint8_t> entityLump(entities.begin(), entities.end());
        entityLump.emplace_back(0u);
        bsp.SetLump(LUMP_ENTITIES, std::move(entityLump));
    }
} // namespace

namespace clip_map
{
    void DumperT5::DumpAsset(AssetDumpingContext& context, const XAssetInfo<AssetClipMap::Type>& asset)
    {
        DumpClipMap(context, *asset.Asset());
    }

    void DumperPvsT5::DumpAsset(AssetDumpingContext& context, const XAssetInfo<AssetClipMapPvs::Type>& asset)
    {
        DumpClipMap(context, *asset.Asset());
    }
} // namespace clip_map
