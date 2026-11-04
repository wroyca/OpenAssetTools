#pragma once

#include "Asset/IAssetCreator.h"
#include "Game/T5/T5.h"
#include "SearchPath/ISearchPath.h"
#include "Utils/MemoryManager.h"
#include "Zone/Zone.h"

#include <memory>

namespace game_world
{
    std::unique_ptr<AssetCreator<T5::AssetGameWorldSp>> CreateSpLoaderT5(MemoryManager& memory, ISearchPath& searchPath, Zone& zone);
    std::unique_ptr<AssetCreator<T5::AssetGameWorldMp>> CreateMpLoaderT5(MemoryManager& memory, ISearchPath& searchPath, Zone& zone);
} // namespace game_world
