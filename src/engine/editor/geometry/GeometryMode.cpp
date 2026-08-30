#include "GeometryMode.h"
#include "GeometryCatalog.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>

#include "engine/World.h"
#include "engine/tracks/Track.h"
#include "engine/AllTracks.h"
#include "assets/models/tracks/mario_raceway/mario_raceway_data.h"
#include "assets/models/tracks/choco_mountain/choco_mountain_data.h"
#include "assets/models/tracks/bowsers_castle/bowsers_castle_data.h"
#include "assets/models/tracks/banshee_boardwalk/banshee_boardwalk_data.h"
#include "assets/models/tracks/yoshi_valley/yoshi_valley_data.h"
#include "assets/models/tracks/frappe_snowland/frappe_snowland_data.h"
#include "assets/models/tracks/koopa_troopa_beach/koopa_troopa_beach_data.h"
#include "assets/models/tracks/royal_raceway/royal_raceway_data.h"
#include "assets/models/tracks/luigi_raceway/luigi_raceway_data.h"
#include "assets/models/tracks/moo_moo_farm/moo_moo_farm_data.h"
#include "assets/models/tracks/toads_turnpike/toads_turnpike_data.h"
#include "assets/models/tracks/kalimari_desert/kalimari_desert_data.h"
#include "assets/models/tracks/sherbet_land/sherbet_land_data.h"
#include "assets/models/tracks/rainbow_road/rainbow_road_data.h"
#include "assets/models/tracks/wario_stadium/wario_stadium_data.h"
#include "assets/models/tracks/dks_jungle_parkway/dks_jungle_parkway_data.h"
#include "engine/editor/EditorMath.h"
#include "port/Engine.h"
#include <fast/interpreter.h>

extern "C" {
#include "main.h"
#include "code_800029B0.h"
#include "defines.h"
#include "racing/math_util.h"
}
#include "engine/Matrix.h"

