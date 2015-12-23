#include "GEK\Context\Common.h"
#include "GEK\Context\ContextUserMixin.h"
#include "GEK\Context\ObservableMixin.h"
#include "GEK\Utility\FileSystem.h"
#include "GEK\Utility\String.h"
#include "GEK\Utility\XML.h"
#include "GEK\Engine\Processor.h"
#include "GEK\Engine\Action.h"
#include "GEK\Engine\Population.h"
#include "GEK\Engine\Entity.h"
#include "GEK\Components\Transform.h"
#include "GEK\Components\Scale.h"
#include "GEK\Newton\Mass.h"
#include "GEK\Newton\RigidBody.h"
#include "GEK\Newton\StaticBody.h"
#include "GEK\Newton\PlayerBody.h"
#include "GEK\Newton\NewtonEntity.h"
#include "GEK\Math\Common.h"
#include "GEK\Math\Matrix4x4.h"
#include "GEK\Shape\AlignedBox.h"
#include <Newton.h>
#include <memory>
#include <map>
#include <set>

#ifdef _DEBUG
#pragma comment(lib, "newton_d.lib")
#else
#pragma comment(lib, "newton.lib")
#endif

namespace Gek
{
    static const Math::Float3 Gravity(0.0f, -32.174f, 0.0f);

    extern NewtonEntity *createPlayerBody(IUnknown *actionProvider, NewtonWorld *newtonWorld, Entity *entity, PlayerBodyComponent &playerBodyComponent, TransformComponent &transformComponent, MassComponent &massComponent);
    extern NewtonEntity *createRigidBody(NewtonWorld *newton, const NewtonCollision* const newtonCollision, Entity *entity, TransformComponent &transformComponent, MassComponent &massComponent);

    struct Surface
    {
        bool ghost;
        float staticFriction;
        float kineticFriction;
        float elasticity;
        float softness;

        Surface(void)
            : ghost(false)
            , staticFriction(0.9f)
            , kineticFriction(0.5f)
            , elasticity(0.4f)
            , softness(1.0f)
        {
        }
    };

