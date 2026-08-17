#include "GeometryMode.h"
#include "GeometryCatalog.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include <imgui.h>

#include "engine/World.h"
#include "engine/tracks/Track.h"
#include "engine/editor/EditorMath.h"
#include "port/Engine.h"
#include <fast/interpreter.h>

extern "C" {
#include "main.h"
#include "code_800029B0.h"
#include "defines.h"
}

namespace GeometryEditor {

static GeometryCatalog sCatalog;
static const char* const* sPrimaryTable = nullptr;
static size_t sSectionCount = 0;
static const void* sCatalogTrack = nullptr; // Track* the catalog belongs to

// Visibility-table row counts per course, from the course_offsets arrays
// (entries / 4). The collision sections list can disagree with the table, so
// the table length is the binding limit. Courses not listed (battle arenas,
// ceremony, custom tracks) have no table and the mode stays off.
struct CourseSections {
    const char* resourceName;
    size_t sections;
};
static const CourseSections kCourseSections[] = {
    { "mk:mario_raceway", 17 },   { "mk:choco_mountain", 24 }, { "mk:bowsers_castle", 27 },
    { "mk:banshee_boardwalk", 25 }, { "mk:yoshi_valley", 31 },   { "mk:frappe_snowland", 17 },
    { "mk:koopa_troopa_beach", 37 }, { "mk:royal_raceway", 33 },  { "mk:luigi_raceway", 30 },
    { "mk:moo_moo_farm", 23 },    { "mk:toads_turnpike", 20 },  { "mk:kalimari_desert", 20 },
    { "mk:sherbet_land", 18 },    { "mk:rainbow_road", 12 },    { "mk:wario_stadium", 27 },
    { "mk:dk_jungle", 26 },
};

static size_t SectionTableLimit(Track* track) {
    if (track == nullptr) {
        return 0;
    }
    for (const auto& c : kCourseSections) {
        if (track->ResourceName == c.resourceName) {
            return c.sections;
        }
    }
    return 0;
}

// Selection / edit state
static int32_t sSelected = -1;             // index into sCatalog.Vertices()
static std::vector<size_t> sMoveSet;       // selected + weld members (catalog indices)
static bool sWeld = true;
static bool sCloud = true;

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
    if (!CVarGetInteger("gGeometryEditorWindow", 0)) {
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

// ---- projection helpers (mirrors ScreenRayTrace's proven conventions) ----

struct ViewProjection {
    bool valid = false;
    Mat4 view;
    Mat4 persp;
    float vpX, vpY, vpW, vpH;
};

static ViewProjection BuildViewProjection() {
    ViewProjection vp;
    if (gScreenOneCtx == nullptr || gScreenOneCtx->camera == nullptr) {
        return vp;
    }
    Camera* camera = gScreenOneCtx->camera;
    u16 perspNorm;
    guPerspectiveF(vp.persp, &perspNorm, camera->fieldOfView, OTRGetAspectRatio(), CM_GetProps()->NearPersp,
                   CM_GetProps()->FarPersp, 1.0f);
    guLookAtF(vp.view, camera->pos[0], camera->pos[1], camera->pos[2], camera->lookAt[0], camera->lookAt[1],
              camera->lookAt[2], camera->up[0], camera->up[1], camera->up[2]);
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
    float ndcX = clip.x / clip.w;
    float ndcY = clip.y / clip.w;
    px = vp.vpX + (ndcX * 0.5f + 0.5f) * vp.vpW;
    py = vp.vpY + (0.5f - ndcY * 0.5f) * vp.vpH;
    clipW = clip.w;
    return true;
}

static int32_t PickVertex(ViewProjection& vp, float mouseX, float mouseY, float radiusPx, bool selectedOnly = false) {
    if (!vp.valid || !sCatalog.Built()) {
        return -1;
    }
    const auto& refs = sCatalog.Vertices();
    float bestDist = radiusPx * radiusPx;
    float bestW = 1e30f;
    int32_t best = -1;
    for (size_t i = 0; i < refs.size(); i++) {
        if (selectedOnly && (int32_t) i != sSelected) {
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
        if (d2 < bestDist || (d2 == bestDist && cw < bestW)) {
            bestDist = d2;
            bestW = cw;
            best = (int32_t) i;
        }
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
        sCatalog.Build(addr, sSectionCount);
        if (sCatalog.Failed()) {
            return 0;
        }
        printf("[GeometryMode] catalog: %zu sections, %zu leaves, %zu vertices, %zu weld groups\n", sSectionCount,
               sCatalog.LeafCount(), sCatalog.Vertices().size(), sCatalog.WeldGroupCount());
    }
    if ((size_t) (direction & 3) != (size_t) direction) {
        return 0;
    }
    gSPDisplayList(gDisplayListHead++, (Gfx*) sCatalog.MergedDL(direction).data());
    return 1;
}

extern "C" void GeometryMode_Tick(void) {
    using namespace GeometryEditor;

    static bool wasMouseDown = false;
    static bool moved = false;
    static Ship::Coords startPos = { 0, 0 };

    if (!ModeActive() || !sCatalog.Built() || gScreenOneCtx == nullptr || gScreenOneCtx->camera == nullptr) {
        sDragging = false;
        wasMouseDown = false;
        return;
    }

    auto wnd = GameEngine::Instance->context->GetWindow();
    Ship::Coords mouse = wnd->GetMousePos();
    bool isMouseDown = wnd->GetMouseState(Ship::LUS_MOUSE_BTN_LEFT);

    bool uiMouse = ImGui::GetIO().WantCaptureMouse;

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
        } else if (!moved && !uiMouse && MouseInViewport(mouse.x, mouse.y)) {
            ViewProjection vp = BuildViewProjection();
            int32_t hit = PickVertex(vp, (float) mouse.x, (float) mouse.y, 12.0f);
            sSelected = hit;
            RebuildMoveSet();
        }
    }

    wasMouseDown = isMouseDown;
}

namespace GeometryEditor {

void DrawOverlay() {
    if (!ModeActive() || !sCatalog.Built()) {
        return;
    }
    ViewProjection vp = BuildViewProjection();
    if (!vp.valid) {
        return;
    }
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    // Same mouse source and coordinate basis as Tick's picking.
    Ship::Coords mouseCoords = GameEngine::Instance->context->GetWindow()->GetMousePos();
    ImVec2 mouse((float) mouseCoords.x, (float) mouseCoords.y);
    const auto& refs = sCatalog.Vertices();

    uint32_t selectedGroup = (sSelected >= 0) ? refs[sSelected].weldGroup : 0xFFFFFFFF;

    for (size_t i = 0; i < refs.size(); i++) {
        float px, py, cw;
        int16_t pos[3] = { refs[i].live->v.ob[0], refs[i].live->v.ob[1], refs[i].live->v.ob[2] };
        if (!ProjectToScreen(vp, pos, px, py, cw)) {
            continue;
        }
        bool isSelected = (int32_t) i == sSelected;
        bool isWeldMate = !isSelected && refs[i].weldGroup == selectedGroup;
        if (isSelected) {
            draw->AddCircleFilled(ImVec2(px, py), 5.0f, IM_COL32(255, 200, 0, 255));
            draw->AddCircle(ImVec2(px, py), 8.0f, IM_COL32(255, 200, 0, 200), 0, 2.0f);
        } else if (isWeldMate) {
            draw->AddCircle(ImVec2(px, py), 6.0f, IM_COL32(255, 120, 0, 220), 0, 2.0f);
        } else if (sCloud) {
            float dx = px - mouse.x;
            float dy = py - mouse.y;
            if (dx * dx + dy * dy < 96.0f * 96.0f) {
                float alpha = std::min(1.0f, 2200.0f / std::max(cw, 1.0f));
                draw->AddCircleFilled(ImVec2(px, py), 2.0f,
                                      IM_COL32(0, 220, 255, (int) (80.0f + 175.0f * alpha)));
            }
        }
    }
}

} // namespace GeometryEditor
