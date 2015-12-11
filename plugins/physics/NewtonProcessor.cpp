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
#include "GEK\Components\Size.h"
#include "GEK\Newton\Mass.h"
#include "GEK\Newton\DynamicBody.h"
#include "GEK\Newton\Player.h"
#include "GEK\Math\Common.h"
#include "GEK\Math\Matrix4x4.h"
#include "GEK\Shape\AlignedBox.h"
#include <Newton.h>
#include <dNewton.h>
#include <dNewtonPlayerManager.h>
#include <dNewtonCollision.h>
#include <dNewtonDynamicBody.h>
#include <concurrent_unordered_map.h>
#include <memory>
#include <map>

#ifdef _DEBUG
#pragma comment(lib, "newton_d.lib")
#pragma comment(lib, "dNewton_d.lib")
#pragma comment(lib, "dContainers_d.lib")
#else
#pragma comment(lib, "newton.lib")
#pragma comment(lib, "dNewton.lib")
#pragma comment(lib, "dContainers.lib")
#endif

namespace Gek
{
    class NewtonBodyMixin : virtual public UnknownMixin
    {
    private:
        Population *population;
        Entity *entity;

    public:
        NewtonBodyMixin(Population *population, Entity *entity)
            : population(population)
            , entity(entity)
        {
        }

        virtual ~NewtonBodyMixin(void)
        {
        }

        Population *getPopulation(void)
        {
            REQUIRE_RETURN(population, nullptr);
            return population;
        }

        Entity *getEntity(void) const
        {
            return entity;
        }

        // NewtonBodyMixin
        STDMETHOD_(NewtonBody *, getNewtonBody)     (THIS) const PURE;
    };

    class DynamicNewtonBody : public NewtonBodyMixin
        , public dNewtonDynamicBody
    {
    public:
        DynamicNewtonBody(Population *population, dNewton *newton, const dNewtonCollision* const newtonCollision, Entity *entity,
            const TransformComponent &transformComponent,
            const MassComponent &massComponent)
            : NewtonBodyMixin(population, entity)
            , dNewtonDynamicBody(newton, massComponent, newtonCollision, nullptr, Math::Float4x4(transformComponent.rotation, transformComponent.position).data, NULL)
        {
        }

        ~DynamicNewtonBody(void)
        {
        }

        // NewtonBodyMixin
        STDMETHODIMP_(NewtonBody *) getNewtonBody(void) const
        {
            return GetNewtonBody();
        }

        // dNewtonBody
        void OnBodyTransform(const dFloat* const newtonMatrix, int threadHandle)
        {
            const Math::Float4x4 &matrix = *reinterpret_cast<const Math::Float4x4 *>(newtonMatrix);
            auto &transformComponent = getEntity()->getComponent<TransformComponent>();
            transformComponent.position = matrix.translation;
            transformComponent.rotation = matrix;
        }

        // dNewtonDynamicBody
        void OnForceAndTorque(dFloat frameTime, int threadHandle)
        {
            auto &massComponent = getEntity()->getComponent<MassComponent>();
            AddForce(Math::Float4(0.0f, (-9.81f * massComponent), 0.0f, 0.0f).data);
        }
    };