namespace GeometryEditor {

static GeometryCatalog sCatalog;
static const char* const* sPrimaryTable = nullptr;
static size_t sSectionCount = 0;
static const void* sCatalogTrack = nullptr; // Track* the catalog belongs to

// Visibility-table row counts per course, from the course_offsets arrays
// (entries / 4). The collision sections list can disagree with the table, so
// the table length is the binding limit. Keyed on the track class, the same
// way the port's Is<Course>() predicates identify courses (Track::ResourceName
// is only populated on the track-browser launch path, not the game menus).
// Courses returning 0 (battle arenas, ceremony, custom) keep the mode off.
static size_t SectionTableLimit(Track* track) {
    if (dynamic_cast<MarioRaceway*>(track)) return 17;
    if (dynamic_cast<ChocoMountain*>(track)) return 24;
    if (dynamic_cast<BowsersCastle*>(track)) return 27;
    if (dynamic_cast<BansheeBoardwalk*>(track)) return 25;
    if (dynamic_cast<YoshiValley*>(track)) return 31;
    if (dynamic_cast<FrappeSnowland*>(track)) return 17;
    if (dynamic_cast<KoopaTroopaBeach*>(track)) return 37;
    if (dynamic_cast<RoyalRaceway*>(track)) return 33;
    if (dynamic_cast<LuigiRaceway*>(track)) return 30;
    if (dynamic_cast<MooMooFarm*>(track)) return 23;
    if (dynamic_cast<ToadsTurnpike*>(track)) return 20;
    if (dynamic_cast<KalimariDesert*>(track)) return 20;
    if (dynamic_cast<SherbetLand*>(track)) return 18;
    if (dynamic_cast<RainbowRoad*>(track)) return 12;
    if (dynamic_cast<WarioStadium*>(track)) return 27;
    if (dynamic_cast<DKJungle*>(track)) return 26;
    return 0;
}

// The authored full-course credits list per course: the stable one-of-each
// assembly (the racing lists contain multiple detail variants of the same
// surfaces, which fight when drawn together).
static const char* CreditsRootFor(Track* track) {
    if (dynamic_cast<MarioRaceway*>(track)) return d_course_mario_raceway_dl_9348;
    if (dynamic_cast<ChocoMountain*>(track)) return d_course_choco_mountain_dl_71B8;
    if (dynamic_cast<BowsersCastle*>(track)) return d_course_bowsers_castle_dl_9148;
    if (dynamic_cast<BansheeBoardwalk*>(track)) return d_course_banshee_boardwalk_dl_B308;
    if (dynamic_cast<YoshiValley*>(track)) return d_course_yoshi_valley_dl_18020;
    if (dynamic_cast<FrappeSnowland*>(track)) return d_course_frappe_snowland_dl_76A0;
    if (dynamic_cast<KoopaTroopaBeach*>(track)) return d_course_koopa_troopa_beach_dl_18D68;
    if (dynamic_cast<RoyalRaceway*>(track)) return d_course_royal_raceway_dl_D8E8;
    if (dynamic_cast<LuigiRaceway*>(track)) return d_course_luigi_raceway_dl_FD40;
    if (dynamic_cast<MooMooFarm*>(track)) return d_course_moo_moo_farm_dl_14088;
    if (dynamic_cast<ToadsTurnpike*>(track)) return d_course_toads_turnpike_dl_23930;
    if (dynamic_cast<KalimariDesert*>(track)) return d_course_kalimari_desert_dl_22E00;
    if (dynamic_cast<SherbetLand*>(track)) return d_course_sherbet_land_dl_9AE8;
    if (dynamic_cast<RainbowRoad*>(track)) return d_course_rainbow_road_dl_16220;
    if (dynamic_cast<WarioStadium*>(track)) return d_course_wario_stadium_dl_CA78;
    if (dynamic_cast<DKJungle*>(track)) return d_course_dks_jungle_parkway_dl_13C30;
    return nullptr;
}

// Selection / edit state
static int32_t sSelected = -1;             // index into sCatalog.Vertices()
static std::vector<size_t> sMoveSet;       // selected + weld members (catalog indices)
static bool sWeld = true;
static bool sCloud = true;
static bool sHideBackfaces = true;
static bool sXray = false;
static int sCourseView = 1; // 0 = race union (diagnostic), 1 = credits assembly (default)

// Front-facing test from accumulated face normals; vertices with no faces
// (never triangulated) count as visible.
static bool RefVisible(const VertexRef& ref, const float camPos[3]) {
    if (!sHideBackfaces) {
        return true;
    }
    float toCam[3] = { camPos[0] - (float) ref.live->v.ob[0], camPos[1] - (float) ref.live->v.ob[1],
                       camPos[2] - (float) ref.live->v.ob[2] };
    float d = ref.nrm[0] * toCam[0] + ref.nrm[1] * toCam[1] + ref.nrm[2] * toCam[2];
    return d >= 0.0f || (ref.nrm[0] == 0.0f && ref.nrm[1] == 0.0f && ref.nrm[2] == 0.0f);
}

static DebugStatus sDebug;

// The game view is itself an ImGui window ("Main Game"), so WantCaptureMouse
// is true everywhere and cannot serve as a UI gate. Block game-view clicks
// only when the port menu is open or the cursor is over the editor window's
// own rectangle.
bool PointerOverUI(float mx, float my) {
    if (CVarGetInteger("gOpenMenu", 0) != 0) {
        return true;
    }
    if (ImGuiWindow* editorWindow = ImGui::FindWindowByName("Geometry Editor")) {
        if (editorWindow->Active && mx >= editorWindow->Pos.x && mx < editorWindow->Pos.x + editorWindow->Size.x &&
            my >= editorWindow->Pos.y && my < editorWindow->Pos.y + editorWindow->Size.y) {
            return true;
        }
    }
    return false;
}

// Mouse gate that shares the projection's coordinate basis (the game
// viewport), unlike IsInGameScreen which uses the render dimensions and
// disagrees below 100% resolution scale.
static bool MouseInViewport(int mx, int my) {
    auto viewport = GetInterpreter()->mGameWindowViewport;
    int w = (int) OTRGetGameViewportWidth();
    int h = (int) OTRGetGameViewportHeight();
    return mx >= viewport.x && mx < viewport.x + w && my >= viewport.y && my < viewport.y + h;
}

struct VertexWrite {
    size_t refIndex;
    int16_t before[3];
    int16_t after[3];
};
struct Transaction {
    std::vector<VertexWrite> writes;
};
static std::vector<Transaction> sUndo;
static std::vector<Transaction> sRedo;

// Drag state
static bool sDragging = false;
static bool sDragCandidate = false;
static float sDragStartHit[3];
static Transaction sDragTxn;

static bool ModeActive() {
    if (!CVarGetInteger("gGeometryEditorEnabled", 0)) {
        return false;
    }
    if (gIsMirrorMode != 0 || gGamestate != RACING) {
        return false;
    }
    Track* track = GetWorld()->GetTrack();
    if (track == nullptr || track->IsMod()) {
        return false;
    }
    // The catalog must belong to the course actually loaded; battle arenas
    // and the ceremony never parse track sections, so a stale stamp keeps
    // the mode safely off there.
    if ((const void*) track != sCatalogTrack) {
        return false;
    }
    return true;
}

bool WindowOpen() {
    return ModeActive();
}

DebugStatus GetDebugStatus() {
    return sDebug;
}

// ---- projection helpers (mirrors ScreenRayTrace's proven conventions) ----

struct ViewProjection {
    bool valid = false;
    Mat4 view;
    Mat4 persp;
    float xAdj = 1.0f; // replicates the interpreter's per-vertex aspect fix
    float vpX, vpY, vpW, vpH;
};

static ViewProjection BuildViewProjection() {
    ViewProjection vp;
    if (gScreenOneCtx == nullptr || gScreenOneCtx->camera == nullptr) {
        return vp;
    }
    Camera* camera = gScreenOneCtx->camera;
    if (camera->perspectiveMatrix != nullptr && camera->lookAtMatrix != nullptr) {
        // Use the exact matrices the renderer consumed (float Mtx on PC), so
        // picking agrees with the picture instead of re-deriving the camera.
        // These were built with the 4:3 aspect; the interpreter widens X per
        // vertex, so replicate that here.
        guMtxL2F(vp.persp, camera->perspectiveMatrix);
        guMtxL2F(vp.view, camera->lookAtMatrix);
        vp.xAdj = (4.0f / 3.0f) / OTRGetAspectRatio();
    } else {
        u16 perspNorm;
        guPerspectiveF(vp.persp, &perspNorm, camera->fieldOfView, OTRGetAspectRatio(), CM_GetProps()->NearPersp,
                       CM_GetProps()->FarPersp, 1.0f);
        guLookAtF(vp.view, camera->pos[0], camera->pos[1], camera->pos[2], camera->lookAt[0], camera->lookAt[1],
                  camera->lookAt[2], camera->up[0], camera->up[1], camera->up[2]);
        vp.xAdj = 1.0f;
    }
    auto viewport = GetInterpreter()->mGameWindowViewport;
    vp.vpX = (float) viewport.x;
    vp.vpY = (float) viewport.y;
    vp.vpW = (float) OTRGetGameViewportWidth();
    vp.vpH = (float) OTRGetGameViewportHeight();
    vp.valid = vp.vpW > 0.0f && vp.vpH > 0.0f;
    return vp;
}

static bool ProjectToScreen(ViewProjection& vp, const int16_t pos[3], float& px, float& py, float& clipW) {
    float world[4] = { (float) pos[0], (float) pos[1], (float) pos[2], 1.0f };
    FVector4 eye = MultiplyMatrixVector(vp.view, world);
    float eyeArr[4] = { eye.x, eye.y, eye.z, eye.w };
    FVector4 clip = MultiplyMatrixVector(vp.persp, eyeArr);
    if (clip.w < 0.001f) {
        return false;
    }
    float ndcX = clip.x * vp.xAdj / clip.w;
    float ndcY = clip.y / clip.w;
    px = vp.vpX + (ndcX * 0.5f + 0.5f) * vp.vpW;
    py = vp.vpY + (0.5f - ndcY * 0.5f) * vp.vpH;
    clipW = clip.w;
    return true;
}

static int32_t PickVertex(ViewProjection& vp, float mouseX, float mouseY, float radiusPx, bool selectedOnly = false,
                          float* outBestPx = nullptr) {
    if (!vp.valid || !sCatalog.Built()) {
        return -1;
    }
    const auto& refs = sCatalog.Vertices();
    float bestAny = 1e30f;
    float bestDist = radiusPx * radiusPx;
    float bestW = 1e30f;
    int32_t best = -1;
    Camera* cam = gScreenOneCtx->camera;
    float camPos[3] = { cam->pos[0], cam->pos[1], cam->pos[2] };
    for (size_t i = 0; i < refs.size(); i++) {
        if (selectedOnly && (int32_t) i != sSelected) {
            continue;
        }
        if (!RefVisible(refs[i], camPos)) {
            continue;
        }
        float px, py, cw;
        int16_t pos[3] = { refs[i].live->v.ob[0], refs[i].live->v.ob[1], refs[i].live->v.ob[2] };
        if (!ProjectToScreen(vp, pos, px, py, cw)) {
            continue;
        }
        float dx = px - mouseX;
        float dy = py - mouseY;
        float d2 = dx * dx + dy * dy;
        if (d2 < bestAny) {
            bestAny = d2;
        }
        if (d2 < bestDist || (d2 == bestDist && cw < bestW)) {
            bestDist = d2;
            bestW = cw;
            best = (int32_t) i;
        }
    }
    if (outBestPx != nullptr) {
        *outBestPx = (bestAny < 1e30f) ? std::sqrt(bestAny) : -1.0f;
    }
    return best;
}

// ---- selection / edits ----

static void RebuildMoveSet() {
    sMoveSet.clear();
    if (sSelected < 0) {
        return;
    }
    const auto& refs = sCatalog.Vertices();
    if (!sWeld) {
        sMoveSet.push_back((size_t) sSelected);
        return;
    }
    uint32_t group = refs[sSelected].weldGroup;
    for (size_t i = 0; i < refs.size(); i++) {
        if (refs[i].weldGroup == group) {
            sMoveSet.push_back(i);
        }
    }
}

static void ApplyWrite(const VertexWrite& w, bool redo) {
    Vtx* v = sCatalog.Vertices()[w.refIndex].live;
    const int16_t* p = redo ? w.after : w.before;
    v->v.ob[0] = p[0];
    v->v.ob[1] = p[1];
    v->v.ob[2] = p[2];
}

static void PushTransaction(Transaction&& txn) {
    bool changed = false;
    for (const auto& w : txn.writes) {
        if (w.before[0] != w.after[0] || w.before[1] != w.after[1] || w.before[2] != w.after[2]) {
            changed = true;
            break;
        }
    }
    if (!changed) {
        return;
    }
    sUndo.push_back(std::move(txn));
    sRedo.clear();
}

static int16_t ClampS16(float v) {
    return (int16_t) std::max(-32768.0f, std::min(32767.0f, std::roundf(v)));
}

void ClearSelection() {
    sSelected = -1;
    sMoveSet.clear();
}

SelectionInfo GetSelection() {
    SelectionInfo info;
    info.catalogVertices = sCatalog.Vertices().size();
    info.catalogLeaves = sCatalog.LeafCount();
    if (sSelected < 0 || !sCatalog.Built() || (size_t) sSelected >= sCatalog.Vertices().size()) {
        return info;
    }
    const VertexRef& ref = sCatalog.Vertices()[sSelected];
    info.valid = true;
    info.resName = ResourceGetNameByCrc(ref.resCrc);
    info.resCrc = ref.resCrc;
    info.index = ref.index;
    for (int i = 0; i < 3; i++) {
        info.livePos[i] = ref.live->v.ob[i];
        info.basePos[i] = ref.pos[i];
    }
    info.uv[0] = ref.live->v.tc[0];
    info.uv[1] = ref.live->v.tc[1];
    for (int i = 0; i < 4; i++) {
        info.color[i] = ((uint8_t*) ref.live->v.cn)[i];
    }
    int weldCount = 0;
    for (const auto& r : sCatalog.Vertices()) {
        if (r.weldGroup == ref.weldGroup) {
            weldCount++;
        }
    }
    info.weldCount = weldCount;
    info.drawnInRace = ref.drawnInRace;
    return info;
}

void CycleSelection() {
    if (sSelected < 0) {
        return;
    }
    const auto& refs = sCatalog.Vertices();
    uint32_t group = refs[sSelected].weldGroup;
    size_t n = refs.size();
    for (size_t step = 1; step <= n; step++) {
        size_t i = ((size_t) sSelected + step) % n;
        if (refs[i].weldGroup == group) {
            sSelected = (int32_t) i;
            break;
        }
    }
    RebuildMoveSet();
}

bool WeldEnabled() {
    return sWeld;
}
void SetWeldEnabled(bool enabled) {
    sWeld = enabled;
    RebuildMoveSet();
}
bool CloudEnabled() {
    return sCloud;
}
void SetCloudEnabled(bool enabled) {
    sCloud = enabled;
}
bool HideBackfacesEnabled() {
    return sHideBackfaces;
}
void SetHideBackfaces(bool enabled) {
    sHideBackfaces = enabled;
}
bool XrayEnabled() {
    return sXray;
}
void SetXray(bool enabled) {
    sXray = enabled;
}
int CourseView() {
    return sCourseView;
}
void SetCourseView(int view) {
    sCourseView = view;
}

static void MoveSelectionBy(const float delta[3]) {
    if (sSelected < 0 || sMoveSet.empty()) {
        return;
    }
    Transaction txn;
    for (size_t idx : sMoveSet) {
        Vtx* v = sCatalog.Vertices()[idx].live;
        VertexWrite w;
        w.refIndex = idx;
        for (int i = 0; i < 3; i++) {
            w.before[i] = v->v.ob[i];
            w.after[i] = ClampS16((float) v->v.ob[i] + delta[i]);
            v->v.ob[i] = w.after[i];
        }
        txn.writes.push_back(w);
    }
    PushTransaction(std::move(txn));
}

void Nudge(int axis, int amount) {
    float delta[3] = { 0, 0, 0 };
    delta[axis] = (float) amount;
    MoveSelectionBy(delta);
}

void SetAxisValue(int axis, int value) {
    if (sSelected < 0) {
        return;
    }
    int16_t cur = sCatalog.Vertices()[sSelected].live->v.ob[axis];
    float delta[3] = { 0, 0, 0 };
    delta[axis] = (float) (value - cur);
    MoveSelectionBy(delta);
}

bool CanUndo() {
    return !sUndo.empty();
}
bool CanRedo() {
    return !sRedo.empty();
}
size_t EditCount() {
    return sUndo.size();
}

void Undo() {
    if (sUndo.empty()) {
        return;
    }
    Transaction txn = std::move(sUndo.back());
    sUndo.pop_back();
    for (auto it = txn.writes.rbegin(); it != txn.writes.rend(); ++it) {
        ApplyWrite(*it, false);
    }
    sRedo.push_back(std::move(txn));
}

void Redo() {
    if (sRedo.empty()) {
        return;
    }
    Transaction txn = std::move(sRedo.back());
    sRedo.pop_back();
    for (const auto& w : txn.writes) {
        ApplyWrite(w, true);
    }
    sUndo.push_back(std::move(txn));
}


// ---- in-scene overlay (default mode) ----
// Markers and wireframe emitted as real z-tested geometry right after the
// merged course DL, so the depth buffer occludes them exactly like the track
// itself, and they render underneath all UI.

static std::vector<Vtx> sSceneVtx[2];
static std::vector<Gfx> sSceneDL[2];
static int sSceneParity = 0;

static void ScenePushQuad(std::vector<Vtx>& vtx, const float c[4][3], uint8_t r, uint8_t g, uint8_t b) {
    for (int i = 0; i < 4; i++) {
        Vtx v;
        v.v.ob[0] = (int16_t) c[i][0];
        v.v.ob[1] = (int16_t) c[i][1];
        v.v.ob[2] = (int16_t) c[i][2];
        v.v.flag = 0;
        v.v.tc[0] = 0;
        v.v.tc[1] = 0;
        v.v.cn[0] = r;
        v.v.cn[1] = g;
        v.v.cn[2] = b;
        v.v.cn[3] = 255;
        vtx.push_back(v);
    }
}

static void EmitSceneOverlay() {
    Camera* camera = gScreenOneCtx->camera;
    const auto& refs = sCatalog.Vertices();
    float camPos[3] = { camera->pos[0], camera->pos[1], camera->pos[2] };
    float fwd[3] = { camera->lookAt[0] - camPos[0], camera->lookAt[1] - camPos[1], camera->lookAt[2] - camPos[2] };
    float fl = std::sqrt(fwd[0] * fwd[0] + fwd[1] * fwd[1] + fwd[2] * fwd[2]);
    if (fl < 1e-5f) {
        return;
    }
    for (auto& c : fwd) {
        c /= fl;
    }
    float upw[3] = { camera->up[0], camera->up[1], camera->up[2] };
    float right[3] = { fwd[1] * upw[2] - fwd[2] * upw[1], fwd[2] * upw[0] - fwd[0] * upw[2],
                       fwd[0] * upw[1] - fwd[1] * upw[0] };
    float rl = std::sqrt(right[0] * right[0] + right[1] * right[1] + right[2] * right[2]);
    if (rl < 1e-5f) {
        return;
    }
    for (auto& c : right) {
        c /= rl;
    }
    float upv[3] = { right[1] * fwd[2] - right[2] * fwd[1], right[2] * fwd[0] - right[0] * fwd[2],
                     right[0] * fwd[1] - right[1] * fwd[0] };

    sSceneParity ^= 1;
    std::vector<Vtx>& vtx = sSceneVtx[sSceneParity];
    std::vector<Gfx>& dl = sSceneDL[sSceneParity];
    static std::vector<size_t> sDiscBases;
    vtx.clear();
    dl.clear();
    sDiscBases.clear();
    vtx.reserve(20000);

    ViewProjection vp = BuildViewProjection();
    ImVec2 mouse(0, 0);
    if (ImGui::GetCurrentContext() != nullptr) {
        mouse = ImGui::GetIO().MousePos;
    }
    sDebug.nearestPx = -1.0f;

    const float kEdgeMaxDist2 = 7000.0f * 7000.0f;
    const size_t kMaxQuads = 12000;
    size_t quads = 0;

    // Wireframe edges as thin camera-aligned ribbons.
    for (const auto& e : sCatalog.Edges()) {
        if (quads >= kMaxQuads) {
            break;
        }
        const VertexRef& a = refs[e.first];
        const VertexRef& b = refs[e.second];
        if (!RefVisible(a, camPos) && !RefVisible(b, camPos)) {
            continue;
        }
        float ax = a.live->v.ob[0], ay = a.live->v.ob[1], az = a.live->v.ob[2];
        float bx = b.live->v.ob[0], by = b.live->v.ob[1], bz = b.live->v.ob[2];
        float mx = (ax + bx) * 0.5f - camPos[0];
        float my = (ay + by) * 0.5f - camPos[1];
        float mz = (az + bz) * 0.5f - camPos[2];
        if (mx * mx + my * my + mz * mz > kEdgeMaxDist2) {
            continue;
        }
        float dir[3] = { bx - ax, by - ay, bz - az };
        float side[3] = { dir[1] * fwd[2] - dir[2] * fwd[1], dir[2] * fwd[0] - dir[0] * fwd[2],
                          dir[0] * fwd[1] - dir[1] * fwd[0] };
        float sl = std::sqrt(side[0] * side[0] + side[1] * side[1] + side[2] * side[2]);
        if (sl < 1e-4f) {
            continue;
        }
        float half = 0.9f;
        for (auto& c : side) {
            c = c / sl * half;
        }
        float corners[4][3] = { { ax - side[0], ay - side[1], az - side[2] },
                                { ax + side[0], ay + side[1], az + side[2] },
                                { bx + side[0], by + side[1], bz + side[2] },
                                { bx - side[0], by - side[1], bz - side[2] } };
        ScenePushQuad(vtx, corners, 0, 170, 220);
        quads++;
    }

    size_t edgeVerts = vtx.size();

    // Vertex dot billboards: cursor-local cloud plus the selection set.
    uint32_t selectedGroup = (sSelected >= 0) ? refs[sSelected].weldGroup : 0xFFFFFFFF;
    for (size_t i = 0; i < refs.size() && quads < kMaxQuads; i++) {
        const VertexRef& ref = refs[i];
        bool isSelected = (int32_t) i == sSelected;
        bool isWeldMate = !isSelected && ref.weldGroup == selectedGroup;
        bool nearCursor = false;
        if (!RefVisible(ref, camPos) && !isSelected && !isWeldMate) {
            continue;
        }
        float px, py, cw;
        int16_t pos[3] = { ref.live->v.ob[0], ref.live->v.ob[1], ref.live->v.ob[2] };
        if (vp.valid && ProjectToScreen(vp, pos, px, py, cw)) {
            float dx = px - mouse.x;
            float dy = py - mouse.y;
            float dist = std::sqrt(dx * dx + dy * dy);
            if (sDebug.nearestPx < 0.0f || dist < sDebug.nearestPx) {
                sDebug.nearestPx = dist;
            }
            nearCursor = sCloud && dist < 96.0f;
        }
        if (!isSelected && !isWeldMate && !nearCursor) {
            continue;
        }
        float radius = isSelected ? 5.0f : (isWeldMate ? 4.0f : 2.5f);
        uint8_t r = isSelected ? 255 : (isWeldMate ? 255 : 0);
        uint8_t g = isSelected ? 200 : (isWeldMate ? 120 : 220);
        uint8_t b = isSelected ? 0 : (isWeldMate ? 0 : 255);
        float cx = (float) pos[0], cy = (float) pos[1], cz = (float) pos[2];
        // Billboarded 12-segment disc, matching the X-ray circles.
        size_t discBase = vtx.size();
        Vtx center;
        center.v.ob[0] = (int16_t) cx;
        center.v.ob[1] = (int16_t) cy;
        center.v.ob[2] = (int16_t) cz;
        center.v.flag = 0;
        center.v.tc[0] = 0;
        center.v.tc[1] = 0;
        center.v.cn[0] = r;
        center.v.cn[1] = g;
        center.v.cn[2] = b;
        center.v.cn[3] = 255;
        vtx.push_back(center);
        for (int seg = 0; seg < 12; seg++) {
            float ang = (float) seg * (2.0f * (float) M_PI / 12.0f);
            float ca = std::cos(ang) * radius;
            float sa = std::sin(ang) * radius;
            Vtx v = center;
            v.v.ob[0] = (int16_t) (cx + right[0] * ca + upv[0] * sa);
            v.v.ob[1] = (int16_t) (cy + right[1] * ca + upv[1] * sa);
            v.v.ob[2] = (int16_t) (cz + right[2] * ca + upv[2] * sa);
            vtx.push_back(v);
        }
        sDiscBases.push_back(discBase);
        quads++; // budget bookkeeping
    }

    if (quads == 0) {
        return;
    }

    // State block mirrors the collision viewer's proven z-tested setup.
    Mat4 matrix;
    gSPTexture(gDisplayListHead++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_OFF);
    gSPSetGeometryMode(gDisplayListHead++, G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH);
    gSPClearGeometryMode(gDisplayListHead++, G_CULL_BACK);
    gDPPipeSync(gDisplayListHead++);
    gDPSetOtherMode(gDisplayListHead++,
                    G_AD_NOTPATTERN | G_CD_MAGICSQ | G_CK_NONE | G_TC_FILT | G_TF_BILERP | G_TT_NONE | G_TL_TILE |
                        G_TD_CLAMP | G_TP_PERSP | G_CYC_1CYCLE | G_PM_NPRIMITIVE,
                    G_AC_NONE | G_ZS_PIXEL | G_RM_AA_ZB_OPA_SURF | G_RM_AA_ZB_OPA_SURF2);
    gDPSetCombineMode(gDisplayListHead++, G_CC_SHADE, G_CC_SHADE);
    mtxf_identity(matrix);
    AddObjectMatrix(matrix, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);

    for (size_t base = 0; base < edgeVerts; base += 32) {
        size_t n = std::min<size_t>(32, edgeVerts - base);
        gSPVertex(gDisplayListHead++, (uintptr_t) &vtx[base], n, 0);
        for (size_t q = 0; q + 3 < n; q += 4) {
            gSP2Triangles(gDisplayListHead++, q, q + 1, q + 2, 0, q, q + 2, q + 3, 0);
        }
    }
    for (size_t base : sDiscBases) {
        gSPVertex(gDisplayListHead++, (uintptr_t) &vtx[base], 13, 0);
        for (int i = 1; i <= 11; i += 2) {
            int j = (i % 12) + 1;
            int k = ((i + 1) % 12) + 1;
            gSP2Triangles(gDisplayListHead++, 0, i, j, 0, 0, j, k, 0);
        }
    }
    gSPSetGeometryMode(gDisplayListHead++, G_CULL_BACK);
}

// ---- TEMP frame dump diagnostic ----

static bool sDumpRequested = false;
static int sDumpCounter = 0;

void RequestFrameDump() {
    sDumpRequested = true;
}

static void DumpGfx(FILE* f, Gfx* ptr, int depth) {
    if (ptr == nullptr || depth > 12) {
        return;
    }
    for (int guard = 0; guard < (1 << 20); guard++) {
        uintptr_t w0 = ptr->words.w0;
        uintptr_t w1 = ptr->words.w1;
        uint8_t op = (uint8_t) ((w0 >> 24) & 0xFF);
        fprintf(f, "%*s%02X %08X %08X", depth * 2, "", op, (uint32_t) w0, (uint32_t) w1);
        if (op == 0x31 || op == 0x32 || op == 0x33 || op == 0x20 || op == 0x35 || op == 0x36 || op == 0x42) {
            uint64_t hash = ((uint64_t) ptr[1].words.w0 << 32) + ptr[1].words.w1;
            const char* n = ResourceGetNameByCrc(hash);
            fprintf(f, "  %s", n != nullptr ? n : "(unresolved)");
        }
        fprintf(f, "\n");
        if (op == (uint8_t) (G_ENDDL & 0xFF)) {
            return;
        }
        if (op == 0x31) {
            uint64_t hash = ((uint64_t) ptr[1].words.w0 << 32) + ptr[1].words.w1;
            DumpGfx(f, (Gfx*) ResourceGetDataByCrc(hash), depth + 1);
            ptr += 2;
            continue;
        }
        if (op == (uint8_t) (G_DL & 0xFF)) {
            if (w1 >= 0x10000 && w1 < ((uintptr_t) 1 << 48)) {
                DumpGfx(f, (Gfx*) w1, depth + 1);
            }
            if (((w0 >> 16) & 1) != 0) {
                return;
            }
            ptr += 1;
            continue;
        }
        switch (op) {
            case 0x20: case 0x24: case 0x32: case 0x33: case 0x35: case 0x36:
            case 0x38: case 0x3E: case 0x3F: case 0x42: case 0x47:
                ptr += 2; break;
            case 0x37: case 0x3C: case 0x45: case 0xE4: case 0xE5:
                ptr += 3; break;
            case 0x4A:
                ptr += 5; break;
            default:
                ptr += 1; break;
        }
    }
}

// ---- drag ----

static bool MouseRayPlaneHit(float outHit[3]) {
    Camera* camera = gScreenOneCtx->camera;
    FVector dir = ScreenRayTrace();
    float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    if (len < 1e-6f) {
        return false;
    }
    float d[3] = { dir.x / len, dir.y / len, dir.z / len };
    float n[3] = { camera->lookAt[0] - camera->pos[0], camera->lookAt[1] - camera->pos[1],
                   camera->lookAt[2] - camera->pos[2] };
    float nl = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (nl < 1e-6f) {
        return false;
    }
    for (auto& c : n) {
        c /= nl;
    }
    const VertexRef& ref = sCatalog.Vertices()[sSelected];
    float plane[3] = { sDragStartHit[0], sDragStartHit[1], sDragStartHit[2] };
    if (!sDragging) {
        plane[0] = (float) ref.live->v.ob[0];
        plane[1] = (float) ref.live->v.ob[1];
        plane[2] = (float) ref.live->v.ob[2];
    }
    float denom = d[0] * n[0] + d[1] * n[1] + d[2] * n[2];
    if (std::fabs(denom) < 1e-6f) {
        return false;
    }
    float diff[3] = { plane[0] - camera->pos[0], plane[1] - camera->pos[1], plane[2] - camera->pos[2] };
    float t = (diff[0] * n[0] + diff[1] * n[1] + diff[2] * n[2]) / denom;
    outHit[0] = camera->pos[0] + d[0] * t;
    outHit[1] = camera->pos[1] + d[1] * t;
    outHit[2] = camera->pos[2] + d[2] * t;
    return true;
}

static void BeginDrag() {
    sDragTxn.writes.clear();
    for (size_t idx : sMoveSet) {
        Vtx* v = sCatalog.Vertices()[idx].live;
        VertexWrite w;
        w.refIndex = idx;
        for (int i = 0; i < 3; i++) {
            w.before[i] = v->v.ob[i];
            w.after[i] = v->v.ob[i];
        }
        sDragTxn.writes.push_back(w);
    }
    float hit[3];
    if (MouseRayPlaneHit(hit)) {
        sDragStartHit[0] = hit[0];
        sDragStartHit[1] = hit[1];
        sDragStartHit[2] = hit[2];
        sDragging = true;
    }
}

static void UpdateDrag() {
    float hit[3];
    if (!MouseRayPlaneHit(hit)) {
        return;
    }
    float delta[3] = { hit[0] - sDragStartHit[0], hit[1] - sDragStartHit[1], hit[2] - sDragStartHit[2] };
    for (auto& w : sDragTxn.writes) {
        Vtx* v = sCatalog.Vertices()[w.refIndex].live;
        for (int i = 0; i < 3; i++) {
            w.after[i] = ClampS16((float) w.before[i] + delta[i]);
            v->v.ob[i] = w.after[i];
        }
    }
}

static void CommitDrag() {
    sDragging = false;
    PushTransaction(std::move(sDragTxn));
    sDragTxn = Transaction{};
}

} // namespace GeometryEditor

