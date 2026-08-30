#ifndef FREE_CAM_ENGINE_H
#define FREE_CAM_ENGINE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <libultraship.h>
#include "camera.h"

// Yaw/pitch rotation sensitivity
#define MOUSE_SENSITIVITY_X 0.006f
#define MOUSE_SENSITIVITY_Y 0.006f

typedef struct {
    Vec3f velocity;
    Vec3f rotVelocity;
    Vec3f forwardVector;
} FreeCam;

extern FreeCam freeCam;
extern f32 gDampValue;
extern f32 gRotDampValue;
extern u32 fTargetPlayer;
extern u32 fRankIndex;

void freecam_calculate_forward_vector_allow_rotation(Camera* camera, Vec3f forwardVector);
// Copies the source camera's pose (position + derived yaw/pitch) onto the
// freecam and clears its momentum, so enabling freecam continues seamlessly
// from the current view instead of the freecam's last resting spot.
void freecam_start_from(Camera* freecamCamera, Camera* source);
void freecam_target_player(Camera* camera, Vec3f forwardVector);
void freecam_tick(Camera* camera, Vec3f forwardVector);

#ifdef __cplusplus
}
#endif

#endif // FREE_CAM_ENGINE_H
