﻿#include "GEK\Math\Float4x4.h"
#include "GEK\Shapes\AlignedBox.h"
#include "GEK\Shapes\OrientedBox.h"
#include "GEK\Utility\FileSystem.h"
#include "GEK\Utility\String.h"
#include "GEK\Utility\XML.h"
#include "GEK\Utility\Allocator.h"
#include "GEK\Context\ContextUser.h"
#include "GEK\System\VideoDevice.h"
#include "GEK\Engine\Core.h"
#include "GEK\Engine\Processor.h"
#include "GEK\Engine\ComponentMixin.h"
#include "GEK\Engine\Population.h"
#include "GEK\Engine\Entity.h"
#include "GEK\Engine\Renderer.h"
#include "GEK\Engine\Resources.h"
#include "GEK\Components\Transform.h"
#include "GEK\Components\Color.h"
#include "GEK\Model\Base.h"
#include <concurrent_queue.h>
#include <concurrent_unordered_map.h>
#include <concurrent_vector.h>
#include <ppl.h>
#include <future>
#include <algorithm>
#include <memory>
#include <array>
#include <map>

namespace Gek
{
    namespace Components
    {
        Model::Model(void)
        {
        }

        void Model::save(Plugin::Population::ComponentDefinition &componentData) const
        {
            saveParameter(componentData, nullptr, name);
            saveParameter(componentData, L"skin", skin);
        }

        void Model::load(const Plugin::Population::ComponentDefinition &componentData)
        {
            name = loadParameter(componentData, nullptr, String());
            skin = loadParameter(componentData, L"skin", String());
        }
    }; // namespace Components

    GEK_CONTEXT_USER(Model)
        , public Plugin::ComponentMixin<Components::Model>
    {
    public:
        Model(Context *context)
            : ContextRegistration(context)
        {
        }

        // Plugin::Component
        const wchar_t * const getName(void) const
        {
            return L"model";
        }
    };

    GEK_CONTEXT_USER(ModelProcessor, Plugin::Core *)
        , public Plugin::PopulationListener
        , public Plugin::RendererListener
        , public Plugin::Processor
    {
        GEK_START_EXCEPTIONS();
        GEK_ADD_EXCEPTION(InvalidModelIdentifier);
        GEK_ADD_EXCEPTION(InvalidModelType);
        GEK_ADD_EXCEPTION(InvalidModelVersion);

    public:
        struct Header
        {
            uint32_t identifier;
            uint16_t type;
            uint16_t version;
            Shapes::AlignedBox boundingBox;
            uint32_t materialCount;

            struct Material
            {
                wchar_t name[64];
                uint32_t vertexCount;
                uint32_t indexCount;
            };
        };

        struct Vertex
        {
            Math::Float3 position;
            Math::Float2 texCoord;
            Math::Float3 normal;
        };

        struct Material
        {
            bool skin;
            MaterialHandle material;
            ResourceHandle vertexBuffer;
            ResourceHandle indexBuffer;
            uint32_t indexCount;

            Material(void)
                : skin(false)
                , indexCount(0)
            {
            }

            Material(const Material &material)
                : skin(material.skin)
                , material(material.material)
                , vertexBuffer(material.vertexBuffer)
                , indexBuffer(material.indexBuffer)
                , indexCount(material.indexCount)
            {
            }
        };

        struct Model
        {
            std::mutex mutex;
            std::shared_future<void> loadBox;
            std::shared_future<void> loadData;
            std::function<void(Model &)> load;

            Shapes::AlignedBox alignedBox;
            std::vector<Material> materialList;

            Model(void)
            {
            }

            Model(const Model &model)
                : loadBox(model.loadBox)
                , loadData(model.loadData)
                , load(model.load)
                , alignedBox(model.alignedBox)
                , materialList(model.materialList)
            {
            }

            bool valid(void)
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (!loadData.valid())
                {
                    loadData = Gek::asynchronous(load, std::ref(*this)).share();
                }

                return (loadData.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready);
            }
        };

        struct Data
        {
            Model &model;
            MaterialHandle skin;
            const Math::Color &color;

            Data(Model &model, MaterialHandle skin, const Math::Color &color)
                : model(model)
                , skin(skin)
                , color(color)
            {
            }
        };

        __declspec(align(16))
            struct Instance
        {
            Math::Float4x4 matrix;
            Math::Color color;
            Math::Float3 scale;
            float buffer;

