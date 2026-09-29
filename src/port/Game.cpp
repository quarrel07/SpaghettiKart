#include <libultraship.h>
#include <typeinfo>

#include "Game.h"
#include "port/Engine.h"

#include <fast/Fast3dWindow.h>
#include <memory>
#include <atomic>
#include "engine/World.h"
#include "engine/AllTracks.h"

#include "engine/tracks/PodiumCeremony.h"

#include "engine/GarbageCollector.h"

#include "engine/TrainCrossing.h"
#include "engine/objects/BombKart.h"
#include "engine/objects/Lakitu.h"

#include "engine/Smoke.h"

#include "engine/HM_Intro.h"

#include "engine/editor/Editor.h"
#include "engine/editor/SceneManager.h"
#include "engine/registry/RegisterContent.h"

#include "engine/cameras/GameCamera.h"
#include "engine/cameras/FreeCamera.h"
#include "engine/cameras/TourCamera.h"
#include "engine/cameras/LookBehindCamera.h"

#include "engine/TrackBrowser.h"
#include "engine/RandomItemTable.h"
#include "engine/sky/Sky.h"

#ifdef _WIN32
#include <locale.h>
#endif

extern "C" {
#include "main.h"
#include "audio/load.h"
#include "audio/external.h"
#include "racing/render_courses.h"
#include "menus.h"
#include "update_objects.h"
#include "spawn_players.h"
#include "src/enhancements/collision_viewer.h"
#include "code_800029B0.h"
#include "code_80057C60.h"
// #include "engine/wasm.h"
}

extern "C" void Graphics_PushFrame(Gfx* pool) {
    GameEngine::ProcessGfxCommands(pool);
}

// Create the world instance
static World sWorldInstance;

// Deferred cleaning when clearing all actors in the editor
bool bCleanWorld = false;

std::unique_ptr<Cup> gMushroomCup;
std::unique_ptr<Cup> gFlowerCup;
std::unique_ptr<Cup> gStarCup;
std::unique_ptr<Cup> gSpecialCup;
std::unique_ptr<Cup> gBattleCup;

HarbourMastersIntro gMenuIntro;

TrackEditor::Editor gEditor;

s32 gTrophyIndex = NULL_OBJECT_ID;

/** Spawner Registries **/
Registry<TrackInfo> gTrackRegistry;
Registry<ActorInfo, const SpawnParams&> gActorRegistry;
Registry<ItemInfo> gItemRegistry;

/** Data Registries **/
DataRegistry<RandomItemTable> gItemTableRegistry;

std::unique_ptr<TrackBrowser> gTrackBrowser;
std::unique_ptr<Sky> gSky;

World* GetWorld() {
    return World::Instance;
}

void CustomEngineInit() {
    // Close the editor because lus remembers if it was open
    // This also turns off freecam
    gEditor.Disable();

    gSky = std::make_unique<Sky>();
    RegisterTracks(gTrackRegistry);
    gTrackBrowser = std::make_unique<TrackBrowser>(gTrackRegistry);
    TrackBrowser::Instance->FindCustomTracks();
    TrackBrowser::Instance->Refresh(gTrackRegistry);

    gMushroomCup = std::make_unique<Cup>("mk:mushroom_cup", "Mushroom Cup", std::vector<std::string>{
        "mk:luigi_raceway", 
        "mk:moo_moo_farm", 
        "mk:koopa_troopa_beach", 
        "mk:kalimari_desert"
    });

    gFlowerCup = std::make_unique<Cup>("mk:flower_cup", "Flower Cup", std::vector<std::string>{
        "mk:toads_turnpike", 
        "mk:frappe_snowland", 
        "mk:choco_mountain", 
        "mk:mario_raceway"
    });

    gStarCup = std::make_unique<Cup>("mk:star_cup", "Star Cup", std::vector<std::string>{
        "mk:wario_stadium", 
        "mk:sherbet_land", 
        "mk:royal_raceway", 
        "mk:bowsers_castle"
    });

    gSpecialCup = std::make_unique<Cup>("mk:special_cup", "Special Cup", std::vector<std::string>{
        "mk:dk_jungle", 
        "mk:yoshi_valley", 
        "mk:banshee_boardwalk", 
        "mk:rainbow_road"
    });

    gBattleCup = std::make_unique<Cup>("mk:battle_cup", "Battle Cup", std::vector<std::string>{
        "mk:big_donut", 
        "mk:block_fort", 
        "mk:double_deck", 
        "mk:skyscraper"
    });

    /* Validate Cup Track IDs */
    gMushroomCup->ValidateTrackIds(gTrackRegistry);
    gFlowerCup->ValidateTrackIds(gTrackRegistry);
    gStarCup->ValidateTrackIds(gTrackRegistry);
    gSpecialCup->ValidateTrackIds(gTrackRegistry);
    gBattleCup->ValidateTrackIds(gTrackRegistry);

    /* Instantiate Cups */
    GetWorld()->AddCup(gMushroomCup.get());
    GetWorld()->AddCup(gFlowerCup.get());
    GetWorld()->AddCup(gStarCup.get());
    GetWorld()->AddCup(gSpecialCup.get());
    GetWorld()->AddCup(gBattleCup.get());

    SetMarioRaceway();

    printf("[Game] Registering Game Content...\n");
    RegisterActors(gActorRegistry);
    RegisterItems(gItemRegistry);
    RegisterItemTables(gItemTableRegistry);
    printf("[Game] Game Content Registered!\n");
}

void CustomEngineDestroy() {
    gTrackRegistry.Clear();
    gActorRegistry.Clear();
    gMushroomCup.reset();
    gFlowerCup.reset();
    gStarCup.reset();
    gSpecialCup.reset();
    gBattleCup.reset();
}

