#pragma once

#include "Asset/IAssetCreator.h"
#include "Game/T5/T5.h"
#include "SearchPath/ISearchPath.h"
#include "Utils/MemoryManager.h"

#include <memory>

namespace com_world
{
    std::unique_ptr<AssetCreator<T5::AssetComWorld>> CreateLoaderT5(MemoryManager& memory, ISearchPath& searchPath);
} // namespace com_world
