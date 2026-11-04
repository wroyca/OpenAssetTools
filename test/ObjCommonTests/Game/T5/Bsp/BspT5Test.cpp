#include "Game/T5/Bsp/BspT5.h"

#include "Game/T5/Bsp/BspEntitiesT5.h"
#include "Game/T5/Bsp/EntityMathT5.h"

#include <catch2/catch_test_macros.hpp>
#include <sstream>

using namespace T5;

namespace
{
    TEST_CASE("BspFile writes and reads lumps (T5)", "[t5][bsp]")
    {
        const std::vector<dplane_t> planes{
            {{1.0f, 0.0f, 0.0f}, 16.0f},
            {{0.0f, 0.0f, 1.0f}, -8.0f},
        };
        const std::vector<uint8_t> vertCounts{3u, 5u, 7u};

        BspFile bsp;
        bsp.SetLump(LUMP_PLANES, planes);
        bsp.SetLump(LUMP_BRUSHVERTSCOUNTS, vertCounts);

        std::stringstream stream;
        bsp.Write(stream);

        BspFile readBsp;
        REQUIRE(readBsp.Read(stream));
        REQUIRE(readBsp.HasLump(LUMP_PLANES));
        REQUIRE(!readBsp.HasLump(LUMP_NODES));

        const auto readPlanes = readBsp.GetLump<dplane_t>(LUMP_PLANES);
        REQUIRE(readPlanes);
        REQUIRE(readPlanes->size() == 2u);
        REQUIRE((*readPlanes)[1].normal[2] == 1.0f);
        REQUIRE((*readPlanes)[1].dist == -8.0f);

        const auto readVertCounts = readBsp.GetLump<uint8_t>(LUMP_BRUSHVERTSCOUNTS);
        REQUIRE(readVertCounts);
        REQUIRE(*readVertCounts == vertCounts);

        const auto missing = readBsp.GetLump<dnode_t>(LUMP_NODES);
        REQUIRE(missing);
        REQUIRE(missing->empty());

        REQUIRE(!readBsp.GetLump<dplane_t>(LUMP_BRUSHVERTSCOUNTS));
    }

    TEST_CASE("BspFile rejects a wrong version (T5)", "[t5][bsp]")
    {
        const uint32_t header[3]{BspFile::IDENT, BspFile::VERSION + 1u, 0u};
        std::stringstream stream(std::string(reinterpret_cast<const char*>(header), sizeof(header)));

        BspFile bsp;
        REQUIRE(!bsp.Read(stream));
    }

    TEST_CASE("BSP entities are parsed and written (T5)", "[t5][bsp]")
    {
        const auto entities =
            ParseBspEntities("{\n\"classname\" \"worldspawn\"\n}\n{\n\"classname\" \"rope\"\n\"origin\" \"1 2.5 -3\"\n\"Origin\" \"4 5 6\"\n}\n");
        REQUIRE(entities);
        REQUIRE(entities->size() == 2u);
        REQUIRE((*entities)[0].GetString("classname") == "worldspawn");

        float origin[3];
        REQUIRE((*entities)[1].GetVec3("origin", origin));
        REQUIRE(origin[0] == 4.0f);
        REQUIRE(origin[2] == 6.0f);

        const auto reparsed = ParseBspEntities(WriteBspEntities(*entities));
        REQUIRE(reparsed);
        REQUIRE(reparsed->size() == 2u);
        REQUIRE((*reparsed)[1].GetString("classname") == "rope");

        REQUIRE(!ParseBspEntities("{\n\"classname\"\n"));
    }

    TEST_CASE("BSP floats are written so they read back exactly (T5)", "[t5][bsp]")
    {
        const float values[3]{0.1f, -123.456787f, 1e-7f};

        float parsed[3];
        REQUIRE(ParseBspVec3(FormatBspVec3(values), parsed));
        REQUIRE(parsed[0] == values[0]);
        REQUIRE(parsed[1] == values[1]);
        REQUIRE(parsed[2] == values[2]);
    }

    TEST_CASE("Entity transforms invert each other (T5)", "[t5][bsp]")
    {
        const float origin[3]{100.0f, -50.0f, 8.0f};
        const float angles[3]{0.0f, 90.0f, 0.0f};
        float xform[4][3];
        AnglesToTransform(origin, angles, xform);

        const float local[3]{16.0f, 0.0f, 0.0f};
        float world[3];
        MatrixTransformVector43(local, xform, world);
        REQUIRE(world[0] > 99.99f);
        REQUIRE(world[0] < 100.01f);
        REQUIRE(world[1] == -34.0f);
        REQUIRE(world[2] == 8.0f);

        float back[3];
        MatrixTransposeTransformVector43(world, xform, back);
        REQUIRE(back[0] == 16.0f);
        REQUIRE(back[2] == 0.0f);
    }

    TEST_CASE("Identity angles give the identity quaternion (T5)", "[t5][bsp]")
    {
        const float angles[3]{0.0f, 0.0f, 0.0f};
        float quat[4];
        AnglesToQuat(angles, quat);

        REQUIRE(quat[0] == 0.0f);
        REQUIRE(quat[1] == 0.0f);
        REQUIRE(quat[2] == 0.0f);
        REQUIRE(quat[3] == 1.0f);
    }

    TEST_CASE("Rope attach distance falls back for coincident points (T5)", "[t5][bsp]")
    {
        const float p0[3]{0.0f, 0.0f, 0.0f};
        const float p1[3]{3.0f, 4.0f, 0.0f};

        REQUIRE(GetAttachPointDistance(p0, p1) == 5.0f);
        REQUIRE(GetAttachPointDistance(p0, p0) == 100.0f);
        REQUIRE(GetConstraintTimeout(30.0f) == 30000);
    }
} // namespace
