#include "SndBankDumperT5.h"

#include "Csv/CsvStream.h"
#include "Game/T5/CommonT5.h"
#include "Game/T5/Sound/SndAliasFieldsT5.h"
#include "Game/T5/Sound/SndAssetFileT5.h"
#include "Game/T5/SoundConstantsT5.h"
#include "Utils/Logging/Log.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <format>
#include <limits>
#include <string>
#include <unordered_set>

using namespace T5;

namespace
{
    constexpr auto FORMATTING_RETRIES = 6;

    template<typename T> T ReadField(const snd_alias_t& alias, const size_t offset)
    {
        T value;
        std::memcpy(&value, reinterpret_cast<const char*>(&alias) + offset, sizeof(T));
        return value;
    }

    std::string FormatFloat(const float value)
    {
        return std::format("{}", value);
    }

    std::string FormatDbspl(const uint16_t value)
    {
        const auto linear = static_cast<float>(value) / static_cast<float>(std::numeric_limits<uint16_t>::max());
        const auto dbSpl = std::clamp(Common::LinearToDbspl(linear), 0.0f, 100.0f);

        std::string dbSplFormat;
        for (auto i = 0; i < FORMATTING_RETRIES; i++)
        {
            dbSplFormat = std::format("{:.{}f}", dbSpl, i);
            const auto dbSplRound = std::stof(dbSplFormat);
            const auto dbSplRoundToValue = static_cast<uint16_t>(Common::DbsplToLinear(dbSplRound) * static_cast<float>(std::numeric_limits<uint16_t>::max()));

            if (dbSplRoundToValue == value)
                break;
        }

        return dbSplFormat;
    }

    std::string FormatCents(const uint16_t value)
    {
        const auto hertz = static_cast<float>(value) / static_cast<float>(std::numeric_limits<int16_t>::max());
        const auto cents = std::clamp(Common::HertzToCents(hertz), -2400.0f, 1200.0f);

        std::string centsFormat;
        for (auto i = 0; i < FORMATTING_RETRIES; i++)
        {
            centsFormat = std::format("{:.{}f}", cents, i);
            const auto centsRound = std::stof(centsFormat);
            const auto centsRoundToValue = static_cast<uint16_t>(Common::CentsToHertz(centsRound) * static_cast<float>(std::numeric_limits<int16_t>::max()));

            if (centsRoundToValue == value)
                break;
        }

        return centsFormat;
    }

    std::string FormatNormByte(const uint8_t value)
    {
        const auto normValue = static_cast<float>(value) / static_cast<float>(std::numeric_limits<uint8_t>::max());

        std::string normValueFormat;
        for (auto i = 0; i < FORMATTING_RETRIES; i++)
        {
            normValueFormat = std::format("{:.{}f}", normValue, i);
            const auto normValueRound = std::stof(normValueFormat);
            const auto normValueRoundToValue = static_cast<uint8_t>(normValueRound * static_cast<float>(std::numeric_limits<uint8_t>::max()));

            if (normValueRoundToValue == value)
                break;
        }

        return normValueFormat;
    }

    std::string FormatHash(const unsigned value, const char* const* knownNames, const size_t knownNameCount)
    {
        if (value == 0u)
            return "";

        for (auto i = 0u; i < knownNameCount; i++)
        {
            if (Common::SND_HashName(knownNames[i]) == value)
                return knownNames[i];
        }

        return std::format("@{:x}", value);
    }

    std::string FormatEnum(const unsigned value, const char* const* enumValues, const size_t enumValueCount)
    {
        if (value < enumValueCount)
            return enumValues[value];

        return std::to_string(value);
    }

    std::string FormatFlags(const unsigned value, const char* const* enumValues, const size_t enumValueCount)
    {
        std::string result;
        for (auto i = 0u; i < enumValueCount; i++)
        {
            if ((value & (1u << i)) == 0)
                continue;

            if (!result.empty())
                result += ' ';
            result += enumValues[i];
        }

        return result;
    }

