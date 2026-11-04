#include "LoaderGameWorldT5.h"

#include "Game/T5/Bsp/BspEntitiesT5.h"
#include "Game/T5/Bsp/BspT5.h"
#include "Game/T5/Bsp/PathConnectionsT5.h"
#include "Utils/Logging/Log.h"
#include "Utils/StringUtils.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

using namespace T5;

namespace
{
    constexpr auto MAX_PATH_NODES = 0x1F80u;
    constexpr auto RUNTIME_NODE_COUNT = 128u;
    constexpr auto DEG_TO_RAD = 0.017453292f;
    constexpr auto MAX_LEAF_NODE_RANGE = 192.0f;
    constexpr auto MIN_SPLIT_NODE_COUNT = 4;

    constexpr auto NODE_FLAG_NOT_LINKED = 0x1u;
    constexpr auto NODE_FLAG_NOT_CHAIN = 0x2u;
    constexpr auto NODE_FLAG_STANCES = 0x1Cu;
    constexpr auto NODE_FLAG_TURRET = 0x40u;
    constexpr auto NODE_FLAG_ANGLE_VALID = 0x8000u;

    constexpr const char* NODE_CLASS_NAMES[]{
        "node_pathnode",
        "node_cover_stand",
        "node_cover_crouch",
        "node_cover_crouch_window",
        "node_cover_prone",
        "node_cover_right",
        "node_cover_left",
        "node_cover_wide_right",
        "node_cover_wide_left",
        "node_cover_pillar",
        "node_concealment_stand",
        "node_concealment_crouch",
        "node_concealment_prone",
        "node_reacquire",
        "node_balcony",
        "node_scripted",
        "node_negotiation_begin",
        "node_negotiation_end",
        "node_turret",
        "node_guard",
    };

    constexpr uint16_t NODE_FLAGS_CLEARED[NODE_NUMTYPES]{0x0, 0x0, 0x4,  0x8, 0x8, 0x10, 0x0, 0x0, 0x0, 0x0, 0x0,
                                                         0x4, 0x8, 0x10, 0x0, 0x4, 0x0,  0x1, 0x1, 0x8, 0x0};

    constexpr float ACTOR_SIZE = 15.0f - -15.0f + 1.0f;
    constexpr float ACTOR_HEIGHT = 72.0f - 0.0f + 1.0f;

    size_t ParseFloats(const std::string& text, float* values, const size_t maxCount)
    {
        const auto* current = text.data();
        const auto* end = text.data() + text.size();
        auto count = 0uz;
        while (count < maxCount)
        {
            while (current < end && (*current == ' ' || *current == '\t' || *current == '\n' || *current == '\r'))
                current++;
            if (current < end && *current == '+')
                current++;

            const auto [next, error] = std::from_chars(current, end, values[count]);
            if (error != std::errc())
                break;

            current = next;
            count++;
        }

        return count;
    }

    float AngleNormalize360(const float angle)
    {
        const auto scaledAngle = angle * (1.0f / 360.0f);
        const auto floored = static_cast<float>(std::floor(static_cast<double>(scaledAngle)));
        const auto result = static_cast<float>((static_cast<double>(scaledAngle) - floored) * 360.0);
        const auto wrapped = result - 360.0f;

        return wrapped >= 0.0f ? wrapped : result;
    }

    class PathDataLoader
    {
    public:
        PathDataLoader(MemoryManager& memory, Zone& zone, GenericAssetRegistration& registration, const std::string& name, PathData& path)
            : m_memory(memory),
              m_zone(zone),
              m_registration(registration),
              m_name(name),
              m_path(path)
        {
        }

        bool Load(const std::vector<BspEntity>& entities, const BspFile& bsp)
        {
            if (!CreateNodes(entities))
                return false;

            BuildChains();
            if (!m_nodes.empty())
            {
                if (!LoadPaths(bsp))
                    return false;

                FindOverlappingNodes();
                BuildNodeTree();
            }
            Finish();

            return true;
        }

    private:
        uint16_t ScriptString(const std::string& value)
        {
            const auto scriptString = m_zone.m_script_strings.AddOrGetScriptString(value);
            m_registration.AddScriptString(scriptString);

            return static_cast<uint16_t>(scriptString);
        }

        static std::optional<nodeType> GetNodeType(const std::string& className)
        {
            for (auto i = 0u; i < std::extent_v<decltype(NODE_CLASS_NAMES)>; i++)
            {
                if (className == NODE_CLASS_NAMES[i])
                    return static_cast<nodeType>(i + 1u);
            }

            return std::nullopt;
        }

