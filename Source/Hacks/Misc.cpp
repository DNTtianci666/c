#include <algorithm>
#include <array>
#include <iomanip>
#include <mutex>
#include <numbers>
#include <numeric>
#include <sstream>
#include <vector>

#include <imgui/imgui.h>
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui/imgui_internal.h>
#include <imgui/imgui_stdlib.h>

#include "../ConfigStructs.h"
#include "../InputUtil.h"
#include "../Memory.h"
#include "../ProtobufReader.h"

#include "EnginePrediction.h"
#include "Misc.h"

#include <CSGO/PODs/ConVar.h>
#include <CSGO/Constants/ClassId.h>
#include <CSGO/Client.h>
#include <CSGO/ClientClass.h>
#include <CSGO/ClientMode.h>
#include <CSGO/ConVar.h>
#include <CSGO/Cvar.h>
#include <CSGO/Engine.h>
#include <CSGO/EngineTrace.h>
#include <CSGO/Entity.h>
#include <CSGO/EntityList.h>
#include <CSGO/Constants/ConVarNames.h>
#include <CSGO/Constants/FrameStage.h>
#include <CSGO/Constants/GameEventNames.h>
#include <CSGO/Constants/UserMessages.h>
#include <CSGO/GameEvent.h>
#include <CSGO/GlobalVars.h>
#include <CSGO/ItemSchema.h>
#include <CSGO/Localize.h>
#include <CSGO/LocalPlayer.h>
#include <CSGO/NetworkChannel.h>
#include <CSGO/Panorama.h>
#include <CSGO/UserCmd.h>
#include <CSGO/UtlVector.h>
#include <CSGO/Vector.h>
#include <CSGO/WeaponData.h>
#include <CSGO/WeaponId.h>
#include <CSGO/WeaponSystem.h>
#include <CSGO/PODs/RenderableInfo.h>

#include "../GUI.h"
#include "../Helpers.h"
#include "../Hooks.h"
#include "../GameData.h"
#include "../GlobalContext.h"

#include "../imguiCustom.h"
#include <Interfaces/ClientInterfaces.h>
#include <RetSpoof/FunctionInvoker.h>

#include <Utils/StringBuilder.h>

struct PreserveKillfeed {
    bool enabled = false;
    bool onlyHeadshots = false;
};

struct OffscreenEnemies : ColorToggle {
    OffscreenEnemies() : ColorToggle{ 1.0f, 0.26f, 0.21f, 1.0f } {}
    HealthBar healthBar;
};

struct PurchaseList {
    bool enabled = false;
    bool onlyDuringFreezeTime = false;
    bool showPrices = false;
    bool noTitleBar = false;

    enum Mode {
        Details = 0,
        Summary
    };
    int mode = Details;
};

struct MiscConfig {
    MiscConfig() { clanTag[0] = '\0'; }

    KeyBind menuKey{ KeyBind::INSERT };
    bool antiAfkKick{ false };
    bool autoStrafe{ false };
    bool bunnyHop{ false };
    bool customClanTag{ false };
    bool clocktag{ false };
    bool animatedClanTag{ false };
    bool fastDuck{ false };
    bool moonwalk{ false };
    bool edgejump{ false };
    bool slowwalk{ false };
    bool autoPistol{ false };
    bool autoReload{ false };
    bool autoAccept{ false };
    bool radarHack{ false };
    bool revealRanks{ false };
    bool revealMoney{ false };
    bool revealSuspect{ false };
    bool revealVotes{ false };
    bool fixAnimationLOD{ false };
    bool fixMovement{ false };
    bool disableModelOcclusion{ false };
    bool nameStealer{ false };
    bool disablePanoramablur{ false };
    bool killMessage{ false };
    bool nadePredict{ false };
    bool fixTabletSignal{ false };
    bool fastPlant{ false };
    bool fastStop{ false };
    bool quickReload{ false };
    bool prepareRevolver{ false };
    bool oppositeHandKnife = false;
    PreserveKillfeed preserveKillfeed;
    char clanTag[16];
    KeyBind edgejumpkey;
    KeyBind slowwalkKey;
    ColorToggleThickness noscopeCrosshair;
    ColorToggleThickness recoilCrosshair;

    struct SpectatorList {
        bool enabled = false;
        bool noTitleBar = false;
        ImVec2 pos;
        ImVec2 size{ 200.0f, 200.0f };
    };

    SpectatorList spectatorList;
    struct Watermark {
        bool enabled = false;
    };
    Watermark watermark;
    float aspectratio{ 0 };
    std::string killMessageString{ "Gotcha!" };
    int banColor{ 6 };
    std::string banText{ "Cheater has been permanently banned from official CS:GO servers." };
    ColorToggle3 bombTimer{ 1.0f, 0.55f, 0.0f };
    KeyBind prepareRevolverKey;
    int hitSound{ 0 };
    int quickHealthshotKey{ 0 };
    float maxAngleDelta{ 255.0f };
    int killSound{ 0 };
    std::string customKillSound;
    std::string customHitSound;
    PurchaseList purchaseList;

    struct Reportbot {
        bool enabled = false;
        bool textAbuse = false;
        bool griefing = false;
        bool wallhack = true;
        bool aimbot = true;
        bool other = true;
        int target = 0;
        int delay = 1;
        int rounds = 1;
    } reportbot;

    OffscreenEnemies offscreenEnemies;
} miscConfig;

Misc::Misc(const ClientInterfaces& clientInterfaces, const EngineInterfaces& engineInterfaces, const OtherInterfaces& otherInterfaces, const Memory& memory, const ClientPatternFinder& clientPatternFinder, const PatternFinder& enginePatternFinder)
    : clientInterfaces{ clientInterfaces }, engineInterfaces{ engineInterfaces }, interfaces{ otherInterfaces }, memory{ memory },
#if IS_WIN32() || IS_WIN64()
    setClanTag{ retSpoofGadgets->engine, enginePatternFinder("53 56 57 8B DA 8B F9 FF 15"_pat).get() },
#elif IS_LINUX()
   setClanTag{ retSpoofGadgets->engine, enginePatternFinder("E8 ? ? ? ? E9 ? ? ? ? 0F 1F 44 00 00 48 8B 7D B0"_pat).add(1).abs().get() },
#endif
submitReport{ retSpoofGadgets->client, clientPatternFinder.submitReport() }
{
 demoOrHLTV = clientPatternFinder.demoOrHLTV();
    money = clientPatternFinder.money();
    insertIntoTree = clientPatternFinder.insertIntoTree();
    demoFileEndReached = clientPatternFinder.demoFileEndReached();
}

bool Misc::isRadarHackOn() noexcept
{
    return miscConfig.radarHack;
}

bool Misc::isMenuKeyPressed() noexcept
{
    return miscConfig.menuKey.isPressed();
}

float Misc::maxAngleDelta() noexcept
{
    return miscConfig.maxAngleDelta;
}

float Misc::aspectRatio() noexcept
{
    return miscConfig.aspectratio;
}

void Misc::edgejump(csgo::UserCmd* cmd) noexcept
{
    if (!miscConfig.edgejump || !miscConfig.edgejumpkey.isDown())
        return;

    if (!localPlayer || !localPlayer.get().isAlive())
        return;

    if (const auto mt = localPlayer.get().moveType(); mt == MoveType::LADDER || mt == MoveType::NOCLIP)
        return;

    if ((EnginePrediction::getFlags() & 1) && !localPlayer.get().isOnGround())
        cmd->buttons |= csgo::UserCmd::IN_JUMP;
}

void Misc::slowwalk(csgo::UserCmd* cmd) noexcept
{
    if (!miscConfig.slowwalk || !miscConfig.slowwalkKey.isDown())
        return;

    if (!localPlayer || !localPlayer.get().isAlive())
        return;

    const auto activeWeapon = csgo::Entity::from(retSpoofGadgets->client, localPlayer.get().getActiveWeapon());
    if (activeWeapon.getPOD() == nullptr)
        return;

    const auto weaponData = activeWeapon.getWeaponData();
    if (!weaponData)
        return;

    const float maxSpeed = (localPlayer.get().isScoped() ? weaponData->maxSpeedAlt : weaponData->maxSpeed) / 3;

    if (cmd->forwardmove && cmd->sidemove) {
        const float maxSpeedRoot = maxSpeed * static_cast<float>(M_SQRT1_2);
        cmd->forwardmove = cmd->forwardmove < 0.0f ? -maxSpeedRoot : maxSpeedRoot;
        cmd->sidemove = cmd->sidemove < 0.0f ? -maxSpeedRoot : maxSpeedRoot;
    } else if (cmd->forwardmove) {
        cmd->forwardmove = cmd->forwardmove < 0.0f ? -maxSpeed : maxSpeed;
    } else if (cmd->sidemove) {
        cmd->sidemove = cmd->sidemove < 0.0f ? -maxSpeed : maxSpeed;
    }
}

void Misc::updateClanTag(bool tagChanged) noexcept
{
    static std::string clanTag;

    if (tagChanged) {
        clanTag = miscConfig.clanTag;
        if (!clanTag.empty() && clanTag.front() != ' ' && clanTag.back() != ' ')
            clanTag.push_back(' ');
        return;
    }
    
    static auto lastTime = 0.0f;

    if (miscConfig.clocktag) {
        if (memory.globalVars->realtime - lastTime < 1.0f)
            return;

        const auto time = std::time(nullptr);
        const auto localTime = std::localtime(&time);
        char s[11];
        s[0] = '\0';
        snprintf(s, sizeof(s), "[%02d:%02d:%02d]", localTime->tm_hour, localTime->tm_min, localTime->tm_sec);
        lastTime = memory.globalVars->realtime;
        setClanTag(s, s);
    } else if (miscConfig.customClanTag) {
        if (memory.globalVars->realtime - lastTime < 0.6f)
            return;

        if (miscConfig.animatedClanTag && !clanTag.empty()) {
            if (const auto offset = Helpers::utf8SeqLen(clanTag[0]); offset <= clanTag.length())
                std::rotate(clanTag.begin(), clanTag.begin() + offset, clanTag.end());
        }
        lastTime = memory.globalVars->realtime;
        setClanTag(clanTag.c_str(), clanTag.c_str());
    }
}

