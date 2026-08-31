#pragma once

#include <stdint.h>

// C bridge for the in-game track geometry editor. The mode owns a
// full-course render path (every leaf display list exactly once) that
// replaces the per-section visibility lists while the editor is active.
#ifdef __cplusplus
extern "C" {
#endif

int32_t GeometryMode_IsActive(void);
// TEMP diagnostic: walks the completed frame command stream into a text file
// when a dump was requested from the editor window.
void GeometryMode_DumpFrame(void* pool);
// Returns 1 if it drew the course (caller must return without the normal
// section draw); 0 to fall through to the game's own path.
int32_t GeometryMode_DrawFullTrack(const char* const addr[], int16_t direction);
void GeometryMode_OnTrackSectionsParsed(void* sections);
// Stock-freecam support: CM_DrawTrack marks the main track pass, and inside
// render_track_sections the marked pass substitutes the credits list for the
// section list, so the course's own Draw() preamble and post-section extras
// (combine modes, shading state, decal shadows) still run around it.
void GeometryMode_MarkTrackPass(int32_t inPass);
// Returns 1 if it drew the course (caller returns). Returns 0 to continue
// the normal sectioned draw; in freecam that continues with *indexInOut
// overridden to the section nearest the camera, so the default view matches
// the race mode and follows the camera instead of stale kart bookkeeping.
int32_t GeometryMode_StockFreecamDraw(const char* const addr[], int16_t direction, int16_t* indexInOut);
void GeometryMode_Tick(void);

#ifdef __cplusplus
}

// C++ surface for the editor window.
#include <cstddef>

namespace GeometryEditor {

struct SelectionInfo {
    bool valid = false;
    const char* resName = nullptr; // full resource path, may be null
    uint64_t resCrc = 0;
    uint16_t index = 0;
    int16_t livePos[3] = { 0, 0, 0 };
    int16_t basePos[3] = { 0, 0, 0 };
    int16_t uv[2] = { 0, 0 };
    uint8_t color[4] = { 0, 0, 0, 0 };
    int weldCount = 0;
    bool drawnInRace = false; // racing also draws this vertex
    size_t catalogVertices = 0;
    size_t catalogLeaves = 0;
};

bool WindowOpen();
SelectionInfo GetSelection();
void ClearSelection();
void CycleSelection();
bool WeldEnabled();
void SetWeldEnabled(bool enabled);
bool CloudEnabled();
void SetCloudEnabled(bool enabled);
bool HideBackfacesEnabled();
void SetHideBackfaces(bool enabled);
bool XrayEnabled();
void SetXray(bool enabled);
// 0 = race assembly (merged union of the racing visibility lists),
// 1 = credits assembly (the authored full-course credits list).
int CourseView();
void SetCourseView(int view);
// axis: 0 = X (left/right), 1 = Y (down/up), 2 = Z (back/forward)
void Nudge(int axis, int amount);
void SetAxisValue(int axis, int value);
bool CanUndo();
bool CanRedo();
void Undo();
void Redo();
size_t EditCount();
// Draws vertex markers into the ImGui foreground draw list; call from the
// editor window's DrawElement (inside the ImGui frame).
void DrawOverlay();
void RequestFrameDump();

// Live input diagnostics so one screenshot shows why clicks or dots
// misbehave: cursor position, game-viewport rectangle, distance to the
// nearest projected vertex, and the click-gate states.
struct DebugStatus {
    float mouseX = 0, mouseY = 0;
    int vpX = 0, vpY = 0, vpW = 0, vpH = 0;
    float nearestPx = -1;
    bool overUI = false;
    bool inViewport = false;
    unsigned int tickCount = 0; // increments every input tick; 0 = tick never ran
    bool mouseDown = false;     // left button as the input tick sees it
    char lastClick[48] = "none yet"; // outcome of the most recent click
};
DebugStatus GetDebugStatus();

} // namespace GeometryEditor
#endif
