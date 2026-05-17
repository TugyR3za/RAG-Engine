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

    [[nodiscard]] inline Vec3 ExtractTranslation(const Mat4& matrix)
    {
        return Vec3{matrix(0, 3), matrix(1, 3), matrix(2, 3)};
    }

    [[nodiscard]] inline Vec3 TransformDirection(const Mat4& matrix, Vec3 direction)
    {
        return Normalize(Vec3{
            (matrix(0, 0) * direction.x) + (matrix(0, 1) * direction.y) + (matrix(0, 2) * direction.z),
            (matrix(1, 0) * direction.x) + (matrix(1, 1) * direction.y) + (matrix(1, 2) * direction.z),
            (matrix(2, 0) * direction.x) + (matrix(2, 1) * direction.y) + (matrix(2, 2) * direction.z)});
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
