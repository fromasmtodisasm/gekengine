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
#include "GEK/Engine/Shader.hpp"
#include <type_traits>
#include <typeindex>

namespace Gek
{
    namespace Plugin
    {
        GEK_PREDECLARE(Visual);

        GEK_INTERFACE(Resources)
        {
            GEK_ADD_EXCEPTION(InvalidParameter);
            GEK_ADD_EXCEPTION(InvalidIncludeType);
            GEK_ADD_EXCEPTION(InvalidIncludeName);

            virtual ~Resources(void) = default;

            virtual VisualHandle loadVisual(const wchar_t *pluginName) = 0;
            virtual MaterialHandle loadMaterial(const wchar_t *materialName) = 0;

            virtual ResourceHandle loadTexture(const wchar_t *textureName, uint32_t flags) = 0;
            virtual ResourceHandle createTexture(const wchar_t *pattern, const wchar_t *parameters) = 0;

            virtual ResourceHandle createTexture(const wchar_t *textureName, const Video::TextureDescription &description) = 0;
            virtual ResourceHandle createBuffer(const wchar_t *bufferName, const Video::BufferDescription &description) = 0;
            virtual ResourceHandle createBuffer(const wchar_t *bufferName, const Video::BufferDescription &description, std::vector<uint8_t> &&staticData) = 0;

            template <typename TYPE>
            ResourceHandle createBuffer(const wchar_t *bufferName, const Video::BufferDescription &description, const TYPE *staticData)
            {
                auto rawData = reinterpret_cast<const uint8_t *>(staticData);
                std::vector<uint8_t> rawBuffer(rawData, (rawData + (sizeof(TYPE) * description.count)));
                return createBuffer(bufferName, description, std::move(rawBuffer));
            }

            virtual void setIndexBuffer(Video::Device::Context *videoContext, ResourceHandle resourceHandle, uint32_t offset) = 0;
            virtual void setVertexBufferList(Video::Device::Context *videoContext, const std::vector<ResourceHandle> &resourceHandleList, uint32_t firstSlot, uint32_t *offsetList = nullptr) = 0;
            virtual void setConstantBufferList(Video::Device::Context::Pipeline *videoPipeline, const std::vector<ResourceHandle> &resourceHandleList, uint32_t firstStage) = 0;
            virtual void setResourceList(Video::Device::Context::Pipeline *videoPipeline, const std::vector<ResourceHandle> &resourceHandleList, uint32_t firstStage) = 0;
            virtual void setUnorderedAccessList(Video::Device::Context::Pipeline *videoPipeline, const std::vector<ResourceHandle> &resourceHandleList, uint32_t firstStage) = 0;

            virtual void clearIndexBuffer(Video::Device::Context *videoContext) = 0;
            virtual void clearVertexBufferList(Video::Device::Context *videoContext, uint32_t count, uint32_t firstSlot) = 0;
            virtual void clearConstantBufferList(Video::Device::Context::Pipeline *videoPipeline, uint32_t count, uint32_t firstStage) = 0;
            virtual void clearResourceList(Video::Device::Context::Pipeline *videoPipeline, uint32_t count, uint32_t firstStage) = 0;
            virtual void clearUnorderedAccessList(Video::Device::Context::Pipeline *videoPipeline, uint32_t count, uint32_t firstStage) = 0;

            virtual void drawPrimitive(Video::Device::Context *videoContext, uint32_t vertexCount, uint32_t firstVertex) = 0;
            virtual void drawInstancedPrimitive(Video::Device::Context *videoContext, uint32_t instanceCount, uint32_t firstInstance, uint32_t vertexCount, uint32_t firstVertex) = 0;
            virtual void drawIndexedPrimitive(Video::Device::Context *videoContext, uint32_t indexCount, uint32_t firstIndex, uint32_t firstVertex) = 0;
            virtual void drawInstancedIndexedPrimitive(Video::Device::Context *videoContext, uint32_t instanceCount, uint32_t firstInstance, uint32_t indexCount, uint32_t firstIndex, uint32_t firstVertex) = 0;
            virtual void dispatch(Video::Device::Context *videoContext, uint32_t threadGroupCountX, uint32_t threadGroupCountY, uint32_t threadGroupCountZ) = 0;
        };
    }; // namespace Plugin