extern "C" {

void HM_InitIntro() {
    gMenuIntro.HM_InitIntro();
}

void HM_TickIntro() {
    gMenuIntro.HM_TickIntro();
}

void HM_DrawIntro() {
    gMenuIntro.HM_DrawIntro();
}

// Set default track; mario raceway
void SetMarioRaceway(void) {
    SelectMarioRaceway();
    GetWorld()->SetCurrentCup(gMushroomCup.get());
    GetWorld()->GetCurrentCup()->CursorPosition = 3;
    GetWorld()->CupIndex = 0;
}

u32 WorldNextCup(void) {
    return GetWorld()->NextCup();
}

u32 WorldPreviousCup(void) {
    return GetWorld()->PreviousCup();
}

void CM_SetCup(void* cup) {
    GetWorld()->SetCurrentCup((Cup*) cup);
}

void* GetCup() {
    return GetWorld()->GetCurrentCup();
}

u32 GetCupIndex(void) {
    return GetWorld()->GetCupIndex();
}

void CM_SetCupIndex(size_t index) {
    GetWorld()->SetCupIndex(index);
}

const char* GetCupName(void) {
    return GetWorld()->GetCurrentCup()->Name;
}

void LoadTrack() {
    GetWorld()->GetRaceManager().Load();
}

void CM_VehicleCollision(s32 playerId, Player* player) {
    for (auto& actor : GetWorld()->Actors) {
        if (actor) {
            actor->VehicleCollision(playerId, player);
        }
    }
}

void CM_BombKartsWaypoint(s32 cameraId) {
    for (auto& object : GetWorld()->Objects) {
        if (auto* kart = dynamic_cast<OBombKart*>(object.get())) {
            if (kart != nullptr) {
                kart->Waypoint(cameraId);
            }
        }
    }
}

void CM_DisplayBattleBombKart(s32 playerId, s32 primAlpha) {

    if ((playerId < 0) || (playerId > 4)) {
        return;
    }

    if (primAlpha == 0) {
        GetWorld()->mPlayerBombKart[playerId].state = PlayerBombKart::PlayerBombKartState::DISABLED;
        GetWorld()->mPlayerBombKart[playerId]._primAlpha = primAlpha;
    } else {
        GetWorld()->mPlayerBombKart[playerId].state = PlayerBombKart::PlayerBombKartState::ACTIVE;
        GetWorld()->mPlayerBombKart[playerId]._primAlpha = primAlpha;
    }
}

void CM_DrawBattleBombKarts(s32 cameraId) {
    for (size_t i = 0; i < gPlayerCount; i++) {
        GetWorld()->mPlayerBombKart[i].Draw(i, cameraId);
    }
}

void CM_ClearVehicles(void) {
    GetWorld()->Crossings.clear();
}

void CM_CrossingTrigger() {
    for (auto& crossing : GetWorld()->Crossings) {
        if (crossing) {
            crossing->CrossingTrigger();
        }
    }
}

void CM_AICrossingBehaviour(s32 playerId) {
    for (auto& crossing : GetWorld()->Crossings) {
        if (crossing) {
            crossing->AICrossingBehaviour(playerId);
        }
    }
}

s32 CM_GetCrossingOnTriggered(uintptr_t* crossing) {
    TrainCrossing* ptr = (TrainCrossing*) crossing;
    if (ptr) {
        return ptr->OnTriggered;
    }
    return 0;
}

/**
 * Tracks are rendered in two ways
 * 1) Track sections --> The scene is split into multiple sections and rendered piece by piece
 * 2) Full scene --> The entire scene is rendered at once
 * 
 * Custom tracks only use the Render() method, and they only render the full scene.
 * They do not use DrawCredits() and they do not use track sections.
 */
void CM_DrawTrack(ScreenContext* screen) {
    if (nullptr == GetWorld()->GetTrack()) {
        return;
    }

    // Check if collision mesh rendering is enabled via CVar
    if (CVarGetInteger("gRenderCollisionMesh", 0)) {
        render_collision();
        return;
    }

    // Custom tracks should never use DrawCredits();
    if (GetWorld()->GetTrack()->IsMod()) {
        switch(screen->camera->renderMode) {
            default:
                GetWorld()->GetTrack()->Draw(screen);
                break;
            case RENDER_COLLISION_MESH:
                render_collision();
                break;
        } 
    } else {
        switch(screen->camera->renderMode) {
            case RENDER_FULL_SCENE:
                if (gModeSelection == BATTLE) {
                    GetWorld()->GetTrack()->Draw(screen);
                } else {
                    GetWorld()->GetTrack()->DrawCredits();
                }
            case RENDER_TRACK_SECTIONS:
                GetWorld()->GetTrack()->Draw(screen);
                break;
            case RENDER_COLLISION_MESH:
                render_collision();
                break;
        }
    }
}

void CM_TickActors() {
    if (GetWorld()->GetTrack()) {
        GetWorld()->TickActors();
    }
}

void CM_TickActors60fps() {
    if (GetWorld()->GetTrack()) {
        GetWorld()->TickActors60fps();
    }
}

void CM_DrawActors(Camera* camera) {
    //AActor* a = GetWorld()->ConvertActorToAActor(actor);
    for (const auto& actor : GetWorld()->Actors) {
        if (actor->IsMod()) {
            actor->Draw(camera);
        }
    }

    for (auto& camera : GetWorld()->Cameras) {
        if (auto* tourCam = dynamic_cast<TourCamera*>(camera.get())) {
            if (tourCam->IsActive()) {
                tourCam->Draw();
            }
        }
    }
}

void CM_DrawStaticMeshActors() {
    GetWorld()->DrawStaticMeshActors();
}

void CM_BeginPlay() {
    static bool tour = false;
    auto track = GetWorld()->GetTrack();
    GetWorld()->CleanActors();
    
    if (nullptr == track) {
        return; 
    }

    if (tour) {
      //  GetWorld()->Cameras[2]->SetActive(true);
       // gScreenOneCtx->camera = GetWorld()->Cameras[2]->Get();
        if (reinterpret_cast<TourCamera*>(GetWorld()->Cameras[2].get())->IsTourComplete()) {
            tour = false;
            gScreenOneCtx->pendingCamera = &cameras[0];
        }
    }

    GetWorld()->GetRaceManager().PreInit();
    GetWorld()->GetRaceManager().BeginPlay();
    GetWorld()->GetRaceManager().PostInit();
}

Camera* CM_GetPlayerCamera(s32 playerIndex) {
    for (auto& cam : GetWorld()->Cameras) {
        // Make sure this is a player camera and not a different type of camera
        if (typeid(*cam) == typeid(GameCamera)) {
            Camera* camera = cam->Get();
            if (camera->playerId == playerIndex) {
                return camera;
            }
        }
    }
    return nullptr;
}

void CM_SetViewProjection(Camera* camera) {
    for (auto& gameCamera : GetWorld()->Cameras) {
        if (camera == gameCamera->Get()) {
            gameCamera->SetViewProjection();
        }
    }
}

void CM_TickCameras() {
    GetWorld()->TickCameras();
}

Camera* CM_AddCamera(Vec3f spawn, s16 rot, u32 mode) {
    if (GetWorld()->Cameras.size() >= NUM_CAMERAS) {
        printf("Reached the max number of cameras, %d\n", NUM_CAMERAS);
        return nullptr;
    }
    GetWorld()->Cameras.push_back(std::make_unique<GameCamera>(FVector(spawn[0], spawn[1], spawn[2]), rot, mode));
    return GetWorld()->Cameras.back()->Get();
}

Camera* CM_AddFreeCamera(Vec3f spawn, s16 rot, u32 mode) {
    if (GetWorld()->Cameras.size() >= NUM_CAMERAS) {
        printf("Reached the max number of cameras, %d\n", NUM_CAMERAS);
        return nullptr;
    }
    GetWorld()->Cameras.push_back(std::make_unique<FreeCamera>(FVector(spawn[0], spawn[1], spawn[2]), rot, mode));
    return GetWorld()->Cameras.back()->Get();
}

Camera* CM_AddTourCamera(Vec3f spawn, s16 rot, u32 mode) {
    if (GetWorld()->Cameras.size() >= NUM_CAMERAS) {
        // This is to prevent soft locking the game
        printf("Reached the max number of cameras, %d\n", NUM_CAMERAS);
        if (GetWorld()->GetTrack()->bTourEnabled) {
            spawn_and_set_player_spawns();
        }
        return nullptr;
    }

    if (nullptr == GetWorld()->GetTrack()) {
        // This is to prevent soft locking the game
        if (GetWorld()->GetTrack()->bTourEnabled) {
            spawn_and_set_player_spawns();
        }
        return nullptr;
    }

    if (GetWorld()->GetTrack()->TourShots.size() == 0) {
        // This is to prevent soft locking the game
        if (GetWorld()->GetTrack()->bTourEnabled) {
            spawn_and_set_player_spawns();
        }
        return nullptr;
    }

    GetWorld()->Cameras.push_back(std::make_unique<TourCamera>(FVector(spawn[0], spawn[1], spawn[2]), rot, mode));
    TourCamera* tour = static_cast<TourCamera*>(GetWorld()->Cameras.back().get());
    tour->SetActive(true);
    return tour->Get();
}

bool CM_IsTourEnabled() {
    if (nullptr != GetWorld()->GetTrack()) {
        if ((GetWorld()->GetTrack()->bTourEnabled) && (gTourComplete == false)) {
            return true;
        } else {
            return false;
        }
    } else {
        return false;
    }
}

Camera* CM_AddLookBehindCamera(Vec3f spawn, s16 rot, u32 mode) {
    if (GetWorld()->Cameras.size() >= NUM_CAMERAS) {
        printf("Reached the max number of cameras, %d\n", NUM_CAMERAS);
        return nullptr;
    }
    GetWorld()->Cameras.push_back(std::make_unique<LookBehindCamera>(FVector(spawn[0], spawn[1], spawn[2]), rot, mode));
    return GetWorld()->Cameras.back()->Get();
}

void CM_AttachCamera(Camera* camera, s32 playerIdx) {
    camera->playerId = playerIdx;
}

void CM_CameraSetActive(size_t idx, bool state) {
    if (idx < GetWorld()->Cameras.size()) {
        GetWorld()->Cameras[idx]->SetActive(state);
    }
}

void CM_SetFreeCamera(bool state) {
    for (auto& cam : GetWorld()->Cameras) {
        if (cam->Get() == gScreenOneCtx->freeCamera) {
            if (state) {
                gScreenOneCtx->pendingCamera = gScreenOneCtx->freeCamera;
                cam->SetActive(true);
            } else {
                if (nullptr != gScreenOneCtx->raceCamera) {
                    if (gGamestate == RACING) {
                        gScreenOneCtx->pendingCamera = gScreenOneCtx->raceCamera;
                        cam->SetActive(false);
                    } else {
                        cam->SetActive(false);
                    }
                }
            }
        }
    }
}

void CM_ActivateTourCamera(Camera* camera) {
    for (auto& cam : GetWorld()->Cameras) {
        if (cam->Get() == camera) {
            cam->SetActive(true);
        }
    }
}

void CM_TickObjects() {
    if (GetWorld()->GetTrack()) {
        GetWorld()->TickObjects();
    }
}

// A couple objects such as lakitu are ticked inside of process_game_tick which support 60fps.
// This is a fallback to support that.
void CM_TickObjects60fps() {
    if (GetWorld()->GetTrack()) {
        GetWorld()->TickObjects60fps();
    }
}

void CM_DrawObjects(s32 cameraId) {
    if (GetWorld()->GetTrack()) {
        GetWorld()->DrawObjects(cameraId);
    }
}

void CM_TickEditor() {
    gEditor.Tick();
}

void CM_DrawEditor() {
    gEditor.Draw();
}

void CM_TickParticles() {
    if (GetWorld()->GetTrack()) {
        GetWorld()->TickParticles();
    }
}

void CM_DrawParticles(s32 cameraId) {
    if (GetWorld()->GetTrack()) {
        GetWorld()->DrawParticles(cameraId);
    }
}

void CM_RaceDrawSky(ScreenContext* screen, s32 someId) {
    // if (bDrawSkybox) {
    if (CVarGetInteger("gDrawSky", true) == true) {
        Sky::Instance->Draw(screen);
        if (gGamestate != CREDITS_SEQUENCE) {
            func_80057FC4(screen, someId); // DrawSkyActors
        }
        Sky::Instance->DrawFloor(screen);
    }
}

void CM_Waypoints(Player* player, int8_t playerId) {
    if (GetWorld()->GetTrack()) {
        GetWorld()->GetTrack()->Waypoints(player, playerId);
    }
}

void CM_SomeCollisionThing(Player* player, Vec3f arg1, Vec3f arg2, Vec3f arg3, f32* arg4, f32* arg5, f32* arg6,
                           f32* arg7) {
    if (GetWorld()->GetTrack()) {
        GetWorld()->GetTrack()->SomeCollisionThing(player, arg1, arg2, arg3, arg4, arg5, arg6, arg7);
    }
}

void CM_InitTrackObjects() {
    if (GetWorld()->GetTrack()) {
        GetWorld()->GetTrack()->InitTrackObjects();
    }
}

void CM_TickTrackObjects() {
    if (GetWorld()->GetTrack()) {
        GetWorld()->GetTrack()->TickTrackObjects();
    }
    TrainSmokeTick();
}

void CM_DrawTrackObjects(s32 cameraId) {
    if (GetWorld()->GetTrack()) {
        GetWorld()->GetTrack()->DrawTrackObjects(cameraId);
    }

    TrainSmokeDraw(cameraId);
}

void CM_SomeSounds() {
    if (GetWorld()->GetTrack()) {
        GetWorld()->GetTrack()->SomeSounds();
    }
}

void CM_CreditsSpawnActors() {
    if (GetWorld()->GetTrack()) {
        GetWorld()->GetTrack()->CreditsSpawnActors();
    }
}

void CM_WhatDoesThisDo(Player* player, int8_t playerId) {
    if (GetWorld()->GetTrack()) {
        GetWorld()->GetTrack()->WhatDoesThisDo(player, playerId);
    }
}

void CM_WhatDoesThisDoAI(Player* player, int8_t playerId) {
    if (GetWorld()->GetTrack()) {
        GetWorld()->GetTrack()->WhatDoesThisDoAI(player, playerId);
    }
}

void CM_SetStaffGhost() {
    if (GetWorld()->GetTrack()) {
        GetWorld()->GetTrack()->SetStaffGhost();
    }
}

// This should only be used for checking if the track has changed
uintptr_t CM_GetTrack() {
    return (uintptr_t) (void*) GetWorld()->GetTrack();
}

Properties* CM_GetProps() {
    if (GetWorld()->GetTrack()) {
        return &GetWorld()->GetTrack()->Props;
    }
    return NULL;
}

void CM_TickTrack() {
    if (GetWorld()->GetTrack()) {
        GetWorld()->GetTrack()->Tick();
    }
}

void CM_DrawTransparency(ScreenContext* screen, uint16_t pathCounter, uint16_t cameraRot,
                  uint16_t playerDirection) {
    if (GetWorld()->GetTrack()) {
        GetWorld()->GetTrack()->DrawTransparency(screen, pathCounter, cameraRot, playerDirection);
    }
}

/**
 * This should only be ran once per track, otherwise animation/timings might become sped up.
 */
void CM_SpawnStarterLakitu() {
    if ((gDemoMode) || (gGamestate == CREDITS_SEQUENCE)) {
        return;
    }

    for (size_t i = 0; i < gPlayerCountSelection1; i++) {
        // Retry does not respawn actors, therefore, re-use lakitu.
        if (auto it = GetWorld()->Lakitus.find(i); it != GetWorld()->Lakitus.end()) {
            if (it->second) {
                it->second->Activate(OLakitu::STARTER);
            }
            continue; // Already exists, skip spawning
        }

        auto lakitu = std::make_unique<OLakitu>(i, OLakitu::LakituType::STARTER);
        GetWorld()->Lakitus[i] = lakitu.get();
        GetWorld()->AddObject(std::move(lakitu));
    }
}

// Checkered flag lakitu
void CM_ActivateFinishLakitu(s32 playerId) {
    if ((gDemoMode) || (gGamestate == CREDITS_SEQUENCE)) {
        return;
    }
    GetWorld()->Lakitus[playerId]->Activate(OLakitu::LakituType::FINISH);
}

void CM_ActivateSecondLapLakitu(s32 playerId) {
    if ((gDemoMode) || (gGamestate == CREDITS_SEQUENCE)) {
        return;
    }
    GetWorld()->Lakitus[playerId]->Activate(OLakitu::LakituType::SECOND_LAP);
}

void CM_ActivateFinalLapLakitu(s32 playerId) {
    if ((gDemoMode) || (gGamestate == CREDITS_SEQUENCE)) {
        return;
    }
    GetWorld()->Lakitus[playerId]->Activate(OLakitu::LakituType::FINAL_LAP);
}

void CM_ActivateReverseLakitu(s32 playerId) {
    if ((gDemoMode) || (gGamestate == CREDITS_SEQUENCE)) {
        return;
    }
    GetWorld()->Lakitus[playerId]->Activate(OLakitu::LakituType::REVERSE);
}

size_t GetCupCursorPosition() {
    return GetWorld()->GetCurrentCup()->CursorPosition;
}

void SetCupCursorPosition(size_t position) {
    GetWorld()->GetCurrentCup()->SetTrack(position);
    // GetWorld()->CurrentCup->CursorPosition = position;
}

size_t GetCupSize() {
    return GetWorld()->GetCurrentCup()->GetSize();
}

void* GetTrack(void) {
    return GetWorld()->GetTrack();
}

struct Actor* CM_GetActor(size_t index) {
    if (index >= 0 && index < GetWorld()->Actors.size()) {
        AActor* actor = GetWorld()->Actors[index].get();
        return reinterpret_cast<struct Actor*>(reinterpret_cast<char*>(actor) + sizeof(void*));
    } else {
        throw std::runtime_error("GetActor() index out of bounds");
        return NULL;
    }
}

size_t CM_FindActorIndex(Actor* actor) {
    // Move the ptr back to look at the vtable.
    // This gets us the proper C++ class instead of just the variables used in C.
    AActor* a = reinterpret_cast<AActor*>(reinterpret_cast<char*>(actor) - sizeof(void*));
    auto& actors = GetWorld()->Actors;

    auto it = std::find_if(actors.begin(), actors.end(), [a](const std::unique_ptr<AActor>& ptr) {
        return ptr.get() == a;
    });

    if (it != actors.end()) {
        return std::distance(actors.begin(), it);
    }
    printf("FindActorIndex() actor not found\n");
    return -1;
}

void CM_DeleteActor(size_t index) {
    auto& actors = GetWorld()->Actors;
    if (index < actors.size()) {
        actors.erase(actors.begin() + index);
    }
}

/**
 * Clean up actors and other game objects.
 */
void CM_CleanWorld(void) {
    GetWorld()->CleanWorld();
}

void CM_CleanCameras(void) {
    GetWorld()->Cameras.clear();
}

struct Actor* CM_AddBaseActor() {
    return (struct Actor*) GetWorld()->AddBaseActor();
}

void CM_ActorBeginPlay(struct Actor* actor) {
    GetWorld()->ActorBeginPlay(actor);
}

void CM_ActorGenerateCollision(struct Actor* actor) {
    AActor* act = GetWorld()->ConvertActorToAActor(actor);

    if ((nullptr != act->Model) && (act->Model[0] != '\0')) {
        if (act->Triangles.size() == 0) {
            TrackEditor::GenerateCollisionMesh(act, (Gfx*)LOAD_ASSET_RAW(act->Model), 1.0f);
        }
    }
}

void Editor_AddLight(s8* direction) {
    static size_t i = 0;
    gEditor.AddLight(("Light "+std::to_string(i)).c_str(), nullptr, direction);
    i += 1;
}

void Editor_ClearMatrix() {
    gEditor.ClearMatrixPool();
}

void Editor_CleanWorld() {
    if (bCleanWorld) {
        CM_CleanWorld();
        bCleanWorld = false;
    }
}

size_t CM_GetActorSize() {
    return GetWorld()->Actors.size();
}

void CM_ActorCollision(Player* player, Actor* actor) {
    AActor* a = GetWorld()->ConvertActorToAActor(actor);

    if (a->IsMod()) {
        a->Collision(player, a);
    }
}

f32 CM_GetWaterLevel(Vec3f pos, struct Collision* collision) {
    FVector fPos = {pos[0], pos[1], pos[2]};
    return GetWorld()->GetTrack()->GetWaterLevel(fPos, collision);
}

// clang-format off
bool IsMarioRaceway()     { return dynamic_cast<MarioRaceway*>(GetWorld()->GetTrack()) != nullptr; }
bool IsLuigiRaceway()     { return dynamic_cast<LuigiRaceway*>(GetWorld()->GetTrack()) != nullptr; }
bool IsChocoMountain()    { return dynamic_cast<ChocoMountain*>(GetWorld()->GetTrack()) != nullptr; }
bool IsBowsersCastle()    { return dynamic_cast<BowsersCastle*>(GetWorld()->GetTrack()) != nullptr; }
bool IsBansheeBoardwalk() { return dynamic_cast<BansheeBoardwalk*>(GetWorld()->GetTrack()) != nullptr; }
bool IsYoshiValley()      { return dynamic_cast<YoshiValley*>(GetWorld()->GetTrack()) != nullptr; }
bool IsFrappeSnowland()   { return dynamic_cast<FrappeSnowland*>(GetWorld()->GetTrack()) != nullptr; }
bool IsKoopaTroopaBeach() { return dynamic_cast<KoopaTroopaBeach*>(GetWorld()->GetTrack()) != nullptr; }
bool IsRoyalRaceway()     { return dynamic_cast<RoyalRaceway*>(GetWorld()->GetTrack()) != nullptr; }
bool IsMooMooFarm()       { return dynamic_cast<MooMooFarm*>(GetWorld()->GetTrack()) != nullptr; }
bool IsToadsTurnpike()    { return dynamic_cast<ToadsTurnpike*>(GetWorld()->GetTrack()) != nullptr; }
bool IsKalimariDesert()   { return dynamic_cast<KalimariDesert*>(GetWorld()->GetTrack()) != nullptr; }
bool IsSherbetLand()      { return dynamic_cast<SherbetLand*>(GetWorld()->GetTrack()) != nullptr; }
bool IsRainbowRoad()      { return dynamic_cast<RainbowRoad*>(GetWorld()->GetTrack()) != nullptr; }
bool IsWarioStadium()     { return dynamic_cast<WarioStadium*>(GetWorld()->GetTrack()) != nullptr; }
bool IsBlockFort()        { return dynamic_cast<BlockFort*>(GetWorld()->GetTrack()) != nullptr; }
bool IsSkyscraper()       { return dynamic_cast<Skyscraper*>(GetWorld()->GetTrack()) != nullptr; }
bool IsDoubleDeck()       { return dynamic_cast<DoubleDeck*>(GetWorld()->GetTrack()) != nullptr; }
bool IsDkJungle()         { return dynamic_cast<DKJungle*>(GetWorld()->GetTrack()) != nullptr; }
bool IsBigDonut()         { return dynamic_cast<BigDonut*>(GetWorld()->GetTrack()) != nullptr; }
bool IsPodiumCeremony()   { return dynamic_cast<PodiumCeremony*>(GetWorld()->GetTrack()) != nullptr; }

void SelectMarioRaceway()       { GetWorld()->SetCurrentTrack(std::make_unique<MarioRaceway>()); }
void SelectLuigiRaceway()       { GetWorld()->SetCurrentTrack(std::make_unique<LuigiRaceway>()); }
void SelectChocoMountain()      { GetWorld()->SetCurrentTrack(std::make_unique<ChocoMountain>()); }
void SelectBowsersCastle()      { GetWorld()->SetCurrentTrack(std::make_unique<BowsersCastle>()); }
void SelectBansheeBoardwalk()   { GetWorld()->SetCurrentTrack(std::make_unique<BansheeBoardwalk>()); }
void SelectYoshiValley()        { GetWorld()->SetCurrentTrack(std::make_unique<YoshiValley>()); }
void SelectFrappeSnowland()     { GetWorld()->SetCurrentTrack(std::make_unique<FrappeSnowland>()); }
void SelectKoopaTroopaBeach()   { GetWorld()->SetCurrentTrack(std::make_unique<KoopaTroopaBeach>()); }
void SelectRoyalRaceway()       { GetWorld()->SetCurrentTrack(std::make_unique<RoyalRaceway>()); }
void SelectMooMooFarm()         { GetWorld()->SetCurrentTrack(std::make_unique<MooMooFarm>()); }
void SelectToadsTurnpike()      { GetWorld()->SetCurrentTrack(std::make_unique<ToadsTurnpike>()); }
void SelectKalimariDesert()     { GetWorld()->SetCurrentTrack(std::make_unique<KalimariDesert>()); }
void SelectSherbetLand()        { GetWorld()->SetCurrentTrack(std::make_unique<SherbetLand>()); }
void SelectRainbowRoad()        { GetWorld()->SetCurrentTrack(std::make_unique<RainbowRoad>()); }
void SelectWarioStadium()       { GetWorld()->SetCurrentTrack(std::make_unique<WarioStadium>()); }
void SelectBlockFort()          { GetWorld()->SetCurrentTrack(std::make_unique<BlockFort>()); }
void SelectSkyscraper()         { GetWorld()->SetCurrentTrack(std::make_unique<Skyscraper>()); }
void SelectDoubleDeck()         { GetWorld()->SetCurrentTrack(std::make_unique<DoubleDeck>()); }
void SelectDkJungle()           { GetWorld()->SetCurrentTrack(std::make_unique<DKJungle>()); }
void SelectBigDonut()           { GetWorld()->SetCurrentTrack(std::make_unique<BigDonut>()); }
void SelectPodiumCeremony()     { GetWorld()->SetCurrentTrack(std::make_unique<PodiumCeremony>()); }
// clang-format on

void* GetMushroomCup(void) {
    return gMushroomCup.get();
}

void* GetFlowerCup(void) {
    return gFlowerCup.get();
}

void* GetStarCup(void) {
    return gStarCup.get();
}

void* GetSpecialCup(void) {
    return gSpecialCup.get();
}

void* GetBattleCup(void) {
    return gBattleCup.get();
}

// End of frame cleanup of actors, objects, etc.
void CM_RunGarbageCollector(void) {
    RunGarbageCollector();
}

void CM_ResetAudio(void) {
    if(HMAS_IsPlaying(HMAS_MUSIC)){
        HMAS_AddEffect(HMAS_MUSIC, HMAS_EFFECT_VOLUME, HMAS_LINEAR, 10, 0);
        HMAS_AddEffect(HMAS_MUSIC, HMAS_EFFECT_STOP,   HMAS_INSTANT, 1, 0);
    }

    // Fade out music for all sequences and music player indexes 0, and 1
    for (size_t soundId = 0; soundId < MUSIC_SEQ_MAX; soundId++) {
        func_800C3448(0x10100000 | soundId);
        func_800C3448(0x11100000 | soundId);
    }
}
}

