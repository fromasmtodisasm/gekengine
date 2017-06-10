﻿#include "GEK/Math/Common.hpp"
#include "GEK/Math/Quaternion.hpp"
#include "GEK/Utility/String.hpp"
#include "GEK/Utility/FileSystem.hpp"
#include "GEK/Utility/JSON.hpp"
#include "GEK/Utility/ContextUser.hpp"
#include "GEK/Engine/Core.hpp"
#include "GEK/Engine/Renderer.hpp"
#include "GEK/Engine/Population.hpp"
#include "GEK/Engine/Entity.hpp"
#include "GEK/Engine/Component.hpp"
#include "GEK/Engine/ComponentMixin.hpp"
#include "GEK/Engine/Editor.hpp"
#include "GEK/Components/Transform.hpp"
#include <concurrent_vector.h>
#include <ppl.h>

namespace Gek
{
    namespace Implementation
    {
        GEK_CONTEXT_USER(Editor, Plugin::Core *)
            , public Plugin::Processor
            , public Plugin::Editor
        {
        private:
            Plugin::Core *core = nullptr;
            Edit::Population *population = nullptr;
            Plugin::Renderer *renderer = nullptr;

            float headingAngle = 0.0f;
            float lookingAngle = 0.0f;
            Math::Float3 position = Math::Float3::Zero;
            bool moveForward = false;
            bool moveBackward = false;
            bool strafeLeft = false;
            bool strafeRight = false;

            uint32_t selectedEntity = 0;
            uint32_t selectedComponent = 0;

            Video::TexturePtr deleteTexture;
            Video::TexturePtr populationButton;

        public:
            Editor(Context *context, Plugin::Core *core)
                : ContextRegistration(context)
                , core(core)
                , population(dynamic_cast<Edit::Population *>(core->getPopulation()))
                , renderer(core->getRenderer())
            {
                assert(population);
                assert(core);

                core->setOption("editor", "active", false);

                population->onAction.connect<Editor, &Editor::onAction>(this);
                population->onUpdate[90].connect<Editor, &Editor::onUpdate>(this);

                auto baseFileName(getContext()->getRootFileName("data", "gui"));
                deleteTexture = core->getVideoDevice()->loadTexture(FileSystem::GetFileName(baseFileName, "delete.png"), 0);
                populationButton = core->getVideoDevice()->loadTexture(FileSystem::GetFileName(baseFileName, "population.png"), 0);

                core->getPanelManager()->getPane(ImGui::PanelManager::RIGHT)->addButtonAndWindow(
                    ImGui::Toolbutton("Population", (Video::Object *)populationButton.get(), ImVec2(0, 0), ImVec2(1, 1), ImVec2(32, 32)),
                    ImGui::PanelManagerPaneAssociatedWindow("Population", -1, [](ImGui::PanelManagerWindowData &windowData) -> void
                {
                    ((Editor *)windowData.userData)->drawPopulation(windowData);
                }, this));
            }

            ~Editor(void)
            {
                population->onUpdate[90].disconnect<Editor, &Editor::onUpdate>(this);
                population->onAction.disconnect<Editor, &Editor::onAction>(this);
            }

            void drawPopulation(ImGui::PanelManagerWindowData &windowData)
            {
                ImGui::PushItemWidth(-1.0f);

                bool editorActive = core->getOption("editor", "active").convert(false);
                if (ImGui::Checkbox("Editor", &editorActive))
                {
                    core->setOption("editor", "active", editorActive);
                }

                auto &entityMap = population->getEntityMap();
                if (!entityMap.empty())
                {
                    if (ImGui::Button("Create Entity", ImVec2(ImGui::GetWindowContentRegionWidth(), 0)))
                    {
                        ImGui::OpenPopup("Entity Name");
                    }

                    if (ImGui::BeginPopup("Entity Name"))
                    {
                        std::string name;
                        if (GUI::InputString("Name", name, ImGuiInputTextFlags_EnterReturnsTrue))
                        {
                            population->createEntity(name);
                            ImGui::CloseCurrentPopup();
                        }

                        ImGui::EndPopup();
                    }

                    const auto entityCount = entityMap.size();
                    if (ImGui::ListBoxHeader("##Entities", entityCount, 7))
                    {
                        ImGuiListClipper clipper(entityCount, ImGui::GetTextLineHeightWithSpacing());
                        while (clipper.Step())
                        {
                            auto entitySearch = std::begin(entityMap);
                            std::advance(entitySearch, clipper.DisplayStart);
                            for (int entityIndex = clipper.DisplayStart; entityIndex < clipper.DisplayEnd; ++entityIndex, ++entitySearch)
                            {
                                ImGui::PushID(entityIndex);
                                if (ImGui::ImageButton((Video::Object *)deleteTexture.get(), ImVec2(9, 9)))
                                {
                                    population->killEntity(entitySearch->second.get());
                                }

                                ImGui::PopID();
                                ImGui::SameLine();
                                ImGui::SetItemAllowOverlap();
                                if (ImGui::Selectable(entitySearch->first.c_str(), (entityIndex == selectedEntity)))
                                {
                                    selectedEntity = entityIndex;
                                    selectedComponent = 0;
                                }
                            }
                        };

                        ImGui::ListBoxFooter();
                    }

                    if (selectedEntity >= 0 && selectedEntity < entityMap.size())
                    {
                        auto entitySearch = std::begin(entityMap);
                        std::advance(entitySearch, selectedEntity);
                        Edit::Entity *entity = dynamic_cast<Edit::Entity *>(entitySearch->second.get());
                        if (entity)
                        {
                            if (entity->hasComponent<Components::Transform>())
                            {
                                auto &transformComponent = entity->getComponent<Components::Transform>();
                            }

                            if (ImGui::Button("Add Component", ImVec2(ImGui::GetWindowContentRegionWidth(), 0)))
                            {
                                ImGui::OpenPopup("Select Component");
                            }

                            if (ImGui::BeginPopup("Select Component"))
                            {
                                const auto &componentMap = population->getComponentMap();
                                const auto componentCount = componentMap.size();
                                if (ImGui::ListBoxHeader("##Components", componentCount, 7))
                                {
                                    ImGuiListClipper clipper(componentCount, ImGui::GetTextLineHeightWithSpacing());
                                    while (clipper.Step())
                                    {
                                        for (auto componentIndex = clipper.DisplayStart; componentIndex < clipper.DisplayEnd; ++componentIndex)
                                        {
                                            auto componentSearch = std::begin(componentMap);
                                            std::advance(componentSearch, componentIndex);
                                            if (ImGui::Selectable((componentSearch->first.name() + 7), (selectedComponent == componentIndex)))
                                            {
                                                auto componentData = std::make_pair(componentSearch->second->getName(), JSON::EmptyObject);
                                                population->addComponent(entity, componentData);
                                                ImGui::CloseCurrentPopup();
                                            }
                                        }
                                    };

                                    ImGui::ListBoxFooter();
                                }

                                ImGui::EndPopup();
                            }

                            const auto &entityComponentMap = entity->getComponentMap();
                            if (!entityComponentMap.empty())
                            {
                                const auto entityComponentsCount = entityComponentMap.size();
                                if (ImGui::ListBoxHeader("##Components", entityComponentsCount, 7))
                                {
                                    std::vector<std::type_index> componentDeleteList;
                                    ImGuiListClipper clipper(entityComponentsCount, ImGui::GetTextLineHeightWithSpacing());
                                    while (clipper.Step())
                                    {
                                        for (auto componentIndex = clipper.DisplayStart; componentIndex < clipper.DisplayEnd; ++componentIndex)
                                        {
                                            auto entityComponentSearch = std::begin(entityComponentMap);
                                            std::advance(entityComponentSearch, componentIndex);

                                            ImGui::PushID(componentIndex);
                                            if (ImGui::ImageButton((Video::Object *)deleteTexture.get(), ImVec2(9, 9)))
                                            {
                                                componentDeleteList.push_back(entityComponentSearch->first);
                                            }

                                            ImGui::PopID();
                                            ImGui::SameLine();
                                            ImGui::SetItemAllowOverlap();
                                            if (ImGui::Selectable((entityComponentSearch->first.name() + 7), (selectedComponent == componentIndex)))
                                            {
                                                selectedComponent = componentIndex;
                                            }
                                        }
                                    };

                                    ImGui::ListBoxFooter();
                                    for (const auto &componentType : componentDeleteList)
                                    {
                                        population->removeComponent(entity, componentType);
                                    }
                                }

                                auto entityComponentSearch = std::begin(entityComponentMap);
                                std::advance(entityComponentSearch, selectedComponent);
                                if (entityComponentSearch != std::end(entityComponentMap))
                                {
                                    Edit::Component *component = population->getComponent(entityComponentSearch->first);
                                    Plugin::Component::Data *componentData = entityComponentSearch->second.get();
                                    if (component && componentData)
                                    {
                                        if (editorActive)
                                        {
                                            Math::Float4x4 viewMatrix(Math::Float4x4::FromPitch(lookingAngle) * Math::Float4x4::FromYaw(headingAngle));
                                            viewMatrix.translation.xyz = position;
                                            viewMatrix.invert();

                                            const auto backBuffer = core->getVideoDevice()->getBackBuffer();
                                            const float width = float(backBuffer->getDescription().width);
                                            const float height = float(backBuffer->getDescription().height);
                                            const auto projectionMatrix(Math::Float4x4::MakePerspective(Math::DegreesToRadians(90.0f), (width / height), 0.1f, 200.0f));
                                            if (component->edit(ImGui::GetCurrentContext(), viewMatrix, projectionMatrix, entity, componentData))
                                            {
                                                onModified.emit(entity, entityComponentSearch->first);
                                            }
                                        }
                                        else
                                        {
                                            component->show(ImGui::GetCurrentContext(), entity, componentData);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                ImGui::PopItemWidth();
            }

            // Plugin::Population Slots
            void onAction(Plugin::Population::Action const &action)
            {
                bool editorActive = core->getOption("editor", "active").convert(false);
                if (!editorActive)
                {
                    return;
                }

                if (action.name == "turn")
                {
                    headingAngle += (action.value * 0.01f);
                }
                else if (action.name == "tilt")
                {
                    lookingAngle += (action.value * 0.01f);
                    lookingAngle = Math::Clamp(lookingAngle, -Math::Pi * 0.5f, Math::Pi * 0.5f);
                }
                else if (action.name == "move_forward")
                {
                    moveForward = action.state;
                }
                else if (action.name == "move_backward")
                {
                    moveBackward = action.state;
                }
                else if (action.name == "strafe_left")
                {
                    strafeLeft = action.state;
                }
                else if (action.name == "strafe_right")
                {
                    strafeRight = action.state;
                }
            }

            void onUpdate(float frameTime)
            {
                bool editorActive = core->getOption("editor", "active").convert(false);
                if (editorActive)
                {
                    Math::Float4x4 viewMatrix(Math::Float4x4::FromPitch(lookingAngle) * Math::Float4x4::FromYaw(headingAngle));
                    position += (viewMatrix.rz.xyz * (((moveForward ? 1.0f : 0.0f) + (moveBackward ? -1.0f : 0.0f)) * 5.0f) * frameTime);
                    position += (viewMatrix.rx.xyz * (((strafeLeft ? -1.0f : 0.0f) + (strafeRight ? 1.0f : 0.0f)) * 5.0f) * frameTime);
                    viewMatrix.translation.xyz = position;
                    viewMatrix.invert();

                    const auto backBuffer = core->getVideoDevice()->getBackBuffer();
                    const float width = float(backBuffer->getDescription().width);
                    const float height = float(backBuffer->getDescription().height);
                    auto projectionMatrix(Math::Float4x4::MakePerspective(Math::DegreesToRadians(90.0f), (width / height), 0.1f, 200.0f));

                    renderer->queueCamera(viewMatrix, projectionMatrix, 0.5f, 200.0f, ResourceHandle());
                }
            }
        };

        GEK_REGISTER_CONTEXT_USER(Editor);
    }; // namespace Implementation
}; // namespace Gek
