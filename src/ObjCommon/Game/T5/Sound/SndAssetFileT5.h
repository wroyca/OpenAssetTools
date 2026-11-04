#pragma once

#include "Game/T5/T5.h"
#include "Utils/MemoryManager.h"

#include <istream>
#include <ostream>

namespace sound
{
    void WriteSndAssetFileT5(std::ostream& stream, const T5::snd_asset& asset);
    bool ReadSndAssetFileT5(std::istream& stream, T5::snd_asset& asset, MemoryManager& memory);
} // namespace sound