static std::atomic<bool> sResetRequested{ false };

void CM_RequestReset(void) {
    sResetRequested.store(true);
}

// The reset widget only requests; the reset is applied here at the top of the
// game frame. Applying it from the widget raced the menu state machine: a
// press landing mid-fade was re-advanced by the in-flight transition, and a
// repeat press rewrote the gamestate it had already set, which the != guard
// in main.c swallows while the audio fade still runs (silent no-op).
static void ApplyPendingReset() {
    if (!sResetRequested.exchange(false)) {
        return;
    }

    // The FROM_QUIT gamestates run identical inits; alternating keeps
    // gGamestateNext != gGamestate true so every press re-enters the menus.
    gGamestateNext = (gGamestate == MAIN_MENU_FROM_QUIT) ? START_MENU_FROM_QUIT : MAIN_MENU_FROM_QUIT;
    gIsGamePaused = 0;
    // Reset credits
    D_800DC5E4 = 0;
    gTourComplete = false;
    SetMarioRaceway();
    memset(&gGameModeMenuColumn, 0, sizeof(s8) * NUM_ROWS_GAME_MODE_MENU);
    memset(&gGameModeSubMenuColumn, 0, sizeof(s8) * NUM_COLUMN_GAME_MODE_SUB_MENU * NUM_ROWS_GAME_MODE_SUB_MENU);

    CM_ResetAudio();

    // Close the editor.
    if (gEditor.IsEnabled()) {
        gEditor.Disable();
    }

    // Set the debug menu track browsing index back to zero
    TrackBrowser::Instance->Reset();

    // Land on the same screen the gSkipIntro setting picks at boot.
    switch (CVarGetInteger("gSkipIntro", 0)) {
        case 0:
            gMenuSelection = HARBOUR_MASTERS_MENU;
            break;
        case 1:
            gMenuSelection = LOGO_INTRO_MENU;
            break;
        case 2:
            gMenuSelection = START_MENU;
            break;
        case 3:
            gMenuSelection = MAIN_MENU;
            break;
    }

    // Debug mode override gSkipIntro
    if (CVarGetInteger("gEnableDebugMode", 0) == true) {
        gMenuSelection = START_MENU;
    }
    // Re-enter through the intro's own transition protocol (see HM_TickIntro):
    // FADE_MODE_LOGO makes setup_menus rebuild the menu items and start a
    // fresh fade-in, replacing any in-flight transition that would otherwise
    // advance the stale screen right after the reset.
    gMenuFadeType = 0;
    gFadeModeSelection = FADE_MODE_LOGO;
}