void Misc::spectatorList() noexcept
{
    if (!miscConfig.spectatorList.enabled)
        return;

    GameData::Lock lock;

    const auto& observers = GameData::observers();

    if (std::ranges::none_of(observers, [](const auto& obs) { return obs.targetIsLocalPlayer; }) && !gui->isOpen())
        return;

    if (miscConfig.spectatorList.pos != ImVec2{}) {
        ImGui::SetNextWindowPos(miscConfig.spectatorList.pos);
        miscConfig.spectatorList.pos = {};
    }

    if (miscConfig.spectatorList.size != ImVec2{}) {
        ImGui::SetNextWindowSize(ImClamp(miscConfig.spectatorList.size, {}, ImGui::GetIO().DisplaySize));
        miscConfig.spectatorList.size = {};
    }

    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoCollapse;
    if (!gui->isOpen())
        windowFlags |= ImGuiWindowFlags_NoInputs;
    if (miscConfig.spectatorList.noTitleBar)
        windowFlags |= ImGuiWindowFlags_NoTitleBar;

    if (!gui->isOpen())
        ImGui::PushStyleColor(ImGuiCol_TitleBg, ImGui::GetColorU32(ImGuiCol_TitleBgActive));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowTitleAlign, { 0.5f, 0.5f });
    ImGui::Begin("观战者名单", nullptr, windowFlags);
    ImGui::PopStyleVar();

    if (!gui->isOpen())
        ImGui::PopStyleColor();

    for (const auto& observer : observers) {
        if (!observer.targetIsLocalPlayer)
            continue;

        if (const auto it = std::ranges::find(GameData::players(), observer.playerHandle, &PlayerData::handle); it != GameData::players().cend()) {
            if (const auto texture = it->getAvatarTexture()) {
                const auto textSize = ImGui::CalcTextSize(it->name.c_str());
                ImGui::Image(texture, ImVec2(textSize.y, textSize.y), ImVec2(0, 0), ImVec2(1, 1), ImVec4(1, 1, 1, 1), ImVec4(1, 1, 1, 0.3f));
                ImGui::SameLine();
                ImGui::TextWrapped("%s", it->name.c_str());
            }
        }
    }

    ImGui::End();
}

static void drawCrosshair(ImDrawList* drawList, const ImVec2& pos, ImU32 color) noexcept
{
    // dot
    drawList->AddRectFilled(pos - ImVec2{ 1, 1 }, pos + ImVec2{ 2, 2 }, color & IM_COL32_A_MASK);
    drawList->AddRectFilled(pos, pos + ImVec2{ 1, 1 }, color);

    // left
    drawList->AddRectFilled(ImVec2{ pos.x - 11, pos.y - 1 }, ImVec2{ pos.x - 3, pos.y + 2 }, color & IM_COL32_A_MASK);
    drawList->AddRectFilled(ImVec2{ pos.x - 10, pos.y }, ImVec2{ pos.x - 4, pos.y + 1 }, color);

    // right
    drawList->AddRectFilled(ImVec2{ pos.x + 4, pos.y - 1 }, ImVec2{ pos.x + 12, pos.y + 2 }, color & IM_COL32_A_MASK);
    drawList->AddRectFilled(ImVec2{ pos.x + 5, pos.y }, ImVec2{ pos.x + 11, pos.y + 1 }, color);

    // top (left with swapped x/y offsets)
    drawList->AddRectFilled(ImVec2{ pos.x - 1, pos.y - 11 }, ImVec2{ pos.x + 2, pos.y - 3 }, color & IM_COL32_A_MASK);
    drawList->AddRectFilled(ImVec2{ pos.x, pos.y - 10 }, ImVec2{ pos.x + 1, pos.y - 4 }, color);

    // bottom (right with swapped x/y offsets)
    drawList->AddRectFilled(ImVec2{ pos.x - 1, pos.y + 4 }, ImVec2{ pos.x + 2, pos.y + 12 }, color & IM_COL32_A_MASK);
    drawList->AddRectFilled(ImVec2{ pos.x, pos.y + 5 }, ImVec2{ pos.x + 1, pos.y + 11 }, color);
}

void Misc::noscopeCrosshair(ImDrawList* drawList) noexcept
{
    if (!miscConfig.noscopeCrosshair.asColorToggle().enabled)
        return;

    {
        GameData::Lock lock;
        if (const auto& local = GameData::local(); !local.exists || !local.alive || !local.noScope)
            return;
    }

    drawCrosshair(drawList, ImGui::GetIO().DisplaySize / 2, Helpers::calculateColor(memory.globalVars->realtime, miscConfig.noscopeCrosshair.asColorToggle().asColor4()));
}

void Misc::recoilCrosshair(ImDrawList* drawList) noexcept
{
    if (!miscConfig.recoilCrosshair.asColorToggle().enabled)
        return;

    GameData::Lock lock;
    const auto& localPlayerData = GameData::local();

    if (!localPlayerData.exists || !localPlayerData.alive)
        return;

    if (!localPlayerData.shooting)
        return;

    if (ImVec2 pos; Helpers::worldToScreenPixelAligned(localPlayerData.aimPunch, pos))
        drawCrosshair(drawList, pos, Helpers::calculateColor(memory.globalVars->realtime, miscConfig.recoilCrosshair.asColorToggle().asColor4()));
}

void Misc::watermark() noexcept
{
    if (!miscConfig.watermark.enabled)
        return;

    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize;
    if (!gui->isOpen())
        windowFlags |= ImGuiWindowFlags_NoInputs;

    ImGui::SetNextWindowBgAlpha(0.3f);
    ImGui::Begin("Watermark", nullptr, windowFlags);

    static auto frameRate = 1.0f;
    frameRate = 0.9f * frameRate + 0.1f * memory.globalVars->absoluteFrameTime;

    ImGui::Text("QS | %d fps | %d ms", frameRate != 0.0f ? static_cast<int>(1 / frameRate) : 0, GameData::getNetOutgoingLatency());
    ImGui::End();
}

void Misc::prepareRevolver(csgo::UserCmd* cmd) noexcept
{
    auto timeToTicks = [this](float time) {  return static_cast<int>(0.5f + time / memory.globalVars->intervalPerTick); };
    constexpr float revolverPrepareTime{ 0.234375f };

    static float readyTime;
    if (miscConfig.prepareRevolver && localPlayer && (!miscConfig.prepareRevolverKey.isSet() || miscConfig.prepareRevolverKey.isDown())) {
        const auto activeWeapon = csgo::Entity::from(retSpoofGadgets->client, localPlayer.get().getActiveWeapon());
        if (activeWeapon.getPOD() != nullptr && activeWeapon.itemDefinitionIndex() == WeaponId::Revolver) {
            if (!readyTime) readyTime = memory.globalVars->serverTime() + revolverPrepareTime;
            auto ticksToReady = timeToTicks(readyTime - memory.globalVars->serverTime() - csgo::NetworkChannel::from(retSpoofGadgets->client, engineInterfaces.getEngine().getNetworkChannel()).getLatency(0));
            if (ticksToReady > 0 && ticksToReady <= timeToTicks(revolverPrepareTime))
                cmd->buttons |= csgo::UserCmd::IN_ATTACK;
            else
                readyTime = 0.0f;
        }
    }
}

void Misc::fastPlant(csgo::UserCmd* cmd) noexcept
{
    if (!miscConfig.fastPlant)
        return;

    if (static auto plantAnywhere = csgo::ConVar::from(retSpoofGadgets->client, interfaces.getCvar().findVar(csgo::mp_plant_c4_anywhere)); plantAnywhere.getInt())
        return;

    if (!localPlayer || !localPlayer.get().isAlive() || (localPlayer.get().inBombZone() && localPlayer.get().isOnGround()))
        return;

    if (const auto activeWeapon = csgo::Entity::from(retSpoofGadgets->client, localPlayer.get().getActiveWeapon()); activeWeapon.getPOD() == nullptr || activeWeapon.getNetworkable().getClientClass()->classId != ClassId::C4)
        return;

    cmd->buttons &= ~csgo::UserCmd::IN_ATTACK;

    constexpr auto doorRange = 200.0f;

    csgo::Trace trace;
    const auto startPos = localPlayer.get().getEyePosition();
    const auto endPos = startPos + csgo::Vector::fromAngle(cmd->viewangles) * doorRange;
    engineInterfaces.engineTrace().traceRay({ startPos, endPos }, 0x46004009, localPlayer.get().getPOD(), trace);

    const auto entity = csgo::Entity::from(retSpoofGadgets->client, trace.entity);
    if (entity.getPOD() == nullptr || entity.getNetworkable().getClientClass()->classId != ClassId::PropDoorRotating)
        cmd->buttons &= ~csgo::UserCmd::IN_USE;
}

void Misc::fastStop(csgo::UserCmd* cmd) noexcept
{
    if (!miscConfig.fastStop)
        return;

    if (!localPlayer || !localPlayer.get().isAlive())
        return;

    if (localPlayer.get().moveType() == MoveType::NOCLIP || localPlayer.get().moveType() == MoveType::LADDER || !localPlayer.get().isOnGround() || cmd->buttons & csgo::UserCmd::IN_JUMP)
        return;

    if (cmd->buttons & (csgo::UserCmd::IN_MOVELEFT | csgo::UserCmd::IN_MOVERIGHT | csgo::UserCmd::IN_FORWARD | csgo::UserCmd::IN_BACK))
        return;
    
    const auto velocity = localPlayer.get().velocity();
    const auto speed = velocity.length2D();
    if (speed < 15.0f)
        return;
    
    csgo::Vector direction = velocity.toAngle();
    direction.y = cmd->viewangles.y - direction.y;

    const auto negatedDirection = csgo::Vector::fromAngle(direction) * -speed;
    cmd->forwardmove = negatedDirection.x;
    cmd->sidemove = negatedDirection.y;
}

