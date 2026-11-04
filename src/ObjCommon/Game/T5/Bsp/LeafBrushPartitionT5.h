#pragma once

#include "Game/T5/T5.h"

#include <cstdint>

namespace T5
{
    [[nodiscard]] float GetLeafBrushPartitionScore(
        const cbrush_t* brushes, const uint16_t* leafBrushes, int leafBrushCount, int axis, const float* mins, const float* maxs, float& dist);

    [[nodiscard]] float UpdateLeafBrushPartitionRange(float range, float distance);
} // namespace T5
