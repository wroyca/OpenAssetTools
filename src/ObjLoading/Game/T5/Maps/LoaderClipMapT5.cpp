#include "LoaderClipMapT5.h"

#include "Game/T5/Bsp/BspEntitiesT5.h"
#include "Game/T5/Bsp/BspT5.h"
#include "Game/T5/Bsp/EntityMathT5.h"
#include "Game/T5/Bsp/LeafBrushPartitionT5.h"
#include "Game/T5/CommonT5.h"
#include "Utils/Logging/Log.h"
#include "Utils/StringUtils.h"

#include <algorithm>
#include <cfloat>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <format>
#include <optional>
#include <string>
#include <vector>

using namespace T5;

namespace
{
    constexpr auto AXIAL_SIDE_COUNT = 6;
    constexpr auto MATERIAL_CONTENTS_MASK = static_cast<int>(0xDFFFFFFBu);

    class LeafBrushPartitioner
    {
    public:
        explicit LeafBrushPartitioner(const cbrush_t* brushes)
            : m_brushes(brushes),
              m_nodes(1u)
        {
        }

        int PartitionLeaf(uint16_t* leafBrushes, const int leafBrushCount, cLeaf_s& leaf)
        {
            float mins[3]{FLT_MAX, FLT_MAX, FLT_MAX};
            float maxs[3]{-FLT_MAX, -FLT_MAX, -FLT_MAX};
            for (auto i = 0; i < leafBrushCount; i++)
            {
                const auto& brush = m_brushes[leafBrushes[i]];
                for (auto axis = 0; axis < 3; axis++)
                {
                    if (mins[axis] > brush.mins[axis])
                        mins[axis] = brush.mins[axis];
                    if (brush.maxs[axis] > maxs[axis])
                        maxs[axis] = brush.maxs[axis];
                }
            }

            for (auto axis = 0; axis < 3; axis++)
            {
                leaf.mins[axis] = mins[axis] - 0.125f;
                leaf.maxs[axis] = maxs[axis] + 0.125f;
            }

            return static_cast<int>(Partition(leafBrushes, leafBrushCount, mins, maxs));
        }

        size_t AllocNode()
        {
            cLeafBrushNode_s node{};
            node.data.children.dist = -FLT_MAX;
            m_nodes.emplace_back(node);

            return m_nodes.size() - 1u;
        }

        cLeafBrushNode_s& Node(const size_t index)
        {
            return m_nodes[index];
        }

        [[nodiscard]] const std::vector<cLeafBrushNode_s>& Nodes() const
        {
            return m_nodes;
        }

    private:
        size_t Partition(uint16_t* leafBrushes, const int leafBrushCount, const float* mins, const float* maxs)
        {
            const auto node = AllocNode();

            auto bestScore = 0.0f;
            auto axis = -1;
            auto dist = 0.0f;
            for (auto testAxis = 0; testAxis < 3; testAxis++)
            {
                for (auto i = 0; i < leafBrushCount; i++)
                {
                    const auto& brush = m_brushes[leafBrushes[i]];
                    for (const auto candidate : {brush.mins[testAxis], brush.maxs[testAxis]})
                    {
                        auto testDist = candidate;
                        const auto score = GetLeafBrushPartitionScore(m_brushes, leafBrushes, leafBrushCount, testAxis, mins, maxs, testDist);
                        if (score > bestScore)
                        {
                            bestScore = score;
                            axis = testAxis;
                            dist = testDist;
                        }
                    }
                }
            }

            if (axis < 0)
            {
                auto& leafNode = m_nodes[node];
                leafNode.leafBrushCount = static_cast<int16_t>(leafBrushCount);
                for (auto i = 0; i < leafBrushCount; i++)
                    leafNode.contents |= m_brushes[leafBrushes[i]].contents;
                leafNode.data.leaf.brushes = leafBrushes;

                return node;
            }

            const std::vector<uint16_t> leafBrushesCopy(leafBrushes, leafBrushes + leafBrushCount);

            auto childCount = 0;
            for (const auto brushIndex : leafBrushesCopy)
            {
                const auto& brush = m_brushes[brushIndex];
                if (brush.mins[axis] < dist && dist < brush.maxs[axis])
                    leafBrushes[childCount++] = brushIndex;
            }

            if (childCount > 0)
            {
                const auto straddleNode = Partition(leafBrushes, childCount, mins, maxs);
                m_nodes[node].leafBrushCount = -1;
                m_nodes[node].contents = m_nodes[straddleNode].contents;
                leafBrushes += childCount;
            }

            auto range = FLT_MAX;
            m_nodes[node].axis = static_cast<char>(axis);
            m_nodes[node].data.children.dist = dist;

            for (auto side = 0u; side < 2u; side++)
            {
                childCount = 0;
                for (const auto brushIndex : leafBrushesCopy)
                {
                    const auto& brush = m_brushes[brushIndex];
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

                    leafBrushes[childCount++] = brushIndex;
                }

                float childMins[3]{mins[0], mins[1], mins[2]};
                float childMaxs[3]{maxs[0], maxs[1], maxs[2]};
                if (side == 0u)
                    childMins[axis] = dist + range;
                else
                    childMaxs[axis] = dist - range;

                const auto childNode = Partition(leafBrushes, childCount, childMins, childMaxs);
                m_nodes[node].data.children.childOffset[side] = static_cast<uint16_t>(childNode - node);
                m_nodes[node].contents |= m_nodes[childNode].contents;
                leafBrushes += childCount;
            }

            m_nodes[node].data.children.range = range;

            return node;
        }

        const cbrush_t* m_brushes;
        std::vector<cLeafBrushNode_s> m_nodes;
    };

    class ClipMapBspLoader
    {
    public:
        ClipMapBspLoader(MemoryManager& memory, const BspFile& bsp, clipMap_t& clipMap)
            : m_memory(memory),
              m_bsp(bsp),
              m_clip_map(clipMap)
        {
        }

        bool Load()
        {
            return LoadMaterials() && LoadPlanes() && LoadBrushRelated() && LoadNodes() && LoadLeafSurfaces() && LoadCollisionVerts()
                   && LoadCollisionTriangles() && LoadPartitionIndices() && LoadCollisionEdgeWalkable() && LoadCollisionBorders() && LoadCollisionPartitions()
                   && LoadVisibility();
        }

    private:
        template<typename T> std::optional<std::vector<T>> GetLump(const LumpType type) const
        {
            auto lump = m_bsp.GetLump<T>(type);
            if (!lump)
                con::error("BSP lump {} of \"{}\" has a funny size", static_cast<unsigned>(type), m_clip_map.name);

            return lump;
        }

        template<typename T> T* Copy(const std::vector<T>& elements)
        {
            if (elements.empty())
                return nullptr;

            auto* copy = m_memory.Alloc<T>(elements.size());
            std::memcpy(copy, elements.data(), sizeof(T) * elements.size());
            return copy;
        }

        bool LoadMaterials()
        {
            const auto materials = GetLump<dmaterial_t>(LUMP_MATERIALS);
            if (!materials)
                return false;

            m_clip_map.numMaterials = static_cast<unsigned>(materials->size());
            m_clip_map.materials = Copy(*materials);
            for (auto i = 0u; i < m_clip_map.numMaterials; i++)
                m_clip_map.materials[i].contentFlags &= MATERIAL_CONTENTS_MASK;

            return true;
        }

        bool LoadPlanes()
        {
            const auto planes = GetLump<dplane_t>(LUMP_PLANES);
            if (!planes)
                return false;

            m_clip_map.planeCount = static_cast<int>(planes->size());
            m_clip_map.planes = m_memory.Alloc<cplane_s>(planes->size());
            for (auto i = 0u; i < planes->size(); i++)
            {
                const auto& diskPlane = (*planes)[i];
                auto& plane = m_clip_map.planes[i];

                char signBits = 0;
                for (auto axis = 0; axis < 3; axis++)
                {
                    plane.normal.v[axis] = diskPlane.normal[axis];
                    if (0.0f > plane.normal.v[axis])
                        signBits |= static_cast<char>(1 << axis);
                }

                plane.dist = diskPlane.dist;
                if (plane.normal.x == 1.0f)
                    plane.type = 0;
                else if (plane.normal.y == 1.0f)
                    plane.type = 1;
                else if (plane.normal.z == 1.0f)
                    plane.type = 2;
                else
                    plane.type = 3;
                plane.signbits = signBits;
            }

            return true;
        }

