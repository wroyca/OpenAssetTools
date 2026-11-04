#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <istream>
#include <map>
#include <optional>
#include <ostream>
#include <vector>

namespace T5
{
    enum LumpType : uint32_t
    {
        LUMP_MATERIALS = 0,
        LUMP_LIGHTBYTES = 1,
        LUMP_LIGHTGRIDENTRIES = 2,
        LUMP_LIGHTGRIDCOLORS = 3,
        LUMP_PLANES = 4,
        LUMP_BRUSHSIDES = 5,
        LUMP_BRUSHSIDEEDGECOUNTS = 6,
        LUMP_BRUSHEDGES = 7,
        LUMP_BRUSHES = 8,
        LUMP_TRIANGLES = 9,
        LUMP_DRAWVERTS = 10,
        LUMP_DRAWINDICES = 11,
        LUMP_CULLGROUPS = 12,
        LUMP_CULLGROUPINDICES = 13,
        LUMP_OBSOLETE_1 = 14,
        LUMP_OBSOLETE_2 = 15,
        LUMP_OBSOLETE_3 = 16,
        LUMP_OBSOLETE_4 = 17,
        LUMP_OBSOLETE_5 = 18,
        LUMP_PORTALVERTS = 19,
        LUMP_OBSOLETE_6 = 20,
        LUMP_UINDS = 21,
        LUMP_BRUSHVERTSCOUNTS = 22,
        LUMP_BRUSHVERTS = 23,
        LUMP_AABBTREES = 24,
        LUMP_CELLS = 25,
        LUMP_PORTALS = 26,
        LUMP_NODES = 27,
        LUMP_LEAFS = 28,
        LUMP_LEAFBRUSHES = 29,
        LUMP_LEAFSURFACES = 30,
        LUMP_COLLISIONVERTS = 31,
        LUMP_COLLISIONTRIS = 32,
        LUMP_COLLISIONEDGEWALKABLE = 33,
        LUMP_COLLISIONBORDERS = 34,
        LUMP_COLLISIONPARTITIONS = 35,
        LUMP_COLLISIONAABBS = 36,
        LUMP_MODELS = 37,
        LUMP_VISIBILITY = 38,
        LUMP_ENTITIES = 39,
        LUMP_PATHCONNECTIONS = 40,
        LUMP_REFLECTION_PROBES = 41,
        LUMP_VERTEX_LAYER_DATA = 42,
        LUMP_PRIMARY_LIGHTS = 43,
        LUMP_LIGHTGRIDHEADER = 44,
        LUMP_LIGHTGRIDROWS = 45,
        LUMP_OBSOLETE_10 = 46,
        LUMP_OBSOLETE_11 = 47,
        LUMP_OBSOLETE_12 = 48,
        LUMP_OBSOLETE_13 = 49,
        LUMP_OBSOLETE_14 = 50,
        LUMP_OBSOLETE_15 = 51,
        LUMP_WATERHEADER = 52,
        LUMP_WATERCELLS = 53,
        LUMP_WATERCELLDATA = 54,
        LUMP_BURNABLEHEADER = 55,
        LUMP_BURNABLECELLS = 56,
        LUMP_BURNABLECELLDATA = 57,
        LUMP_SIMPLELIGHTMAPBYTES = 58,
        LUMP_LODCHAINS = 59,
        LUMP_LODINFOS = 60,
        LUMP_LODSURFACES = 61,
        LUMP_LIGHTREGIONS = 62,
        LUMP_LIGHTREGION_HULLS = 63,
        LUMP_LIGHTREGION_AXES = 64,
        LUMP_WIILIGHTGRID = 65,
        LUMP_LIGHTGRID2D_LIGHTS = 66,
        LUMP_LIGHTGRID2D_INDICES = 67,
        LUMP_LIGHTGRID2D_POINTS = 68,
        LUMP_LIGHTGRID2D_CELLS = 69,
        LUMP_LIGHT_CORONAS = 70,
        LUMP_SHADOWMAP_VOLUMES = 71,
        LUMP_SHADOWMAP_VOLUME_PLANES = 72,
        LUMP_EXPOSURE_VOLUMES = 73,
        LUMP_EXPOSURE_VOLUME_PLANES = 74,
        LUMP_OCCLUDERS = 75,
        LUMP_OUTDOORBOUNDS = 76,
        LUMP_HERO_ONLY_LIGHTS = 77,

        LUMP_COUNT
    };

    struct dplane_t
    {
        float normal[3];
        float dist;
    };

    struct dbrushside_t
    {
        union
        {
            int planeNum;
            float bound;
        } u;

        int materialNum;
    };

    struct dbrush_t
    {
        int16_t numSides;
        int16_t materialNum;
    };

