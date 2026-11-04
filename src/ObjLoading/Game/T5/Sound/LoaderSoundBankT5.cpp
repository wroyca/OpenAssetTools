#include "LoaderSoundBankT5.h"

#include "Csv/CsvHeaderRow.h"
#include "Csv/CsvStream.h"
#include "Game/T5/CommonT5.h"
#include "Game/T5/Sound/SndAliasFieldsT5.h"
#include "Game/T5/Sound/SndAssetFileT5.h"
#include "Game/T5/SoundConstantsT5.h"
#include "Utils/Logging/Log.h"
#include "Utils/StringUtils.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <limits>
#include <map>
#include <string>
#include <type_traits>
#include <vector>

using namespace T5;

namespace
{
    constexpr auto INVALID_INDEX = std::numeric_limits<uint16_t>::max();

    template<typename T> void WriteField(snd_alias_t& alias, const size_t offset, const T value)
    {
        std::memcpy(reinterpret_cast<char*>(&alias) + offset, &value, sizeof(T));
    }

    unsigned ParseHash(const std::string& value)
    {
        if (value.empty())
            return 0u;

        if (value[0] == '@')
            return static_cast<unsigned>(std::strtoul(&value[1], nullptr, 16));

        return Common::SND_HashName(value.c_str());
    }

    bool ParseFloatInRange(const CsvCell& cell, const float minimum, const float maximum, float& value)
    {
        return cell.AsFloat(value) && value >= minimum && value <= maximum;
    }

    bool ParseUnsignedInRange(const CsvCell& cell, const unsigned maximum, unsigned& value)
    {
        uint32_t parsedValue;
        if (!cell.AsUInt32(parsedValue) || parsedValue > maximum)
            return false;

        value = parsedValue;
        return true;
    }

    bool ParseEnum(const CsvCell& cell, const SndAliasField& field, const unsigned maximum, unsigned& value)
    {
        for (auto i = 0u; i < field.enumValueCount; i++)
        {
            if (cell.m_value == field.enumValues[i])
            {
                value = i;
                return true;
            }
        }

        return ParseUnsignedInRange(cell, maximum, value);
    }

    bool ParseFlags(const CsvCell& cell, const SndAliasField& field, unsigned& value)
    {
        value = 0u;
        for (const auto& entry : utils::StringSplit(cell.m_value, ' '))
        {
            if (entry.empty())
                continue;

            const auto* const* enumValuesEnd = field.enumValues + field.enumValueCount;
            const auto* foundValue = std::find(field.enumValues, enumValuesEnd, entry);
            if (foundValue == enumValuesEnd)
                return false;

            value |= 1u << static_cast<unsigned>(foundValue - field.enumValues);
        }

        return true;
    }

    bool ParseAliasField(const CsvCell& cell, const SndAliasField& field, snd_alias_t& alias)
    {
        switch (field.type)
        {
        case SndAliasFieldType::DBSPL:
        {
            float dbspl;
            if (!ParseFloatInRange(cell, 0.0f, 100.0f, dbspl))
                return false;

            WriteField(alias, field.offset, static_cast<uint16_t>(Common::DbsplToLinear(dbspl) * static_cast<float>(std::numeric_limits<uint16_t>::max())));
            return true;
        }

        case SndAliasFieldType::CENTS:
        {
            float cents;
            if (!ParseFloatInRange(cell, -2400.0f, 1200.0f, cents))
                return false;

            WriteField(alias, field.offset, static_cast<uint16_t>(Common::CentsToHertz(cents) * static_cast<float>(std::numeric_limits<int16_t>::max())));
            return true;
        }

        case SndAliasFieldType::HASH:
            WriteField(alias, field.offset, ParseHash(cell.m_value));
            return true;

        case SndAliasFieldType::ENUM_BITS:
        case SndAliasFieldType::FLAG_BITS:
        {
            const auto mask = (1u << field.bitCount) - 1u;
            unsigned bits;
            if (field.type == SndAliasFieldType::ENUM_BITS ? !ParseEnum(cell, field, mask, bits) : !ParseFlags(cell, field, bits))
                return false;

            alias.flags |= (bits & mask) << field.bitShift;
            return true;
        }

        case SndAliasFieldType::BYTE:
        {
            unsigned value;
            if (!ParseUnsignedInRange(cell, std::numeric_limits<uint8_t>::max(), value))
                return false;

            WriteField(alias, field.offset, static_cast<uint8_t>(value));
            return true;
        }

        case SndAliasFieldType::ENUM_BYTE:
        {
            unsigned value;
            if (!ParseEnum(cell, field, std::numeric_limits<uint8_t>::max(), value))
                return false;

            WriteField(alias, field.offset, static_cast<uint8_t>(value));
            return true;
        }

        case SndAliasFieldType::USHORT:
        {
            unsigned value;
            if (!ParseUnsignedInRange(cell, std::numeric_limits<uint16_t>::max(), value))
                return false;

            WriteField(alias, field.offset, static_cast<uint16_t>(value));
            return true;
        }

        case SndAliasFieldType::NORM_BYTE:
        {
            float value;
            if (!ParseFloatInRange(cell, 0.0f, 1.0f, value))
                return false;

            WriteField(alias, field.offset, static_cast<uint8_t>(value * static_cast<float>(std::numeric_limits<uint8_t>::max())));
            return true;
        }
        }

        return false;
    }

