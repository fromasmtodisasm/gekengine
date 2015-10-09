﻿#include "GEK\Math\Matrix4x4.h"
#include "GEK\Shape\AlignedBox.h"
#include "GEK\Shape\OrientedBox.h"
#include "GEK\Utility\FileSystem.h"
#include "GEK\Utility\String.h"
#include "GEK\Utility\XML.h"
#include "GEK\Context\Common.h"
#include "GEK\Context\UserMixin.h"
#include "GEK\Context\ObservableMixin.h"
#include "GEK\System\VideoInterface.h"
#include "GEK\Engine\SystemInterface.h"
#include "GEK\Engine\PopulationInterface.h"
#include "GEK\Engine\RenderInterface.h"
#include "GEK\Components\Transform.h"
#include "GEK\Components\Size.h"
#include "GEK\Components\Color.h"
#include "GEK\Engine\Model.h"
#include <concurrent_unordered_map.h>
#include <concurrent_vector.h>
#include <memory>
#include <map>
#include <set>
#include <ppl.h>
#include <algorithm>
#include <array>

namespace Gek
{
    struct Vertex
    {
        Math::Float3 position;
        Math::Float2 texCoord;
        Math::Float3 normal;

        Vertex(const Math::Float3 &position)
            : position(position)
            , normal(position.getNormal())
        {
            texCoord.u = std::acos(position.x / position.getLength());
            texCoord.v = std::atan(position.y / position.z);
        }
    };

    class GeoSphere
    {
    private:
        struct Edge
        {
            bool split; // whether this edge has been split
            std::array <size_t, 2> vertices; // the two endpoint vertex indices
            size_t splitVertex; // the split vertex index
            std::array <size_t, 2> splitEdges; // the two edges after splitting - they correspond to the "vertices" vector

            Edge(size_t v0, size_t v1)
                : vertices{ v0, v1 }
            {
                split = false;
            }

            size_t getSplitEdge(size_t vertex)
            {
                if (vertex == vertices[0])
                    return splitEdges[0];
                else if (vertex == vertices[1])
                    return splitEdges[1];
                else
                    return 0;
            }
        };

        struct Triangle
        {
            std::array <size_t, 3> vertices; // the vertex indices
            std::array <size_t, 3> edges; // the edge indices

            Triangle(size_t v0, size_t v1, size_t v2, size_t e0, size_t e1, size_t e2)
                : vertices{ v0, v1, v2 }
                , edges{ e0, e1, e2 }
            {
            }
        };

        std::vector <Vertex> vertices;
        std::vector <Edge> edges;
        std::vector <Edge> newEdges;
        std::vector <Triangle> triangles;
        std::vector <Triangle> newTriangles;

        std::vector <UINT16> indices;
        float inscriptionRadiusMultiplier;

        void splitEdge(Edge & e)
        {
            if (e.split)
            {
                return;
            }

            e.splitVertex = vertices.size();
            vertices.push_back(Vertex((Math::Float3(0.5f) * (vertices[e.vertices[0]].position + vertices[e.vertices[1]].position)).getNormal()));
            e.splitEdges = { newEdges.size(), newEdges.size() + 1 };
            newEdges.push_back(Edge(e.vertices[0], e.splitVertex));
            newEdges.push_back(Edge(e.splitVertex, e.vertices[1]));
            e.split = true;
        }

