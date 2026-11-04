#include "LoaderComWorldT5.h"

#include "Game/T5/Bsp/BspT5.h"
#include "Utils/Logging/Log.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

using namespace T5;

namespace
{
    constexpr auto BURNABLE_SAMPLE_COUNT = 32u;

    float CosOfSumOfArcCos(const float cos0, const float cos1)
    {
        const auto sinSq0 = 1.0f - cos0 * cos0;
        const auto sinSq1 = 1.0f - cos1 * cos1;
        const auto product = static_cast<float>(static_cast<double>(cos0) * cos1);
        const auto root = static_cast<float>(std::sqrt(static_cast<double>(sinSq0) * sinSq1));

        return product - root;
    }

    class ComWorldBspLoader
    {
    public:
        ComWorldBspLoader(MemoryManager& memory, const BspFile& bsp, ComWorld& comWorld)
            : m_memory(memory),
              m_bsp(bsp),
              m_com_world(comWorld)
        {
        }

        bool Load()
        {
            return LoadPrimaryLights() && LoadWaterData() && LoadBurnableData();
        }

    private:
        const char* GetLightDefName(const std::string& defName, const unsigned lightCount) const
        {
            for (auto i = 0u; i < lightCount; i++)
            {
                const auto* existingDefName = m_com_world.primaryLights[i].defName;
                if (existingDefName && defName == existingDefName)
                    return existingDefName;
            }

            return m_memory.Dup(defName.c_str());
        }

        bool LoadPrimaryLights()
        {
            const auto diskLights = m_bsp.GetLump<DiskPrimaryLight>(LUMP_PRIMARY_LIGHTS);
            if (!diskLights || diskLights->size() <= 1u)
            {
                con::error("ComWorld \"{}\" has no primary lights", m_com_world.name);
                return false;
            }

            m_com_world.primaryLightCount = static_cast<unsigned>(diskLights->size());
            m_com_world.primaryLights = m_memory.Alloc<ComPrimaryLight>(diskLights->size());

            for (auto i = 0u; i < m_com_world.primaryLightCount; i++)
            {
                const auto& in = (*diskLights)[i];
                auto& out = m_com_world.primaryLights[i];

                out.type = static_cast<char>(in.type);
                out.canUseShadowMap = static_cast<char>(in.canUseShadowMap);
                out.cullDist = static_cast<int16_t>(in.cullDist);
                out.exponent = static_cast<char>(static_cast<uint8_t>(in.exponent));
                out.priority = static_cast<char>(in.priority);
                std::memcpy(out.color, in.color, sizeof(out.color));
                std::memcpy(out.dir, in.dir, sizeof(out.dir));
                std::memcpy(out.origin, in.origin, sizeof(out.origin));
                out.radius = in.radius;
                out.cosHalfFovOuter = in.cosHalfFovOuter;
                out.cosHalfFovInner = in.cosHalfFovInner;
                out.rotationLimit = in.rotationLimit;
                out.translationLimit = in.translationLimit;

                if (in.type == 0u || in.type == 1u)
                {
                    out.defName = nullptr;
                    out.cosHalfFovExpanded = in.cosHalfFovOuter;
                }
                else
                {
                    out.defName = GetLightDefName(std::string(in.defName, strnlen(in.defName, sizeof(in.defName))), i);

                    if (out.cosHalfFovOuter >= out.cosHalfFovInner)
                        out.cosHalfFovInner = out.cosHalfFovOuter * 0.75f + 0.25f;

                    if (out.rotationLimit == 1.0f)
                        out.cosHalfFovExpanded = out.cosHalfFovOuter;
                    else if (-out.cosHalfFovOuter >= out.rotationLimit)
                        out.cosHalfFovExpanded = -1.0f;
                    else
                        out.cosHalfFovExpanded = CosOfSumOfArcCos(out.cosHalfFovOuter, out.rotationLimit);
                }

                std::memcpy(out.diffuseColor, in.color, sizeof(in.color));
                out.diffuseColor[3] = 0.0f;
                std::memcpy(out.specularColor, in.specularColor, sizeof(in.specularColor));
                out.specularColor[3] = 0.0f;
                std::memcpy(out.attenuation, in.attenuation, sizeof(in.attenuation));
                out.attenuation[3] = 0.0f;

                const auto falloffRange = in.radius - in.falloffStart;
                out.falloff[0] = in.falloffStart;
                out.falloff[1] = in.radius;
                out.falloff[2] = falloffRange * in.falloffLerp[0] + in.falloffStart;
                out.falloff[3] = falloffRange * in.falloffLerp[1] + in.falloffStart;

                std::memcpy(out.angle, in.angle, sizeof(in.angle));
                out.angle[3] = in.angleW;
                out.mipDistance = in.mipDistance;
                std::memcpy(out.aAbB, in.aAbB, sizeof(out.aAbB));
                std::memcpy(out.cookieControl0, in.cookieControl0, sizeof(out.cookieControl0));
                std::memcpy(out.cookieControl1, in.cookieControl1, sizeof(out.cookieControl1));
                std::memcpy(out.cookieControl2, in.cookieControl2, sizeof(out.cookieControl2));
            }

            return true;
        }

