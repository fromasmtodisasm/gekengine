#pragma once

#include "GEK\Math\Float2.h"
#include "GEK\Math\Float3.h"
#include "GEK\Math\Float4.h"
#include "GEK\Math\Color.h"
#include "GEK\Math\Quaternion.h"
#include "GEK\Utility\Trace.h"
#include <functional>
#include <sstream>
#include <codecvt>
#include <vector>
#include <cctype>

namespace Gek
{
    template<class ELEMENT, class TRAITS = std::char_traits<ELEMENT>, class ALLOCATOR = std::allocator<ELEMENT>>
    class BaseString : public std::basic_string<ELEMENT, TRAITS, ALLOCATOR>
    {
    private:
        template <typename TYPE>
        struct traits;

        template <>
        struct traits<char>
        {
            static void convert(std::basic_string<wchar_t> &result, const char *input)
            {
                std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> convert;
                result.assign(convert.from_bytes(input));
            }
        };

        template <>
        struct traits<wchar_t>
        {
            static void convert(std::basic_string<char> &result, const wchar_t *input)
            {
                std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> convert;
                result.assign(convert.to_bytes(input));
            }
        };

        template <typename DESTINATION, typename SOURCE>
        struct converter
        {
            static void convert(std::basic_string<DESTINATION> &result, const SOURCE *string)
            {
               traits<SOURCE>::convert(result, string);
            }
        };

        template <typename DESTINATION>
        struct converter<DESTINATION, DESTINATION>
        {
            static void convert(std::basic_string<DESTINATION> &result, const DESTINATION *string)
            {
                result.assign(string);
            }
        };

        struct MustMatch
        {
            ELEMENT character;
            MustMatch(ELEMENT character)
                : character(character)
            {
            }

            template <typename STREAM>
            friend STREAM &operator >> (STREAM &stream, const MustMatch &match)
            {
                ELEMENT next;
                stream.get(next);
                if (next != match.character)
                {
                    stream.setstate(std::ios::failbit);
                }

                return stream;
            }
        };

    public:
        BaseString(void)
        {
        }

        BaseString(ELEMENT character, size_t length)
            : basic_string(length, character)
        {
        }

        BaseString(const ELEMENT *string)
        {
            if (string)
            {
                assign(string);
            }
        }

        template<typename TYPE, typename... ARGUMENTS>
        BaseString(const ELEMENT *formatting, const TYPE &value, ARGUMENTS... arguments)
        {
            if (formatting)
            {
                format(formatting, value, arguments...);
            }
        }

        template<typename CONVERSION>
        BaseString(const CONVERSION *string)
        {
            if (string)
            {
                converter<ELEMENT, CONVERSION>::convert(*this, string);
            }
        }

        template<typename CONVERSION>
        BaseString(const basic_string<CONVERSION> &string)
        {
            if (!string.empty())
            {
                converter<ELEMENT, CONVERSION>::convert(*this, string.c_str());
            }
        }

        template<typename CONVERSION>
        BaseString(const BaseString<CONVERSION> &string)
        {
            if (!string.empty())
            {
                converter<ELEMENT, CONVERSION>::convert(*this, string.c_str());
            }
        }

        BaseString subString(size_t position = 0, size_t length = std::string::npos) const
        {
            return BaseString(substr(position, length));
        }

        bool replace(const BaseString &from, const BaseString &to)
        {
            bool replaced = false;
            size_t position = 0;
            while ((position = find(from, position)) != std::string::npos)
            {
                basic_string::replace(position, from.size(), to);
                position += to.length();
                replaced = true;
            };

            return replaced;
        }

        void trimLeft(void)
        {
            erase(begin(), std::find_if(begin(), end(), [](ELEMENT ch) { return !std::isspace<ELEMENT>(ch, std::locale::classic()); }));
        }

