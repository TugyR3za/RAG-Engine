#pragma once

#include "Rag/Core/Base.h"

#include <array>
#include <cmath>

namespace rag::math
{
    inline constexpr f32 Pi = 3.14159265358979323846f;

    struct Vec3
    {
        f32 x = 0.0f;
        f32 y = 0.0f;
        f32 z = 0.0f;
    };

    struct Vec4
    {
        f32 x = 0.0f;
        f32 y = 0.0f;
        f32 z = 0.0f;
        f32 w = 0.0f;
    };

    // Rotation quaternion (x, y, z, w); identity by default. Matches the
    // component order used by physics back-ends such as Jolt.
    struct Quat
    {
        f32 x = 0.0f;
        f32 y = 0.0f;
        f32 z = 0.0f;
        f32 w = 1.0f;
    };

    struct Mat4
    {
        std::array<f32, 16> elements{};

        [[nodiscard]] f32& operator()(u32 row, u32 column)
        {
            return elements[(column * 4u) + row];
        }

        [[nodiscard]] f32 operator()(u32 row, u32 column) const
        {
            return elements[(column * 4u) + row];
        }
    };

    [[nodiscard]] inline Vec3 operator+(Vec3 left, Vec3 right)
    {
        return Vec3{left.x + right.x, left.y + right.y, left.z + right.z};
    }

    [[nodiscard]] inline Vec3 operator-(Vec3 left, Vec3 right)
    {
        return Vec3{left.x - right.x, left.y - right.y, left.z - right.z};
    }

    [[nodiscard]] inline Vec3 operator*(Vec3 value, f32 scalar)
    {
        return Vec3{value.x * scalar, value.y * scalar, value.z * scalar};
    }

    [[nodiscard]] inline f32 Dot(Vec3 left, Vec3 right)
    {
        return (left.x * right.x) + (left.y * right.y) + (left.z * right.z);
    }

    [[nodiscard]] inline f32 Length(Vec3 value)
    {
        return std::sqrt(Dot(value, value));
    }

    [[nodiscard]] inline Vec3 Normalize(Vec3 value)
    {
        const f32 length = Length(value);
        if (length <= 0.000001f)
        {
            return Vec3{};
        }

        const f32 inverse = 1.0f / length;
        return value * inverse;
    }

    // Angle-unit conversions. Editor UIs edit rotation in degrees while the engine
    // and physics store radians, so these are the single conversion point.
    inline constexpr f32 DegreesPerRadian = 180.0f / Pi;
    inline constexpr f32 RadiansPerDegree = Pi / 180.0f;

    [[nodiscard]] inline f32 ToDegrees(f32 radians) { return radians * DegreesPerRadian; }
    [[nodiscard]] inline f32 ToRadians(f32 degrees) { return degrees * RadiansPerDegree; }

    [[nodiscard]] inline Vec3 ToDegrees(Vec3 radians)
    {
        return Vec3{radians.x * DegreesPerRadian, radians.y * DegreesPerRadian, radians.z * DegreesPerRadian};
    }

    [[nodiscard]] inline Vec3 ToRadians(Vec3 degrees)
    {
        return Vec3{degrees.x * RadiansPerDegree, degrees.y * RadiansPerDegree, degrees.z * RadiansPerDegree};
    }

    [[nodiscard]] inline Mat4 Identity()
    {
        Mat4 result{};
        result(0, 0) = 1.0f;
        result(1, 1) = 1.0f;
        result(2, 2) = 1.0f;
        result(3, 3) = 1.0f;
        return result;
    }

    [[nodiscard]] inline Mat4 Multiply(const Mat4& left, const Mat4& right)
    {
        Mat4 result{};
        for (u32 column = 0; column < 4; ++column)
        {
            for (u32 row = 0; row < 4; ++row)
            {
                result(row, column) =
                    (left(row, 0) * right(0, column)) +
                    (left(row, 1) * right(1, column)) +
                    (left(row, 2) * right(2, column)) +
                    (left(row, 3) * right(3, column));
            }
        }
        return result;
    }

