#include "GeometryEditorWindow.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <cmath>
#include <typeinfo>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "port/ui/PortMenu.h"
#include "port/ui/UIWidgets.h"
#include "port/Game.h"
#include "engine/World.h"
#include "engine/tracks/Track.h"
#include "engine/editor/geometry/GeometryMode.h"

extern "C" {
#include "code_800029B0.h"
#include "main.h"
#include "defines.h"
#include "audio/external.h"
#include "camera.h"
}

namespace GeometryEditor {

static const char* ShortResName(const char* full) {
    if (full == nullptr) {
        return "(unknown resource)";
    }
    const char* slash = strrchr(full, '/');
    return (slash != nullptr) ? slash + 1 : full;
}

static void DrawNudgeRow(const char* label, int axis) {
    static const int kAmounts[6] = { -100, -10, -1, 1, 10, 100 };
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::PushID(label);
    for (int i = 0; i < 6; i++) {
        ImGui::TableNextColumn();
        char text[8];
        snprintf(text, sizeof(text), "%+d", kAmounts[i]);
        if (ImGui::Button(text, ImVec2(-1.0f, 0.0f))) {
            Nudge(axis, kAmounts[i]);
        }
    }
    ImGui::PopID();
}

void GeometryEditorWindow::DrawElement() {
    DrawOverlay();

    // The menu framework also renders this window's element embedded in
    // the Developer > Geometry tab, which duplicated the whole interface
    // there. The editor lives ONLY in the popped-out window; the tab keeps
    // just its own buttons.
    if (strncmp(ImGui::GetCurrentWindowRead()->Name, "Geometry Editor", 15) != 0) {
        ImGui::TextDisabled("Use the Popout button above to open the editor.");
        return;
    }

    Track* track = GetWorld()->GetTrack();
    // The world keeps the last track object alive on the menus; only trust it
    // during an actual race.
    if (track == nullptr || gGamestate != RACING) {
        ImGui::Text("Course: not loaded");
        ImGui::TextWrapped("Start a race on one of the 16 GP courses to edit its geometry.");
        return;
    }
    if (track->IsMod()) {
        ImGui::TextWrapped("Custom tracks are not supported. Load one of the 20 original courses.");
        return;
    }
    if (gIsMirrorMode != 0) {
        ImGui::TextWrapped("Mirror mode is not supported. Turn it off and reload the course.");
        return;
    }

    SelectionInfo info = GetSelection();

    ImGui::Text("Course: %s", track->Props.Name);
    if (info.catalogVertices == 0) {
        ImGui::TextWrapped("Editor inactive: no vertex catalog for this course yet. Start (or restart) a race on one "
                           "of the 16 GP courses with the editor enabled.");
        return;
    }
    ImGui::TextDisabled("%zu vertices in %zu display lists", info.catalogVertices, info.catalogLeaves);
    DebugStatus dbg = GetDebugStatus();
    ImGui::TextDisabled("dbg: tick %u btn %s | mouse %.0f,%.0f | view %d,%d %dx%d %s | nearest %.0f px | %s",
                        dbg.tickCount, dbg.mouseDown ? "DOWN" : "up", dbg.mouseX, dbg.mouseY, dbg.vpX, dbg.vpY,
                        dbg.vpW, dbg.vpH, dbg.inViewport ? "in" : "OUT", dbg.nearestPx,
                        dbg.overUI ? "clicks blocked: over UI" : "clicks live");
    ImGui::TextDisabled("last click: %s", dbg.lastClick);
    ImGui::SameLine();
    if (ImGui::SmallButton("Dump frame")) {
        RequestFrameDump();
    }

    bool freecam = CVarGetInteger("gFreecam", 0);
    if (ImGui::Button(freecam ? "Switch to Kart Camera" : "Switch to Freecam")) {
        CVarSetInteger("gFreecam", !freecam);
        CM_SetFreeCamera(!freecam);
    }
    ImGui::SameLine();
    bool cloud = CloudEnabled();
    if (ImGui::Checkbox("Vertex dots near cursor", &cloud)) {
        SetCloudEnabled(cloud);
    }
    ImGui::SameLine();
    bool hideBack = HideBackfacesEnabled();
    if (ImGui::Checkbox("Hide back-facing", &hideBack)) {
        SetHideBackfaces(hideBack);
    }
    ImGui::SameLine();
    bool xray = XrayEnabled();
    if (ImGui::Checkbox("X-ray", &xray)) {
        SetXray(xray);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Show vertices and wireframe through geometry (screen overlay) instead of depth-tested in-scene rendering");
    }
    int view = CourseView();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Course assembly:");
    ImGui::SameLine();
    if (ImGui::RadioButton("Race", view == 0)) {
        SetCourseView(0);
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Credits", view == 1)) {
        SetCourseView(1);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Credits (default) = the authored one-of-each course; stable, and what the wireframe and "
                          "vertices now follow.\nRace = the raw union of the racing section lists; detail variants "
                          "overlap and shimmer there, which makes it a diagnostic view for spotting them.");
    }