        bool LoadBrushRelated()
        {
            const auto diskLeafs = GetLump<dleaf_t>(LUMP_LEAFS);
            const auto diskModels = GetLump<dmodel_t>(LUMP_MODELS);
            if (!diskLeafs || !diskModels || !LoadBrushes() || !LoadLeafBrushes() || !LoadCollisionAabbTrees() || !LoadLeafs(*diskLeafs)
                || !LoadSubmodels(*diskModels))
                return false;

            LeafBrushPartitioner partitioner(m_clip_map.brushes);
            if (!LoadLeafBrushNodes(*diskLeafs, partitioner) || !LoadSubmodelBrushNodes(*diskModels, partitioner))
                return false;
            InitBoxHull(partitioner);

            m_clip_map.leafbrushNodesCount = static_cast<unsigned>(partitioner.Nodes().size());
            m_clip_map.leafbrushNodes = Copy(partitioner.Nodes());

            return true;
        }

        bool LoadBrushes()
        {
            const auto diskBrushes = GetLump<dbrush_t>(LUMP_BRUSHES);
            const auto diskSides = GetLump<dbrushside_t>(LUMP_BRUSHSIDES);
            const auto diskVerts = GetLump<vec3_t>(LUMP_BRUSHVERTS);
            const auto diskVertCounts = GetLump<uint8_t>(LUMP_BRUSHVERTSCOUNTS);
            if (!diskBrushes || !diskSides || !diskVerts || !diskVertCounts)
                return false;

            const auto brushCount = diskBrushes->size();
            if (diskVertCounts->size() != brushCount || diskSides->size() < brushCount * AXIAL_SIDE_COUNT)
            {
                con::error("BSP brushes of \"{}\" do not match their sides and vertex counts", m_clip_map.name);
                return false;
            }

            m_clip_map.numBrushSides = static_cast<unsigned>(diskSides->size() - brushCount * AXIAL_SIDE_COUNT);
            m_clip_map.brushsides = m_clip_map.numBrushSides > 0 ? m_memory.Alloc<cbrushside_t>(m_clip_map.numBrushSides) : nullptr;
            m_clip_map.numBrushes = static_cast<uint16_t>(brushCount);
            m_clip_map.brushes = m_memory.Alloc<cbrush_t>(brushCount + 1u);
            m_clip_map.numBrushVerts = static_cast<unsigned>(diskVerts->size());
            m_clip_map.brushVerts = Copy(*diskVerts);

            auto* outSide = m_clip_map.brushsides;
            auto* outVerts = m_clip_map.brushVerts;
            auto inSide = 0u;
            for (auto i = 0u; i < brushCount; i++)
            {
                const auto& diskBrush = (*diskBrushes)[i];
                auto& brush = m_clip_map.brushes[i];
                if (diskBrush.numSides < AXIAL_SIDE_COUNT)
                {
                    con::error("BSP brush {} of \"{}\" has fewer than six sides", i, m_clip_map.name);
                    return false;
                }

                brush.numsides = static_cast<unsigned>(diskBrush.numSides - AXIAL_SIDE_COUNT);
                brush.numverts = (*diskVertCounts)[i];
                brush.verts = outVerts;
                outVerts += brush.numverts;
                brush.sides = brush.numsides > 0 ? outSide : nullptr;

                for (auto axis = 0u; axis < 3u; axis++)
                {
                    for (auto side = 0u; side < 2u; side++)
                    {
                        const auto& diskSide = (*diskSides)[inSide++];
                        if (side == 0u)
                            brush.mins[axis] = diskSide.u.bound;
                        else
                            brush.maxs[axis] = diskSide.u.bound;

                        const auto* material = GetMaterial(diskSide.materialNum);
                        if (!material)
                            return false;
                        brush.axial_cflags[side][axis] = material->contentFlags;
                        brush.axial_sflags[side][axis] = material->surfaceFlags;
                    }
                }

                for (auto side = 0u; side < brush.numsides; side++)
                {
                    if (inSide >= diskSides->size())
                    {
                        con::error("BSP brush {} of \"{}\" has more sides than the side lump", i, m_clip_map.name);
                        return false;
                    }

                    const auto& diskSide = (*diskSides)[inSide++];
                    const auto* material = GetMaterial(diskSide.materialNum);
                    if (!material || !IsValidPlane(diskSide.u.planeNum))
                        return false;

                    outSide->plane = &m_clip_map.planes[diskSide.u.planeNum];
                    outSide->cflags = material->contentFlags;
                    outSide->sflags = material->surfaceFlags;
                    outSide++;
                }

                const auto* material = GetMaterial(diskBrush.materialNum);
                if (!material)
                    return false;
                brush.contents = material->contentFlags;
            }

            return true;
        }

        [[nodiscard]] const dmaterial_t* GetMaterial(const int materialNum) const
        {
            if (materialNum < 0 || static_cast<unsigned>(materialNum) >= m_clip_map.numMaterials)
            {
                con::error("BSP material {} of \"{}\" is out of range", materialNum, m_clip_map.name);
                return nullptr;
            }

            return &m_clip_map.materials[materialNum];
        }

        [[nodiscard]] bool IsValidPlane(const int planeNum) const
        {
            if (planeNum < 0 || planeNum >= m_clip_map.planeCount)
            {
                con::error("BSP plane {} of \"{}\" is out of range", planeNum, m_clip_map.name);
                return false;
            }

            return true;
        }

        bool LoadLeafBrushes()
        {
            const auto leafBrushes = GetLump<uint32_t>(LUMP_LEAFBRUSHES);
            if (!leafBrushes)
                return false;

            m_clip_map.numLeafBrushes = static_cast<unsigned>(leafBrushes->size());
            m_clip_map.leafbrushes = m_memory.Alloc<uint16_t>(leafBrushes->size() + 1u);
            for (auto i = 0u; i < leafBrushes->size(); i++)
            {
                if ((*leafBrushes)[i] >= m_clip_map.numBrushes)
                {
                    con::error("BSP leaf brush {} of \"{}\" is out of range", (*leafBrushes)[i], m_clip_map.name);
                    return false;
                }

                m_clip_map.leafbrushes[i] = static_cast<uint16_t>((*leafBrushes)[i]);
            }

            return true;
        }

        bool LoadCollisionAabbTrees()
        {
            const auto aabbTrees = GetLump<DiskCollAabbTree>(LUMP_COLLISIONAABBS);
            if (!aabbTrees)
                return false;

            m_clip_map.aabbTreeCount = static_cast<int>(aabbTrees->size());
            m_clip_map.aabbTrees = aabbTrees->empty() ? nullptr : m_memory.Alloc<CollisionAabbTree>(aabbTrees->size());
            for (auto i = 0u; i < aabbTrees->size(); i++)
            {
                const auto& diskTree = (*aabbTrees)[i];
                auto& tree = m_clip_map.aabbTrees[i];
                for (auto axis = 0u; axis < 3u; axis++)
                {
                    tree.origin[axis] = diskTree.origin[axis];
                    tree.halfSize[axis] = diskTree.halfSize[axis];
                }
                tree.materialIndex = diskTree.materialIndex;
                tree.childCount = diskTree.childCount;
                tree.u.firstChildIndex = diskTree.u;
            }

            return true;
        }

        bool LoadLeafs(const std::vector<dleaf_t>& diskLeafs)
        {
            if (diskLeafs.empty())
            {
                con::error("BSP of \"{}\" has no leafs", m_clip_map.name);
                return false;
            }

            m_clip_map.numLeafs = static_cast<unsigned>(diskLeafs.size());
            m_clip_map.leafs = m_memory.Alloc<cLeaf_s>(diskLeafs.size());
            for (auto i = 0u; i < diskLeafs.size(); i++)
            {
                const auto& diskLeaf = diskLeafs[i];
                auto& leaf = m_clip_map.leafs[i];
                leaf.cluster = static_cast<int16_t>(diskLeaf.cluster);
                leaf.firstCollAabbIndex = static_cast<uint16_t>(diskLeaf.firstCollAabbIndex);
                leaf.collAabbCount = static_cast<uint16_t>(diskLeaf.collAabbCount);
                if (diskLeaf.cluster >= m_clip_map.numClusters)
                    m_clip_map.numClusters = diskLeaf.cluster + 1;
            }

            return true;
        }