    [[nodiscard]] inline Mat4 Translation(Vec3 position)
    {
        Mat4 result = Identity();
        result(0, 3) = position.x;
        result(1, 3) = position.y;
        result(2, 3) = position.z;
        return result;
    }

    [[nodiscard]] inline Mat4 Scale(Vec3 scale)
    {
        Mat4 result = Identity();
        result(0, 0) = scale.x;
        result(1, 1) = scale.y;
        result(2, 2) = scale.z;
        return result;
    }

    [[nodiscard]] inline Mat4 RotationX(f32 radians)
    {
        Mat4 result = Identity();
        const f32 c = std::cos(radians);
        const f32 s = std::sin(radians);
        result(1, 1) = c;
        result(1, 2) = -s;
        result(2, 1) = s;
        result(2, 2) = c;
        return result;
    }

    [[nodiscard]] inline Mat4 RotationY(f32 radians)
    {
        Mat4 result = Identity();
        const f32 c = std::cos(radians);
        const f32 s = std::sin(radians);
        result(0, 0) = c;
        result(0, 2) = s;
        result(2, 0) = -s;
        result(2, 2) = c;
        return result;
    }

    [[nodiscard]] inline Mat4 RotationZ(f32 radians)
    {
        Mat4 result = Identity();
        const f32 c = std::cos(radians);
        const f32 s = std::sin(radians);
        result(0, 0) = c;
        result(0, 1) = -s;
        result(1, 0) = s;
        result(1, 1) = c;
        return result;
    }

    [[nodiscard]] inline Mat4 RotationEulerXYZ(Vec3 radians)
    {
        return Multiply(Multiply(RotationZ(radians.z), RotationY(radians.y)), RotationX(radians.x));
    }

    [[nodiscard]] inline Mat4 ComposeTransform(Vec3 position, Vec3 rotation_radians, Vec3 scale)
    {
        return Multiply(Multiply(Translation(position), RotationEulerXYZ(rotation_radians)), Scale(scale));
    }

    // Builds a rotation matrix from a quaternion (column-vector convention,
    // v' = R * v). The quaternion is normalized defensively so simulation drift
    // never shears the resulting basis.
    [[nodiscard]] inline Mat4 QuatToMat4(Quat q)
    {
        const f32 length_squared = (q.x * q.x) + (q.y * q.y) + (q.z * q.z) + (q.w * q.w);
        if (length_squared <= 0.000001f)
        {
            return Identity();
        }

        const f32 inverse = 1.0f / std::sqrt(length_squared);
        const f32 x = q.x * inverse;
        const f32 y = q.y * inverse;
        const f32 z = q.z * inverse;
        const f32 w = q.w * inverse;

        Mat4 result = Identity();
        result(0, 0) = 1.0f - (2.0f * ((y * y) + (z * z)));
        result(0, 1) = 2.0f * ((x * y) - (w * z));
        result(0, 2) = 2.0f * ((x * z) + (w * y));
        result(1, 0) = 2.0f * ((x * y) + (w * z));
        result(1, 1) = 1.0f - (2.0f * ((x * x) + (z * z)));
        result(1, 2) = 2.0f * ((y * z) - (w * x));
        result(2, 0) = 2.0f * ((x * z) - (w * y));
        result(2, 1) = 2.0f * ((y * z) + (w * x));
        result(2, 2) = 1.0f - (2.0f * ((x * x) + (y * y)));
        return result;
    }