        bool LoadWaterData() const
        {
            const auto headers = m_bsp.GetLump<DiskWaterHeader>(LUMP_WATERHEADER);
            const auto cells = m_bsp.GetLump<DiskWaterCell>(LUMP_WATERCELLS);
            if (!headers || headers->size() > 1u || !cells)
            {
                con::error("ComWorld \"{}\" has invalid water data", m_com_world.name);
                return false;
            }

            if (headers->empty())
                return true;

            const auto& header = headers->front();
            m_com_world.waterHeader = ComWaterHeader{header.minx, header.miny, header.maxx, header.maxy};
            m_com_world.numWaterCells = static_cast<unsigned>(cells->size());
            if (cells->empty())
                return true;

            m_com_world.waterCells = m_memory.Alloc<ComWaterCell>(cells->size());
            for (auto i = 0u; i < m_com_world.numWaterCells; i++)
            {
                const auto& in = (*cells)[i];
                auto& out = m_com_world.waterCells[i];
                out.waterheight = in.waterheight;
                out.flooroffset = static_cast<char>(in.flooroffset);
                out.shoredist = static_cast<char>(in.shoredist);
                std::memcpy(out.color, in.color, sizeof(out.color));
            }

            return true;
        }

        bool LoadBurnableData() const
        {
            const auto headers = m_bsp.GetLump<DiskBurnableHeader>(LUMP_BURNABLEHEADER);
            const auto cells = m_bsp.GetLump<DiskBurnableCell>(LUMP_BURNABLECELLS);
            const auto blocks = m_bsp.GetLump<DiskBurnableBlock>(LUMP_BURNABLECELLDATA);
            if (!headers || headers->size() > 1u || !cells || !blocks)
            {
                con::error("ComWorld \"{}\" has invalid burnable data", m_com_world.name);
                return false;
            }

            if (headers->empty())
                return true;

            const auto& header = headers->front();
            m_com_world.burnableHeader = ComBurnableHeader{header.minx, header.miny, header.maxx, header.maxy};
            m_com_world.numBurnableCells = static_cast<unsigned>(cells->size());
            if (cells->empty())
                return true;

            m_com_world.burnableCells = m_memory.Alloc<ComBurnableCell>(cells->size());
            for (auto i = 0u; i < m_com_world.numBurnableCells; i++)
            {
                const auto& in = (*cells)[i];
                auto& out = m_com_world.burnableCells[i];
                if (in.dataIndex < 0 || static_cast<size_t>(in.dataIndex) >= blocks->size())
                {
                    con::error("ComWorld \"{}\" burnable cell {} has an invalid data index", m_com_world.name, i);
                    return false;
                }

                out.x = in.x;
                out.y = in.y;
                out.data = m_memory.Alloc<ComBurnableSample>(BURNABLE_SAMPLE_COUNT);
                const auto& block = (*blocks)[in.dataIndex];
                for (auto sample = 0u; sample < BURNABLE_SAMPLE_COUNT; sample++)
                    out.data[sample].state = static_cast<char>(block.states[sample]);
            }

            std::stable_sort(m_com_world.burnableCells,
                             m_com_world.burnableCells + m_com_world.numBurnableCells,
                             [](const ComBurnableCell& a, const ComBurnableCell& b)
                             {
                                 if (a.y != b.y)
                                     return a.y < b.y;

                                 return a.x < b.x;
                             });

            return true;
        }

        MemoryManager& m_memory;
        const BspFile& m_bsp;
        ComWorld& m_com_world;
    };

    class ComWorldLoader final : public AssetCreator<AssetComWorld>
    {
    public:
        ComWorldLoader(MemoryManager& memory, ISearchPath& searchPath)
            : m_memory(memory),
              m_search_path(searchPath)
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

            auto* comWorld = m_memory.Alloc<ComWorld>();
            comWorld->name = m_memory.Dup(assetName.c_str());
            comWorld->isInUse = 1;

            ComWorldBspLoader loader(m_memory, bsp, *comWorld);
            if (!loader.Load())
            {
                con::error("Failed to load ComWorld \"{}\"", assetName);
                return AssetCreationResult::Failure();
            }

            return AssetCreationResult::Success(context.AddAsset<AssetComWorld>(assetName, comWorld));
        }

    private:
        MemoryManager& m_memory;
        ISearchPath& m_search_path;
    };
} // namespace

namespace com_world
{
    std::unique_ptr<AssetCreator<AssetComWorld>> CreateLoaderT5(MemoryManager& memory, ISearchPath& searchPath)
    {
        return std::make_unique<ComWorldLoader>(memory, searchPath);
    }
} // namespace com_world
