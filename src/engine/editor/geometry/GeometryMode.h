#pragma once

#include <stdint.h>

// C bridge for the in-game track geometry editor. The mode owns a
// full-course render path (every leaf display list exactly once) that
// replaces the per-section visibility lists while the editor is active.
#ifdef __cplusplus
extern "C" {
#endif

int32_t GeometryMode_IsActive(void);
// Returns 1 if it drew the course (caller must return without the normal
// section draw); 0 to fall through to the game's own path.
int32_t GeometryMode_DrawFullTrack(const char* const addr[], int16_t direction);
void GeometryMode_OnTrackSectionsParsed(void* sections);

#ifdef __cplusplus
}
#endif