    ImGui::Separator();

    if (!ImGui::GetIO().WantTextInput &&
        (ImGui::GetIO().KeySuper || ImGui::GetIO().KeyCtrl) && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
        if (ImGui::GetIO().KeyShift) {
            Redo();
        } else {
            Undo();
        }
    }

    if (!info.valid) {
        ImGui::TextWrapped("Click a vertex in the game view to select it.");
        return;
    }

    ImGui::Text("Vertex: %s [%u]", ShortResName(info.resName), info.index);
    if (info.drawnInRace) {
        ImGui::TextDisabled("Drawn in: races + credits view");
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f),
                           "Credits view only: real races draw another version of this surface");
    }
    if (info.resName != nullptr && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", info.resName);
    }

    bool weld = WeldEnabled();
    if (ImGui::Checkbox("Weld", &weld)) {
        SetWeldEnabled(weld);
    }
    ImGui::SameLine();
    ImGui::Text("Co-located: %d", info.weldCount);
    if (info.weldCount > 1) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Cycle")) {
            CycleSelection();
        }
    }

    static const char* kAxisNames[3] = { "X", "Y", "Z" };
    int pos[3] = { info.livePos[0], info.livePos[1], info.livePos[2] };
    for (int axis = 0; axis < 3; axis++) {
        ImGui::PushID(axis);
        ImGui::SetNextItemWidth(90.0f);
        if (ImGui::InputInt(kAxisNames[axis], &pos[axis], 0, 0, ImGuiInputTextFlags_EnterReturnsTrue)) {
            SetAxisValue(axis, pos[axis]);
        }
        if (axis < 2) {
            ImGui::SameLine();
        }
        ImGui::PopID();
    }
    ImGui::TextDisabled("Original: %d, %d, %d   UV: %d, %d   RGBA: %u, %u, %u, %u", info.basePos[0], info.basePos[1],
                        info.basePos[2], info.uv[0], info.uv[1], info.color[0], info.color[1], info.color[2],
                        info.color[3]);

    if (ImGui::BeginTable("nudges", 7, ImGuiTableFlags_SizingStretchProp)) {
        DrawNudgeRow("X Left/Right", 0);
        DrawNudgeRow("Y Down/Up", 1);
        DrawNudgeRow("Z Back/Fwd", 2);
        ImGui::EndTable();
    }

    ImGui::Separator();
    ImGui::BeginDisabled(!CanUndo());
    if (ImGui::Button("Undo")) {
        Undo();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!CanRedo());
    if (ImGui::Button("Redo")) {
        Redo();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("%zu edits this course (visual only; cleared on course reload)", EditCount());
}

} // namespace GeometryEditor

