#include "EntityMathT5.h"

#include <cfloat>
#include <cmath>
#include <cstdint>

namespace
{
    constexpr auto DEG_TO_RAD = 0.017453292f;

    void SinCos(const float angle, float& sine, float& cosine)
    {
        const auto radians = static_cast<double>(angle * DEG_TO_RAD);
        sine = static_cast<float>(std::sin(radians));
        cosine = static_cast<float>(std::cos(radians));
    }

    float Vec4LengthSq(const float* v)
    {
        auto lengthSq = static_cast<double>(v[0]) * static_cast<double>(v[0]);
        lengthSq += static_cast<double>(v[1]) * static_cast<double>(v[1]);
        lengthSq += static_cast<double>(v[2]) * static_cast<double>(v[2]);
        lengthSq += static_cast<double>(v[3]) * static_cast<double>(v[3]);

        return static_cast<float>(lengthSq);
    }

    void MatrixTransformVector(const float* in, const float (&matrix)[3][3], float* out)
    {
        for (auto j = 0; j < 3; j++)
            out[j] = static_cast<float>(static_cast<double>(in[0]) * matrix[0][j] + static_cast<double>(in[1]) * matrix[1][j]
                                        + static_cast<double>(in[2]) * matrix[2][j]);
    }

    bool XModelGetStaticBoundsExact(const T5::XModel& model, const float (&axis)[3][3], float* mins, float* maxs)
    {
        if (model.numCollSurfs == 0)
            return false;

        for (auto j = 0; j < 3; j++)
        {
            mins[j] = FLT_MAX;
            maxs[j] = -FLT_MAX;
        }

        if (model.collLod < 0 || model.collLod >= 4 || !model.surfs)
            return true;

        const auto& lodInfo = model.lodInfo[model.collLod];
        for (auto surfIndex = 0u; surfIndex < lodInfo.numsurfs; surfIndex++)
        {
            const auto& surf = model.surfs[lodInfo.surfIndex + surfIndex];
            for (auto vertListIndex = 0u; vertListIndex < surf.vertListCount; vertListIndex++)
            {
                const auto& vertList = surf.vertList[vertListIndex];
                for (auto triIndex = 0u; triIndex < vertList.triCount; triIndex++)
                {
                    const auto* tri = reinterpret_cast<const uint16_t*>(surf.triIndices) + (vertList.triOffset + triIndex) * 3u;
                    for (auto corner = 0u; corner < 3u; corner++)
                    {
                        const auto vertIndex = tri[corner];
                        float rotated[3];
                        MatrixTransformVector(surf.verts0[vertIndex].xyz.v, axis, rotated);
                        for (auto j = 0; j < 3; j++)
                        {
                            if (mins[j] > rotated[j])
                                mins[j] = rotated[j];
                            if (rotated[j] > maxs[j])
                                maxs[j] = rotated[j];
                        }
                    }
                }
            }
        }

        return true;
    }
} // namespace

namespace T5
{
    void AnglesToAxis(const float* angles, float (&axis)[3][3])
    {
        float sinYaw, cosYaw, sinPitch, cosPitch, sinRoll, cosRoll;
        SinCos(angles[1], sinYaw, cosYaw);
        SinCos(angles[0], sinPitch, cosPitch);
        SinCos(angles[2], sinRoll, cosRoll);

        const double sy = sinYaw;
        const double cy = cosYaw;
        const double sp = sinPitch;
        const double cp = cosPitch;
        const double sr = sinRoll;
        const double cr = cosRoll;

        axis[0][0] = static_cast<float>(cp * cy);
        axis[0][1] = static_cast<float>(cp * sy);
        axis[0][2] = static_cast<float>(-sp);
        axis[1][0] = static_cast<float>(sr * sp * cy + -sy * cr);
        axis[1][1] = static_cast<float>(sr * sp * sy + cr * cy);
        axis[1][2] = static_cast<float>(sr * cp);
        axis[2][0] = static_cast<float>(cr * sp * cy + -sr * -sy);
        axis[2][1] = static_cast<float>(cr * sp * sy + -sr * cy);
        axis[2][2] = static_cast<float>(cr * cp);
    }