// ============================================================================
// TEST RIG, NEVER MERGE: scene jumper for PR #753 testing.
//   ]  next scene      [  previous scene      \  replay current scene
//   =  cycle game aspect ratio (4:3, 16:9, 21:9, 32:9, off) without resizing the window
// Jumps reuse the reset path above (drop to the menus), then launch a race the
// way the attract demo does, then optionally press Start for a player, force
// the human players over the finish line, or pop the other battle balloons.
// ============================================================================
#include <imgui.h>
#include <cstdio>

extern "C" {
extern s16 gCreditsCourseId;
extern s16 gPlayerBalloonCount[];
extern s32 gLapCountByPlayerId[];
extern s8 gCharacterSelections[];
extern s8 gDemoUseController;
extern bool gGhostPlayerInit;
void func_8000F124(void);
void set_mirror_mode(s32 mirror);
}

namespace {
enum RigKind { RIG_MENU, RIG_RACE, RIG_CREDITS };
enum RigAction { RIG_NONE, RIG_PAUSE, RIG_FINISH, RIG_BATTLE_WIN };

struct RigScene {
    const char* name;
    RigKind kind;
    s32 menu;
    s32 mode;
    s32 players;
    RigAction action;
    s32 actionPlayer;
};

#define RIG_M(name, menu) { name, RIG_MENU, menu, GRAND_PRIX, 1, RIG_NONE, 0 }
#define RIG_R(name, mode, players, action, p) { name, RIG_RACE, MAIN_MENU, mode, players, action, p }

const RigScene sRigScenes[] = {
    RIG_M("Title screen", START_MENU),
    RIG_M("Main menu", MAIN_MENU),
    RIG_M("Character select", CHARACTER_SELECT_MENU),
    RIG_M("Course select", COURSE_SELECT_MENU),
    RIG_M("Options", OPTIONS_MENU),
    RIG_M("Records", DATA_MENU),
    RIG_R("GP 1P: race start, course name slides in", GRAND_PRIX, 1, RIG_NONE, 0),
    RIG_R("GP 1P: pause", GRAND_PRIX, 1, RIG_PAUSE, 0),
    RIG_R("GP 1P: results, then points (auto finish)", GRAND_PRIX, 1, RIG_FINISH, 0),
    RIG_R("GP 2P: pause as player 1", GRAND_PRIX, 2, RIG_PAUSE, 0),
    RIG_R("GP 2P: results, then points (auto finish)", GRAND_PRIX, 2, RIG_FINISH, 0),
    RIG_R("VS 2P: pause as player 1", VERSUS, 2, RIG_PAUSE, 0),
    RIG_R("VS 2P: results (auto finish)", VERSUS, 2, RIG_FINISH, 0),
    RIG_R("VS 3P: pause as player 1, black panel names", VERSUS, 3, RIG_PAUSE, 0),
    RIG_R("VS 3P: pause as player 2", VERSUS, 3, RIG_PAUSE, 1),
    RIG_R("VS 3P: pause as player 3", VERSUS, 3, RIG_PAUSE, 2),
    RIG_R("VS 3P: results (auto finish)", VERSUS, 3, RIG_FINISH, 0),
    RIG_R("VS 4P: pause as player 1", VERSUS, 4, RIG_PAUSE, 0),
    RIG_R("VS 4P: pause as player 2", VERSUS, 4, RIG_PAUSE, 1),
    RIG_R("VS 4P: pause as player 3", VERSUS, 4, RIG_PAUSE, 2),
    RIG_R("VS 4P: pause as player 4", VERSUS, 4, RIG_PAUSE, 3),
    RIG_R("VS 4P: results (auto finish)", VERSUS, 4, RIG_FINISH, 0),
    RIG_R("Battle 3P: pause as player 1, black panel names", BATTLE, 3, RIG_PAUSE, 0),
    RIG_R("Battle 3P: pause as player 2", BATTLE, 3, RIG_PAUSE, 1),
    RIG_R("Battle 3P: pause as player 3", BATTLE, 3, RIG_PAUSE, 2),
    RIG_R("Battle 3P: results (auto win for player 1)", BATTLE, 3, RIG_BATTLE_WIN, 0),
    RIG_R("Battle 4P: pause as player 1", BATTLE, 4, RIG_PAUSE, 0),
    RIG_R("Battle 4P: pause as player 2", BATTLE, 4, RIG_PAUSE, 1),
    RIG_R("Battle 4P: pause as player 3", BATTLE, 4, RIG_PAUSE, 2),
    RIG_R("Battle 4P: pause as player 4", BATTLE, 4, RIG_PAUSE, 3),
    RIG_R("Battle 4P: results (auto win for player 1)", BATTLE, 4, RIG_BATTLE_WIN, 0),
    RIG_R("Time Trials: pause", TIME_TRIALS, 1, RIG_PAUSE, 0),
    RIG_R("Time Trials: finish screen (auto finish)", TIME_TRIALS, 1, RIG_FINISH, 0),
    { "Credits", RIG_CREDITS, MAIN_MENU, GRAND_PRIX, 1, RIG_NONE, 0 },
};
const int kRigSceneCount = sizeof(sRigScenes) / sizeof(sRigScenes[0]);

enum RigPhase { PH_IDLE, PH_RESET, PH_MENU_WAIT, PH_RACE_WAIT, PH_ACTION, PH_DONE };
int sRigIndex = -1;
RigPhase sRigPhase = PH_IDLE;
int sRigTimer = 0;
int sRigNextFinisher = 0;
int sRigInjectStart = -1;
int sRigAspect = -1;
const char* sRigStatus = "press ] to start";

const char* kRigAspectNames[] = { "4:3", "16:9", "21:9", "32:9" };

bool RigKeyEdge(ImGuiKey key, int slot) {
    // Edge-detect across game ticks so a quick tap is never missed.
    static bool prev[4] = { false, false, false, false };
    bool down = ImGui::IsKeyDown(key);
    bool edge = down && !prev[slot];
    prev[slot] = down;
    return edge;
}

void RigCycleAspect() {
    // Uses libultraship's advanced resolution aspect setting, which shapes the
    // game picture inside the window (fullscreen included) without resizing it.
    // "off" puts back whatever the settings were before the first press.
    static bool saved = false;
    static int savedEnabled = 0;
    static float savedX = 0.0f;
    static float savedY = 0.0f;
    static int savedVertical = 0;
    if (!saved) {
        savedEnabled = CVarGetInteger(CVAR_PREFIX_ADVANCED_RESOLUTION ".Enabled", 0);
        savedX = CVarGetFloat(CVAR_PREFIX_ADVANCED_RESOLUTION ".AspectRatioX", 16.0f);
        savedY = CVarGetFloat(CVAR_PREFIX_ADVANCED_RESOLUTION ".AspectRatioY", 9.0f);
        savedVertical = CVarGetInteger(CVAR_PREFIX_ADVANCED_RESOLUTION ".VerticalResolutionToggle", 0);
        saved = true;
    }
    sRigAspect++;
    if (sRigAspect >= 4) {
        sRigAspect = -1;
        CVarSetInteger(CVAR_PREFIX_ADVANCED_RESOLUTION ".Enabled", savedEnabled);
        CVarSetFloat(CVAR_PREFIX_ADVANCED_RESOLUTION ".AspectRatioX", savedX);
        CVarSetFloat(CVAR_PREFIX_ADVANCED_RESOLUTION ".AspectRatioY", savedY);
        CVarSetInteger(CVAR_PREFIX_ADVANCED_RESOLUTION ".VerticalResolutionToggle", savedVertical);
        return;
    }
    const float x[] = { 4.0f, 16.0f, 21.0f, 32.0f };
    const float y[] = { 3.0f, 9.0f, 9.0f, 9.0f };
    CVarSetInteger(CVAR_PREFIX_ADVANCED_RESOLUTION ".Enabled", 1);
    CVarSetInteger(CVAR_PREFIX_ADVANCED_RESOLUTION ".VerticalResolutionToggle", 0);
    CVarSetFloat(CVAR_PREFIX_ADVANCED_RESOLUTION ".AspectRatioX", x[sRigAspect]);
    CVarSetFloat(CVAR_PREFIX_ADVANCED_RESOLUTION ".AspectRatioY", y[sRigAspect]);
}

void RigStartScene() {
    sRigPhase = PH_RESET;
    sRigTimer = 0;
    sRigNextFinisher = 0;
    sRigInjectStart = -1;
    sRigStatus = "going to the menus";
}

void RigResetToMenus(const RigScene& scene) {
    // Same steps as ApplyPendingReset, landing on the scene's menu.
    gGamestateNext = (gGamestate == MAIN_MENU_FROM_QUIT) ? START_MENU_FROM_QUIT : MAIN_MENU_FROM_QUIT;
    gIsGamePaused = 0;
    D_800DC5E4 = 0;
    gTourComplete = false;
    gDemoMode = 0;
    gDemoUseController = 0;
    SetMarioRaceway();
    memset(&gGameModeMenuColumn, 0, sizeof(s8) * NUM_ROWS_GAME_MODE_MENU);
    memset(&gGameModeSubMenuColumn, 0, sizeof(s8) * NUM_COLUMN_GAME_MODE_SUB_MENU * NUM_ROWS_GAME_MODE_SUB_MENU);
    CM_ResetAudio();
    if (gEditor.IsEnabled()) {
        gEditor.Disable();
    }
    TrackBrowser::Instance->Reset();
    gModeSelection = GRAND_PRIX;
    gPlayerCountSelection1 = 1;
    gPlayerCount = 1;
    gScreenModeSelection = SCREEN_MODE_1P;
    gMenuSelection = scene.menu;
    gMenuFadeType = 0;
    gFadeModeSelection = FADE_MODE_LOGO;
}

void RigLaunchRace(const RigScene& scene) {
    gDemoMode = 0;
    gDemoUseController = 0;
    gModeSelection = scene.mode;
    gPlayerCountSelection1 = scene.players;
    gPlayerCount = (s8) scene.players;
    if (scene.players == 1) {
        gScreenModeSelection = SCREEN_MODE_1P;
    } else if (scene.players == 2) {
        gScreenModeSelection = SCREEN_MODE_2P_SPLITSCREEN_HORIZONTAL;
    } else {
        gScreenModeSelection = SCREEN_MODE_3P_4P_SPLITSCREEN;
    }
    for (int i = 0; i < 4; i++) {
        gCharacterSelections[i] = (s8) i;
    }
    gCCSelection = 1;
    set_mirror_mode(0);
    if (scene.mode == BATTLE) {
        SelectBlockFort();
        CM_SetCup(GetBattleCup());
    } else {
        SelectMarioRaceway();
        CM_SetCup(GetFlowerCup());
    }
    TrackBrowser_SetTrackFromCup();
    D_800DC540 = (s32) GetCupIndex();
    gCourseIndexInCup = GetCupCursorPosition();
    gGhostPlayerInit = (scene.mode == TIME_TRIALS);
    func_8000F124();
    gGamestateNext = RACING;
}

void RigDoAction(const RigScene& scene) {
    sRigTimer++;
    switch (scene.action) {
        case RIG_NONE:
            sRigPhase = PH_DONE;
            sRigStatus = "ready";
            break;
        case RIG_PAUSE:
            if (sRigTimer >= 30) {
                sRigInjectStart = scene.actionPlayer;
                sRigPhase = PH_DONE;
                sRigStatus = "paused";
            }
            break;
        case RIG_BATTLE_WIN:
            if (sRigTimer >= 45) {
                for (int i = 1; i < scene.players; i++) {
                    gPlayerBalloonCount[i] = -1;
                }
                sRigPhase = PH_DONE;
                sRigStatus = "popped the other balloons";
            }
            break;
        case RIG_FINISH:
            if ((gRaceState != RACE_IN_PROGRESS) && (gRaceState != RACE_CALCULATE_RANKS)) {
                sRigPhase = PH_DONE;
                sRigStatus = "race finished";
                break;
            }
            // One player at a time, so each finish runs through the game's own logic alone.
            if ((sRigTimer >= 45) && ((sRigTimer % 25) == 20)) {
                while (sRigNextFinisher < 4) {
                    Player* player = &gPlayers[sRigNextFinisher];
                    int index = sRigNextFinisher++;
                    if ((player->type & PLAYER_EXISTS) && (player->type & PLAYER_HUMAN) &&
                        !(player->type & PLAYER_CPU)) {
                        gLapCountByPlayerId[index] = 3;
                        sRigStatus = "finishing players";
                        break;
                    }
                }
                if (sRigNextFinisher >= 4) {
                    sRigStatus = "waiting for results";
                }
            }
            break;
    }
}

void RigBeginFrame() {
    if (RigKeyEdge(ImGuiKey_RightBracket, 0)) {
        sRigIndex = (sRigIndex + 1) % kRigSceneCount;
        RigStartScene();
    }
    if (RigKeyEdge(ImGuiKey_LeftBracket, 1)) {
        sRigIndex = (sRigIndex <= 0) ? (kRigSceneCount - 1) : (sRigIndex - 1);
        RigStartScene();
    }
    if (RigKeyEdge(ImGuiKey_Backslash, 2)) {
        if (sRigIndex < 0) {
            sRigIndex = 0;
        }
        RigStartScene();
    }
    if (RigKeyEdge(ImGuiKey_Equal, 3)) {
        RigCycleAspect();
    }
    if (sRigIndex < 0) {
        return;
    }
    const RigScene& scene = sRigScenes[sRigIndex];
    switch (sRigPhase) {
        case PH_RESET:
            RigResetToMenus(scene);
            sRigTimer = 0;
            if (scene.kind == RIG_MENU) {
                sRigPhase = PH_DONE;
                sRigStatus = "ready";
            } else {
                sRigPhase = PH_MENU_WAIT;
                sRigStatus = "loading";
            }
            break;
        case PH_MENU_WAIT:
            sRigTimer++;
            if ((sRigTimer >= 20) && (gGamestate == gGamestateNext)) {
                if (scene.kind == RIG_CREDITS) {
                    gCreditsCourseId = TRACK_LUIGI_RACEWAY;
                    gGamestateNext = CREDITS_SEQUENCE;
                    sRigPhase = PH_DONE;
                    sRigStatus = "ready";
                } else {
                    RigLaunchRace(scene);
                    sRigPhase = PH_RACE_WAIT;
                    sRigStatus = "starting race";
                }
            }
            break;
        case PH_RACE_WAIT:
            if ((gGamestate == RACING) && (gRaceState == RACE_IN_PROGRESS)) {
                sRigPhase = PH_ACTION;
                sRigTimer = 0;
                sRigStatus = "race running";
            }
            break;
        case PH_ACTION:
            RigDoAction(scene);
            break;
        default:
            break;
    }
}

class RigOverlayWindow : public Ship::GuiWindow {
  public:
    using Ship::GuiWindow::GuiWindow;

