﻿#include "GEK\Math\Float4x4.h"
#include "GEK\Shapes\AlignedBox.h"
#include "GEK\Shapes\OrientedBox.h"
#include "GEK\Utility\FileSystem.h"
#include "GEK\Utility\String.h"
#include "GEK\Utility\XML.h"
#include "GEK\Utility\Allocator.h"
#include "GEK\Utility\ShuntingYard.h"
#include "GEK\Context\ContextUser.h"
#include "GEK\Context\ObservableMixin.h"
#include "GEK\System\VideoSystem.h"
#include "GEK\Engine\Engine.h"
#include "GEK\Engine\Processor.h"
#include "GEK\Engine\Population.h"
#include "GEK\Engine\Entity.h"
#include "GEK\Engine\Render.h"
#include "GEK\Engine\Resources.h"
#include "GEK\Components\Transform.h"
#include "GEK\Components\Color.h"
#include "GEK\Engine\Particles.h"
#include <concurrent_queue.h>
#include <concurrent_unordered_map.h>
#include <concurrent_vector.h>
#include <ppl.h>
#include <functional>
#include <random>
#include <map>

static std::random_device randomDevice;
static std::mt19937 mersineTwister(randomDevice());
static std::uniform_real_distribution<float> zeroToOne(0.0f, 1.0f);
static std::uniform_real_distribution<float> halfToOne(0.5f, 1.0f);
static std::uniform_real_distribution<float> negativeOneToOne(-1.0f, 1.0f);

