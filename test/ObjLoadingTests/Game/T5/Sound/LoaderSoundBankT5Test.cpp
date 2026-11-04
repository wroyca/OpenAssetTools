#include "Game/T5/Sound/LoaderSoundBankT5.h"

#include "Game/T5/CommonT5.h"
#include "Game/T5/Sound/SndAliasFieldsT5.h"
#include "Game/T5/Sound/SndAssetFileT5.h"
#include "SearchPath/MockSearchPath.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <map>
#include <sstream>
#include <string>

using namespace T5;
using namespace Catch;

namespace
{
    std::string MakeAliasCsv(const std::vector<std::map<std::string, std::string>>& rows)
    {
        std::vector<std::string> headers{"name", "file", "secondary", "subtitle"};
        for (const auto& field : SND_ALIAS_FIELDS)
            headers.emplace_back(field.name);

        const std::map<std::string, std::string> defaults{
            {"vol_min",                  "100"       },
            {"vol_max",                  "100"       },
            {"pitch_min",                "0"         },
            {"pitch_max",                "0"         },
            {"probability",              "1"         },
            {"volume_falloff_curve",     "default"   },
            {"reverb_falloff_curve",     "default"   },
            {"volume_min_falloff_curve", "default"   },
            {"reverb_min_falloff_curve", "default"   },
            {"pan",                      "default"   },
            {"snapshot",                 "reference" },
            {"limit_type",               "none"      },
            {"entity_limit_type",        "none"      },
            {"move_type",                "none"      },
            {"type",                     "loaded"    },
            {"group",                    "reference" },
            {"real_delay",               "no"        },
            {"distance_lpf",             "no"        },
            {"doppler",                  "no"        },
            {"is_big",                   "no"        },
            {"loop",                     "nonlooping"},
            {"spatialized",              "2d"        },
            {"futz",                     "no"        },
            {"music",                    "no"        },
            {"timescale",                "no"        },
            {"pause",                    "no"        },
            {"stop_on_death",            "no"        },
            {"bus",                      "world"     },
            {"voice_limit",              "no"        },
        };

        std::ostringstream csv;
        for (auto i = 0u; i < headers.size(); i++)
            csv << (i ? "," : "") << headers[i];
        csv << "\n";

        for (const auto& row : rows)
        {
            for (auto i = 0u; i < headers.size(); i++)
            {
                std::string value = "0";
                if (const auto rowValue = row.find(headers[i]); rowValue != row.end())
                    value = rowValue->second;
                else if (const auto defaultValue = defaults.find(headers[i]); defaultValue != defaults.end())
                    value = defaultValue->second;
                else if (i < 4 || SND_ALIAS_FIELDS[i - 4].type == SndAliasFieldType::HASH || SND_ALIAS_FIELDS[i - 4].type == SndAliasFieldType::FLAG_BITS)
                    value = "";

                csv << (i ? "," : "") << value;
            }
            csv << "\n";
        }

        return csv.str();
    }

    std::string MakeSoundFile()
    {
        unsigned seekTable[]{0u, 4096u};
        char data[]{1, 2, 3, 4, 5, 6};

        snd_asset asset{};
        asset.version = 1;
        asset.frame_count = 1024;
        asset.frame_rate = 48000;
        asset.channel_count = 2;
        asset.header_size = 2096;
        asset.block_size = 67072;
        asset.buffer_size = 536576;
        asset.format = SND_ASSET_FORMAT_WMA;
        asset.channel_flags = static_cast<snd_asset_channel>(SND_ASSET_CHANNEL_L | SND_ASSET_CHANNEL_R);
        asset.seek_table_count = 2;
        asset.seek_table = seekTable;
        asset.data_size = sizeof(data);
        asset.data = data;

        std::ostringstream stream;
        sound::WriteSndAssetFileT5(stream, asset);

        return stream.str();
    }

