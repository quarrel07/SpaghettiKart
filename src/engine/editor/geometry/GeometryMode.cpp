#include "GeometryMode.h"
#include "GeometryCatalog.h"

#include <cstdio>

#include "engine/World.h"
#include "engine/tracks/Track.h"

extern "C" {
#include "main.h"
#include "code_800029B0.h"
}

namespace GeometryEditor {

static GeometryCatalog sCatalog;
static const char* const* sPrimaryTable = nullptr;
static size_t sSectionCount = 0;

static bool ModeActive() {
    // TEMP default-on while the editor window (M3) does not exist yet; the
    // window's visibility becomes the real switch.
    if (!CVarGetInteger("gGeometryEditor", 1)) {
        return false;
    }
    if (gIsMirrorMode != 0) {
        return false;
    }
    Track* track = GetWorld()->GetTrack();
    if (track == nullptr || track->IsMod()) {
        return false;
    }
    return true;
}

} // namespace GeometryEditor

extern "C" int32_t GeometryMode_IsActive(void) {
    return GeometryEditor::ModeActive() ? 1 : 0;
}

extern "C" void GeometryMode_OnTrackSectionsParsed(void* sections) {
    using namespace GeometryEditor;
    TrackSections* section = (TrackSections*) sections;
    size_t maxSection = 0;
    while (section != nullptr && section->crc != 0) {
        if (section->sectionId > maxSection) {
            maxSection = section->sectionId;
        }
        section++;
    }
    sSectionCount = maxSection;
    sPrimaryTable = nullptr;
    sCatalog.Reset();
}

extern "C" int32_t GeometryMode_DrawFullTrack(const char* const addr[], int16_t direction) {
    using namespace GeometryEditor;
    if (!ModeActive() || sSectionCount == 0) {
        return 0;
    }
    // The first table rendered after a course parse is the primary one;
    // secondary tables (transparency passes) keep the game's section path.
    if (sPrimaryTable == nullptr) {
        sPrimaryTable = addr;
    }
    if (addr != sPrimaryTable) {
        return 0;
    }
    if (!sCatalog.Built()) {
        sCatalog.Build(addr, sSectionCount);
        printf("[GeometryMode] catalog: %zu sections, %zu leaves, %zu vertices, %zu weld groups\n", sSectionCount,
               sCatalog.LeafCount(), sCatalog.Vertices().size(), sCatalog.WeldGroupCount());
    }
    gSPDisplayList(gDisplayListHead++, (Gfx*) sCatalog.MergedDL(direction).data());
    return 1;
}