namespace Gek
{
    class ParticlesProcessorImplementation
        : public ContextRegistration<ParticlesProcessorImplementation, EngineContext *>
        , public PopulationObserver
        , public RenderObserver
        , public Processor
    {
    public:
        __declspec(align(16))
        struct Particle
        {
            Math::Float3 origin;
            Math::Float2 offset;
            float age, death;
            float angle, spin;
            float size;

            Particle(void)
                : age(0.0f)
                , death(0.0f)
                , angle(0.0f)
                , spin(0.0f)
                , size(1.0f)
            {
            }
        };

        static const uint32_t ParticleBufferCount = ((64 * 1024) / sizeof(Particle));

        struct Emitter
            : public Shapes::AlignedBox
        {
            const Math::Color &color;
            MaterialHandle material;
            ResourceHandle colorMap;
            std::uniform_real_distribution<float> lifeExpectancy;
            std::uniform_real_distribution<float> size;
            std::vector<Particle> particles;

            Emitter(const Math::Color &color)
                : color(color)
            {
            }
        };

        struct Properties
        {
            union
            {
                uint64_t value;
                struct
                {
                    uint16_t buffer;
                    ResourceHandle colorMap;
                    MaterialHandle material;
                };
            };

            Properties(void)
                : value(0)
            {
            }

            Properties(MaterialHandle material, ResourceHandle colorMap)
                : material(material)
                , colorMap(colorMap)
                , buffer(0)
            {
            }

            std::size_t operator()(const Properties &properties) const
            {
                return std::hash<uint64_t>()(properties.value);
            }

            bool operator == (const Properties &properties) const
            {
                return (value == properties.value);
            }
        };

    private:
        Population *population;
        uint32_t updateHandle;
        PluginResources *resources;
        Render *render;

        PluginHandle plugin;
        ResourceHandle particleBuffer;

        ShuntingYard shuntingYard;
        typedef std::unordered_map<Entity *, Emitter> EntityEmitterMap;
        EntityEmitterMap entityEmitterMap;

        typedef concurrency::concurrent_unordered_multimap<Properties, const EntityEmitterMap::value_type *, Properties> VisibleList;
        VisibleList visibleList;

    public:
        ParticlesProcessorImplementation(Context *context, EngineContext *engine)
            : ContextRegistration(context)
            , population(engine->getPopulation())
            , updateHandle(0)
            , resources(engine->getResources())
            , render(engine->getRender())
        {
            population->addObserver((PopulationObserver *)this);
            updateHandle = population->setUpdatePriority(this, 60);
            render->addObserver((RenderObserver *)this);

            plugin = resources->loadPlugin(L"particles");

            particleBuffer = resources->createBuffer(nullptr, sizeof(Particle), ParticleBufferCount, Video::BufferType::Structured, Video::BufferFlags::Mappable | Video::BufferFlags::Resource);
        }

        ~ParticlesProcessorImplementation(void)
        {
            render->removeObserver((RenderObserver *)this);
            if (population)
            {
                population->removeUpdatePriority(updateHandle);
                population->removeObserver((PopulationObserver *)this);
            }
        }

        // PopulationObserver
        void onLoadSucceeded(void)
        {
        }

        void onLoadFailed(void)
        {
            onFree();
        }

        void onFree(void)
        {
            entityEmitterMap.clear();
        }

        template <float const &(*OPERATION)(float const &, float const &)>
        struct combinable
            : public concurrency::combinable<float>
        {
            combinable(float defaultValue)
                : concurrency::combinable<float>([&] {return defaultValue; })
            {
            }

            void set(float value)
            {
                auto &localValue = local();
                localValue = OPERATION(value, localValue);
            }

            float get(void)
            {
                return combine([](float left, float right) { return OPERATION(left, right); });
            }
        };

        void onEntityCreated(Entity *entity)
        {
            GEK_REQUIRE(resources);
            GEK_REQUIRE(entity);

            if (entity->hasComponents<ParticlesComponent, TransformComponent>())
            {
                auto &particlesComponent = entity->getComponent<ParticlesComponent>();
                auto &transformComponent = entity->getComponent<TransformComponent>();

                std::reference_wrapper<const Math::Color> color = Math::Color::White;
                if (entity->hasComponent<ColorComponent>())
                {
                    color = entity->getComponent<ColorComponent>().value;
                }

                auto &emitter = entityEmitterMap.insert(std::make_pair(entity, Emitter(color))).first->second;
                emitter.particles.resize(particlesComponent.density);
                emitter.material = resources->loadMaterial(String(L"Particles\\%v", particlesComponent.material));
                emitter.colorMap = resources->loadTexture(String(L"Particles\\%v", particlesComponent.colorMap), nullptr, 0);
                emitter.lifeExpectancy = std::uniform_real_distribution<float>(particlesComponent.lifeExpectancy.x, particlesComponent.lifeExpectancy.y);
                emitter.size = std::uniform_real_distribution<float>(particlesComponent.size.x, particlesComponent.size.y);
                concurrency::parallel_for_each(emitter.particles.begin(), emitter.particles.end(), [&](auto &particle) -> void
                {
                    particle.age = emitter.lifeExpectancy(mersineTwister);
                    particle.death = emitter.lifeExpectancy(mersineTwister);
                    particle.spin = (negativeOneToOne(mersineTwister) * Math::Pi);
                    particle.angle = (zeroToOne(mersineTwister) * Math::Pi * 2.0f);
                    particle.origin = transformComponent.position;
                    particle.offset.x = std::cos(particle.angle);
                    particle.offset.y = -std::sin(particle.angle);
                    particle.offset *= halfToOne(mersineTwister);
                    particle.size = emitter.size(mersineTwister);
                });
            }
        }

        void onEntityDestroyed(Entity *entity)
        {
            GEK_REQUIRE(entity);

            auto dataIterator = entityEmitterMap.find(entity);
            if (dataIterator != entityEmitterMap.end())
            {
                entityEmitterMap.erase(dataIterator);
            }
        }

        void onUpdate(uint32_t handle, bool isIdle)
        {
            GEK_TRACE_SCOPE(GEK_PARAMETER(handle), GEK_PARAMETER(isIdle));
            if (!isIdle)
            {
                float frameTime = population->getFrameTime();
                concurrency::parallel_for_each(entityEmitterMap.begin(), entityEmitterMap.end(), [&](EntityEmitterMap::value_type &data) -> void
                {
                    Entity *entity = data.first;
                    Emitter &emitter = data.second;
                    auto &transformComponent = entity->getComponent<TransformComponent>();

                    combinable<std::min<float>> minimum[3] = { (Math::Infinity), (Math::Infinity), (Math::Infinity) };
                    combinable<std::max<float>> maximum[3] = { (-Math::Infinity), (-Math::Infinity), (-Math::Infinity) };
                    concurrency::parallel_for_each(emitter.particles.begin(), emitter.particles.end(), [&](auto &particle) -> void
                    {
                        particle.age += frameTime;
                        if (particle.age >= particle.death)
                        {
                            particle.age = 0.0f;
                            particle.death = emitter.lifeExpectancy(mersineTwister);
                            particle.spin = (negativeOneToOne(mersineTwister) * Math::Pi);
                            particle.angle = (zeroToOne(mersineTwister) * Math::Pi * 2);
                            particle.origin = transformComponent.position;
                            particle.offset.x = std::cos(particle.angle);
                            particle.offset.y = -std::sin(particle.angle);
                            particle.offset *= halfToOne(mersineTwister);
                            particle.size = emitter.size(mersineTwister);
                        }

                        particle.angle += (particle.spin * frameTime);
                        minimum[0].set(particle.origin.x - 1.0f);
                        minimum[1].set(particle.origin.y);
                        minimum[2].set(particle.origin.z - 1.0f);
                        maximum[0].set(particle.origin.x + 1.0f);
                        maximum[1].set(particle.origin.y + emitter.lifeExpectancy.max());
                        maximum[2].set(particle.origin.z + 1.0f);
                    });

                    for (auto index : { 0, 1, 2 })
                    {
                        emitter.minimum[index] = minimum[index].get();
                        emitter.maximum[index] = maximum[index].get();
                    }
                });
            }
        }

        // RenderObserver
        static void drawCall(RenderContext *renderContext, PluginResources *resources, ResourceHandle colorMap, VisibleList::iterator visibleBegin, VisibleList::iterator visibleEnd, ResourceHandle particleBuffer)
        {
            GEK_REQUIRE(renderContext);
            GEK_REQUIRE(resources);

            resources->setResource(renderContext->vertexPipeline(), particleBuffer, 0);
            resources->setResource(renderContext->vertexPipeline(), colorMap, 1);

            uint32_t bufferCopied = 0;
            Particle *bufferData = nullptr;
            resources->mapBuffer(particleBuffer, (void **)&bufferData);
            for (auto emitterIterator = visibleBegin; emitterIterator != visibleEnd; ++emitterIterator)
            {
                const auto &emitter = emitterIterator->second->second;

                uint32_t particlesCopied = 0;
                uint32_t particlesCount = emitter.particles.size();
                const Particle *particleData = emitter.particles.data();
                while (particlesCopied < particlesCount)
                {
                    uint32_t bufferRemaining = (ParticleBufferCount - bufferCopied);
                    uint32_t particlesRemaining = (particlesCount - particlesCopied);
                    uint32_t copyCount = std::min(bufferRemaining, particlesRemaining);
                    memcpy(&bufferData[bufferCopied], &particleData[particlesCopied], (sizeof(Particle) * copyCount));

                    bufferCopied += copyCount;
                    particlesCopied += copyCount;
                    if (bufferCopied >= ParticleBufferCount)
                    {
                        resources->unmapBuffer(particleBuffer);
                        renderContext->drawPrimitive((ParticleBufferCount * 6), 0);
                        resources->mapBuffer(particleBuffer, (void **)&bufferData);
                        bufferCopied = 0;
                    }
                };
            }

            resources->unmapBuffer(particleBuffer);
            if (bufferCopied > 0)
            {
                renderContext->drawPrimitive((bufferCopied * 6), 0);
            }
        }

        void onRenderScene(Entity *cameraEntity, const Math::Float4x4 *viewMatrix, const Shapes::Frustum *viewFrustum)
        {
            GEK_TRACE_SCOPE();
            GEK_REQUIRE(render);
            GEK_REQUIRE(cameraEntity);
            GEK_REQUIRE(viewFrustum);

            visibleList.clear();
            concurrency::parallel_for_each(entityEmitterMap.begin(), entityEmitterMap.end(), [&](auto &data) -> void
            {
                Entity *entity = data.first;
                const Emitter &emitter = data.second;
                if (viewFrustum->isVisible(emitter))
                {
                    visibleList.insert(std::make_pair(Properties(emitter.material, emitter.colorMap), &data));
                }
            });

            for (auto propertiesIterator = visibleList.begin(); propertiesIterator != visibleList.end(); )
            {
                auto emittersIterator = visibleList.equal_range(propertiesIterator->first);
                render->queueDrawCall(plugin, propertiesIterator->first.material, std::bind(drawCall, std::placeholders::_1, resources, propertiesIterator->first.colorMap, emittersIterator.first, emittersIterator.second, particleBuffer));
                propertiesIterator = emittersIterator.second;
            }
        }
    };

    GEK_REGISTER_CONTEXT_USER(ParticlesProcessorImplementation)
}; // namespace Gek

