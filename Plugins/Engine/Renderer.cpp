﻿#include "GEK/Utility/String.hpp"
#include "GEK/Utility/FileSystem.hpp"
#include "GEK/Utility/JSON.hpp"
#include "GEK/Utility/ContextUser.hpp"
#include "GEK/Utility/ThreadPool.hpp"
#include "GEK/Utility/Allocator.hpp"
#include "GEK/Engine/Core.hpp"
#include "GEK/Engine/Renderer.hpp"
#include "GEK/Engine/Resources.hpp"
#include "GEK/Engine/Visual.hpp"
#include "GEK/Engine/Shader.hpp"
#include "GEK/Engine/Filter.hpp"
#include "GEK/Engine/Material.hpp"
#include "GEK/Engine/Population.hpp"
#include "GEK/Engine/Entity.hpp"
#include "GEK/Engine/Component.hpp"
#include "GEK/Engine/ComponentMixin.hpp"
#include "GEK/Components/Transform.hpp"
#include "GEK/Components/Color.hpp"
#include "GEK/Components/Light.hpp"
#include "GEK/Shapes/Sphere.hpp"
#include <concurrent_unordered_set.h>
#include <concurrent_vector.h>
#include <concurrent_queue.h>
#include <smmintrin.h>
#include <algorithm>
#include <ppl.h>