        void ParseNodeField(pathnode_constant_t& node, const std::string& key, const std::string& value)
        {
            if (utils::StringEqualsIgnoreCase(key, "targetname"))
                node.targetname = ScriptString(value);
            else if (utils::StringEqualsIgnoreCase(key, "target"))
                node.target = ScriptString(value);
            else if (utils::StringEqualsIgnoreCase(key, "animscript"))
                node.animscript = ScriptString(value);
            else if (utils::StringEqualsIgnoreCase(key, "script_noteworthy"))
                node.script_noteworthy = ScriptString(value);
            else if (utils::StringEqualsIgnoreCase(key, "script_linkname"))
                node.script_linkName = ScriptString(value);
            else if (utils::StringEqualsIgnoreCase(key, "origin"))
            {
                float origin[3]{};
                ParseFloats(value, origin, 3u);
                std::memcpy(node.vOrigin, origin, sizeof(origin));
            }
            else if (utils::StringEqualsIgnoreCase(key, "angles"))
            {
                float angles[3]{};
                ParseFloats(value, angles, 3u);
                node.fAngle = AngleNormalize360(angles[1]);
            }
            else if (utils::StringEqualsIgnoreCase(key, "radius"))
                node.fRadius = static_cast<float>(std::atof(value.c_str()));
            else if (utils::StringEqualsIgnoreCase(key, "minusedistsq"))
                node.minUseDistSq = static_cast<float>(std::atof(value.c_str()));
            else if (utils::StringEqualsIgnoreCase(key, "spawnflags"))
                node.spawnflags = static_cast<uint16_t>(std::atoi(value.c_str()));
        }

        bool CreateNodes(const std::vector<BspEntity>& entities)
        {
            for (const auto& entity : entities)
            {
                const auto className = entity.GetString("classname");
                if (!className.starts_with("node_"))
                    continue;

                const auto type = GetNodeType(className);
                if (!type)
                {
                    con::error("GameWorld \"{}\" has no spawn function for \"{}\"", m_name, className);
                    return false;
                }

                if (m_nodes.size() >= MAX_PATH_NODES)
                {
                    con::error("GameWorld \"{}\" exceeds {} path nodes", m_name, MAX_PATH_NODES);
                    return false;
                }

                auto& node = m_nodes.emplace_back().constant;
                node.fAngle = -1.0f;
                for (const auto& [key, value] : entity.Values())
                    ParseNodeField(node, key, value);

                if (node.fAngle == -1.0f)
                    node.fAngle = 0.0f;
                else
                    node.spawnflags |= NODE_FLAG_ANGLE_VALID;

                const auto radians = static_cast<long double>(node.fAngle * DEG_TO_RAD);
                node.forward[0] = static_cast<float>(std::cos(radians));
                node.forward[1] = static_cast<float>(std::sin(radians));

                node.spawnflags &= static_cast<uint16_t>(~NODE_FLAGS_CLEARED[*type]);
                node.wOverlapNode[0] = -1;
                node.wOverlapNode[1] = -1;

                if ((node.spawnflags & NODE_FLAG_STANCES) == NODE_FLAG_STANCES)
                {
                    con::warn("GameWorld \"{}\" node at ({} {} {}) has no allowed stances", m_name, node.vOrigin[0], node.vOrigin[1], node.vOrigin[2]);
                    node.spawnflags &= static_cast<uint16_t>(~NODE_FLAG_STANCES);
                }

                if (*type == NODE_NEGOTIATION_BEGIN || *type == NODE_NEGOTIATION_END)
                    node.spawnflags |= NODE_FLAG_NOT_CHAIN;
                else if (*type == NODE_TURRET)
                    node.spawnflags |= NODE_FLAG_TURRET;

                node.type = *type;
            }

            return true;
        }

        void BuildChain(const size_t nodeIndex, const int16_t chainId, const int16_t chainDepth)
        {
            auto& node = m_nodes[nodeIndex].constant;
            node.wChainId = chainId;
            node.wChainDepth = chainDepth;
            if (node.target == 0u)
                return;

            for (auto i = 0uz; i < m_nodes.size(); i++)
            {
                const auto& child = m_nodes[i].constant;
                if (child.targetname != node.target || (child.spawnflags & NODE_FLAG_NOT_CHAIN))
                    continue;

                BuildChain(i, chainId, static_cast<int16_t>(chainDepth ? chainDepth + 1 : 0));
            }
        }

