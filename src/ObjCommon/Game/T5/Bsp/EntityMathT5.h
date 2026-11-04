#pragma once

#include "Game/T5/T5.h"

namespace T5
{
    void AnglesToAxis(const float* angles, float (&axis)[3][3]);
    void AxisToQuat(const float (&axis)[3][3], float* quat);
    void AnglesToQuat(const float* angles, float* quat);

    void MatrixTransformVector43(const float* in, const float (&xform)[4][3], float* out);
    void MatrixTransposeTransformVector43(const float* in, const float (&xform)[4][3], float* out);
    void AnglesToTransform(const float* origin, const float* angles, float (&xform)[4][3]);

    [[nodiscard]] float GetAttachPointDistance(const float* p0, const float* p1);
    [[nodiscard]] int GetConstraintTimeout(float timeout);

    void InitStaticModel(cStaticModel_s& staticModel, const float* origin, const float* angles, float scale);
} // namespace T5