    void AxisToQuat(const float (&axis)[3][3], float* quat)
    {
        float test[4][4];
        test[0][0] = axis[1][2] - axis[2][1];
        test[0][1] = axis[2][0] - axis[0][2];
        test[0][2] = axis[0][1] - axis[1][0];
        test[0][3] = static_cast<float>(static_cast<double>(axis[0][0]) + axis[1][1] + axis[2][2] + 1.0);

        auto best = 0;
        auto testSizeSq = Vec4LengthSq(test[0]);
        if (testSizeSq < 1.0f)
        {
            test[1][0] = axis[2][0] + axis[0][2];
            test[1][1] = axis[2][1] + axis[1][2];
            test[1][2] = static_cast<float>(static_cast<double>(axis[2][2]) - axis[1][1] - axis[0][0] + 1.0);
            test[1][3] = test[0][2];

            best = 1;
            testSizeSq = Vec4LengthSq(test[1]);
            if (testSizeSq < 1.0f)
            {
                test[2][0] = static_cast<float>(static_cast<double>(axis[0][0]) - axis[1][1] - axis[2][2] + 1.0);
                test[2][1] = axis[1][0] + axis[0][1];
                test[2][2] = test[1][0];
                test[2][3] = test[0][0];

                best = 2;
                testSizeSq = Vec4LengthSq(test[2]);
                if (testSizeSq < 1.0f)
                {
                    test[3][0] = test[2][1];
                    test[3][1] = static_cast<float>(static_cast<double>(axis[1][1]) - axis[0][0] - axis[2][2] + 1.0);
                    test[3][2] = test[1][1];
                    test[3][3] = test[0][1];

                    best = 3;
                    testSizeSq = Vec4LengthSq(test[3]);
                }
            }
        }

        const auto invLength = 1.0f / std::sqrt(testSizeSq);
        for (auto i = 0; i < 4; i++)
            quat[i] = invLength * test[best][i];
    }

    void AnglesToQuat(const float* angles, float* quat)
    {
        float axis[3][3];
        AnglesToAxis(angles, axis);
        AxisToQuat(axis, quat);
    }

    void MatrixTransformVector43(const float* in, const float (&xform)[4][3], float* out)
    {
        for (auto j = 0; j < 3; j++)
            out[j] = static_cast<float>(static_cast<double>(in[0]) * xform[0][j] + static_cast<double>(in[1]) * xform[1][j]
                                        + static_cast<double>(in[2]) * xform[2][j] + xform[3][j]);
    }

    void MatrixTransposeTransformVector43(const float* in, const float (&xform)[4][3], float* out)
    {
        const float temp[3]{in[0] - xform[3][0], in[1] - xform[3][1], in[2] - xform[3][2]};
        for (auto j = 0; j < 3; j++)
            out[j] = static_cast<float>(static_cast<double>(xform[j][0]) * temp[0] + static_cast<double>(xform[j][1]) * temp[1]
                                        + static_cast<double>(xform[j][2]) * temp[2]);
    }

    void AnglesToTransform(const float* origin, const float* angles, float (&xform)[4][3])
    {
        float axis[3][3];
        AnglesToAxis(angles, axis);
        for (auto i = 0; i < 3; i++)
        {
            for (auto j = 0; j < 3; j++)
                xform[i][j] = axis[i][j];
            xform[3][i] = origin[i];
        }
    }

    float GetAttachPointDistance(const float* p0, const float* p1)
    {
        const float delta[3]{p0[0] - p1[0], p0[1] - p1[1], p0[2] - p1[2]};
        const auto lengthSq =
            static_cast<float>(static_cast<double>(delta[0]) * delta[0] + static_cast<double>(delta[1]) * delta[1] + static_cast<double>(delta[2]) * delta[2]);
        if (lengthSq > 0.01f)
            return std::sqrt(lengthSq);

        return 100.0f;
    }

    int GetConstraintTimeout(const float timeout)
    {
        return static_cast<int>(static_cast<double>(timeout) * 1000.0);
    }

    void InitStaticModel(cStaticModel_s& staticModel, const float* origin, const float* angles, const float scale)
    {
        for (auto i = 0; i < 3; i++)
            staticModel.origin[i] = origin[i];

        float axis[3][3];
        AnglesToAxis(angles, axis);

        const auto invScale = 1.0f / scale;
        for (auto i = 0; i < 3; i++)
        {
            for (auto j = 0; j < 3; j++)
                staticModel.invScaledAxis[i][j] = invScale * axis[j][i];
        }

        for (auto& row : axis)
        {
            for (auto& value : row)
                value = scale * value;
        }

        if (!XModelGetStaticBoundsExact(*staticModel.xmodel, axis, staticModel.absmin, staticModel.absmax))
            return;

        for (auto i = 0; i < 3; i++)
        {
            staticModel.absmin[i] = staticModel.absmin[i] + origin[i];
            staticModel.absmax[i] = staticModel.absmax[i] + origin[i];
        }
    }
} // namespace T5