        bool LoadSubmodels(const std::vector<dmodel_t>& diskModels)
        {
            if (diskModels.empty())
            {
                con::error("BSP of \"{}\" has no models", m_clip_map.name);
                return false;
            }

            m_clip_map.numSubModels = static_cast<unsigned>(diskModels.size());
            m_clip_map.cmodels = m_memory.Alloc<cmodel_t>(diskModels.size());
            for (auto i = 0u; i < diskModels.size(); i++)
            {
                const auto& diskModel = diskModels[i];
                auto& model = m_clip_map.cmodels[i];

                float extent[3];
                for (auto axis = 0u; axis < 3u; axis++)
                {
                    model.mins[axis] = diskModel.mins[axis] - 1.0f;
                    model.maxs[axis] = diskModel.maxs[axis] + 1.0f;

                    const auto maxsExtent = std::fabs(model.maxs[axis]);
                    const auto minsExtent = std::fabs(model.mins[axis]);
                    extent[axis] = minsExtent - maxsExtent >= 0.0f ? minsExtent : maxsExtent;
                }

                const auto lengthSquared = static_cast<float>(static_cast<double>(extent[2]) * extent[2]
                                                              + (static_cast<double>(extent[1]) * extent[1] + static_cast<double>(extent[0]) * extent[0]));
                model.radius = static_cast<float>(std::sqrt(static_cast<double>(lengthSquared)));

                if (i == 0u)
                    continue;

                model.leaf.collAabbCount = static_cast<uint16_t>(diskModel.collAabbCount);
                model.leaf.firstCollAabbIndex = static_cast<uint16_t>(diskModel.firstCollAabbIndex);
            }

            return true;
        }

        [[nodiscard]] int GetLeafTerrainContents(const cLeaf_s& leaf) const
        {
            auto contents = 0;
            for (auto i = 0u; i < leaf.collAabbCount; i++)
            {
                const auto& tree = m_clip_map.aabbTrees[leaf.firstCollAabbIndex + i];
                contents |= m_clip_map.materials[tree.materialIndex].contentFlags;
            }

            return contents;
        }

        void PartitionLeafBrushes(LeafBrushPartitioner& partitioner, uint16_t* leafBrushes, const int leafBrushCount, cLeaf_s& leaf) const
        {
            auto contents = 0;
            for (auto i = 0; i < leafBrushCount; i++)
                contents |= m_clip_map.brushes[leafBrushes[i]].contents;

            leaf.brushContents = contents;
            leaf.terrainContents = GetLeafTerrainContents(leaf);
            if (leafBrushCount > 0)
                leaf.leafBrushNode = partitioner.PartitionLeaf(leafBrushes, leafBrushCount, leaf);
        }

        [[nodiscard]] bool IsValidCollAabbRange(const cLeaf_s& leaf) const
        {
            if (leaf.firstCollAabbIndex + leaf.collAabbCount > m_clip_map.aabbTreeCount)
            {
                con::error("BSP leaf aabb trees of \"{}\" are out of range", m_clip_map.name);
                return false;
            }

            for (auto i = 0u; i < leaf.collAabbCount; i++)
            {
                if (m_clip_map.aabbTrees[leaf.firstCollAabbIndex + i].materialIndex >= m_clip_map.numMaterials)
                {
                    con::error("BSP aabb tree material of \"{}\" is out of range", m_clip_map.name);
                    return false;
                }
            }

            return true;
        }

        bool LoadLeafBrushNodes(const std::vector<dleaf_t>& diskLeafs, LeafBrushPartitioner& partitioner) const
        {
            for (auto i = 0u; i < m_clip_map.numLeafs; i++)
            {
                const auto& diskLeaf = diskLeafs[i];
                if (diskLeaf.firstLeafBrush < 0 || diskLeaf.numLeafBrushes < 0
                    || static_cast<unsigned>(diskLeaf.firstLeafBrush) + static_cast<unsigned>(diskLeaf.numLeafBrushes) > m_clip_map.numLeafBrushes)
                {
                    con::error("BSP leaf {} brushes of \"{}\" are out of range", i, m_clip_map.name);
                    return false;
                }
                if (!IsValidCollAabbRange(m_clip_map.leafs[i]))
                    return false;

                PartitionLeafBrushes(partitioner, &m_clip_map.leafbrushes[diskLeaf.firstLeafBrush], diskLeaf.numLeafBrushes, m_clip_map.leafs[i]);
            }

            return true;
        }

        bool LoadSubmodelBrushNodes(const std::vector<dmodel_t>& diskModels, LeafBrushPartitioner& partitioner) const
        {
            for (auto i = 1u; i < m_clip_map.numSubModels; i++)
            {
                const auto& diskModel = diskModels[i];
                if (diskModel.firstBrush + diskModel.numBrushes > m_clip_map.numBrushes)
                {
                    con::error("BSP model {} brushes of \"{}\" are out of range", i, m_clip_map.name);
                    return false;
                }
                if (!IsValidCollAabbRange(m_clip_map.cmodels[i].leaf))
                    return false;

                auto* indices = diskModel.numBrushes > 0 ? m_memory.Alloc<uint16_t>(diskModel.numBrushes) : nullptr;
                for (auto k = 0u; k < diskModel.numBrushes; k++)
                    indices[k] = static_cast<uint16_t>(diskModel.firstBrush + k);

                PartitionLeafBrushes(partitioner, indices, static_cast<int>(diskModel.numBrushes), m_clip_map.cmodels[i].leaf);
            }

            return true;
        }

        void InitBoxHull(LeafBrushPartitioner& partitioner) const
        {
            auto& boxBrush = m_clip_map.brushes[m_clip_map.numBrushes];
            m_clip_map.box_brush = &boxBrush;
            boxBrush.numsides = 0u;
            boxBrush.sides = nullptr;
            boxBrush.contents = -1;
            for (auto side = 0u; side < 2u; side++)
            {
                for (auto axis = 0u; axis < 3u; axis++)
                {
                    boxBrush.axial_cflags[side][axis] = -1;
                    boxBrush.axial_sflags[side][axis] = -1;
                }
            }

            auto& boxLeaf = m_clip_map.box_model.leaf;
            boxLeaf.brushContents = -1;
            boxLeaf.terrainContents = 0;
            for (auto axis = 0u; axis < 3u; axis++)
            {
                boxLeaf.mins[axis] = FLT_MAX;
                boxLeaf.maxs[axis] = -FLT_MAX;
            }

            const auto boxNode = partitioner.AllocNode();
            boxLeaf.leafBrushNode = static_cast<int>(boxNode);
            partitioner.Node(boxNode).leafBrushCount = 1;
            partitioner.Node(boxNode).data.leaf.brushes = &m_clip_map.leafbrushes[m_clip_map.numLeafBrushes];
            m_clip_map.leafbrushes[m_clip_map.numLeafBrushes] = m_clip_map.numBrushes;
        }

        bool LoadNodes()
        {
            const auto nodes = GetLump<dnode_t>(LUMP_NODES);
            if (!nodes)
                return false;

            m_clip_map.numNodes = static_cast<unsigned>(nodes->size());
            m_clip_map.nodes = m_memory.Alloc<cNode_t>(nodes->size());
            for (auto i = 0u; i < nodes->size(); i++)
            {
                const auto& diskNode = (*nodes)[i];
                if (!IsValidPlane(diskNode.planeNum))
                    return false;

                auto& node = m_clip_map.nodes[i];
                node.plane = &m_clip_map.planes[diskNode.planeNum];
                node.children[0] = static_cast<int16_t>(diskNode.children[0]);
                node.children[1] = static_cast<int16_t>(diskNode.children[1]);
            }

            return true;
        }

