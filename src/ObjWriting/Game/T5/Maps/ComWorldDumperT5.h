#pragma once

#include "Dumping/AbstractAssetDumper.h"
#include "Game/T5/T5.h"

namespace com_world
{
    class DumperT5 final : public AbstractAssetDumper<T5::AssetComWorld>
    {
    protected:
        void DumpAsset(AssetDumpingContext& context, const XAssetInfo<T5::AssetComWorld::Type>& asset) override;
    };
} // namespace com_world