        void subdivideTriangle(const Triangle & t)
        {
            //    0
            //    /\
            //  0/  \2
            //  /____\
            // 1  1   2
            size_t edge0 = t.edges[0];
            size_t edge1 = t.edges[1];
            size_t edge2 = t.edges[2];
            size_t vert0 = t.vertices[0];
            size_t vert1 = t.vertices[1];
            size_t vert2 = t.vertices[2];
            splitEdge(edges[edge0]);
            splitEdge(edges[edge1]);
            splitEdge(edges[edge2]);
            size_t edge01 = newEdges.size();
            size_t edge12 = newEdges.size() + 1;
            size_t edge20 = newEdges.size() + 2;
            newEdges.push_back(Edge(edges[edge0].splitVertex, edges[edge1].splitVertex));
            newEdges.push_back(Edge(edges[edge1].splitVertex, edges[edge2].splitVertex));
            newEdges.push_back(Edge(edges[edge2].splitVertex, edges[edge0].splitVertex));

            // important: we push the "center" triangle first
            // this is so a center-most triangle is guaranteed to be at index 0 of the final object
            newTriangles.push_back(Triangle(edges[edge0].splitVertex, edges[edge1].splitVertex, edges[edge2].splitVertex, edge01, edge12, edge20));

            newTriangles.push_back(Triangle(vert0, edges[edge0].splitVertex, edges[edge2].splitVertex, edges[edge0].getSplitEdge(vert0), edge20, edges[edge2].getSplitEdge(vert0)));

            newTriangles.push_back(Triangle(edges[edge0].splitVertex, vert1, edges[edge1].splitVertex, edges[edge0].getSplitEdge(vert1), edges[edge1].getSplitEdge(vert1), edge01));

            newTriangles.push_back(Triangle(edges[edge2].splitVertex, edges[edge1].splitVertex, vert2, edge12, edges[edge1].getSplitEdge(vert2), edges[edge2].getSplitEdge(vert2)));
        }

        void subdivide()
        {
            for (size_t i = 0; i < triangles.size(); ++i)
            {
                subdivideTriangle(triangles[i]);
            }

            edges.swap(newEdges);
            triangles.swap(newTriangles);
            newEdges.clear();
            newTriangles.clear();
        }

        float computeInscriptionRadiusMultiplier() const
        {
            // all triangles have 3 points with norm 1
            // this means for each triangle, the point on the plane
            // with the smallest norm is the centroid (average) of
            // the three vertices. Thus, since all vertices have
            // norm 1, the largest triangle will have a centroid
            // with mean closest to 0. Each time we subdivide a triangle,
            // we get 3 triangles with 2 points "pushed out" (the surrounding
            // triangles) and one triangle with all 3 points "pushed out"
            // (the center triangle). Since pushing out a point makes the
            // triangle bigger, the biggest triangle must be the
            // center-most triangle - i.e. take the path of the center
            // triangle in each subdivision to get the biggest triangle.
            // In the subdivideTriangle() method, we ensured the center
            // triangle is always pushed to the new triangle array first -
            // thus, one of the center triangles must have index 0. Therefore
            // we compute the centroid of the triangle with index 0
            // and take its norm. If we divide by this value, "expand" the
            // sphere to contain a given radius rather than be contained by
            // a given radius. However, since division is slower, we'd rather
            // multiply, so we return the reciprocal.
            // 1/3 times the sum of the three vertices
            static const Math::Float3 oneThird(1.0f / 3.0f);
            Math::Float3 centroid = oneThird * (vertices[triangles[0].vertices[0]].position + vertices[triangles[0].vertices[1]].position + vertices[triangles[0].vertices[2]].position);
            return 1.0f / centroid.getLength();
        }

    public:
        GeoSphere()
        {
        }