        void BuildChains()
        {
            for (auto& node : m_nodes)
                node.constant.wChainParent = -1;

            for (auto i = 0uz; i < m_nodes.size(); i++)
            {
                auto& node = m_nodes[i].constant;
                if (node.targetname == 0u || (node.spawnflags & NODE_FLAG_NOT_CHAIN))
                    continue;

                for (auto j = 0uz; j < m_nodes.size(); j++)
                {
                    auto& parent = m_nodes[j].constant;
                    if (parent.target != node.targetname)
                        continue;

                    if (node.wChainParent >= 0 || i == j)
                        con::warn("GameWorld \"{}\" node {} has an invalid chain parent", m_name, i);

                    node.wChainParent = static_cast<int16_t>(j);
                    parent.wChainId = -1;
                }
            }

            int16_t chainId = 1;
            for (auto i = 0uz; i < m_nodes.size(); i++)
            {
                const auto& node = m_nodes[i].constant;
                if (node.wChainId >= 0 || node.wChainParent >= 0)
                    continue;

                if (node.spawnflags & NODE_FLAG_NOT_CHAIN)
                    BuildChain(i, 0, 0);
                else
                    BuildChain(i, chainId++, 1);
            }

            m_node_for_chain_node.resize(m_nodes.size());
            for (auto i = 0uz; i < m_nodes.size(); i++)
                m_node_for_chain_node[i] = static_cast<uint16_t>(i);

            std::ranges::sort(m_node_for_chain_node,
                              [this](const uint16_t index0, const uint16_t index1)
                              {
                                  const auto& node0 = m_nodes[index0].constant;
                                  const auto& node1 = m_nodes[index1].constant;
                                  if (node0.wChainId != node1.wChainId)
                                      return node0.wChainId > node1.wChainId;
                                  if (node0.wChainDepth != node1.wChainDepth)
                                      return node0.wChainDepth < node1.wChainDepth;
                                  if (node0.wChainParent != node1.wChainParent)
                                      return node0.wChainParent < node1.wChainParent;
                                  if (node0.type != node1.type)
                                      return node0.type > node1.type;

                                  return index0 < index1;
                              });

            m_chain_node_for_node.resize(m_nodes.size());
            for (auto i = 0uz; i < m_nodes.size(); i++)
                m_chain_node_for_node[m_node_for_chain_node[i]] = static_cast<uint16_t>(i);

            m_chain_node_count = 0u;
            while (m_chain_node_count < m_nodes.size() && m_nodes[m_node_for_chain_node[m_chain_node_count]].constant.wChainId != 0)
                m_chain_node_count++;
        }

        bool LoadPaths(const BspFile& bsp)
        {
            const auto* lump = bsp.GetLumpData(LUMP_PATHCONNECTIONS);
            if (!lump || lump->empty())
            {
                con::error("GameWorld \"{}\" does not have paths generated", m_name);
                return false;
            }

            PathConnections connections;
            if (!connections.Read(*lump) || connections.m_node_links.size() != m_nodes.size())
            {
                con::error("GameWorld \"{}\" path connections do not match its {} path nodes", m_name, m_nodes.size());
                return false;
            }

            for (auto i = 0uz; i < m_nodes.size(); i++)
            {
                const auto& diskLinks = connections.m_node_links[i];
                auto& node = m_nodes[i].constant;
                node.totalLinkCount = static_cast<uint16_t>(diskLinks.size());
                if (diskLinks.empty())
                    continue;

                node.Links = m_memory.Alloc<pathlink_s>(diskLinks.size());
                for (auto j = 0uz; j < diskLinks.size(); j++)
                {
                    const auto& diskLink = diskLinks[j];
                    if (diskLink.nodeNum >= m_nodes.size())
                    {
                        con::error("GameWorld \"{}\" node {} links to invalid node {}", m_name, i, diskLink.nodeNum);
                        return false;
                    }

                    auto& link = node.Links[j];
                    link.fDist = diskLink.dist;
                    link.nodeNum = diskLink.nodeNum;

                    const auto& to = m_nodes[diskLink.nodeNum].constant;
                    link.negotiationLink = node.type == NODE_NEGOTIATION_BEGIN && to.type == NODE_NEGOTIATION_END && node.target == to.targetname;
                }
            }

            m_vis = std::move(connections.m_vis);
            return true;
        }

