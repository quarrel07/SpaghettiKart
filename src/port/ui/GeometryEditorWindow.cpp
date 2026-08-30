#include "GeometryEditorWindow.h"

#include <imgui.h>

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

    if (!CVarGetInteger("gGeometryEditorEnabled", 0)) {
        ImGui::TextWrapped(
            "The Geometry Editor is off. Turn on \"Enable Geometry Editor\" (Developer > Geometry) to use it.");
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
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Show vertices and wireframe through geometry (screen overlay) instead of depth-tested in-scene rendering");
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

namespace GameUI {
extern std::shared_ptr<PortMenu> mPortMenu;

static void RegisterGeometryEditorWidgets() {
    mPortMenu->AddSidebarEntry("Developer", "Geometry", 1);
    WidgetPath path = { "Developer", "Geometry", SECTION_COLUMN_1 };

    mPortMenu->AddWidget(path, "Enable Geometry Editor", WIDGET_CVAR_CHECKBOX)
        .CVar("gGeometryEditorEnabled")
        .Options(UIWidgets::CheckboxOptions().Tooltip(
            "Master switch for the track geometry editor: full-course rendering, wireframe, and vertex editing"));

    mPortMenu->AddWidget(path, "Popout Geometry Editor", WIDGET_WINDOW_BUTTON)
        .CVar("gGeometryEditorWindow")
        .Options(UIWidgets::ButtonOptions().Tooltip(
            "Opens the track geometry editor: click course vertices to inspect and move them"))
        .WindowName("Geometry Editor");
}

static RegisterMenuInitFunc initFunc(RegisterGeometryEditorWidgets);
} // namespace GameUI