            Instance(const Math::Float4x4 &matrix, const Math::Color &color, const Math::Float3 &scale)
                : matrix(matrix)
                , color(color)
                , scale(scale)
            {
            }
        };

    private:
        Plugin::Population *population;
        Plugin::Resources *resources;
        Plugin::Renderer *renderer;

        VisualHandle visual;
        Video::BufferPtr constantBuffer;

        concurrency::concurrent_unordered_map<std::size_t, Model> modelMap;
        using EntityDataMap = concurrency::concurrent_unordered_map<Plugin::Entity *, Data>;
        EntityDataMap entityDataMap;

        using InstanceList = concurrency::concurrent_vector<Instance, AlignedAllocator<Instance, 16>>;
        using MaterialMap = concurrency::concurrent_unordered_map<MaterialHandle, InstanceList>;
        using VisibleMap = concurrency::concurrent_unordered_map<Model *, MaterialMap>;
        VisibleMap visibleMap;

    public:
        ModelProcessor(Context *context, Plugin::Core *core)
            : ContextRegistration(context)
            , population(core->getPopulation())
            , resources(core->getResources())
            , renderer(core->getRenderer())
        {
            GEK_REQUIRE(population);
            GEK_REQUIRE(resources);
            GEK_REQUIRE(renderer);

            population->addListener(this);
            renderer->addListener(this);

            visual = resources->loadVisual(L"model");

            constantBuffer = renderer->getDevice()->createBuffer(sizeof(Instance), 1, Video::BufferType::Constant, Video::BufferFlags::Mappable, false);
        }

        ~ModelProcessor(void)
        {
            renderer->removeListener(this);
            population->removeListener(this);
        }

        // Plugin::PopulationListener
        void onLoadBegin(void)
        {
            modelMap.clear();
            entityDataMap.clear();
        }

        void onLoadSucceeded(void)
        {
        }

        void onLoadFailed(void)
        {
        }

        void onEntityCreated(Plugin::Entity *entity)
        {
            GEK_REQUIRE(resources);
            GEK_REQUIRE(entity);

            if (entity->hasComponents<Components::Model, Components::Transform>())
            {
                auto &modelComponent = entity->getComponent<Components::Model>();
                String fileName(getContext()->getFileName(L"data\\models", modelComponent.name).append(L".gek"));
                auto pair = modelMap.insert(std::make_pair(getHash(modelComponent.name), Model()));
                if (pair.second)
                {
                    pair.first->second.loadBox = Gek::asynchronous([this, name = String(modelComponent.name), fileName, alignedBox = &pair.first->second.alignedBox](void) -> void
                    {
                        std::vector<uint8_t> buffer;
                        FileSystem::load(fileName, buffer, sizeof(Header));

                        Header *header = (Header *)buffer.data();
                        if (header->identifier != *(uint32_t *)"GEKX")
                        {
                            throw InvalidModelIdentifier();
                        }

                        if (header->type != 0)
                        {
                            throw InvalidModelType();
                        }

                        if (header->version != 3)
                        {
                            throw InvalidModelVersion();
                        }

                        (*alignedBox) = header->boundingBox;
                    }).share();

                    pair.first->second.load = [this, name = String(modelComponent.name), fileName](Model &model) -> void
                    {
                        std::vector<uint8_t> buffer;
                        FileSystem::load(fileName, buffer);

                        auto data = buffer.data();
                        Header *header = (Header *)data;
                        data += sizeof(Header);

                        model.materialList.resize(header->materialCount);
                        for (uint32_t modelIndex = 0; modelIndex < header->materialCount; ++modelIndex)
                        {
                            Header::Material *materialHeader = (Header::Material *)data;
                            data += sizeof(Header::Material);

                            Material &material = model.materialList[modelIndex];
                            if (wcscmp(materialHeader->name, L"skin") == 0)
                            {
                                material.skin = true;
                            }
                            else
                            {
                                material.material = resources->loadMaterial(materialHeader->name);
                            }

                            auto vertexStart = data;
                            data += (sizeof(Vertex) * materialHeader->vertexCount);
                            auto vertexEnd = data;

                            auto indexStart = data;
                            data += (sizeof(uint16_t) * materialHeader->indexCount);
                            auto indexEnd = data;

                            material.indexCount = materialHeader->indexCount;
                            material.indexBuffer = resources->createBuffer(String(L"model:index:%v:%v", name, modelIndex), Video::Format::R16_UINT, materialHeader->indexCount, Video::BufferType::Index, 0, std::vector<uint8_t>(indexStart, indexEnd));
                            material.vertexBuffer = resources->createBuffer(String(L"model:vertex:%v:%v", name, modelIndex), sizeof(Vertex), materialHeader->vertexCount, Video::BufferType::Vertex, 0, std::vector<uint8_t>(vertexStart, vertexEnd));
                        }
                    };
                }

                MaterialHandle skinMaterial;
                if (!modelComponent.skin.empty())
                {
                    skinMaterial = resources->loadMaterial(modelComponent.skin);
                }

                std::reference_wrapper<const Math::Color> color = Math::Color::White;
                if (entity->hasComponent<Components::Color>())
                {
                    color = std::cref(entity->getComponent<Components::Color>().value);
                }

                Data data(pair.first->second, skinMaterial, color);
                entityDataMap.insert(std::make_pair(entity, data));
            }
        }

