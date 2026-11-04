#pragma once

#include "Asset/IAssetCreator.h"
#include "Game/T5/T5.h"
#include "SearchPath/ISearchPath.h"
#include "Utils/MemoryManager.h"

#include <memory>

namespace sound
{
    std::unique_ptr<AssetCreator<T5::AssetSoundBank>> CreateSoundBankLoaderT5(MemoryManager& memory, ISearchPath& searchPath);
} // namespace sound
