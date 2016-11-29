/// @file
/// @author Todd Zupan <toddzupan@gmail.com>
/// @version $Revision$
/// @section LICENSE
/// https://en.wikipedia.org/wiki/MIT_License
/// @section DESCRIPTION
/// Last Changed: $Date$
#pragma once

#include "GEK/Utility/String.hpp"
#include <jsoncons/json.hpp>

namespace jsoncons
{
    template<class Json>
    struct json_type_traits<Json, float>
    {
        static float as(const Json &object)
        {
            return float(object.as_double());
        }

        static Json to_json(const float &value)
        {
            return double(value);
        }
    };

    template<typename TYPE, class Json>
    struct json_type_traits<Json, Gek::Math::Vector2<TYPE>>
    {
        static Json to_json(const Gek::Math::Vector2<TYPE> &value)
        {
            return Gek::String::Format(L"%v", value);
        }
    };

    template<typename TYPE, class Json>
    struct json_type_traits<Json, Gek::Math::Vector3<TYPE>>
    {
        static Json to_json(const Gek::Math::Vector3<TYPE> &value)
        {
            return Gek::String::Format(L"%v", value);
        }
    };

    template<typename TYPE, class Json>
    struct json_type_traits<Json, Gek::Math::Vector4<TYPE>>
    {
        static Json to_json(const Gek::Math::Vector4<TYPE> &value)
        {
            return Gek::String::Format(L"%v", value);
        }
    };

    template<class Json>
    struct json_type_traits<Json, Gek::Math::Float4>
    {
        static Json to_json(const Gek::Math::Float4 &value)
        {
            return Gek::String::Format(L"%v", value);
        }
    };

    template<class Json>
    struct json_type_traits<Json, Gek::Math::Quaternion>
    {
        static Json to_json(const Gek::Math::Quaternion &value)
        {
            return Gek::String::Format(L"%v", value);
        }
    };
};

namespace Gek
{
    namespace JSON
    {
        using Object = jsoncons::wjson;
        using Member = Object::member_type;

        Object Load(const wchar_t *fileName);
        void Save(const wchar_t *fileName, const Object &object);
	}; // namespace JSON
}; // namespace Gek