void Misc::drawBombTimer() noexcept
{
    if (!miscConfig.bombTimer.enabled)
        return;

    GameData::Lock lock;
    
    const auto& plantedC4 = GameData::plantedC4();
    if (plantedC4.blowTime == 0.0f && !gui->isOpen())
        return;

    if (!gui->isOpen()) {
        ImGui::SetNextWindowBgAlpha(0.3f);
    }

    static float windowWidth = 200.0f;
    ImGui::SetNextWindowPos({ (ImGui::GetIO().DisplaySize.x - 200.0f) / 2.0f, 60.0f }, ImGuiCond_Once);
    ImGui::SetNextWindowSize({ windowWidth, 0 }, ImGuiCond_Once);

    if (!gui->isOpen())
        ImGui::SetNextWindowSize({ windowWidth, 0 });

    ImGui::SetNextWindowSizeConstraints({ 0, -1 }, { FLT_MAX, -1 });
    ImGui::Begin("炸弹计时器", nullptr, ImGuiWindowFlags_NoTitleBar | (gui->isOpen() ? 0 : ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoDecoration));

    std::ostringstream ss; ss << "Bomb on " << (!plantedC4.bombsite ? 'A' : 'B') << " : " << std::fixed << std::showpoint << std::setprecision(3) << (std::max)(plantedC4.blowTime - memory.globalVars->currenttime, 0.0f) << " s";
    ImGui::textUnformattedCentered(ss.str().c_str());

    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, Helpers::calculateColor(memory.globalVars->realtime, miscConfig.bombTimer.asColor3()));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4{ 0.2f, 0.2f, 0.2f, 1.0f });
    ImGui::progressBarFullWidth((plantedC4.blowTime - memory.globalVars->currenttime) / plantedC4.timerLength, 5.0f);

    if (plantedC4.defuserHandle != -1) {
        const bool canDefuse = plantedC4.blowTime >= plantedC4.defuseCountDown;

        if (plantedC4.defuserHandle == GameData::local().handle) {
            if (canDefuse) {
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 255, 0, 255));
                ImGui::textUnformattedCentered("可以拆包!");
            } else {
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 0, 0, 255));
                ImGui::textUnformattedCentered("不能拆包!");
            }
            ImGui::PopStyleColor();
        } else if (const auto defusingPlayer = GameData::playerByHandle(plantedC4.defuserHandle)) {
            std::ostringstream ss; ss << defusingPlayer->name << " 正在拆包: " << std::fixed << std::showpoint << std::setprecision(3) << (std::max)(plantedC4.defuseCountDown - memory.globalVars->currenttime, 0.0f) << " s";
            ImGui::textUnformattedCentered(ss.str().c_str());

            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, canDefuse ? IM_COL32(0, 255, 0, 255) : IM_COL32(255, 0, 0, 255));
            ImGui::progressBarFullWidth((plantedC4.defuseCountDown - memory.globalVars->currenttime) / plantedC4.defuseLength, 5.0f);
            ImGui::PopStyleColor();
        }
    }

    windowWidth = ImGui::GetCurrentWindow()->SizeFull.x;

    ImGui::PopStyleColor(2);
    ImGui::End();
}

void Misc::stealNames() noexcept
{
    if (!miscConfig.nameStealer)
        return;

    if (!localPlayer)
        return;

    static std::vector<int> stolenIds;

    for (int i = 1; i <= memory.globalVars->maxClients; ++i) {
        const auto entityPtr = clientInterfaces.getEntityList().getEntity(i);
        const auto entity = csgo::Entity::from(retSpoofGadgets->client, entityPtr);

        if (entity.getPOD() == nullptr || entity.getPOD() == localPlayer.get().getPOD())
            continue;

        csgo::PlayerInfo playerInfo;
        if (!engineInterfaces.getEngine().getPlayerInfo(entity.getNetworkable().index(), playerInfo))
            continue;

        if (playerInfo.fakeplayer || std::ranges::find(stolenIds, playerInfo.userId) != stolenIds.cend())
            continue;

        if (changeName(false, (std::string{ playerInfo.name } +'\x1').c_str(), 1.0f))
            stolenIds.push_back(playerInfo.userId);

        return;
    }
    stolenIds.clear();
}

void Misc::disablePanoramablur() noexcept
{
    static auto blur = interfaces.getCvar().findVar(csgo::panorama_disable_blur);
    csgo::ConVar::from(retSpoofGadgets->client, blur).setValue(miscConfig.disablePanoramablur);
}

void Misc::quickReload(csgo::UserCmd* cmd) noexcept
{
    if (miscConfig.quickReload) {
        static csgo::EntityPOD* reloadedWeapon = nullptr;

        if (reloadedWeapon) {
            for (auto weaponHandle : localPlayer.get().weapons()) {
                if (weaponHandle == -1)
                    break;

                if (clientInterfaces.getEntityList().getEntityFromHandle(weaponHandle) == reloadedWeapon) {
                    cmd->weaponselect = csgo::Entity::from(retSpoofGadgets->client, reloadedWeapon).getNetworkable().index();
                    cmd->weaponsubtype = csgo::Entity::from(retSpoofGadgets->client, reloadedWeapon).getWeaponSubType();
                    break;
                }
            }
            reloadedWeapon = nullptr;
        }

        if (const auto activeWeapon = csgo::Entity::from(retSpoofGadgets->client, localPlayer.get().getActiveWeapon()); activeWeapon.getPOD() != nullptr && activeWeapon.isInReload() && activeWeapon.clip() == activeWeapon.getWeaponData()->maxClip) {
            reloadedWeapon = activeWeapon.getPOD();

            for (auto weaponHandle : localPlayer.get().weapons()) {
                if (weaponHandle == -1)
                    break;

                if (const auto weapon = csgo::Entity::from(retSpoofGadgets->client, clientInterfaces.getEntityList().getEntityFromHandle(weaponHandle)); weapon.getPOD() && weapon.getPOD() != reloadedWeapon) {
                    cmd->weaponselect = weapon.getNetworkable().index();
                    cmd->weaponsubtype = weapon.getWeaponSubType();
                    break;
                }
            }
        }
    }
}

bool Misc::changeName(bool reconnect, const char* newName, float delay) noexcept
{
    static auto exploitInitialized{ false };

    static auto name{ interfaces.getCvar().findVar(csgo::name) };

    if (reconnect) {
        exploitInitialized = false;
        return false;
    }

    if (!exploitInitialized && engineInterfaces.getEngine().isInGame()) {
        if (csgo::PlayerInfo playerInfo; localPlayer && engineInterfaces.getEngine().getPlayerInfo(localPlayer.get().getNetworkable().index(), playerInfo) && (!strcmp(playerInfo.name, "?empty") || !strcmp(playerInfo.name, "\n\xAD\xAD\xAD"))) {
            exploitInitialized = true;
        } else {
            name->onChangeCallbacks.size = 0;
            csgo::ConVar::from(retSpoofGadgets->client, name).setValue("\n\xAD\xAD\xAD");
            return false;
        }
    }

    if (static auto nextChangeTime = 0.0f; nextChangeTime <= memory.globalVars->realtime) {
        csgo::ConVar::from(retSpoofGadgets->client, name).setValue(newName);
        nextChangeTime = memory.globalVars->realtime + delay;
        return true;
    }
    return false;
}

void Misc::bunnyHop(csgo::UserCmd* cmd) noexcept
{
    if (!localPlayer)
        return;

    static auto wasLastTimeOnGround{ localPlayer.get().isOnGround() };

    if (miscConfig.bunnyHop && !localPlayer.get().isOnGround() && localPlayer.get().moveType() != MoveType::LADDER && !wasLastTimeOnGround)
        cmd->buttons &= ~csgo::UserCmd::IN_JUMP;

    wasLastTimeOnGround = localPlayer.get().isOnGround();
}

void Misc::fakeBan(bool set) noexcept
{
    static bool shouldSet = false;

    if (set)
        shouldSet = set;

    if (shouldSet && engineInterfaces.getEngine().isInGame() && changeName(false, std::string{ "\x1\xB" }.append(std::string{ static_cast<char>(miscConfig.banColor + 1) }).append(miscConfig.banText).append("\x1").c_str(), 5.0f))
        shouldSet = false;
}

void Misc::nadePredict() noexcept
{
    static auto nadeVar{ interfaces.getCvar().findVar(csgo::cl_grenadepreview) };

    nadeVar->onChangeCallbacks.size = 0;
    csgo::ConVar::from(retSpoofGadgets->client, nadeVar).setValue(miscConfig.nadePredict);
}

void Misc::fixTabletSignal() noexcept
{
    if (miscConfig.fixTabletSignal && localPlayer) {
        if (const auto activeWeapon = csgo::Entity::from(retSpoofGadgets->client, localPlayer.get().getActiveWeapon()); activeWeapon.getPOD() != nullptr && activeWeapon.getNetworkable().getClientClass()->classId == ClassId::Tablet)
            activeWeapon.tabletReceptionIsBlocked() = false;
    }
}

void Misc::killMessage(const csgo::GameEvent& event) noexcept
{
    if (!miscConfig.killMessage)
        return;

    if (!localPlayer || !localPlayer.get().isAlive())
        return;

    if (const auto localUserId = localPlayer.get().getUserId(engineInterfaces.getEngine()); event.getInt("attacker") != localUserId || event.getInt("userid") == localUserId)
        return;

    std::string cmd = "say \"";
    cmd += miscConfig.killMessageString;
    cmd += '"';
    engineInterfaces.getEngine().clientCmdUnrestricted(cmd.c_str());
}

void Misc::fixMovement(csgo::UserCmd* cmd, float yaw) noexcept
{
    if (miscConfig.fixMovement) {
        float oldYaw = yaw + (yaw < 0.0f ? 360.0f : 0.0f);
        float newYaw = cmd->viewangles.y + (cmd->viewangles.y < 0.0f ? 360.0f : 0.0f);
        float yawDelta = newYaw < oldYaw ? fabsf(newYaw - oldYaw) : 360.0f - fabsf(newYaw - oldYaw);
        yawDelta = 360.0f - yawDelta;

        const float forwardmove = cmd->forwardmove;
        const float sidemove = cmd->sidemove;
        cmd->forwardmove = std::cos(Helpers::deg2rad(yawDelta)) * forwardmove + std::cos(Helpers::deg2rad(yawDelta + 90.0f)) * sidemove;
        cmd->sidemove = std::sin(Helpers::deg2rad(yawDelta)) * forwardmove + std::sin(Helpers::deg2rad(yawDelta + 90.0f)) * sidemove;
    }
}

