#include "LeafBrushPartitionT5.h"

#include <cfloat>

namespace T5
{
    float GetLeafBrushPartitionScore(
        const cbrush_t* brushes, const uint16_t* leafBrushes, const int leafBrushCount, const int axis, const float* mins, const float* maxs, float& dist)
    {
        auto rightBrushCount = -1;
        auto leftBrushCount = -1;
        auto min = -FLT_MAX;
        auto max = FLT_MAX;

        for (auto i = 0; i < leafBrushCount; i++)
        {
            const auto& brush = brushes[leafBrushes[i]];
            if (brush.mins[axis] >= dist)
            {
                rightBrushCount++;
                if (max > brush.mins[axis])
                    max = brush.mins[axis];
            }
            else if (dist >= brush.maxs[axis])
            {
                leftBrushCount++;
                if (brush.maxs[axis] > min)
                    min = brush.maxs[axis];
            }
        }

        const auto score = rightBrushCount < leftBrushCount ? rightBrushCount : leftBrushCount;
        dist = (min + max) * 0.5f;
        if (score <= 0)
            return 0.0f;

        const auto rightSpace = max - mins[axis];
        const auto leftSpace = maxs[axis] - min;
        const auto space = leftSpace - rightSpace >= 0.0f ? rightSpace : leftSpace;

        return static_cast<float>(score) * space;
    }

    float UpdateLeafBrushPartitionRange(const float range, const float distance)
    {
        return distance - range >= 0.0f ? range : distance;
    }
} // namespace T5