    const char* DupOrNull(MemoryManager& memory, const std::string& value)
    {
        return value.empty() ? nullptr : memory.Dup(value.c_str());
    }

    void CopyName(char (&destination)[32], const std::string& value)
    {
        std::memset(destination, 0, sizeof(destination));
        value.copy(destination, sizeof(destination) - 1u);
    }

    void BuildAliasIndex(MemoryManager& memory, SndBank& sndBank)
    {
        const auto count = sndBank.aliasCount;
        sndBank.aliasIndex = memory.Alloc<SndIndexEntry>(count);
        for (auto i = 0u; i < count; i++)
            sndBank.aliasIndex[i] = SndIndexEntry{INVALID_INDEX, INVALID_INDEX};

        std::vector<bool> placed(count);
        for (auto i = 0u; i < count; i++)
        {
            auto& entry = sndBank.aliasIndex[sndBank.alias[i].id % count];
            if (entry.value == INVALID_INDEX)
            {
                entry.value = static_cast<uint16_t>(i);
                placed[i] = true;
            }
        }

        for (auto i = 0u; i < count; i++)
        {
            if (placed[i])
                continue;

            auto chainEnd = sndBank.alias[i].id % count;
            while (sndBank.aliasIndex[chainEnd].next != INVALID_INDEX)
                chainEnd = sndBank.aliasIndex[chainEnd].next;

            auto freeIndex = INVALID_INDEX;
            for (auto offset = 1u; offset < count && freeIndex == INVALID_INDEX; offset++)
            {
                if (sndBank.aliasIndex[(chainEnd + offset) % count].value == INVALID_INDEX)
                    freeIndex = static_cast<uint16_t>((chainEnd + offset) % count);
                else if (sndBank.aliasIndex[(chainEnd + count - offset) % count].value == INVALID_INDEX)
                    freeIndex = static_cast<uint16_t>((chainEnd + count - offset) % count);
            }

            sndBank.aliasIndex[chainEnd].next = freeIndex;
            sndBank.aliasIndex[freeIndex].value = static_cast<uint16_t>(i);
        }
    }

    class SoundBankLoader final : public AssetCreator<AssetSoundBank>
    {
    public:
        SoundBankLoader(MemoryManager& memory, ISearchPath& searchPath)
            : m_memory(memory),
              m_search_path(searchPath)
        {
        }