        void trimRight(void)
        {
            erase(std::find_if(rbegin(), rend(), [](ELEMENT ch) { return !std::isspace<ELEMENT>(ch, std::locale::classic()); }).base(), end());
        }

        void trim(void)
        {
            trimLeft();
            trimRight();
        }

        void toLower(void)
        {
            std::transform(begin(), end(), begin(), std::tolower<ELEMENT>);
        }

        void toUpper(void)
        {
            std::transform(begin(), end(), begin(), std::toupper<ELEMENT>);
        }

        BaseString<ELEMENT> getLower(void) const
        {
            BaseString transformed;
            std::transform(begin(), end(), std::back_inserter(transformed), ::tolower);
            return transformed;
        }

        BaseString<ELEMENT> getUpper(void) const
        {
            BaseString transformed;
            std::transform(begin(), end(), std::back_inserter(transformed), ::toupper);
            return transformed;
        }

        std::vector<BaseString<ELEMENT>> split(ELEMENT delimiter, bool clearSpaces = true) const
        {
            BaseString current;
            std::vector<BaseString> tokens;
            std::basic_stringstream<ELEMENT, TRAITS, ALLOCATOR> stream(c_str());
            while (std::getline(stream, current, delimiter))
            {
                if (clearSpaces)
                {
                    current.trim();
                }

                tokens.push_back(current);
            };

            return tokens;
        }

        int compareNoCase(const ELEMENT *string) const
        {
            return std::equal(begin(), end(), string, [](const ELEMENT &left, const ELEMENT &right) -> bool
            {
                return (std::toupper(left) == std::toupper(right));
            }) ? 0 : 1;
        }

        void format(const ELEMENT *formatting)
        {
            append(formatting);
        }

        template<typename TYPE, typename... ARGUMENTS>
        void format(const ELEMENT *formatting, const TYPE &value, ARGUMENTS... arguments)
        {
            while (formatting && *formatting)
            {
                ELEMENT currentCharacter = *formatting++;
                if (currentCharacter == '%' && formatting && *formatting)
                {
                    ELEMENT nextCharacter = *formatting++;
                    if (nextCharacter == '%')
                    {
                        // %%
                        (*this) += nextCharacter;
                    }
                    else if (nextCharacter == 'v')
                    {
                        // %v
                        (*this) += value;
                        format(formatting, arguments...);
                        return;
                    }
                    else
                    {
                        // %(other)
                        (*this) += currentCharacter;
                        (*this) += nextCharacter;
                    }
                }
                else
                {
                    (*this) += currentCharacter;
                }
            };
        }

        BaseString<ELEMENT> &operator = (const ELEMENT &value)
        {
            std::basic_stringstream<ELEMENT, std::char_traits<ELEMENT>, std::allocator<ELEMENT>> stream;
            stream << value;
            return static_cast<BaseString &>(assign(stream.str()));
        }

        BaseString<ELEMENT> &operator = (const bool &value)
        {
            std::basic_stringstream<ELEMENT, std::char_traits<ELEMENT>, std::allocator<ELEMENT>> stream;
            stream << std::boolalpha << value;
            return static_cast<BaseString &>(assign(stream.str()));
        }

        BaseString<ELEMENT> &operator = (const float &value)
        {
            std::basic_stringstream<ELEMENT, std::char_traits<ELEMENT>, std::allocator<ELEMENT>> stream;
            stream << std::showpoint << value;
            return static_cast<BaseString &>(assign(stream.str()));
        }

        BaseString<ELEMENT> &operator = (const long &value)
        {
            std::basic_stringstream<ELEMENT, std::char_traits<ELEMENT>, std::allocator<ELEMENT>> stream;
            stream << std::uppercase << std::setfill(ELEMENT('0')) << std::setw(8) << std::hex << value;
            return static_cast<BaseString &>(assign(stream.str()));
        }