        void generate(size_t subdivisions)
        {
            static const float PHI = 1.618033988749894848204586834365638f;

            static const Vertex initialVertices[] =
            {
                Vertex(Math::Float3(-1.0f,  0.0f,   PHI).getNormal()),  // 0
                Vertex(Math::Float3( 1.0f,  0.0f,   PHI).getNormal()),  // 1
                Vertex(Math::Float3( 0.0f,   PHI,  1.0f).getNormal()),  // 2
                Vertex(Math::Float3( -PHI,  1.0f,  0.0f).getNormal()),  // 3
                Vertex(Math::Float3( -PHI, -1.0f,  0.0f).getNormal()),  // 4
                Vertex(Math::Float3( 0.0f,  -PHI,  1.0f).getNormal()),  // 5
                Vertex(Math::Float3(  PHI, -1.0f,  0.0f).getNormal()),  // 6
                Vertex(Math::Float3(  PHI,  1.0f,  0.0f).getNormal()),  // 7
                Vertex(Math::Float3( 0.0f,   PHI, -1.0f).getNormal()),  // 8
                Vertex(Math::Float3(-1.0f,  0.0f,  -PHI).getNormal()),  // 9
                Vertex(Math::Float3( 0.0f,  -PHI, -1.0f).getNormal()),  // 10
                Vertex(Math::Float3( 1.0f,  0.0f,  -PHI).getNormal()),  // 11
            };

            static const Edge initialEdges[] =
            {
                Edge( 0,  1),   // 0
                Edge( 0,  2),   // 1
                Edge( 0,  3),   // 2
                Edge( 0,  4),   // 3
                Edge( 0,  5),   // 4
                Edge( 1,  2),   // 5
                Edge( 2,  3),   // 6
                Edge( 3,  4),   // 7
                Edge( 4,  5),   // 8
                Edge( 5,  1),   // 9
                Edge( 5,  6),   // 10
                Edge( 6,  1),   // 11
                Edge( 1,  7),   // 12
                Edge( 7,  2),   // 13
                Edge( 2,  8),   // 14
                Edge( 8,  3),   // 15
                Edge( 3,  9),   // 16
                Edge( 9,  4),   // 17
                Edge( 4, 10),   // 18
                Edge(10,  5),   // 19
                Edge(10,  6),   // 20
                Edge( 6,  7),   // 21
                Edge( 7,  8),   // 22
                Edge( 8,  9),   // 23
                Edge( 9, 10),   // 24
                Edge(10, 11),   // 25
                Edge( 6, 11),   // 26
                Edge( 7, 11),   // 27
                Edge( 8, 11),   // 28
                Edge( 9, 11),   // 29
            };

            static const Triangle initialTriangles[] =
            {
                Triangle( 0,  1,  2,  0,  5,  1),
                Triangle( 0,  2,  3,  1,  6,  2),
                Triangle( 0,  3,  4,  2,  7,  3),
                Triangle( 0,  4,  5,  3,  8,  4),
                Triangle( 0,  5,  1,  4,  9,  0),
                Triangle( 1,  6,  7, 11, 21, 12),
                Triangle( 1,  7,  2, 12, 13,  5),
                Triangle( 2,  7,  8, 13, 22, 14),
                Triangle( 2,  8,  3, 14, 15,  6),
                Triangle( 3,  8,  9, 15, 23, 16),
                Triangle( 3,  9,  4, 16, 17,  7),
                Triangle( 4,  9, 10, 17, 24, 18),
                Triangle( 4, 10,  5, 18, 19,  8),
                Triangle( 5, 10,  6, 19, 20, 10),
                Triangle( 5,  6,  1, 10, 11,  9),
                Triangle( 6, 11,  7, 26, 27, 21),
                Triangle( 7, 11,  8, 27, 28, 22),
                Triangle( 8, 11,  9, 28, 29, 23),
                Triangle( 9, 11, 10, 29, 25, 24),
                Triangle(10, 11,  6, 25, 26, 20),
            };

            vertices.clear();
            edges.clear();
            triangles.clear();

            size_t vertexCount = ARRAYSIZE(initialVertices);
            size_t edgeCount = ARRAYSIZE(initialEdges);
            size_t triangleCount = ARRAYSIZE(initialTriangles);

            // reserve space
            for (size_t i = 0; i < subdivisions; ++i)
            {
                vertexCount += edgeCount;
                edgeCount = edgeCount * 2 + triangleCount * 3;
                triangleCount *= 4;
            }

            vertices.reserve(vertexCount);
            edges.reserve(edgeCount);
            newEdges.reserve(edgeCount);
            triangles.reserve(triangleCount);
            newTriangles.reserve(triangleCount);

            vertices.assign(initialVertices, initialVertices + ARRAYSIZE(initialVertices));
            edges.assign(initialEdges, initialEdges + ARRAYSIZE(initialEdges));
            triangles.assign(initialTriangles, initialTriangles + ARRAYSIZE(initialTriangles));

            for (size_t i = 0; i < subdivisions; ++i)
            {
                subdivide();
            }

            inscriptionRadiusMultiplier = computeInscriptionRadiusMultiplier();

            // now we create the array of indices
            indices.reserve(triangles.size() * 3);
            for (size_t i = 0; i < triangles.size(); ++i)
            {
                indices.push_back((UINT16)triangles[i].vertices[0]);
                indices.push_back((UINT16)triangles[i].vertices[1]);
                indices.push_back((UINT16)triangles[i].vertices[2]);
            }

            // clear out all the extra memory
            edges.clear();
            edges.shrink_to_fit();
            newEdges.clear();
            newEdges.shrink_to_fit();
            triangles.clear();
            triangles.shrink_to_fit();
        }

        const std::vector <Vertex> & getVertices() const
        {
            return vertices;
        }