void Misc::antiAfkKick(csgo::UserCmd* cmd) noexcept
{
    if (miscConfig.antiAfkKick && cmd->commandNumber % 2)
        cmd->buttons |= 1 << 27;
}

void Misc::fixAnimationLOD(csgo::FrameStage stage) noexcept
{
#if IS_WIN32()
    if (miscConfig.fixAnimationLOD && stage == csgo::FrameStage::RENDER_START) {
        if (!localPlayer)
            return;

        for (int i = 1; i <= engineInterfaces.getEngine().getMaxClients(); i++) {
            const auto entity = csgo::Entity::from(retSpoofGadgets->client, clientInterfaces.getEntityList().getEntity(i));
            if (entity.getPOD() == nullptr || entity.getPOD() == localPlayer.get().getPOD() || entity.getNetworkable().isDormant() || !entity.isAlive()) continue;
            *reinterpret_cast<int*>(std::uintptr_t(entity.getPOD()) + 0xA28) = 0;
            *reinterpret_cast<int*>(std::uintptr_t(entity.getPOD()) + 0xA30) = memory.globalVars->framecount;
        }
    }
#endif
}

void Misc::autoPistol(csgo::UserCmd* cmd) noexcept
{
    if (miscConfig.autoPistol && localPlayer) {
        const auto activeWeapon = csgo::Entity::from(retSpoofGadgets->client, localPlayer.get().getActiveWeapon());
        if (activeWeapon.getPOD() != nullptr && activeWeapon.isPistol() && activeWeapon.nextPrimaryAttack() > memory.globalVars->serverTime()) {
            if (activeWeapon.itemDefinitionIndex() == WeaponId::Revolver)
                cmd->buttons &= ~csgo::UserCmd::IN_ATTACK2;
            else
                cmd->buttons &= ~csgo::UserCmd::IN_ATTACK;
        }
    }
}

void Misc::autoReload(csgo::UserCmd* cmd) noexcept
{
    if (miscConfig.autoReload && localPlayer) {
        const auto activeWeapon = csgo::Entity::from(retSpoofGadgets->client, localPlayer.get().getActiveWeapon());
        if (activeWeapon.getPOD() != nullptr && getWeaponIndex(activeWeapon.itemDefinitionIndex()) && !activeWeapon.clip())
            cmd->buttons &= ~(csgo::UserCmd::IN_ATTACK | csgo::UserCmd::IN_ATTACK2);
    }
}

void Misc::revealRanks(csgo::UserCmd* cmd) noexcept
{
    if (miscConfig.revealRanks && cmd->buttons & csgo::UserCmd::IN_SCORE)
        clientInterfaces.getClient().dispatchUserMessage(50, 0, 0, nullptr);
}

void Misc::autoStrafe(csgo::UserCmd* cmd) noexcept
{
    if (localPlayer
        && miscConfig.autoStrafe
        && !localPlayer.get().isOnGround()
        && localPlayer.get().moveType() != MoveType::NOCLIP) {
        if (cmd->mousedx < 0)
            cmd->sidemove = -450.0f;
        else if (cmd->mousedx > 0)
            cmd->sidemove = 450.0f;
    }
}

void Misc::removeCrouchCooldown(csgo::UserCmd* cmd) noexcept
{
    if (miscConfig.fastDuck)
        cmd->buttons |= csgo::UserCmd::IN_BULLRUSH;
}

void Misc::moonwalk(csgo::UserCmd* cmd) noexcept
{
    if (miscConfig.moonwalk && localPlayer && localPlayer.get().moveType() != MoveType::LADDER)
        cmd->buttons ^= csgo::UserCmd::IN_FORWARD | csgo::UserCmd::IN_BACK | csgo::UserCmd::IN_MOVELEFT | csgo::UserCmd::IN_MOVERIGHT;
}

void Misc::playHitSound(const csgo::GameEvent& event) noexcept
{
    if (!miscConfig.hitSound)
        return;

    if (!localPlayer)
        return;

    if (const auto localUserId = localPlayer.get().getUserId(engineInterfaces.getEngine()); event.getInt("attacker") != localUserId || event.getInt("userid") == localUserId)
        return;

    static constexpr std::array hitSounds{
        "play physics/metal/metal_solid_impact_bullet2",
        "play buttons/arena_switch_press_02",
        "play training/timer_bell",
        "play physics/glass/glass_impact_bullet1"
    };

    if (static_cast<std::size_t>(miscConfig.hitSound - 1) < hitSounds.size())
        engineInterfaces.getEngine().clientCmdUnrestricted(hitSounds[miscConfig.hitSound - 1]);
    else if (miscConfig.hitSound == 5)
        engineInterfaces.getEngine().clientCmdUnrestricted(("play " + miscConfig.customHitSound).c_str());
}

void Misc::killSound(const csgo::GameEvent& event) noexcept
{
    if (!miscConfig.killSound)
        return;

    if (!localPlayer || !localPlayer.get().isAlive())
        return;

    if (const auto localUserId = localPlayer.get().getUserId(engineInterfaces.getEngine()); event.getInt("attacker") != localUserId || event.getInt("userid") == localUserId)
        return;

    static constexpr std::array killSounds{
        "play physics/metal/metal_solid_impact_bullet2",
        "play buttons/arena_switch_press_02",
        "play training/timer_bell",
        "play physics/glass/glass_impact_bullet1"
    };

    if (static_cast<std::size_t>(miscConfig.killSound - 1) < killSounds.size())
        engineInterfaces.getEngine().clientCmdUnrestricted(killSounds[miscConfig.killSound - 1]);
    else if (miscConfig.killSound == 5)
        engineInterfaces.getEngine().clientCmdUnrestricted(("play " + miscConfig.customKillSound).c_str());
}

void Misc::purchaseList(const csgo::GameEvent* event) noexcept
{
    static std::mutex mtx;
    std::scoped_lock _{ mtx };

    struct PlayerPurchases {
        int totalCost;
        std::unordered_map<std::string, int> items;
    };

    static std::unordered_map<int, PlayerPurchases> playerPurchases;
    static std::unordered_map<std::string, int> purchaseTotal;
    static int totalCost;

    static auto freezeEnd = 0.0f;

    if (event) {
        switch (fnv::hashRuntime(event->getName())) {
        case fnv::hash("item_purchase"): {
            if (const auto player = csgo::Entity::from(retSpoofGadgets->client, clientInterfaces.getEntityList().getEntity(engineInterfaces.getEngine().getPlayerForUserID(event->getInt("userid")))); player.getPOD() != nullptr && localPlayer && localPlayer.get().isOtherEnemy(memory, player)) {
                if (const auto definition = csgo::EconItemDefinition::from(retSpoofGadgets->client, csgo::ItemSchema::from(retSpoofGadgets->client, memory.itemSystem().getItemSchema()).getItemDefinitionByName(event->getString("weapon"))); definition.getPOD() != nullptr) {
                    auto& purchase = playerPurchases[player.handle()];
                    if (const auto weaponInfo = memory.weaponSystem.getWeaponInfo(definition.getWeaponId())) {
                        purchase.totalCost += weaponInfo->price;
                        totalCost += weaponInfo->price;
                    }
                    const std::string weapon = interfaces.getLocalize().findAsUTF8(definition.getItemBaseName());
                    ++purchaseTotal[weapon];
                    ++purchase.items[weapon];
                }
            }
            break;
        }
        case fnv::hash("round_start"):
            freezeEnd = 0.0f;
            playerPurchases.clear();
            purchaseTotal.clear();
            totalCost = 0;
            break;
        case fnv::hash("round_freeze_end"):
            freezeEnd = memory.globalVars->realtime;
            break;
        }
    } else {
        if (!miscConfig.purchaseList.enabled)
            return;

        if (static const auto mp_buytime = interfaces.getCvar().findVar(csgo::mp_buytime); (!engineInterfaces.getEngine().isInGame() || freezeEnd != 0.0f && memory.globalVars->realtime > freezeEnd + (!miscConfig.purchaseList.onlyDuringFreezeTime ? csgo::ConVar::from(retSpoofGadgets->client, mp_buytime).getFloat() : 0.0f) || playerPurchases.empty() || purchaseTotal.empty()) && !gui->isOpen())
            return;

        ImGui::SetNextWindowSize({ 200.0f, 200.0f }, ImGuiCond_Once);

        ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoCollapse;
        if (!gui->isOpen())
            windowFlags |= ImGuiWindowFlags_NoInputs;
        if (miscConfig.purchaseList.noTitleBar)
            windowFlags |= ImGuiWindowFlags_NoTitleBar;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowTitleAlign, { 0.5f, 0.5f });
        ImGui::Begin("购买", nullptr, windowFlags);
        ImGui::PopStyleVar();

        if (miscConfig.purchaseList.mode == PurchaseList::Details) {
            GameData::Lock lock;

            StringBuilderStorage<500> stringBuilderStorage;
            for (const auto& [handle, purchases] : playerPurchases) {
                auto stringBuilder = stringBuilderStorage.builder();

                bool printedFirst = false;
                for (const auto& purchasedItem : purchases.items) {
                    if (printedFirst)
                        stringBuilder.put(", ");
                    
                    if (purchasedItem.second > 1)
                        stringBuilder.put(purchasedItem.second, "x ");
                    stringBuilder.put(purchasedItem.first);
                    printedFirst = true;
                }
                

                if (const auto player = GameData::playerByHandle(handle)) {
                    if (miscConfig.purchaseList.showPrices)
                        ImGui::TextWrapped("%s $%d: %s", player->name.c_str(), purchases.totalCost, stringBuilder.cstring());
                    else
                        ImGui::TextWrapped("%s: %s", player->name.c_str(), stringBuilder.cstring());
                }
            }
        } else if (miscConfig.purchaseList.mode == PurchaseList::Summary) {
            for (const auto& purchase : purchaseTotal)
                ImGui::TextWrapped("%d x %s", purchase.second, purchase.first.c_str());

            if (miscConfig.purchaseList.showPrices && totalCost > 0) {
                ImGui::Separator();
                ImGui::TextWrapped("总共: $%d", totalCost);
            }
        }
        ImGui::End();
    }
}