        BaseString<ELEMENT> &operator = (const unsigned long &value)
        {
            std::basic_stringstream<ELEMENT, std::char_traits<ELEMENT>, std::allocator<ELEMENT>> stream;
            stream << std::uppercase << std::setfill(ELEMENT('0')) << std::setw(8) << std::hex << value;
            return static_cast<BaseString &>(assign(stream.str()));
        }

        BaseString<ELEMENT> &operator = (const Math::Float2 &value)
        {
            std::basic_stringstream<ELEMENT, std::char_traits<ELEMENT>, std::allocator<ELEMENT>> stream;
            stream << '(' << value.x << ',' << value.y << ')';
            return static_cast<BaseString &>(assign(stream.str()));
        }

        BaseString<ELEMENT> &operator = (const Math::Float3 &value)
        {
            std::basic_stringstream<ELEMENT, std::char_traits<ELEMENT>, std::allocator<ELEMENT>> stream;
            stream << '(' << value.x << ',' << value.y << ',' << value.z << ')';
            return static_cast<BaseString &>(assign(stream.str()));
        }

        BaseString<ELEMENT> &operator = (const Math::Float4 &value)
        {
            std::basic_stringstream<ELEMENT, std::char_traits<ELEMENT>, std::allocator<ELEMENT>> stream;
            stream << '(' << value.x << ',' << value.y << ',' << value.z << ',' << value.w << ')';
            return static_cast<BaseString &>(assign(stream.str()));
        }

        BaseString<ELEMENT> &operator = (const Math::Color &value)
        {
            std::basic_stringstream<ELEMENT, std::char_traits<ELEMENT>, std::allocator<ELEMENT>> stream;
            stream << '(' << value.r << ',' << value.g << ',' << value.b << ',' << value.a << ')';
            return static_cast<BaseString &>(assign(stream.str()));
        }

        BaseString<ELEMENT> &operator = (const Math::Quaternion &value)
        {
            std::basic_stringstream<ELEMENT, std::char_traits<ELEMENT>, std::allocator<ELEMENT>> stream;
            stream << '(' << value.x << ',' << value.y << ',' << value.z << ',' << value.w << ')';
            return static_cast<BaseString &>(assign(stream.str()));
        }

        BaseString<ELEMENT> &operator = (const ELEMENT *string)
        {
            if (string)
            {
                assign(string);
            }
            else
            {
                empty();
            }

            return (*this);
        }

        template <typename TYPE>
        BaseString<ELEMENT> &operator = (const TYPE &value)
        {
            std::basic_stringstream<ELEMENT, std::char_traits<ELEMENT>, std::allocator<ELEMENT>> stream;
            stream << value;
            return static_cast<BaseString &>(assign(stream.str()));
        }

        template <typename CONVERSION>
        BaseString<ELEMENT> &operator = (const CONVERSION *string)
        {
            if (string)
            {
                converter<ELEMENT, CONVERSION>::convert(*this, string);
            }
            else
            {
                empty();
            }

            return (*this);
        }

        template <typename CONVERSION>
        BaseString<ELEMENT> &operator = (const basic_string<CONVERSION> &string)
        {
            if (string.empty())
            {
                empty();
            }
            else
            {
                converter<ELEMENT, CONVERSION>::convert(*this, string.c_str());
            }

            return (*this);
        }

        template <typename CONVERSION>
        BaseString<ELEMENT> &operator = (const BaseString<CONVERSION> &string)
        {
            if (string.empty())
            {
                empty();
            }
            else
            {
                converter<ELEMENT, CONVERSION>::convert(*this, string.c_str());
            }

            return (*this);
        }

        void operator += (const ELEMENT &value)
        {
            std::basic_stringstream<ELEMENT, std::char_traits<ELEMENT>, std::allocator<ELEMENT>> stream;
            stream << value;
            append(stream.str());
        }

        void operator += (const bool &value)
        {
            std::basic_stringstream<ELEMENT, std::char_traits<ELEMENT>, std::allocator<ELEMENT>> stream;
            stream << std::boolalpha << value;
            append(stream.str());
        }

