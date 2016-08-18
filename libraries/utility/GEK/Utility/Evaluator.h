#pragma once

#include "GEK\Math\Float2.h"
#include "GEK\Math\Float3.h"
#include "GEK\Math\Float4.h"
#include "GEK\Math\Color.h"
#include "GEK\Math\Quaternion.h"
#include "GEK\Utility\String.h"

namespace Gek
{
    namespace Evaluator
    {
        void get(const String &expression, int32_t &result, int32_t defaultValue = 0);
        void get(const String &expression, uint32_t &result, uint32_t defaultValue = 0);
        void get(const String &expression, float &result, float defaultValue = 0);
        void get(const String &expression, Math::Float2 &result, const Math::Float2 &defaultValue = Math::Float2::Zero);
        void get(const String &expression, Math::Float3 &result, const Math::Float3 &defaultValue = Math::Float3::Zero);
        void get(const String &expression, Math::Float4 &result, const Math::Float4 &defaultValue = Math::Float4::Zero);
        void get(const String &expression, Math::Color &result, const Math::Color &defaultValue = Math::Color::White);
        void get(const String &expression, Math::Quaternion &result, const Math::Quaternion &defaultValue = Math::Quaternion::Identity);
        void get(const String &expression, String &result);

        template <typename TYPE>
        TYPE get(const String &expression)
        {
            TYPE value;
            get(expression, value);
            return value;
        }
    }; // namespace Evaluator
}; // namespace Gek