namespace GeometryEditor {
// The 19 uncovered vanilla-gated pieces, with bounding boxes computed
// offline from the o2r vertex data (little-endian s16 positions). Boxes
// are in course data coordinates; mirror mode flips X at runtime.
struct GatePiece {
    const char* course;  // typeid substring of the Track class
    const char* label;   // short piece id
    const char* desc;    // what the data says it is
    float minX, minY, minZ, maxX, maxY, maxZ;
};
static const GatePiece kGatePieces[] = {
    { "BigDonut", "DE8", "dark disc, whole arena, mid-height", -854, 190, -854, 854, 230, 854 },
    { "BowsersCastle", "6A80", "large near-black structure, low", -300, -151, -2820, 2570, 260, 672 },
    { "ChocoMountain", "4608", "course-wide dark shell (known harmful)", -937, -80, -1464, 2096, 113, 1244 },
    { "DKJungle", "92D8", "brownish panel, one side", -944, -254, -2674, 581, 42, 1057 },
    { "FrappeSnowland", "65E0", "small dark-blue patch, very low", -93, -5, 466, 1577, 52, 1368 },
    { "KalimariDesert", "71C8", "dark sheet, very low", -1337, -2, -2274, 1947, 108, 1379 },
    { "KoopaTroopaBeach", "9CC0", "dark piece, low", -1354, -100, 481, 378, 113, 2711 },
    { "LuigiRaceway", "9EC0", "dark strip along one side", -1248, -250, -2479, 142, 72, 1356 },
    { "MarioRaceway", "3050", "textured but near-black, course-spanning", -351, -40, -1300, 2800, 95, 1800 },
    { "RoyalRaceway", "B030", "very wide dark-green sheet, low", -4001, -173, -2738, 1456, 193, 400 },
    { "SherbetLand", "2B48", "course-wide dark shell, mid-height", -3450, -170, -2602, 761, 343, 2150 },
    { "WarioStadium", "A0C8", "course-wide dark floor, lowest", -2931, -124, -2288, 131, 100, 1792 },
    { "BansheeBoardwalk", "6310", "underwater course view (textured)", -3150, -80, -2100, 150, 6, 1500 },
    { "KoopaTroopaBeach", "9E70", "bright translucent water sheet", -1501, -3, -1288, 256, 141, 2409 },
    { "DKJungle", "3E40", "flat sandy river sheet (east)", 877, -34, -1928, 2302, -34, -1001 },
    { "DKJungle", "3EB0", "large sandy river sheet (west)", -1817, -475, -1877, 1101, -34, 2320 },
    { "DKJungle", "3DD0", "flat sandy river sheet (far east)", 2074, -34, -1626, 4250, -34, -59 },
    { "DKJungle", "36A8", "small cyan waterfall/splash piece", -1422, -475, -1005, -434, -34, -426 },
    { "DKJungle", "3F30", "brown river/bank sheet", -1817, -475, -1133, -80, -4, 2320 },
};

static bool GateInView(const GatePiece& g, Camera* cam) {
    float minX = g.minX, maxX = g.maxX;
    if (gIsMirrorMode) {
        minX = -g.maxX;
        maxX = -g.minX;
    }
    float cx = (minX + maxX) * 0.5f;
    float cz = (g.minZ + g.maxZ) * 0.5f;
    float dx = cx - cam->pos[0];
    float dz = cz - cam->pos[2];
    float dist = sqrtf(dx * dx + dz * dz);
    float radius = 0.5f * sqrtf((maxX - minX) * (maxX - minX) + (g.maxZ - g.minZ) * (g.maxZ - g.minZ));
    if (dist < radius) {
        return true; // camera is inside the piece's footprint
    }
    float targetYaw = atan2f(dx, dz);
    float camYaw = (float) cam->rot[1] * ((float) M_PI / 32768.0f);
    float diff = targetYaw - camYaw;
    while (diff > (float) M_PI) diff -= 2.0f * (float) M_PI;
    while (diff < -(float) M_PI) diff += 2.0f * (float) M_PI;
    float halfFov = cam->fieldOfView * 0.5f * ((float) M_PI / 180.0f);
    float angRadius = atanf(radius / dist);
    return fabsf(diff) < halfFov + angRadius;
}
} // namespace GeometryEditor