  protected:
    void InitElement() override {
    }
    void UpdateElement() override {
    }
    void DrawElement() override {
        // libultraship draws the game picture after every GuiWindow, which would
        // cover a normal window, so draw on the foreground layer instead.
        char lines[3][160];
        int count = 0;
        if (sRigIndex < 0) {
            snprintf(lines[count++], sizeof(lines[0]), "TEST RIG  press ] to start");
        } else {
            snprintf(lines[count++], sizeof(lines[0]), "#%d of %d  %s", sRigIndex + 1, kRigSceneCount,
                     sRigScenes[sRigIndex].name);
            snprintf(lines[count++], sizeof(lines[0]), "status: %s", sRigStatus);
        }
        snprintf(lines[count++], sizeof(lines[0]), "[ ] prev/next   \\ replay   = aspect: %s",
                 sRigAspect < 0 ? "off (window shape)" : kRigAspectNames[sRigAspect]);

        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        ImFont* font = ImGui::GetFont();
        float fontSize = ImGui::GetFontSize() * 1.25f;
        ImVec2 origin = ImGui::GetMainViewport()->Pos;
        origin.x += 12.0f;
        origin.y += 12.0f;
        float width = 0.0f;
        for (int i = 0; i < count; i++) {
            ImVec2 size = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, lines[i]);
            width = (size.x > width) ? size.x : width;
        }
        float lineHeight = fontSize + 4.0f;
        drawList->AddRectFilled(ImVec2(origin.x - 8.0f, origin.y - 6.0f),
                                ImVec2(origin.x + width + 8.0f, origin.y + (lineHeight * count) + 4.0f),
                                IM_COL32(0, 0, 0, 190), 6.0f);
        for (int i = 0; i < count; i++) {
            drawList->AddText(font, fontSize, ImVec2(origin.x, origin.y + (lineHeight * i)),
                              (i == 0) ? IM_COL32(255, 230, 80, 255) : IM_COL32(255, 255, 255, 255), lines[i]);
        }
    }
};
} // namespace