    // Builds a rotation quaternion from XYZ Euler angles (radians), matching the
    // Rz*Ry*Rx convention of RotationEulerXYZ so that
    // QuatToMat4(QuatFromEulerXYZ(e)) == RotationEulerXYZ(e). Used to feed edited
    // Euler angles to a physics body, whose orientation is stored as a quaternion.
    [[nodiscard]] inline Quat QuatFromEulerXYZ(Vec3 radians)
    {
        const f32 hx = radians.x * 0.5f;
        const f32 hy = radians.y * 0.5f;
        const f32 hz = radians.z * 0.5f;
        const f32 cx = std::cos(hx);
        const f32 sx = std::sin(hx);
        const f32 cy = std::cos(hy);
        const f32 sy = std::sin(hy);
        const f32 cz = std::cos(hz);
        const f32 sz = std::sin(hz);

        Quat result;
        result.x = (cz * cy * sx) - (sz * sy * cx);
        result.y = (cz * sy * cx) + (sz * cy * sx);
        result.z = (sz * cy * cx) - (cz * sy * sx);
        result.w = (cz * cy * cx) + (sz * sy * sx);
        return result;
    }

    [[nodiscard]] inline Mat4 ComposeTransform(Vec3 position, Quat rotation, Vec3 scale)
    {
        return Multiply(Multiply(Translation(position), QuatToMat4(rotation)), Scale(scale));
    }

    [[nodiscard]] inline Vec3 ExtractTranslation(const Mat4& matrix)
    {
        return Vec3{matrix(0, 3), matrix(1, 3), matrix(2, 3)};
    }

    // The position/rotation/scale a TRS matrix was built from. Rotation is XYZ
    // Euler radians in the same Rz*Ry*Rx convention as RotationEulerXYZ, so that
    // ComposeTransform(d.position, d.rotation_radians, d.scale) reproduces the
    // original matrix. This is the inverse of ComposeTransform and lets an editor
    // gizmo write a manipulated model matrix back into a TransformComponent.
    struct TransformDecomposition
    {
        Vec3 position{};
        Vec3 rotation_radians{};
        Vec3 scale{1.0f, 1.0f, 1.0f};
    };

    [[nodiscard]] inline TransformDecomposition DecomposeTransform(const Mat4& matrix)
    {
        TransformDecomposition result;
        result.position = ExtractTranslation(matrix);

        // Scale is the length of each basis column; a mirrored (negative
        // determinant) matrix is represented as a negative X scale so recompose
        // matches. The epsilon guards a collapsed axis from producing NaNs.
        const Vec3 column0{matrix(0, 0), matrix(1, 0), matrix(2, 0)};
        const Vec3 column1{matrix(0, 1), matrix(1, 1), matrix(2, 1)};
        const Vec3 column2{matrix(0, 2), matrix(1, 2), matrix(2, 2)};

        f32 scale_x = Length(column0);
        const f32 scale_y = Length(column1);
        const f32 scale_z = Length(column2);

        const f32 determinant =
            (column0.x * ((column1.y * column2.z) - (column1.z * column2.y))) -
            (column1.x * ((column0.y * column2.z) - (column0.z * column2.y))) +
            (column2.x * ((column0.y * column1.z) - (column0.z * column1.y)));
        if (determinant < 0.0f)
        {
            scale_x = -scale_x;
        }
        result.scale = Vec3{scale_x, scale_y, scale_z};

        const f32 inverse_x = std::abs(scale_x) > 1e-8f ? 1.0f / scale_x : 0.0f;
        const f32 inverse_y = std::abs(scale_y) > 1e-8f ? 1.0f / scale_y : 0.0f;
        const f32 inverse_z = std::abs(scale_z) > 1e-8f ? 1.0f / scale_z : 0.0f;

        // Rotation basis with the scale divided out (r_row_col).
        const f32 r00 = matrix(0, 0) * inverse_x;
        const f32 r10 = matrix(1, 0) * inverse_x;
        const f32 r20 = matrix(2, 0) * inverse_x;
        const f32 r01 = matrix(0, 1) * inverse_y;
        const f32 r11 = matrix(1, 1) * inverse_y;
        const f32 r21 = matrix(2, 1) * inverse_y;
        const f32 r22 = matrix(2, 2) * inverse_z;

        // Invert R = Rz(z)*Ry(y)*Rx(x): R(2,0) = -sin(y), R(2,1)=cos(y)sin(x),
        // R(2,2)=cos(y)cos(x), R(1,0)=sin(z)cos(y), R(0,0)=cos(z)cos(y).
        f32 rotation_x;
        f32 rotation_y;
        f32 rotation_z;
        if (std::abs(r20) < 0.99999f)
        {
            rotation_y = std::asin(-r20);
            rotation_x = std::atan2(r21, r22);
            rotation_z = std::atan2(r10, r00);
        }
        else
        {
            // Gimbal lock (cos(y) ~ 0): pin roll (z) to 0 and solve pitch (x).
            rotation_z = 0.0f;
            if (r20 < 0.0f)
            {
                rotation_y = Pi * 0.5f;
                rotation_x = std::atan2(r01, r11);
            }
            else
            {
                rotation_y = -Pi * 0.5f;
                rotation_x = std::atan2(-r01, r11);
            }
        }
        result.rotation_radians = Vec3{rotation_x, rotation_y, rotation_z};
        return result;
    }

