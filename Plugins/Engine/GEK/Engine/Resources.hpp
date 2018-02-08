/// @file
/// @author Todd Zupan <toddzupan@gmail.com>
/// @version $Revision: 1d50799f92d32e762c5d618bca16d2fe7fbeb872 $
/// @section LICENSE
/// https://en.wikipedia.org/wiki/MIT_License
/// @section DESCRIPTION
/// Last Changed: $Date:   Fri Oct 21 04:24:02 2016 +0000 $
#pragma once

#include "GEK/Math/Vector4.hpp"
#include "GEK/Utility/Context.hpp"
#include "GEK/System/VideoDevice.hpp"
#include "GEK/API/Resources.hpp"
#include "GEK/Engine/Shader.hpp"

namespace Gek
{
    namespace Engine
    {
        GEK_PREDECLARE(Visual);
        GEK_PREDECLARE(Filter);
        GEK_PREDECLARE(Material);

        GEK_INTERFACE(Resources)
            : virtual public Plugin::Resources
        {
            virtual ~Resources(void) = default;
        
            virtual void clear(void) = 0;
            virtual void reload(void) = 0;

            virtual ShaderHandle getMaterialShader(MaterialHandle material) const = 0;
            virtual ResourceHandle getResourceHandle(std::string_view resourceName) const = 0;

            virtual ShaderHandle const getShader(std::string_view shaderName, MaterialHandle materialHandle = MaterialHandle()) = 0;
            virtual Shader * const getShader(ShaderHandle handle) const = 0;
            virtual Filter * const getFilter(std::string_view filterName) = 0;

            virtual Video::Texture::Description const * const getTextureDescription(ResourceHandle resourceHandle) const = 0;
            virtual Video::Buffer::Description const * const getBufferDescription(ResourceHandle resourceHandle) const = 0;
            virtual Video::Object * const getResource(ResourceHandle resourceHandle) const = 0;

            virtual Video::Program * getProgram(Video::Program::Type type, std::string_view name, std::string_view entryFunction, std::string_view engineData = String::Empty) = 0;
            virtual ProgramHandle loadProgram(Video::Program::Type type, std::string_view name, std::string_view entryFunction, std::string_view engineData = String::Empty) = 0;

            virtual RenderStateHandle createRenderState(Video::RenderState::Description const &renderState) = 0;
            virtual DepthStateHandle createDepthState(Video::DepthState::Description const &depthState) = 0;
            virtual BlendStateHandle createBlendState(Video::BlendState::Description const &blendState) = 0;

            virtual void generateMipMaps(Video::Device::Context *videoContext, ResourceHandle resourceHandle) = 0;
            virtual void resolveSamples(Video::Device::Context *videoContext, ResourceHandle destinationHandle, ResourceHandle sourceHandle) = 0;
            virtual void copyResource(ResourceHandle destinationHandle, ResourceHandle sourceHandle) = 0;

            virtual void clearUnorderedAccess(Video::Device::Context *videoContext, ResourceHandle resourceHandle, Math::Float4 const &value) = 0;
            virtual void clearUnorderedAccess(Video::Device::Context *videoContext, ResourceHandle resourceHandle, Math::UInt4 const &value) = 0;
            virtual void clearRenderTarget(Video::Device::Context *videoContext, ResourceHandle resourceHandle, Math::Float4 const &color) = 0;
            virtual void clearDepthStencilTarget(Video::Device::Context *videoContext, ResourceHandle depthBuffer, uint32_t flags, float clearDepth, uint32_t clearStencil) = 0;

            virtual void setMaterial(Video::Device::Context *videoContext, Shader::Pass *pass, MaterialHandle handle, bool forceShader = false) = 0;
            virtual void setVisual(Video::Device::Context *videoContext, VisualHandle handle) = 0;
            virtual void setRenderState(Video::Device::Context *videoContext, RenderStateHandle renderStateHandle) = 0;
            virtual void setDepthState(Video::Device::Context *videoContext, DepthStateHandle depthStateHandle, uint32_t stencilReference) = 0;
            virtual void setBlendState(Video::Device::Context *videoContext, BlendStateHandle blendStateHandle, Math::Float4 const &blendFactor, uint32_t sampleMask) = 0;
            virtual void setProgram(Video::Device::Context::Pipeline *videoPipeline, ProgramHandle programHandle) = 0;

            virtual void setRenderTargetList(Video::Device::Context *videoContext, std::vector<ResourceHandle> const &renderTargetHandleList, ResourceHandle const *depthBuffer) = 0;

            virtual void clearRenderTargetList(Video::Device::Context *videoContext, int32_t count, bool depthBuffer) = 0;

            virtual void startResourceBlock(void) = 0;
        };
    }; // namespace Engine
}; // namespace Gek