void GeometryEditor::GateTourWindow::DrawElement() {
    Track* track = GetWorld() != nullptr ? GetWorld()->GetTrack() : nullptr;
    const char* courseName = (track != nullptr && gGamestate == RACING) ? typeid(*track).name() : nullptr;
    Camera* cam = (gScreenOneCtx != nullptr) ? gScreenOneCtx->camera : nullptr;

    int spotted = 0;
    char key[96];
    for (const GatePiece& g : kGatePieces) {
        snprintf(key, sizeof(key), "gGateSpotted_%s_%s", g.course, g.label);
        if (CVarGetInteger(key, 0)) {
            spotted++;
        }
    }
    ImGui::Text("Gate Tour: %d of %d spotted", spotted, (int) (sizeof(kGatePieces) / sizeof(kGatePieces[0])));
    ImGui::TextDisabled("IN VIEW (yellow) = the piece's location is in your\ncamera's horizontal view - a compass, not an X-ray\n(ignores height and walls).\nDRAWING (green) = the piece's draw code executed\nthis frame, with the reason: forced by the TEST\ntoggle, or its vanilla rule is true right now.");
    ImGui::Separator();

    const char* lastCourse = "";
    for (const GatePiece& g : kGatePieces) {
        bool here = courseName != nullptr && strstr(courseName, g.course) != nullptr;
        if (strcmp(lastCourse, g.course) != 0) {
            lastCourse = g.course;
            ImGui::Spacing();
            if (here) {
                ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.0f, 1.0f), "%s  (you are here)", g.course);
            } else {
                ImGui::TextDisabled("%s", g.course);
            }
        }
        snprintf(key, sizeof(key), "gGateSpotted_%s_%s", g.course, g.label);
        bool done = CVarGetInteger(key, 0);
        ImGui::PushID(key);
        if (ImGui::Checkbox("##spot", &done)) {
            CVarSetInteger(key, done);
        }
        ImGui::PopID();
        ImGui::SameLine();
        bool inView = here && cam != nullptr && GateInView(g, cam);
        // Ground-truth drawing signal: the piece's real draw sites report
        // each execution. Probe pieces share one signal; only the current
        // course's probe piece can be the one firing.
        char drawnId[64];
        bool probeClass = strcmp(g.label, "6310") != 0 && strcmp(g.label, "9E70") != 0 && g.course[0] != 'D';
        bool isDkWindow = strncmp(g.course, "DKJungle", 8) == 0 && strcmp(g.label, "92D8") != 0;
        if (isDkWindow || strcmp(g.label, "6310") == 0 || strcmp(g.label, "9E70") == 0) {
            snprintf(drawnId, sizeof(drawnId), "%s_%s", g.course, g.label);
        } else {
            snprintf(drawnId, sizeof(drawnId), "probe");
        }
        bool drawing = here && GeometryMode_GateDrawnRecently(drawnId);
        const char* drawTag = "";
        if (drawing) {
            drawTag = CVarGetInteger("gTestOpenGates", 0) ? "  [DRAWING - forced]" : "  [DRAWING - vanilla]";
        }
        if (inView && drawing) {
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s - %s  [IN VIEW]%s", g.label, g.desc, drawTag);
        } else if (inView) {
            ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.3f, 1.0f), "%s - %s  [IN VIEW]%s", g.label, g.desc, drawTag);
        } else if (drawing) {
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s - %s%s", g.label, g.desc, drawTag);
        } else if (here) {
            ImGui::Text("%s - %s", g.label, g.desc);
        } else {
            ImGui::TextDisabled("%s - %s", g.label, g.desc);
        }
        (void) probeClass;
    }
}