    [[nodiscard]] inline Vec3 TransformDirection(const Mat4& matrix, Vec3 direction)
    {
        return Normalize(Vec3{
            (matrix(0, 0) * direction.x) + (matrix(0, 1) * direction.y) + (matrix(0, 2) * direction.z),
            (matrix(1, 0) * direction.x) + (matrix(1, 1) * direction.y) + (matrix(1, 2) * direction.z),
            (matrix(2, 0) * direction.x) + (matrix(2, 1) * direction.y) + (matrix(2, 2) * direction.z)});
    }

    // Transforms a position by an affine matrix (implicit w = 1), so translation
    // is applied and the result is not normalized. Complements TransformDirection
    // for points such as bounding-box corners.
    [[nodiscard]] inline Vec3 TransformPoint(const Mat4& matrix, Vec3 point)
    {
        return Vec3{
            (matrix(0, 0) * point.x) + (matrix(0, 1) * point.y) + (matrix(0, 2) * point.z) + matrix(0, 3),
            (matrix(1, 0) * point.x) + (matrix(1, 1) * point.y) + (matrix(1, 2) * point.z) + matrix(1, 3),
            (matrix(2, 0) * point.x) + (matrix(2, 1) * point.y) + (matrix(2, 2) * point.z) + matrix(2, 3)};
    }

    [[nodiscard]] inline Mat4 InverseRigidTransform(const Mat4& matrix)
    {
        Mat4 result = Identity();

        for (u32 row = 0; row < 3; ++row)
        {
            for (u32 column = 0; column < 3; ++column)
            {
                result(row, column) = matrix(column, row);
            }
        }

        const Vec3 translation = ExtractTranslation(matrix);
        result(0, 3) = -((result(0, 0) * translation.x) + (result(0, 1) * translation.y) + (result(0, 2) * translation.z));
        result(1, 3) = -((result(1, 0) * translation.x) + (result(1, 1) * translation.y) + (result(1, 2) * translation.z));
        result(2, 3) = -((result(2, 0) * translation.x) + (result(2, 1) * translation.y) + (result(2, 2) * translation.z));

        return result;
    }

    [[nodiscard]] inline Mat4 PerspectiveRH_ZO(f32 vertical_fov_radians, f32 aspect_ratio, f32 near_plane, f32 far_plane)
    {
        const f32 f = 1.0f / std::tan(vertical_fov_radians * 0.5f);

        Mat4 result{};
        result(0, 0) = f / aspect_ratio;
        result(1, 1) = f;
        result(2, 2) = far_plane / (near_plane - far_plane);
        result(2, 3) = -(far_plane * near_plane) / (far_plane - near_plane);
        result(3, 2) = -1.0f;
        return result;
    }

    [[nodiscard]] inline Mat4 OrthographicRH_ZO(f32 width, f32 height, f32 near_plane, f32 far_plane)
    {
        Mat4 result = Identity();
        result(0, 0) = 2.0f / width;
        result(1, 1) = 2.0f / height;
        result(2, 2) = 1.0f / (near_plane - far_plane);
        result(2, 3) = near_plane / (near_plane - far_plane);
        return result;
    }
}