void Misc::oppositeHandKnife(csgo::FrameStage stage) noexcept
{
    if (!miscConfig.oppositeHandKnife)
        return;

    if (!localPlayer)
        return;

    if (stage != csgo::FrameStage::RENDER_START && stage != csgo::FrameStage::RENDER_END)
        return;

    static const auto cl_righthand = csgo::ConVar::from(retSpoofGadgets->client, interfaces.getCvar().findVar(csgo::cl_righthand));
    static bool original;

    if (stage == csgo::FrameStage::RENDER_START) {
        original = cl_righthand.getInt();

        if (const auto activeWeapon = csgo::Entity::from(retSpoofGadgets->client, localPlayer.get().getActiveWeapon()); activeWeapon.getPOD() != nullptr) {
            if (const auto classId = activeWeapon.getNetworkable().getClientClass()->classId; classId == ClassId::Knife || classId == ClassId::KnifeGG)
                cl_righthand.setValue(!original);
        }
    } else {
        cl_righthand.setValue(original);
    }
}

static std::vector<std::uint64_t> reportedPlayers;
static int reportbotRound;

static void generateReportString(StringBuilder& builder)
{
    
    if (miscConfig.reportbot.textAbuse)
        builder.put("textabuse,");
    if (miscConfig.reportbot.griefing)
        builder.put("grief,");
    if (miscConfig.reportbot.wallhack)
        builder.put("wallhack,");
    if (miscConfig.reportbot.aimbot)
        builder.put("aimbot,");
    if (miscConfig.reportbot.other)
        builder.put("speedhack,");
}

[[nodiscard]] static bool isPlayerReported(std::uint64_t xuid)
{
    return std::ranges::find(std::as_const(reportedPlayers), xuid) != reportedPlayers.cend();
}

[[nodiscard]] static std::vector<std::uint64_t> getXuidsOfCandidatesToBeReported(const csgo::Engine& engine, const ClientInterfaces& clientInterfaces, const OtherInterfaces& interfaces, const Memory& memory)
{
    std::vector<std::uint64_t> xuids;

    for (int i = 1; i <= engine.getMaxClients(); ++i) {
        const auto entity = csgo::Entity::from(retSpoofGadgets->client, clientInterfaces.getEntityList().getEntity(i));
        if (entity.getPOD() == 0 || entity.getPOD() == localPlayer.get().getPOD())
            continue;

        if (miscConfig.reportbot.target != 2 && (localPlayer.get().isOtherEnemy(memory, entity) ? miscConfig.reportbot.target != 0 : miscConfig.reportbot.target != 1))
            continue;

        if (csgo::PlayerInfo playerInfo; engine.getPlayerInfo(i, playerInfo) && !playerInfo.fakeplayer)
            xuids.push_back(playerInfo.xuid);
    }

    return xuids;
}

void Misc::runReportbot() noexcept
{
    if (!miscConfig.reportbot.enabled)
        return;

    if (!localPlayer)
        return;

    static auto lastReportTime = 0.0f;

    if (lastReportTime + miscConfig.reportbot.delay > memory.globalVars->realtime)
        return;

    if (reportbotRound >= miscConfig.reportbot.rounds)
        return;
StringBuilderStorage<100> stringBuilderStorage;
    for (const auto& xuid : getXuidsOfCandidatesToBeReported(engineInterfaces.getEngine(), clientInterfaces, interfaces, memory)) {
        if (isPlayerReported(xuid))
            continue;

        auto stringBuilder = stringBuilderStorage.builder();
        generateReportString(stringBuilder);
        if (const auto report = stringBuilder.cstring(); report[0] != '\0') {
            submitReport(LINUX_ARGS(nullptr,) std::to_string(xuid).c_str(), report);
            lastReportTime = memory.globalVars->realtime;
            reportedPlayers.push_back(xuid);
            return;
        }
    }

    reportedPlayers.clear();
    ++reportbotRound;
}

void Misc::resetReportbot() noexcept
{
    reportbotRound = 0;
    reportedPlayers.clear();
}

void Misc::preserveKillfeed(bool roundStart) noexcept
{
    if (!miscConfig.preserveKillfeed.enabled)
        return;

    static auto nextUpdate = 0.0f;

    if (roundStart) {
        nextUpdate = memory.globalVars->realtime + 10.0f;
        return;
    }

    if (nextUpdate > memory.globalVars->realtime)
        return;

    nextUpdate = memory.globalVars->realtime + 2.0f;

    const auto deathNotice = std::uintptr_t(memory.findHudElement(memory.hud, "CCSGO_HudDeathNotice"));
    if (!deathNotice)
        return;

    const auto deathNoticePanel = csgo::UIPanel::from(retSpoofGadgets->client, (*(csgo::UIPanelPOD**)(*reinterpret_cast<std::uintptr_t*>(deathNotice WIN32_LINUX(-20 + 88, -32 + 128)) + sizeof(std::uintptr_t))));

    const auto childPanelCount = deathNoticePanel.getChildCount();

    for (int i = 0; i < childPanelCount; ++i) {
        const auto childPointer = deathNoticePanel.getChild(i);
        if (!childPointer)
            continue;

        const auto child = csgo::UIPanel::from(retSpoofGadgets->client, childPointer);
        if (child.hasClass("DeathNotice_Killer") && (!miscConfig.preserveKillfeed.onlyHeadshots || child.hasClass("DeathNoticeHeadShot")))
            child.setAttributeFloat("SpawnTime", memory.globalVars->currenttime);
    }
}

void Misc::voteRevealer(const csgo::GameEvent& event) noexcept
{
    if (!miscConfig.revealVotes)
        return;

    const auto entity = csgo::Entity::from(retSpoofGadgets->client, clientInterfaces.getEntityList().getEntity(event.getInt("entityid")));
    if (entity.getPOD() == nullptr || !entity.isPlayer())
        return;
    
    const auto votedYes = event.getInt("vote_option") == 0;
    const auto isLocal = localPlayer && entity.getPOD() == localPlayer.get().getPOD();
    const char color = votedYes ? '\x06' : '\x07';

    csgo::HudChat::from(retSpoofGadgets->client, memory.clientMode->hudChat).printf(0, " \x0C\u2022QS\u2022 %c%s\x01 voted %c%s\x01", isLocal ? '\x01' : color, isLocal ? "You" : entity.getPlayerName(interfaces, memory).c_str(), color, votedYes ? "Yes" : "No");
}

void Misc::onVoteStart(const void* data, int size) noexcept
{
    if (!miscConfig.revealVotes)
        return;

    constexpr auto voteName = [](int index) {
        switch (index) {
        case 0: return "Kick";
        case 1: return "Change Level";
        case 6: return "Surrender";
        case 13: return "Start TimeOut";
        default: return "";
        }
    };

    const auto reader = ProtobufReader{ static_cast<const std::uint8_t*>(data), size };
    const auto entityIndex = reader.readInt32(2);

    const auto entity = csgo::Entity::from(retSpoofGadgets->client, clientInterfaces.getEntityList().getEntity(entityIndex));
    if (entity.getPOD() == nullptr || !entity.isPlayer())
        return;

    const auto isLocal = localPlayer && entity.getPOD() == localPlayer.get().getPOD();

    const auto voteType = reader.readInt32(3);
    csgo::HudChat::from(retSpoofGadgets->client, memory.clientMode->hudChat).printf(0, " \x0C\u2022QS\u2022 %c%s\x01 call vote (\x06%s\x01)", isLocal ? '\x01' : '\x06', isLocal ? "You" : entity.getPlayerName(interfaces, memory).c_str(), voteName(voteType));
}

void Misc::onVotePass() noexcept
{
    if (miscConfig.revealVotes)
        csgo::HudChat::from(retSpoofGadgets->client, memory.clientMode->hudChat).printf(0, " \x0C\u2022QS\u2022\x01 Vote\x06 PASSED");
}

void Misc::onVoteFailed() noexcept
{
    if (miscConfig.revealVotes)
        csgo::HudChat::from(retSpoofGadgets->client, memory.clientMode->hudChat).printf(0, " \x0C\u2022QS\u2022\x01 Vote\x07 FAILED");
}

// ImGui::ShadeVertsLinearColorGradientKeepAlpha() modified to do interpolation in HSV
static void shadeVertsHSVColorGradientKeepAlpha(ImDrawList* draw_list, int vert_start_idx, int vert_end_idx, ImVec2 gradient_p0, ImVec2 gradient_p1, ImU32 col0, ImU32 col1)
{
    ImVec2 gradient_extent = gradient_p1 - gradient_p0;
    float gradient_inv_length2 = 1.0f / ImLengthSqr(gradient_extent);
    ImDrawVert* vert_start = draw_list->VtxBuffer.Data + vert_start_idx;
    ImDrawVert* vert_end = draw_list->VtxBuffer.Data + vert_end_idx;

    ImVec4 col0HSV = ImGui::ColorConvertU32ToFloat4(col0);
    ImVec4 col1HSV = ImGui::ColorConvertU32ToFloat4(col1);
    ImGui::ColorConvertRGBtoHSV(col0HSV.x, col0HSV.y, col0HSV.z, col0HSV.x, col0HSV.y, col0HSV.z);
    ImGui::ColorConvertRGBtoHSV(col1HSV.x, col1HSV.y, col1HSV.z, col1HSV.x, col1HSV.y, col1HSV.z);
    ImVec4 colDelta = col1HSV - col0HSV;

    for (ImDrawVert* vert = vert_start; vert < vert_end; vert++)
    {
        float d = ImDot(vert->pos - gradient_p0, gradient_extent);
        float t = ImClamp(d * gradient_inv_length2, 0.0f, 1.0f);

        float h = col0HSV.x + colDelta.x * t;
        float s = col0HSV.y + colDelta.y * t;
        float v = col0HSV.z + colDelta.z * t;

        ImVec4 rgb;
        ImGui::ColorConvertHSVtoRGB(h, s, v, rgb.x, rgb.y, rgb.z);
        vert->col = (ImGui::ColorConvertFloat4ToU32(rgb) & ~IM_COL32_A_MASK) | (vert->col & IM_COL32_A_MASK);
    }
}