        void onEntityDestroyed(Plugin::Entity *entity)
        {
            GEK_REQUIRE(entity);

            auto entitySearch = entityDataMap.find(entity);
            if (entitySearch != entityDataMap.end())
            {
                entityDataMap.unsafe_erase(entitySearch);
            }
        }

        // Plugin::RendererListener
        static void drawCall(Video::Device::Context *deviceContext, Plugin::Resources *resources, const Material &material, const Instance *instanceList, Video::Buffer *constantBuffer)
        {
            Instance *instanceData = nullptr;
            deviceContext->getDevice()->mapBuffer(constantBuffer, (void **)&instanceData);
            memcpy(instanceData, instanceList, sizeof(Instance));
            deviceContext->getDevice()->unmapBuffer(constantBuffer);

            deviceContext->vertexPipeline()->setConstantBuffer(constantBuffer, 4);
            resources->setVertexBuffer(deviceContext, 0, material.vertexBuffer, 0);
            resources->setIndexBuffer(deviceContext, material.indexBuffer, 0);
            deviceContext->drawIndexedPrimitive(material.indexCount, 0, 0);
        }

        void onRenderScene(Plugin::Entity *cameraEntity, const Math::Float4x4 &viewMatrix, const Shapes::Frustum &viewFrustum)
        {
            GEK_REQUIRE(renderer);
            GEK_REQUIRE(cameraEntity);

            visibleMap.clear();
            concurrency::parallel_for_each(entityDataMap.begin(), entityDataMap.end(), [&](auto &entityDataPair) -> void
            {
                Plugin::Entity *entity = entityDataPair.first;
                Data &data = entityDataPair.second;
                Model &model = data.model;
                if (model.loadBox.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
                {
                    const auto &transformComponent = entity->getComponent<Components::Transform>();
                    Math::Float4x4 matrix(transformComponent.getMatrix());

                    Shapes::OrientedBox orientedBox(model.alignedBox, matrix);
                    orientedBox.halfsize *= transformComponent.scale;

                    if (viewFrustum.isVisible(orientedBox))
                    {
                        auto &materialList = visibleMap[&model];
                        auto &instanceList = materialList[data.skin];
                        instanceList.push_back(Instance((matrix * viewMatrix), data.color, transformComponent.scale));
                    }
                }
            });

            concurrency::parallel_for_each(visibleMap.begin(), visibleMap.end(), [&](auto &visibleMap) -> void
            {
                Model &model = *visibleMap.first;
                if (model.valid() && !model.materialList.empty())
                {
                    concurrency::parallel_for_each(visibleMap.second.begin(), visibleMap.second.end(), [&](auto &materialMap) -> void
                    {
                        concurrency::parallel_for_each(materialMap.second.begin(), materialMap.second.end(), [&](auto &instanceList) -> void
                        {
                            concurrency::parallel_for_each(model.materialList.begin(), model.materialList.end(), [&](const Material &material) -> void
                            {
                                renderer->queueDrawCall(visual, (material.skin ? materialMap.first : material.material), std::bind(drawCall, std::placeholders::_1, resources, material, &instanceList, constantBuffer.get()));
                            });
                        });
                    });
                }
            });
        }
    };

    GEK_REGISTER_CONTEXT_USER(Model)
    GEK_REGISTER_CONTEXT_USER(ModelProcessor)
}; // namespace Gek
