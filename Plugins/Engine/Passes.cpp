#include "GEK/Engine/Renderer.hpp"
#include "Passes.hpp"

namespace Gek
{
    Map::Map(MapSource source, MapType type, BindType binding, ResourceHandle resource)
        : source(source)
        , type(type)
        , binding(binding)
        , resource(resource)
    {
    }

    ClearData::ClearData(ClearType type, const String &data)
        : type(type)
    {
        switch (type)
        {
        case ClearType::Float:
        case ClearType::Target:
            floats = data;
            break;
            
        case ClearType::UInt:
            integers[0] = integers[1] = integers[2] = integers[3] = data;
            break;
        };
    }

    String getFormatSemantic(Video::Format format)
    {
        switch (format)
        {
        case Video::Format::R32G32B32A32_FLOAT:
        case Video::Format::R16G16B16A16_FLOAT:
        case Video::Format::R16G16B16A16_UNORM:
        case Video::Format::R10G10B10A2_UNORM:
        case Video::Format::R8G8B8A8_UNORM:
        case Video::Format::R8G8B8A8_UNORM_SRGB:
        case Video::Format::R16G16B16A16_NORM:
        case Video::Format::R8G8B8A8_NORM:
            return L"float4";

        case Video::Format::R32G32B32_FLOAT:
        case Video::Format::R11G11B10_FLOAT:
            return L"float3";

        case Video::Format::R32G32_FLOAT:
        case Video::Format::R16G16_FLOAT:
        case Video::Format::R16G16_UNORM:
        case Video::Format::R8G8_UNORM:
        case Video::Format::R16G16_NORM:
        case Video::Format::R8G8_NORM:
            return L"float2";

        case Video::Format::R32_FLOAT:
        case Video::Format::R16_FLOAT:
        case Video::Format::R16_UNORM:
        case Video::Format::R8_UNORM:
        case Video::Format::R16_NORM:
        case Video::Format::R8_NORM:
        case Video::Format::D32_FLOAT_S8X24_UINT:
        case Video::Format::D24_UNORM_S8_UINT:
        case Video::Format::D32_FLOAT:
        case Video::Format::D16_UNORM:
            return L"float";

        case Video::Format::R32G32B32A32_UINT:
        case Video::Format::R16G16B16A16_UINT:
        case Video::Format::R10G10B10A2_UINT:
            return L"uint4";

        case Video::Format::R8G8B8A8_UINT:
        case Video::Format::R32G32B32_UINT:
            return L"uint3";

        case Video::Format::R32G32_UINT:
        case Video::Format::R16G16_UINT:
        case Video::Format::R8G8_UINT:
            return L"uint2";

        case Video::Format::R32_UINT:
        case Video::Format::R16_UINT:
        case Video::Format::R8_UINT:
            return L"uint";

        case Video::Format::R32G32B32A32_INT:
        case Video::Format::R16G16B16A16_INT:
        case Video::Format::R8G8B8A8_INT:
            return L"int4";

        case Video::Format::R32G32B32_INT:
            return L"int3";

        case Video::Format::R32G32_INT:
        case Video::Format::R16G16_INT:
        case Video::Format::R8G8_INT:
            return L"int2";

        case Video::Format::R32_INT:
        case Video::Format::R16_INT:
        case Video::Format::R8_INT:
            return L"int";
        };

        return L"";
    }

    ClearType getClearType(const String &clearType)
    {
        if (clearType.compareNoCase(L"Target") == 0) return ClearType::Target;
        else if (clearType.compareNoCase(L"Float") == 0) return ClearType::Float;
        else if (clearType.compareNoCase(L"UInt") == 0) return ClearType::UInt;
        return ClearType::Unknown;
    }