    namespace Engine
    {
        GEK_PREDECLARE(Shader);
        GEK_PREDECLARE(Filter);
        GEK_PREDECLARE(Material);

        GEK_INTERFACE(Resources)
            : virtual public Plugin::Resources
        {
            virtual void clear(void) = 0;

            virtual ShaderHandle getMaterialShader(MaterialHandle material) const = 0;
            virtual ResourceHandle getResourceHandle(const wchar_t *resourceName) const = 0;

            virtual Shader * const getShader(const wchar_t *shaderName, MaterialHandle materialHandle) = 0;
            virtual Shader * const getShader(ShaderHandle handle) const = 0;
            virtual Filter * const getFilter(const wchar_t *filterName) = 0;

            virtual std::vector<uint8_t> compileProgram(Video::PipelineType pipelineType, const wchar_t *name, const wchar_t *entryFunction, const wchar_t *engineData = nullptr) = 0;
            virtual ProgramHandle loadProgram(Video::PipelineType pipelineType, const wchar_t *name, const wchar_t *entryFunction, const wchar_t *engineData = nullptr) = 0;

            virtual RenderStateHandle createRenderState(const Video::RenderStateInformation &renderState) = 0;
            virtual DepthStateHandle createDepthState(const Video::DepthStateInformation &depthState) = 0;
            virtual BlendStateHandle createBlendState(const Video::UnifiedBlendStateInformation &blendState) = 0;
            virtual BlendStateHandle createBlendState(const Video::IndependentBlendStateInformation &blendState) = 0;

            virtual void generateMipMaps(Video::Device::Context *videoContext, ResourceHandle resourceHandle) = 0;
            virtual void resolveSamples(Video::Device::Context *videoContext, ResourceHandle destinationHandle, ResourceHandle sourceHandle) = 0;
            virtual void copyResource(ResourceHandle destinationHandle, ResourceHandle sourceHandle) = 0;

            virtual void clearUnorderedAccess(Video::Device::Context *videoContext, ResourceHandle resourceHandle, const Math::Float4 &value) = 0;
            virtual void clearUnorderedAccess(Video::Device::Context *videoContext, ResourceHandle resourceHandle, const uint32_t value[4]) = 0;
            virtual void clearRenderTarget(Video::Device::Context *videoContext, ResourceHandle resourceHandle, const Math::Float4 &color) = 0;
            virtual void clearDepthStencilTarget(Video::Device::Context *videoContext, ResourceHandle depthBuffer, uint32_t flags, float clearDepth, uint32_t clearStencil) = 0;

            virtual void setMaterial(Video::Device::Context *videoContext, Shader::Pass *pass, MaterialHandle handle) = 0;
            virtual void setVisual(Video::Device::Context *videoContext, VisualHandle handle) = 0;
            virtual void setRenderState(Video::Device::Context *videoContext, RenderStateHandle renderStateHandle) = 0;
            virtual void setDepthState(Video::Device::Context *videoContext, DepthStateHandle depthStateHandle, uint32_t stencilReference) = 0;
            virtual void setBlendState(Video::Device::Context *videoContext, BlendStateHandle blendStateHandle, const Math::Float4 &blendFactor, uint32_t sampleMask) = 0;
            virtual void setProgram(Video::Device::Context::Pipeline *videoPipeline, ProgramHandle programHandle) = 0;

            virtual void setRenderTargetList(Video::Device::Context *videoContext, const std::vector<ResourceHandle> &renderTargetHandleList, ResourceHandle *depthBuffer) = 0;
            virtual void setBackBuffer(Video::Device::Context *videoContext, ResourceHandle *depthBuffer) = 0;

            virtual void clearRenderTargetList(Video::Device::Context *videoContext, int32_t count, bool depthBuffer) = 0;

            virtual void startResourceBlock(void) = 0;
        };
    }; // namespace Engine
}; // namespace Gek