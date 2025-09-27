#ifndef PICOSTATION_PS1_VIDEO_OVERRIDE_H
#define PICOSTATION_PS1_VIDEO_OVERRIDE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PS1_VIDEO_MODE_NTSC = 0,
    PS1_VIDEO_MODE_PAL  = 1,
} ps1_video_mode_t;

// Install the GPU override hook and apply the user preference immediately.
void ps1_video_override_install(void);
// Force a specific video mode (also persisted in mod storage).
void ps1_video_override_set_mode(ps1_video_mode_t mode);
ps1_video_mode_t ps1_video_override_get_mode(void);
// Poll the controller once and toggle when SELECT+R1 are held.
bool ps1_video_override_toggle_from_input(void);

#ifdef __cplusplus
}
#endif

#endif // PICOSTATION_PS1_VIDEO_OVERRIDE_H