        bool LoadLeafSurfaces()
        {
            const auto leafSurfaces = GetLump<unsigned>(LUMP_LEAFSURFACES);
            if (!leafSurfaces)
                return false;

            m_clip_map.numLeafSurfaces = static_cast<unsigned>(leafSurfaces->size());
            m_clip_map.leafsurfaces = Copy(*leafSurfaces);
            return true;
        }

        bool LoadCollisionVerts()
        {
            const auto verts = GetLump<vec3_t>(LUMP_COLLISIONVERTS);
            if (!verts)
                return false;

            m_clip_map.vertCount = static_cast<unsigned>(verts->size());
            m_clip_map.verts = Copy(*verts);
            return true;
        }

        bool LoadCollisionTriangles()
        {
            const auto triIndices = GetLump<uint16_t>(LUMP_COLLISIONTRIS);
            if (!triIndices)
                return false;

            m_clip_map.triCount = static_cast<int>(triIndices->size() / 3u);
            m_clip_map.triIndices = Copy(*triIndices);
            return true;
        }

        bool LoadPartitionIndices()
        {
            const auto uinds = GetLump<uint16_t>(LUMP_UINDS);
            if (!uinds)
                return false;

            m_clip_map.nuinds = static_cast<unsigned>(uinds->size());
            m_clip_map.uinds = Copy(*uinds);
            return true;
        }

        bool LoadCollisionEdgeWalkable()
        {
            const auto walkable = GetLump<char>(LUMP_COLLISIONEDGEWALKABLE);
            if (!walkable)
                return false;

            m_clip_map.triEdgeIsWalkable = Copy(*walkable);
            return true;
        }

        bool LoadCollisionBorders()
        {
            const auto borders = GetLump<DiskCollBorder>(LUMP_COLLISIONBORDERS);
            if (!borders)
                return false;

            m_clip_map.borderCount = static_cast<int>(borders->size());
            m_clip_map.borders = borders->empty() ? nullptr : m_memory.Alloc<CollisionBorder>(borders->size());
            for (auto i = 0u; i < borders->size(); i++)
            {
                const auto& diskBorder = (*borders)[i];
                auto& border = m_clip_map.borders[i];
                for (auto axis = 0u; axis < 3u; axis++)
                    border.distEq[axis] = diskBorder.distEq[axis];
                border.zBase = diskBorder.zBase;
                border.zSlope = diskBorder.zSlope;
                border.start = diskBorder.start;
                border.length = diskBorder.length;
            }

            return true;
        }

        bool LoadCollisionPartitions()
        {
            const auto partitions = GetLump<DiskCollPartition>(LUMP_COLLISIONPARTITIONS);
            if (!partitions)
                return false;

            m_clip_map.partitionCount = static_cast<int>(partitions->size());
            m_clip_map.partitions = partitions->empty() ? nullptr : m_memory.Alloc<CollisionPartition>(partitions->size());
            for (auto i = 0u; i < partitions->size(); i++)
            {
                const auto& diskPartition = (*partitions)[i];
                auto& partition = m_clip_map.partitions[i];
                partition.triCount = static_cast<char>(diskPartition.triCount);
                partition.borderCount = static_cast<char>(diskPartition.borderCount);
                partition.firstTri = diskPartition.firstTriIndex;
                if (diskPartition.firstBorderIndex < 0 || diskPartition.firstBorderIndex + diskPartition.borderCount > m_clip_map.borderCount)
                {
                    con::error("BSP partition {} borders of \"{}\" are out of range", i, m_clip_map.name);
                    return false;
                }
                partition.borders = m_clip_map.borders + diskPartition.firstBorderIndex;
                partition.nuinds = diskPartition.nuinds;
                partition.fuind = diskPartition.fuind;
            }

            return true;
        }

        bool LoadVisibility()
        {
            const auto* visibility = m_bsp.GetLumpData(LUMP_VISIBILITY);
            if (!visibility || visibility->empty())
            {
                m_clip_map.clusterBytes = ((m_clip_map.numClusters + 63) & ~63) >> 3;
                m_clip_map.numClusters = 1;
                m_clip_map.visibility = m_memory.Alloc<char>(static_cast<size_t>(m_clip_map.clusterBytes));
                std::memset(m_clip_map.visibility, 0xFF, static_cast<size_t>(m_clip_map.clusterBytes));
                return true;
            }

            int32_t header[2];
            if (visibility->size() < sizeof(header))
            {
                con::error("BSP visibility of \"{}\" is too small", m_clip_map.name);
                return false;
            }
            std::memcpy(header, visibility->data(), sizeof(header));

            m_clip_map.vised = 1;
            m_clip_map.numClusters = header[0];
            m_clip_map.clusterBytes = header[1];
            if (visibility->size() != sizeof(header) + static_cast<size_t>(header[0]) * static_cast<size_t>(header[1]))
            {
                con::error("BSP visibility of \"{}\" does not match its cluster count", m_clip_map.name);
                return false;
            }

            const auto size = visibility->size() - sizeof(header);
            m_clip_map.visibility = size > 0 ? m_memory.Alloc<char>(size) : nullptr;
            if (size > 0)
                std::memcpy(m_clip_map.visibility, visibility->data() + sizeof(header), size);

            return true;
        }

        MemoryManager& m_memory;
        const BspFile& m_bsp;
        clipMap_t& m_clip_map;
    };

    constexpr auto MAX_DYN_ENTITIES = 0x1000u;
    constexpr auto MAX_CONSTRAINTS = 0x1FFu;
    constexpr auto SPARE_DYN_ENTITY_COUNT = 256u;
    constexpr auto MAX_ENTITY_CONSTRAINTS = 4u;
    constexpr auto MAX_ROPES = 300;
    constexpr auto EXTRA_ROPES = 32;
    constexpr int16_t NO_CONSTRAINT = 0x1FF;

    constexpr const char* DYN_ENTITY_TYPE_NAMES[]{"invalid", "clutter", "destruct"};

    struct NodeParams
    {
        std::string targetname;
        std::string target;
        std::string target2;
        float origin[3]{};
        float angles[3]{};
    };

    struct ConstraintParams
    {
        std::string classname;
        std::string targetname;
        std::string target;
        std::string target2;
        std::string material;
        float origin[3]{};
        float angles[3]{};
        int health = 0;
        float minAngle = 0.0f;
        float maxAngle = 0.0f;
        float damp = 0.0f;
        float distance = 0.0f;
        float lengthScale = 0.0f;
        float forceScale = 0.0f;
        float width = 0.0f;
        int spawnflags = 0;
        float timeout = 0.0f;
    };

    struct DynEntityParams
    {
        std::string typeName;
        std::string modelName;
        std::string physModelName;
        std::string destroyFxFile;
        std::string destroySound;
        std::string destroyPiecesFile;
        std::string physPresetFile;
        std::string targetname;
        std::string target;
        std::string destroyedModelName;
        float origin[3]{};
        float angles[3]{};
        int health = 0;
        float mass = 0.0f;
        float bounce = 0.0f;
        float friction = 0.0f;
        float centerOfMass[3]{};
        int spawnflags = 0;
    };

    bool IsKey(const std::string& key, const char* name)
    {
        return utils::StringEqualsIgnoreCase(key, name);
    }

    bool IsClass(const BspEntity& entity, const char* className)
    {
        return utils::StringEqualsIgnoreCase(entity.GetString("classname"), className);
    }

    std::string StripLegacyXModelPrefix(const std::string& name)
    {
        if (name.size() > 7u
            && (utils::StringEqualsIgnoreCase(name.substr(0u, 7u), "xmodel/") || utils::StringEqualsIgnoreCase(name.substr(0u, 7u), "xmodel\\")))
            return name.substr(7u);

        return name;
    }

    std::string StripExtension(const std::string& name)
    {
        const auto dot = name.find_last_of('.');
        const auto separator = name.find_last_of("/\\");
        if (dot == std::string::npos || (separator != std::string::npos && separator > dot))
            return name;

        return name.substr(0u, dot);
    }

    void ParseVec3(const std::string& value, float* vec)
    {
        float parsed[3];
        if (ParseBspVec3(value, parsed))
        {
            for (auto i = 0; i < 3; i++)
                vec[i] = parsed[i];
        }
    }

