#include "SndAssetFileT5.h"

#include "Utils/Logging/Log.h"

#include <cstdint>
#include <vector>

using namespace T5;

namespace
{
    struct SndAssetFileHeader
    {
        uint32_t version;
        uint32_t frameCount;
        uint32_t frameRate;
        uint32_t channelCount;
        uint32_t headerSize;
        uint32_t blockSize;
        uint32_t bufferSize;
        uint32_t format;
        uint32_t channelFlags;
        uint32_t flags;
        uint32_t seekTableCount;
        uint32_t seekTable;
        uint32_t dataSize;
        uint32_t data;
    };

    static_assert(sizeof(SndAssetFileHeader) == 0x38);
} // namespace

namespace sound
{
    void WriteSndAssetFileT5(std::ostream& stream, const snd_asset& asset)
    {
        const SndAssetFileHeader header{
            .version = asset.version,
            .frameCount = asset.frame_count,
            .frameRate = asset.frame_rate,
            .channelCount = asset.channel_count,
            .headerSize = asset.header_size,
            .blockSize = asset.block_size,
            .bufferSize = asset.buffer_size,
            .format = static_cast<uint32_t>(asset.format),
            .channelFlags = static_cast<uint32_t>(asset.channel_flags),
            .flags = static_cast<uint32_t>(asset.flags),
            .seekTableCount = asset.seek_table_count,
            .seekTable = 0,
            .dataSize = asset.data_size,
            .data = 0,
        };

        stream.write(reinterpret_cast<const char*>(&header), sizeof(header));

        const auto seekTableSize = sizeof(uint32_t) * asset.seek_table_count;
        if (asset.seek_table_count > 0)
            stream.write(reinterpret_cast<const char*>(asset.seek_table), static_cast<std::streamsize>(seekTableSize));

        const auto writtenHeaderSize = sizeof(header) + seekTableSize;
        if (asset.header_size > writtenHeaderSize)
        {
            const std::vector<char> padding(asset.header_size - writtenHeaderSize);
            stream.write(padding.data(), static_cast<std::streamsize>(padding.size()));
        }

        if (asset.data_size > 0)
            stream.write(asset.data, asset.data_size);
    }

    bool ReadSndAssetFileT5(std::istream& stream, snd_asset& asset, MemoryManager& memory)
    {
        SndAssetFileHeader header{};
        stream.read(reinterpret_cast<char*>(&header), sizeof(header));
        if (stream.gcount() != sizeof(header))
        {
            con::error("Sound asset file is too small for its header");
            return false;
        }

        const auto seekTableSize = sizeof(uint32_t) * header.seekTableCount;
        if (header.headerSize < sizeof(header) + seekTableSize)
        {
            con::error("Sound asset header size {} cannot hold {} seek table entries", header.headerSize, header.seekTableCount);
            return false;
        }

        asset.version = header.version;
        asset.frame_count = header.frameCount;
        asset.frame_rate = header.frameRate;
        asset.channel_count = header.channelCount;
        asset.header_size = header.headerSize;
        asset.block_size = header.blockSize;
        asset.buffer_size = header.bufferSize;
        asset.format = static_cast<snd_asset_format>(header.format);
        asset.channel_flags = static_cast<snd_asset_channel>(header.channelFlags);
        asset.flags = static_cast<snd_asset_flags>(header.flags);
        asset.seek_table_count = header.seekTableCount;
        asset.data_size = header.dataSize;

        asset.seek_table = nullptr;
        if (header.seekTableCount > 0)
        {
            asset.seek_table = memory.Alloc<unsigned int>(header.seekTableCount);
            stream.read(reinterpret_cast<char*>(asset.seek_table), static_cast<std::streamsize>(seekTableSize));
            if (stream.gcount() != static_cast<std::streamsize>(seekTableSize))
            {
                con::error("Sound asset file is too small for its seek table");
                return false;
            }
        }

        stream.seekg(header.headerSize, std::ios::beg);

        asset.data = nullptr;
        if (header.dataSize > 0)
        {
            asset.data = memory.Alloc<snd_align_char>(header.dataSize);
            stream.read(asset.data, header.dataSize);
            if (stream.gcount() != static_cast<std::streamsize>(header.dataSize))
            {
                con::error("Sound asset file is too small for its {} data bytes", header.dataSize);
                return false;
            }
        }

        return true;
    }
} // namespace sound
