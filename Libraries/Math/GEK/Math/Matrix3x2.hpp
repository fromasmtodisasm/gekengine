/// @file
/// @author Todd Zupan <toddzupan@gmail.com>
/// @version $Revision: 1143 $
/// @section LICENSE
/// https://en.wikipedia.org/wiki/MIT_License
/// @section DESCRIPTION
/// Last Changed: $Date: 2016-10-13 13:29:45 -0700 (Thu, 13 Oct 2016) $
#pragma once

#include "GEK/Math/Vector2.hpp"

namespace Gek
{
    namespace Math
    {
        template <typename TYPE, typename = typename std::enable_if<std::is_arithmetic<TYPE>::value, TYPE>::type>
        struct Matrix3x2
        {
        public:
            static const Matrix3x2 Identity;

        public:
            union
            {
                struct { TYPE data[6]; };
                struct { TYPE table[3][2]; };
                struct { Vector2<TYPE> rows[3]; };

                struct
                {
                    TYPE _11, _12;
                    TYPE _21, _22;
                    TYPE _31, _32;
                };

                struct
                {
                    Vector2<TYPE> rx;
                    Vector2<TYPE> ry;
                    Vector2<TYPE> translation;
                };
            };

        public:
            static Matrix3x2 FromScale(TYPE scale)
            {
                return Matrix3x2(
                {
                    scale, 0.0f,
                    0.0f, scale,
                    0.0f, 0.0f,
                });
            }

            static Matrix3x2 FromScale(const Vector2<TYPE> &scale)
            {
                return Matrix3x2(
                {
                    scale.x, 0.0f,
                    0.0f, scale.y,
                    0.0f, 0.0f,
                });
            }

            static Matrix3x2 FromAngle(TYPE radians)
            {
                return Matrix3x2(
                {
                    std::cos(radians), -std::sin(radians),
                    std::sin(radians),  std::cos(radians),
                });
            }

        public:
            Matrix3x2(void)
            {
            }

            explicit Matrix3x2(const Matrix3x2 &matrix)
                : rows{
                matrix.rows[0],
                matrix.rows[1],
                matrix.rows[2] }
            {
            }

            explicit Matrix3x2(TYPE _11, TYPE _12, TYPE _21, TYPE _22, TYPE _31, TYPE _32)
                : data{
                    _11, _12,
                    _21, _22,
                    _31, _32 }
            {
            }

            explicit Matrix3x2(const TYPE *data)
                : data {
                data[0], data[1],
                data[2], data[3],
                data[4], data[5] }
            {
            }

            Vector2<TYPE> getScaling(void) const
            {
                return Vector2<TYPE>(_11, _22);
            }

            void operator *= (const Matrix3x2 &matrix)
            {
                (*this) = ((*this) * matrix);
            }

            Matrix3x2 operator * (const Matrix3x2 &matrix) const
            {
                return Matrix3x2({ _11 * matrix._11 + _12 * matrix._21,
                    _11 * matrix._12 + _12 * matrix._22,
                    _21 * matrix._11 + _22 * matrix._21,
                    _21 * matrix._12 + _22 * matrix._22,
                    _31 * matrix._11 + _32 * matrix._21 + matrix._31,
                    _31 * matrix._12 + _32 * matrix._22 + matrix._32 });
            }

            Matrix3x2 &operator = (const Matrix3x2 &matrix)
            {
                rows[0] = matrix.rows[0];
                rows[1] = matrix.rows[1];
                rows[2] = matrix.rows[2];
                return (*this);
            }
        };

        using Float3x2 = Matrix3x2<float>;
    }; // namespace Math
}; // namespace Gek
