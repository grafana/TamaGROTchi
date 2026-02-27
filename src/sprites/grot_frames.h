#pragma once
#include <lvgl.h>

// =============================================================================
// Grot Sprite Frames — LVGL v9 image descriptors
//
// Each symbol is defined in its own auto-generated .cpp file produced by
// tools/png_to_lvgl.py.  Format: LV_COLOR_FORMAT_RGB565A8, 32×32 px.
//
// To regenerate after editing PNGs:
//   python3 tools/png_to_lvgl.py src/sprites/<file>.png
// =============================================================================

// grot.png — 4-frame idle bob
extern const lv_image_dsc_t grot_0;
extern const lv_image_dsc_t grot_1;
extern const lv_image_dsc_t grot_2;
extern const lv_image_dsc_t grot_3;

// grot-blink.png — 2-frame blink / drowsy
extern const lv_image_dsc_t grot_blink_0;
extern const lv_image_dsc_t grot_blink_1;

// grot-wave.png — 6-frame wave / happy
extern const lv_image_dsc_t grot_wave_0;
extern const lv_image_dsc_t grot_wave_1;
extern const lv_image_dsc_t grot_wave_2;
extern const lv_image_dsc_t grot_wave_3;
extern const lv_image_dsc_t grot_wave_4;
extern const lv_image_dsc_t grot_wave_5;

// grot-walk.png — 8-frame walk cycle
extern const lv_image_dsc_t grot_walk_0;
extern const lv_image_dsc_t grot_walk_1;
extern const lv_image_dsc_t grot_walk_2;
extern const lv_image_dsc_t grot_walk_3;
extern const lv_image_dsc_t grot_walk_4;
extern const lv_image_dsc_t grot_walk_5;
extern const lv_image_dsc_t grot_walk_6;
extern const lv_image_dsc_t grot_walk_7;

// grot-jumping.png — 8-frame jump
extern const lv_image_dsc_t grot_jumping_0;
extern const lv_image_dsc_t grot_jumping_1;
extern const lv_image_dsc_t grot_jumping_2;
extern const lv_image_dsc_t grot_jumping_3;
extern const lv_image_dsc_t grot_jumping_4;
extern const lv_image_dsc_t grot_jumping_5;
extern const lv_image_dsc_t grot_jumping_6;
extern const lv_image_dsc_t grot_jumping_7;

// grot-thinks.png — 5-frame thinking / sad
extern const lv_image_dsc_t grot_thinks_0;
extern const lv_image_dsc_t grot_thinks_1;
extern const lv_image_dsc_t grot_thinks_2;
extern const lv_image_dsc_t grot_thinks_3;
extern const lv_image_dsc_t grot_thinks_4;

// ---------------------------------------------------------------------------
// No-op — sprites are statically initialised; kept for call-site compat.
// ---------------------------------------------------------------------------
inline void grot_frames_init() {}

// ---------------------------------------------------------------------------
// Frame lookup — NULL-terminated arrays; used by sprite_engine
// ---------------------------------------------------------------------------
extern const lv_image_dsc_t* const* grot_get_frames(uint8_t stage, uint8_t emotion, uint8_t quality);
extern uint8_t grot_get_frame_count(uint8_t stage, uint8_t emotion, uint8_t quality);