        void FindOverlappingNodes()
        {
            constexpr auto radiusSq = ACTOR_SIZE * ACTOR_SIZE;

            for (auto i = 0uz; i < m_nodes.size(); i++)
            {
                auto& node = m_nodes[i].constant;
                for (auto j = 0uz; j < i; j++)
                {
                    auto& other = m_nodes[j].constant;
                    const float delta[3]{
                        node.vOrigin[0] - other.vOrigin[0],
                        node.vOrigin[1] - other.vOrigin[1],
                        node.vOrigin[2] - other.vOrigin[2],
                    };
                    const auto distSq = delta[0] * delta[0] + delta[1] * delta[1];
                    if (distSq > radiusSq || -ACTOR_HEIGHT > delta[2] || delta[2] > ACTOR_HEIGHT)
                        continue;

                    if (1.0f > distSq && !((node.spawnflags | other.spawnflags) & NODE_FLAG_NOT_LINKED))
                        con::warn("GameWorld \"{}\" has duplicate linking nodes {} and {}", m_name, i, j);

                    if (node.wOverlapNode[0] < 0)
                        node.wOverlapNode[0] = static_cast<int16_t>(j);
                    else if (node.wOverlapNode[1] < 0)
                        node.wOverlapNode[1] = static_cast<int16_t>(j);

                    if (other.wOverlapNode[0] < 0)
                        other.wOverlapNode[0] = static_cast<int16_t>(i);
                    else if (other.wOverlapNode[1] < 0)
                        other.wOverlapNode[1] = static_cast<int16_t>(i);
                }
            }
        }

        struct TreeBuildNode
        {
            int axis;
            float dist;
            size_t children[2];
            size_t firstNode;
            int nodeCount;
        };

        float NodeOrigin(const uint16_t nodeIndex, const int axis) const
        {
            return m_nodes[nodeIndex].constant.vOrigin[axis];
        }

        size_t AllocLeaf(const size_t firstNode, const int nodeCount)
        {
            m_tree.emplace_back(TreeBuildNode{-1, 0.0f, {}, firstNode, nodeCount});
            return m_tree.size() - 1u;
        }

        size_t BuildNodeTree(const size_t firstNode, const int nodeCount)
        {
            if (nodeCount < MIN_SPLIT_NODE_COUNT)
                return AllocLeaf(firstNode, nodeCount);

            auto* nodes = m_indirect_nodes.data() + firstNode;
            float mins[2]{NodeOrigin(nodes[0], 0), NodeOrigin(nodes[0], 1)};
            float maxs[2]{mins[0], mins[1]};
            for (auto i = 1; i < nodeCount; i++)
            {
                for (auto axis = 0; axis < 2; axis++)
                {
                    const auto value = NodeOrigin(nodes[i], axis);
                    if (mins[axis] > value)
                        mins[axis] = value;
                    else if (value > maxs[axis])
                        maxs[axis] = value;
                }
            }

            const auto axis = maxs[1] - mins[1] > maxs[0] - mins[0] ? 1 : 0;
            if (MAX_LEAF_NODE_RANGE >= maxs[axis] - mins[axis])
                return AllocLeaf(firstNode, nodeCount);

            const auto dist = (maxs[axis] + mins[axis]) * 0.5f;
            auto left = 0;
            auto right = nodeCount - 1;
            while (true)
            {
                while (dist > NodeOrigin(nodes[left], axis))
                    left++;
                while (NodeOrigin(nodes[right], axis) > dist)
                    right--;
                if (left >= right)
                    break;

                std::swap(nodes[left], nodes[right]);
                left++;
                right--;
            }

            while (left * 2 < nodeCount && NodeOrigin(nodes[left], axis) == dist)
                left++;
            while (left * 2 > nodeCount && NodeOrigin(nodes[left - 1], axis) == dist)
                left--;

            const auto child0 = BuildNodeTree(firstNode, left);
            const auto child1 = BuildNodeTree(firstNode + static_cast<size_t>(left), nodeCount - left);
            m_tree.emplace_back(TreeBuildNode{
                axis, dist, {child0, child1},
                  0u, 0
            });
            return m_tree.size() - 1u;
        }

        void BuildNodeTree()
        {
            m_indirect_nodes.resize(m_nodes.size());
            for (auto i = 0uz; i < m_nodes.size(); i++)
                m_indirect_nodes[i] = static_cast<uint16_t>(i);

            m_tree.clear();
            BuildNodeTree(0u, static_cast<int>(m_nodes.size()));
        }