namespace Gek
{
    namespace Implementation
    {
        GEK_CONTEXT_USER(Renderer, Plugin::Core *)
            , public Plugin::Renderer
        {
        public:
            static const uint32_t GridWidth = 16;
            static const uint32_t GridHeight = 8;
            static const uint32_t GridDepth = 24;
            static const uint32_t GridSize = (GridWidth * GridHeight * GridDepth);

        public:
            struct EngineConstantData
            {
                float worldTime;
                float frameTime;
                float buffer[2];
            };

            struct CameraConstantData
            {
                Math::Float2 fieldOfView;
                float nearClip;
                float farClip;
                Math::Float4x4 viewMatrix;
                Math::Float4x4 projectionMatrix;
            };

            struct LightConstantData
            {
                Math::UInt3 gridSize;
                uint32_t directionalLightCount;
                Math::UInt2 tileSize;
                uint32_t pointLightCount;
                uint32_t spotLightCount;
            };

            struct TileLightIndex
            {
                concurrency::concurrent_vector<uint16_t> pointLightList;
                concurrency::concurrent_vector<uint16_t> spotLightList;
            };

            struct TileOffsetCount
            {
                uint32_t indexOffset;
                uint16_t pointLightCount;
                uint16_t spotLightCount;
            };

            struct DrawCallValue
            {
                union
                {
                    uint32_t value;
                    struct
                    {
                        MaterialHandle material;
                        VisualHandle plugin;
                        ShaderHandle shader;
                    };
                };

                std::function<void(Video::Device::Context *)> onDraw;

                DrawCallValue(MaterialHandle material, VisualHandle plugin, ShaderHandle shader, std::function<void(Video::Device::Context *)> &&onDraw)
                    : material(material)
                    , plugin(plugin)
                    , shader(shader)
                    , onDraw(std::move(onDraw))
                {
                }
            };

            using DrawCallList = concurrency::concurrent_vector<DrawCallValue>;

            struct DrawCallSet
            {
                Engine::Shader *shader = nullptr;
                DrawCallList::iterator begin;
                DrawCallList::iterator end;

                DrawCallSet(Engine::Shader *shader, DrawCallList::iterator begin, DrawCallList::iterator end)
                    : shader(shader)
                    , begin(begin)
                    , end(end)
                {
                }
            };

            struct RenderCall
            {
                Shapes::Frustum viewFrustum;
                Math::Float4x4 viewMatrix;
                Math::Float4x4 projectionMatrix;
                float nearClip = 0.0f;
                float farClip = 0.0f;
                ResourceHandle cameraTarget;

                RenderCall(void)
                {
                }

                RenderCall(const RenderCall &renderCall)
                    : viewFrustum(renderCall.viewFrustum)
                    , viewMatrix(renderCall.viewMatrix)
                    , projectionMatrix(renderCall.projectionMatrix)
                    , nearClip(renderCall.nearClip)
                    , farClip(renderCall.farClip)
                    , cameraTarget(renderCall.cameraTarget)
                {
                }
            };

            template <typename COMPONENT, typename DATA>
            struct LightData
            {
                Video::Device *videoDevice = nullptr;
                std::vector<Plugin::Entity *> entityList;
                concurrency::concurrent_vector<DATA, AlignedAllocator<DATA, 16>> lightList;
                Video::BufferPtr lightDataBuffer;

                concurrency::critical_section addSection;
                concurrency::critical_section removeSection;

                LightData(size_t reserve, Video::Device *videoDevice)
                    : videoDevice(videoDevice)
                {
                    lightList.reserve(reserve);
                    createBuffer();
                }

                void addEntity(Plugin::Entity * const entity)
                {
                    if (entity->hasComponent<COMPONENT>())
                    {
                        concurrency::critical_section::scoped_lock lock(addSection);
                        auto search = std::find_if(std::begin(entityList), std::end(entityList), [entity](Plugin::Entity * const search) -> bool
                        {
                            return (entity == search);
                        });

                        if (search == std::end(entityList))
                        {
                            entityList.push_back(entity);
                        }
                    }
                }

                void removeEntity(Plugin::Entity * const entity)
                {
                    concurrency::critical_section::scoped_lock lock(removeSection);
                    auto search = std::find_if(std::begin(entityList), std::end(entityList), [entity](Plugin::Entity * const search) -> bool
                    {
                        return (entity == search);
                    });

                    if (search != std::end(entityList))
                    {
                        entityList.erase(search);
                    }
                }

                void clearEntities(void)
                {
                    entityList.clear();
                }

                void createBuffer(void)
                {
                    if (!lightDataBuffer || lightDataBuffer->getDescription().count < lightList.size())
                    {
                        lightDataBuffer = nullptr;

                        Video::Buffer::Description lightBufferDescription;
                        lightBufferDescription.type = Video::Buffer::Description::Type::Structured;
                        lightBufferDescription.flags = Video::Buffer::Description::Flags::Mappable | Video::Buffer::Description::Flags::Resource;

                        lightBufferDescription.stride = sizeof(DATA);
                        lightBufferDescription.count = lightList.capacity();
                        lightDataBuffer = videoDevice->createBuffer(lightBufferDescription);
                        lightDataBuffer->setName(String::Format(L"render:%v", typeid(COMPONENT).name()));
                    }
                }

                bool updateBuffer(void)
                {
                    bool updated = true;
                    if (!lightList.empty())
                    {
                        DATA *lightData = nullptr;
                        if (updated = videoDevice->mapBuffer(lightDataBuffer.get(), lightData))
                        {
                            std::copy(std::begin(lightList), std::end(lightList), lightData);
                            videoDevice->unmapBuffer(lightDataBuffer.get());
                        }
                    }

                    return updated;
                }
            };

            template <typename COMPONENT, typename DATA>
            struct LightVisibilityData
                : public LightData<COMPONENT, DATA>
            {
                std::vector<float, AlignedAllocator<float, 16>> shapeXPositionList;
                std::vector<float, AlignedAllocator<float, 16>> shapeYPositionList;
                std::vector<float, AlignedAllocator<float, 16>> shapeZPositionList;
                std::vector<float, AlignedAllocator<float, 16>> shapeRadiusList;
                std::vector<bool> visibilityList;

                LightVisibilityData(size_t reserve, Video::Device *videoDevice)
                    : LightData(reserve, videoDevice)
                {
                }

                void clearEntities(void)
                {
                    LightData::clearEntities();
                    shapeXPositionList.clear();
                    shapeYPositionList.clear();
                    shapeZPositionList.clear();
                    shapeRadiusList.clear();
                    visibilityList.clear();
                }

                void update(Video::Device *videoDevice, const __m128 (&frustumData)[6][4], const std::function<void(Plugin::Entity * const, const COMPONENT &)> &addLight)
                {
                    const auto entityCount = entityList.size();
                    auto buffer = (entityCount % 4);
                    buffer = (buffer ? (4 - buffer) : buffer);
                    shapeXPositionList.resize(entityCount + buffer);
                    shapeYPositionList.resize(entityCount + buffer);
                    shapeZPositionList.resize(entityCount + buffer);
                    shapeRadiusList.resize(entityCount + buffer);
                    for (auto entityIndex = 0U; entityIndex < entityCount; entityIndex++)
                    {
                        auto entity = entityList[entityIndex];
                        auto &transformComponent = entity->getComponent<Components::Transform>();
                        auto &lightComponent = entity->getComponent<COMPONENT>();

                        shapeXPositionList[entityIndex] = transformComponent.position.x;
                        shapeYPositionList[entityIndex] = transformComponent.position.y;
                        shapeZPositionList[entityIndex] = transformComponent.position.z;
                        shapeRadiusList[entityIndex] = (lightComponent.range + lightComponent.radius);
                    }

                    visibilityList.resize(entityCount + buffer);
                    static const auto AllZero = _mm_setzero_ps();
                    for (size_t entityIndex = 0; entityIndex < entityCount; entityIndex += 4)
                    {
                        const auto shapeXPosition = _mm_load_ps(&shapeXPositionList[entityIndex]);
                        const auto shapeYPosition = _mm_load_ps(&shapeYPositionList[entityIndex]);
                        const auto shapeZPosition = _mm_load_ps(&shapeZPositionList[entityIndex]);
                        const auto shapeRadius = _mm_load_ps(&shapeRadiusList[entityIndex]);
                        const auto negativeShapeRadius = _mm_sub_ps(AllZero, shapeRadius);

                        auto intersectionResult = AllZero;
                        for (uint32_t plane = 0; plane < 6; ++plane)
                        {
                            // Plane.Normal.Dot(Sphere.Position)
                            const auto dotX = _mm_mul_ps(shapeXPosition, frustumData[plane][0]);
                            const auto dotY = _mm_mul_ps(shapeYPosition, frustumData[plane][1]);
                            const auto dotZ = _mm_mul_ps(shapeZPosition, frustumData[plane][2]);
                            const auto dotXY = _mm_add_ps(dotX, dotY);
                            const auto dotProduct = _mm_add_ps(dotXY, dotZ);

                            // + Plane.Distance
                            const auto planeDistance = _mm_add_ps(dotProduct, frustumData[plane][3]);

                            // < -Sphere.Radius
                            const auto planeTest = _mm_cmplt_ps(planeDistance, negativeShapeRadius);
                            intersectionResult = _mm_or_ps(intersectionResult, planeTest);
                        }

                        __declspec(align(16)) uint32_t resultValues[4];
                        _mm_store_ps((float *)resultValues, intersectionResult);
                        for (uint32_t subIndex = 0; subIndex < 4; subIndex++)
                        {
                            visibilityList[entityIndex + subIndex] = !resultValues[subIndex];
                        }
                    }

                    lightList.clear();
                    concurrency::parallel_for(0U, entityList.size(), [&](size_t index) -> void
                    {
                        if (visibilityList[index])
                        {
                            auto entity = entityList[index];
                            auto &lightComponent = entity->getComponent<COMPONENT>();
                            addLight(entity, lightComponent);
                        }
                    });

                    if (!lightList.empty())
                    {
                        createBuffer();
                    }
                }
            };

        private:
            Plugin::Core *core = nullptr;
            Video::Device *videoDevice = nullptr;
            Plugin::Population *population = nullptr;
            Engine::Resources *resources = nullptr;

            Video::ObjectPtr pointSamplerState;
            Video::ObjectPtr linearClampSamplerState;
            Video::ObjectPtr linearWrapSamplerState;
            Video::BufferPtr engineConstantBuffer;
            Video::BufferPtr cameraConstantBuffer;

            Video::ObjectPtr deferredVertexProgram;
            Video::ObjectPtr deferredPixelProgram;
            Video::ObjectPtr blendState;
            Video::ObjectPtr renderState;
            Video::ObjectPtr depthState;

            ThreadPool threadPool;
            LightData<Components::DirectionalLight, DirectionalLightData> directionalLightData;
            LightVisibilityData<Components::PointLight, PointLightData> pointLightData;
            LightVisibilityData<Components::SpotLight, SpotLightData> spotLightData;

            TileLightIndex tileLightIndexList[GridSize];
            TileOffsetCount tileOffsetCountList[GridSize];
            std::vector<uint16_t> lightIndexList;
            uint32_t lightIndexCount = 0;

            Video::BufferPtr lightConstantBuffer;
            Video::BufferPtr tileOffsetCountBuffer;
            Video::BufferPtr lightIndexBuffer;

            DrawCallList drawCallList;
            concurrency::concurrent_queue<RenderCall> renderCallList;
            RenderCall currentRenderCall;

        public:
            Renderer(Context *context, Plugin::Core *core)
                : ContextRegistration(context)
                , core(core)
                , videoDevice(core->getVideoDevice())
                , population(core->getPopulation())
                , resources(dynamic_cast<Engine::Resources *>(core->getResources()))
                , threadPool(3)
                , directionalLightData(10, core->getVideoDevice())
                , pointLightData(200, core->getVideoDevice())
                , spotLightData(200, core->getVideoDevice())
            {
                core->getLog()->message(L"Renderer", Plugin::Core::Log::Type::Message, L"Initializing rendering system components");

                core->onDisplay.connect<Renderer, &Renderer::onDisplay>(this);
                population->onLoadBegin.connect<Renderer, &Renderer::onLoadBegin>(this);
                population->onLoadSucceeded.connect<Renderer, &Renderer::onLoadSucceeded>(this);
                population->onEntityCreated.connect<Renderer, &Renderer::onEntityCreated>(this);
                population->onEntityDestroyed.connect<Renderer, &Renderer::onEntityDestroyed>(this);
                population->onComponentAdded.connect<Renderer, &Renderer::onComponentAdded>(this);
                population->onComponentRemoved.connect<Renderer, &Renderer::onComponentRemoved>(this);

                Video::SamplerStateInformation pointSamplerStateData;
                pointSamplerStateData.filterMode = Video::SamplerStateInformation::FilterMode::MinificationMagnificationMipMapPoint;
                pointSamplerStateData.addressModeU = Video::SamplerStateInformation::AddressMode::Clamp;
                pointSamplerStateData.addressModeV = Video::SamplerStateInformation::AddressMode::Clamp;
                pointSamplerState = videoDevice->createSamplerState(pointSamplerStateData);
                pointSamplerState->setName(L"renderer:pointSamplerState");

                Video::SamplerStateInformation linearClampSamplerStateData;
                linearClampSamplerStateData.maximumAnisotropy = 8;
                linearClampSamplerStateData.filterMode = Video::SamplerStateInformation::FilterMode::Anisotropic;
                linearClampSamplerStateData.addressModeU = Video::SamplerStateInformation::AddressMode::Clamp;
                linearClampSamplerStateData.addressModeV = Video::SamplerStateInformation::AddressMode::Clamp;
                linearClampSamplerState = videoDevice->createSamplerState(linearClampSamplerStateData);
                linearClampSamplerState->setName(L"renderer:linearClampSamplerState");

                Video::SamplerStateInformation linearWrapSamplerStateData;
                linearWrapSamplerStateData.maximumAnisotropy = 8;
                linearWrapSamplerStateData.filterMode = Video::SamplerStateInformation::FilterMode::Anisotropic;
                linearWrapSamplerStateData.addressModeU = Video::SamplerStateInformation::AddressMode::Wrap;
                linearWrapSamplerStateData.addressModeV = Video::SamplerStateInformation::AddressMode::Wrap;
                linearWrapSamplerState = videoDevice->createSamplerState(linearWrapSamplerStateData);
                linearWrapSamplerState->setName(L"renderer:linearWrapSamplerState");

                Video::UnifiedBlendStateInformation blendStateInformation;
                blendState = videoDevice->createBlendState(blendStateInformation);
                blendState->setName(L"renderer:blendState");

                Video::RenderStateInformation renderStateInformation;
                renderState = videoDevice->createRenderState(renderStateInformation);
                renderState->setName(L"renderer:renderState");

                Video::DepthStateInformation depthStateInformation;
                depthState = videoDevice->createDepthState(depthStateInformation);
                depthState->setName(L"renderer:depthState");

                Video::Buffer::Description constantBufferDescription;
                constantBufferDescription.stride = sizeof(EngineConstantData);
                constantBufferDescription.count = 1;
                constantBufferDescription.type = Video::Buffer::Description::Type::Constant;
                engineConstantBuffer = videoDevice->createBuffer(constantBufferDescription);
                engineConstantBuffer->setName(L"renderer:engineConstantBuffer");

                constantBufferDescription.stride = sizeof(CameraConstantData);
                cameraConstantBuffer = videoDevice->createBuffer(constantBufferDescription);
                cameraConstantBuffer->setName(L"renderer:cameraConstantBuffer");

                constantBufferDescription.stride = sizeof(LightConstantData);
                lightConstantBuffer = videoDevice->createBuffer(constantBufferDescription);
                lightConstantBuffer->setName(L"renderer:lightConstantBuffer");

                static const wchar_t program[] =
                    L"struct Output" \
                    L"{" \
                    L"    float4 screen : SV_POSITION;" \
                    L"    float2 texCoord : TEXCOORD0;" \
                    L"};" \
                    L"" \
                    L"Output mainVertexProgram(in uint vertexID : SV_VertexID)" \
                    L"{" \
                    L"    Output output;" \
                    L"    output.texCoord = float2((vertexID << 1) & 2, vertexID & 2);" \
                    L"    output.screen = float4(output.texCoord * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);" \
                    L"    return output;" \
                    L"}" \
                    L"" \
                    L"struct Input" \
                    L"{" \
                    L"    float4 screen : SV_POSITION;\r\n" \
                    L"    float2 texCoord : TEXCOORD0;" \
                    L"};" \
                    L"" \
                    L"Texture2D<float3> inputBuffer : register(t0);" \
                    L"float3 mainPixelProgram(in Input input) : SV_TARGET0" \
                    L"{" \
                    L"    return inputBuffer[input.screen.xy];" \
                    L"}";

                auto compiledVertexProgram = resources->compileProgram(Video::PipelineType::Vertex, L"deferredVertexProgram", L"mainVertexProgram", program);
                deferredVertexProgram = videoDevice->createProgram(Video::PipelineType::Vertex, compiledVertexProgram.data(), compiledVertexProgram.size());
                deferredVertexProgram->setName(L"renderer:deferredVertexProgram");

                auto compiledPixelProgram = resources->compileProgram(Video::PipelineType::Pixel, L"deferredPixelProgram", L"mainPixelProgram", program);
                deferredPixelProgram = videoDevice->createProgram(Video::PipelineType::Pixel, compiledPixelProgram.data(), compiledPixelProgram.size());
                deferredPixelProgram->setName(L"renderer:deferredPixelProgram");

                Video::Buffer::Description lightBufferDescription;
                lightBufferDescription.type = Video::Buffer::Description::Type::Structured;
                lightBufferDescription.flags = Video::Buffer::Description::Flags::Mappable | Video::Buffer::Description::Flags::Resource;

                Video::Buffer::Description tileBufferDescription;
                tileBufferDescription.type = Video::Buffer::Description::Type::Raw;
                tileBufferDescription.flags = Video::Buffer::Description::Flags::Mappable | Video::Buffer::Description::Flags::Resource;
                tileBufferDescription.format = Video::Format::R32G32_UINT;
                tileBufferDescription.count = GridSize;
                tileOffsetCountBuffer = videoDevice->createBuffer(tileBufferDescription);
                tileOffsetCountBuffer->setName(L"renderer:tileOffsetCountBuffer");

                lightIndexList.reserve(GridSize * 10);
                tileBufferDescription.format = Video::Format::R16_UINT;
                tileBufferDescription.count = lightIndexList.capacity();
                lightIndexBuffer = videoDevice->createBuffer(tileBufferDescription);
                lightIndexBuffer->setName(L"renderer:lightIndexBuffer");
            }

            ~Renderer(void)
            {
                population->onComponentRemoved.disconnect<Renderer, &Renderer::onComponentRemoved>(this);
                population->onComponentAdded.disconnect<Renderer, &Renderer::onComponentAdded>(this);
                population->onEntityDestroyed.disconnect<Renderer, &Renderer::onEntityDestroyed>(this);
                population->onEntityCreated.disconnect<Renderer, &Renderer::onEntityCreated>(this);
                population->onLoadSucceeded.disconnect<Renderer, &Renderer::onLoadSucceeded>(this);
                population->onLoadBegin.disconnect<Renderer, &Renderer::onLoadBegin>(this);
                core->onDisplay.disconnect<Renderer, &Renderer::onDisplay>(this);
            }

            void addEntity(Plugin::Entity * const entity)
            {
                if (entity->hasComponent<Components::Transform>())
                {
                    directionalLightData.addEntity(entity);
                    pointLightData.addEntity(entity);
                    spotLightData.addEntity(entity);
                }
            }

            void removeEntity(Plugin::Entity * const entity)
            {
                directionalLightData.removeEntity(entity);
                pointLightData.removeEntity(entity);
                spotLightData.removeEntity(entity);
            }

            // Clustered Lighting
            inline Math::Float3 getLightDirection(Math::Quaternion const &quaternion) const
            {
                float xx(quaternion.x * quaternion.x);
                float yy(quaternion.y * quaternion.y);
                float zz(quaternion.z * quaternion.z);
                float ww(quaternion.w * quaternion.w);
                float length(xx + yy + zz + ww);
                if (length == 0.0f)
                {
                    return Math::Float3(0.0f, 1.0f, 0.0f);
                }
                else
                {
                    float determinant(1.0f / length);
                    float xy(quaternion.x * quaternion.y);
                    float xw(quaternion.x * quaternion.w);
                    float yz(quaternion.y * quaternion.z);
                    float zw(quaternion.z * quaternion.w);
                    return -Math::Float3((2.0f * (xy - zw) * determinant), ((-xx + yy - zz + ww) * determinant), (2.0f * (yz + xw) * determinant));
                }
            }

            inline void updateClipRegionRoot(float tangentCoordinate, float lightCoordinate, float lightDepth, float radius, float radiusSquared, float lightRangeSquared, float cameraScale, float& minimum, float& maximum) const
            {
                float nz = ((radius - tangentCoordinate * lightCoordinate) / lightDepth);
                float pz = ((lightRangeSquared - radiusSquared) / (lightDepth - (nz / tangentCoordinate) * lightCoordinate));
                if (pz > 0.0f)
                {
                    float clip = (-nz * cameraScale / tangentCoordinate);
                    if (tangentCoordinate > 0.0f)
                    {
                        // Left side boundary
                        minimum = std::max(minimum, clip);
                    }
                    else
                    {
                        // Right side boundary
                        maximum = std::min(maximum, clip);
                    }
                }
            }

            inline void updateClipRegion(float lightCoordinate, float lightDepth, float radius, float cameraScale, float& minimum, float& maximum) const
            {
                float radiusSquared = (radius * radius);
                float lightDepthSquared = (lightDepth * lightDepth);
                float lightCoordinateSquared = (lightCoordinate * lightCoordinate);
                float lightRangeSquared = (lightCoordinateSquared + lightDepthSquared);
                float distanceSquared = ((radiusSquared * lightCoordinateSquared) - (lightRangeSquared * (radiusSquared - lightDepthSquared)));
                if (distanceSquared > 0.0f)
                {
                    float projectedRadius = (radius * lightCoordinate);
                    float distance = std::sqrt(distanceSquared);
                    float positiveTangent = ((projectedRadius + distance) / lightRangeSquared);
                    float negativeTangent = ((projectedRadius - distance) / lightRangeSquared);
                    updateClipRegionRoot(positiveTangent, lightCoordinate, lightDepth, radius, radiusSquared, lightRangeSquared, cameraScale, minimum, maximum);
                    updateClipRegionRoot(negativeTangent, lightCoordinate, lightDepth, radius, radiusSquared, lightRangeSquared, cameraScale, minimum, maximum);
                }
            }

            // Returns bounding box [min.xy, max.xy] in clip [-1, 1] space.
            inline Math::Float4 getClipBounds(Math::Float3 const &position, float range) const
            {
                // Early out with empty rectangle if the light is too far behind the view frustum
                Math::Float4 clipRegion(1.0f, 1.0f, 0.0f, 0.0f);
                if ((position.z + range) >= currentRenderCall.nearClip)
                {
                    clipRegion.set(-1.0f, -1.0f, 1.0f, 1.0f);
                    updateClipRegion(position.x, position.z, range, currentRenderCall.projectionMatrix.rx.x, clipRegion.minimum.x, clipRegion.maximum.x);
                    updateClipRegion(position.y, position.z, range, currentRenderCall.projectionMatrix.ry.y, clipRegion.minimum.y, clipRegion.maximum.y);
                }

                return clipRegion;
            }

            inline Math::Float4 getScreenBounds(Math::Float3 const &position, float range) const
            {
                auto clipBounds((getClipBounds(position, range) + 1.0f) * 0.5f);
                return Math::Float4(clipBounds.x, (1.0f - clipBounds.w), clipBounds.z, (1.0f - clipBounds.y));
            }

            bool isSeparated(float x, float y, float z, Math::Float3 const &position, float range) const
            {
                // sub-frustrum bounds in view space       
                float minimumZ = (z - 0) * 1.0f / GridDepth * (currentRenderCall.farClip - currentRenderCall.nearClip) + currentRenderCall.nearClip;
                float maximumZ = (z + 1) * 1.0f / GridDepth * (currentRenderCall.farClip - currentRenderCall.nearClip) + currentRenderCall.nearClip;

                static const Math::Float4 Negate(Math::Float2(-1.0f), Math::Float2(1.0f));
                static const Math::Float4 GridDimensions(GridWidth, GridWidth, GridHeight, GridHeight);

                Math::Float4 tileBounds(x, (x + 1.0f), y, (y + 1.0f));
                Math::Float4 projectionScale(Math::Float2(currentRenderCall.projectionMatrix.rx.x), Math::Float2(currentRenderCall.projectionMatrix.ry.y));
                auto minimum = Negate * (1.0f - 2.0f / GridDimensions * tileBounds) * minimumZ / projectionScale;
                auto maximum = Negate * (1.0f - 2.0f / GridDimensions * tileBounds) * maximumZ / projectionScale;

                // heuristic plane separation test - works pretty well in practice
                Math::Float3 minimumZcenter((minimum.x + minimum.y) * 0.5f, (minimum.z + minimum.w) * 0.5f, minimumZ);
                Math::Float3 maximumZcenter((maximum.x + maximum.y) * 0.5f, (maximum.z + maximum.w) * 0.5f, maximumZ);
                Math::Float3 center((minimumZcenter + maximumZcenter) * 0.5f);
                Math::Float3 normal((center - position).getNormal());

                // compute distance of all corners to the tangent plane, with a few shortcuts (saves 14 muls)
                Math::Float2 tileCorners(-normal.dot(position));
                tileCorners.minimum += std::min(normal.x * minimum.x, normal.x * minimum.y);
                tileCorners.minimum += std::min(normal.y * minimum.z, normal.y * minimum.w);
                tileCorners.minimum += normal.z * minimumZ;
                tileCorners.maximum += std::min(normal.x * maximum.x, normal.x * maximum.y);
                tileCorners.maximum += std::min(normal.y * maximum.z, normal.y * maximum.w);
                tileCorners.maximum += normal.z * maximumZ;
                return (std::min(tileCorners.minimum, tileCorners.maximum) > range);
            }

            void addLightCluster(Math::Float3 const &position, float range, uint32_t lightIndex, bool pointLight)
            {
                Math::Float4 screenBounds(getScreenBounds(position, range));

                static const Math::Int2 GridDimensions(GridWidth, GridHeight);
                Math::Int4 gridBounds(
                    int32_t(std::floor(screenBounds.x * GridWidth)),
                    int32_t(std::floor(screenBounds.y * GridHeight)),
                    int32_t(std::ceil(screenBounds.z * GridWidth)),
                    int32_t(std::ceil(screenBounds.w * GridHeight))
                );

                gridBounds[0] = (gridBounds[0] < 0 ? 0 : gridBounds[0]);
                gridBounds[1] = (gridBounds[1] < 0 ? 0 : gridBounds[1]);
                gridBounds[2] = (gridBounds[2] > GridWidth ? GridWidth : gridBounds[2]);
                gridBounds[3] = (gridBounds[3] > GridHeight ? GridHeight : gridBounds[3]);

                float centerDepth = ((position.z - currentRenderCall.nearClip) / (currentRenderCall.farClip - currentRenderCall.nearClip));
                float rangeDepth = (range / (currentRenderCall.farClip - currentRenderCall.nearClip));

                Math::Int2 depthBounds(
                    int32_t(std::floor((centerDepth - rangeDepth) * GridDepth)),
                    int32_t(std::ceil((centerDepth + rangeDepth) * GridDepth))
                );

                depthBounds[0] = (depthBounds[0] < 0 ? 0 : depthBounds[0]);
                depthBounds[1] = (depthBounds[1] > GridDepth ? GridDepth : depthBounds[1]);

                concurrency::parallel_for(depthBounds.minimum, depthBounds.maximum, [&](auto z) -> void
                {
                    uint32_t zSlice = (z * GridHeight);
                    for (auto y = gridBounds.minimum.y; y < gridBounds.maximum.y; y++)
                    {
                        uint32_t ySlize = ((zSlice + y) * GridWidth);
                        for (auto x = gridBounds.minimum.x; x < gridBounds.maximum.x; x++)
                        {
                            if (!isSeparated(float(x), float(y), float(z), position, range))
                            {
                                uint32_t gridIndex = (ySlize + x);
                                auto &gridData = tileLightIndexList[gridIndex];
                                if (pointLight)
                                {
                                    gridData.pointLightList.push_back(lightIndex);
                                }
                                else
                                {
                                    gridData.spotLightList.push_back(lightIndex);
                                }

                                InterlockedIncrement(&lightIndexCount);
                            }
                        }
                    }
                });
            }

            void addLight(Plugin::Entity * const entity, const Components::PointLight &lightComponent)
            {
                auto &transformComponent = entity->getComponent<Components::Transform>();
                auto &colorComponent = entity->getComponent<Components::Color>();

                auto lightIterator = pointLightData.lightList.grow_by(1);
                PointLightData &lightData = (*lightIterator);
                lightData.radiance = (colorComponent.value.xyz * lightComponent.intensity);
                lightData.position = currentRenderCall.viewMatrix.transform(transformComponent.position);
                lightData.radius = lightComponent.radius;
                lightData.range = lightComponent.range;

                auto lightIndex = std::distance(std::begin(pointLightData.lightList), lightIterator);
                addLightCluster(lightData.position, (lightData.radius + lightData.range), lightIndex, true);
            }

            void addLight(Plugin::Entity * const entity, const Components::SpotLight &lightComponent)
            {
                auto &transformComponent = entity->getComponent<Components::Transform>();
                auto &colorComponent = entity->getComponent<Components::Color>();

                auto lightIterator = spotLightData.lightList.grow_by(1);
                SpotLightData &lightData = (*lightIterator);
                lightData.radiance = (colorComponent.value.xyz * lightComponent.intensity);
                lightData.position = currentRenderCall.viewMatrix.transform(transformComponent.position);
                lightData.radius = lightComponent.radius;
                lightData.range = lightComponent.range;
                lightData.direction = currentRenderCall.viewMatrix.rotate(getLightDirection(transformComponent.rotation));
                lightData.innerAngle = lightComponent.innerAngle;
                lightData.outerAngle = lightComponent.outerAngle;
                lightData.coneFalloff = lightComponent.coneFalloff;

                auto lightIndex = std::distance(std::begin(spotLightData.lightList), lightIterator);
                addLightCluster(lightData.position, (lightData.radius + lightData.range), lightIndex, false);
            }

            // Plugin::Population Slots
            void onLoadBegin(String const &populationName)
            {
                GEK_REQUIRE(resources);

                resources->clear();
                directionalLightData.clearEntities();
                pointLightData.clearEntities();
                spotLightData.clearEntities();
            }

            void onLoadSucceeded(String const &populationName)
            {
                population->listEntities([&](Plugin::Entity * const entity, wchar_t const * const) -> void
                {
                    addEntity(entity);
                });
            }

            void onEntityCreated(Plugin::Entity * const entity, wchar_t const * const entityName)
            {
                addEntity(entity);
            }

            void onEntityDestroyed(Plugin::Entity * const entity)
            {
                removeEntity(entity);
            }

            void onComponentAdded(Plugin::Entity * const entity, const std::type_index &type)
            {
                addEntity(entity);
            }

            void onComponentRemoved(Plugin::Entity * const entity, const std::type_index &type)
            {
                removeEntity(entity);
            }

            // Renderer
            void queueDrawCall(VisualHandle plugin, MaterialHandle material, std::function<void(Video::Device::Context *videoContext)> &&draw)
            {
                if (plugin && material && draw)
                {
                    ShaderHandle shader = resources->getMaterialShader(material);
                    if (shader)
                    {
                        drawCallList.push_back(DrawCallValue(material, plugin, shader, std::move(draw)));
                    }
                }
            }

            void queueRenderCall(Math::Float4x4 const &viewMatrix, Math::Float4x4 const &projectionMatrix, float nearClip, float farClip, ResourceHandle cameraTarget)
            {
                RenderCall renderCall;
                renderCall.viewMatrix = viewMatrix;
                renderCall.projectionMatrix = projectionMatrix;
                renderCall.viewFrustum.create(viewMatrix * projectionMatrix);
                renderCall.nearClip = nearClip;
                renderCall.farClip = farClip;
                renderCall.cameraTarget = cameraTarget;
                renderCallList.push(renderCall);
            }

            // Plugin::Core Slots
            void onDisplay(void)
            {
                GEK_REQUIRE(videoDevice);
                GEK_REQUIRE(population);

                Plugin::Core::Log::Scope function(core->getLog(), "Render Scene");
                core->getLog()->addValue("Display Cameras", renderCallList.unsafe_size());
                while (renderCallList.try_pop(currentRenderCall))
                {
                    drawCallList.clear();
                    onRenderScene.emit(currentRenderCall.viewFrustum, currentRenderCall.viewMatrix);
                    if (!drawCallList.empty())
                    {
                        core->getLog()->addValue("Display Draw Calls", drawCallList.size());
                        auto backBuffer = videoDevice->getBackBuffer();
                        auto width = backBuffer->getDescription().width;
                        auto height = backBuffer->getDescription().height;

                        concurrency::parallel_sort(std::begin(drawCallList), std::end(drawCallList), [](const DrawCallValue &leftValue, const DrawCallValue &rightValue) -> bool
                        {
                            return (leftValue.value < rightValue.value);
                        });

                        bool isLightingRequired = false;

                        ShaderHandle currentShader;
                        std::map<uint32_t, std::vector<DrawCallSet>> drawCallSetMap;
                        for (auto &drawCall = std::begin(drawCallList); drawCall != std::end(drawCallList); )
                        {
                            currentShader = drawCall->shader;

                            auto beginShaderList = drawCall;
                            while (drawCall != std::end(drawCallList) && drawCall->shader == currentShader)
                            {
                                ++drawCall;
                            };

                            auto endShaderList = drawCall;
                            Engine::Shader *shader = resources->getShader(currentShader);
                            if (!shader)
                            {
                                continue;
                            }

                            isLightingRequired |= shader->isLightingRequired();
                            auto &shaderList = drawCallSetMap[shader->getDrawOrder()];
                            shaderList.push_back(DrawCallSet(shader, beginShaderList, endShaderList));
                        }

                        if (isLightingRequired)
                        {
                            auto directionalLightsDone = threadPool.enqueue([&](void) -> void
                            {
                                directionalLightData.lightList.clear();
                                directionalLightData.lightList.reserve(directionalLightData.entityList.size());
                                std::for_each(std::begin(directionalLightData.entityList), std::end(directionalLightData.entityList), [&](Plugin::Entity * const entity) -> void
                                {
                                    auto &transformComponent = entity->getComponent<Components::Transform>();
                                    auto &colorComponent = entity->getComponent<Components::Color>();
                                    auto &lightComponent = entity->getComponent<Components::DirectionalLight>();

                                    DirectionalLightData lightData;
                                    lightData.radiance = (colorComponent.value.xyz * lightComponent.intensity);
                                    lightData.direction = currentRenderCall.viewMatrix.rotate(getLightDirection(transformComponent.rotation));
                                    directionalLightData.lightList.push_back(lightData);
                                });

                                directionalLightData.createBuffer();
                            });

                            const __m128 frustumData[6][4] =
                            {
                                {
                                    _mm_set_ps1(currentRenderCall.viewFrustum.planeList[0].normal.x),
                                    _mm_set_ps1(currentRenderCall.viewFrustum.planeList[0].normal.y),
                                    _mm_set_ps1(currentRenderCall.viewFrustum.planeList[0].normal.z),
                                    _mm_set_ps1(currentRenderCall.viewFrustum.planeList[0].distance),
                                },
                                {
                                    _mm_set_ps1(currentRenderCall.viewFrustum.planeList[1].normal.x),
                                    _mm_set_ps1(currentRenderCall.viewFrustum.planeList[1].normal.y),
                                    _mm_set_ps1(currentRenderCall.viewFrustum.planeList[1].normal.z),
                                    _mm_set_ps1(currentRenderCall.viewFrustum.planeList[1].distance),
                                },
                                {
                                    _mm_set_ps1(currentRenderCall.viewFrustum.planeList[2].normal.x),
                                    _mm_set_ps1(currentRenderCall.viewFrustum.planeList[2].normal.y),
                                    _mm_set_ps1(currentRenderCall.viewFrustum.planeList[2].normal.z),
                                    _mm_set_ps1(currentRenderCall.viewFrustum.planeList[2].distance),
                                },
                                {
                                    _mm_set_ps1(currentRenderCall.viewFrustum.planeList[3].normal.x),
                                    _mm_set_ps1(currentRenderCall.viewFrustum.planeList[3].normal.y),
                                    _mm_set_ps1(currentRenderCall.viewFrustum.planeList[3].normal.z),
                                    _mm_set_ps1(currentRenderCall.viewFrustum.planeList[3].distance),
                                },
                                {
                                    _mm_set_ps1(currentRenderCall.viewFrustum.planeList[4].normal.x),
                                    _mm_set_ps1(currentRenderCall.viewFrustum.planeList[4].normal.y),
                                    _mm_set_ps1(currentRenderCall.viewFrustum.planeList[4].normal.z),
                                    _mm_set_ps1(currentRenderCall.viewFrustum.planeList[4].distance),
                                },
                                {
                                    _mm_set_ps1(currentRenderCall.viewFrustum.planeList[5].normal.x),
                                    _mm_set_ps1(currentRenderCall.viewFrustum.planeList[5].normal.y),
                                    _mm_set_ps1(currentRenderCall.viewFrustum.planeList[5].normal.z),
                                    _mm_set_ps1(currentRenderCall.viewFrustum.planeList[5].distance),
                                },
                            };

                            lightIndexCount = 0;
                            concurrency::parallel_for_each(std::begin(tileLightIndexList), std::end(tileLightIndexList), [&](auto &gridData) -> void
                            {
                                gridData.pointLightList.clear();
                                gridData.spotLightList.clear();
                            });

                            auto pointLightsDone = threadPool.enqueue([&](void) -> void
                            {
                                pointLightData.update(videoDevice, frustumData, [this](Plugin::Entity * const entity, const Components::PointLight &lightComponent) -> void
                                {
                                    addLight(entity, lightComponent);
                                });
                            });

                            auto spotLightsDone = threadPool.enqueue([&](void) -> void
                            {
                                spotLightData.update(videoDevice, frustumData, [this](Plugin::Entity * const entity, const Components::SpotLight &lightComponent) -> void
                                {
                                    addLight(entity, lightComponent);
                                });
                            });

                            directionalLightsDone.get();
                            pointLightsDone.get();
                            spotLightsDone.get();

                            core->getLog()->addValue("Display Directional Lights", directionalLightData.lightList.size());
                            core->getLog()->addValue("Display Point Lights", pointLightData.lightList.size());
                            core->getLog()->addValue("Display Spot Lights", spotLightData.lightList.size());

                            lightIndexList.clear();
                            lightIndexList.reserve(lightIndexCount);
                            for (uint32_t tileIndex = 0; tileIndex < GridSize; tileIndex++)
                            {
                                auto &tileOffsetCount = tileOffsetCountList[tileIndex];
                                auto &tileLightIndex = tileLightIndexList[tileIndex];
                                tileOffsetCount.indexOffset = lightIndexList.size();
                                tileOffsetCount.pointLightCount = uint16_t(tileLightIndex.pointLightList.size() & 0xFFFF);
                                tileOffsetCount.spotLightCount = uint16_t(tileLightIndex.spotLightList.size() & 0xFFFF);
                                lightIndexList.insert(std::end(lightIndexList), std::begin(tileLightIndex.pointLightList), std::end(tileLightIndex.pointLightList));
                                lightIndexList.insert(std::end(lightIndexList), std::begin(tileLightIndex.spotLightList), std::end(tileLightIndex.spotLightList));
                            }

                            if (!directionalLightData.updateBuffer() ||
                                !pointLightData.updateBuffer() ||
                                !spotLightData.updateBuffer())
                            {
                                continue;
                            }

                            TileOffsetCount *tileOffsetCountData = nullptr;
                            if (videoDevice->mapBuffer(tileOffsetCountBuffer.get(), tileOffsetCountData))
                            {
                                std::copy(std::begin(tileOffsetCountList), std::end(tileOffsetCountList), tileOffsetCountData);
                                videoDevice->unmapBuffer(tileOffsetCountBuffer.get());
                            }
                            else
                            {
                                continue;
                            }

                            if (!lightIndexList.empty())
                            {
                                if (!lightIndexBuffer || lightIndexBuffer->getDescription().count < lightIndexList.size())
                                {
                                    lightIndexBuffer = nullptr;

                                    Video::Buffer::Description tileBufferDescription;
                                    tileBufferDescription.type = Video::Buffer::Description::Type::Raw;
                                    tileBufferDescription.flags = Video::Buffer::Description::Flags::Mappable | Video::Buffer::Description::Flags::Resource;
                                    tileBufferDescription.format = Video::Format::R16_UINT;
                                    tileBufferDescription.count = lightIndexList.size();
                                    lightIndexBuffer = videoDevice->createBuffer(tileBufferDescription);
                                    lightIndexBuffer->setName(String::Format(L"renderer:lightIndexBuffer:%v", lightIndexBuffer.get()));
                                }

                                uint16_t *lightIndexData = nullptr;
                                if (videoDevice->mapBuffer(lightIndexBuffer.get(), lightIndexData))
                                {
                                    std::copy(std::begin(lightIndexList), std::end(lightIndexList), lightIndexData);
                                    videoDevice->unmapBuffer(lightIndexBuffer.get());
                                }
                                else
                                {
                                    continue;
                                }
                            }

                            LightConstantData lightConstants;
                            lightConstants.directionalLightCount = directionalLightData.lightList.size();
                            lightConstants.pointLightCount = pointLightData.lightList.size();
                            lightConstants.spotLightCount = spotLightData.lightList.size();
                            lightConstants.gridSize.x = GridWidth;
                            lightConstants.gridSize.y = GridHeight;
                            lightConstants.gridSize.z = GridDepth;
                            lightConstants.tileSize.x = (width / GridWidth);
                            lightConstants.tileSize.y = (height / GridHeight);
                            videoDevice->updateResource(lightConstantBuffer.get(), &lightConstants);
                        }

                        EngineConstantData engineConstantData;
                        engineConstantData.frameTime = population->getFrameTime();
                        engineConstantData.worldTime = population->getWorldTime();

                        CameraConstantData cameraConstantData;
                        cameraConstantData.fieldOfView.x = (1.0f / currentRenderCall.projectionMatrix._11);
                        cameraConstantData.fieldOfView.y = (1.0f / currentRenderCall.projectionMatrix._22);
                        cameraConstantData.nearClip = currentRenderCall.nearClip;
                        cameraConstantData.farClip = currentRenderCall.farClip;
                        cameraConstantData.viewMatrix = currentRenderCall.viewMatrix;
                        cameraConstantData.projectionMatrix = currentRenderCall.projectionMatrix;

                        Video::Device::Context *videoContext = videoDevice->getDefaultContext();
                        videoContext->clearState();

                        videoDevice->updateResource(engineConstantBuffer.get(), &engineConstantData);
                        videoDevice->updateResource(cameraConstantBuffer.get(), &cameraConstantData);

                        std::vector<Video::Buffer *> bufferList = { engineConstantBuffer.get(), cameraConstantBuffer.get() };
                        videoContext->geometryPipeline()->setConstantBufferList(bufferList, 0);
                        videoContext->vertexPipeline()->setConstantBufferList(bufferList, 0);
                        videoContext->pixelPipeline()->setConstantBufferList(bufferList, 0);
                        videoContext->computePipeline()->setConstantBufferList(bufferList, 0);

                        std::vector<Video::Object *> samplerList = { pointSamplerState.get(), linearClampSamplerState.get(), linearWrapSamplerState.get() };
                        videoContext->pixelPipeline()->setSamplerStateList(samplerList, 0);

                        videoContext->setPrimitiveType(Video::PrimitiveType::TriangleList);

                        if (isLightingRequired)
                        {
                            videoContext->pixelPipeline()->setConstantBufferList({ lightConstantBuffer.get() }, 3);
                            videoContext->pixelPipeline()->setResourceList(
                            {
                                directionalLightData.lightDataBuffer.get(),
                                pointLightData.lightDataBuffer.get(),
                                spotLightData.lightDataBuffer.get(),
                                tileOffsetCountBuffer.get(),
                                lightIndexBuffer.get()
                            }, 0);
                        }

                        for (auto &shaderDrawCallList : drawCallSetMap)
                        {
                            for (auto &shaderDrawCall : shaderDrawCallList.second)
                            {
                                auto &shader = shaderDrawCall.shader;
                                for (auto pass = shader->begin(videoContext, cameraConstantData.viewMatrix, currentRenderCall.viewFrustum); pass; pass = pass->next())
                                {
                                    resources->startResourceBlock();
                                    switch (pass->prepare())
                                    {
                                    case Engine::Shader::Pass::Mode::Forward:
                                        if (true)
                                        {
                                            VisualHandle currentVisual;
                                            MaterialHandle currentMaterial;
                                            for (auto drawCall = shaderDrawCall.begin; drawCall != shaderDrawCall.end; ++drawCall)
                                            {
                                                if (currentVisual != drawCall->plugin)
                                                {
                                                    currentVisual = drawCall->plugin;
                                                    resources->setVisual(videoContext, currentVisual);
                                                }

                                                if (currentMaterial != drawCall->material)
                                                {
                                                    currentMaterial = drawCall->material;
                                                    resources->setMaterial(videoContext, pass.get(), currentMaterial);
                                                }

                                                drawCall->onDraw(videoContext);
                                            }
                                        }

                                        break;

                                    case Engine::Shader::Pass::Mode::Deferred:
                                        videoContext->vertexPipeline()->setProgram(deferredVertexProgram.get());
                                        resources->drawPrimitive(videoContext, 3, 0);
                                        break;

                                    case Engine::Shader::Pass::Mode::Compute:
                                        break;
                                    };

                                    pass->clear();
                                }
                            }
                        }

                        videoContext->vertexPipeline()->setProgram(deferredVertexProgram.get());
                        for (auto &filterName : { L"tonemap", L"antialias" })
                        {
                            Engine::Filter * const filter = resources->getFilter(filterName);
                            if (filter)
                            {
                                for (auto pass = filter->begin(videoContext); pass; pass = pass->next())
                                {
                                    switch (pass->prepare())
                                    {
                                    case Engine::Filter::Pass::Mode::Deferred:
                                        resources->drawPrimitive(videoContext, 3, 0);
                                        break;

                                    case Engine::Filter::Pass::Mode::Compute:
                                        break;
                                    };

                                    pass->clear();
                                }
                            }
                        }

                        videoContext->geometryPipeline()->clearConstantBufferList(2, 0);
                        videoContext->vertexPipeline()->clearConstantBufferList(2, 0);
                        videoContext->pixelPipeline()->clearConstantBufferList(2, 0);
                        videoContext->computePipeline()->clearConstantBufferList(2, 0);
                        if (currentRenderCall.cameraTarget)
                        {
                            renderOverlay(videoContext, resources->getResourceHandle(L"screen"), currentRenderCall.cameraTarget);
                        }
                    }
                };
            }

            void renderOverlay(Video::Device::Context *videoContext, ResourceHandle input, ResourceHandle target)
            {
                videoContext->setBlendState(blendState.get(), Math::Float4::Black, 0xFFFFFFFF);
                videoContext->setDepthState(depthState.get(), 0);
                videoContext->setRenderState(renderState.get());

                videoContext->setPrimitiveType(Video::PrimitiveType::TriangleList);

                resources->startResourceBlock();
                resources->setResourceList(videoContext->pixelPipeline(), { input }, 0);

                videoContext->vertexPipeline()->setProgram(deferredVertexProgram.get());
                videoContext->pixelPipeline()->setProgram(deferredPixelProgram.get());
                if (target)
                {
                    resources->setRenderTargetList(videoContext, { target }, nullptr);
                }
                else
                {
                    resources->setBackBuffer(videoContext, nullptr);
                }

                resources->drawPrimitive(videoContext, 3, 0);
            }
        };

        GEK_REGISTER_CONTEXT_USER(Renderer);
    }; // namespace Implementation
}; // namespace Gek