    float ParseFloat(const std::string& value)
    {
        auto parsed = 0.0f;
        if (!ParseBspFloat(value, parsed))
            return 0.0f;

        return parsed;
    }

    bool IsConstraintClassName(const std::string& className)
    {
        return utils::StringEqualsIgnoreCase(className, "dyn_pointconstraint") || utils::StringEqualsIgnoreCase(className, "dyn_hingeconstraint")
               || utils::StringEqualsIgnoreCase(className, "dyn_lightconstraint") || utils::StringEqualsIgnoreCase(className, "rope");
    }

    bool IsDynEntityClassName(const std::string& className)
    {
        return utils::StringEqualsIgnoreCase(className, "dyn_model") || utils::StringEqualsIgnoreCase(className, "dyn_brushmodel");
    }

    class ClipMapEntityLoader
    {
    public:
        ClipMapEntityLoader(MemoryManager& memory, Zone& zone, AssetCreationContext& context, GenericAssetRegistration& registration, clipMap_t& clipMap)
            : m_memory(memory),
              m_zone(zone),
              m_context(context),
              m_registration(registration),
              m_clip_map(clipMap)
        {
        }

        bool Load(const std::string& entityString)
        {
            const auto entities = ParseBspEntities(entityString);
            if (!entities)
            {
                con::error("Could not parse entities of clipMap \"{}\"", m_clip_map.name);
                return false;
            }

            m_entities = &*entities;
            LoadNodeParams();

            return LoadStaticModels() && LoadConstraints() && LoadDynEntities();
        }

    private:
        uint16_t ScriptString(const std::string& value)
        {
            auto lowerValue = value;
            utils::MakeStringLowerCase(lowerValue);
            const auto scriptString = m_zone.m_script_strings.AddOrGetScriptString(lowerValue);
            m_registration.AddScriptString(scriptString);

            return static_cast<uint16_t>(scriptString);
        }

        template<typename Asset_t> typename Asset_t::Type* Dependency(const std::string& name)
        {
            auto* asset = m_context.LoadDependency<Asset_t>(name);
            if (!asset)
                return nullptr;

            m_registration.AddDependency(asset);
            return asset->Asset();
        }

        template<typename Asset_t> typename Asset_t::Type* FullDependency(const std::string& name)
        {
            auto* asset = m_context.ForceLoadDependency<Asset_t>(name);
            if (!asset)
                return nullptr;

            m_registration.AddDependency(asset);
            return asset->Asset();
        }

        void LoadNodeParams()
        {
            for (const auto& entity : *m_entities)
            {
                std::vector<NodeParams>* destination = nullptr;
                if (IsClass(entity, "info_null"))
                    destination = &m_infos;
                else if (IsClass(entity, "script_model") || IsClass(entity, "script_brushmodel"))
                    destination = &m_ents;
                else if (IsClass(entity, "dyn_model") || IsClass(entity, "dyn_brushmodel"))
                    destination = &m_dents;
                else
                    continue;

                NodeParams node;
                for (const auto& [key, value] : entity.Values())
                {
                    if (IsKey(key, "origin"))
                        ParseVec3(value, node.origin);
                    else if (IsKey(key, "angles"))
                        ParseVec3(value, node.angles);
                    else if (IsKey(key, "targetname"))
                        node.targetname = value;
                    else if (IsKey(key, "target"))
                        node.target = value;
                    else if (IsKey(key, "target2"))
                        node.target2 = value;
                }

                destination->emplace_back(std::move(node));
            }
        }

        bool LoadStaticModels()
        {
            std::vector<cStaticModel_s> staticModels;
            for (const auto& entity : *m_entities)
            {
                if (!IsClass(entity, "misc_model"))
                    continue;

                const auto name = StripLegacyXModelPrefix(entity.GetString("model"));
                float origin[3]{};
                float angles[3]{};
                ParseVec3(entity.GetString("origin"), origin);
                ParseVec3(entity.GetString("angles"), angles);
                const auto scale = entity.GetValue("modelscale") ? ParseFloat(*entity.GetValue("modelscale")) : 1.0f;

                if (name.empty())
                {
                    con::error("No model specified for static model at ({}, {}, {})", origin[0], origin[1], origin[2]);
                    return false;
                }
                if (scale == 0.0f)
                {
                    con::error("Static model \"{}\" has a scale of 0", name);
                    return false;
                }

                auto* xmodelAsset = m_context.ForceLoadDependency<AssetXModel>(name);
                if (!xmodelAsset)
                {
                    con::error("Could not load xmodel \"{}\" of static model", name);
                    return false;
                }
                if (!xmodelAsset->Asset()->collSurfs)
                    continue;
                m_registration.AddDependency(xmodelAsset);

                auto& staticModel = staticModels.emplace_back();
                std::memset(&staticModel, 0, sizeof(staticModel));
                staticModel.xmodel = xmodelAsset->Asset();
                InitStaticModel(staticModel, origin, angles, scale);
            }

            m_clip_map.numStaticModels = static_cast<unsigned>(staticModels.size());
            m_clip_map.staticModelList = nullptr;
            if (!staticModels.empty())
            {
                m_clip_map.staticModelList = m_memory.Alloc<cStaticModel_s>(staticModels.size());
                std::memcpy(m_clip_map.staticModelList, staticModels.data(), sizeof(cStaticModel_s) * staticModels.size());
            }

            return true;
        }

        static ConstraintParams ParseConstraintParams(const BspEntity& entity)
        {
            ConstraintParams params;
            for (const auto& [key, value] : entity.Values())
            {
                if (IsKey(key, "classname"))
                    params.classname = value;
                else if (IsKey(key, "origin"))
                    ParseVec3(value, params.origin);
                else if (IsKey(key, "angles"))
                    ParseVec3(value, params.angles);
                else if (IsKey(key, "health"))
                    params.health = std::atoi(value.c_str());
                else if (IsKey(key, "targetname"))
                    params.targetname = value;
                else if (IsKey(key, "target"))
                    params.target = value;
                else if (IsKey(key, "target2"))
                    params.target2 = value;
                else if (IsKey(key, "min_angle"))
                    params.minAngle = ParseFloat(value);
                else if (IsKey(key, "max_angle"))
                    params.maxAngle = ParseFloat(value);
                else if (IsKey(key, "length") || IsKey(key, "distance"))
                    params.distance = ParseFloat(value);
                else if (IsKey(key, "length_scale"))
                    params.lengthScale = ParseFloat(value);
                else if (IsKey(key, "width"))
                    params.width = ParseFloat(value);
                else if (IsKey(key, "force_scale"))
                    params.forceScale = ParseFloat(value);
                else if (IsKey(key, "damp"))
                    params.damp = ParseFloat(value);
                else if (IsKey(key, "material"))
                    params.material = value;
                else if (IsKey(key, "spawnflags"))
                    params.spawnflags = std::atoi(value.c_str());
                else if (IsKey(key, "timeout"))
                    params.timeout = ParseFloat(value);
            }

            return params;
        }