        AssetCreationResult CreateAsset(const std::string& assetName, AssetCreationContext& context) override
        {
            const auto aliasFile = m_search_path.Open(std::format("soundbank/{}.aliases.csv", assetName));
            if (!aliasFile.IsOpen())
                return AssetCreationResult::NoAction();

            m_sound_files.clear();

            auto* sndBank = m_memory.Alloc<SndBank>();
            sndBank->name = m_memory.Dup(assetName.c_str());

            if (!LoadAliases(*sndBank, *aliasFile.m_stream))
            {
                con::error("Sound bank aliases file for {} is invalid", assetName);
                return AssetCreationResult::Failure();
            }

            const auto radverbFile = m_search_path.Open(std::format("soundbank/{}.radverbs.csv", assetName));
            if (radverbFile.IsOpen() && !LoadRadverbs(*sndBank, *radverbFile.m_stream))
            {
                con::error("Sound bank radverbs file for {} is invalid", assetName);
                return AssetCreationResult::Failure();
            }

            const auto snapshotFile = m_search_path.Open(std::format("soundbank/{}.snapshots.csv", assetName));
            if (snapshotFile.IsOpen() && !LoadSnapshots(*sndBank, *snapshotFile.m_stream))
            {
                con::error("Sound bank snapshots file for {} is invalid", assetName);
                return AssetCreationResult::Failure();
            }

            return AssetCreationResult::Success(context.AddAsset<AssetSoundBank>(assetName, sndBank));
        }

    private:
        SoundFile* GetSoundFile(const std::string& fileName, const unsigned type)
        {
            const auto key = std::format("{}:{}", type, fileName);
            const auto existingFile = m_sound_files.find(key);
            if (existingFile != m_sound_files.end())
                return existingFile->second;

            auto* soundFile = m_memory.Alloc<SoundFile>();
            soundFile->type = static_cast<char>(type);
            soundFile->exists = 1;

            if (type == SAT_LOADED)
            {
                std::string path(fileName);
                std::ranges::replace(path, '\\', '/');

                const auto file = m_search_path.Open(path);
                if (!file.IsOpen())
                {
                    con::error("Could not find sound file \"{}\"", fileName);
                    return nullptr;
                }

                auto* loadedSound = m_memory.Alloc<LoadedSound>();
                loadedSound->name = m_memory.Dup(fileName.c_str());
                if (!sound::ReadSndAssetFileT5(*file.m_stream, loadedSound->sound, m_memory))
                {
                    con::error("Invalid sound file \"{}\"", fileName);
                    return nullptr;
                }

                soundFile->u.loadSnd = loadedSound;
            }
            else
            {
                auto* streamedSound = m_memory.Alloc<StreamedSound>();
                streamedSound->filename = m_memory.Dup(fileName.c_str());
                soundFile->u.streamSnd = streamedSound;
            }

            m_sound_files.emplace(key, soundFile);
            return soundFile;
        }

        bool LoadAlias(const std::vector<CsvCell>& row, const std::vector<unsigned>& fieldColumns, const unsigned (&stringColumns)[4], snd_alias_t& alias)
        {
            const auto& name = row[stringColumns[0]].m_value;
            if (name.empty())
            {
                con::error("Sound alias without a name");
                return false;
            }

            alias.name = m_memory.Dup(name.c_str());
            alias.id = Common::SND_HashName(alias.name);
            alias.secondaryname = DupOrNull(m_memory, row[stringColumns[2]].m_value);
            alias.subtitle = DupOrNull(m_memory, row[stringColumns[3]].m_value);

            for (auto i = 0u; i < std::extent_v<decltype(SND_ALIAS_FIELDS)>; i++)
            {
                if (!ParseAliasField(row[fieldColumns[i]], SND_ALIAS_FIELDS[i], alias))
                {
                    con::error("Invalid value \"{}\" for column \"{}\" of sound alias \"{}\"", row[fieldColumns[i]].m_value, SND_ALIAS_FIELDS[i].name, name);
                    return false;
                }
            }

            const auto& fileName = row[stringColumns[1]].m_value;
            if (!fileName.empty())
            {
                alias.soundFile = GetSoundFile(fileName, (alias.flags >> 14) & 3u);
                if (!alias.soundFile)
                    return false;
            }

            return true;
        }

