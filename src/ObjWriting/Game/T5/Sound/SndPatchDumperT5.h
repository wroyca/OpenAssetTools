#pragma once

#include "Dumping/AbstractAssetDumper.h"
#include "Game/T5/T5.h"

namespace sound
{
    class SndPatchDumperT5 final : public AbstractAssetDumper<T5::AssetSoundPatch>
    {
    protected:
        void DumpAsset(AssetDumpingContext& context, const XAssetInfo<T5::AssetSoundPatch::Type>& asset) override;
    };
} // namespace sound
