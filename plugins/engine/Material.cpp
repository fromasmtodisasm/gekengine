﻿#include "GEK\Utility\String.h"
#include "GEK\Utility\FileSystem.h"
#include "GEK\Utility\XML.h"
#include "GEK\Context\ContextUser.h"
#include "GEK\System\VideoDevice.h"
#include "GEK\Engine\Shader.h"
#include "GEK\Engine\Resources.h"
#include "GEK\Engine\Renderer.h"
#include "GEK\Engine\Material.h"
#include "ShaderFilter.h"
#include <set>
#include <ppl.h>

namespace Gek
{
    namespace Implementation
    {
        GEK_CONTEXT_USER(Material, Engine::Resources *, const wchar_t *, MaterialHandle)
            , public Engine::Material
        {
        private:
            Engine::Resources *resources;
            DataPtr data;

        public:
            Material(Context *context, Engine::Resources *resources, const wchar_t *materialName, MaterialHandle materialHandle)
                : ContextRegistration(context)
                , resources(resources)
            {
                GEK_TRACE_SCOPE(GEK_PARAMETER(materialName));
                GEK_REQUIRE(resources);
                GEK_REQUIRE(materialName);

                Xml::Root materialNode = Xml::load(String(L"$root\\data\\materials\\%v.xml", materialName), L"material");

                auto &shaderNode = materialNode.getChild(L"shader");
                GEK_CHECK_CONDITION(!shaderNode.attributes.count(L"name"), Material::Exception, "Material missing shader name attribute: %v", materialName);

                Engine::Shader *shader = resources->getShader(shaderNode.attributes[L"name"], materialHandle);

                FileSystem::Path filePath(FileSystem::Path(materialName).getPath());
                String fileSpecifier(FileSystem::Path(materialName).getFileName());

                PassMap passMap;
                for (auto &passNodePair : shaderNode.children)
                {
                    const String &passName = passNodePair.first;
                    auto &passNode = passNodePair.second;
                    auto &resourceMap = passMap[passName];
                    for (auto &resourceNodePair : passNode.children)
                    {
                        const String &resourceName = resourceNodePair.first;
                        auto &resourceNode = resourceNodePair.second;
                        ResourceHandle &resource = resourceMap[resourceName];
                        if (resourceNode.attributes.count(L"file"))
                        {
                            String file(resourceNode.attributes[L"file"]);
                            file.replace(L"$directory", filePath);
                            file.replace(L"$filename", fileSpecifier);
                            file.replace(L"$material", materialName);
                            uint32_t flags = getTextureLoadFlags(resourceNode.getAttribute(L"flags", L"0"));
                            resource = this->resources->loadTexture(file, flags);
                        }
                        else if (resourceNode.attributes.count(L"pattern"))
                        {
                            resource = this->resources->createTexture(resourceNode.attributes[L"pattern"], resourceNode.attributes[L"parameters"]);
                        }
                        else if (resourceNode.attributes.count(L"name"))
                        {
                            resource = this->resources->getResourceHandle(resourceNode.attributes[L"name"]);
                        }
                    }
                }

                this->data = shader->loadMaterialData(passMap);
            }

            // Material
            Data * const getData(void) const
            {
                return data.get();
            }
        };

        GEK_REGISTER_CONTEXT_USER(Material);
    }; // namespace Implementation
}; // namespace Gek
