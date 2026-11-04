#pragma once

#include "Asset/IAssetCreator.h"
#include "Game/T5/T5.h"
#include "SearchPath/ISearchPath.h"
#include "Utils/MemoryManager.h"
#include "Zone/Zone.h"

#include <memory>

namespace clip_map
{
    std::unique_ptr<AssetCreator<T5::AssetClipMap>> CreateLoaderT5(MemoryManager& memory, ISearchPath& searchPath, Zone& zone);
    std::unique_ptr<AssetCreator<T5::AssetClipMapPvs>> CreatePvsLoaderT5(MemoryManager& memory, ISearchPath& searchPath, Zone& zone);
} // namespace clip_map
