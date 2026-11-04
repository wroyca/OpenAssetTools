#pragma once

#include "Dumping/AbstractAssetDumper.h"
#include "Game/T5/T5.h"

namespace sound
{
    class SndBankDumperT5 final : public AbstractAssetDumper<T5::AssetSoundBank>
    {
    protected:
        void DumpAsset(AssetDumpingContext& context, const XAssetInfo<T5::AssetSoundBank::Type>& asset) override;
    };
} // namespace sound