        const std::vector <UINT16> & getIndices() const
        {
            return indices;
        }

        float getInscriptionRadiusMultiplier() const
        {
            return inscriptionRadiusMultiplier;
        }
    };

    namespace Model
    {
        static const UINT32 MaxInstanceCount = 500;

        class System : public Context::User::Mixin
            , public Observable::Mixin
            , public Engine::Population::Observer
            , public Engine::Render::Observer
            , public Engine::System::Interface
        {
        public:
            struct MaterialInfo
            {
                CComPtr<IUnknown> material;
                UINT32 firstVertex;
                UINT32 firstIndex;
                UINT32 indexCount;

                MaterialInfo(void)
                    : firstVertex(0)
                    , firstIndex(0)
                    , indexCount(0)
                {
                }
            };

            struct ModelData
            {
                CStringW parameters;
                Shape::AlignedBox alignedBox;
                CComPtr<Video::Buffer::Interface> vertexBuffer;
                CComPtr<Video::Buffer::Interface> indexBuffer;
                std::vector<MaterialInfo> materialInfoList;

                std::function<HRESULT(ModelData &)> load;

                ModelData(void)
                {
                }
            };

            struct InstanceData
            {
                Math::Float4x4 matrix;
                Math::Float4 color;
                Math::Float3 size;
                float distance;

                InstanceData(const Math::Float4x4 &matrix, const Math::Float4 &color, const Math::Float3 &size, float distance)
                    : matrix(matrix)
                    , color(color)
                    , size(size)
                    , distance(distance)
                {
                }
            };

        private:
            Video::Interface *video;
            Engine::Render::Interface *render;
            Engine::Population::Interface *population;

            CComPtr<IUnknown> plugin;

            concurrency::concurrent_unordered_map<CStringW, ModelData> dataMap;
            concurrency::concurrent_unordered_map<Engine::Population::Entity, ModelData *> dataEntityList;
            CComPtr<Video::Buffer::Interface> instanceBuffer;

        public:
            System(void)
                : render(nullptr)
                , video(nullptr)
                , population(nullptr)
            {
            }

            ~System(void)
            {
                Observable::Mixin::removeObserver(render, getClass<Engine::Render::Observer>());
                Observable::Mixin::removeObserver(population, getClass<Engine::Population::Observer>());
            }

            BEGIN_INTERFACE_LIST(System)
                INTERFACE_LIST_ENTRY_COM(Observable::Interface)
                INTERFACE_LIST_ENTRY_COM(Engine::Population::Observer)
                INTERFACE_LIST_ENTRY_COM(Engine::Render::Observer)
                INTERFACE_LIST_ENTRY_COM(Engine::System::Interface)
            END_INTERFACE_LIST_USER

            HRESULT loadShape(ModelData &data)
            {
                data.load = nullptr;

                int position = 0;
                CStringW shape = data.parameters.Tokenize(L"|", position);
                CStringW materialName = data.parameters.Tokenize(L"|", position);

                CComPtr<IUnknown> material;
                HRESULT resultValue = render->loadMaterial(&material, materialName);
                if (material)
                {
                    if (shape.CompareNoCase(L"cube") == 0)
                    {
                        Math::Float3 size(String::getFloat3(data.parameters.Tokenize(L"|", position)));
                    }
                    else if (shape.CompareNoCase(L"sphere") == 0)
                    {
                        UINT32 divisionCount = String::getUINT32(data.parameters.Tokenize(L"|", position));

                        GeoSphere geoSphere;
                        geoSphere.generate(divisionCount);

                        MaterialInfo materialInfo;
                        materialInfo.firstVertex = 0;
                        materialInfo.firstIndex = 0;
                        materialInfo.indexCount = geoSphere.getIndices().size();
                        materialInfo.material = material;

                        data.materialInfoList.push_back(materialInfo);

                        resultValue = video->createBuffer(&data.vertexBuffer, sizeof(Vertex), geoSphere.getVertices().size(), Video::BufferFlags::VertexBuffer | Video::BufferFlags::Static, geoSphere.getVertices().data());
                        if (SUCCEEDED(resultValue))
                        {
                            resultValue = video->createBuffer(&data.indexBuffer, Video::Format::Short, geoSphere.getIndices().size(), Video::BufferFlags::IndexBuffer | Video::BufferFlags::Static, geoSphere.getIndices().data());
                        }
                    }
                    else
                    {
                        resultValue = E_FAIL;
                    }
                }

                return resultValue;
            }

            HRESULT preLoadShape(ModelData &data)
            {
                HRESULT resultValue = S_OK;

                int position = 0;
                CStringW shape = data.parameters.Tokenize(L"|", position);
                CStringW material = data.parameters.Tokenize(L"|", position);
                if (shape.CompareNoCase(L"cube") == 0)
                {
                    Math::Float3 size(String::getFloat3(data.parameters.Tokenize(L"|", position)));
                    data.alignedBox.minimum = -Math::Float3(size * Math::Float3(0.5f));
                    data.alignedBox.maximum =  Math::Float3(size * Math::Float3(0.5f));
                }
                else if (shape.CompareNoCase(L"sphere") == 0)
                {
                    float radius = String::getFloat(data.parameters.Tokenize(L"|", position));
                    data.alignedBox.minimum = Math::Float3(-radius);
                    data.alignedBox.maximum = Math::Float3(radius);
                }
                else
                {
                    resultValue = E_FAIL;
                }

                if (SUCCEEDED(resultValue))
                {
                    data.load = std::bind(&System::loadShape, this, std::placeholders::_1);
                }

                return resultValue;
            }

            HRESULT loadModel(ModelData &data)
            {
                data.load = nullptr;

                std::vector<UINT8> fileData;
                HRESULT resultValue = Gek::FileSystem::load(data.parameters, fileData);
                if (SUCCEEDED(resultValue))
                {
                    UINT8 *rawFileData = fileData.data();
                    UINT32 gekIdentifier = *((UINT32 *)rawFileData);
                    rawFileData += sizeof(UINT32);

                    UINT16 gekModelType = *((UINT16 *)rawFileData);
                    rawFileData += sizeof(UINT16);

                    UINT16 gekModelVersion = *((UINT16 *)rawFileData);
                    rawFileData += sizeof(UINT16);

                    resultValue = E_INVALIDARG;
                    if (gekIdentifier == *(UINT32 *)"GEKX" && gekModelType == 0 && gekModelVersion == 2)
                    {
                        data.alignedBox = *(Gek::Shape::AlignedBox *)rawFileData;
                        rawFileData += sizeof(Gek::Shape::AlignedBox);

                        UINT32 materialCount = *((UINT32 *)rawFileData);
                        rawFileData += sizeof(UINT32);

                        resultValue = S_OK;
                        data.materialInfoList.resize(materialCount);
                        for (UINT32 materialIndex = 0; materialIndex < materialCount; ++materialIndex)
                        {
                            CStringA materialNameUtf8(rawFileData);
                            rawFileData += (materialNameUtf8.GetLength() + 1);

                            MaterialInfo &materialInfo = data.materialInfoList[materialIndex];
                            resultValue = render->loadMaterial(&materialInfo.material, CA2W(materialNameUtf8, CP_UTF8));
                            if (!materialInfo.material)
                            {
                                resultValue = E_FAIL;
                                break;
                            }

                            materialInfo.firstVertex = *((UINT32 *)rawFileData);
                            rawFileData += sizeof(UINT32);

                            materialInfo.firstIndex = *((UINT32 *)rawFileData);
                            rawFileData += sizeof(UINT32);

                            materialInfo.indexCount = *((UINT32 *)rawFileData);
                            rawFileData += sizeof(UINT32);
                        }

                        if (SUCCEEDED(resultValue))
                        {
                            UINT32 vertexCount = *((UINT32 *)rawFileData);
                            rawFileData += sizeof(UINT32);

                            resultValue = video->createBuffer(&data.vertexBuffer, sizeof(Vertex), vertexCount, Video::BufferFlags::VertexBuffer | Video::BufferFlags::Static, rawFileData);
                            rawFileData += (sizeof(Vertex) * vertexCount);
                        }

                        if (SUCCEEDED(resultValue))
                        {
                            UINT32 indexCount = *((UINT32 *)rawFileData);
                            rawFileData += sizeof(UINT32);

                            resultValue = video->createBuffer(&data.indexBuffer, Video::Format::Short, indexCount, Video::BufferFlags::IndexBuffer | Video::BufferFlags::Static, rawFileData);
                            rawFileData += (sizeof(UINT16) * indexCount);
                        }
                    }
                }

                return resultValue;
            }

            HRESULT preLoadModel(ModelData &data)
            {
                static const UINT32 nPreReadSize = (sizeof(UINT32) + sizeof(UINT16) + sizeof(UINT16) + sizeof(Shape::AlignedBox));

                std::vector<UINT8> fileData;
                HRESULT resultValue = Gek::FileSystem::load(data.parameters, fileData, nPreReadSize);
                if (SUCCEEDED(resultValue))
                {
                    UINT8 *rawFileData = fileData.data();
                    UINT32 gekIdentifier = *((UINT32 *)rawFileData);
                    rawFileData += sizeof(UINT32);

                    UINT16 gekModelType = *((UINT16 *)rawFileData);
                    rawFileData += sizeof(UINT16);

                    UINT16 gekModelVersion = *((UINT16 *)rawFileData);
                    rawFileData += sizeof(UINT16);

                    resultValue = E_INVALIDARG;
                    if (gekIdentifier == *(UINT32 *)"GEKX" && gekModelType == 0 && gekModelVersion == 2)
                    {
                        data.alignedBox = *(Gek::Shape::AlignedBox *)rawFileData;
                        resultValue = S_OK;
                    }
                }

                if (SUCCEEDED(resultValue))
                {
                    data.load = std::bind(&System::loadModel, this, std::placeholders::_1);
                }

                return resultValue;
            }

            HRESULT preLoadData(LPCWSTR model, ModelData &data)
            {
                gekLogScope(__FUNCTION__);

                REQUIRE_RETURN(model, E_INVALIDARG);

                HRESULT resultValue = E_FAIL;
                if (*model == L'*')
                {
                    data.parameters = (model + 1);
                    resultValue = preLoadShape(data);
                }
                else
                {
                    data.parameters.Format(L"%%root%%\\data\\models\\%s.gek", model);
                    resultValue = preLoadModel(data);
                }

                return resultValue;
            }

            // System::Interface
            STDMETHODIMP initialize(IUnknown *initializerContext)
            {
                gekLogScope(__FUNCTION__);

                REQUIRE_RETURN(initializerContext, E_INVALIDARG);

                HRESULT resultValue = E_FAIL;
                CComQIPtr<Video::Interface> video(initializerContext);
                CComQIPtr<Engine::Render::Interface> render(initializerContext);
                CComQIPtr<Engine::Population::Interface> population(initializerContext);
                if (render && video && population)
                {
                    this->video = video;
                    this->render = render;
                    this->population = population;
                    resultValue = Observable::Mixin::addObserver(population, getClass<Engine::Population::Observer>());
                }

                if (SUCCEEDED(resultValue))
                {
                    resultValue = Observable::Mixin::addObserver(render, getClass<Engine::Render::Observer>());
                }

                if (SUCCEEDED(resultValue))
                {
                    resultValue = render->loadPlugin(&plugin, L"model");
                }

                if (SUCCEEDED(resultValue))
                {
                    resultValue = video->createBuffer(&instanceBuffer, sizeof(InstanceData), 1024, Video::BufferFlags::VertexBuffer | Video::BufferFlags::Dynamic);
                }

                return resultValue;
            };

            // Engine::Population::Observer
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
                REQUIRE_VOID_RETURN(video);

                dataMap.clear();
                dataEntityList.clear();
            }