        bool LoadAliases(SndBank& sndBank, std::istream& stream)
        {
            const CsvInputStream csv(stream);
            CsvHeaderRow headerRow;
            if (!headerRow.Read(csv))
                return false;

            unsigned stringColumns[4];
            if (!headerRow.RequireIndexForHeader("name", stringColumns[0]) || !headerRow.RequireIndexForHeader("file", stringColumns[1])
                || !headerRow.RequireIndexForHeader("secondary", stringColumns[2]) || !headerRow.RequireIndexForHeader("subtitle", stringColumns[3]))
            {
                return false;
            }

            std::vector<unsigned> fieldColumns(std::extent_v<decltype(SND_ALIAS_FIELDS)>);
            for (auto i = 0u; i < fieldColumns.size(); i++)
            {
                if (!headerRow.RequireIndexForHeader(SND_ALIAS_FIELDS[i].name, fieldColumns[i]))
                    return false;
            }

            const auto columnCount = std::max(*std::ranges::max_element(fieldColumns), *std::ranges::max_element(stringColumns)) + 1u;

            std::vector<std::vector<snd_alias_t>> aliasLists;
            std::vector<CsvCell> row;
            while (csv.NextRow(row))
            {
                if (row.size() < columnCount)
                    row.resize(columnCount, CsvCell(""));

                snd_alias_t alias{};
                if (!LoadAlias(row, fieldColumns, stringColumns, alias))
                    return false;

                if (aliasLists.empty() || aliasLists.back().front().id != alias.id)
                    aliasLists.emplace_back();
                aliasLists.back().emplace_back(alias);
            }

            sndBank.aliasCount = static_cast<unsigned>(aliasLists.size());
            sndBank.alias = m_memory.Alloc<snd_alias_list_t>(sndBank.aliasCount);
            for (auto i = 0u; i < sndBank.aliasCount; i++)
            {
                auto& aliasList = sndBank.alias[i];
                const auto& aliases = aliasLists[i];

                aliasList.name = aliases.front().name;
                aliasList.id = aliases.front().id;
                aliasList.count = static_cast<int>(aliases.size());
                aliasList.head = m_memory.Alloc<snd_alias_t>(aliases.size());
                std::ranges::copy(aliases, aliasList.head);
            }

            if (sndBank.aliasCount > 0)
                BuildAliasIndex(m_memory, sndBank);

            return true;
        }

        bool LoadRadverbs(SndBank& sndBank, std::istream& stream)
        {
            const CsvInputStream csv(stream);
            CsvHeaderRow headerRow;
            if (!headerRow.Read(csv))
                return false;

            constexpr const char* FLOAT_HEADERS[]{
                "smoothing",
                "earlyTime",
                "lateTime",
                "earlyGain",
                "lateGain",
                "earlyLpf",
                "lateLpf",
                "inputLpf",
                "dampLpf",
                "wallReflect",
                "dryGain",
                "earlySize",
                "lateSize",
                "diffusion",
                "returnGain",
            };

            unsigned nameColumn;
            unsigned floatColumns[std::extent_v<decltype(FLOAT_HEADERS)>];
            if (!headerRow.RequireIndexForHeader("name", nameColumn))
                return false;
            for (auto i = 0u; i < std::extent_v<decltype(FLOAT_HEADERS)>; i++)
            {
                if (!headerRow.RequireIndexForHeader(FLOAT_HEADERS[i], floatColumns[i]))
                    return false;
            }

            std::vector<snd_radverb> radverbs;
            std::vector<CsvCell> row;
            while (csv.NextRow(row))
            {
                snd_radverb radverb{};
                CopyName(radverb.name, row[nameColumn].m_value);
                radverb.id = Common::SND_HashName(radverb.name);

                float* const values[]{
                    &radverb.smoothing,
                    &radverb.earlyTime,
                    &radverb.lateTime,
                    &radverb.earlyGain,
                    &radverb.lateGain,
                    &radverb.earlyLpf,
                    &radverb.lateLpf,
                    &radverb.inputLpf,
                    &radverb.dampLpf,
                    &radverb.wallReflect,
                    &radverb.dryGain,
                    &radverb.earlySize,
                    &radverb.lateSize,
                    &radverb.diffusion,
                    &radverb.returnGain,
                };
                for (auto i = 0u; i < std::extent_v<decltype(values)>; i++)
                {
                    if (!row[floatColumns[i]].AsFloat(*values[i]))
                    {
                        con::error("Invalid value \"{}\" for column \"{}\" of radverb \"{}\"", row[floatColumns[i]].m_value, FLOAT_HEADERS[i], radverb.name);
                        return false;
                    }
                }

                radverbs.emplace_back(radverb);
            }

            sndBank.radverbCount = static_cast<unsigned>(radverbs.size());
            sndBank.radverbs = m_memory.Alloc<snd_radverb>(radverbs.size());
            std::ranges::copy(radverbs, sndBank.radverbs);

            return true;
        }