        bool CreateConstraint(PhysConstraint& constraint, const ConstraintParams& params)
        {
            constraint.attach_point_type1 = ATTACH_POINT_WORLD;
            constraint.attach_point_type2 = ATTACH_POINT_WORLD;
            constraint.target_index1 = -1;
            constraint.target_index2 = -1;
            constraint.constraintHandle = 0;
            constraint.rope_index = -1;
            constraint.type = CONSTRAINT_NONE;

            if (utils::StringEqualsIgnoreCase(params.classname, "dyn_hingeconstraint"))
            {
                constraint.type = CONSTRAINT_HINGE;
                constraint.minAngle = params.minAngle;
                constraint.maxAngle = params.maxAngle;
            }
            else if (utils::StringEqualsIgnoreCase(params.classname, "dyn_pointconstraint"))
                constraint.type = CONSTRAINT_POINT;
            else if (utils::StringEqualsIgnoreCase(params.classname, "rope"))
            {
                constraint.type = CONSTRAINT_ROPE;
                constraint.minAngle = params.width;
                if (!params.material.empty())
                {
                    constraint.material = Dependency<AssetMaterial>(params.material);
                    if (!constraint.material)
                    {
                        con::error("Could not load material \"{}\" of rope", params.material);
                        return false;
                    }
                }
                if (params.spawnflags & 1)
                    constraint.flags |= 1;
            }
            else if (utils::StringEqualsIgnoreCase(params.classname, "dyn_lightconstraint"))
            {
                constraint.type = CONSTRAINT_LIGHT;
                constraint.target_index2 = std::atoi(params.target2.c_str());
                constraint.centity_num[0] = -1;
            }

            constraint.targetname = ScriptString(params.targetname);
            constraint.damp = params.damp;
            for (auto i = 0; i < 3; i++)
            {
                constraint.pos.v[i] = params.origin[i];
                constraint.pos2.v[i] = params.origin[i];
                constraint.dir.v[i] = params.angles[i];
            }
            constraint.min_health = params.health;
            constraint.distance = params.distance;
            constraint.scale.x = params.forceScale;
            constraint.scale.y = params.lengthScale;
            constraint.timeout = GetConstraintTimeout(params.timeout);

            if (constraint.type != CONSTRAINT_ROPE)
                return true;

            for (const auto& info : m_infos)
            {
                if (info.targetname.empty())
                    continue;

                if (utils::StringEqualsIgnoreCase(info.targetname, params.target))
                {
                    for (auto i = 0; i < 3; i++)
                        constraint.pos.v[i] = info.origin[i];
                }
                else if (utils::StringEqualsIgnoreCase(info.targetname, params.target2))
                {
                    for (auto i = 0; i < 3; i++)
                        constraint.pos2.v[i] = info.origin[i];
                }
            }

            return true;
        }

        bool LoadConstraints()
        {
            for (const auto& entity : *m_entities)
            {
                if (IsConstraintClassName(entity.GetString("classname")))
                    m_constraint_params.emplace_back(ParseConstraintParams(entity));
            }

            if (m_constraint_params.size() >= MAX_CONSTRAINTS)
            {
                con::error("Max number of constraints exceeded [{}]", MAX_CONSTRAINTS);
                return false;
            }

            m_clip_map.num_constraints = static_cast<int>(m_constraint_params.size());
            m_clip_map.constraints = nullptr;
            if (m_constraint_params.empty())
                return true;

            m_clip_map.constraints = m_memory.Alloc<PhysConstraint>(m_constraint_params.size());
            for (auto i = 0u; i < m_constraint_params.size(); i++)
            {
                if (!CreateConstraint(m_clip_map.constraints[i], m_constraint_params[i]))
                    return false;
            }

            return true;
        }

        void FindConstraints(const std::string& targetname, const float* origin, const float* angles, const AttachPointType attachType, int* outConstraints)
        {
            if (targetname.empty())
                return;

            float xform[4][3];
            AnglesToTransform(origin, angles, xform);

            const auto targetnameString = ScriptString(targetname);
            auto count = 0u;

            const auto attach = [&](PhysConstraint& constraint, const bool second, const float* worldPos, const int constraintIndex)
            {
                if (second)
                {
                    constraint.attach_point_type2 = attachType;
                    constraint.target_ent2 = targetnameString;
                    MatrixTransposeTransformVector43(worldPos, xform, constraint.pos2.v);
                }
                else
                {
                    constraint.attach_point_type1 = attachType;
                    constraint.target_ent1 = targetnameString;
                    MatrixTransposeTransformVector43(worldPos, xform, constraint.pos.v);
                }

                if (outConstraints && count < MAX_ENTITY_CONSTRAINTS)
                    outConstraints[count++] = constraintIndex;
            };

            for (auto i = 0; i < m_clip_map.num_constraints; i++)
            {
                if (count >= MAX_ENTITY_CONSTRAINTS)
                    break;

                auto& constraint = m_clip_map.constraints[i];
                const auto& params = m_constraint_params[i];
                if (utils::StringEqualsIgnoreCase(targetname, params.target2))
                    attach(constraint, true, params.origin, i);
                if (utils::StringEqualsIgnoreCase(targetname, params.target))
                    attach(constraint, false, params.origin, i);

                for (const auto& info : m_infos)
                {
                    if (count >= MAX_ENTITY_CONSTRAINTS)
                        break;
                    if (!utils::StringEqualsIgnoreCase(info.target, targetname))
                        continue;

                    if (utils::StringEqualsIgnoreCase(info.targetname, params.target2))
                        attach(constraint, true, info.origin, i);
                    if (utils::StringEqualsIgnoreCase(info.targetname, params.target))
                        attach(constraint, false, info.origin, i);
                }
            }
        }

        static DynEntityParams ParseDynEntityParams(const BspEntity& entity)
        {
            DynEntityParams params;
            for (const auto& [key, value] : entity.Values())
            {
                if (IsKey(key, "type"))
                    params.typeName = value;
                else if (IsKey(key, "model"))
                    params.modelName = StripLegacyXModelPrefix(value);
                else if (IsKey(key, "destroyedModel"))
                    params.destroyedModelName = StripLegacyXModelPrefix(value);
                else if (IsKey(key, "physicsmodel"))
                    params.physModelName = StripLegacyXModelPrefix(value);
                else if (IsKey(key, "destroyEfx"))
                    params.destroyFxFile = StripExtension(value);
                else if (IsKey(key, "destroySound"))
                    params.destroySound = value;
                else if (IsKey(key, "destroyPieces"))
                    params.destroyPiecesFile = StripExtension(value);
                else if (IsKey(key, "origin"))
                    ParseVec3(value, params.origin);
                else if (IsKey(key, "angles"))
                    ParseVec3(value, params.angles);
                else if (IsKey(key, "health"))
                    params.health = std::atoi(value.c_str());
                else if (IsKey(key, "physPreset"))
                    params.physPresetFile = value;
                else if (IsKey(key, "centerofmass"))
                    ParseVec3(value, params.centerOfMass);
                else if (IsKey(key, "targetname"))
                    params.targetname = value;
                else if (IsKey(key, "target"))
                    params.target = value;
                else if (IsKey(key, "mass"))
                    params.mass = ParseFloat(value);
                else if (IsKey(key, "bounch"))
                    params.bounce = ParseFloat(value);
                else if (IsKey(key, "friction"))
                    params.friction = ParseFloat(value);
                else if (IsKey(key, "spawnflags"))
                    params.spawnflags |= std::atoi(value.c_str()) & 0x1F;
            }

            return params;
        }

        static std::optional<unsigned> ParseSoundHash(const std::string& name)
        {
            if (name.empty() || name[0] != '@')
                return Common::SND_HashName(name.c_str());

            unsigned hash;
            const auto* end = name.data() + name.size();
            const auto [next, error] = std::from_chars(name.data() + 1, end, hash, 16);
            if (error != std::errc() || next != end)
                return std::nullopt;

            return hash;
        }

        XModel* LoadDynEntityModel(const std::string& name, const char* description)
        {
            auto* model = FullDependency<AssetXModel>(name);
            if (!model)
            {
                con::error("Couldn't find {} [{}] for Dyn Entity", description, name);
                return nullptr;
            }
            if (model->collLod < 0)
            {
                con::error("Dyn Entity {} [{}] has no collision lod", description, name);
                return nullptr;
            }

            return model;
        }

        static int GetXModelContents(const XModel& model)
        {
            auto contents = model.contents;
            for (auto i = 0u; i < model.numCollmaps; i++)
            {
                if (model.collmaps[i].geomList)
                    contents |= model.collmaps[i].geomList->contents;
            }

            return contents;
        }

