#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace T5
{
    struct PathConnectionLink
    {
        uint16_t nodeNum;
        float dist;
    };

    class PathConnections
    {
    public:
        static constexpr int32_t VERSION = 8;

        [[nodiscard]] static size_t GetVisBytes(size_t nodeCount);

        [[nodiscard]] bool Read(const std::vector<uint8_t>& data);
        [[nodiscard]] std::vector<uint8_t> Write() const;

        std::vector<std::vector<PathConnectionLink>> m_node_links;
        std::vector<uint8_t> m_vis;
    };
} // namespace T5