        bool LoadSnapshots(SndBank& sndBank, std::istream& stream)
        {
            const CsvInputStream csv(stream);
            CsvHeaderRow headerRow;
            if (!headerRow.Read(csv))
                return false;

            unsigned nameColumn, occlusionColumn, fadeInColumn, fadeInCurveColumn, fadeOutColumn, fadeOutCurveColumn, distanceColumn;
            if (!headerRow.RequireIndexForHeader("name", nameColumn) || !headerRow.RequireIndexForHeader("occlusion", occlusionColumn)
                || !headerRow.RequireIndexForHeader("fadeIn", fadeInColumn) || !headerRow.RequireIndexForHeader("fadeInCurve", fadeInCurveColumn)
                || !headerRow.RequireIndexForHeader("fadeOut", fadeOutColumn) || !headerRow.RequireIndexForHeader("fadeOutCurve", fadeOutCurveColumn)
                || !headerRow.RequireIndexForHeader("distance", distanceColumn))
            {
                return false;
            }

            unsigned groupColumns[std::extent_v<decltype(SOUND_SNAPSHOT_GROUPS)>];
            for (auto i = 0u; i < std::extent_v<decltype(SOUND_SNAPSHOT_GROUPS)>; i++)
            {
                if (!headerRow.RequireIndexForHeader(SOUND_SNAPSHOT_GROUPS[i], groupColumns[i]))
                    return false;
            }

            std::vector<snd_snapshot> snapshots;
            std::vector<CsvCell> row;
            while (csv.NextRow(row))
            {
                snd_snapshot snapshot{};
                CopyName(snapshot.name, row[nameColumn].m_value);
                snapshot.id = Common::SND_HashName(snapshot.name);
                CopyName(snapshot.occlusionName, row[occlusionColumn].m_value);
                snapshot.occlusionId = Common::SND_HashName(snapshot.occlusionName);
                snapshot.fadeInCurve = ParseHash(row[fadeInCurveColumn].m_value);
                snapshot.fadeOutCurve = ParseHash(row[fadeOutCurveColumn].m_value);

                auto valid = row[fadeInColumn].AsFloat(snapshot.fadeIn) && row[fadeOutColumn].AsFloat(snapshot.fadeOut)
                             && row[distanceColumn].AsFloat(snapshot.distance);
                for (auto i = 0u; i < std::extent_v<decltype(SOUND_SNAPSHOT_GROUPS)>; i++)
                    valid = valid && row[groupColumns[i]].AsFloat(snapshot.attenuation[i]);

                if (!valid)
                {
                    con::error("Invalid values for snapshot \"{}\"", snapshot.name);
                    return false;
                }

                snapshots.emplace_back(snapshot);
            }

            sndBank.snapshotCount = static_cast<unsigned>(snapshots.size());
            sndBank.snapshots = m_memory.Alloc<snd_snapshot>(snapshots.size());
            std::ranges::copy(snapshots, sndBank.snapshots);

            return true;
        }

        MemoryManager& m_memory;
        ISearchPath& m_search_path;
        std::map<std::string, SoundFile*> m_sound_files;
    };
} // namespace

namespace sound
{
    std::unique_ptr<AssetCreator<AssetSoundBank>> CreateSoundBankLoaderT5(MemoryManager& memory, ISearchPath& searchPath)
    {
        return std::make_unique<SoundBankLoader>(memory, searchPath);
    }
} // namespace sound
