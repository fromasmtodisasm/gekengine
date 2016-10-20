/// @file
/// @author Todd Zupan <toddzupan@gmail.com>
/// @version $Revision$
/// @section LICENSE
/// https://en.wikipedia.org/wiki/MIT_License
/// @section DESCRIPTION
/// Last Changed: $Date$
#pragma once

#include "GEK\Math\Float3.hpp"
#include <xmmintrin.h>

namespace Gek
{
    namespace Math
    {
        template <typename TYPE>
        struct Vector4
        {
        public:
            static const Vector4 Zero;
            static const Vector4 One;

        public:
            union
            {
                struct { TYPE x, y, z, w; };
                struct { TYPE data[4]; };
                struct { __m128 simd; };
            };

        public:
            inline Vector4(void)
            {
            }

            inline Vector4(TYPE value)
                : simd(_mm_set1_ps(value))
            {
            }

            inline Vector4(__m128 simd)
                : simd(simd)
            {
            }

            inline Vector4(const TYPE(&data)[4])
                : simd(_mm_loadu_ps(data))
            {
            }

            inline Vector4(const TYPE *data)
                : simd(_mm_loadu_ps(data))
            {
            }

            inline Vector4(const Vector4 &vector)
                : simd(vector.simd)
            {
            }

            inline Vector4(TYPE x, TYPE y, TYPE z, TYPE w)
                : simd(_mm_setr_ps(x, y, z, w))
            {
            }

            inline void set(TYPE x, TYPE y, TYPE z, TYPE w)
            {
                simd = _mm_setr_ps(x, y, z, w);
            }

            inline void set(TYPE value)
            {
                this->x = value;
                this->y = value;
                this->z = value;
                this->w = value;
            }

            TYPE getLengthSquared(void) const
            {
                return this->dot(*this);
            }

            TYPE getLength(void) const
            {
                return std::sqrt(getLengthSquared());
            }

            TYPE getDistance(const Vector4 &vector) const
            {
                return (vector - (*this)).getLength();
            }

            Vector4 getNormal(void) const
            {
                return _mm_mul_ps(simd, _mm_rcp_ps(_mm_set1_ps(getLength())));
            }

            TYPE dot(const Vector4 &vector) const
            {
                return ((x * vector.x) + (y * vector.y) + (z * vector.z) + (w * vector.w));
            }

            Vector4 lerp(const Vector4 &vector, TYPE factor) const
            {
                return Math::lerp((*this), vector, factor);
            }

            void normalize(void)
            {
                (*this) = getNormal();
            }

            bool operator < (const Vector4 &vector) const
            {
                if (x >= vector.x) return false;
                if (y >= vector.y) return false;
                if (z >= vector.z) return false;
                if (w >= vector.w) return false;
                return true;
            }

            bool operator > (const Vector4 &vector) const
            {
                if (x <= vector.x) return false;
                if (y <= vector.y) return false;
                if (z <= vector.z) return false;
                if (w <= vector.w) return false;
                return true;
            }

            bool operator <= (const Vector4 &vector) const
            {
                if (x > vector.x) return false;
                if (y > vector.y) return false;
                if (z > vector.z) return false;
                if (w > vector.w) return false;
                return true;
            }

            bool operator >= (const Vector4 &vector) const
            {
                if (x < vector.x) return false;
                if (y < vector.y) return false;
                if (z < vector.z) return false;
                if (w < vector.w) return false;
                return true;
            }

            bool operator == (const Vector4 &vector) const
            {
                if (x != vector.x) return false;
                if (y != vector.y) return false;
                if (z != vector.z) return false;
                if (w != vector.w) return false;
                return true;
            }

            bool operator != (const Vector4 &vector) const
            {
                if (x != vector.x) return true;
                if (y != vector.y) return true;
                if (z != vector.z) return true;
                if (w != vector.w) return true;
                return false;
            }

            inline TYPE operator [] (int axis) const
            {
                return data[axis];
            }