    struct dnode_t
    {
        int planeNum;
        int children[2];
        int mins[3];
        int maxs[3];
    };

    struct dleaf_t
    {
        int cluster;
        int firstCollAabbIndex;
        int collAabbCount;
        int firstLeafBrush;
        int numLeafBrushes;
        int reserved;
    };

    struct dmodel_t
    {
        float mins[3];
        float maxs[3];
        unsigned int reserved;
        unsigned int firstCollAabbIndex;
        unsigned int collAabbCount;
        unsigned int firstBrush;
        unsigned int numBrushes;
    };

    struct DiskCollPartition
    {
        uint16_t checkStamp;
        uint8_t triCount;
        uint8_t borderCount;
        int firstTriIndex;
        int firstBorderIndex;
        int nuinds;
        int fuind;
    };

    struct DiskCollAabbTree
    {
        float origin[3];
        float halfSize[3];
        uint16_t materialIndex;
        uint16_t childCount;
        int u;
    };

    struct DiskCollBorder
    {
        float distEq[3];
        float zBase;
        float zSlope;
        float start;
        float length;
    };

    struct DiskPrimaryLight
    {
        uint8_t type;
        uint8_t canUseShadowMap;
        uint16_t cullDist;
        uint8_t priority;
        uint8_t pad[3];
        float dir[3];
        float origin[3];
        float color[3];
        float specularColor[3];
        float attenuation[3];
        float angle[3];
        float unused[4];
        float falloffStart;
        float radius;
        float falloffLerp[2];
        float cosHalfFovOuter;
        float cosHalfFovInner;
        int exponent;
        float rotationLimit;
        float translationLimit;
        float angleW;
        float mipDistance;
        float aAbB[4];
        float cookieControl0[4];
        float cookieControl1[4];
        float cookieControl2[4];
        char defName[64];
    };

    struct DiskWaterHeader
    {
        int minx;
        int miny;
        int maxx;
        int maxy;
        int reserved;
    };

    struct DiskWaterCell
    {
        int16_t waterheight;
        uint8_t flooroffset;
        uint8_t shoredist;
        uint8_t color[4];
    };

    struct DiskBurnableHeader
    {
        int minx;
        int miny;
        int maxx;
        int maxy;
    };

    struct DiskBurnableCell
    {
        int x;
        int y;
        int reserved;
        int dataIndex;
    };

    struct DiskBurnableBlock
    {
        uint8_t states[32];
    };

    static_assert(sizeof(dplane_t) == 16);
    static_assert(sizeof(dbrushside_t) == 8);
    static_assert(sizeof(dbrush_t) == 4);
    static_assert(sizeof(dnode_t) == 36);
    static_assert(sizeof(dleaf_t) == 24);
    static_assert(sizeof(dmodel_t) == 44);
    static_assert(sizeof(DiskCollPartition) == 20);
    static_assert(sizeof(DiskCollAabbTree) == 32);
    static_assert(sizeof(DiskCollBorder) == 28);
    static_assert(sizeof(DiskPrimaryLight) == 268);
    static_assert(sizeof(DiskWaterHeader) == 20);
    static_assert(sizeof(DiskWaterCell) == 8);
    static_assert(sizeof(DiskBurnableHeader) == 16);
    static_assert(sizeof(DiskBurnableCell) == 16);
    static_assert(sizeof(DiskBurnableBlock) == 32);

    class BspFile
    {
    public:
        static constexpr uint32_t IDENT = 0x50534249u;
        static constexpr uint32_t VERSION = 45u;

        void SetLump(LumpType type, std::vector<uint8_t> data);

        template<typename T> void SetLump(const LumpType type, const T* elements, const size_t count)
        {
            std::vector<uint8_t> data(sizeof(T) * count);
            if (!data.empty())
                std::memcpy(data.data(), elements, data.size());
            SetLump(type, std::move(data));
        }

        template<typename T> void SetLump(const LumpType type, const std::vector<T>& elements)
        {
            SetLump(type, elements.data(), elements.size());
        }

        [[nodiscard]] bool HasLump(LumpType type) const;
        [[nodiscard]] const std::vector<uint8_t>* GetLumpData(LumpType type) const;

        template<typename T> [[nodiscard]] std::optional<std::vector<T>> GetLump(const LumpType type) const
        {
            const auto* data = GetLumpData(type);
            if (!data)
                return std::vector<T>();
            if (data->size() % sizeof(T) != 0)
                return std::nullopt;

            std::vector<T> elements(data->size() / sizeof(T));
            if (!elements.empty())
                std::memcpy(elements.data(), data->data(), data->size());
            return elements;
        }

        bool Read(std::istream& stream);
        void Write(std::ostream& stream) const;

    private:
        std::map<uint32_t, std::vector<uint8_t>> m_lumps;
    };
} // namespace T5