    class PlayerNewtonBody : public NewtonBodyMixin
        , virtual public dNewtonPlayerManager::dNewtonPlayer
        , virtual public ActionObserver
    {
    private:
        IUnknown *action;

        float height;
        float jumpHeight;
        float moveSpeed;

        float viewAngle;
        bool moveForward;
        bool moveBackward;
        bool strafeLeft;
        bool strafeRight;
        float jumpCharge;
        DWORD jumpStart;
        bool crouching;

    public:
        PlayerNewtonBody(IUnknown *action, Population *population, dNewtonPlayerManager *newtonPlayerManager, Entity *entity,
            const TransformComponent &transformComponent,
            const MassComponent &massComponent,
            const PlayerComponent &playerComponent)
            : NewtonBodyMixin(population, entity)
            , action(action)
            , dNewtonPlayerManager::dNewtonPlayer(newtonPlayerManager, nullptr, massComponent, playerComponent.outerRadius, playerComponent.innerRadius,
                playerComponent.height, playerComponent.stairStep, Math::Float4(0.0f, 1.0f, 0.0f, 0.0f).data, Math::Float4(0.0f, 0.0f, -1.0f, 0.0f).data, 1)
            , height(playerComponent.height)
            , jumpHeight(playerComponent.height * 0.75f)
            , moveSpeed(5.0f)
            , viewAngle(0.0f)
            , moveForward(false)
            , moveBackward(false)
            , strafeLeft(false)
            , strafeRight(false)
            , jumpCharge(0.0f)
            , jumpStart(0)
            , crouching(false)
        {
            SetMatrix(Math::Float4x4(transformComponent.rotation, transformComponent.position).data);
        }

        ~PlayerNewtonBody(void)
        {
            ObservableMixin::removeObserver(action, getClass<ActionObserver>());
        }

        // IUnknown
        BEGIN_INTERFACE_LIST(PlayerNewtonBody)
            INTERFACE_LIST_ENTRY_COM(ActionObserver);
        END_INTERFACE_LIST_UNKNOWN

        // NewtonBodyMixin
        STDMETHODIMP_(NewtonBody *) getNewtonBody(void) const
        {
            return GetNewtonBody();
        }

        // dNewtonBody
        void OnBodyTransform(const dFloat* const newtonMatrix, int threadHandle)
        {
            Math::Float4x4 matrix(newtonMatrix);
            auto &transformComponent = getEntity()->getComponent<TransformComponent>();
            transformComponent.position = matrix.translation;
            transformComponent.rotation = matrix;
        }

        // dNewtonPlayerManager::dNewtonPlayer
        void OnPlayerMove(dFloat frameTime)
        {
            float lateralSpeed = ((moveForward ? -1.0f : 0.0f) + (moveBackward ? 1.0f : 0.0f)) * moveSpeed;
            float strafeSpeed = ((strafeLeft ? -1.0f : 0.0f) + (strafeRight ? 1.0f : 0.0f)) * moveSpeed;
            SetPlayerVelocity(lateralSpeed, strafeSpeed, jumpCharge, viewAngle, Math::Float4(0.0f, -9.81f, 0.0f, 0.0f)/*GetNewtonSystem()->GetGravity()*/.data, frameTime);
            if (jumpCharge > 0.0f)
            {
                jumpCharge = 0.0f;
            }
        }

        // ActionObserver
        STDMETHODIMP_(void) onState(LPCWSTR name, bool state)
        {
            if (_wcsicmp(name, L"crouch") == 0)
            {
                crouching = state;
            }
            else if (_wcsicmp(name, L"move_forward") == 0)
            {
                moveForward = state;
            }
            else if (_wcsicmp(name, L"move_backward") == 0)
            {
                moveBackward = state;
            }
            else if (_wcsicmp(name, L"strafe_left") == 0)
            {
                strafeLeft = state;
            }
            else if (_wcsicmp(name, L"strafe_right") == 0)
            {
                strafeRight = state;
            }
            else if (_wcsicmp(name, L"jump") == 0)
            {
                if (state)
                {
                    jumpStart = GetTickCount();
                }
                else
                {
                    jumpCharge = ((float(GetTickCount() - jumpStart) / 100.0f) * jumpHeight);
                }
            }
        }

        STDMETHODIMP_(void) onValue(LPCWSTR name, float value)
        {
            if (_wcsicmp(name, L"turn") == 0)
            {
                viewAngle += (value * 0.01f);
            }
        }
    };