            inline TYPE &operator [] (int axis)
            {
                return data[axis];
            }

            inline operator const TYPE *() const
            {
                return data;
            }

            inline operator TYPE *()
            {
                return data;
            }

            // vector operations
            inline Vector4 &operator = (const Vector4 &vector)
            {
                simd = vector.simd;
                return (*this);
            }

            inline void operator -= (const Vector4 &vector)
            {
                simd = _mm_sub_ps(simd, vector.simd);
            }

            inline void operator += (const Vector4 &vector)
            {
                simd = _mm_add_ps(simd, vector.simd);
            }

            inline void operator /= (const Vector4 &vector)
            {
                simd = _mm_div_ps(simd, vector.simd);
            }

            inline void operator *= (const Vector4 &vector)
            {
                simd = _mm_mul_ps(simd, vector.simd);
            }

            inline Vector4 operator - (const Vector4 &vector) const
            {
                return _mm_sub_ps(simd, vector.simd);
            }

            inline Vector4 operator + (const Vector4 &vector) const
            {
                return _mm_add_ps(simd, vector.simd);
            }

            inline Vector4 operator / (const Vector4 &vector) const
            {
                return _mm_div_ps(simd, vector.simd);
            }

            inline Vector4 operator * (const Vector4 &vector) const
            {
                return _mm_mul_ps(simd, vector.simd);
            }

            // scalar operations
            inline Vector4 &operator = (TYPE scalar)
            {
                simd = _mm_set1_ps(scalar);
                return (*this);
            }

            inline void operator -= (TYPE scalar)
            {
                simd = _mm_sub_ps(simd, _mm_set1_ps(scalar));
            }

            inline void operator += (TYPE scalar)
            {
                simd = _mm_add_ps(simd, _mm_set1_ps(scalar));
            }

            inline void operator /= (TYPE scalar)
            {
                simd = _mm_div_ps(simd, _mm_set1_ps(scalar));
            }

            inline void operator *= (TYPE scalar)
            {
                simd = _mm_mul_ps(simd, _mm_set1_ps(scalar));
            }

            inline Vector4 operator - (TYPE scalar) const
            {
                return _mm_sub_ps(simd, _mm_set1_ps(scalar));
            }

            inline Vector4 operator + (TYPE scalar) const
            {
                return _mm_add_ps(simd, _mm_set1_ps(scalar));
            }

            inline Vector4 operator / (TYPE scalar) const
            {
                return _mm_div_ps(simd, _mm_set1_ps(scalar));
            }

            inline Vector4 operator * (TYPE scalar) const
            {
                return _mm_mul_ps(simd, _mm_set1_ps(scalar));
            }
        };

        template <typename TYPE>
        inline Vector4<TYPE> operator - (const Vector4<TYPE> &vector)
        {
            return _mm_sub_ps(_mm_set1_ps(0.0f), vector.simd);
        }

        template <typename TYPE>
        inline Vector4<TYPE> operator + (TYPE scalar, const Vector4<TYPE> &vector)
        {
            return _mm_add_ps(_mm_set1_ps(scalar), vector.simd);
        }

        template <typename TYPE>
        inline Vector4<TYPE> operator - (TYPE scalar, const Vector4<TYPE> &vector)
        {
            return _mm_sub_ps(_mm_set1_ps(scalar), vector.simd);
        }

        template <typename TYPE>
        inline Vector4<TYPE> operator * (TYPE scalar, const Vector4<TYPE> &vector)
        {
            return _mm_mul_ps(_mm_set1_ps(scalar), vector.simd);
        }

        template <typename TYPE>
        inline Vector4<TYPE> operator / (TYPE scalar, const Vector4<TYPE> &vector)
        {
            return _mm_div_ps(_mm_set1_ps(scalar), vector.simd);
        }

        using Float4 = Vector4<float>;
        using Int4 = Vector4<int>;
        using UInt4 = Vector4<unsigned int>;
    }; // namespace Math
}; // namespace Gek