    MapType getMapType(const String &mapType)
    {
        if (mapType.compareNoCase(L"Texture1D") == 0) return MapType::Texture1D;
        else if (mapType.compareNoCase(L"Texture2D") == 0) return MapType::Texture2D;
        else if (mapType.compareNoCase(L"Texture2DMS") == 0) return MapType::Texture2DMS;
        else if (mapType.compareNoCase(L"Texture3D") == 0) return MapType::Texture3D;
        else if (mapType.compareNoCase(L"Buffer") == 0) return MapType::Buffer;
        else if (mapType.compareNoCase(L"ByteAddressBuffer") == 0) return MapType::ByteAddressBuffer;
        return MapType::Unknown;
    }

    String getMapType(MapType mapType)
    {
        switch (mapType)
        {
        case MapType::Texture1D:            return L"Texture1D";
        case MapType::Texture2D:            return L"Texture2D";
        case MapType::Texture2DMS:          return L"Texture2DMS";
        case MapType::TextureCube:          return L"TextureCube";
        case MapType::Texture3D:            return L"Texture3D";
        case MapType::Buffer:               return L"Buffer";
        case MapType::ByteAddressBuffer:    return L"ByteAddressBuffer";
        };

        return L"void";
    }

    BindType getBindType(const String &bindType)
    {
        if (bindType.compareNoCase(L"Float") == 0) return BindType::Float;
        else if (bindType.compareNoCase(L"Float2") == 0) return BindType::Float2;
        else if (bindType.compareNoCase(L"Float3") == 0) return BindType::Float3;
        else if (bindType.compareNoCase(L"Float4") == 0) return BindType::Float4;

        else if (bindType.compareNoCase(L"Half") == 0) return BindType::Half;
        else if (bindType.compareNoCase(L"Half2") == 0) return BindType::Half2;
        else if (bindType.compareNoCase(L"Half3") == 0) return BindType::Half3;
        else if (bindType.compareNoCase(L"Half4") == 0) return BindType::Half4;

        else if (bindType.compareNoCase(L"Int") == 0) return BindType::Int;
        else if (bindType.compareNoCase(L"Int2") == 0) return BindType::Int2;
        else if (bindType.compareNoCase(L"Int3") == 0) return BindType::Int3;
        else if (bindType.compareNoCase(L"Int4") == 0) return BindType::Int4;

        else if (bindType.compareNoCase(L"UInt") == 0) return BindType::UInt;
        else if (bindType.compareNoCase(L"UInt2") == 0) return BindType::UInt2;
        else if (bindType.compareNoCase(L"UInt3") == 0) return BindType::UInt3;
        else if (bindType.compareNoCase(L"UInt4") == 0) return BindType::UInt4;

        else if (bindType.compareNoCase(L"Bool") == 0) return BindType::Bool;

        return BindType::Unknown;
    }

    String getBindType(BindType bindType)
    {
        switch (bindType)
        {
        case BindType::Float:       return L"float";
        case BindType::Float2:      return L"float2";
        case BindType::Float3:      return L"float3";
        case BindType::Float4:      return L"float4";

        case BindType::Half:        return L"half";
        case BindType::Half2:       return L"half2";
        case BindType::Half3:       return L"half3";
        case BindType::Half4:       return L"half4";

        case BindType::Int:         return L"int";
        case BindType::Int2:        return L"int2";
        case BindType::Int3:        return L"int3";
        case BindType::Int4:        return L"int4";

        case BindType::UInt:        return L"uint";
        case BindType::UInt2:       return L"uint2";
        case BindType::UInt3:       return L"uint3";
        case BindType::UInt4:       return L"uint4";

        case BindType::Bool:        return L"bool";
        };

        return L"void";
    }

