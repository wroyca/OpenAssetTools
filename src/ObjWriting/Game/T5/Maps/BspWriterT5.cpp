#include "BspWriterT5.h"

#include "BspDumperStateT5.h"
#include "Utils/Logging/Log.h"

using namespace bsp;

size_t BspWriterT5::GetProgressTotalCount(AssetDumpingContext& context) const
{
    return 0u;
}

void BspWriterT5::Dump(AssetDumpingContext& context)
{
    auto* state = context.GetZoneAssetDumperState<BspDumperStateT5>();
    for (const auto& [name, bsp] : state->m_bsps)
    {
        const auto file = context.OpenAssetFile(name);
        if (!file)
        {
            con::error("Could not open bsp file \"{}\" for dumping", name);
            continue;
        }

        bsp.Write(*file);
    }
}