        void Finish()
        {
            const auto nodeCount = m_nodes.size();
            m_path.nodeCount = static_cast<unsigned>(nodeCount);
            m_path.nodes = m_memory.Alloc<pathnode_t>(nodeCount + RUNTIME_NODE_COUNT);
            std::ranges::copy(m_nodes, m_path.nodes);
            m_path.basenodes = m_memory.Alloc<pathbasenode_t>(nodeCount + RUNTIME_NODE_COUNT);

            m_path.chainNodeCount = static_cast<unsigned>(m_chain_node_count);
            m_path.chainNodeForNode = m_memory.Alloc<uint16_t>(nodeCount);
            std::ranges::copy(m_chain_node_for_node, m_path.chainNodeForNode);
            m_path.nodeForChainNode = m_memory.Alloc<uint16_t>(nodeCount);
            std::ranges::copy(m_node_for_chain_node, m_path.nodeForChainNode);

            m_path.visBytes = static_cast<int>(m_vis.size());
            if (!m_vis.empty())
            {
                m_path.pathVis = m_memory.Alloc<char>(m_vis.size());
                std::memcpy(m_path.pathVis, m_vis.data(), m_vis.size());
            }

            const auto treeCount = m_tree.size();
            m_path.nodeTreeCount = static_cast<int>(treeCount);
            if (treeCount == 0u)
                return;

            m_path.nodeTree = m_memory.Alloc<pathnode_tree_t>(treeCount);
            for (auto i = 0uz; i < treeCount; i++)
            {
                const auto& source = m_tree[i];
                auto& tree = m_path.nodeTree[treeCount - 1u - i];
                tree.axis = source.axis;
                tree.dist = source.dist;
                if (source.axis < 0)
                {
                    tree.u.s.nodeCount = source.nodeCount;
                    tree.u.s.nodes = m_memory.Alloc<uint16_t>(static_cast<size_t>(source.nodeCount));
                    std::copy_n(m_indirect_nodes.begin() + static_cast<std::ptrdiff_t>(source.firstNode), source.nodeCount, tree.u.s.nodes);
                }
                else
                {
                    tree.u.child[0] = &m_path.nodeTree[treeCount - 1u - source.children[0]];
                    tree.u.child[1] = &m_path.nodeTree[treeCount - 1u - source.children[1]];
                }
            }
        }

        MemoryManager& m_memory;
        Zone& m_zone;
        GenericAssetRegistration& m_registration;
        const std::string& m_name;
        PathData& m_path;

        std::vector<pathnode_t> m_nodes;
        std::vector<uint16_t> m_chain_node_for_node;
        std::vector<uint16_t> m_node_for_chain_node;
        size_t m_chain_node_count = 0u;
        std::vector<uint8_t> m_vis;
        std::vector<uint16_t> m_indirect_nodes;
        std::vector<TreeBuildNode> m_tree;
    };

    template<typename AssetType> class GameWorldLoader final : public AssetCreator<AssetType>
    {
    public:
        GameWorldLoader(MemoryManager& memory, ISearchPath& searchPath, Zone& zone)
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

            std::string entityString;
            if (const auto* entityLump = bsp.GetLumpData(LUMP_ENTITIES))
                entityString.assign(entityLump->begin(), std::ranges::find(*entityLump, 0u));
            else if (const auto entsFile = m_search_path.Open(assetName + ".ents"); entsFile.IsOpen())
                entityString.assign(std::istreambuf_iterator<char>(*entsFile.m_stream), std::istreambuf_iterator<char>());

            const auto entities = ParseBspEntities(entityString);
            if (!entities)
            {
                con::error("Could not parse entities of GameWorld \"{}\"", assetName);
                return AssetCreationResult::Failure();
            }

            auto* gameWorld = m_memory.Alloc<typename AssetType::Type>();
            gameWorld->name = m_memory.Dup(assetName.c_str());

            AssetRegistration<AssetType> registration(assetName, gameWorld);
            PathDataLoader loader(m_memory, m_zone, registration, assetName, gameWorld->path);
            if (!loader.Load(*entities, bsp))
            {
                con::error("Failed to load GameWorld \"{}\"", assetName);
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

namespace game_world
{
    std::unique_ptr<AssetCreator<AssetGameWorldSp>> CreateSpLoaderT5(MemoryManager& memory, ISearchPath& searchPath, Zone& zone)
    {
        return std::make_unique<GameWorldLoader<AssetGameWorldSp>>(memory, searchPath, zone);
    }

    std::unique_ptr<AssetCreator<AssetGameWorldMp>> CreateMpLoaderT5(MemoryManager& memory, ISearchPath& searchPath, Zone& zone)
    {
        return std::make_unique<GameWorldLoader<AssetGameWorldMp>>(memory, searchPath, zone);
    }
} // namespace game_world