// ---- C bridge ----

extern "C" void GeometryMode_DumpFrame(void* pool) {
    using namespace GeometryEditor;
    if (!sDumpRequested || pool == nullptr) {
        return;
    }
    sDumpRequested = false;
    char path[512];
    snprintf(path, sizeof(path),
             "/Users/Andy/Documents/GitHub/Repo-Clones/SpaghettiKart-geometry/Claude Test Builds/framedump_%d.txt",
             sDumpCounter++);
    FILE* f = fopen(path, "w");
    if (f == nullptr) {
        return;
    }
    fprintf(f, "# frame dump, freecam=%d gamestate=%d\n", CVarGetInteger("gFreecam", 0), gGamestate);
    DumpGfx(f, (Gfx*) pool, 0);
    fclose(f);
    printf("[GeometryMode] frame dumped to %s\n", path);
}

extern "C" int32_t GeometryMode_IsActive(void) {
    return GeometryEditor::ModeActive() ? 1 : 0;
}

extern "C" void GeometryMode_OnTrackSectionsParsed(void* sections) {
    using namespace GeometryEditor;

    // Roll back any outstanding edits from the previous session with this
    // catalog: resources are process-cached, so without this, moved vertices
    // would silently survive into the freshly loaded course.
    if (sDragging) {
        for (const auto& w : sDragTxn.writes) {
            ApplyWrite(w, false);
        }
        sDragging = false;
        sDragTxn = Transaction{};
    }
    for (auto it = sUndo.rbegin(); it != sUndo.rend(); ++it) {
        for (auto wit = it->writes.rbegin(); wit != it->writes.rend(); ++wit) {
            ApplyWrite(*wit, false);
        }
    }
    sUndo.clear();
    sRedo.clear();

    TrackSections* section = (TrackSections*) sections;
    size_t maxSection = 0;
    while (section != nullptr && section->crc != 0) {
        if (section->sectionId > maxSection) {
            maxSection = section->sectionId;
        }
        section++;
    }
    Track* track = GetWorld()->GetTrack();
    size_t tableLimit = SectionTableLimit(track);
    sSectionCount = (maxSection < tableLimit) ? maxSection : tableLimit;
    sCatalogTrack = (const void*) track;
    sPrimaryTable = nullptr;
    sCatalog.Reset();
    ClearSelection();
}

