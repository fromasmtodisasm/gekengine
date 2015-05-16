#pragma once

#include <Math.h>

namespace Gek
{
    namespace Math
    {
        template <typename TYPE> struct BaseVector2;

        template <typename TYPE>
        struct BaseMatrix3x2
        {
        public:
            union
            {
                struct { TYPE data[16]; };
                struct { TYPE table[4][4]; };

                struct
                {
                    TYPE _11, _12;
                    TYPE _21, _22;
                    TYPE _31, _32;
                };

                struct
                {
                    BaseVector2<TYPE> rx;
                    BaseVector2<TYPE> ry;
                    BaseVector2<TYPE> translation;
                };
            };

        public:
            BaseMatrix3x2(void)
            {
                setIdentity();
            }

            BaseMatrix3x2(const BaseMatrix3x2<TYPE> &matrix)
            {
                memcpy(data, matrix.data, sizeof(data));
            }

            void setZero(void)
            {
                _11 = _12 = TYPE(0);
                _21 = _22 = TYPE(0);
                _31 = _32 = TYPE(0);
            }

            void setIdentity(void)
            {
                _11 = _22 = TYPE(1);
                _12 = TYPE(0);
                _21 = TYPE(0);
                _31 = _32 = TYPE(0);
            }

            void setScaling(TYPE scalar)
            {
                _11 = scalar;
                _22 = scalar;
            }

            void setScaling(const BaseVector2<TYPE> &vector)
            {
                _11 = vector.x;
                _22 = vector.y;
            }

            void setRotation(float radians)
            {
                table[0][0] = cos(radians);
                table[1][0] = sin(radians);
                table[2][0] = TYPE(0);

                table[0][1] = -sin(radians);
                table[1][1] = cos(radians);
                table[2][1] = TYPE(0);
            }

            BaseVector2<TYPE> getScaling(void) const
            {
                return BaseVector2<TYPE>(_11, _22);
            }

            void operator *= (const BaseMatrix3x2<TYPE> &matrix)
            {
                (*this) = ((*this) * matrix);
            }

            BaseMatrix3x2<TYPE> operator * (const BaseMatrix3x2<TYPE> &matrix) const
            {
                return BaseMatrix3x2<TYPE>(_11 * matrix._11 + _12 * matrix._21,
                                           _11 * matrix._12 + _12 * matrix._22,
                                           _21 * matrix._11 + _22 * matrix._21,
                                           _21 * matrix._12 + _22 * matrix._22,
                                           _31 * matrix._11 + _32 * matrix._21 + matrix._31,
                                           _31 * matrix._12 + _32 * matrix._22 + matrix._32);
            }

            BaseMatrix3x2<TYPE> operator = (const BaseMatrix3x2<TYPE> &matrix)
            {
                memcpy(data, matrix.data, sizeof(data));
                return (*this);
            }
        };

        typedef BaseMatrix3x2<float> Float3x2;
    }; // namespace Math
}; // namespace Gek