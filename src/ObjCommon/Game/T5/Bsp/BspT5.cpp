#include "BspT5.h"

#include <iterator>
#include <ranges>

using namespace T5;

namespace
{
    struct BspChunk
    {
        uint32_t type;
        uint32_t length;
    };

    constexpr size_t LUMP_ALIGNMENT = 4u;

    size_t AlignLumpLength(const size_t length)
    {
        return (length + LUMP_ALIGNMENT - 1u) & ~(LUMP_ALIGNMENT - 1u);
    }
} // namespace

void BspFile::SetLump(const LumpType type, std::vector<uint8_t> data)
{
    m_lumps[type] = std::move(data);
}

bool BspFile::HasLump(const LumpType type) const
{
    return m_lumps.contains(type);
}

const std::vector<uint8_t>* BspFile::GetLumpData(const LumpType type) const
{
    const auto lump = m_lumps.find(type);
    if (lump == m_lumps.end())
        return nullptr;

    return &lump->second;
}

bool BspFile::Read(std::istream& stream)
{
    const std::vector<char> file{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};

    uint32_t header[3];
    if (file.size() < sizeof(header))
        return false;
    std::memcpy(header, file.data(), sizeof(header));

    if (header[0] != IDENT || header[1] != VERSION)
        return false;

    const auto chunkCount = static_cast<size_t>(header[2]);
    if (file.size() < sizeof(header) + chunkCount * sizeof(BspChunk))
        return false;

    std::vector<BspChunk> chunks(chunkCount);
    if (chunkCount > 0)
        std::memcpy(chunks.data(), file.data() + sizeof(header), chunkCount * sizeof(BspChunk));

    m_lumps.clear();
    auto offset = sizeof(header) + chunkCount * sizeof(BspChunk);
    for (const auto& chunk : chunks)
    {
        if (offset + chunk.length > file.size())
            return false;

        const auto* begin = reinterpret_cast<const uint8_t*>(file.data() + offset);
        m_lumps.try_emplace(chunk.type, begin, begin + chunk.length);
        offset += AlignLumpLength(chunk.length);
    }

    return true;
}

void BspFile::Write(std::ostream& stream) const
{
    const uint32_t header[3]{IDENT, VERSION, static_cast<uint32_t>(m_lumps.size())};
    stream.write(reinterpret_cast<const char*>(header), sizeof(header));

    for (const auto& [type, data] : m_lumps)
    {
        const BspChunk chunk{type, static_cast<uint32_t>(data.size())};
        stream.write(reinterpret_cast<const char*>(&chunk), sizeof(chunk));
    }

    constexpr char padding[LUMP_ALIGNMENT]{};
    for (const auto& data : m_lumps | std::views::values)
    {
        stream.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        stream.write(padding, static_cast<std::streamsize>(AlignLumpLength(data.size()) - data.size()));
    }
}