static int32_t sTrackPassMarked = 0;

extern "C" void GeometryMode_MarkTrackPass(int32_t inPass) {
    sTrackPassMarked = inPass;
}

extern "C" int32_t GeometryMode_StockFreecamDraw(void) {
    using namespace GeometryEditor;
    if (!sTrackPassMarked || gGamestate != RACING) {
        return 0;
    }
    Track* track = GetWorld()->GetTrack();
    if (track == nullptr) {
        return 0;
    }
    // One-shot per pass: never fires for the transparency-pass calls into
    // render_track_sections (Koopa Beach, Sherbet Land secondary tables).
    sTrackPassMarked = 0;
    track->DrawCredits();
    return 1;
}

extern "C" int32_t GeometryMode_DrawFullTrack(const char* const addr[], int16_t direction) {
    using namespace GeometryEditor;
    if (!ModeActive() || sSectionCount == 0 || sCatalog.Failed()) {
        return 0;
    }
    // The first table rendered after a course parse is the primary one;
    // secondary tables (transparency passes) keep the game's section path.
    // Never latch while the collision-mesh view suppresses the opaque pass.
    if (sPrimaryTable == nullptr) {
        if (CVarGetInteger("gRenderCollisionMesh", 0)) {
            return 0;
        }
        sPrimaryTable = addr;
    }
    if (addr != sPrimaryTable) {
        return 0;
    }
    if (!sCatalog.Built()) {
        const char* creditsPath = CreditsRootFor(GetWorld()->GetTrack());
        Gfx* creditsRoot = (creditsPath != nullptr) ? (Gfx*) ResourceGetDataByName(creditsPath) : nullptr;
        sCatalog.Build(addr, sSectionCount, creditsRoot);
        if (sCatalog.Failed()) {
            return 0;
        }
        printf("[GeometryMode] catalog: %zu sections, %zu leaves, %zu vertices, %zu weld groups\n", sSectionCount,
               sCatalog.LeafCount(), sCatalog.Vertices().size(), sCatalog.WeldGroupCount());
    }
    if ((size_t) (direction & 3) != (size_t) direction) {
        return 0;
    }
    if (sCourseView == 1) {
        // Credits assembly, with the race-union overlay kept on top: any
        // mismatch shows as wireframe without a surface (race-only piece)
        // or surface without wireframe (credits-only piece).
        GetWorld()->GetTrack()->DrawCredits();
    } else {
        gSPDisplayList(gDisplayListHead++, (Gfx*) sCatalog.MergedDL(direction).data());
    }
    if (!sXray) {
        EmitSceneOverlay();
    }
    return 1;
}

