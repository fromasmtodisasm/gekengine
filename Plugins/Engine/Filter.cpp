#include "GEK/Engine/Filter.hpp"
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
#include "GEK/Engine/Core.hpp"
#include "GEK/Engine/Material.hpp"
#include "Passes.hpp"
#include <ppl.h>

namespace Gek
{
    namespace Implementation
    {
        GEK_CONTEXT_USER(Filter, Engine::Core *, std::string)
            , public Engine::Filter
        {
        public:
            struct PassData
            {
                std::string name;
                bool enabled = true;
                Pass::Mode mode = Pass::Mode::Deferred;
                Math::Float4 blendFactor = Math::Float4::Zero;
                BlendStateHandle blendState;
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

        private:
            Engine::Core *core = nullptr;
            Video::Device *videoDevice = nullptr;
            Engine::Resources *resources = nullptr;
            Plugin::Population *population = nullptr;

            std::string filterName;

            DepthStateHandle depthState;
            RenderStateHandle renderState;
            std::vector<PassData> passList;

        public:
            Filter(Context *context, Engine::Core *core, std::string filterName)
                : ContextRegistration(context)
                , core(core)
                , videoDevice(core->getVideoDevice())
                , resources(core->getFullResources())
                , population(core->getPopulation())
                , filterName(filterName)
            {
                assert(videoDevice);
                assert(resources);
                assert(population);

                reload();
            }

            void reload(void)
            {
                LockedWrite{ std::cout } << "Loading filter: " << filterName;
				
                passList.clear();

                std::unordered_map<std::string, ResourceHandle> resourceMap;
                std::unordered_map<std::string, std::string> resourceSemanticsMap;

                auto backBuffer = videoDevice->getBackBuffer();
                auto &backBufferDescription = backBuffer->getDescription();
                depthState = resources->createDepthState(Video::DepthState::Description());
                renderState = resources->createRenderState(Video::RenderState::Description());

                JSON rootNode;
                rootNode.load(getContext()->findDataPath(FileSystem::CombinePaths("filters", filterName).withExtension(".json")));

                ShuntingYard shuntingYard(population->getShuntingYard());
                const auto &coreOptionsNode = core->getOption("filters", filterName);
                for (auto &coreValuePair : coreOptionsNode.asType(JSON::EmptyObject))
                {
                    shuntingYard.setVariable(coreValuePair.first, coreValuePair.second.convert(0.0f));
                }

                auto rootOptionsNode = rootNode.getMember("options"sv);
                auto &rootOptionsObject = rootOptionsNode.asType(JSON::EmptyObject);
                for (auto &coreValuePair : coreOptionsNode.asType(JSON::EmptyObject))
                {
                    rootOptionsObject[coreValuePair.first] = coreValuePair.second;
                }

                auto importSearch = rootOptionsObject.find("#import");
                if (importSearch != std::end(rootOptionsObject))
                {
                    auto importExternal = [&](std::string_view importName) -> void
                    {
                        JSON importOptions;
                        importOptions.load(getContext()->findDataPath(FileSystem::CombinePaths("shaders", importName).withExtension(".json")));
                        for (auto &importPair : importOptions.asType(JSON::EmptyObject))
                        {
                            if (rootOptionsObject.count(importPair.first) == 0)
                            {
                                rootOptionsObject[importPair.first] = importPair.second;
                            }
                        }
                    };

                    importSearch->second.visit(
                        [&](std::string const &importName)
                    {
                        importExternal(importName);
                    },
                        [&](JSON::Array const &importArray)
                    {
                        for (auto &importName : importArray)
                        {
                            importExternal(importName.convert(String::Empty));
                        }
                    },
                        [&](auto const &)
                    {
                    });

                    rootOptionsObject.erase(importSearch);
                }

                for (auto &requiredNode : rootNode.getMember("requires"sv).asType(JSON::EmptyArray))
                {
                    resources->getShader(requiredNode.convert(String::Empty), MaterialHandle());
                }

                for (auto &rootTexturesPair : rootNode.getMember("textures"sv).asType(JSON::EmptyObject))
                {
                    std::string textureName(rootTexturesPair.first);
                    if (resourceMap.count(textureName) > 0)
                    {
                        LockedWrite{ std::cout } << "Texture name same as already listed resource: " << textureName;
                        continue;
                    }

                    ResourceHandle resource;
                    auto textureNode = rootTexturesPair.second;
                    auto textureMap = textureNode.asType(JSON::EmptyObject);
                    if (textureMap.count("file"))
                    {
                        std::string fileName(textureNode.getMember("file"sv).convert(String::Empty));
                        uint32_t flags = getTextureLoadFlags(textureNode.getMember("flags"sv).convert(String::Empty));
                        resource = resources->loadTexture(fileName, flags);
                    }
                    else if (textureMap.count("format"))
                    {
                        Video::Texture::Description description(backBufferDescription);
                        description.format = Video::GetFormat(textureNode.getMember("format"sv).convert(String::Empty));
                        auto &sizeNode = textureNode.getMember("size"sv);
                        if (sizeNode.isType<JSON::Array>())
                        {
                            auto sizeArray = sizeNode.asType(JSON::EmptyArray);
                            if (sizeArray.size() == 3)
                            {
                                description.depth = sizeArray.at(2).evaluate(shuntingYard, 1);
                                description.height = sizeArray.at(1).evaluate(shuntingYard, 1);
                                description.width = sizeArray.at(0).evaluate(shuntingYard, 1);
                            }
                        }
                        else
                        {
                            description.width = description.height = sizeNode.evaluate(shuntingYard, 1);
                        };

                        description.sampleCount = textureNode.getMember("sampleCount"sv).convert(1);
                        description.flags = getTextureFlags(textureNode.getMember("flags"sv).convert(String::Empty));
                        description.mipMapCount = textureNode.getMember("mipmaps"sv).evaluate(shuntingYard, 1);
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

                for (auto &bufferPair : rootNode.getMember("buffers"sv).asType(JSON::EmptyObject))
                {
                    std::string bufferName(bufferPair.first);
                    if (resourceMap.count(bufferName) > 0)
                    {
                        LockedWrite{ std::cout } << "Texture name same as already listed resource: " << bufferName;
                        continue;
                    }

                    auto &bufferNode = bufferPair.second;
                    auto &bufferObject = bufferNode.asType(JSON::EmptyObject);

                    Video::Buffer::Description description;
                    description.count = bufferNode.getMember("count"sv).evaluate(shuntingYard, 0);
                    description.flags = getBufferFlags(bufferNode.getMember("flags"sv).convert(String::Empty));
                    if (bufferObject.count("format"))
                    {
                        description.type = Video::Buffer::Type::Raw;
                        description.format = Video::GetFormat(bufferNode.getMember("format"sv).convert(String::Empty));
                    }
                    else
                    {
                        description.type = Video::Buffer::Type::Structured;
                        description.stride = bufferNode.getMember("stride"sv).evaluate(shuntingYard, 0);
                    }

                    auto resource = resources->createBuffer(bufferName, description, true);
                    if (resource)
                    {
                        resourceMap[bufferName] = resource;
                        if (bufferNode.getMember("byteaddress"sv).convert(false))
                        {
                            resourceSemanticsMap[bufferName] = "ByteAddressBuffer";
                        }
                        else
                        {
                            auto description = resources->getBufferDescription(resource);
                            if (description != nullptr)
                            {
                                auto structure = bufferNode.getMember("structure"sv).convert(String::Empty);
                                resourceSemanticsMap[bufferName] += String::Format("Buffer<{}>", structure.empty() ? getFormatSemantic(description->format) : structure);
                            }
                        }
                    }
                }

                auto &passesNode = rootNode.getMember("passes"sv);
                passList.resize(passesNode.asType(JSON::EmptyArray).size());
                auto passData = std::begin(passList);
                for (auto &passNode : passesNode.asType(JSON::EmptyArray))
                {
                    PassData &pass = *passData++;
                    std::string entryPoint(passNode.getMember("entry"sv).convert(String::Empty));
                    auto programName = passNode.getMember("program"sv).convert(String::Empty);
                    pass.name = programName;

                    auto enableOption = passNode.getMember("enable"sv).convert(String::Empty);
                    if (!enableOption.empty())
                    {
                        String::Replace(enableOption, "::", "|");
                        auto nameList = String::Split(enableOption, '|');
                        const JSON *nameNode = &rootOptionsNode;
                        for (auto &name : nameList)
                        {
                            nameNode = &nameNode->getMember(name);
                        }

                        pass.enabled = nameNode->convert(true);
                        if (!pass.enabled)
                        {
                            continue;
                        }
                    }

                    JSON passOptions(std::cref(rootOptionsNode).get());
                    for (auto &overridePair : passNode.getMember("options"sv).asType(JSON::EmptyObject))
                    {
                        std::function<void(JSON &, std::string_view, JSON const &)> insertOptions;
                        insertOptions = [&](JSON &options, std::string_view name, JSON const &node) -> void
                        {
                            node.visit(
                                [&](JSON::Object const &object)
                            {
                                auto &localOptions = options[name];
                                for (auto &objectPair : object)
                                {
                                    insertOptions(localOptions, objectPair.first, objectPair.second);
                                }
                            },
                                [&](auto const &value)
                            {
                                options[name] = value;
                            });
                        };

                        insertOptions(passOptions, overridePair.first, overridePair.second);
                    }

                    std::function<std::string(JSON const &)> addOptions;
                    addOptions = [&](JSON const &options) -> std::string
                    {
                        std::string optionsData;
                        for (auto &optionPair : options.asType(JSON::EmptyObject))
                        {
                            auto optionName = optionPair.first;
                            auto &optionNode = optionPair.second;
                            optionNode.visit(
                                [&](JSON::Object const &optionObject)
                            {
                                if (optionObject.count("options"))
                                {
                                    optionsData += String::Format("    namespace {}\r\n", optionName);
                                    optionsData += String::Format("    {\r\n");

                                    std::vector<std::string> choices;
                                    for (auto &choice : optionNode.getMember("options"sv).asType(JSON::EmptyArray))
                                    {
                                        auto name = choice.convert(String::Empty);
                                        optionsData += String::Format("        static const int {} = {};\r\n", name, choices.size());
                                        choices.push_back(name);
                                    }

                                    int selection = 0;
                                    auto &selectionNode = optionNode.getMember("selection"sv);
                                    if (selectionNode.isType<std::string>())
                                    {
                                        auto selectedName = selectionNode.convert(String::Empty);
                                        auto optionsSearch = std::find_if(std::begin(choices), std::end(choices), [selectedName](std::string const &choice) -> bool
                                        {
                                            return (selectedName == choice);
                                        });

                                        if (optionsSearch != std::end(choices))
                                        {
                                            selection = std::distance(std::begin(choices), optionsSearch);
                                        }
                                    }
                                    else
                                    {
                                        selection = selectionNode.convert(0U);
                                    }

                                    optionsData += String::Format("        static const int Selection = {};\r\n", selection);
                                    optionsData += String::Format("    };\r\n");
                                }
                                else
                                {
                                    auto optionData = addOptions(optionNode);
                                    if (!optionData.empty())
                                    {
                                        optionsData += String::Format(
                                            "namespace {}\r\n" \
                                            "{\r\n" \
                                            "{}" \
                                            "};\r\n" \
                                            "\r\n", optionName, optionData);
                                    }
                                }
                            },
                                [&](JSON::Array const &optionArray)
                            {
                                switch (optionArray.size())
                                {
                                case 1:
                                    optionsData += String::Format("    static const float {} = {};\r\n", optionName,
                                        optionArray[0].convert(0.0f));
                                    break;

                                case 2:
                                    optionsData += String::Format("    static const float2 {} = float2({}, {});\r\n", optionName,
                                        optionArray[0].convert(0.0f),
                                        optionArray[1].convert(0.0f));
                                    break;

                                case 3:
                                    optionsData += String::Format("    static const float3 {} = float3({}, {}, {});\r\n", optionName,
                                        optionArray[0].convert(0.0f),
                                        optionArray[1].convert(0.0f),
                                        optionArray[2].convert(0.0f));
                                    break;

                                case 4:
                                    optionsData += String::Format("    static const float4 {} = float4({}, {}, {}, {})\r\n", optionName,
                                        optionArray[0].convert(0.0f),
                                        optionArray[1].convert(0.0f),
                                        optionArray[2].convert(0.0f),
                                        optionArray[3].convert(0.0f));
                                    break;
                                };
                            },
                                [&](std::nullptr_t const &)
                            {
                            },
                                [&](bool optionBoolean)
                            {
                                optionsData += String::Format("    static const bool {} = {};\r\n", optionName, optionBoolean);
                            },
                                [&](float optionFloat)
                            {
                                optionsData += String::Format("    static const float {} = {};\r\n", optionName, optionFloat);
                            },
                                [&](auto const &optionValue)
                            {
                                optionsData += String::Format("    static const int {} = {};\r\n", optionName, optionValue);
                            });
                        }

                        return optionsData;
                    };

                    std::string engineData;
                    auto optionsData = addOptions(passOptions);
                    if (!optionsData.empty())
                    {
                        engineData += String::Format(
                            "namespace Options\r\n" \
                            "{\r\n" \
                            "{}" \
                            "};\r\n" \
                            "\r\n", optionsData);
                    }

                    std::string mode(String::GetLower(passNode.getMember("mode"sv).convert(String::Empty)));
                    if (mode == "compute")
                    {
                        pass.mode = Pass::Mode::Compute;
                    }
                    else
                    {
                        pass.mode = Pass::Mode::Deferred;
                    }

                    if (pass.mode == Pass::Mode::Compute)
                    {
                        auto &dispatchNode = passNode.getMember("dispatch"sv);
                        if (dispatchNode.isType<JSON::Array>())
                        {
                            auto &dispatchArray = dispatchNode.asType(JSON::EmptyArray);
                            if (dispatchArray.size() == 3)
                            {
                                pass.dispatchWidth = dispatchArray.at(0).evaluate(shuntingYard, 1);
                                pass.dispatchHeight = dispatchArray.at(1).evaluate(shuntingYard, 1);
                                pass.dispatchDepth = dispatchArray.at(2).evaluate(shuntingYard, 1);
                            }
                        }
                        else
                        {
                            pass.dispatchWidth = pass.dispatchHeight = pass.dispatchDepth = dispatchNode.evaluate(shuntingYard, 1);
                        }
                    }
                    else
                    {
                        engineData +=
                            "struct InputPixel\r\n" \
                            "{\r\n" \
                            "    float4 screen : SV_POSITION;\r\n" \
                            "    float2 texCoord : TEXCOORD0;\r\n" \
                            "};\r\n" \
                            "\r\n";

                        std::string outputData;
                        uint32_t nextTargetStage = 0;
                        std::unordered_map<std::string, std::string> renderTargetsMap = getAliasedMap(passNode.getMember("targets"sv));
                        if (!renderTargetsMap.empty())
                        {
                            for (auto &renderTarget : renderTargetsMap)
                            {
                                uint32_t currentStage = nextTargetStage++;
                                if (renderTarget.first == "outputBuffer")
                                {
                                    pass.renderTargetList.push_back(ResourceHandle());
                                    outputData += String::Format("    Texture2D<float3> {} : SV_TARGET{};\r\n", renderTarget.second, currentStage);
                                }
                                else
                                {
                                    auto resourceSearch = resourceMap.find(renderTarget.first);
                                    if (resourceSearch == std::end(resourceMap))
                                    {
                                        LockedWrite{ std::cerr } << "Unable to find render target for pass: " << renderTarget.first;
                                    }
                                    else
                                    {
                                        auto description = resources->getTextureDescription(resourceSearch->second);
                                        if (description)
                                        {
                                            pass.renderTargetList.push_back(resourceSearch->second);
                                            outputData += String::Format("    {} {} : SV_TARGET{};\r\n", getFormatSemantic(description->format), renderTarget.second, currentStage);
                                        }
                                        else
                                        {
                                            LockedWrite{ std::cerr } << "Unable to get description for render target: " << renderTarget.first;
                                        }
                                    }
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

                        Video::BlendState::Description blendStateInformation;
                        blendStateInformation.load(passNode.getMember("blendState"sv));
                        pass.blendState = resources->createBlendState(blendStateInformation);
                    }

                    for (auto &baseClearTargetNode : passNode.getMember("clear"sv).asType(JSON::EmptyObject))
                    {
                        auto resourceName = baseClearTargetNode.first;
                        auto resourceSearch = resourceMap.find(resourceName);
                        if (resourceSearch != std::end(resourceMap))
                        {
                            JSON clearTargetNode(baseClearTargetNode.second);
                            auto clearType = getClearType(clearTargetNode.getMember("type"sv).convert(String::Empty));
                            auto clearValue = clearTargetNode.getMember("value"sv).convert(String::Empty);
                            pass.clearResourceMap.insert(std::make_pair(resourceSearch->second, ClearData(clearType, clearValue)));
                        }
                        else
                        {
                            LockedWrite{ std::cerr } << "Missing clear target encountered: " << resourceName;
                        }
                    }

                    for (auto &baseGenerateMipMapsNode : passNode.getMember("generateMipMaps"sv).asType(JSON::EmptyArray))
                    {
                        JSON generateMipMapNode(baseGenerateMipMapsNode);
                        auto resourceName = generateMipMapNode.convert(String::Empty);
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

                    for (auto &baseCopyNode : passNode.getMember("copy"sv).asType(JSON::EmptyObject))
                    {
                        auto targetResourceName = baseCopyNode.first;
                        auto nameSearch = resourceMap.find(targetResourceName);
                        if (nameSearch != std::end(resourceMap))
                        {
                            JSON copyNode(baseCopyNode.second);
                            auto sourceResourceName = copyNode.convert(String::Empty);
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

                    for (auto &baseResolveNode : passNode.getMember("resolve"sv).asType(JSON::EmptyObject))
                    {
                        auto targetResourceName = baseResolveNode.first;
                        auto nameSearch = resourceMap.find(targetResourceName);
                        if (nameSearch != std::end(resourceMap))
                        {
                            JSON resolveNode(baseResolveNode.second);
                            auto sourceResourceName = resolveNode.convert(String::Empty);
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

                    std::string resourceData;
                    uint32_t nextResourceStage = 0;
                    std::unordered_map<std::string, std::string> resourceAliasMap = getAliasedMap(passNode.getMember("resources"sv));
                    for (auto &resourcePair : resourceAliasMap)
                    {
                        uint32_t currentStage = nextResourceStage++;
                        if (resourcePair.first == "inputBuffer")
                        {
                            pass.resourceList.push_back(ResourceHandle());
                            resourceData += String::Format("    Texture2D<float3> {} : register(t{});\r\n", resourcePair.second, currentStage);
                        }
                        else
                        {
                            auto resourceSearch = resourceMap.find(resourcePair.first);
                            if (resourceSearch != std::end(resourceMap))
                            {
                                pass.resourceList.push_back(resourceSearch->second);
                            }

                            auto semanticsSearch = resourceSemanticsMap.find(resourcePair.first);
                            if (semanticsSearch != std::end(resourceSemanticsMap))
                            {
                                resourceData += String::Format("    {} {} : register(t{});\r\n", semanticsSearch->second, resourcePair.second, currentStage);
                            }
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

                    std::unordered_map<std::string, std::string> unorderedAccessAliasMap = getAliasedMap(passNode.getMember("unorderedAccess"sv));
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

                    std::string fileName(FileSystem::CombinePaths(filterName, programName).withExtension(".hlsl").getString());
                    Video::Program::Type pipelineType = (pass.mode == Pass::Mode::Compute ? Video::Program::Type::Compute : Video::Program::Type::Pixel);
                    pass.program = resources->loadProgram(pipelineType, fileName, entryPoint, engineData);
				}

				core->setOption("filters", filterName, std::move(rootOptionsNode));
				LockedWrite{ std::cout } << "Filter loaded successfully: " << filterName;
			}

            ~Filter(void)
            {
            }

            // Filter
			Hash getIdentifier(void) const
			{
				return GetHash(this);
			}

			std::string_view getName(void) const
			{
                return filterName;
            }

            Pass::Mode preparePass(Video::Device::Context *videoContext, ResourceHandle input, ResourceHandle output, PassData const &pass)
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

                for (auto const &resource : pass.generateMipMapsList)
                {
                    resources->generateMipMaps(videoContext, resource);
                }

                for (auto const &copyResource : pass.copyResourceMap)
                {
                    resources->copyResource(copyResource.first, copyResource.second);
                }

                Video::Device::Context::Pipeline *videoPipeline = (pass.mode == Pass::Mode::Compute ? videoContext->computePipeline() : videoContext->pixelPipeline());
                if (!pass.resourceList.empty())
                {
                    auto resourceList(pass.resourceList);
                    for (auto &resource : resourceList)
                    {
                        resource = (resource ? resource : input);
                    }

                    resources->setResourceList(videoPipeline, resourceList, 0);
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
                    resources->setDepthState(videoContext, depthState, 0x0);
                    resources->setRenderState(videoContext, renderState);
                    resources->setBlendState(videoContext, pass.blendState, pass.blendFactor, 0xFFFFFFFF);
                    if (!pass.renderTargetList.empty())
                    {
                        auto renderTargetList(pass.renderTargetList);
                        for (auto &resource : renderTargetList)
                        {
                            resource = (resource ? resource : output);
                        }

                        resources->setRenderTargetList(videoContext, renderTargetList, nullptr);
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
                    resources->clearResourceList(videoPipeline,  pass.resourceList.size(), 0);
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

                for (auto &resolveResource : pass.resolveSampleMap)
                {
                    resources->resolveSamples(videoContext, resolveResource.first, resolveResource.second);
                }
            }

            class PassImplementation
                : public Pass
            {
            public:
                Video::Device::Context *videoContext;
                ResourceHandle input, output;
                Filter *filterNode;
                std::vector<Filter::PassData>::iterator current, end;

            public:
                PassImplementation(Video::Device::Context *videoContext, ResourceHandle input, ResourceHandle output, Filter *filterNode, std::vector<Filter::PassData>::iterator current, std::vector<Filter::PassData>::iterator end)
                    : videoContext(videoContext)
                    , input(input)
                    , output(output)
                    , filterNode(filterNode)
                    , current(current)
                    , end(end)
                {
                }

                Iterator next(void)
                {
                    auto next = current;
                    return Iterator(++next == end ? nullptr : new PassImplementation(videoContext, input, output, filterNode, next, end));
                }

                Mode prepare(void)
                {
                    return filterNode->preparePass(videoContext, input, output, (*current));
                }

                void clear(void)
                {
                    filterNode->clearPass(videoContext, (*current));
                }

                bool isEnabled(void) const
                {
                    return (*current).enabled;
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

            Pass::Iterator begin(Video::Device::Context *videoContext, ResourceHandle input, ResourceHandle output)
            {
                assert(videoContext);
                return Pass::Iterator(passList.empty() ? nullptr : new PassImplementation(videoContext, input, output, this, std::begin(passList), std::end(passList)));
            }
        };

        GEK_REGISTER_CONTEXT_USER(Filter);
    }; // namespace Implementation
}; // namespace Gek