        void operator += (const float &value)
        {
            std::basic_stringstream<ELEMENT, std::char_traits<ELEMENT>, std::allocator<ELEMENT>> stream;
            stream << std::showpoint << value;
            append(stream.str());
        }

        void operator += (const long &value)
        {
            std::basic_stringstream<ELEMENT, std::char_traits<ELEMENT>, std::allocator<ELEMENT>> stream;
            stream << std::uppercase << std::setfill(ELEMENT('0')) << std::setw(8) << std::hex << value;
            append(stream.str());
        }

        void operator += (const unsigned long &value)
        {
            std::basic_stringstream<ELEMENT, std::char_traits<ELEMENT>, std::allocator<ELEMENT>> stream;
            stream << std::uppercase << std::setfill(ELEMENT('0')) << std::setw(8) << std::hex << value;
            append(stream.str());
        }

        void operator += (const Math::Float2 &value)
        {
            std::basic_stringstream<ELEMENT, std::char_traits<ELEMENT>, std::allocator<ELEMENT>> stream;
            stream << '(' << value.x << ',' << value.y << ')';
            append(stream.str());
        }

        void operator += (const Math::Float3 &value)
        {
            std::basic_stringstream<ELEMENT, std::char_traits<ELEMENT>, std::allocator<ELEMENT>> stream;
            stream << '(' << value.x << ',' << value.y << ',' << value.z << ')';
            append(stream.str());
        }

        void operator += (const Math::Float4 &value)
        {
            std::basic_stringstream<ELEMENT, std::char_traits<ELEMENT>, std::allocator<ELEMENT>> stream;
            stream << '(' << value.x << ',' << value.y << ',' << value.z << ',' << value.w << ')';
            append(stream.str());
        }

        void operator += (const Math::Color &value)
        {
            std::basic_stringstream<ELEMENT, std::char_traits<ELEMENT>, std::allocator<ELEMENT>> stream;
            stream << '(' << value.r << ',' << value.g << ',' << value.b << ',' << value.a << ')';
            append(stream.str());
        }

        void operator += (const Math::Quaternion &value)
        {
            std::basic_stringstream<ELEMENT, std::char_traits<ELEMENT>, std::allocator<ELEMENT>> stream;
            stream << '(' << value.x << ',' << value.y << ',' << value.z << ',' << value.w << ')';
            append(stream.str());
        }

        void operator += (const ELEMENT *string)
        {
            if (string)
            {
                append(string);
            }
        }

        template <typename TYPE>
        void operator += (const TYPE &value)
        {
            std::basic_stringstream<ELEMENT, std::char_traits<ELEMENT>, std::allocator<ELEMENT>> stream;
            stream << value;
            append(stream.str());
        }

        template <typename CONVERSION>
        void operator += (const CONVERSION *string)
        {
            append(BaseString(string));
        }

        template <typename CONVERSION>
        void operator += (const basic_string<CONVERSION> &string)
        {
            append(BaseString(string));
        }

        template <typename CONVERSION>
        void operator += (const BaseString<CONVERSION> &string)
        {
            append(BaseString(string));
        }

        operator bool() const
        {
            bool value;
            std::basic_stringstream<ELEMENT, std::char_traits<ELEMENT>, std::allocator<ELEMENT>> stream(*this);
            stream >> std::boolalpha >> value;
            return (stream.fail() ? false : value);
        }

        operator float () const
        {
            float value;
            std::basic_stringstream<ELEMENT, std::char_traits<ELEMENT>, std::allocator<ELEMENT>> stream(*this);
            stream >> value;
            return (stream.fail() ? 0.0f : value);
        }

        operator int32_t () const
        {
            int32_t value;
            std::basic_stringstream<ELEMENT, std::char_traits<ELEMENT>, std::allocator<ELEMENT>> stream(*this);
            stream >> value;
            return (stream.fail() ? 0 : value);
        }