extern "C" void GeometryMode_Tick(void) {
    using namespace GeometryEditor;

    static bool wasMouseDown = false;
    static bool moved = false;
    static Ship::Coords startPos = { 0, 0 };

    sDebug.tickCount++;

    if (!ModeActive() || !sCatalog.Built() || gScreenOneCtx == nullptr || gScreenOneCtx->camera == nullptr) {
        sDragging = false;
        wasMouseDown = false;
        return;
    }

    // ImGui's input state is the one mouse path proven working on every
    // backend here (all UI interaction runs on it); the window-manager mouse
    // API is not exercised anywhere verified on macOS Metal.
    ImVec2 mousePos = ImGui::GetIO().MousePos;
    Ship::Coords mouse = { (int32_t) mousePos.x, (int32_t) mousePos.y };
    bool isMouseDown = ImGui::GetIO().MouseDown[0];
    sDebug.mouseDown = isMouseDown;

    bool uiMouse = GeometryEditor::PointerOverUI((float) mouse.x, (float) mouse.y);

    if (isMouseDown && !wasMouseDown) {
        startPos = mouse;
        moved = false;
        sDragCandidate = false;
        if (!uiMouse && MouseInViewport(mouse.x, mouse.y) && sSelected >= 0) {
            ViewProjection vp = BuildViewProjection();
            sDragCandidate = PickVertex(vp, (float) mouse.x, (float) mouse.y, 14.0f, true) == sSelected;
        }
    }

    if (isMouseDown && wasMouseDown) {
        int dx = mouse.x - startPos.x;
        int dy = mouse.y - startPos.y;
        if (!moved && (dx * dx + dy * dy) > 25) {
            moved = true;
            if (sDragCandidate) {
                BeginDrag();
            }
        }
        if (sDragging) {
            UpdateDrag();
        }
    }

    if (!isMouseDown && wasMouseDown) {
        if (sDragging) {
            CommitDrag();
        } else if (!moved) {
            if (uiMouse) {
                snprintf(sDebug.lastClick, sizeof(sDebug.lastClick), "blocked: over UI");
            } else if (!MouseInViewport(mouse.x, mouse.y)) {
                snprintf(sDebug.lastClick, sizeof(sDebug.lastClick), "blocked: outside view");
            } else {
                ViewProjection vp = BuildViewProjection();
                float bestPx = -1.0f;
                int32_t hit = PickVertex(vp, (float) mouse.x, (float) mouse.y, 18.0f, false, &bestPx);
                if (hit >= 0) {
                    snprintf(sDebug.lastClick, sizeof(sDebug.lastClick), "hit #%d (%.0f px)", hit, bestPx);
                } else {
                    snprintf(sDebug.lastClick, sizeof(sDebug.lastClick), "miss (nearest %.0f px)", bestPx);
                }
                sSelected = hit;
                RebuildMoveSet();
            }
        }
    }

    wasMouseDown = isMouseDown;

    // Keep the overlay alive even when no editor panel is on screen.
    GeometryEditor::DrawOverlay();
}