    std::string FormatAliasField(const snd_alias_t& alias, const SndAliasField& field)
    {
        switch (field.type)
        {
        case SndAliasFieldType::DBSPL:
            return FormatDbspl(ReadField<uint16_t>(alias, field.offset));

        case SndAliasFieldType::CENTS:
            return FormatCents(ReadField<uint16_t>(alias, field.offset));

        case SndAliasFieldType::HASH:
            return FormatHash(ReadField<unsigned>(alias, field.offset), field.enumValues, field.enumValueCount);

        case SndAliasFieldType::ENUM_BITS:
        {
            const auto bits = (ReadField<unsigned>(alias, field.offset) >> field.bitShift) & ((1u << field.bitCount) - 1u);
            return FormatEnum(bits, field.enumValues, field.enumValueCount);
        }

        case SndAliasFieldType::FLAG_BITS:
        {
            const auto bits = (ReadField<unsigned>(alias, field.offset) >> field.bitShift) & ((1u << field.bitCount) - 1u);
            return FormatFlags(bits, field.enumValues, field.enumValueCount);
        }

        case SndAliasFieldType::BYTE:
            return std::to_string(ReadField<uint8_t>(alias, field.offset));

        case SndAliasFieldType::ENUM_BYTE:
            return FormatEnum(ReadField<uint8_t>(alias, field.offset), field.enumValues, field.enumValueCount);

        case SndAliasFieldType::USHORT:
            return std::to_string(ReadField<uint16_t>(alias, field.offset));

        case SndAliasFieldType::NORM_BYTE:
            return FormatNormByte(ReadField<uint8_t>(alias, field.offset));
        }

        assert(false);
        return "";
    }

    const char* GetSoundFileName(const SoundFile* soundFile)
    {
        if (!soundFile)
            return nullptr;

        if (soundFile->type == SAT_LOADED)
            return soundFile->u.loadSnd ? soundFile->u.loadSnd->name : nullptr;

        return soundFile->u.streamSnd ? soundFile->u.streamSnd->filename : nullptr;
    }

    std::string GetSoundFilePath(const char* fileName)
    {
        std::string path(fileName);
        std::ranges::replace(path, '\\', '/');

        return path;
    }

    void DumpLoadedSound(const AssetDumpingContext& context, const LoadedSound& loadedSound)
    {
        const auto outFile = context.OpenAssetFile(GetSoundFilePath(loadedSound.name));
        if (!outFile)
        {
            con::error("Failed to open sound output file: \"{}\"", loadedSound.name);
            return;
        }

        sound::WriteSndAssetFileT5(*outFile, loadedSound.sound);
    }

    void DumpAliases(const AssetDumpingContext& context, const SndBank& sndBank)
    {
        const auto outFile = context.OpenAssetFile(std::format("soundbank/{}.aliases.csv", sndBank.name));
        if (!outFile)
        {
            con::error("Failed to open sound alias output file: \"{}\"", sndBank.name);
            return;
        }

        CsvOutputStream csvStream(*outFile);
        csvStream.WriteColumn("name");
        csvStream.WriteColumn("file");
        csvStream.WriteColumn("secondary");
        csvStream.WriteColumn("subtitle");
        for (const auto& field : SND_ALIAS_FIELDS)
            csvStream.WriteColumn(field.name);
        csvStream.NextRow();

        std::unordered_set<const LoadedSound*> dumpedSounds;
        for (auto i = 0u; i < sndBank.aliasCount; i++)
        {
            const auto& aliasList = sndBank.alias[i];
            for (auto j = 0; j < aliasList.count; j++)
            {
                const auto& alias = aliasList.head[j];
                const auto* fileName = GetSoundFileName(alias.soundFile);

                csvStream.WriteColumn(alias.name ? alias.name : "");
                csvStream.WriteColumn(fileName ? fileName : "");
                csvStream.WriteColumn(alias.secondaryname ? alias.secondaryname : "");
                csvStream.WriteColumn(alias.subtitle ? alias.subtitle : "");
                for (const auto& field : SND_ALIAS_FIELDS)
                    csvStream.WriteColumn(FormatAliasField(alias, field));
                csvStream.NextRow();

                if (alias.soundFile && alias.soundFile->type == SAT_LOADED && alias.soundFile->u.loadSnd && alias.soundFile->u.loadSnd->name
                    && dumpedSounds.emplace(alias.soundFile->u.loadSnd).second)
                {
                    DumpLoadedSound(context, *alias.soundFile->u.loadSnd);
                }
            }
        }
    }

