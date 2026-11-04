#include "PathConnectionsT5.h"

#include <cstring>

namespace
{
    constexpr auto MAX_VIS_RUN = 0x7Fu;

    class LumpReader
    {
    public:
        explicit LumpReader(const std::vector<uint8_t>& data)
            : m_data(data),
              m_pos(0u)
        {
        }

        template<typename T> bool Read(T& value)
        {
            if (m_data.size() - m_pos < sizeof(T))
                return false;

            std::memcpy(&value, m_data.data() + m_pos, sizeof(T));
            m_pos += sizeof(T);
            return true;
        }

    private:
        const std::vector<uint8_t>& m_data;
        size_t m_pos;
    };

    template<typename T> void Append(std::vector<uint8_t>& data, const T value)
    {
        const auto offset = data.size();
        data.resize(offset + sizeof(T));
        std::memcpy(data.data() + offset, &value, sizeof(T));
    }
} // namespace

namespace T5
{
    size_t PathConnections::GetVisBytes(const size_t nodeCount)
    {
        if (nodeCount == 0u)
            return 0u;

        return ((nodeCount - 1u) * nodeCount + 7u) >> 3u;
    }

    bool PathConnections::Read(const std::vector<uint8_t>& data)
    {
        LumpReader reader(data);

        int32_t version;
        uint16_t nodeCount;
        if (!reader.Read(version) || version != VERSION || !reader.Read(nodeCount))
            return false;

        m_node_links.resize(nodeCount);
        for (auto& links : m_node_links)
        {
            uint16_t linkCount;
            if (!reader.Read(linkCount))
                return false;

            links.resize(linkCount);
            for (auto& link : links)
            {
                if (!reader.Read(link.nodeNum) || !reader.Read(link.dist))
                    return false;
            }
        }

        const auto visBytes = GetVisBytes(nodeCount);
        m_vis.clear();
        m_vis.reserve(visBytes);
        while (m_vis.size() < visBytes)
        {
            uint8_t control;
            if (!reader.Read(control))
                return false;

            if (control & 0x80u)
            {
                for (auto i = 0u; i < static_cast<uint8_t>(~control); i++)
                {
                    uint8_t value;
                    if (!reader.Read(value))
                        return false;
                    m_vis.emplace_back(value);
                }
            }
            else
            {
                m_vis.insert(m_vis.end(), control, 0u);

                uint8_t value;
                if (!reader.Read(value))
                    return false;
                m_vis.emplace_back(value);
            }
        }

        return m_vis.size() == visBytes;
    }

    std::vector<uint8_t> PathConnections::Write() const
    {
        std::vector<uint8_t> data;
        Append(data, VERSION);
        Append(data, static_cast<uint16_t>(m_node_links.size()));

        for (const auto& links : m_node_links)
        {
            Append(data, static_cast<uint16_t>(links.size()));
            for (const auto& link : links)
            {
                Append(data, link.nodeNum);
                Append(data, link.dist);
            }
        }

        const auto visBytes = m_vis.size();
        size_t i = 0u;
        while (i < visBytes)
        {
            if (i + 1u < visBytes && m_vis[i] == 0u)
            {
                auto j = i + 1u;
                while (j < visBytes - 1u && m_vis[j] == 0u && j - i < MAX_VIS_RUN)
                    j++;

                data.emplace_back(static_cast<uint8_t>(j - i));
                data.emplace_back(m_vis[j]);
                i = j + 1u;
                continue;
            }

            auto j = i + 1u;
            while (j < visBytes && j - i < MAX_VIS_RUN)
            {
                if (m_vis[j] == 0u && (j + 1u == visBytes || j - i >= MAX_VIS_RUN - 1u || m_vis[j + 1u] == 0u))
                    break;
                j++;
            }

            data.emplace_back(static_cast<uint8_t>(~(j - i)));
            data.insert(data.end(), m_vis.begin() + static_cast<std::ptrdiff_t>(i), m_vis.begin() + static_cast<std::ptrdiff_t>(j));
            i = j;
        }

        return data;
    }
} // namespace T5