        operator uint32_t () const
        {
            uint32_t value;
            std::basic_stringstream<ELEMENT, std::char_traits<ELEMENT>, std::allocator<ELEMENT>> stream(*this);
            stream >> value;
            return (stream.fail() ? 0 : value);
        }

        operator Math::Float2 () const
        {
            Math::Float2 value;
            std::basic_stringstream<ELEMENT, std::char_traits<ELEMENT>, std::allocator<ELEMENT>> stream(*this);
            stream >> MustMatch('(') >> value.x >> MustMatch(',') >> value.y >> MustMatch(')'); // ( X , Y )
            return (stream.fail() ? Math::Float2::Zero : value);
        }

        operator Math::Float3 () const
        {
            Math::Float3 value;
            std::basic_stringstream<ELEMENT, std::char_traits<ELEMENT>, std::allocator<ELEMENT>> stream(*this);
            stream >> MustMatch('(') >> value.x >> MustMatch(',') >> value.y >> MustMatch(',') >> value.z >> MustMatch(')'); // ( x , Y , Z )
            return (stream.fail() ? Math::Float3::Zero : value);
        }

        operator Math::Float4 () const
        {
            Math::Float4 value;
            std::basic_stringstream<ELEMENT, std::char_traits<ELEMENT>, std::allocator<ELEMENT>> stream(*this);
            stream >> MustMatch('(') >> value.x >> MustMatch(',') >> value.y >> MustMatch(',') >> value.z >> MustMatch(',') >> value.w >> MustMatch(')'); // ( X , Y , Z , W )
            return (stream.fail() ? Math::Float4::Zero : value);
        }

        operator Math::Color () const
        {
            Math::Color value;
            std::basic_stringstream<ELEMENT, std::char_traits<ELEMENT>, std::allocator<ELEMENT>> stream(*this);
            if (stream.peek() == '(')
            {
                stream >> MustMatch('(') >> value.r >> MustMatch(',') >> value.g >> MustMatch(',') >> value.b;
                switch (stream.peek())
                {
                case ')':
                    value.a = 1.0f;
                    stream >> MustMatch(')');
                    break;

                case ',':
                    stream >> MustMatch(',') >> value.a >> MustMatch(')');
                    break;
                };
            }
            else
            {
                stream >> value.r;
                value.set(value.r);
            }

            return (stream.fail() ? Math::Color::White : value);
        }

        operator Math::Quaternion () const
        {
            Math::Quaternion value;
            std::basic_stringstream<ELEMENT, std::char_traits<ELEMENT>, std::allocator<ELEMENT>> stream(*this);
            stream >> MustMatch('(') >> value.x >> MustMatch(',') >> value.y >> MustMatch(',') >> value.z;
            switch (stream.peek())
            {
            case ')':
                value.setEulerRotation(value.x, value.y, value.z);
                stream >> MustMatch(')');
                break;

            case ',':
                stream >> MustMatch(',') >> value.w >> MustMatch(')');
                break;
            };

            return (stream.fail() ? Math::Quaternion::Identity : value);
        }

        operator const ELEMENT * () const
        {
            return c_str();
        }

        operator std::basic_string<ELEMENT> & ()
        {
            return static_cast<basic_string &>(*this);
        }

        operator const std::basic_string<ELEMENT> & () const
        {
            return static_cast<const basic_string &>(*this);
        }
    };

    typedef BaseString<char> StringUTF8;
    typedef BaseString<wchar_t> String;
}; // namespace Gek

namespace std
{
    template <>
    struct hash<Gek::StringUTF8>
    {
        size_t operator()(const Gek::StringUTF8 &value) const
        {
            return hash<string>()(value);
        }
    };

    template <>
    struct hash<Gek::String>
    {
        size_t operator()(const Gek::String &value) const
        {
            return hash<wstring>()(value);
        }
    };
}; // namespace std