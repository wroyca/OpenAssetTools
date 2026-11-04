#pragma once

#include "Dumping/IAssetDumper.h"

namespace bsp
{
    class BspWriterT5 final : public IAssetDumper
    {
    public:
        [[nodiscard]] size_t GetProgressTotalCount(AssetDumpingContext& context) const override;
        void Dump(AssetDumpingContext& context) override;
    };
} // namespace bsp