        PhysPreset* AutoCreatePhysPreset(const DynEntityDef& def, const DynEntityParams& params)
        {
            if (def.xModel)
            {
                for (auto* preset : m_auto_phys_presets)
                {
                    if (preset->name && utils::StringEqualsIgnoreCase(preset->name, def.xModel->name))
                        return preset;
                }
            }

            const auto name = def.xModel ? std::string(def.xModel->name) : std::to_string(m_auto_phys_presets.size() + 1u);

            auto* preset = m_memory.Alloc<PhysPreset>();
            preset->name = m_memory.Dup(name.c_str());
            if (params.mass > 0.0f)
                preset->mass = params.mass;
            if (params.bounce > 0.0f)
                preset->bounce = params.bounce;
            if (params.friction > 0.0f)
                preset->friction = params.friction;
            preset->flags |= 1;

            auto* presetAsset = m_context.AddAsset<AssetPhysPreset>(name, preset);
            m_registration.AddDependency(presetAsset);
            if (m_auto_phys_presets.size() < 0x200u)
                m_auto_phys_presets.emplace_back(preset);

            return preset;
        }

        bool CreateDynEntity(DynEntityDef& def, const DynEntityParams& params)
        {
            std::memset(&def, 0, sizeof(def));

            if (!params.typeName.empty())
            {
                const auto type = std::ranges::find_if(DYN_ENTITY_TYPE_NAMES,
                                                       [&params](const char* typeName)
                                                       {
                                                           return utils::StringEqualsIgnoreCase(typeName, params.typeName);
                                                       });
                def.type = static_cast<DynEntityType>(type == std::end(DYN_ENTITY_TYPE_NAMES) ? 0 : type - std::begin(DYN_ENTITY_TYPE_NAMES));
                if (def.type == DYNENT_TYPE_INVALID)
                {
                    con::error("Invalid Dyn Entity type [{}]", params.typeName);
                    return false;
                }
            }
            else
                def.type = DYNENT_TYPE_CLUTTER;

            if (!params.modelName.empty() && params.modelName[0] == '*')
            {
                const auto brushModel = std::atoi(params.modelName.c_str() + 1);
                if (brushModel < 0 || static_cast<unsigned>(brushModel) >= m_clip_map.numSubModels)
                {
                    con::error("Dyn Entity brush model [{}] is out of range", params.modelName);
                    return false;
                }

                def.brushModel = static_cast<uint16_t>(brushModel);
                const auto& leaf = m_clip_map.cmodels[brushModel].leaf;
                def.contents = leaf.brushContents | leaf.terrainContents;
            }
            else
            {
                def.xModel = LoadDynEntityModel(params.modelName, "xmodel");
                if (!def.xModel)
                    return false;
                def.contents = GetXModelContents(*def.xModel);
            }

            if (!params.physModelName.empty() && params.physModelName[0] == '*')
                def.physicsBrushModel = static_cast<uint16_t>(std::atoi(params.physModelName.c_str() + 1));

            if (!params.destroyFxFile.empty())
            {
                def.destroyFx = Dependency<AssetFx>(params.destroyFxFile);
                if (!def.destroyFx)
                {
                    con::error("Couldn't find fx [{}] for Dyn Entity", params.destroyFxFile);
                    return false;
                }
            }

            if (!params.destroySound.empty())
            {
                const auto hash = ParseSoundHash(params.destroySound);
                if (!hash || *hash == 0u)
                {
                    con::error("Couldn't find destroy sound [{}] for Dyn Entity", params.destroySound);
                    return false;
                }
                def.destroySound = *hash;
            }

            if (!params.destroyedModelName.empty())
            {
                def.destroyedxModel = LoadDynEntityModel(params.destroyedModelName, "destroyed xmodel");
                if (!def.destroyedxModel)
                    return false;
                def.contents = GetXModelContents(*def.destroyedxModel);
            }

            if (!params.destroyPiecesFile.empty())
            {
                con::error("Pieces [{}] of Dyn Entity are not supported", params.destroyPiecesFile);
                return false;
            }

            if (def.type == DYNENT_TYPE_CLUTTER)
            {
                if (def.xModel && def.xModel->physPreset)
                    def.physPreset = def.xModel->physPreset;
                if (def.destroyedxModel && def.destroyedxModel->physPreset)
                    def.physPreset = def.destroyedxModel->physPreset;
                if (!params.physPresetFile.empty())
                {
                    def.physPreset = Dependency<AssetPhysPreset>(params.physPresetFile);
                    if (!def.physPreset)
                    {
                        con::error("Couldn't find physics preset [{}] for Dyn Entity", params.physPresetFile);
                        return false;
                    }
                }
                if (!def.physPreset)
                    def.physPreset = AutoCreatePhysPreset(def, params);
            }

            def.targetname = ScriptString(params.targetname);
            def.target = ScriptString(params.target);
            for (auto i = 0; i < 3; i++)
                def.pose.origin[i] = params.origin[i];
            AnglesToQuat(params.angles, def.pose.quat);

            def.health = params.health;
            if (def.destroyedxModel && def.health == 0)
                def.health = 1;
            def.flags = params.spawnflags;

            int constraints[MAX_ENTITY_CONSTRAINTS]{NO_CONSTRAINT, NO_CONSTRAINT, NO_CONSTRAINT, NO_CONSTRAINT};
            FindConstraints(params.targetname, params.origin, params.angles, ATTACH_POINT_DYNENT, constraints);
            for (auto i = 0u; i < MAX_ENTITY_CONSTRAINTS; i++)
                def.physConstraints[i] = static_cast<int16_t>(constraints[i]);

            return true;
        }

        [[nodiscard]] int GetClientAbsId(const unsigned index, const DynEntityDrawType drawType) const
        {
            if (drawType == DYNENT_DRAW_MODEL)
                return static_cast<int>(index);

            return static_cast<int>(m_clip_map.originalDynEntCount + SPARE_DYN_ENTITY_COUNT + index);
        }

        void GetAttachPoint(const AttachPointType attachType, const uint16_t targetEnt, const vec3_t& pos, float* out) const
        {
            if (attachType == ATTACH_POINT_WORLD)
            {
                for (auto i = 0; i < 3; i++)
                    out[i] = pos.v[i];
                return;
            }

            const auto* nodes = attachType == ATTACH_POINT_ENT ? &m_ents : attachType == ATTACH_POINT_DYNENT ? &m_dents : nullptr;
            if (!nodes)
                return;

            const auto& targetName = m_zone.m_script_strings[targetEnt];
            for (const auto& node : *nodes)
            {
                if (!utils::StringEqualsIgnoreCase(node.targetname, targetName))
                    continue;

                float xform[4][3];
                AnglesToTransform(node.origin, node.angles, xform);
                MatrixTransformVector43(pos.v, xform, out);
            }
        }