    const BindType getBindType(Video::Format format)
    {
        switch (format)
        {
        case Video::Format::R32G32B32A32_FLOAT:
        case Video::Format::R16G16B16A16_FLOAT:
        case Video::Format::R16G16B16A16_UNORM:
        case Video::Format::R10G10B10A2_UNORM:
        case Video::Format::R8G8B8A8_UNORM:
        case Video::Format::R8G8B8A8_UNORM_SRGB:
        case Video::Format::R16G16B16A16_NORM:
        case Video::Format::R8G8B8A8_NORM:
            return BindType::Float4;

        case Video::Format::R32G32B32_FLOAT:
        case Video::Format::R11G11B10_FLOAT:
            return BindType::Float3;

        case Video::Format::R32G32_FLOAT:
        case Video::Format::R16G16_FLOAT:
        case Video::Format::R16G16_UNORM:
        case Video::Format::R8G8_UNORM:
        case Video::Format::R16G16_NORM:
        case Video::Format::R8G8_NORM:
            return BindType::Float2;

        case Video::Format::R32_FLOAT:
        case Video::Format::R16_FLOAT:
        case Video::Format::R16_UNORM:
        case Video::Format::R8_UNORM:
        case Video::Format::R16_NORM:
        case Video::Format::R8_NORM:
            return BindType::Float;

        case Video::Format::R32G32B32A32_UINT:
        case Video::Format::R16G16B16A16_UINT:
        case Video::Format::R10G10B10A2_UINT:
        case Video::Format::R8G8B8A8_UINT:
            return BindType::UInt4;

        case Video::Format::R32G32B32_UINT:
        case Video::Format::R32G32B32_INT:
            return BindType::UInt3;

        case Video::Format::R32G32_UINT:
        case Video::Format::R16G16_UINT:
        case Video::Format::R8G8_UINT:
            return BindType::UInt2;

        case Video::Format::R32_UINT:
        case Video::Format::R16_UINT:
        case Video::Format::R8_UINT:
            return BindType::UInt;

        case Video::Format::R32G32B32A32_INT:
        case Video::Format::R16G16B16A16_INT:
        case Video::Format::R8G8B8A8_INT:
            return BindType::Int4;

        case Video::Format::R32G32_INT:
        case Video::Format::R16G16_INT:
        case Video::Format::R8G8_INT:
            return BindType::Int2;

        case Video::Format::R32_INT:
        case Video::Format::R16_INT:
        case Video::Format::R8_INT:
            return BindType::Int;
        };

        return BindType::Unknown;
    }

    const Video::Format getBindFormat(BindType bindType)
    {
        switch (bindType)
        {
        case BindType::Float4:
            return Video::Format::R32G32B32A32_FLOAT;

        case BindType::Float3:
            return Video::Format::R32G32B32_FLOAT;

        case BindType::Float2:
            return Video::Format::R32G32_FLOAT;

        case BindType::Float:
            return Video::Format::R32_FLOAT;

        case BindType::UInt4:
            return Video::Format::R32G32B32A32_UINT;

        case BindType::UInt3:
            return Video::Format::R32G32B32_UINT;

        case BindType::UInt2:
            return Video::Format::R32G32_UINT;

        case BindType::UInt:
            return Video::Format::R32_UINT;

        case BindType::Int4:
            return Video::Format::R32G32B32A32_INT;

        case BindType::Int2:
            return Video::Format::R32G32_INT;

        case BindType::Int:
            return Video::Format::R32_INT;
        };

        return Video::Format::Unknown;
    }

    uint32_t getTextureLoadFlags(const String &loadFlags)
    {
        uint32_t flags = 0;
        int position = 0;
        std::vector<String> flagList(loadFlags.split(L','));
        for (auto &flag : flagList)
        {
            if (flag.compareNoCase(L"sRGB") == 0)
            {
                flags |= Video::TextureLoadFlags::sRGB;
            }
        }

        return flags;
    }

    uint32_t getTextureFlags(const String &createFlags)
    {
        uint32_t flags = 0;
        int position = 0;
        std::vector<String> flagList(createFlags.split(L','));
        for (auto &flag : flagList)
        {
            flag.trim();
            if (flag.compareNoCase(L"target") == 0)
            {
                flags |= Video::Texture::Description::Flags::RenderTarget;
            }
            else if (flag.compareNoCase(L"depth") == 0)
            {
                flags |= Video::Texture::Description::Flags::DepthTarget;
            }
            else if (flag.compareNoCase(L"unorderedaccess") == 0)
            {
                flags |= Video::Texture::Description::Flags::UnorderedAccess;
            }
        }

        return (flags | Video::Texture::Description::Flags::Resource);
    }