void GeometryEditor::SceneryWindow::DrawElement() {
    bool on = CVarGetInteger("gCreditsScenery", 0);
    if (ImGui::Checkbox("Show Credits-Only Scenery", &on)) {
        CVarSetInteger("gCreditsScenery", on);
    }
    ImGui::TextDisabled("Extra set dressing authored for the credits");
    ImGui::Separator();
    bool hide = CVarGetInteger("gTestHideUnconfirmedPieces", 0);
    if (ImGui::Checkbox("TEST: Hide Unconfirmed Race Pieces", &hide)) {
        CVarSetInteger("gTestHideUnconfirmedPieces", hide);
    }
    ImGui::TextDisabled("Luigi Raceway (3) and Moo Moo Farm (3):\nanything that vanishes is confirmed race content");
    ImGui::Separator();
    bool openGates = CVarGetInteger("gTestOpenGates", 0);
    if (ImGui::Checkbox("TEST: Open All Uncovered Gates", &openGates)) {
        CVarSetInteger("gTestOpenGates", openGates);
    }
    ImGui::TextDisabled("Forces every zone/probe-gated course piece to draw\nso each course's hidden pieces can be identified.\nKnown: darkens Choco (its gated piece is an interior\nshade copy - the verified-harmful case).");
    ImGui::Separator();
    if (ImGui::Button("Capture Frame Snapshot (debug)")) {
        RequestFrameDump();
    }
    ImGui::TextDisabled("Records everything drawn this frame to a file\nfor offline analysis. Takes one frame, no lag.");
}

namespace GameUI {
extern std::shared_ptr<PortMenu> mPortMenu;

static void RegisterGeometryEditorWidgets() {
    mPortMenu->AddSidebarEntry("Developer", "Geometry", 1);
    WidgetPath path = { "Developer", "Geometry", SECTION_COLUMN_1 };

    mPortMenu->AddWidget(path, "Popout Gate Tour", WIDGET_WINDOW_BUTTON)
        .CVar("gGateTourWindow")
        .Options(UIWidgets::ButtonOptions().Tooltip(
            "Checklist of the 19 uncovered vanilla-gated pieces: rows highlight when the piece's location is in "
            "your view, with a checkbox to mark each one spotted."))
        .WindowName("Gate Tour");

    mPortMenu->AddWidget(path, "Popout Geometry Editor", WIDGET_WINDOW_BUTTON)
        .CVar("gGeometryEditorWindow")
        .Options(UIWidgets::ButtonOptions().Tooltip(
            "While this window is open, the editor is active: full-course rendering and vertex clicking. "
            "Close it and races render fully stock."))
        .WindowName("Geometry Editor");

    WidgetPath enh = { "Enhancements", "General", SECTION_COLUMN_1 };
    mPortMenu->AddWidget(enh, "Show Credits-Only Scenery", WIDGET_CVAR_CHECKBOX)
        .CVar("gCreditsScenery")
        .Options(UIWidgets::CheckboxOptions().Tooltip(
            "Adds the extra scenery authored for the credits fly-through (billboards, set dressing) to normal "
            "races. May show quirks; it was never meant to be seen up close."));
    mPortMenu->AddWidget(enh, "Popout Scenery Toggle", WIDGET_WINDOW_BUTTON)
        .CVar("gCreditsSceneryWindow")
        .Options(UIWidgets::ButtonOptions().Tooltip("Floating toggle for live before/after comparison"))
        .WindowName("Credits Scenery");
    // TEMP test instrument (2026-08-31): live A/B for the six race-code
    // pieces whose race visibility is guessed, not confirmed.
    mPortMenu->AddWidget(enh, "TEST: Hide Unconfirmed Race Pieces", WIDGET_CVAR_CHECKBOX)
        .CVar("gTestHideUnconfirmedPieces")
        .Options(UIWidgets::CheckboxOptions().Tooltip(
            "Temporary test toggle. Hides six race-drawn pieces on Luigi Raceway (3) and Moo Moo Farm (3) whose "
            "race visibility is unconfirmed. Flip it during a race: anything that visibly disappears is confirmed "
            "race content; if nothing changes on a course, its pieces are invisible in races."));

    mPortMenu->AddWidget(path, "Watch Credits Sequence", WIDGET_BUTTON)
        .Callback([](WidgetInfo& info) {
            func_800CA330(0x19); // the game's own pre-credits music fade-out
            gGamestateNext = CREDITS_SEQUENCE;
        })
        .Options(UIWidgets::ButtonOptions().Tooltip(
            "Jumps straight to the game's credits sequence (the game's own debug shortcut). Quit and relaunch to "
            "return to normal play."));
}

static RegisterMenuInitFunc initFunc(RegisterGeometryEditorWidgets);
} // namespace GameUI
