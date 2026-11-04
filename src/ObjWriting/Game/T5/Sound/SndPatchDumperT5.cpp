#include "SndPatchDumperT5.h"

#include "Csv/CsvStream.h"
#include "Game/T5/CommonT5.h"
#include "Game/T5/Sound/SndPatchTablesT5.h"
#include "Utils/Logging/Log.h"

#include <format>
#include <string>
#include <type_traits>
#include <unordered_map>

using namespace T5;

namespace
{
    constexpr auto TABLE_ALIAS = 0u;
    constexpr auto TABLE_SNAPSHOT = 5u;
    constexpr auto TABLE_RADVERB = 7u;

    using KnownNames = std::unordered_map<unsigned, std::string>;

    KnownNames CollectZoneNames(const Zone& zone, const unsigned table)
    {
        KnownNames names;
        for (const auto* bankInfo : zone.m_pools.PoolAssets<AssetSoundBank>())
        {
            const auto* bank = bankInfo->Asset();
            if (table == TABLE_ALIAS)
            {
                for (auto i = 0u; i < bank->aliasCount; i++)
                    names.emplace(bank->alias[i].id, bank->alias[i].name);
            }
            else if (table == TABLE_RADVERB)
            {
                for (auto i = 0u; i < bank->radverbCount; i++)
                    names.emplace(bank->radverbs[i].id, bank->radverbs[i].name);
            }
            else if (table == TABLE_SNAPSHOT)
            {
                for (auto i = 0u; i < bank->snapshotCount; i++)
                    names.emplace(bank->snapshots[i].id, bank->snapshots[i].name);
            }
        }

        return names;
    }

    std::string FormatRowName(const SndPatchTable& table, const KnownNames& zoneNames, const unsigned id)
    {
        for (auto i = 0u; i < table.rowNameCount; i++)
        {
            if (Common::SND_HashName(table.rowNames[i]) == id)
                return table.rowNames[i];
        }

        if (const auto zoneName = zoneNames.find(id); zoneName != zoneNames.end())
            return zoneName->second;

        return std::format("@{:x}", id);
    }

    std::string FormatFieldName(const SndPatchTable& table, const unsigned field)
    {
        if (field < table.fieldNameCount)
            return table.fieldNames[field];

        if (field - table.fieldNameCount < table.extraFieldNameCount)
            return table.extraFieldNames[field - table.fieldNameCount];

        return std::to_string(field);
    }
} // namespace

namespace sound
{
    void SndPatchDumperT5::DumpAsset(AssetDumpingContext& context, const XAssetInfo<AssetSoundPatch::Type>& asset)
    {
        const auto* patch = asset.Asset();

        if (patch->fileCount > 0)
            con::warn("Sound patch \"{}\" has {} sound files, which are not dumped", patch->name, patch->fileCount);

        const auto outFile = context.OpenAssetFile(std::format("soundpatch/{}.csv", patch->name));
        if (!outFile)
        {
            con::error("Failed to open sound patch output file: \"{}\"", patch->name);
            return;
        }

        CsvOutputStream csvStream(*outFile);
        for (const auto* header : {"table", "name", "field", "value"})
            csvStream.WriteColumn(header);
        csvStream.NextRow();

        std::unordered_map<unsigned, KnownNames> zoneNames;
        for (auto i = 0u; i + 1u < patch->elementCount;)
        {
            const auto tableIndex = patch->elements[i] >> 16;
            const auto fieldCount = patch->elements[i] & 0xFFFFu;
            const auto id = patch->elements[i + 1];
            i += 2u;

            if (tableIndex >= std::extent_v<decltype(SND_PATCH_TABLES)>)
            {
                con::error("Sound patch \"{}\" references unknown table {}", patch->name, tableIndex);
                return;
            }

            const auto& table = SND_PATCH_TABLES[tableIndex];
            if (!zoneNames.contains(tableIndex))
                zoneNames.emplace(tableIndex, CollectZoneNames(context.m_zone, tableIndex));
            const auto rowName = FormatRowName(table, zoneNames[tableIndex], id);

            for (auto f = 0u; f < fieldCount && i < patch->elementCount; f++, i++)
            {
                csvStream.WriteColumn(table.name);
                csvStream.WriteColumn(rowName);
                csvStream.WriteColumn(FormatFieldName(table, patch->elements[i] >> 16));
                csvStream.WriteColumn(std::to_string(patch->elements[i] & 0xFFFFu));
                csvStream.NextRow();
            }
        }
    }
} // namespace sound
