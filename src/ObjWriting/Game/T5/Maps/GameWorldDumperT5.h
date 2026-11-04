#pragma once

#include "Dumping/AbstractAssetDumper.h"
#include "Game/T5/T5.h"

namespace game_world
{
    class DumperSpT5 final : public AbstractAssetDumper<T5::AssetGameWorldSp>
    {
    protected:
        void DumpAsset(AssetDumpingContext& context, const XAssetInfo<T5::AssetGameWorldSp::Type>& asset) override;
    };

    class DumperMpT5 final : public AbstractAssetDumper<T5::AssetGameWorldMp>
    {
    protected:
        void DumpAsset(AssetDumpingContext& context, const XAssetInfo<T5::AssetGameWorldMp::Type>& asset) override;
    };
} // namespace game_world