    class NewtonProcessorImplementation : public ContextUserMixin
        , public ObservableMixin
        , public PopulationObserver
        , public Processor
    {
    public:
        struct Vertex
        {
            Math::Float3 position;
            Math::Float2 texCoord;
            Math::Float3 normal;
        };

        struct Material
        {
            UINT32 firstVertex;
            UINT32 firstIndex;
            UINT32 indexCount;
        };

    private:
        Population *population;
        UINT32 updateHandle;

        IUnknown *actionProvider;

        NewtonWorld *newtonWorld;
        NewtonCollision *newtonStaticScene;

        Math::Float3 gravity;
        std::vector<Surface> surfaceList;
        std::map<std::size_t, INT32> surfaceIndexList;
        std::unordered_map<Entity *, CComPtr<NewtonEntity>> entityMap;
        std::unordered_map<std::size_t, NewtonCollision *> collisionList;

    public:
        NewtonProcessorImplementation(void)
            : population(nullptr)
            , updateHandle(0)
            , actionProvider(nullptr)
            , newtonWorld(nullptr)
            , newtonStaticScene(nullptr)
            , gravity(0.0f, -9.8331f, 0.0f)
        {
        }

        ~NewtonProcessorImplementation(void)
        {
            onFree();
            population->removeUpdatePriority(updateHandle);
            ObservableMixin::removeObserver(population, getClass<PopulationObserver>());
        }

        BEGIN_INTERFACE_LIST(NewtonProcessorImplementation)
            INTERFACE_LIST_ENTRY_COM(Observable)
            INTERFACE_LIST_ENTRY_COM(PopulationObserver)
            INTERFACE_LIST_ENTRY_COM(Processor)
        END_INTERFACE_LIST_USER

        const Surface &getSurface(INT32 surfaceIndex) const
        {
            if (surfaceIndex >= 0 && surfaceIndex < int(surfaceList.size()))
            {
                return surfaceList[surfaceIndex];
            }
            else
            {
                static const Surface defaultSurface;
                return defaultSurface;
            }
        }

        INT32 getContactSurface(Entity *entity, const NewtonBody *const newtonBody, const NewtonMaterial *newtonMaterial, const Math::Float3 &position, const Math::Float3 &normal)
        {
            if (entity && entity->hasComponent<RigidBodyComponent>())
            {
                auto &rigidBodyComponent = entity->getComponent<RigidBodyComponent>();
                if (!rigidBodyComponent.surface.IsEmpty())
                {
                    return loadSurface(rigidBodyComponent.surface);
                }
            }

            NewtonCollision *newtonCollision = NewtonMaterialGetBodyCollidingShape(newtonMaterial, newtonBody);
            if (newtonCollision)
            {
                dLong surfaceAttribute = 0;
                Math::Float3 collisionNormal;
                NewtonCollisionRayCast(newtonCollision, (position - normal).data, (position + normal).data, collisionNormal.data, &surfaceAttribute);
                if (surfaceAttribute > 0)
                {
                    return INT32(surfaceAttribute);
                }
            }

            return -1;
        }

        INT32 loadSurface(LPCWSTR fileName)
        {
            REQUIRE_RETURN(fileName, -1);

            INT32 surfaceIndex = -1;
            std::size_t fileNameHash = std::hash<CStringW>()(fileName);
            auto surfaceIterator = surfaceIndexList.find(fileNameHash);
            if (surfaceIterator != surfaceIndexList.end())
            {
                surfaceIndex = (*surfaceIterator).second;
            }
            else
            {
                gekLogScope(fileName);

                surfaceIndexList[fileNameHash] = -1;

                Gek::XmlDocument xmlDocument;
                if (SUCCEEDED(xmlDocument.load(Gek::String::format(L"%%root%%\\data\\materials\\%s.xml", fileName))))
                {
                    Surface surface;
                    Gek::XmlNode xmlMaterialNode = xmlDocument.getRoot();
                    if (xmlMaterialNode && xmlMaterialNode.getType().CompareNoCase(L"material") == 0)
                    {
                        Gek::XmlNode xmlSurfaceNode = xmlMaterialNode.firstChildElement(L"surface");
                        if (xmlSurfaceNode)
                        {
                            surface.ghost = String::to<bool>(xmlSurfaceNode.getAttribute(L"ghost"));
                            if (xmlSurfaceNode.hasAttribute(L"staticfriction"))
                            {
                                surface.staticFriction = Gek::String::to<float>(xmlSurfaceNode.getAttribute(L"staticfriction"));
                            }

                            if (xmlSurfaceNode.hasAttribute(L"kineticfriction"))
                            {
                                surface.kineticFriction = Gek::String::to<float>(xmlSurfaceNode.getAttribute(L"kineticfriction"));
                            }

                            if (xmlSurfaceNode.hasAttribute(L"elasticity"))
                            {
                                surface.elasticity = Gek::String::to<float>(xmlSurfaceNode.getAttribute(L"elasticity"));
                            }

                            if (xmlSurfaceNode.hasAttribute(L"softness"))
                            {
                                surface.softness = Gek::String::to<float>(xmlSurfaceNode.getAttribute(L"softness"));
                            }

                            surfaceIndex = surfaceList.size();
                            surfaceIndexList[fileNameHash] = surfaceIndex;
                            surfaceList.push_back(surface);
                        }
                    }
                }
            }

            return surfaceIndex;
        }

        NewtonCollision *createCollision(Entity *entity, const CStringW &shape)
        {
            REQUIRE_RETURN(population, nullptr);

            Math::Float3 scale(1.0f, 1.0f, 1.0f);
            if (entity->hasComponent<ScaleComponent>())
            {
                scale.set(entity->getComponent<ScaleComponent>());
            }

            NewtonCollision *newtonCollision = nullptr;
            std::size_t collisionHash = std::hash<CStringW>()(shape);
            collisionHash = std::hash_combine(collisionHash, std::hash<float>()(scale.x), std::hash<float>()(scale.y), std::hash<float>()(scale.z));
            auto collisionIterator = collisionList.find(collisionHash);
            if (collisionIterator != collisionList.end())
            {
                if ((*collisionIterator).second)
                {
                    newtonCollision = (*collisionIterator).second;
                }
            }
            else
            {
                gekLogScope();

                int position = 0;
                CStringW shapeType(shape.Tokenize(L"|", position));
                CStringW parameters(shape.Tokenize(L"|", position));

                collisionList[collisionHash] = nullptr;
                if (shapeType.CompareNoCase(L"*cube") == 0)
                {
                    Math::Float3 size(String::to<Math::Float3>(parameters));
                    newtonCollision = NewtonCreateBox(newtonWorld, size.x, size.y, size.z, collisionHash, Math::Float4x4().data);
                }
                else if (shapeType.CompareNoCase(L"*sphere") == 0)
                {
                    float size = String::to<float>(parameters);
                    newtonCollision = NewtonCreateSphere(newtonWorld, size, collisionHash, Math::Float4x4().data);
                }
                else if (shapeType.CompareNoCase(L"*cone") == 0)
                {
                    Math::Float2 size(String::to<Math::Float2>(parameters));
                    newtonCollision = NewtonCreateCone(newtonWorld, size.x, size.y, collisionHash, Math::Float4x4().data);
                }
                else if (shapeType.CompareNoCase(L"*capsule") == 0)
                {
                    Math::Float2 size(String::to<Math::Float2>(parameters));
                    newtonCollision = NewtonCreateCapsule(newtonWorld, size.x, size.y, collisionHash, Math::Float4x4().data);
                }
                else if (shapeType.CompareNoCase(L"*cylinder") == 0)
                {
                    Math::Float2 size(String::to<Math::Float2>(parameters));
                    newtonCollision = NewtonCreateCylinder(newtonWorld, size.x, size.y, collisionHash, Math::Float4x4().data);
                }
                else if (shapeType.CompareNoCase(L"*tapered_capsule") == 0)
                {
                    Math::Float3 size(String::to<Math::Float3>(parameters));
                    newtonCollision = NewtonCreateTaperedCapsule(newtonWorld, size.x, size.y, size.z, collisionHash, Math::Float4x4().data);
                }
                else if (shapeType.CompareNoCase(L"*tapered_cylinder") == 0)
                {
                    Math::Float3 size(String::to<Math::Float3>(parameters));
                    newtonCollision = NewtonCreateTaperedCylinder(newtonWorld, size.x, size.y, size.z, collisionHash, Math::Float4x4().data);
                }
                else if (shapeType.CompareNoCase(L"*chamfer_cylinder") == 0)
                {
                    Math::Float2 size(String::to<Math::Float2>(parameters));
                    newtonCollision = NewtonCreateChamferCylinder(newtonWorld, size.x, size.y, collisionHash, Math::Float4x4().data);
                }

                if (newtonCollision)
                {
                    NewtonCollisionSetScale(newtonCollision, scale.x, scale.y, scale.z);
                    collisionList[collisionHash] = newtonCollision;
                }
            }

            return newtonCollision;
        }

        void loadModel(LPCWSTR model, std::function<void(std::map<CStringA, Material> &materialList, UINT32 vertexCount, Vertex *vertexList, UINT32 indexCount, UINT16 *indexList)> loadData)
        {
            std::vector<UINT8> fileData;
            HRESULT resultValue = Gek::FileSystem::load(Gek::String::format(L"%%root%%\\data\\models\\%s.gek", model), fileData);
            if (SUCCEEDED(resultValue))
            {
                UINT8 *rawFileData = fileData.data();
                UINT32 gekIdentifier = *((UINT32 *)rawFileData);
                rawFileData += sizeof(UINT32);

                UINT16 gekModelType = *((UINT16 *)rawFileData);
                rawFileData += sizeof(UINT16);

                UINT16 gekModelVersion = *((UINT16 *)rawFileData);
                rawFileData += sizeof(UINT16);

                if (gekIdentifier == *(UINT32 *)"GEKX" && gekModelType == 0 && gekModelVersion == 2)
                {
                    Gek::Shape::AlignedBox alignedBox = *(Gek::Shape::AlignedBox *)rawFileData;
                    rawFileData += sizeof(Gek::Shape::AlignedBox);

                    UINT32 materialCount = *((UINT32 *)rawFileData);
                    rawFileData += sizeof(UINT32);

                    std::map<CStringA, Material> materialList;
                    for (UINT32 surfaceIndex = 0; surfaceIndex < materialCount; ++surfaceIndex)
                    {
                        CStringA materialName = rawFileData;
                        rawFileData += (materialName.GetLength() + 1);

                        Material &material = materialList[materialName];
                        material.firstVertex = *((UINT32 *)rawFileData);
                        rawFileData += sizeof(UINT32);

                        material.firstIndex = *((UINT32 *)rawFileData);
                        rawFileData += sizeof(UINT32);

                        material.indexCount = *((UINT32 *)rawFileData);
                        rawFileData += sizeof(UINT32);
                    }

                    UINT32 vertexCount = *((UINT32 *)rawFileData);
                    rawFileData += sizeof(UINT32);

                    Vertex *vertexList = (Vertex *)rawFileData;
                    rawFileData += (sizeof(Vertex) * vertexCount);

                    UINT32 indexCount = *((UINT32 *)rawFileData);
                    rawFileData += sizeof(UINT32);

                    UINT16 *indexList = (UINT16 *)rawFileData;

                    loadData(materialList, vertexCount, vertexList, indexCount, indexList);
                }
            }
        }

        NewtonCollision *loadCollision(Entity *entity, const CStringW &shape)
        {
            NewtonCollision *newtonCollision = nullptr;
            if (shape.GetAt(0) == L'*')
            {
                newtonCollision = createCollision(entity, shape);
            }
            else
            {
                std::size_t shapeHash = std::hash<CStringW>()(shape);
                auto collisionIterator = collisionList.find(shapeHash);
                if (collisionIterator != collisionList.end())
                {
                    if ((*collisionIterator).second)
                    {
                        newtonCollision = (*collisionIterator).second;
                    }
                }
                else
                {
                    gekLogScope(shape);

                    collisionList[shapeHash] = nullptr;
                    loadModel(shape, [&](std::map<CStringA, Material> &materialList, UINT32 vertexCount, Vertex *vertexList, UINT32 indexCount, UINT16 *indexList) -> void
                    {
                        if (materialList.empty())
                        {
                            std::vector<Math::Float3> pointCloudList(indexCount);
                            for (UINT32 index = 0; index < indexCount; ++index)
                            {
                                pointCloudList[index] = vertexList[indexList[index]].position;
                            }

                            newtonCollision = NewtonCreateConvexHull(newtonWorld, pointCloudList.size(), pointCloudList[0].data, sizeof(Math::Float3), 0.025f, shapeHash, Math::Float4x4().data);
                        }
                        else
                        {
                            newtonCollision = NewtonCreateTreeCollision(newtonWorld, shapeHash);
                            if (newtonCollision != nullptr)
                            {
                                NewtonTreeCollisionBeginBuild(newtonCollision);
                                for (auto &materialPair : materialList)
                                {
                                    Material &material = materialPair.second;
                                    INT32 surfaceIndex = loadSurface(CA2W(materialPair.first, CP_UTF8));
                                    const Surface &surface = getSurface(surfaceIndex);
                                    if (!surface.ghost)
                                    {
                                        for (UINT32 index = 0; index < material.indexCount; index += 3)
                                        {
                                            Math::Float3 face[3] =
                                            {
                                                vertexList[material.firstVertex + indexList[material.firstIndex + index + 0]].position,
                                                vertexList[material.firstVertex + indexList[material.firstIndex + index + 1]].position,
                                                vertexList[material.firstVertex + indexList[material.firstIndex + index + 2]].position,
                                            };

                                            NewtonTreeCollisionAddFace(newtonCollision, 3, face[0].data, sizeof(Math::Float3), surfaceIndex);
                                        }
                                    }
                                }

#ifdef _DEBUG
                                NewtonTreeCollisionEndBuild(newtonCollision, 0);
#else
                                NewtonTreeCollisionEndBuild(newtonCollision, 1);
#endif
                            }
                        }
                    });

                    if (newtonCollision)
                    {
                        collisionList[shapeHash] = newtonCollision;
                    }
                }
            }

            return newtonCollision;
        }

        // System::Interface
        STDMETHODIMP initialize(IUnknown *initializerContext)
        {
            gekLogScope();

            REQUIRE_RETURN(initializerContext, E_INVALIDARG);

            HRESULT resultValue = E_FAIL;
            CComQIPtr<Population> population(initializerContext);
            if (population)
            {
                this->actionProvider = initializerContext;
                this->population = population;

                updateHandle = population->setUpdatePriority(this, 50);
                resultValue = ObservableMixin::addObserver(population, getClass<PopulationObserver>());
            }

            return resultValue;
        };

        void onPreUpdate(float timeStep)
        {
            for (auto &entityPair : entityMap)
            {
                entityPair.second->onPreUpdate(timeStep);
            }
        }

        static void newtonOnPreUpdate(const NewtonWorld* const world, void* const listenerUserData, dFloat timeStep)
        {
            NewtonProcessorImplementation *processor = static_cast<NewtonProcessorImplementation *>(listenerUserData);
            processor->onPreUpdate(timeStep);
        }

        void onPostUpdate(float timeStep)
        {
            for (auto &entityPair : entityMap)
            {
                entityPair.second->onPostUpdate(timeStep);
            }
        }

        static void newtonOnPostUpdate(const NewtonWorld* const world, void* const listenerUserData, dFloat timeStep)
        {
            NewtonProcessorImplementation *processor = static_cast<NewtonProcessorImplementation *>(listenerUserData);
            processor->onPostUpdate(timeStep);
        }

        static int newtonOnAABBOverlap(const NewtonMaterial* const material, const NewtonBody* const body0, const NewtonBody* const body1, int threadIndex)
        {
            return 1;
        }

        static void newtonOnContactFriction(const NewtonJoint* contactJoint, dFloat timeStep, int threadHandle)
        {
            const NewtonBody* const body0 = NewtonJointGetBody0(contactJoint);
            const NewtonBody* const body1 = NewtonJointGetBody1(contactJoint);

            NewtonWorld *newtonWorld = NewtonBodyGetWorld(body0);
            NewtonProcessorImplementation *processor = static_cast<NewtonProcessorImplementation *>(NewtonWorldGetUserData(newtonWorld));
            processor->onPostUpdate(timeStep);
        }

        void onContactFriction(const NewtonJoint* contactJoint, dFloat timeStep, int threadHandle)
        {
            const NewtonBody* const body0 = NewtonJointGetBody0(contactJoint);
            const NewtonBody* const body1 = NewtonJointGetBody1(contactJoint);
            NewtonEntity *newtonEntity0 = static_cast<NewtonEntity *>(NewtonBodyGetUserData(body0));
            NewtonEntity *newtonEntity1 = static_cast<NewtonEntity *>(NewtonBodyGetUserData(body1));

            NewtonWorldCriticalSectionLock(newtonWorld, threadHandle);
            for (void* newtonContact = NewtonContactJointGetFirstContact(contactJoint); newtonContact; newtonContact = NewtonContactJointGetNextContact(contactJoint, newtonContact))
            {
                NewtonMaterial *newtonMaterial = NewtonContactGetMaterial(newtonContact);

                Math::Float3 position, normal;
                NewtonMaterialGetContactPositionAndNormal(newtonMaterial, body0, position.data, normal.data);

                INT32 surfaceIndex0 = getContactSurface((newtonEntity0 ? newtonEntity0->getEntity() : nullptr), body0, newtonMaterial, position, normal);
                INT32 surfaceIndex1 = getContactSurface((newtonEntity1 ? newtonEntity1->getEntity() : nullptr), body1, newtonMaterial, position, normal);
                const Surface &surface0 = getSurface(surfaceIndex0);
                const Surface &surface1 = getSurface(surfaceIndex1);

                NewtonMaterialSetContactSoftness(newtonMaterial, ((surface0.softness + surface1.softness) * 0.5f));
                NewtonMaterialSetContactElasticity(newtonMaterial, ((surface0.elasticity + surface1.elasticity) * 0.5f));
                NewtonMaterialSetContactFrictionCoef(newtonMaterial, surface0.staticFriction, surface0.kineticFriction, 0);
                NewtonMaterialSetContactFrictionCoef(newtonMaterial, surface1.staticFriction, surface1.kineticFriction, 1);
            }

            NewtonWorldCriticalSectionUnlock(newtonWorld);
        }

        // PopulationObserver
        STDMETHODIMP_(void) onLoadBegin(void)
        {
            newtonWorld = NewtonCreate();
            NewtonWorldSetUserData(newtonWorld, this);
            auto preListener = NewtonWorldAddPreListener(newtonWorld, "core", this, newtonOnPreUpdate, nullptr);
            auto postListener = NewtonWorldAddPostListener(newtonWorld, "core", this, newtonOnPostUpdate, nullptr);

            int defaultMaterialID = NewtonMaterialGetDefaultGroupID(newtonWorld);
            NewtonMaterialSetCollisionCallback(newtonWorld, defaultMaterialID, defaultMaterialID, NULL, newtonOnAABBOverlap, newtonOnContactFriction);

            newtonStaticScene = NewtonCreateSceneCollision(newtonWorld, 1);
            if (newtonStaticScene)
            {
                NewtonSceneCollisionBeginAddRemove(newtonStaticScene);
            }
        }

        STDMETHODIMP_(void) onLoadEnd(HRESULT resultValue)
        {
            if (newtonStaticScene)
            {
                NewtonSceneCollisionEndAddRemove(newtonStaticScene);
                if (SUCCEEDED(resultValue))
                {
                    NewtonBody *newtonStaticBody = NewtonCreateDynamicBody(newtonWorld, newtonStaticScene, Math::Float4x4().data);
                    NewtonBodySetMassProperties(newtonStaticBody, 0.0f, newtonStaticScene);
                }
            }

            if (FAILED(resultValue))
            {
                onFree();
            }
        }

        STDMETHODIMP_(void) onFree(void)
        {
            if (newtonWorld)
            {
                NewtonWaitForUpdateToFinish(newtonWorld);
            }

            for (auto &collisionPair : collisionList)
            {
                NewtonDestroyCollision(collisionPair.second);
            }

            collisionList.clear();
            entityMap.clear();
            surfaceList.clear();
            surfaceIndexList.clear();

            if (newtonStaticScene)
            {
                NewtonDestroyCollision(newtonStaticScene);
                newtonStaticScene = nullptr;
            }

            if (newtonWorld)
            {
                NewtonDestroyAllBodies(newtonWorld);
                NewtonInvalidateCache(newtonWorld);
                NewtonDestroy(newtonWorld);
                newtonWorld = nullptr;
            }

            REQUIRE_VOID_RETURN(NewtonGetMemoryUsed() == 0);
        }

        STDMETHODIMP_(void) onEntityCreated(Entity *entity)
        {
            REQUIRE_VOID_RETURN(entity);

            if (entity->hasComponents<TransformComponent>())
            {
                auto &transformComponent = entity->getComponent<TransformComponent>();
                if (entity->hasComponent<StaticBodyComponent>())
                {
                    auto &staticBodyComponent = entity->getComponent<StaticBodyComponent>();
                    NewtonCollision *newtonCollision = loadCollision(entity, staticBodyComponent.shape);
                    if (newtonCollision != nullptr)
                    {
                        NewtonCollision *clonedCollision = NewtonCollisionCreateInstance(newtonCollision);
                        NewtonCollisionSetMatrix(clonedCollision, Math::Float4x4::createMatrix(transformComponent.rotation, transformComponent.position).data);
                        NewtonSceneCollisionAddSubCollision(newtonStaticScene, clonedCollision);
                        NewtonDestroyCollision(clonedCollision);
                    }
                }
                else if (entity->hasComponents<MassComponent>())
                {
                    auto &massComponent = entity->getComponent<MassComponent>();
                    if (entity->hasComponent<RigidBodyComponent>())
                    {
                        auto &rigidBodyComponent = entity->getComponent<RigidBodyComponent>();
                        NewtonCollision *newtonCollision = loadCollision(entity, rigidBodyComponent.shape);
                        if (newtonCollision != nullptr)
                        {
                            CComPtr<NewtonEntity> rigidBody = createRigidBody(newtonWorld, newtonCollision, entity, transformComponent, massComponent);
                            if (rigidBody)
                            {
                                entityMap[entity] = rigidBody;
                            }
                        }
                    }
                    else if (entity->hasComponent<PlayerBodyComponent>())
                    {
                        auto &playerBodyComponent = entity->getComponent<PlayerBodyComponent>();
                        CComPtr<NewtonEntity> playerBody = createPlayerBody(actionProvider, newtonWorld, entity, playerBodyComponent, transformComponent, massComponent);
                        if (playerBody)
                        {
                            entityMap[entity] = playerBody;
                        }
                    }
                }
            }
        }

        STDMETHODIMP_(void) onEntityDestroyed(Entity *entity)
        {
            auto entityIterator = entityMap.find(entity);
            if (entityIterator != entityMap.end())
            {
                entityMap.erase(entityIterator);
            }
        }

        STDMETHODIMP_(void) onUpdate(float frameTime)
        {
            if (newtonWorld && frameTime > 0.0f)
            {
                NewtonUpdateAsync(newtonWorld, frameTime);
            }
        }
    };

    REGISTER_CLASS(NewtonProcessorImplementation)
}; // namespace Gek
