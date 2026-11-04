#include "ComWorldDumperT5.h"

#include "BspDumperStateT5.h"
#include "Game/T5/Bsp/BspT5.h"
#include "Utils/Logging/Log.h"

#include <cfloat>
#include <cmath>
#include <cstring>
#include <vector>

using namespace T5;

namespace
{
    constexpr auto BURNABLE_SAMPLE_COUNT = 32u;
    constexpr auto MAX_FALLOFF_SEARCH_STEPS = 64u;

    class ComWorldBspDumper
    {
    public:
        explicit ComWorldBspDumper(const ComWorld& comWorld)
            : m_com_world(comWorld)
        {
        }

        void Dump(BspFile& bsp) const
        {
            DumpPrimaryLights(bsp);
            DumpWaterData(bsp);
            DumpBurnableData(bsp);
        }

    private:
        float FindFalloffLerp(const ComPrimaryLight& light, const unsigned falloffIndex) const
        {
            const auto start = light.falloff[0];
            const auto range = light.falloff[1] - start;
            const auto target = light.falloff[falloffIndex];
            if (range == 0.0f)
                return 0.0f;

            const auto estimate = (target - start) / range;
            auto below = estimate;
            auto above = estimate;
            for (auto step = 0u; step <= MAX_FALLOFF_SEARCH_STEPS; step++)
            {
                if (range * below + start == target)
                    return below;
                if (range * above + start == target)
                    return above;

                below = std::nextafter(below, -FLT_MAX);
                above = std::nextafter(above, FLT_MAX);
            }

            con::warn("ComWorld \"{}\" light falloff {} could not be reproduced", m_com_world.name, falloffIndex);
            return estimate;
        }

        void DumpPrimaryLights(BspFile& bsp) const
        {
            std::vector<DiskPrimaryLight> lights(m_com_world.primaryLightCount);
            for (auto i = 0u; i < m_com_world.primaryLightCount; i++)
            {
                const auto& in = m_com_world.primaryLights[i];
                auto& out = lights[i];

                out.type = static_cast<uint8_t>(in.type);
                out.canUseShadowMap = static_cast<uint8_t>(in.canUseShadowMap);
                out.cullDist = static_cast<uint16_t>(in.cullDist);
                out.priority = static_cast<uint8_t>(in.priority);
                std::memcpy(out.dir, in.dir, sizeof(out.dir));
                std::memcpy(out.origin, in.origin, sizeof(out.origin));
                std::memcpy(out.color, in.diffuseColor, sizeof(out.color));
                std::memcpy(out.specularColor, in.specularColor, sizeof(out.specularColor));
                std::memcpy(out.attenuation, in.attenuation, sizeof(out.attenuation));
                std::memcpy(out.angle, in.angle, sizeof(out.angle));
                out.falloffStart = in.falloff[0];
                out.radius = in.falloff[1];
                out.falloffLerp[0] = FindFalloffLerp(in, 2u);
                out.falloffLerp[1] = FindFalloffLerp(in, 3u);
                out.cosHalfFovOuter = in.cosHalfFovOuter;
                out.cosHalfFovInner = in.cosHalfFovInner;
                out.exponent = static_cast<uint8_t>(in.exponent);
                out.rotationLimit = in.rotationLimit;
                out.translationLimit = in.translationLimit;
                out.angleW = in.angle[3];
                out.mipDistance = in.mipDistance;
                std::memcpy(out.aAbB, in.aAbB, sizeof(out.aAbB));
                std::memcpy(out.cookieControl0, in.cookieControl0, sizeof(out.cookieControl0));
                std::memcpy(out.cookieControl1, in.cookieControl1, sizeof(out.cookieControl1));
                std::memcpy(out.cookieControl2, in.cookieControl2, sizeof(out.cookieControl2));

                if (in.defName)
                {
                    if (std::strlen(in.defName) >= sizeof(out.defName))
                        con::warn("ComWorld \"{}\" light {} def name \"{}\" is too long", m_com_world.name, i, in.defName);
                    std::strncpy(out.defName, in.defName, sizeof(out.defName) - 1u);
                }
            }

            bsp.SetLump(LUMP_PRIMARY_LIGHTS, lights);
        }

        void DumpWaterData(BspFile& bsp) const
        {
            const auto& header = m_com_world.waterHeader;
            if (m_com_world.numWaterCells == 0u && header.minx == 0 && header.miny == 0 && header.maxx == 0 && header.maxy == 0)
                return;

            const DiskWaterHeader diskHeader{header.minx, header.miny, header.maxx, header.maxy, 0};
            bsp.SetLump(LUMP_WATERHEADER, &diskHeader, 1u);

            std::vector<DiskWaterCell> cells(m_com_world.numWaterCells);
            for (auto i = 0u; i < m_com_world.numWaterCells; i++)
            {
                const auto& in = m_com_world.waterCells[i];
                auto& out = cells[i];
                out.waterheight = in.waterheight;
                out.flooroffset = static_cast<uint8_t>(in.flooroffset);
                out.shoredist = static_cast<uint8_t>(in.shoredist);
                std::memcpy(out.color, in.color, sizeof(out.color));
            }

            bsp.SetLump(LUMP_WATERCELLS, cells);
        }

        void DumpBurnableData(BspFile& bsp) const
        {
            const auto& header = m_com_world.burnableHeader;
            if (m_com_world.numBurnableCells == 0u && header.minx == 0 && header.miny == 0 && header.maxx == 0 && header.maxy == 0)
                return;

            const DiskBurnableHeader diskHeader{header.minx, header.miny, header.maxx, header.maxy};
            bsp.SetLump(LUMP_BURNABLEHEADER, &diskHeader, 1u);

            std::vector<DiskBurnableCell> cells(m_com_world.numBurnableCells);
            std::vector<DiskBurnableBlock> blocks(m_com_world.numBurnableCells);
            for (auto i = 0u; i < m_com_world.numBurnableCells; i++)
            {
                const auto& in = m_com_world.burnableCells[i];
                cells[i] = DiskBurnableCell{in.x, in.y, 0, static_cast<int>(i)};
                if (!in.data)
                    continue;

                for (auto sample = 0u; sample < BURNABLE_SAMPLE_COUNT; sample++)
                    blocks[i].states[sample] = static_cast<uint8_t>(in.data[sample].state);
            }

            bsp.SetLump(LUMP_BURNABLECELLS, cells);
            bsp.SetLump(LUMP_BURNABLECELLDATA, blocks);
        }

        const ComWorld& m_com_world;
    };
} // namespace

namespace com_world
{
    void DumperT5::DumpAsset(AssetDumpingContext& context, const XAssetInfo<AssetComWorld::Type>& asset)
    {
        const auto& comWorld = *asset.Asset();
        auto& bsp = context.GetZoneAssetDumperState<bsp::BspDumperStateT5>()->GetBsp(comWorld.name);

        const ComWorldBspDumper dumper(comWorld);
        dumper.Dump(bsp);
    }
} // namespace com_world