    uint32_t getBufferFlags(const String &createFlags)
    {
        uint32_t flags = 0;
        int position = 0;
        std::vector<String> flagList(createFlags.split(L','));
        for (auto &flag : flagList)
        {
            flag.trim();
            if (flag.compareNoCase(L"unorderedaccess") == 0)
            {
                flags |= Video::Buffer::Description::Flags::UnorderedAccess;
            }
            else if (flag.compareNoCase(L"counter") == 0)
            {
                flags |= Video::Buffer::Description::Flags::Counter;
            }
        }

        return (flags | Video::Buffer::Description::Flags::Resource);
    }

    std::unordered_map<String, String> getAliasedMap(const JSON::Object &parent, const wchar_t *name)
    {
        std::unordered_map<String, String> aliasedMap;
        if (parent.has_member(name))
        {
            auto &object = parent.get(name);
            if (object.is_array())
            {
                uint32_t mapCount = object.size();
                for (auto &element : object.elements())
                {
                    if (element.is_string())
                    {
                        String name(element.as_string());
                        aliasedMap[name] = name;
                    }
                    else if (element.is_object() && !element.empty())
                    {
                        auto &member = element.begin_members();
                        String name(member->name());
                        String value(member->value().as_string());
                        aliasedMap[name] = value;
                    }
                    else
                    {
                    }
                }
            }
            else
            {
            }
        }

        return aliasedMap;
    }

    Video::Format getElementFormat(const String &format)
    {
        if (format.compareNoCase(L"float") == 0) return Video::Format::R32_FLOAT;
        else if (format.compareNoCase(L"float2") == 0) return Video::Format::R32G32_FLOAT;
        else if (format.compareNoCase(L"float3") == 0) return Video::Format::R32G32B32_FLOAT;
        else if (format.compareNoCase(L"float4") == 0) return Video::Format::R32G32B32A32_FLOAT;
        else if (format.compareNoCase(L"int") == 0) return Video::Format::R32_INT;
        else if (format.compareNoCase(L"int2") == 0) return Video::Format::R32G32_INT;
        else if (format.compareNoCase(L"int3") == 0) return Video::Format::R32G32B32_INT;
        else if (format.compareNoCase(L"int4") == 0) return Video::Format::R32G32B32A32_INT;
        else if (format.compareNoCase(L"uint") == 0) return Video::Format::R32_UINT;
        else if (format.compareNoCase(L"uint2") == 0) return Video::Format::R32G32_UINT;
        else if (format.compareNoCase(L"uint3") == 0) return Video::Format::R32G32B32_UINT;
        else if (format.compareNoCase(L"uint4") == 0) return Video::Format::R32G32B32A32_UINT;
        return Video::Format::Unknown;
    }

    Video::InputElement::Source getElementSource(const String &elementSource)
    {
        if (elementSource.compareNoCase(L"instance") == 0) return Video::InputElement::Source::Instance;
        else return Video::InputElement::Source::Vertex;
    }

    Video::InputElement::Semantic getElementSemantic(const String &semantic)
    {
        if (semantic.compareNoCase(L"Position") == 0) return Video::InputElement::Semantic::Position;
        else if (semantic.compareNoCase(L"Tangent") == 0) return Video::InputElement::Semantic::Tangent;
        else if (semantic.compareNoCase(L"BiTangent") == 0) return Video::InputElement::Semantic::BiTangent;
        else if (semantic.compareNoCase(L"Normal") == 0) return Video::InputElement::Semantic::Normal;
        else if (semantic.compareNoCase(L"Color") == 0) return Video::InputElement::Semantic::Color;
        else return Video::InputElement::Semantic::TexCoord;
    }
}; // namespace Gek