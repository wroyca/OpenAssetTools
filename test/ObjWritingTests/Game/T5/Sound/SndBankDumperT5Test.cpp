#include "Game/T5/Sound/SndBankDumperT5.h"

#include "Csv/CsvStream.h"
#include "Game/T5/CommonT5.h"
#include "SearchPath/MockOutputPath.h"
#include "SearchPath/MockSearchPath.h"

#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <sstream>

using namespace T5;

namespace
{
    std::vector<std::vector<std::string>> ReadCsv(const MockOutputPath& output, const std::string& fileName)
    {
        const auto* file = output.GetMockedFile(fileName);
        REQUIRE(file != nullptr);

        std::istringstream stream(file->AsString());
        const CsvInputStream csv(stream);
        std::vector<std::vector<std::string>> rows;
        std::vector<std::string> row;
        while (csv.NextRow(row))
            rows.emplace_back(row);

        return rows;
    }

    std::string Column(const std::vector<std::vector<std::string>>& rows, const size_t rowIndex, const std::string& header)
    {
        const auto& headers = rows[0];
        const auto column = std::ranges::find(headers, header);
        REQUIRE(column != headers.end());

        return rows[rowIndex][static_cast<size_t>(column - headers.begin())];
    }

    TEST_CASE("SndBank dumper writes aliases, sound files and radverbs (T5)", "[t5][soundbank][system]")
    {
        unsigned seekTable[]{0u, 2048u};
        char data[]{9, 8, 7};

        LoadedSound loadedSound{};
        loadedSound.name = "sound\\test\\beep.wav";
        loadedSound.sound.version = 1;
        loadedSound.sound.header_size = 2096;
        loadedSound.sound.format = SND_ASSET_FORMAT_MSADPCM;
        loadedSound.sound.seek_table_count = 2;
        loadedSound.sound.seek_table = seekTable;
        loadedSound.sound.data_size = sizeof(data);
        loadedSound.sound.data = data;

        SoundFile soundFile{};
        soundFile.u.loadSnd = &loadedSound;
        soundFile.type = SAT_LOADED;
        soundFile.exists = 1;

        snd_alias_t aliases[2]{};
        aliases[0].name = "test_beep";
        aliases[0].id = Common::SND_HashName("test_beep");
        aliases[0].soundFile = &soundFile;
        aliases[0].volMin = 65535;
        aliases[0].pitchMin = 32767;
        aliases[0].probability = static_cast<char>(255);
        aliases[0].duck = Common::SND_HashName("default");
        aliases[0].contextType = 0x1234abcd;
        aliases[0].dryCurve = 15;
        aliases[0].flags = (1u << 14) | (18u << 16) | 1u | (5u << 29);
        aliases[1] = aliases[0];
        aliases[1].secondaryname = "test_other";
        aliases[1].limitCount = 4;

        snd_alias_list_t aliasList{};
        aliasList.name = "test_beep";
        aliasList.id = aliases[0].id;
        aliasList.head = aliases;
        aliasList.count = 2;

        snd_radverb radverb{};
        std::strcpy(radverb.name, "test_room");
        radverb.smoothing = 0.1f;
        radverb.returnGain = 0.25f;

        SndBank bank{};
        bank.name = "test_bank.all";
        bank.aliasCount = 1;
        bank.alias = &aliasList;
        bank.radverbCount = 1;
        bank.radverbs = &radverb;

        Zone zone("DumpingZone", 0, GameId::T5, GamePlatform::PC);
        zone.m_pools.AddAsset(std::make_unique<XAssetInfo<SndBank>>(
            ASSET_TYPE_SOUND, bank.name, &bank, std::vector<XAssetInfoGeneric*>(), std::vector<scr_string_t>(), std::vector<IndirectAssetReference>(), &zone));

        MockSearchPath objPath;
        MockOutputPath output;
        AssetDumpingContext context(zone, "", output, objPath, std::nullopt);
        sound::SndBankDumperT5 dumper;
        dumper.Dump(context);

        const auto rows = ReadCsv(output, "soundbank/test_bank.all.aliases.csv");
        REQUIRE(rows.size() == 3u);
        REQUIRE(Column(rows, 1, "name") == "test_beep");
        REQUIRE(Column(rows, 1, "file") == "sound\\test\\beep.wav");
        REQUIRE(Column(rows, 1, "vol_min") == "100");
        REQUIRE(Column(rows, 1, "pitch_min") == "0");
        REQUIRE(Column(rows, 1, "probability") == "1");
        REQUIRE(Column(rows, 1, "duck") == "default");
        REQUIRE(Column(rows, 1, "context_type") == "@1234abcd");
        REQUIRE(Column(rows, 1, "context_value").empty());
        REQUIRE(Column(rows, 1, "volume_falloff_curve") == "allon");
        REQUIRE(Column(rows, 1, "type") == "loaded");
        REQUIRE(Column(rows, 1, "group") == "ui");
        REQUIRE(Column(rows, 1, "loop") == "looping");
        REQUIRE(Column(rows, 1, "randomize_type") == "volume variant");
        REQUIRE(Column(rows, 2, "secondary") == "test_other");
        REQUIRE(Column(rows, 2, "limit_count") == "4");

        const auto radverbRows = ReadCsv(output, "soundbank/test_bank.all.radverbs.csv");
        REQUIRE(radverbRows.size() == 2u);
        REQUIRE(Column(radverbRows, 1, "name") == "test_room");
        REQUIRE(Column(radverbRows, 1, "smoothing") == "0.1");
        REQUIRE(Column(radverbRows, 1, "returnGain") == "0.25");
        REQUIRE(output.GetMockedFile("soundbank/test_bank.all.snapshots.csv") == nullptr);

        const auto* soundOutput = output.GetMockedFile("sound/test/beep.wav");
        REQUIRE(soundOutput != nullptr);
        const auto soundData = soundOutput->AsString();
        REQUIRE(soundData.size() == 2096u + sizeof(data));

        unsigned headerSize, format, seekTableCount, secondSeekEntry;
        std::memcpy(&headerSize, &soundData[0x10], sizeof(headerSize));
        std::memcpy(&format, &soundData[0x1C], sizeof(format));
        std::memcpy(&seekTableCount, &soundData[0x28], sizeof(seekTableCount));
        std::memcpy(&secondSeekEntry, &soundData[0x3C], sizeof(secondSeekEntry));
        REQUIRE(headerSize == 2096u);
        REQUIRE(format == SND_ASSET_FORMAT_MSADPCM);
        REQUIRE(seekTableCount == 2u);
        REQUIRE(secondSeekEntry == 2048u);
        REQUIRE(soundData[2096] == 9);
    }
} // namespace
