#include "LoaderSoundPatchT5.h"

#include "Csv/CsvHeaderRow.h"
#include "Csv/CsvStream.h"
#include "Game/T5/CommonT5.h"
#include "Game/T5/Sound/SndPatchTablesT5.h"
#include "Utils/Logging/Log.h"

#include <algorithm>
#include <cstdlib>
#include <format>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

using namespace T5;

namespace
{
    std::optional<unsigned> ParseTable(const std::string& value)
    {
        for (auto i = 0u; i < std::extent_v<decltype(SND_PATCH_TABLES)>; i++)
        {
            if (value == SND_PATCH_TABLES[i].name)
                return i;
        }

        return std::nullopt;
    }

    unsigned ParseRowName(const std::string& value)
    {
        if (!value.empty() && value[0] == '@')
            return static_cast<unsigned>(std::strtoul(&value[1], nullptr, 16));

        return Common::SND_HashName(value.c_str());
    }

    std::optional<unsigned> ParseField(const SndPatchTable& table, const CsvCell& cell)
    {
        for (auto i = 0u; i < table.fieldNameCount; i++)
        {
            if (cell.m_value == table.fieldNames[i])
                return i;
        }

        for (auto i = 0u; i < table.extraFieldNameCount; i++)
        {
            if (cell.m_value == table.extraFieldNames[i])
                return static_cast<unsigned>(table.fieldNameCount + i);
        }

        uint32_t field;
        if (cell.AsUInt32(field) && field <= 0xFFFFu)
            return field;

        return std::nullopt;
    }

    class SoundPatchLoader final : public AssetCreator<AssetSoundPatch>
    {
    public:
        SoundPatchLoader(MemoryManager& memory, ISearchPath& searchPath)
            : m_memory(memory),
              m_search_path(searchPath)
        {
        }

        AssetCreationResult CreateAsset(const std::string& assetName, AssetCreationContext& context) override
        {
            const auto file = m_search_path.Open(std::format("soundpatch/{}.csv", assetName));
            if (!file.IsOpen())
                return AssetCreationResult::NoAction();

            std::vector<unsigned> elements;
            if (!LoadElements(*file.m_stream, elements))
            {
                con::error("Sound patch file for {} is invalid", assetName);
                return AssetCreationResult::Failure();
            }

            auto* patch = m_memory.Alloc<SndPatch>();
            patch->name = m_memory.Dup(assetName.c_str());
            patch->elementCount = static_cast<unsigned>(elements.size());
            patch->elements = m_memory.Alloc<unsigned>(elements.size());
            std::ranges::copy(elements, patch->elements);

            return AssetCreationResult::Success(context.AddAsset<AssetSoundPatch>(assetName, patch));
        }

    private:
        static bool LoadElements(std::istream& stream, std::vector<unsigned>& elements)
        {
            const CsvInputStream csv(stream);
            CsvHeaderRow headerRow;
            unsigned tableColumn, nameColumn, fieldColumn, valueColumn;
            if (!headerRow.Read(csv) || !headerRow.RequireIndexForHeader("table", tableColumn) || !headerRow.RequireIndexForHeader("name", nameColumn)
                || !headerRow.RequireIndexForHeader("field", fieldColumn) || !headerRow.RequireIndexForHeader("value", valueColumn))
            {
                return false;
            }

            const auto columnCount = std::max({tableColumn, nameColumn, fieldColumn, valueColumn}) + 1u;

            std::optional<size_t> recordStart;
            unsigned recordTable = 0u, recordId = 0u;
            std::vector<CsvCell> row;
            while (csv.NextRow(row))
            {
                if (row.size() < columnCount)
                    row.resize(columnCount, CsvCell(""));

                const auto table = ParseTable(row[tableColumn].m_value);
                if (!table)
                {
                    con::error("Unknown sound patch table \"{}\"", row[tableColumn].m_value);
                    return false;
                }

                const auto& tableDef = SND_PATCH_TABLES[*table];
                const auto field = ParseField(tableDef, row[fieldColumn]);
                uint32_t value;
                if (!field || !row[valueColumn].AsUInt32(value) || value > 0xFFFFu)
                {
                    con::error("Invalid sound patch entry \"{}\" = \"{}\" for {} \"{}\"",
                               row[fieldColumn].m_value,
                               row[valueColumn].m_value,
                               tableDef.name,
                               row[nameColumn].m_value);
                    return false;
                }

                const auto id = ParseRowName(row[nameColumn].m_value);
                if (!recordStart || recordTable != *table || recordId != id)
                {
                    recordStart = elements.size();
                    recordTable = *table;
                    recordId = id;
                    elements.emplace_back(recordTable << 16);
                    elements.emplace_back(recordId);
                }

                elements.emplace_back((*field << 16) | value);
                elements[*recordStart]++;
            }

            return true;
        }

        MemoryManager& m_memory;
        ISearchPath& m_search_path;
    };
} // namespace

namespace sound
{
    std::unique_ptr<AssetCreator<AssetSoundPatch>> CreateSoundPatchLoaderT5(MemoryManager& memory, ISearchPath& searchPath)
    {
        return std::make_unique<SoundPatchLoader>(memory, searchPath);
    }
} // namespace sound
