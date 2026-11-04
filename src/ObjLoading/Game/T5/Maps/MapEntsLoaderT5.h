#pragma once

#include "Asset/IAssetCreator.h"
#include "Game/T5/T5.h"
#include "SearchPath/ISearchPath.h"
#include "Utils/MemoryManager.h"

#include <memory>

namespace map_ents
{
    std::unique_ptr<AssetCreator<T5::AssetMapEnts>> CreateLoaderT5(MemoryManager& memory, ISearchPath& searchPath);
} // namespace map_ents