void Misc::drawOffscreenEnemies(ImDrawList* drawList) noexcept
{
    if (!miscConfig.offscreenEnemies.enabled)
        return;

    const auto yaw = Helpers::deg2rad(engineInterfaces.getEngine().getViewAngles().y);

    GameData::Lock lock;
    for (auto& player : GameData::players()) {
        if ((player.dormant && player.fadingAlpha(memory) == 0.0f) || !player.alive || !player.enemy || player.inViewFrustum)
            continue;

        const auto positionDiff = GameData::local().origin - player.origin;

        auto x = std::cos(yaw) * positionDiff.y - std::sin(yaw) * positionDiff.x;
        auto y = std::cos(yaw) * positionDiff.x + std::sin(yaw) * positionDiff.y;
        if (const auto len = std::sqrt(x * x + y * y); len != 0.0f) {
            x /= len;
            y /= len;
        }

        constexpr auto avatarRadius = 13.0f;
        constexpr auto triangleSize = 10.0f;

        const auto pos = ImGui::GetIO().DisplaySize / 2 + ImVec2{ x, y } * 200;
        const auto trianglePos = pos + ImVec2{ x, y } * (avatarRadius + (miscConfig.offscreenEnemies.healthBar.enabled ? 5 : 3));

        Helpers::setAlphaFactor(player.fadingAlpha(memory));
        const auto white = Helpers::calculateColor(255, 255, 255, 255);
        const auto background = Helpers::calculateColor(0, 0, 0, 80);
        const auto color = Helpers::calculateColor(memory.globalVars->realtime, miscConfig.offscreenEnemies.asColor4());
        const auto healthBarColor = miscConfig.offscreenEnemies.healthBar.type == HealthBar::HealthBased ? Helpers::healthColor(std::clamp(player.health / 100.0f, 0.0f, 1.0f)) : Helpers::calculateColor(memory.globalVars->realtime, miscConfig.offscreenEnemies.healthBar.asColor4());
        Helpers::setAlphaFactor(1.0f);

        const ImVec2 trianglePoints[]{
            trianglePos + ImVec2{  0.4f * y, -0.4f * x } * triangleSize,
            trianglePos + ImVec2{  1.0f * x,  1.0f * y } * triangleSize,
            trianglePos + ImVec2{ -0.4f * y,  0.4f * x } * triangleSize
        };

        drawList->AddConvexPolyFilled(trianglePoints, 3, color);
        drawList->AddCircleFilled(pos, avatarRadius + 1, white & IM_COL32_A_MASK, 40);

        const auto texture = player.getAvatarTexture();

        const bool pushTextureId = drawList->_TextureIdStack.empty() || texture != drawList->_TextureIdStack.back();
        if (pushTextureId)
            drawList->PushTextureID(texture);

        const int vertStartIdx = drawList->VtxBuffer.Size;
        drawList->AddCircleFilled(pos, avatarRadius, white, 40);
        const int vertEndIdx = drawList->VtxBuffer.Size;
        ImGui::ShadeVertsLinearUV(drawList, vertStartIdx, vertEndIdx, pos - ImVec2{ avatarRadius, avatarRadius }, pos + ImVec2{ avatarRadius, avatarRadius }, { 0, 0 }, { 1, 1 }, true);

        if (pushTextureId)
            drawList->PopTextureID();

        if (miscConfig.offscreenEnemies.healthBar.enabled) {
            const auto radius = avatarRadius + 2;
            const auto healthFraction = std::clamp(player.health / 100.0f, 0.0f, 1.0f);

            drawList->AddCircle(pos, radius, background, 40, 3.0f);

            const int vertStartIdx = drawList->VtxBuffer.Size;
            if (healthFraction == 1.0f) { // sometimes PathArcTo is missing one top pixel when drawing a full circle, so draw it with AddCircle
                drawList->AddCircle(pos, radius, healthBarColor, 40, 2.0f);
            } else {
                constexpr float pi = std::numbers::pi_v<float>;
                drawList->PathArcTo(pos, radius - 0.5f, pi / 2 - pi * healthFraction, pi / 2 + pi * healthFraction, 40);
                drawList->PathStroke(healthBarColor, false, 2.0f);
            }
            const int vertEndIdx = drawList->VtxBuffer.Size;

            if (miscConfig.offscreenEnemies.healthBar.type == HealthBar::Gradient)
                shadeVertsHSVColorGradientKeepAlpha(drawList, vertStartIdx, vertEndIdx, pos - ImVec2{ 0.0f, radius }, pos + ImVec2{ 0.0f, radius }, IM_COL32(0, 255, 0, 255), IM_COL32(255, 0, 0, 255));
        }
    }
}

void Misc::autoAccept(const char* soundEntry) noexcept
{
    if (!miscConfig.autoAccept)
        return;

    if (std::strcmp(soundEntry, "UIPanorama.popup_accept_match_beep"))
        return;

    if (const auto idx = memory.registeredPanoramaEvents->find(memory.makePanoramaSymbol("MatchAssistedAccept")); idx != -1) {
        if (const auto eventPtr = FunctionInvoker{ retSpoofGadgets->client, memory.registeredPanoramaEvents->memory[idx].value.makeEvent }(nullptr))
            csgo::UIEngine::from(retSpoofGadgets->client, interfaces.getPanoramaUIEngine().accessUIEngine()).dispatchEvent(eventPtr);
    }

#if IS_WIN32()
    auto window = FindWindowW(L"Valve001", NULL);
    FLASHWINFO flash{ sizeof(FLASHWINFO), window, FLASHW_TRAY | FLASHW_TIMERNOFG, 0, 0 };
    FlashWindowEx(&flash);
    ShowWindow(window, SW_RESTORE);
#endif
}

bool Misc::isPlayingDemoHook(ReturnAddress returnAddress, std::uintptr_t frameAddress) const
{
    return miscConfig.revealMoney && returnAddress == demoOrHLTV && *reinterpret_cast<std::uintptr_t*>(frameAddress + WIN32_LINUX(8, 24)) == money;
}

const csgo::DemoPlaybackParameters* Misc::getDemoPlaybackParametersHook(ReturnAddress returnAddress, const csgo::DemoPlaybackParameters& demoPlaybackParameters) const
{
    if (miscConfig.revealSuspect && returnAddress != demoFileEndReached) {
        static csgo::DemoPlaybackParameters customParams;
        customParams = demoPlaybackParameters;
        customParams.anonymousPlayerIdentity = false;
        return &customParams;
    }

    return &demoPlaybackParameters;
}

std::optional<std::pair<csgo::Vector, csgo::Vector>> Misc::listLeavesInBoxHook(ReturnAddress returnAddress, std::uintptr_t frameAddress) const
{
    if (!miscConfig.disableModelOcclusion || returnAddress != insertIntoTree)
        return {};

    const auto info = *reinterpret_cast<csgo::RenderableInfo**>(frameAddress + WIN32_LINUX(0x18, 0x10 + 0x948));
    if (!info || !info->renderable)
        return {};

    const auto ent = VirtualCallable{ retSpoofGadgets->client, std::uintptr_t(info->renderable) - sizeof(std::uintptr_t) }.call<csgo::EntityPOD*, WIN32_LINUX(7, 8)>();
    if (!ent || !csgo::Entity::from(retSpoofGadgets->client, ent).isPlayer())
        return {};

    constexpr float maxCoord = 16384.0f;
    constexpr float minCoord = -maxCoord;
    constexpr csgo::Vector min{ minCoord, minCoord, minCoord };
    constexpr csgo::Vector max{ maxCoord, maxCoord, maxCoord };
    return std::pair{ min, max };
}

void Misc::dispatchUserMessageHook(csgo::UserMessageType type, int size, const void* data)
{
    switch (type) {
    using enum csgo::UserMessageType;
    case VoteStart: return onVoteStart(data, size);
    case VotePass: return onVotePass();
    case VoteFailed: return onVoteFailed();
    default: break;
    }
}

void Misc::updateEventListeners(bool forceRemove) noexcept
{
    static DefaultEventListener listener;
    static bool listenerRegistered = false;

    if (miscConfig.purchaseList.enabled && !listenerRegistered) {
        engineInterfaces.getGameEventManager(memory.getEventDescriptor).addListener(&listener, csgo::item_purchase);
        listenerRegistered = true;
    } else if ((!miscConfig.purchaseList.enabled || forceRemove) && listenerRegistered) {
        engineInterfaces.getGameEventManager(memory.getEventDescriptor).removeListener(&listener);
        listenerRegistered = false;
    }
}

void Misc::updateInput() noexcept
{

}

static bool windowOpen = false;

void Misc::menuBarItem() noexcept
{
    if (ImGui::MenuItem("杂项")) {
        windowOpen = true;
        ImGui::SetWindowFocus("杂项");
        ImGui::SetWindowPos("杂项", { 100.0f, 100.0f });
    }
}

void Misc::tabItem(Visuals& visuals, inventory_changer::InventoryChanger& inventoryChanger, Glow& glow) noexcept
{
    if (ImGui::BeginTabItem("杂项")) {
        drawGUI(visuals, inventoryChanger, glow, true);
        ImGui::EndTabItem();
    }
}