namespace GeometryEditor {

static int sLastOverlayFrame = -1;

void DrawOverlay() {
    if (!ModeActive() || !sCatalog.Built()) {
        return;
    }
    if (!sXray) {
        // Native mode renders in-scene; only refresh the input diagnostics.
        ImVec2 m = ImGui::GetIO().MousePos;
        sDebug.mouseX = m.x;
        sDebug.mouseY = m.y;
        auto vprect = GetInterpreter()->mGameWindowViewport;
        sDebug.vpX = vprect.x;
        sDebug.vpY = vprect.y;
        sDebug.vpW = (int) OTRGetGameViewportWidth();
        sDebug.vpH = (int) OTRGetGameViewportHeight();
        sDebug.inViewport = MouseInViewport((int) m.x, (int) m.y);
        sDebug.overUI = PointerOverUI(m.x, m.y);
        return;
    }
    // Runs from both the UI panels and the input tick; draw once per frame,
    // and only while ImGui is inside a frame.
    ImGuiContext* ctx = ImGui::GetCurrentContext();
    if (ctx == nullptr || !ctx->WithinFrameScope || sLastOverlayFrame == ImGui::GetFrameCount()) {
        return;
    }
    sLastOverlayFrame = ImGui::GetFrameCount();
    ViewProjection vp = BuildViewProjection();
    if (!vp.valid) {
        return;
    }
    // Draw into the game view's own window list so markers stay underneath
    // the editor windows and menu; foreground only as a fallback.
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    ImGuiWindow* gameWindow = ImGui::FindWindowByName("Main Game");
    if (gameWindow != nullptr && gameWindow->Active) {
        draw = gameWindow->DrawList;
    }
    ImVec2 mouse = ImGui::GetIO().MousePos;
    const auto& refs = sCatalog.Vertices();
    Camera* overlayCam = gScreenOneCtx->camera;
    float camPos[3] = { overlayCam->pos[0], overlayCam->pos[1], overlayCam->pos[2] };

    auto viewport = GetInterpreter()->mGameWindowViewport;
    sDebug.mouseX = mouse.x;
    sDebug.mouseY = mouse.y;
    sDebug.vpX = viewport.x;
    sDebug.vpY = viewport.y;
    sDebug.vpW = (int) OTRGetGameViewportWidth();
    sDebug.vpH = (int) OTRGetGameViewportHeight();
    sDebug.inViewport = MouseInViewport((int) mouse.x, (int) mouse.y);
    sDebug.overUI = PointerOverUI(mouse.x, mouse.y);
    sDebug.nearestPx = -1.0f;

    // One projection pass shared by dots and wireframe edges.
    struct ScreenPt {
        float x, y, w;
        bool valid;
        float mouseDist;
    };
    draw->PushClipRect(ImVec2((float) sDebug.vpX, (float) sDebug.vpY),
                       ImVec2((float) (sDebug.vpX + sDebug.vpW), (float) (sDebug.vpY + sDebug.vpH)), false);
    static std::vector<ScreenPt> sScreen;
    sScreen.assign(refs.size(), ScreenPt{ 0, 0, 0, false, 0 });
    for (size_t i = 0; i < refs.size(); i++) {
        int16_t pos[3] = { refs[i].live->v.ob[0], refs[i].live->v.ob[1], refs[i].live->v.ob[2] };
        ScreenPt& sp = sScreen[i];
        sp.valid = ProjectToScreen(vp, pos, sp.x, sp.y, sp.w) && RefVisible(refs[i], camPos);
        if (!sp.valid) {
            continue;
        }
        float dx = sp.x - mouse.x;
        float dy = sp.y - mouse.y;
        sp.mouseDist = std::sqrt(dx * dx + dy * dy);
        if (sDebug.nearestPx < 0.0f || sp.mouseDist < sDebug.nearestPx) {
            sDebug.nearestPx = sp.mouseDist;
        }
    }

    const float cloudRadius = 96.0f;

    // Wireframe context: always on while the tool is active, fading with
    // distance so far geometry reads as texture rather than noise.
    for (const auto& e : sCatalog.Edges()) {
        const ScreenPt& a = sScreen[e.first];
        const ScreenPt& b = sScreen[e.second];
        if (!a.valid || !b.valid) {
            continue;
        }
        float avgW = 0.5f * (a.w + b.w);
        int alpha = 26 + (int) std::min(64.0f, 3000.0f / std::max(avgW, 1.0f));
        draw->AddLine(ImVec2(a.x, a.y), ImVec2(b.x, b.y), IM_COL32(0, 200, 255, alpha), 1.0f);
    }

    uint32_t selectedGroup = (sSelected >= 0) ? refs[sSelected].weldGroup : 0xFFFFFFFF;

    for (size_t i = 0; i < refs.size(); i++) {
        const ScreenPt& sp = sScreen[i];
        if (!sp.valid) {
            continue;
        }
        bool isSelected = (int32_t) i == sSelected;
        bool isWeldMate = !isSelected && refs[i].weldGroup == selectedGroup;
        // Perspective size cue: nearer vertices get bigger markers.
        float depthR = std::min(5.0f, std::max(1.2f, 1400.0f / std::max(sp.w, 1.0f)));
        if (isSelected) {
            draw->AddCircleFilled(ImVec2(sp.x, sp.y), depthR + 3.0f, IM_COL32(255, 200, 0, 255));
            draw->AddCircle(ImVec2(sp.x, sp.y), depthR + 6.0f, IM_COL32(255, 200, 0, 200), 0, 2.0f);
        } else if (isWeldMate) {
            draw->AddCircle(ImVec2(sp.x, sp.y), depthR + 4.0f, IM_COL32(255, 120, 0, 220), 0, 2.0f);
        } else if (sCloud && sp.mouseDist < cloudRadius) {
            float alpha = std::min(1.0f, 2200.0f / std::max(sp.w, 1.0f));
            draw->AddCircleFilled(ImVec2(sp.x, sp.y), depthR, IM_COL32(0, 220, 255, (int) (80.0f + 175.0f * alpha)));
        }
    }
    draw->PopClipRect();
}

} // namespace GeometryEditor
