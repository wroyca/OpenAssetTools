#include "Game/T5/Bsp/PathConnectionsT5.h"

#include <catch2/catch_test_macros.hpp>

using namespace T5;

namespace
{
    TEST_CASE("PathConnections writes links and run length vis (T5)", "[t5][bsp]")
    {
        PathConnections connections;
        connections.m_node_links.resize(9u);
        connections.m_node_links[0].emplace_back(PathConnectionLink{1u, 2.5f});
        connections.m_vis = {0u, 0u, 0u, 7u, 1u, 2u, 0u, 3u, 4u};

        REQUIRE(PathConnections::GetVisBytes(9u) == 9u);

        const std::vector<uint8_t> expected{
            0x08, 0x00, 0x00, 0x00, 0x09, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x20, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x07, 0xFA, 0x01, 0x02, 0x00, 0x03, 0x04,
        };
        REQUIRE(connections.Write() == expected);

        PathConnections readConnections;
        REQUIRE(readConnections.Read(expected));
        REQUIRE(readConnections.m_node_links.size() == 9u);
        REQUIRE(readConnections.m_node_links[0].size() == 1u);
        REQUIRE(readConnections.m_node_links[0][0].nodeNum == 1u);
        REQUIRE(readConnections.m_node_links[0][0].dist == 2.5f);
        REQUIRE(readConnections.m_node_links[8].empty());
        REQUIRE(readConnections.m_vis == connections.m_vis);
    }

    TEST_CASE("PathConnections ends zero vis with a literal byte (T5)", "[t5][bsp]")
    {
        PathConnections connections;
        connections.m_node_links.resize(4u);
        connections.m_vis = {0u, 0u};

        const auto data = connections.Write();
        REQUIRE(data.size() == 16u);
        REQUIRE(data[14] == 0x01);
        REQUIRE(data[15] == 0x00);

        PathConnections readConnections;
        REQUIRE(readConnections.Read(data));
        REQUIRE(readConnections.m_vis == connections.m_vis);
    }

    TEST_CASE("PathConnections rejects other versions and short data (T5)", "[t5][bsp]")
    {
        PathConnections connections;
        REQUIRE(!connections.Read({0x07, 0x00, 0x00, 0x00, 0x00, 0x00}));
        REQUIRE(!connections.Read({0x08, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00}));
    }
} // namespace