void Misc::drawGUI(Visuals& visuals, inventory_changer::InventoryChanger& inventoryChanger, Glow& glow, bool contentOnly) noexcept
{
    if (!contentOnly) {
        if (!windowOpen)
            return;
        ImGui::SetNextWindowSize({ 580.0f, 0.0f });
        ImGui::Begin("杂项", &windowOpen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize
            | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    }
    ImGui::Columns(2, nullptr, false);
    ImGui::SetColumnOffset(1, 230.0f);
    ImGui::hotkey("呼出菜单", miscConfig.menuKey);
    ImGui::Checkbox("挂机防踢", &miscConfig.antiAfkKick);
    ImGui::Checkbox("自动转向", &miscConfig.autoStrafe);
    ImGui::Checkbox("连跳", &miscConfig.bunnyHop);
    ImGui::Checkbox("快速蹲起", &miscConfig.fastDuck);
    ImGui::Checkbox("滑步", &miscConfig.moonwalk);
    ImGui::Checkbox("边缘跳", &miscConfig.edgejump);
    ImGui::SameLine();
    ImGui::PushID("Edge Jump Key");
    ImGui::hotkey("", miscConfig.edgejumpkey);
    ImGui::PopID();
    ImGui::Checkbox("慢走", &miscConfig.slowwalk);
    ImGui::SameLine();
    ImGui::PushID("Slowwalk Key");
    ImGui::hotkey("", miscConfig.slowwalkKey);
    ImGui::PopID();
    ImGuiCustom::colorPicker("盲狙十字准星", miscConfig.noscopeCrosshair);
    ImGuiCustom::colorPicker("压强十字准星", miscConfig.recoilCrosshair);
    ImGui::Checkbox("手枪连发", &miscConfig.autoPistol);
    ImGui::Checkbox("自动换弹", &miscConfig.autoReload);
    ImGui::Checkbox("自动接收", &miscConfig.autoAccept);
    ImGui::Checkbox("雷达显示", &miscConfig.radarHack);
    ImGui::Checkbox("显示段位", &miscConfig.revealRanks);
    ImGui::Checkbox("显示金钱", &miscConfig.revealMoney);
    ImGui::Checkbox("显示嫌疑人", &miscConfig.revealSuspect);
    ImGui::Checkbox("公布投票", &miscConfig.revealVotes);

    ImGui::Checkbox("观战者名单", &miscConfig.spectatorList.enabled);
    ImGui::SameLine();

    ImGui::PushID("Spectator list");
    if (ImGui::Button("..."))
        ImGui::OpenPopup("");

    if (ImGui::BeginPopup("")) {
        ImGui::Checkbox("去除边框", &miscConfig.spectatorList.noTitleBar);
        ImGui::EndPopup();
    }
    ImGui::PopID();

    ImGui::Checkbox("显示水印", &miscConfig.watermark.enabled);
    ImGuiCustom::colorPicker("屏幕外的敌人", miscConfig.offscreenEnemies.asColor4(), &miscConfig.offscreenEnemies.enabled);
    ImGui::SameLine();
    ImGui::PushID("Offscreen Enemies");
    if (ImGui::Button("..."))
        ImGui::OpenPopup("");

    if (ImGui::BeginPopup("")) {
        ImGui::Checkbox("显示血量", &miscConfig.offscreenEnemies.healthBar.enabled);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(95.0f);
        ImGui::Combo("类型", &miscConfig.offscreenEnemies.healthBar.type, "渐变\0立体\0基于血量\0");
        if (miscConfig.offscreenEnemies.healthBar.type == HealthBar::Solid) {
            ImGui::SameLine();
            ImGuiCustom::colorPicker("", miscConfig.offscreenEnemies.healthBar.asColor4());
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
    ImGui::Checkbox("修复动画详细等级", &miscConfig.fixAnimationLOD);
    ImGui::Checkbox("修复骨骼矩阵", &miscConfig.fixMovement);
    ImGui::Checkbox("禁用模型模型遮挡", &miscConfig.disableModelOcclusion);
    ImGui::SliderFloat("长宽比", &miscConfig.aspectratio, 0.0f, 5.0f, "%.2f");
    ImGui::NextColumn();
    ImGui::Checkbox("隐藏HUD", &miscConfig.disablePanoramablur);
    ImGui::Checkbox("动画氏族标记", &miscConfig.animatedClanTag);
    ImGui::Checkbox("显示时间", &miscConfig.clocktag);
    ImGui::Checkbox("自定义组名", &miscConfig.customClanTag);
    ImGui::SameLine();
    ImGui::PushItemWidth(120.0f);
    ImGui::PushID(0);

    if (ImGui::InputText("", miscConfig.clanTag, sizeof(miscConfig.clanTag)))
        updateClanTag(true);
    ImGui::PopID();
    ImGui::Checkbox("击杀信息", &miscConfig.killMessage);
    ImGui::SameLine();
    ImGui::PushItemWidth(120.0f);
    ImGui::PushID(1);
    ImGui::InputText("", &miscConfig.killMessageString);
    ImGui::PopID();
    ImGui::Checkbox("窃取名称", &miscConfig.nameStealer);
    ImGui::PushID(3);
    ImGui::SetNextItemWidth(100.0f);
    ImGui::Combo("", &miscConfig.banColor, "白\0红\0粉\0绿\0亮绿\0绿宝石\0亮红\0灰\0黄\0灰2\0亮蓝\0灰/紫\0蓝\0粉\0暗橙\0橙\0");
    ImGui::PopID();
    ImGui::SameLine();
    ImGui::PushID(4);
    ImGui::InputText("", &miscConfig.banText);
    ImGui::PopID();
    ImGui::SameLine();
    if (ImGui::Button("设置假封禁"))
        fakeBan(true);
    ImGui::Checkbox("快速安放炸弹", &miscConfig.fastPlant);
    ImGui::Checkbox("快速急停", &miscConfig.fastStop);
    ImGuiCustom::colorPicker("C4倒计时", miscConfig.bombTimer);
    ImGui::Checkbox("快速换弹", &miscConfig.quickReload);
    ImGui::Checkbox("自动捏左轮", &miscConfig.prepareRevolver);
    ImGui::SameLine();
    ImGui::PushID("Prepare revolver Key");
    ImGui::hotkey("", miscConfig.prepareRevolverKey);
    ImGui::PopID();
    ImGui::Combo("击中音效", &miscConfig.hitSound, "无\0金属\0游戏感\0钟\0玻璃\0自定义\0");
    if (miscConfig.hitSound == 5) {
        ImGui::InputText("击中音效文件名", &miscConfig.customHitSound);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("音频文件必须放在csgo/sound/目录中");
    }
    ImGui::PushID(5);
    ImGui::Combo("击杀音效", &miscConfig.killSound, "无\0金属\0游戏感\0钟\0玻璃\0自定义\0");
    if (miscConfig.killSound == 5) {
        ImGui::InputText("击杀音效文件名", &miscConfig.customKillSound);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("音频文件必须放在csgo/sound/目录中");
    }
    ImGui::PopID();
    ImGui::Checkbox("投掷物抛物线", &miscConfig.nadePredict);
    ImGui::Checkbox("特训助手预测", &miscConfig.fixTabletSignal);
    ImGui::SetNextItemWidth(120.0f);
    ImGui::SliderFloat("最大视角变化", &miscConfig.maxAngleDelta, 0.0f, 255.0f, "%.2f");
    ImGui::Checkbox("刀枪反手", &miscConfig.oppositeHandKnife);
    ImGui::Checkbox("保留击杀", &miscConfig.preserveKillfeed.enabled);
    ImGui::SameLine();

    ImGui::PushID("Preserve Killfeed");
    if (ImGui::Button("..."))
        ImGui::OpenPopup("");

    if (ImGui::BeginPopup("")) {
        ImGui::Checkbox("仅保留爆头", &miscConfig.preserveKillfeed.onlyHeadshots);
        ImGui::EndPopup();
    }
    ImGui::PopID();

    ImGui::Checkbox("购买记录", &miscConfig.purchaseList.enabled);
    ImGui::SameLine();

    ImGui::PushID("Purchase List");
    if (ImGui::Button("..."))
        ImGui::OpenPopup("");

    if (ImGui::BeginPopup("")) {
        ImGui::SetNextItemWidth(75.0f);
        ImGui::Combo("模式", &miscConfig.purchaseList.mode, "详细\0总共\0");
        ImGui::Checkbox("仅在冻结期间", &miscConfig.purchaseList.onlyDuringFreezeTime);
        ImGui::Checkbox("显示价格", &miscConfig.purchaseList.showPrices);
        ImGui::Checkbox("隐藏标题栏", &miscConfig.purchaseList.noTitleBar);
        ImGui::EndPopup();
    }
    ImGui::PopID();

    ImGui::Checkbox("自动举报", &miscConfig.reportbot.enabled);
    ImGui::SameLine();
    ImGui::PushID("Reportbot");

    if (ImGui::Button("..."))
        ImGui::OpenPopup("");

    if (ImGui::BeginPopup("")) {
        ImGui::PushItemWidth(80.0f);
        ImGui::Combo("目标", &miscConfig.reportbot.target, "敌人\0队友\0全部\0");
        ImGui::InputInt("延迟 (秒)", &miscConfig.reportbot.delay);
        miscConfig.reportbot.delay = (std::max)(miscConfig.reportbot.delay, 1);
        ImGui::InputInt("回合", &miscConfig.reportbot.rounds);
        miscConfig.reportbot.rounds = (std::max)(miscConfig.reportbot.rounds, 1);
        ImGui::PopItemWidth();
        ImGui::Checkbox("语言辱骂", &miscConfig.reportbot.textAbuse);
        ImGui::Checkbox("恶意骚扰", &miscConfig.reportbot.griefing);
        ImGui::Checkbox("透视作弊", &miscConfig.reportbot.wallhack);
        ImGui::Checkbox("自瞄作弊", &miscConfig.reportbot.aimbot);
        ImGui::Checkbox("其他作弊", &miscConfig.reportbot.other);
        if (ImGui::Button("刷新"))
            Misc::resetReportbot();
        ImGui::EndPopup();
    }
    ImGui::PopID();

    if (ImGui::Button("卸载辅助"))
        hooks->uninstall(*this, glow, memory, visuals, inventoryChanger);

    ImGui::Columns(1);
    if (!contentOnly)
        ImGui::End();
}

static void from_json(const json& j, ImVec2& v)
{
    read(j, "X", v.x);
    read(j, "Y", v.y);
}

static void from_json(const json& j, PurchaseList& pl)
{
    read(j, "Enabled", pl.enabled);
    read(j, "Only During Freeze Time", pl.onlyDuringFreezeTime);
    read(j, "Show Prices", pl.showPrices);
    read(j, "No Title Bar", pl.noTitleBar);
    read(j, "Mode", pl.mode);
}

static void from_json(const json& j, OffscreenEnemies& o)
{
    from_json(j, static_cast<ColorToggle&>(o));

    read<value_t::object>(j, "Health Bar", o.healthBar);
}

static void from_json(const json& j, MiscConfig::SpectatorList& sl)
{
    read(j, "Enabled", sl.enabled);
    read(j, "No Title Bar", sl.noTitleBar);
    read<value_t::object>(j, "Pos", sl.pos);
    read<value_t::object>(j, "Size", sl.size);
}

static void from_json(const json& j, MiscConfig::Watermark& o)
{
    read(j, "Enabled", o.enabled);
}

static void from_json(const json& j, PreserveKillfeed& o)
{
    read(j, "Enabled", o.enabled);
    read(j, "Only Headshots", o.onlyHeadshots);
}

static void from_json(const json& j, MiscConfig& m)
{
    read(j, "Menu key", m.menuKey);
    read(j, "Anti AFK kick", m.antiAfkKick);
    read(j, "Auto strafe", m.autoStrafe);
    read(j, "Bunny hop", m.bunnyHop);
    read(j, "Custom clan tag", m.customClanTag);
    read(j, "Clock tag", m.clocktag);
    read(j, "Clan tag", m.clanTag, sizeof(m.clanTag));
    read(j, "Animated clan tag", m.animatedClanTag);
    read(j, "Fast duck", m.fastDuck);
    read(j, "Moonwalk", m.moonwalk);
    read(j, "Edge Jump", m.edgejump);
    read(j, "Edge Jump Key", m.edgejumpkey);
    read(j, "Slowwalk", m.slowwalk);
    read(j, "Slowwalk key", m.slowwalkKey);
    read<value_t::object>(j, "Noscope crosshair", m.noscopeCrosshair);
    read<value_t::object>(j, "Recoil crosshair", m.recoilCrosshair);
    read(j, "Auto pistol", m.autoPistol);
    read(j, "Auto reload", m.autoReload);
    read(j, "Auto accept", m.autoAccept);
    read(j, "Radar hack", m.radarHack);
    read(j, "Reveal ranks", m.revealRanks);
    read(j, "Reveal money", m.revealMoney);
    read(j, "Reveal suspect", m.revealSuspect);
    read(j, "Reveal votes", m.revealVotes);
    read<value_t::object>(j, "Spectator list", m.spectatorList);
    read<value_t::object>(j, "Watermark", m.watermark);
    read<value_t::object>(j, "Offscreen Enemies", m.offscreenEnemies);
    read(j, "Fix animation LOD", m.fixAnimationLOD);
    read(j, "Fix movement", m.fixMovement);
    read(j, "Disable model occlusion", m.disableModelOcclusion);
    read(j, "Aspect Ratio", m.aspectratio);
    read(j, "Kill message", m.killMessage);
    read<value_t::string>(j, "Kill message string", m.killMessageString);
    read(j, "Name stealer", m.nameStealer);
    read(j, "Disable HUD blur", m.disablePanoramablur);
    read(j, "Ban color", m.banColor);
    read<value_t::string>(j, "Ban text", m.banText);
    read(j, "Fast plant", m.fastPlant);
    read(j, "Fast Stop", m.fastStop);
    read<value_t::object>(j, "Bomb timer", m.bombTimer);
    read(j, "Quick reload", m.quickReload);
    read(j, "Prepare revolver", m.prepareRevolver);
    read(j, "Prepare revolver key", m.prepareRevolverKey);
    read(j, "Hit sound", m.hitSound);
    read(j, "Quick healthshot key", m.quickHealthshotKey);
    read(j, "Grenade predict", m.nadePredict);
    read(j, "Fix tablet signal", m.fixTabletSignal);
    read(j, "Max angle delta", m.maxAngleDelta);
    read(j, "Fix tablet signal", m.fixTabletSignal);
    read<value_t::string>(j, "Custom Hit Sound", m.customHitSound);
    read(j, "Kill sound", m.killSound);
    read<value_t::string>(j, "Custom Kill Sound", m.customKillSound);
    read<value_t::object>(j, "Purchase List", m.purchaseList);
    read<value_t::object>(j, "Reportbot", m.reportbot);
    read(j, "Opposite Hand Knife", m.oppositeHandKnife);
    read<value_t::object>(j, "Preserve Killfeed", m.preserveKillfeed);
}

static void from_json(const json& j, MiscConfig::Reportbot& r)
{
    read(j, "Enabled", r.enabled);
    read(j, "Target", r.target);
    read(j, "Delay", r.delay);
    read(j, "Rounds", r.rounds);
    read(j, "Abusive Communications", r.textAbuse);
    read(j, "Griefing", r.griefing);
    read(j, "Wall Hacking", r.wallhack);
    read(j, "Aim Hacking", r.aimbot);
    read(j, "Other Hacking", r.other);
}

static void to_json(json& j, const MiscConfig::Reportbot& o, const MiscConfig::Reportbot& dummy = {})
{
    WRITE("Enabled", enabled);
    WRITE("Target", target);
    WRITE("Delay", delay);
    WRITE("Rounds", rounds);
    WRITE("Abusive Communications", textAbuse);
    WRITE("Griefing", griefing);
    WRITE("Wall Hacking", wallhack);
    WRITE("Aim Hacking", aimbot);
    WRITE("Other Hacking", other);
}

static void to_json(json& j, const PurchaseList& o, const PurchaseList& dummy = {})
{
    WRITE("Enabled", enabled);
    WRITE("Only During Freeze Time", onlyDuringFreezeTime);
    WRITE("Show Prices", showPrices);
    WRITE("No Title Bar", noTitleBar);
    WRITE("Mode", mode);
}

static void to_json(json& j, const ImVec2& o, const ImVec2& dummy = {})
{
    WRITE("X", x);
    WRITE("Y", y);
}

static void to_json(json& j, const OffscreenEnemies& o, const OffscreenEnemies& dummy = {})
{
    to_json(j, static_cast<const ColorToggle&>(o), dummy);

    WRITE("Health Bar", healthBar);
}

static void to_json(json& j, const MiscConfig::SpectatorList& o, const MiscConfig::SpectatorList& dummy = {})
{
    WRITE("Enabled", enabled);
    WRITE("No Title Bar", noTitleBar);

    if (const auto window = ImGui::FindWindowByName("Spectator list")) {
        j["Pos"] = window->Pos;
        j["Size"] = window->SizeFull;
    }
}

static void to_json(json& j, const MiscConfig::Watermark& o, const MiscConfig::Watermark& dummy = {})
{
    WRITE("Enabled", enabled);
}

static void to_json(json& j, const PreserveKillfeed& o, const PreserveKillfeed& dummy = {})
{
    WRITE("Enabled", enabled);
    WRITE("Only Headshots", onlyHeadshots);
}

static void to_json(json& j, const MiscConfig& o)
{
    const MiscConfig dummy;

    WRITE("Menu key", menuKey);
    WRITE("Anti AFK kick", antiAfkKick);
    WRITE("Auto strafe", autoStrafe);
    WRITE("Bunny hop", bunnyHop);
    WRITE("Custom clan tag", customClanTag);
    WRITE("Clock tag", clocktag);

    if (o.clanTag[0])
        j["Clan tag"] = o.clanTag;

    WRITE("Animated clan tag", animatedClanTag);
    WRITE("Fast duck", fastDuck);
    WRITE("Moonwalk", moonwalk);
    WRITE("Edge Jump", edgejump);
    WRITE("Edge Jump Key", edgejumpkey);
    WRITE("Slowwalk", slowwalk);
    WRITE("Slowwalk key", slowwalkKey);
    WRITE("Noscope crosshair", noscopeCrosshair);
    WRITE("Recoil crosshair", recoilCrosshair);
    WRITE("Auto pistol", autoPistol);
    WRITE("Auto reload", autoReload);
    WRITE("Auto accept", autoAccept);
    WRITE("Radar hack", radarHack);
    WRITE("Reveal ranks", revealRanks);
    WRITE("Reveal money", revealMoney);
    WRITE("Reveal suspect", revealSuspect);
    WRITE("Reveal votes", revealVotes);
    WRITE("Spectator list", spectatorList);
    WRITE("Watermark", watermark);
    WRITE("Offscreen Enemies", offscreenEnemies);
    WRITE("Fix animation LOD", fixAnimationLOD);
    WRITE("Fix movement", fixMovement);
    WRITE("Disable model occlusion", disableModelOcclusion);
    WRITE("Aspect Ratio", aspectratio);
    WRITE("Kill message", killMessage);
    WRITE("Kill message string", killMessageString);
    WRITE("Name stealer", nameStealer);
    WRITE("Disable HUD blur", disablePanoramablur);
    WRITE("Ban color", banColor);
    WRITE("Ban text", banText);
    WRITE("Fast plant", fastPlant);
    WRITE("Fast Stop", fastStop);
    WRITE("Bomb timer", bombTimer);
    WRITE("Quick reload", quickReload);
    WRITE("Prepare revolver", prepareRevolver);
    WRITE("Prepare revolver key", prepareRevolverKey);
    WRITE("Hit sound", hitSound);
    WRITE("Quick healthshot key", quickHealthshotKey);
    WRITE("Grenade predict", nadePredict);
    WRITE("Fix tablet signal", fixTabletSignal);
    WRITE("Max angle delta", maxAngleDelta);
    WRITE("Fix tablet signal", fixTabletSignal);
    WRITE("Custom Hit Sound", customHitSound);
    WRITE("Kill sound", killSound);
    WRITE("Custom Kill Sound", customKillSound);
    WRITE("Purchase List", purchaseList);
    WRITE("Reportbot", reportbot);
    WRITE("Opposite Hand Knife", oppositeHandKnife);
    WRITE("Preserve Killfeed", preserveKillfeed);
}

json Misc::toJson() noexcept
{
    json j;
    to_json(j, miscConfig);
    return j;
}

void Misc::fromJson(const json& j) noexcept
{
    from_json(j, miscConfig);
}

void Misc::resetConfig() noexcept
{
    miscConfig = {};
}