        bool LoadDynEntities()
        {
            std::vector<DynEntityParams> dynEntityParams;
            for (const auto& entity : *m_entities)
            {
                if (IsDynEntityClassName(entity.GetString("classname")))
                    dynEntityParams.emplace_back(ParseDynEntityParams(entity));
            }

            if (dynEntityParams.size() >= MAX_DYN_ENTITIES)
            {
                con::error("Found [{}] Dyn Entities, Max is [{}]", dynEntityParams.size(), MAX_DYN_ENTITIES - 1u);
                return false;
            }

            std::vector<DynEntityDef> defs(dynEntityParams.size());
            for (auto i = 0u; i < dynEntityParams.size(); i++)
            {
                if (!CreateDynEntity(defs[i], dynEntityParams[i]))
                    return false;
            }

            std::vector<const XModel*> modelOrder;
            for (const auto& def : defs)
            {
                if (def.xModel && std::ranges::find(modelOrder, def.xModel) == modelOrder.end())
                    modelOrder.emplace_back(def.xModel);
            }

            const auto sortKey = [&modelOrder](const DynEntityDef& def)
            {
                if (!def.xModel)
                    return std::make_pair(0, static_cast<ptrdiff_t>(def.brushModel));

                return std::make_pair(1, std::ranges::find(modelOrder, def.xModel) - modelOrder.begin());
            };
            std::ranges::stable_sort(defs,
                                     [&sortKey](const DynEntityDef& lhs, const DynEntityDef& rhs)
                                     {
                                         return sortKey(lhs) < sortKey(rhs);
                                     });

            std::vector<DynEntityDef> lists[DYNENT_DRAW_COUNT];
            for (auto& def : defs)
            {
                const auto drawType = def.xModel ? DYNENT_DRAW_MODEL : DYNENT_DRAW_BRUSH;
                def.flags |= drawType == DYNENT_DRAW_MODEL ? 0x200 : 0x400;
                lists[drawType].emplace_back(def);
            }

            m_clip_map.originalDynEntCount = static_cast<uint16_t>(lists[DYNENT_DRAW_MODEL].size());
            for (auto i = 0u; i < SPARE_DYN_ENTITY_COUNT; i++)
            {
                auto& spare = lists[DYNENT_DRAW_MODEL].emplace_back();
                std::memset(&spare, 0, sizeof(spare));
                for (auto& constraint : spare.physConstraints)
                    constraint = NO_CONSTRAINT;
            }

            m_clip_map.dynEntCount[0] = static_cast<uint16_t>(lists[DYNENT_DRAW_MODEL].size());
            m_clip_map.dynEntCount[1] = static_cast<uint16_t>(lists[DYNENT_DRAW_BRUSH].size());
            m_clip_map.dynEntCount[2] = m_clip_map.originalDynEntCount;
            m_clip_map.dynEntCount[3] = m_clip_map.dynEntCount[1];

            for (auto drawType = 0u; drawType < DYNENT_DRAW_COUNT; drawType++)
            {
                const auto& list = lists[drawType];
                auto*& defList = m_clip_map.dynEntDefList[drawType];
                defList = nullptr;
                if (list.empty())
                    continue;

                defList = m_memory.Alloc<DynEntityDef>(list.size());
                std::memcpy(defList, list.data(), sizeof(DynEntityDef) * list.size());

                for (auto j = 0u; j < list.size(); j++)
                {
                    for (const auto constraintIndex : defList[j].physConstraints)
                    {
                        if (constraintIndex == NO_CONSTRAINT)
                            continue;

                        auto& constraint = m_clip_map.constraints[constraintIndex];
                        const auto absId = GetClientAbsId(j, static_cast<DynEntityDrawType>(drawType));
                        if (constraint.attach_point_type1 == ATTACH_POINT_DYNENT && constraint.target_ent1 == defList[j].targetname)
                            constraint.target_index1 = absId;
                        if (constraint.attach_point_type2 == ATTACH_POINT_DYNENT && constraint.target_ent2 == defList[j].targetname)
                            constraint.target_index2 = absId;
                    }
                }
            }

            for (const auto& ent : m_ents)
                FindConstraints(ent.targetname, ent.origin, ent.angles, ATTACH_POINT_ENT, nullptr);

            LoadRopes();
            AllocRuntimeLists();

            return true;
        }

        void LoadRopes()
        {
            auto ropeCount = 0;
            for (auto i = 0; i < m_clip_map.num_constraints; i++)
            {
                auto& constraint = m_clip_map.constraints[i];
                if (constraint.type != CONSTRAINT_ROPE)
                    continue;

                if (constraint.distance == 0.0f)
                {
                    float p0[3]{};
                    float p1[3]{};
                    GetAttachPoint(constraint.attach_point_type1, constraint.target_ent1, constraint.pos, p0);
                    GetAttachPoint(constraint.attach_point_type2, constraint.target_ent2, constraint.pos2, p1);

                    constraint.distance = GetAttachPointDistance(p0, p1);
                }

                if (constraint.scale.y != 0.0f)
                    constraint.distance = constraint.distance * constraint.scale.y;

                ropeCount++;
            }

            m_clip_map.max_ropes = ropeCount + EXTRA_ROPES;
            if (m_clip_map.max_ropes > MAX_ROPES)
            {
                con::warn("Max ropes count exceeded! {}", MAX_ROPES);
                m_clip_map.max_ropes = MAX_ROPES;
            }

            m_clip_map.ropes = m_memory.Alloc<rope_t>(static_cast<size_t>(m_clip_map.max_ropes));
        }

        template<typename T> T* AllocRuntimeList(const uint16_t count)
        {
            return count > 0u ? m_memory.Alloc<T>(count) : nullptr;
        }

        void AllocRuntimeLists()
        {
            for (auto drawType = 0u; drawType < DYNENT_DRAW_COUNT; drawType++)
            {
                m_clip_map.dynEntPoseList[drawType] = AllocRuntimeList<DynEntityPose>(m_clip_map.dynEntCount[drawType]);
                m_clip_map.dynEntClientList[drawType] = AllocRuntimeList<DynEntityClient>(m_clip_map.dynEntCount[drawType]);
                m_clip_map.dynEntCollList[drawType] = AllocRuntimeList<DynEntityColl>(m_clip_map.dynEntCount[drawType]);
                m_clip_map.dynEntServerList[drawType] = AllocRuntimeList<DynEntityServer>(m_clip_map.dynEntCount[drawType + 2u]);
                m_clip_map.dynEntCollList[drawType + 2u] = AllocRuntimeList<DynEntityColl>(m_clip_map.dynEntCount[drawType + 2u]);
            }
        }

        MemoryManager& m_memory;
        Zone& m_zone;
        AssetCreationContext& m_context;
        GenericAssetRegistration& m_registration;
        clipMap_t& m_clip_map;
        const std::vector<BspEntity>* m_entities = nullptr;
        std::vector<NodeParams> m_infos;
        std::vector<NodeParams> m_ents;
        std::vector<NodeParams> m_dents;
        std::vector<ConstraintParams> m_constraint_params;
        std::vector<PhysPreset*> m_auto_phys_presets;
    };

    template<typename AssetType> class ClipMapLoader final : public AssetCreator<AssetType>
    {
    public:
        ClipMapLoader(MemoryManager& memory, ISearchPath& searchPath, Zone& zone)
            : m_memory(memory),
              m_search_path(searchPath),
              m_zone(zone)
        {
        }

        AssetCreationResult CreateAsset(const std::string& assetName, AssetCreationContext& context) override
        {
            const auto file = m_search_path.Open(assetName);
            if (!file.IsOpen())
                return AssetCreationResult::NoAction();

            BspFile bsp;
            if (!bsp.Read(*file.m_stream))
            {
                con::error("Could not read bsp file \"{}\"", assetName);
                return AssetCreationResult::Failure();
            }

            auto* clipMap = m_memory.Alloc<clipMap_t>();
            clipMap->name = m_memory.Dup(assetName.c_str());

            AssetRegistration<AssetType> registration(assetName, clipMap);

            ClipMapBspLoader loader(m_memory, bsp, *clipMap);
            if (!loader.Load())
            {
                con::error("Failed to load clipMap \"{}\"", assetName);
                return AssetCreationResult::Failure();
            }

            auto* mapEnts = context.LoadDependency<AssetMapEnts>(assetName);
            if (!mapEnts)
            {
                con::error("Failed to load map ents \"{}\" of clipMap", assetName);
                return AssetCreationResult::Failure();
            }
            registration.AddDependency(mapEnts);
            clipMap->mapEnts = mapEnts->Asset();

            std::string entityString;
            if (const auto* entityLump = bsp.GetLumpData(LUMP_ENTITIES))
                entityString.assign(entityLump->begin(), entityLump->end());
            else if (clipMap->mapEnts->entityString)
                entityString = clipMap->mapEnts->entityString;

            ClipMapEntityLoader entityLoader(m_memory, m_zone, context, registration, *clipMap);
            if (!entityLoader.Load(entityString))
            {
                con::error("Failed to load entities of clipMap \"{}\"", assetName);
                return AssetCreationResult::Failure();
            }

            return AssetCreationResult::Success(context.AddAsset(std::move(registration)));
        }

    private:
        MemoryManager& m_memory;
        ISearchPath& m_search_path;
        Zone& m_zone;
    };
} // namespace

namespace clip_map
{
    std::unique_ptr<AssetCreator<AssetClipMap>> CreateLoaderT5(MemoryManager& memory, ISearchPath& searchPath, Zone& zone)
    {
        return std::make_unique<ClipMapLoader<AssetClipMap>>(memory, searchPath, zone);
    }

    std::unique_ptr<AssetCreator<AssetClipMapPvs>> CreatePvsLoaderT5(MemoryManager& memory, ISearchPath& searchPath, Zone& zone)
    {
        return std::make_unique<ClipMapLoader<AssetClipMapPvs>>(memory, searchPath, zone);
    }
} // namespace clip_map