    TEST_CASE("SoundBank loader restores aliases, sound files and radverbs (T5)", "[t5][soundbank][system]")
    {
        MockSearchPath searchPath;
        searchPath.AddFileData("soundbank/test_bank.all.aliases.csv",
                               MakeAliasCsv({
                                   {{"name", "test_beep"},
                                    {"file", "sound\\test\\beep.wav"},
                                    {"duck", "default"},
                                    {"group", "ui"},
                                    {"loop", "looping"},
                                    {"randomize_type", "volume pitch"}},
                                   {{"name", "test_beep"}, {"file", "sound\\test\\beep.wav"}, {"vol_min", "0"}, {"pitch_min", "-1200"}, {"limit_count", "4"}},
                                   {{"name", "test_music"}, {"file", "sound\\test\\music.wav"}, {"type", "streamed"}, {"context_type", "@1234abcd"}},
        }));
        searchPath.AddFileData("sound/test/beep.wav", MakeSoundFile());
        searchPath.AddFileData("soundbank/test_bank.all.radverbs.csv",
                               "name,smoothing,earlyTime,lateTime,earlyGain,lateGain,earlyLpf,lateLpf,inputLpf,dampLpf,wallReflect,dryGain,earlySize,lateSize,"
                               "diffusion,returnGain\n"
                               "test_room,0.5,0.1,0.2,1,1,0.3,0.4,0.5,0.6,0.7,0.8,1,2,0.9,0.25\n");

        Zone zone("LoadingZone", 0, GameId::T5, GamePlatform::PC);
        AssetCreatorCollection creatorCollection(zone);
        IgnoredAssetLookup ignoredAssetLookup;
        AssetCreationContext context(zone, &creatorCollection, &ignoredAssetLookup);
        const auto loader = sound::CreateSoundBankLoaderT5(zone.Memory(), searchPath);
        const auto result = loader->CreateAsset("test_bank.all", context);

        REQUIRE(result.HasBeenSuccessful());
        const auto* bank = reinterpret_cast<XAssetInfo<SndBank>*>(result.GetAssetInfo())->Asset();

        REQUIRE(std::string(bank->name) == "test_bank.all");
        REQUIRE(bank->aliasCount == 2u);

        const auto& beepList = bank->alias[0];
        REQUIRE(std::string(beepList.name) == "test_beep");
        REQUIRE(beepList.id == Common::SND_HashName("test_beep"));
        REQUIRE(beepList.count == 2);

        const auto& beep = beepList.head[0];
        REQUIRE(beep.id == beepList.id);
        REQUIRE(beep.volMin == 65535u);
        REQUIRE(beep.pitchMin == 32767u);
        REQUIRE(static_cast<uint8_t>(beep.probability) == 255u);
        REQUIRE(beep.duck == Common::SND_HashName("default"));
        REQUIRE(beep.flags == ((1u << 14) | (18u << 16) | 1u | (3u << 29)));
        REQUIRE(beep.secondaryname == nullptr);
        REQUIRE(beep.subtitle == nullptr);

        const auto& quietBeep = beepList.head[1];
        REQUIRE(quietBeep.volMin == 0u);
        REQUIRE(quietBeep.pitchMin == 16383u);
        REQUIRE(quietBeep.limitCount == 4u);
        REQUIRE(quietBeep.soundFile == beep.soundFile);

        REQUIRE(beep.soundFile->type == SAT_LOADED);
        REQUIRE(beep.soundFile->exists == 1);
        const auto& loadedSound = *beep.soundFile->u.loadSnd;
        REQUIRE(std::string(loadedSound.name) == "sound\\test\\beep.wav");
        REQUIRE(loadedSound.sound.format == SND_ASSET_FORMAT_WMA);
        REQUIRE(loadedSound.sound.frame_rate == 48000u);
        REQUIRE(loadedSound.sound.header_size == 2096u);
        REQUIRE(loadedSound.sound.seek_table_count == 2u);
        REQUIRE(loadedSound.sound.seek_table[1] == 4096u);
        REQUIRE(loadedSound.sound.data_size == 6u);
        REQUIRE(loadedSound.sound.data[5] == 6);

        const auto& music = bank->alias[1].head[0];
        REQUIRE(music.contextType == 0x1234abcdu);
        REQUIRE(music.soundFile->type == SAT_STREAMED);
        REQUIRE(std::string(music.soundFile->u.streamSnd->filename) == "sound\\test\\music.wav");
        REQUIRE(music.soundFile->u.streamSnd->primeSnd == nullptr);

        for (auto i = 0u; i < bank->aliasCount; i++)
        {
            auto index = bank->alias[i].id % bank->aliasCount;
            while (bank->alias[bank->aliasIndex[index].value].id != bank->alias[i].id)
                index = bank->aliasIndex[index].next;
            REQUIRE(bank->aliasIndex[index].value == i);
        }

        REQUIRE(bank->radverbCount == 1u);
        REQUIRE(std::string(bank->radverbs[0].name) == "test_room");
        REQUIRE(bank->radverbs[0].id == Common::SND_HashName("test_room"));
        REQUIRE(bank->radverbs[0].lateSize == Approx(2.0f));
        REQUIRE(bank->radverbs[0].returnGain == Approx(0.25f));
        REQUIRE(bank->snapshotCount == 0u);
    }

    TEST_CASE("SoundBank loader rejects an unknown enum value (T5)", "[t5][soundbank]")
    {
        MockSearchPath searchPath;
        searchPath.AddFileData("soundbank/bad_bank.all.aliases.csv",
                               MakeAliasCsv({
                                   {{"name", "bad"}, {"bus", "radio"}}
        }));

        Zone zone("LoadingZone", 0, GameId::T5, GamePlatform::PC);
        AssetCreatorCollection creatorCollection(zone);
        IgnoredAssetLookup ignoredAssetLookup;
        AssetCreationContext context(zone, &creatorCollection, &ignoredAssetLookup);
        const auto loader = sound::CreateSoundBankLoaderT5(zone.Memory(), searchPath);

        REQUIRE(!loader->CreateAsset("bad_bank.all", context).HasBeenSuccessful());
    }
} // namespace
