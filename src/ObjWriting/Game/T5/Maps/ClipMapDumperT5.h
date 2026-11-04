#pragma once

#include "Dumping/AbstractAssetDumper.h"
#include "Game/T5/T5.h"

namespace clip_map
{
    class DumperT5 final : public AbstractAssetDumper<T5::AssetClipMap>
    {
    protected:
        void DumpAsset(AssetDumpingContext& context, const XAssetInfo<T5::AssetClipMap::Type>& asset) override;
    };

    class DumperPvsT5 final : public AbstractAssetDumper<T5::AssetClipMapPvs>
    {
    protected:
        void DumpAsset(AssetDumpingContext& context, const XAssetInfo<T5::AssetClipMapPvs::Type>& asset) override;
    };
} // namespace clip_map