std::shared_ptr<Ship::GuiWindow> Rig_CreateOverlayWindow() {
    CVarSetInteger("gRigOverlay", 1);
    auto window = std::make_shared<RigOverlayWindow>(
        "gRigOverlay", "Test Rig", ImVec2(-1, -1),
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBackground |
            ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoNav);
    return window;
}

extern "C" void Rig_AfterControllers(void) {
    // Runs right after the game reads the controllers, like a real Start press.
    if (sRigInjectStart >= 0) {
        gControllers[sRigInjectStart].buttonPressed |= START_BUTTON;
        sRigInjectStart = -1;
    }
}
// ========================== END TEST RIG ====================================

void push_frame() {
    ApplyPendingReset();
    RigBeginFrame();
    GameEngine::StartAudioFrame();
    GameEngine::Instance->StartFrame();
    thread5_iteration();
    GameEngine::EndAudioFrame();
    // thread5_game_loop();
    // Graphics_ThreadUpdate();w
}

void CM_ThrowRuntimeError(const char* fmt, ...) {
    char error_mesg[2048];

    va_list args;
    va_start(args, fmt);
    vsnprintf(error_mesg, sizeof(error_mesg), fmt, args);
    va_end(args);

    const char* crash_desc = "\nSpaghettiKart has crashed! Please upload the logs to the support channel in Discord.";
    strncat(error_mesg, crash_desc, sizeof(error_mesg) - strlen(error_mesg) - 1);

    SPDLOG_ERROR(error_mesg);

    SDL_ShowSimpleMessageBox(
        SDL_MESSAGEBOX_ERROR,
        "You dropped your plate of Spaghetti!",
        error_mesg,
        NULL
    );

    exit(EXIT_FAILURE);
}

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

