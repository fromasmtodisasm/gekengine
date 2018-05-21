#include "GEK/Engine/Shader.hpp"
#include "GEK/Engine/Core.hpp"
#include "GEK/Utility/String.hpp"
#include "GEK/Utility/FileSystem.hpp"
#include "GEK/Utility/JSON.hpp"
#include "GEK/Shapes/Sphere.hpp"
#include "GEK/Utility/ContextUser.hpp"
#include "GEK/System/VideoDevice.hpp"
#include "GEK/API/Resources.hpp"
#include "GEK/API/Renderer.hpp"
#include "GEK/API/Population.hpp"
#include "GEK/API/Entity.hpp"
#include "GEK/Components/Transform.hpp"
#include "GEK/Components/Light.hpp"
#include "GEK/Components/Color.hpp"
#include "GEK/Engine/Material.hpp"
#include "Passes.hpp"
#include <concurrent_vector.h>
#include <unordered_set>
#include <ppl.h>

namespace Gek
{
    namespace Implementation
    {
        GEK_CONTEXT_USER(Shader, Engine::Core *, std::string)
            , public Engine::Shader
        {
        public:
            struct MaterialData
            {
                std::vector<Material::Initializer> initializerList;
                RenderStateHandle renderState;
            };

            struct PassData
            {
                std::string name;
                bool enabled = true;
                size_t materialHash = 0;
                uint32_t firstResourceStage = 0;
                Pass::Mode mode = Pass::Mode::Forward;
                bool lighting = false;
                ResourceHandle depthBuffer;
                uint32_t clearDepthFlags = 0;
                float clearDepthValue = 1.0f;
                uint32_t clearStencilValue = 0;
                DepthStateHandle depthState;
                Math::Float4 blendFactor = Math::Float4::Zero;
                BlendStateHandle blendState;
                RenderStateHandle renderState;
                std::vector<ResourceHandle> resourceList;
                std::vector<ResourceHandle> unorderedAccessList;
                std::vector<ResourceHandle> renderTargetList;
                ProgramHandle program;
                uint32_t dispatchWidth = 0;
                uint32_t dispatchHeight = 0;
                uint32_t dispatchDepth = 0;

                std::unordered_map<ResourceHandle, ClearData> clearResourceMap;
                std::vector<ResourceHandle> generateMipMapsList;
                std::unordered_map<ResourceHandle, ResourceHandle> copyResourceMap;
                std::unordered_map<ResourceHandle, ResourceHandle> resolveSampleMap;
            };

            enum class LightType : uint8_t
            {
                Directional = 0,
                Point = 1,
                Spot = 2,
            };

        private:
            Engine::Core *core = nullptr;
            Video::Device *videoDevice = nullptr;
            Engine::Resources *resources = nullptr;
            Plugin::Population *population = nullptr;

            std::string shaderName;
            std::string outputResource;
            uint32_t drawOrder = 0;

            using PassList = std::vector<PassData>;
            using MaterialMap = std::unordered_map<std::string, MaterialData>;

            PassList passList;
            MaterialMap materialMap;
            bool lightingRequired = false;

        public:
            Shader(Context *context, Engine::Core *core, std::string shaderName)
                : ContextRegistration(context)
                , core(core)
                , videoDevice(core->getVideoDevice())
                , resources(core->getFullResources())
                , population(core->getPopulation())
                , shaderName(shaderName)
            {
                assert(videoDevice);
                assert(resources);
                assert(population);

                reload();
            }

            void reload(void)
            {
                LockedWrite{ std::cout } << "Loading shader: " << shaderName;

                ShuntingYard shuntingYard(population->getShuntingYard());
				static auto evaluate = [&](JSON::Reference data, float defaultValue) -> float
				{
                    return data.evaluate(shuntingYard, defaultValue);
				};
				
                passList.clear();
                materialMap.clear();

                auto backBuffer = videoDevice->getBackBuffer();
                auto &backBufferDescription = backBuffer->getDescription();

                JSON shaderNode;
                shaderNode.load(getContext()->findDataPath(FileSystem::CombinePaths("shaders", shaderName).withExtension(".json")));
                outputResource = shaderNode.get("output").as(String::Empty);
                auto globalOptions = shaderNode.get("options").getObject();
                auto engineOptions = core->getOption("shaders", shaderName);
                for (auto &enginePair : engineOptions.getMembers())
                {
                    globalOptions[enginePair.name()] = enginePair.value();
                }

                for (auto &globalPair : globalOptions.members())
                {
                    if (globalPair.name() == "#import")
                    {
                        auto importName = JSON::Reference(globalPair.value()).as(String::Empty);
                        globalOptions.erase(globalPair.name());

                        JSON importOptions;
                        importOptions.load(getContext()->findDataPath(FileSystem::CombinePaths("shaders", importName).withExtension(".json")));
                        for (auto &importPair : importOptions.getMembers())
                        {
                            globalOptions[importPair.name()] = importPair.value();
                        }
                    }
                }

                drawOrder = shaderNode.get("requires").getArray().size();
                for (auto &requires : shaderNode.get("requires").getArray())
                {
                    auto shaderHandle = resources->getShader(JSON::Reference(requires).as(String::Empty));
                    auto shader = resources->getShader(shaderHandle);
                    if (shader)
                    {
                        drawOrder += shader->getDrawOrder();
                    }
                }

				std::string inputData;
                uint32_t semanticIndexList[static_cast<uint8_t>(Video::InputElement::Semantic::Count)] = { 0 };
                for (auto &baseElementNode : shaderNode.get("input").getArray())
                {
                    JSON::Reference elementNode(baseElementNode);
                    std::string name(elementNode.get("name").as(String::Empty));
                    std::string system(String::GetLower(elementNode.get("system").as(String::Empty)));
                    if (system == "isfrontfacing")
                    {
                        inputData += String::Format("    uint {} : SV_IsFrontFace;\r\n", name);
                    }
                    else if (system == "sampleindex")
                    {
                        inputData += String::Format("    uint {} : SV_SampleIndex;\r\n", name);
                    }
                    else
                    {
                        Video::Format format = Video::GetFormat(elementNode.get("format").as(String::Empty));
                        uint32_t count = elementNode.get("count").as(1);
                        auto semantic = Video::InputElement::GetSemantic(elementNode.get("semantic").as(String::Empty));
                        auto semanticIndex = semanticIndexList[static_cast<uint8_t>(semantic)];
                        semanticIndexList[static_cast<uint8_t>(semantic)] += count;

                        inputData += String::Format("    {} {} : {}{};\r\n", getFormatSemantic(format, count), name, videoDevice->getSemanticMoniker(semantic), semanticIndex);
                    }
                }

				static constexpr std::string_view lightsData =
                    "namespace Lights\r\n" \
                    "{\r\n" \
                    "    cbuffer Parameters : register(b3)\r\n" \
                    "    {\r\n" \
                    "        uint3 gridSize;\r\n" \
                    "        uint directionalCount;\r\n" \
                    "        uint2 tileSize;\r\n" \
                    "        uint pointCount;\r\n" \
                    "        uint spotCount;\r\n" \
                    "    };\r\n" \
                    "\r\n" \
                    "    static const float2 ReciprocalTileSize = (1.0 / tileSize);" \
                    "\r\n" \
                    "    struct DirectionalData\r\n" \
                    "    {\r\n" \
                    "        float3 radiance;\r\n" \
                    "        float padding1;\r\n" \
                    "        float3 direction;\r\n" \
                    "        float padding2;\r\n" \
                    "    };\r\n" \
                    "\r\n" \
                    "    struct PointData\r\n" \
                    "    {\r\n" \
                    "        float3 radiance;\r\n" \
                    "        float radius;\r\n" \
                    "        float3 position;\r\n" \
                    "        float range;\r\n" \
                    "    };\r\n" \
                    "\r\n" \
                    "    struct SpotData\r\n" \
                    "    {\r\n" \
                    "        float3 radiance;\r\n" \
                    "        float radius;\r\n" \
                    "        float3 position;\r\n" \
                    "        float range;\r\n" \
                    "        float3 direction;\r\n" \
                    "        float padding1;\r\n" \
                    "        float innerAngle;\r\n" \
                    "        float outerAngle;\r\n" \
                    "        float coneFalloff;\r\n" \
                    "        float padding2;\r\n" \
                    "    };\r\n" \
                    "\r\n" \
                    "    StructuredBuffer<DirectionalData> directionalList : register(t0);\r\n" \
                    "    StructuredBuffer<PointData> pointList : register(t1);\r\n" \
                    "    StructuredBuffer<SpotData> spotList : register(t2);\r\n" \
                    "    Buffer<uint2> clusterDataList : register(t3);\r\n" \
                    "    Buffer<uint> clusterIndexList : register(t4);\r\n" \
                    "};\r\n" \
                    "\r\n"sv;

                std::unordered_map<std::string, ResourceHandle> resourceMap;
                std::unordered_map<std::string, std::string> resourceSemanticsMap;
                for (auto &baseTextureNode : shaderNode.get("textures").getMembers())
                {
                    std::string textureName(baseTextureNode.name());
                    if (resourceMap.count(textureName) > 0)
                    {
                        LockedWrite{ std::cout } << "Texture name same as already listed resource: " << textureName;
                        continue;
                    }

                    ResourceHandle resource;
                    JSON::Reference textureNode(baseTextureNode.value());
                    if (textureNode.has("file"))
                    {
                        std::string fileName(textureNode.get("file").as(String::Empty));
                        uint32_t flags = getTextureLoadFlags(textureNode.get("flags").as(String::Empty));
                        resource = resources->loadTexture(fileName, flags);
                    }
                    else
                    {
                        Video::Texture::Description description(backBufferDescription);
                        description.format = Video::GetFormat(textureNode.get("format").as(String::Empty));
                        auto &size = textureNode.get("size");
                        if (size.isFloat())
                        {
                            description.width = evaluate(size, 1);
                        }
                        else
                        {
                            auto &sizeArray = size.getArray();
                            switch (sizeArray.size())
                            {
                            case 3:
                                description.depth = evaluate(size.at(2), 1);

                            case 2:
                                description.height = evaluate(size.at(1), 1);

                            case 1:
                                description.width = evaluate(size.at(0), 1);
                                break;
                            };
                        }

                        description.sampleCount = textureNode.get("sampleCount").as(1);
                        description.flags = getTextureFlags(textureNode.get("flags").as(String::Empty));
                        description.mipMapCount = evaluate(textureNode.get("mipmaps"), 1);
                        resource = resources->createTexture(textureName, description, true);
                    }

                    auto description = resources->getTextureDescription(resource);
                    if (description)
                    {
                        resourceMap[textureName] = resource;
                        if (description->depth > 1)
                        {
                            resourceSemanticsMap[textureName] = String::Format("Texture3D<{}>", getFormatSemantic(description->format));
                        }
                        else if (description->height > 1 || description->width == 1)
                        {
                            resourceSemanticsMap[textureName] = String::Format("Texture2D<{}>", getFormatSemantic(description->format));
                        }
                        else
                        {
                            resourceSemanticsMap[textureName] = String::Format("Texture1D<{}>", getFormatSemantic(description->format));
                        }
                    }
                }

                for (auto &baseBufferNode : shaderNode.get("buffers").getMembers())
                {
                    std::string bufferName(baseBufferNode.name());
                    if (resourceMap.count(bufferName) > 0)
                    {
                        LockedWrite{ std::cout } << "Texture name same as already listed resource: " << bufferName;
                        continue;
                    }

                    JSON::Reference bufferValue(baseBufferNode.value());

                    Video::Buffer::Description description;
                    description.count = evaluate(bufferValue.get("count"), 0);
                    description.flags = getBufferFlags(bufferValue.get("flags").as(String::Empty));
                    if (bufferValue.has("format"))
                    {
                        description.type = Video::Buffer::Type::Raw;
                        description.format = Video::GetFormat(bufferValue.get("format").as(String::Empty));
                    }
                    else
                    {
                        description.type = Video::Buffer::Type::Structured;
                        description.stride = evaluate(bufferValue.get("stride"), 0);
                    }

                    auto resource = resources->createBuffer(bufferName, description, true);
                    if (resource)
                    {
                        resourceMap[bufferName] = resource;
                        if (bufferValue.get("byteaddress").as(false))
                        {
                            resourceSemanticsMap[bufferName] = "ByteAddressBuffer";
                        }
                        else
                        {
                            auto description = resources->getBufferDescription(resource);
                            if (description != nullptr)
                            {
                                auto structure = bufferValue.get("structure").as(String::Empty);
                                resourceSemanticsMap[bufferName] += String::Format("Buffer<{}>", structure.empty() ? getFormatSemantic(description->format) : structure);
                            }
                        }
                    }
                }

                auto materialsNode = shaderNode.get("materials");
                for (auto &materialPair : materialsNode.getMembers())
                {
                    auto materialName = materialPair.name();
                    auto &materialData = materialMap[materialName];
                    JSON::Reference materialNode(materialPair.value());
                    for (JSON::Reference data : materialNode.get("data").getArray())
                    {
                        Material::Initializer initializer;
                        initializer.name = data.get("name").as(String::Empty);
                        initializer.fallback = resources->createPattern(data.get("pattern").as(String::Empty), data.get("parameters"));
                        materialData.initializerList.push_back(initializer);
                    }

                    Video::RenderState::Description renderStateInformation;
                    renderStateInformation.load(materialNode.get("renderState"), globalOptions);
                    materialData.renderState = resources->createRenderState(renderStateInformation);
                }

                auto passesNode = shaderNode.get("passes");
                passList.resize(passesNode.getArray().size());
                auto passData = std::begin(passList);
                for (auto &basePassNode : passesNode.getArray())
                {
                    PassData &pass = *passData++;
                    JSON::Reference passNode(basePassNode);
                    std::string entryPoint(passNode.get("entry").as(String::Empty));
                    auto programName = passNode.get("program").as(String::Empty);
                    pass.name = programName;

                    auto passMaterial = passNode.get("material").as(String::Empty);
                    pass.lighting = passNode.get("lighting").as(false);
                    pass.materialHash = GetHash(passMaterial);
                    lightingRequired |= pass.lighting;

                    if (passNode.has("enable"))
                    {
                        auto options = std::make_shared<JSON::Reference>(globalOptions);
                        auto enableOption = passNode.get("enable").as(String::Empty);
                        for (auto &group : String::Split(enableOption, ':'))
                        {
                            options = std::make_shared<JSON::Reference>(options->get(group));
                        }

                        pass.enabled = options->convert(true);
                        if (!pass.enabled)
                        {
                            continue;
                        }
                    }

                    JSON passOptions(globalOptions);
                    if (passNode.has("options"))
                    {
                        auto overrideOptions = passNode.get("options");
                        for (auto &overridePair : overrideOptions.getMembers())
                        {
                            passOptions[overridePair.name()] = overridePair.value();
                        }
                    }

                    std::function<std::string(JSON::Reference, std::string)> addOptions;
                    addOptions = [&](JSON::Reference options, std::string indent) -> std::string
                    {
                        std::string optionsData;
                        for (auto &optionPair : options.getMembers())
                        {
                            auto optionName = optionPair.name();
                            auto &optionValue = optionPair.value();
                            JSON::Reference option(optionValue);
                            if (optionValue.is_object())
                            {
                                if (option.has("options"))
                                {
                                    optionsData += indent + String::Format("namespace {}\r\n", optionName);
                                    optionsData += indent + String::Format("{\r\n");

                                    uint32_t optionValue = 0;
                                    std::vector<std::string> optionList;
                                    for (JSON::Reference choice : option.get("options").getArray())
                                    {
                                        auto optionName = choice.as(String::Empty);
                                        optionsData += indent + String::Format("    static const int {} = {};\r\n", optionName, optionValue++);
                                        optionList.push_back(optionName);
                                    }

                                    int selection = 0;
                                    auto &selectionNode = option.get("selection");
                                    if (selectionNode.isString())
                                    {
                                        auto selectedName = selectionNode.as(String::Empty);
                                        auto optionsSearch = std::find_if(std::begin(optionList), std::end(optionList), [selectedName](std::string const &choice) -> bool
                                        {
                                            return (selectedName == choice);
                                        });

                                        if (optionsSearch != std::end(optionList))
                                        {
                                            selection = std::distance(std::begin(optionList), optionsSearch);
                                        }
                                    }
                                    else
                                    {
                                        selection = selectionNode.as(0ULL);
                                    }

                                    optionsData += indent + String::Format("    static const int Selection = {};\r\n", selection);
                                    optionsData += indent + String::Format("}; // namespace {}\r\n\r\n", optionName);
                                }
                                else
                                {
                                    auto optionData = addOptions(option, indent + "    ");
                                    if (!optionData.empty())
                                    {
                                        optionsData += indent + String::Format("namespace {}\r\n", optionName);
                                        optionsData += indent + "{\r\n";
                                        optionsData += optionData;
                                        optionsData += indent + String::Format("}; // namespace {}\r\n\r\n", optionName);
                                    }
                                }
                            }
                            else if (optionValue.is_array())
                            {
                                switch (optionValue.size())
                                {
                                case 1:
                                    optionsData += indent + String::Format("static const float {} = {};\r\n", optionName,
                                        JSON::Reference(optionValue[0]).as(0.0f));
                                    break;

                                case 2:
                                    optionsData += indent + String::Format("static const float2 {} = float2({}, {});\r\n", optionName,
                                        JSON::Reference(optionValue[0]).as(0.0f),
                                        JSON::Reference(optionValue[1]).as(0.0f));
                                    break;

                                case 3:
                                    optionsData += indent + String::Format("static const float3 {} = float3({}, {}, {});\r\n", optionName,
                                        JSON::Reference(optionValue[0]).as(0.0f),
                                        JSON::Reference(optionValue[1]).as(0.0f),
                                        JSON::Reference(optionValue[2]).as(0.0f));
                                    break;

                                case 4:
                                    optionsData += indent + String::Format("static const float4 {} = float4({}, {}, {}, {})\r\n", optionName,
                                        JSON::Reference(optionValue[0]).as(0.0f),
                                        JSON::Reference(optionValue[1]).as(0.0f),
                                        JSON::Reference(optionValue[2]).as(0.0f),
                                        JSON::Reference(optionValue[3]).as(0.0f));
                                    break;
                                };
                            }
                            else
                            {
                                if (optionValue.is_bool())
                                {
                                    optionsData += indent + String::Format("static const bool {} = {};\r\n", optionName, option.as(false));
                                }
                                else if (optionValue.is_integer())
                                {
                                    optionsData += indent + String::Format("static const int {} = {};\r\n", optionName, option.as(0ULL));
                                }
                                else
                                {
                                    optionsData += indent + String::Format("static const float {} = {};\r\n", optionName, option.as(0.0f));
                                }
                            }
                        }

                        return optionsData;
                    };

                    std::string engineData;
                    auto optionsData = addOptions(passOptions, "    ");
                    if (!optionsData.empty())
                    {
                        engineData += String::Format(
                            "namespace Options\r\n" \
                            "{\r\n" \
                            "{}" \
                            "}; // namespace Options\r\n" \
                            "\r\n", optionsData);
                    }

                    std::string mode(String::GetLower(passNode.get("mode").as(String::Empty)));
                    if (mode == "forward")
                    {
                        pass.mode = Pass::Mode::Forward;
                    }
                    else if (mode == "compute")
                    {
                        pass.mode = Pass::Mode::Compute;
                    }
                    else
                    {
                        pass.mode = Pass::Mode::Deferred;
                    }

                    if (pass.mode == Pass::Mode::Compute)
                    {
                        auto &dispatch = passNode.get("dispatch");
                        if (dispatch.isFloat())
                        {
                            pass.dispatchWidth = pass.dispatchHeight = pass.dispatchDepth = evaluate(dispatch, 1);
                        }
                        else
                        {
                            auto &dispatchArray = dispatch.getArray();
                            if (dispatchArray.size() == 3)
                            {
                                pass.dispatchWidth = evaluate(dispatch.at(0), 1);
                                pass.dispatchHeight = evaluate(dispatch.at(1), 1);
                                pass.dispatchDepth = evaluate(dispatch.at(2), 1);
                            }
                        }
                    }
                    else
                    {
                        engineData +=
                            "struct InputPixel\r\n" \
                            "{\r\n";
                        if (pass.mode == Pass::Mode::Forward)
                        {
                            engineData += String::Format(
                                "    float4 screen : SV_POSITION;\r\n" \
                                "{}", inputData);
                        }
                        else
                        {
                            engineData +=
                                "    float4 screen : SV_POSITION;\r\n" \
                                "    float2 texCoord : TEXCOORD0;\r\n";
                        }

                        engineData +=
                            "};\r\n" \
                            "\r\n";

                        std::string outputData;
                        uint32_t currentStage = 0;
                        std::unordered_map<std::string, std::string> renderTargetsMap = getAliasedMap(passNode, "targets");
                        if (!renderTargetsMap.empty())
                        {
                            for (auto &renderTarget : renderTargetsMap)
                            {
                                auto resourceSearch = resourceMap.find(renderTarget.first);
                                if (resourceSearch == std::end(resourceMap))
                                {
                                    LockedWrite{ std::cerr } << "Unable to find render target for pass: " << renderTarget.first;
                                }

                                pass.renderTargetList.push_back(resourceSearch->second);
                                auto description = resources->getTextureDescription(resourceSearch->second);
                                if (description)
                                {
                                    outputData += String::Format("    {} {} : SV_TARGET{};\r\n", getFormatSemantic(description->format), renderTarget.second, currentStage++);
                                }
                                else
                                {
                                    LockedWrite{ std::cerr } << "Unable to get description for render target: " << renderTarget.first;
                                }
                            }
                        }

                        if (!outputData.empty())
                        {
                            engineData += String::Format(
                                "struct OutputPixel\r\n" \
                                "{\r\n" \
                                "{}" \
                                "};\r\n" \
                                "\r\n", outputData);
                        }

                        Video::DepthState::Description depthStateInformation;
                        if (passNode.has("depthStyle"s))
                        {
                            auto &depthStyleNode = passNode.get("depthStyle"s);
                            auto camera = String::GetLower(depthStyleNode.get("camera"s).as("perspective"s));
                            if (camera == "perspective"s)
                            {
                                auto invertedDepthBuffer = core->getOption("render"s, "invertedDepthBuffer"s).as(true);

                                depthStateInformation.enable = true;
                                depthStateInformation.writeMask = Video::DepthState::Write::All;
                                if (invertedDepthBuffer)
                                {
                                    depthStateInformation.comparisonFunction = Video::ComparisonFunction::GreaterEqual;
                                }
                                else
                                {
                                    depthStateInformation.comparisonFunction = Video::ComparisonFunction::LessEqual;
                                }

                                auto clear = depthStyleNode.get("clear"s).as(false);
                                if (clear)
                                {
                                    pass.clearDepthValue = (invertedDepthBuffer ? 0.0f : 1.0f);
                                    pass.clearDepthFlags |= Video::ClearFlags::Depth;
                                }
                            }
                        }
                        else
                        {
                            auto &depthStateNode = passNode.get("depthState"s);
                            depthStateInformation.load(depthStateNode, passOptions);
                            pass.clearDepthValue = depthStateNode.get("clear"s).as(Math::Infinity);
                            if (pass.clearDepthValue != Math::Infinity)
                            {
                                pass.clearDepthFlags |= Video::ClearFlags::Depth;
                            }
                        }

						if (depthStateInformation.enable)
						{
                            auto depthBuffer = passNode.get("depthBuffer").as(String::Empty);
							auto depthBufferSearch = resourceMap.find(depthBuffer);
                            if (depthBufferSearch != std::end(resourceMap))
                            {
                                pass.depthBuffer = depthBufferSearch->second;
                            }
                            else
                            {
                                LockedWrite{ std::cerr } << "Missing depth buffer encountered: " << depthBuffer;
							}
                        }

                        Video::BlendState::Description blendStateInformation;
                        blendStateInformation.load(passNode.get("blendState"), passOptions);

                        Video::RenderState::Description renderStateInformation;
                        renderStateInformation.load(passNode.get("renderState"), passOptions);

                        pass.depthState = resources->createDepthState(depthStateInformation);
                        pass.blendState = resources->createBlendState(blendStateInformation);
                        pass.renderState = resources->createRenderState(renderStateInformation);
                    }

                    for (auto &baseClearTargetNode : passNode.get("clear").getMembers())
                    {
                        auto resourceName = baseClearTargetNode.name();
                        auto resourceSearch = resourceMap.find(resourceName);
                        if (resourceSearch != std::end(resourceMap))
                        {
                            JSON::Reference clearTargetNode(baseClearTargetNode.value());
                            auto clearType = getClearType(clearTargetNode.get("type").as(String::Empty));
                            auto clearValue = clearTargetNode.get("value").as(String::Empty);
                            pass.clearResourceMap.insert(std::make_pair(resourceSearch->second, ClearData(clearType, clearValue)));
                        }
                        else
                        {
                            LockedWrite{ std::cerr } << "Missing clear target encountered: " << resourceName;
                        }
                    }

                    for (auto &baseGenerateMipMapsNode : passNode.get("generateMipMaps").getArray())
                    {
                        JSON::Reference generateMipMapNode(baseGenerateMipMapsNode);
                        auto resourceName = generateMipMapNode.as(String::Empty);
                        auto resourceSearch = resourceMap.find(resourceName);
                        if (resourceSearch != std::end(resourceMap))
                        {
                            pass.generateMipMapsList.push_back(resourceSearch->second);
                        }
                        else
                        {
                            LockedWrite{ std::cerr } << "Missing mipmap generation target encountered: " << resourceName;
                        }
                    }

                    for (auto &baseCopyNode : passNode.get("copy").getMembers())
                    {
                        auto targetResourceName = baseCopyNode.name();
                        auto nameSearch = resourceMap.find(targetResourceName);
                        if (nameSearch != std::end(resourceMap))
                        {
                            JSON::Reference copyNode(baseCopyNode.value());
                            auto sourceResourceName = copyNode.as(String::Empty);
                            auto valueSearch = resourceMap.find(sourceResourceName);
                            if (valueSearch != std::end(resourceMap))
                            {
                                pass.copyResourceMap[nameSearch->second] = valueSearch->second;
                            }
                            else
                            {
                                LockedWrite{ std::cerr } << "Missing copy source encountered: " << sourceResourceName;
                            }
                        }
                        else
                        {
                            LockedWrite{ std::cerr } << "Missing copy target encountered: " << targetResourceName;
                        }
                    }

                    for (auto &baseResolveNode : passNode.get("resolve").getMembers())
                    {
                        auto targetResourceName = baseResolveNode.name();
                        auto nameSearch = resourceMap.find(targetResourceName);
                        if (nameSearch != std::end(resourceMap))
                        {
                            JSON::Reference resolveNode(baseResolveNode.value());
                            auto sourceResourceName = resolveNode.as(String::Empty);
                            auto valueSearch = resourceMap.find(sourceResourceName);
                            if (valueSearch != std::end(resourceMap))
                            {
                                pass.resolveSampleMap[nameSearch->second] = valueSearch->second;
                            }
                            else
                            {
                                LockedWrite{ std::cerr } << "Missing resolve source encountered: " << sourceResourceName;
                            }
                        }
                        else
                        {
                            LockedWrite{ std::cerr } << "Missing resolve target encountered: " << targetResourceName;
                        }
                    }

                    if (pass.lighting)
                    {
                        engineData += lightsData;
                    }

                    std::string resourceData;
                    uint32_t nextResourceStage(pass.lighting ? 5 : 0);
                    if (pass.mode == Pass::Mode::Forward)
                    {
                        auto &materialSearch = materialMap.find(passMaterial);
                        if (materialSearch != std::end(materialMap))
                        {
                            pass.firstResourceStage = materialSearch->second.initializerList.size();
                            for (auto &initializer : materialSearch->second.initializerList)
                            {
                                uint32_t currentStage = nextResourceStage++;
                                auto description = resources->getTextureDescription(initializer.fallback);
                                if (description)
                                {
                                    std::string textureType;
                                    if (description->depth > 1)
                                    {
                                        textureType = "Texture3D";
                                    }
                                    else if (description->height > 1 || description->width == 1)
                                    {
                                        textureType = "Texture2D";
                                    }
                                    else
                                    {
                                        textureType = "Texture1D";
                                    }

                                    resourceData += String::Format("    {}<{}> {} : register(t{});\r\n", textureType, getFormatSemantic(description->format), initializer.name, currentStage);
                                }
                            }
                        }
                    }

                    std::unordered_map<std::string, std::string> resourceAliasMap = getAliasedMap(passNode, "resources");
                    for (auto &resourcePair : resourceAliasMap)
                    {
                        auto resourceSearch = resourceMap.find(resourcePair.first);
                        if (resourceSearch != std::end(resourceMap))
                        {
                            pass.resourceList.push_back(resourceSearch->second);
                        }

                        uint32_t currentStage = nextResourceStage++;
                        auto semanticsSearch = resourceSemanticsMap.find(resourcePair.first);
                        if (semanticsSearch != std::end(resourceSemanticsMap))
                        {
                            resourceData += String::Format("    {} {} : register(t{});\r\n", semanticsSearch->second, resourcePair.second, currentStage);
                        }
                    }

                    if (!resourceData.empty())
                    {
                        engineData += String::Format(
                            "namespace Resources\r\n" \
                            "{\r\n" \
                            "{}" \
                            "};\r\n" \
                            "\r\n", resourceData);
                    }

                    std::string unorderedAccessData;
                    uint32_t nextUnorderedStage = 0;
                    if (pass.mode != Pass::Mode::Compute)
                    {
                        nextUnorderedStage = pass.renderTargetList.size();
                    }

                    std::unordered_map<std::string, std::string> unorderedAccessAliasMap = getAliasedMap(passNode, "unorderedAccess");
                    for (auto &resourcePair : unorderedAccessAliasMap)
                    {
                        auto resourceSearch = resourceMap.find(resourcePair.first);
                        if (resourceSearch != std::end(resourceMap))
                        {
                            pass.unorderedAccessList.push_back(resourceSearch->second);
                        }

                        uint32_t currentStage = nextUnorderedStage++;
                        auto semanticsSearch = resourceSemanticsMap.find(resourcePair.first);
                        if (semanticsSearch != std::end(resourceSemanticsMap))
                        {
                            unorderedAccessData += String::Format("    RW{} {} : register(u{});\r\n", semanticsSearch->second, resourcePair.second, currentStage);
                        }
                    }

                    if (!unorderedAccessData.empty())
                    {
                        engineData += String::Format(
                            "namespace UnorderedAccess\r\n" \
                            "{\r\n" \
                            "{}" \
                            "};\r\n" \
                            "\r\n", unorderedAccessData);
                    }

                    std::string fileName(FileSystem::CombinePaths(shaderName, programName).withExtension(".hlsl").getString());
                    Video::Program::Type pipelineType = (pass.mode == Pass::Mode::Compute ? Video::Program::Type::Compute : Video::Program::Type::Pixel);
                    pass.program = resources->loadProgram(pipelineType, fileName, entryPoint, engineData);
				}

				core->setOption("shaders", shaderName, std::move(globalOptions));
				LockedWrite{ std::cout } << "Shader loaded successfully: " << shaderName;
			}

            // Shader
			Hash getIdentifier(void) const
			{
				return GetHash(this);
			}

			std::string_view getName(void) const
			{
                return shaderName;
            }

            uint32_t getDrawOrder(void) const
            {
                return drawOrder;
            }

            bool isLightingRequired(void) const
            {
                return lightingRequired;
            }

            Pass::Mode preparePass(Video::Device::Context *videoContext, PassData const &pass)
            {
                if (!pass.enabled)
                {
                    return Pass::Mode::None;
                }

                for (auto const &clearTarget : pass.clearResourceMap)
                {
                    switch (clearTarget.second.type)
                    {
                    case ClearType::Target:
                        resources->clearRenderTarget(videoContext, clearTarget.first, clearTarget.second.floats);
                        break;

                    case ClearType::Float:
                        resources->clearUnorderedAccess(videoContext, clearTarget.first, clearTarget.second.floats);
                        break;

                    case ClearType::UInt:
                        resources->clearUnorderedAccess(videoContext, clearTarget.first, clearTarget.second.integers);
                        break;
                    };
                }

                for (auto const &copyResource : pass.copyResourceMap)
                {
                    resources->copyResource(copyResource.first, copyResource.second);
                }

                for (auto const &resource : pass.generateMipMapsList)
                {
                    resources->generateMipMaps(videoContext, resource);
                }

                Video::Device::Context::Pipeline *videoPipeline = (pass.mode == Pass::Mode::Compute ? videoContext->computePipeline() : videoContext->pixelPipeline());
                if (!pass.resourceList.empty())
                {
                    uint32_t firstResourceStage = (pass.lighting ? 5 : 0);
                    firstResourceStage += pass.firstResourceStage;
                    resources->setResourceList(videoPipeline, pass.resourceList, firstResourceStage);
                }

                if (!pass.unorderedAccessList.empty())
                {
                    uint32_t firstUnorderedAccessStage = 0;
                    if (pass.mode != Pass::Mode::Compute)
                    {
                        firstUnorderedAccessStage = pass.renderTargetList.size();
                    }

                    resources->setUnorderedAccessList(videoPipeline, pass.unorderedAccessList, firstUnorderedAccessStage);
                }

                resources->setProgram(videoPipeline, pass.program);
                switch (pass.mode)
                {
                case Pass::Mode::Compute:
                    resources->dispatch(videoContext, pass.dispatchWidth, pass.dispatchHeight, pass.dispatchDepth);
                    break;

                default:
                    resources->setDepthState(videoContext, pass.depthState, 0x0);
                    resources->setBlendState(videoContext, pass.blendState, pass.blendFactor, 0xFFFFFFFF);
                    resources->setRenderState(videoContext, pass.renderState);
                    if (pass.depthBuffer && pass.clearDepthFlags > 0)
                    {
                        resources->clearDepthStencilTarget(videoContext, pass.depthBuffer, pass.clearDepthFlags, pass.clearDepthValue, pass.clearStencilValue);
                    }

                    if (!pass.renderTargetList.empty())
                    {
                        resources->setRenderTargetList(videoContext, pass.renderTargetList, (pass.depthBuffer ? &pass.depthBuffer : nullptr));
                    }

                    break;
                };

                return pass.mode;
            }

            void clearPass(Video::Device::Context *videoContext, PassData const &pass)
            {
                if (!pass.enabled)
                {
                    return;
                }

                Video::Device::Context::Pipeline *videoPipeline = (pass.mode == Pass::Mode::Compute ? videoContext->computePipeline() : videoContext->pixelPipeline());
                if (!pass.resourceList.empty())
                {
                    uint32_t firstResourceStage = (pass.lighting ? 5 : 0);
                    firstResourceStage += pass.firstResourceStage;
                    resources->clearResourceList(videoPipeline, pass.resourceList.size(), firstResourceStage);
                }

                if (!pass.unorderedAccessList.empty())
                {
                    uint32_t firstUnorderedAccessStage = 0;
                    if (pass.mode != Pass::Mode::Compute)
                    {
                        firstUnorderedAccessStage = pass.renderTargetList.size();
                    }

                    resources->clearUnorderedAccessList(videoPipeline, pass.unorderedAccessList.size(), firstUnorderedAccessStage);
                }

                if (!pass.renderTargetList.empty())
                {
                    resources->clearRenderTargetList(videoContext, pass.renderTargetList.size(), true);
                }

                videoContext->geometryPipeline()->clearConstantBufferList(1, 2);
                videoContext->vertexPipeline()->clearConstantBufferList(1, 2);
                videoContext->pixelPipeline()->clearConstantBufferList(1, 2);
                videoContext->computePipeline()->clearConstantBufferList(1, 2);
                for (auto &resolveResource : pass.resolveSampleMap)
                {
                    resources->resolveSamples(videoContext, resolveResource.first, resolveResource.second);
                }
            }

            struct MaterialImplementation
                : public Material
            {
            public:
                Shader *shaderNode;
                Shader::MaterialMap::iterator current, end;

            public:
                MaterialImplementation(Shader *shaderNode, Shader::MaterialMap::iterator current, Shader::MaterialMap::iterator end)
                    : shaderNode(shaderNode)
                    , current(current)
                    , end(end)
                {
                }

                Iterator next(void)
                {
                    auto next = current;
                    return Iterator(++next == end ? nullptr : new MaterialImplementation(shaderNode, next, end));
                }

				Hash getIdentifier(void) const
				{
					return GetHash(this);
				}

				std::string_view getName(void) const
				{
                    return (*current).first;
                }

                std::vector<Initializer> const &getInitializerList(void) const
                {
                    return (*current).second.initializerList;
                }

                RenderStateHandle getRenderState(void) const
                {
                    return (*current).second.renderState;
                }
            };

            class PassImplementation
                : public Pass
            {
            public:
                Video::Device::Context *videoContext;
                Shader *shaderNode;
                Shader::PassList::iterator current, end;

            public:
                PassImplementation(Video::Device::Context *videoContext, Shader *shaderNode, Shader::PassList::iterator current, Shader::PassList::iterator end)
                    : videoContext(videoContext)
                    , shaderNode(shaderNode)
                    , current(current)
                    , end(end)
                {
                }

                Iterator next(void)
                {
                    auto next = current;
                    return Iterator(++next == end ? nullptr : new PassImplementation(videoContext, shaderNode, next, end));
                }

                Mode prepare(void)
                {
                    return shaderNode->preparePass(videoContext, (*current));
                }

                void clear(void)
                {
                    shaderNode->clearPass(videoContext, (*current));
                }

                bool isEnabled(void) const
                {
                    return (*current).enabled;
                }

                size_t getMaterialHash(void) const
                {
                    return (*current).materialHash;
                }

                uint32_t getFirstResourceStage(void) const
                {
                    return ((*current).lighting ? 5 : 0);
                }

                bool isLightingRequired(void) const
                {
                    return (*current).lighting;
                }

				Hash getIdentifier(void) const
				{
					return (*current).program.identifier;
				}

				std::string_view getName(void) const
				{
                    return (*current).name;
                }
            };

            std::string const &getOutput(void) const
            {
                return outputResource;
            }

            Material::Iterator begin(void)
            {
                return Material::Iterator(materialMap.empty() ? nullptr : new MaterialImplementation(this, std::begin(materialMap), std::end(materialMap)));
            }

            Pass::Iterator begin(Video::Device::Context *videoContext, Math::Float4x4 const &viewMatrix, Shapes::Frustum const &viewFrustum)
            {
                assert(videoContext);

                return Pass::Iterator(passList.empty() ? nullptr : new PassImplementation(videoContext, this, std::begin(passList), std::end(passList)));
            }
        };

        GEK_REGISTER_CONTEXT_USER(Shader);
    }; // namespace Implementation
}; // namespace Gek