    class NewtonProcessorImplementation : public ContextUserMixin
        , public ObservableMixin
        , public PopulationObserver
        , public Processor
        , public dNewton
    {
    public:
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
        IUnknown *action;

        dNewtonPlayerManager *newtonPlayerManager;

        Math::Float3 gravity;
        std::vector<Surface> surfaceList;
        std::map<std::size_t, INT32> surfaceIndexList;
        concurrency::concurrent_unordered_map<Entity *, CComPtr<IUnknown>> bodyList;
        std::unordered_map<std::size_t, std::unique_ptr<dNewtonCollision>> collisionList;

    public:
        NewtonProcessorImplementation(void)
            : population(nullptr)
            , action(nullptr)
            , gravity(0.0f, -9.8331f, 0.0f)
        {
        }

        ~NewtonProcessorImplementation(void)
        {
            ObservableMixin::removeObserver(population, getClass<PopulationObserver>());

            bodyList.clear();
            collisionList.clear();
            surfaceList.clear();
            surfaceIndexList.clear();
            DestroyAllBodies();
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

        INT32 getContactSurface(Entity *entity, NewtonBody *newtonBody, NewtonMaterial *newtonMaterial, const Math::Float3 &position, const Math::Float3 &normal)
        {
            REQUIRE_RETURN(population, -1);

            if (entity->hasComponent<DynamicBodyComponent>())
            {
                auto &dynamicBodyComponent = entity->getComponent<DynamicBodyComponent>();
                if (dynamicBodyComponent.surface.IsEmpty())
                {
                    NewtonCollision *newtonCollision = NewtonMaterialGetBodyCollidingShape(newtonMaterial, newtonBody);
                    if (newtonCollision)
                    {
                        dLong nAttribute = 0;
                        Math::Float3 nCollisionNormal;
                        NewtonCollisionRayCast(newtonCollision, (position - normal).data, (position + normal).data, nCollisionNormal.data, &nAttribute);
                        if (nAttribute > 0)
                        {
                            return INT32(nAttribute);
                        }
                    }
                }
                else
                {
                    return loadSurface(dynamicBodyComponent.surface);
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

        dNewtonCollision *createCollision(Entity *entity, const DynamicBodyComponent &dynamicBodyComponent)
        {
            REQUIRE_RETURN(population, nullptr);

            Math::Float3 size(1.0f, 1.0f, 1.0f);
            if (entity->hasComponent<SizeComponent>())
            {
                size.set(entity->getComponent<SizeComponent>());
            }

            CStringW shape(Gek::String::format(L"%s:%f,%f,%f", dynamicBodyComponent.shape.GetString(), size.x, size.y, size.z));
            std::size_t shapeHash = std::hash<CStringW>()(shape);

            dNewtonCollision *newtonCollision = nullptr;
            auto collisionIterator = collisionList.find(shapeHash);
            if (collisionIterator != collisionList.end())
            {
                if ((*collisionIterator).second)
                {
                    newtonCollision = (*collisionIterator).second.get();
                }
            }
            else
            {
                gekLogScope();

                collisionList[shapeHash].reset();
                if (dynamicBodyComponent.shape.CompareNoCase(L"*cube") == 0)
                {
                    newtonCollision = new dNewtonCollisionBox(this, size.x, size.y, size.z, 1);
                }
                else if (dynamicBodyComponent.shape.CompareNoCase(L"*sphere") == 0)
                {
                    newtonCollision = new dNewtonCollisionSphere(this, size.x, 1);
                }
                else if (dynamicBodyComponent.shape.CompareNoCase(L"*cone") == 0)
                {
                    newtonCollision = new dNewtonCollisionCone(this, size.x, size.y, 1);
                }
                else if (dynamicBodyComponent.shape.CompareNoCase(L"*capsule") == 0)
                {
                    newtonCollision = new dNewtonCollisionCapsule(this, size.x, size.y, 1);
                }
                else if (dynamicBodyComponent.shape.CompareNoCase(L"*cylinder") == 0)
                {
                    newtonCollision = new dNewtonCollisionCylinder(this, size.x, size.y, 1);
                }
                else if (dynamicBodyComponent.shape.CompareNoCase(L"*tapered_capsule") == 0)
                {
                    newtonCollision = new dNewtonCollisionTaperedCapsule(this, size.x, size.y, size.z, 1);
                }
                else if (dynamicBodyComponent.shape.CompareNoCase(L"*tapered_cylinder") == 0)
                {
                    newtonCollision = new dNewtonCollisionTaperedCylinder(this, size.x, size.y, size.z, 1);
                }
                else if (dynamicBodyComponent.shape.CompareNoCase(L"*chamfer_cylinder") == 0)
                {
                    newtonCollision = new dNewtonCollisionChamferedCylinder(this, size.x, size.y, 1);
                }

                if (newtonCollision)
                {
                    collisionList[shapeHash].reset(newtonCollision);
                }
            }

            return newtonCollision;
        }

        dNewtonCollision *loadCollision(Entity *entity, const DynamicBodyComponent &dynamicBodyComponent)
        {
            dNewtonCollision *newtonCollision = nullptr;
            if (dynamicBodyComponent.shape.GetAt(0) == L'*')
            {
                newtonCollision = createCollision(entity, dynamicBodyComponent);
            }
            else
            {
                std::size_t shapeHash = std::hash<CStringW>()(dynamicBodyComponent.shape);
                auto collisionIterator = collisionList.find(shapeHash);
                if (collisionIterator != collisionList.end())
                {
                    if ((*collisionIterator).second)
                    {
                        newtonCollision = (*collisionIterator).second.get();
                    }
                }
                else
                {
                    gekLogScope(dynamicBodyComponent.shape);

                    collisionList[shapeHash].reset();

                    std::vector<UINT8> fileData;
                    HRESULT resultValue = Gek::FileSystem::load(Gek::String::format(L"%%root%%\\data\\models\\%s.gek", dynamicBodyComponent.shape.GetString()), fileData);
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

                            if (materialList.empty())
                            {
                                std::vector<Math::Float3> pointCloudList(indexCount);
                                for (UINT32 index = 0; index < indexCount; ++index)
                                {
                                    pointCloudList[index] = vertexList[indexList[index]].position;
                                }

                                newtonCollision = new dNewtonCollisionConvexHull(this, pointCloudList.size(), pointCloudList[0].data, sizeof(Math::Float3), 0.025f, 1);
                            }
                            else
                            {
                                dNewtonCollisionMesh *newtonCollisionMesh = new dNewtonCollisionMesh(this, 1);
                                if (newtonCollisionMesh != nullptr)
                                {
                                    newtonCollisionMesh->BeginFace();
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

                                                newtonCollisionMesh->AddFace(3, face[0].data, sizeof(Math::Float3), surfaceIndex);
                                            }
                                        }
                                    }

                                    newtonCollisionMesh->EndFace();
                                    newtonCollision = newtonCollisionMesh;
                                }
                            }
                        }
                    }

                    if (newtonCollision)
                    {
                        collisionList[shapeHash].reset(newtonCollision);
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
                this->action = initializerContext;
                this->population = population;

                resultValue = ObservableMixin::addObserver(population, getClass<PopulationObserver>());
            }

            if (SUCCEEDED(resultValue))
            {
                newtonPlayerManager = new dNewtonPlayerManager(this);
                resultValue = (newtonPlayerManager ? S_OK : E_OUTOFMEMORY);
            }

            return resultValue;
        };

        // dNewton
        bool OnBodiesAABBOverlap(const dNewtonBody* const newtonBody0, const dNewtonBody* const newtonBody1, int threadHandle)
        {
            return true;
        }

        bool OnCompoundSubCollisionAABBOverlap(const dNewtonBody* const newtonBody0, const dNewtonCollision* const newtonCollision0, const dNewtonBody* const newtonBody1, const dNewtonCollision* const newtonCollision1, int threadHandle)
        {
            return true;
        }

        void OnContactProcess(dNewtonContactMaterial* const newtonContactMaterial, dFloat frameTime, int threadHandle)
        {
            NewtonBodyMixin *baseBody0 = dynamic_cast<NewtonBodyMixin *>(newtonContactMaterial->GetBody0());
            NewtonBodyMixin *baseBody1 = dynamic_cast<NewtonBodyMixin *>(newtonContactMaterial->GetBody1());
            if (baseBody0 && baseBody1)
            {
                NewtonWorldCriticalSectionLock(GetNewton(), threadHandle);
                for (void *newtonContact = newtonContactMaterial->GetFirstContact(); newtonContact; newtonContact = newtonContactMaterial->GetNextContact(newtonContact))
                {
                    NewtonMaterial *newtonMaterial = NewtonContactGetMaterial(newtonContact);

                    Math::Float3 position, normal;
                    NewtonMaterialGetContactPositionAndNormal(newtonMaterial, baseBody0->getNewtonBody(), position.data, normal.data);

                    INT32 surfaceIndex0 = getContactSurface(baseBody0->getEntity(), baseBody0->getNewtonBody(), newtonMaterial, position, normal);
                    INT32 surfaceIndex1 = getContactSurface(baseBody1->getEntity(), baseBody1->getNewtonBody(), newtonMaterial, position, normal);
                    const Surface &surface0 = getSurface(surfaceIndex0);
                    const Surface &surface1 = getSurface(surfaceIndex1);

                    NewtonMaterialSetContactSoftness(newtonMaterial, ((surface0.softness + surface1.softness) * 0.5f));
                    NewtonMaterialSetContactElasticity(newtonMaterial, ((surface0.elasticity + surface1.elasticity) * 0.5f));
                    NewtonMaterialSetContactFrictionCoef(newtonMaterial, surface0.staticFriction, surface0.kineticFriction, 0);
                    NewtonMaterialSetContactFrictionCoef(newtonMaterial, surface1.staticFriction, surface1.kineticFriction, 1);
                }
            }

            NewtonWorldCriticalSectionUnlock(GetNewton());
        }

        // PopulationObserver
        STDMETHODIMP_(void) onLoadBegin(void)
        {
        }

        STDMETHODIMP_(void) onLoadEnd(HRESULT resultValue)
        {
            if (FAILED(resultValue))
            {
                onFree();
            }
        }

        STDMETHODIMP_(void) onFree(void)
        {
            collisionList.clear();
            bodyList.clear();
            surfaceList.clear();
            surfaceIndexList.clear();
            DestroyAllBodies();
        }

        STDMETHODIMP_(void) onEntityCreated(Entity *entity)
        {
            REQUIRE_VOID_RETURN(population);

            if (entity->hasComponents<TransformComponent, MassComponent>())
            {
                auto &massComponent = entity->getComponent<MassComponent>();
                auto &transformComponent = entity->getComponent<TransformComponent>();
                if (entity->hasComponent<DynamicBodyComponent>())
                {
                    auto &dynamicBodyComponent = entity->getComponent<DynamicBodyComponent>();
                    dNewtonCollision *newtonCollision = loadCollision(entity, dynamicBodyComponent);
                    if (newtonCollision != nullptr)
                    {
                        Math::Float4x4 matrix(transformComponent.rotation, transformComponent.position);
                        CComPtr<IUnknown> dynamicBody = new DynamicNewtonBody(population, this, newtonCollision, entity, transformComponent, massComponent);
                        if (dynamicBody)
                        {
                            bodyList[entity] = dynamicBody;
                        }
                    }
                }
                else if (entity->hasComponent<PlayerComponent>())
                {
                    auto &playerComponent = entity->getComponent<PlayerComponent>();
                    CComPtr<PlayerNewtonBody> player = new PlayerNewtonBody(action, population, newtonPlayerManager, entity, transformComponent, massComponent, playerComponent);
                    if (player)
                    {
                        ObservableMixin::addObserver(action, player->getClass<ActionObserver>());
                        HRESULT resultValue = player.QueryInterface(&bodyList[entity]);
                    }
                }
            }
        }

        STDMETHODIMP_(void) onEntityDestroyed(Entity *entity)
        {
            auto bodyIterator = bodyList.find(entity);
            if (bodyIterator != bodyList.end())
            {
                bodyList.unsafe_erase(bodyIterator);
            }
        }

        STDMETHODIMP_(void) onUpdate(float frameTime)
        {
            if (frameTime > 0.0f)
            {
                UpdateOffLine(frameTime);
            }
        }
    };

    REGISTER_CLASS(NewtonProcessorImplementation)
}; // namespace Gek
