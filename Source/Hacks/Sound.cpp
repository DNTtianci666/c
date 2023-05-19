#include <array>
#include <functional>
#include <memory>
#include <string_view>

#include <imgui/imgui.h>

#include "../ConfigStructs.h"
#include <CSGO/Entity.h>
#include <CSGO/EntityList.h>
#include <CSGO/LocalPlayer.h>

#include <Interfaces/ClientInterfaces.h>

#include "Sound.h"

void Sound::modulateSound(std::string_view name, int entityIndex, float& volume) noexcept
{
    auto modulateVolume = [&](int Player::* proj) {
        if (const auto entity = csgo::Entity::from(retSpoofGadgets->client, clientInterfaces.getEntityList().getEntity(entityIndex)); localPlayer && entity.getPOD() != nullptr && entity.isPlayer()) {
            if (entityIndex == localPlayer.get().getNetworkable().index())
                volume *= std::invoke(proj, players[0]) / 100.0f;
            else if (!entity.isOtherEnemy(memory, localPlayer.get()))
                volume *= std::invoke(proj, players[1]) / 100.0f;
            else
                volume *= std::invoke(proj, players[2]) / 100.0f;
        }
    };

    modulateVolume(&Player::masterVolume);

    using namespace std::literals;

    if (name == "Player.DamageHelmetFeedback"sv)
        modulateVolume(&Player::headshotVolume);
    else if (name.find("Weapon"sv) != std::string_view::npos && name.find("Single"sv) != std::string_view::npos)
        modulateVolume(&Player::weaponVolume);
    else if (name.find("Step"sv) != std::string_view::npos)
        modulateVolume(&Player::footstepVolume);
    else if (name.find("Chicken"sv) != std::string_view::npos)
       volume *= chickenVolume / 100.0f;
}

void Sound::tabItem() noexcept
{
    if (ImGui::BeginTabItem("音量效果")) {
        drawGUI(true);
        ImGui::EndTabItem();
    }
}

static bool soundWindowOpen = false;

void Sound::menuBarItem() noexcept
{
    if (ImGui::MenuItem("音量效果")) {
        soundWindowOpen = true;
        ImGui::SetWindowFocus("音量效果");
        ImGui::SetWindowPos("音量效果", { 100.0f, 100.0f });
    }
}

void Sound::drawGUI(bool contentOnly) noexcept
{
    if (!contentOnly) {
        if (!soundWindowOpen)
            return;
        ImGui::SetNextWindowSize({ 0.0f, 0.0f });
        ImGui::Begin("音量效果", &soundWindowOpen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    }

    ImGui::PushID("音量效果");
    ImGui::SliderInt("小鸡音效", &chickenVolume, 0, 200, "%d%%");

    static int currentCategory{ 0 };
    ImGui::PushItemWidth(110.0f);
    ImGui::Combo("", &currentCategory, "自己\0队友\0敌人\0");
    ImGui::PopItemWidth();
    ImGui::SliderInt("全局音效", &players[currentCategory].masterVolume, 0, 200, "%d%%");
    ImGui::SliderInt("爆头音效", &players[currentCategory].headshotVolume, 0, 200, "%d%%");
    ImGui::SliderInt("武器音效", &players[currentCategory].weaponVolume, 0, 200, "%d%%");
    ImGui::SliderInt("脚步音效", &players[currentCategory].footstepVolume, 0, 200, "%d%%");
    ImGui::PopID();

    if (!contentOnly)
        ImGui::End();
}