#ifdef _WIN32
int SDL_main(int argc, char** argv) {
#else
#if defined(__cplusplus) && defined(PLATFORM_IOS)
extern "C"
#endif
    int
    main(int argc, char* argv[]) {
#endif
#ifdef _WIN32
    // Allow non-ascii characters for Windows
    setlocale(LC_ALL, ".UTF8");
#endif
#if defined(__APPLE__)
    // Disable the macOS "press and hold" accent/diacritic popup for this app. SDL keeps a Cocoa text
    // input context active, so holding a movement key is interpreted as holding a letter key in a text
    // field, and macOS shows the accent picker instead of repeating it. Per-app equivalent of
    // `defaults write -app <App> ApplePressAndHoldEnabled -bool false`; key repeat still works.
    CFPreferencesSetAppValue(CFSTR("ApplePressAndHoldEnabled"), kCFBooleanFalse, kCFPreferencesCurrentApplication);
    CFPreferencesAppSynchronize(kCFPreferencesCurrentApplication);
#endif
#if defined(__APPLE__) && !defined(PLATFORM_IOS)
    // Default the writable data folder to ~/Library/Application Support/SpaghettiKart
    // (libultraship expands the ~ and creates the directory). Without this, a Finder
    // launch has no SHIP_HOME and libultraship falls back to the current working
    // directory, scattering config/saves/mods into the user's home folder.
    // overwrite=0 keeps any SHIP_HOME the user already set.
    setenv("SHIP_HOME", "~/Library/Application Support/SpaghettiKart", 0);
#endif
    // load_wasm();
    GameEngine::Create();
    audio_init();
    sound_init();

    CustomEngineInit();

    switch(CVarGetInteger("gSkipIntro", 0)) {
        case 0:
            gMenuSelection = HARBOUR_MASTERS_MENU;
            break;
        case 1:
            gMenuSelection = LOGO_INTRO_MENU;
            break;
        case 2:
            gMenuSelection = START_MENU;
            break;
        case 3:
            gMenuSelection = MAIN_MENU;
            break;
    }

    // Debug mode override gSkipIntro
    if (CVarGetInteger("gEnableDebugMode", 0) == true) {
        gMenuSelection = START_MENU;
    }

    thread5_game_loop();
    gEditor.Load();
    while (WindowIsRunning()) {
        push_frame();
    }
    CustomEngineDestroy();
    // GameEngine::Instance->ProcessFrame(push_frame);
    GameEngine::Instance->Destroy();
    // Everything user-visible (config, saves) is persisted by Destroy(). Skip the
    // remaining static destructors: their order is unspecified and some log after
    // spdlog's own statics are gone, crashing on quit (same class as issue #689).
    // Mirrors the _Exit precedent on the extraction error paths.
    _Exit(0);
}