            STDMETHODIMP_(void) onEntityCreated(const Engine::Population::Entity &entity)
            {
                REQUIRE_VOID_RETURN(population);

                if (population->hasComponent(entity, Model::identifier) &&
                    population->hasComponent(entity, Engine::Components::Transform::identifier))
                {
                    auto &modelComponent = population->getComponent<Model::Data>(entity, Model::identifier);
                    auto &transformComponent = population->getComponent<Engine::Components::Transform::Data>(entity, Engine::Components::Transform::identifier);
                    auto dataNameIterator = dataMap.find(modelComponent);
                    if (dataNameIterator != dataMap.end())
                    {
                        dataEntityList[entity] = &(*dataNameIterator).second;
                    }
                    else
                    {
                        ModelData &data = dataMap[modelComponent];
                        preLoadData(modelComponent, data);
                        dataEntityList[entity] = &data;
                    }
                }
            }

            STDMETHODIMP_(void) onEntityDestroyed(const Engine::Population::Entity &entity)
            {
                auto dataEntityIterator = dataEntityList.find(entity);
                if (dataEntityIterator != dataEntityList.end())
                {
                    dataEntityList.unsafe_erase(dataEntityIterator);
                }
            }

            // Render::Observer
            STDMETHODIMP_(void) OnRenderScene(const Engine::Population::Entity &cameraEntity, const Gek::Shape::Frustum *viewFrustum)
            {
                REQUIRE_VOID_RETURN(population);
                REQUIRE_VOID_RETURN(viewFrustum);

                const auto &cameraTransform = population->getComponent<Engine::Components::Transform::Data>(cameraEntity, Engine::Components::Transform::identifier);

                concurrency::concurrent_unordered_map<ModelData *, concurrency::concurrent_vector<InstanceData>> visibleList;
                concurrency::parallel_for_each(dataEntityList.begin(), dataEntityList.end(), [&](const std::pair<const Engine::Population::Entity, ModelData *> &dataEntity) -> void
                {
                    ModelData &data = *(dataEntity.second);
                    Gek::Math::Float3 size(1.0f, 1.0f, 1.0f);
                    if (population->hasComponent(dataEntity.first, Engine::Components::Size::identifier))
                    {
                        size.set(population->getComponent<Engine::Components::Size::Data>(dataEntity.first, Engine::Components::Size::identifier));
                    }

                    Gek::Shape::AlignedBox alignedBox(data.alignedBox);
                    alignedBox.minimum *= size;
                    alignedBox.maximum *= size;

                    const auto &transformComponent = population->getComponent<Engine::Components::Transform::Data>(dataEntity.first, Engine::Components::Transform::identifier);
                    Shape::OrientedBox orientedBox(alignedBox, transformComponent.rotation, transformComponent.position);
                    if (viewFrustum->isVisible(orientedBox))
                    {
                        Gek::Math::Float4 color(1.0f, 1.0f, 1.0f, 1.0f);
                        if (population->hasComponent(dataEntity.first, Engine::Components::Color::identifier))
                        {
                            color = population->getComponent<Engine::Components::Color::Data>(dataEntity.first, Engine::Components::Color::identifier);
                        }

                        visibleList[dataEntity.second].push_back(InstanceData(Math::Float4x4(transformComponent.rotation, transformComponent.position), color, size, cameraTransform.position.getDistance(transformComponent.position)));
                    }
                });

                std::vector<InstanceData> instanceArray;
                std::map<ModelData *, std::pair<UINT32, UINT32>> instanceMap;
                for (auto instancePair : visibleList)
                {
                    ModelData &data = *(instancePair.first);
                    if (!data.load || SUCCEEDED(data.load(data)))
                    {
                        auto &instanceList = instancePair.second;
                        concurrency::parallel_sort(instanceList.begin(), instanceList.end(), [&](const InstanceData &leftInstance, const InstanceData &rightInstance) -> bool
                        {
                            return (leftInstance.distance < rightInstance.distance);
                        });

                        instanceMap[&data] = std::make_pair(instanceList.size(), instanceArray.size());
                        instanceArray.insert(instanceArray.end(), instanceList.begin(), instanceList.end());
                    }
                }

                LPVOID instanceData = nullptr;
                if (SUCCEEDED(video->mapBuffer(instanceBuffer, &instanceData)))
                {
                    UINT32 instanceCount = std::min(instanceArray.size(), size_t(1024));
                    memcpy(instanceData, instanceArray.data(), (sizeof(InstanceData) * instanceCount));
                    video->unmapBuffer(instanceBuffer);

                    for (auto &instancePair : instanceMap)
                    {
                        auto &data = *instancePair.first;
                        for (auto &materialInfo : data.materialInfoList)
                        {
                            render->drawInstancedIndexedPrimitive(plugin, materialInfo.material, { data.vertexBuffer, instanceBuffer }, instancePair.second.first, instancePair.second.second, materialInfo.firstVertex, data.indexBuffer, materialInfo.indexCount, materialInfo.firstIndex);
                        }
                    }
                }
            }

            STDMETHODIMP_(void) onRenderOverlay(void)
            {
            }
        };

        REGISTER_CLASS(System)
    }; // namespace Model
}; // namespace Gek