    void DumpRadverbs(const AssetDumpingContext& context, const SndBank& sndBank)
    {
        if (sndBank.radverbCount == 0)
            return;

        const auto outFile = context.OpenAssetFile(std::format("soundbank/{}.radverbs.csv", sndBank.name));
        if (!outFile)
        {
            con::error("Failed to open sound radverb output file: \"{}\"", sndBank.name);
            return;
        }

        CsvOutputStream csvStream(*outFile);
        for (const auto* header : {"name",
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
                                   "returnGain"})
        {
            csvStream.WriteColumn(header);
        }
        csvStream.NextRow();

        for (auto i = 0u; i < sndBank.radverbCount; i++)
        {
            const auto& radverb = sndBank.radverbs[i];
            csvStream.WriteColumn(radverb.name);
            for (const auto value : {radverb.smoothing,
                                     radverb.earlyTime,
                                     radverb.lateTime,
                                     radverb.earlyGain,
                                     radverb.lateGain,
                                     radverb.earlyLpf,
                                     radverb.lateLpf,
                                     radverb.inputLpf,
                                     radverb.dampLpf,
                                     radverb.wallReflect,
                                     radverb.dryGain,
                                     radverb.earlySize,
                                     radverb.lateSize,
                                     radverb.diffusion,
                                     radverb.returnGain})
            {
                csvStream.WriteColumn(FormatFloat(value));
            }
            csvStream.NextRow();
        }
    }

    void DumpSnapshots(const AssetDumpingContext& context, const SndBank& sndBank)
    {
        if (sndBank.snapshotCount == 0)
            return;

        const auto outFile = context.OpenAssetFile(std::format("soundbank/{}.snapshots.csv", sndBank.name));
        if (!outFile)
        {
            con::error("Failed to open sound snapshot output file: \"{}\"", sndBank.name);
            return;
        }

        CsvOutputStream csvStream(*outFile);
        for (const auto* header : {"name", "occlusion", "fadeIn", "fadeInCurve", "fadeOut", "fadeOutCurve", "distance"})
            csvStream.WriteColumn(header);
        for (const auto* snapshotGroup : SOUND_SNAPSHOT_GROUPS)
            csvStream.WriteColumn(snapshotGroup);
        csvStream.NextRow();

        for (auto i = 0u; i < sndBank.snapshotCount; i++)
        {
            const auto& snapshot = sndBank.snapshots[i];
            csvStream.WriteColumn(snapshot.name);
            csvStream.WriteColumn(snapshot.occlusionName);
            csvStream.WriteColumn(FormatFloat(snapshot.fadeIn));
            csvStream.WriteColumn(FormatHash(snapshot.fadeInCurve, SOUND_CURVES, std::extent_v<decltype(SOUND_CURVES)>));
            csvStream.WriteColumn(FormatFloat(snapshot.fadeOut));
            csvStream.WriteColumn(FormatHash(snapshot.fadeOutCurve, SOUND_CURVES, std::extent_v<decltype(SOUND_CURVES)>));
            csvStream.WriteColumn(FormatFloat(snapshot.distance));
            for (auto group = 0u; group < std::extent_v<decltype(SOUND_SNAPSHOT_GROUPS)>; group++)
                csvStream.WriteColumn(FormatFloat(snapshot.attenuation[group]));
            csvStream.NextRow();
        }
    }
} // namespace

namespace sound
{
    void SndBankDumperT5::DumpAsset(AssetDumpingContext& context, const XAssetInfo<AssetSoundBank::Type>& asset)
    {
        const auto* sndBank = asset.Asset();

        DumpAliases(context, *sndBank);
        DumpRadverbs(context, *sndBank);
        DumpSnapshots(context, *sndBank);
    }
} // namespace sound
