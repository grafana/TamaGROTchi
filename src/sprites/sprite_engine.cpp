#include "sprite_engine.h"
#include "grot_frames.h"
#include <math.h>
#include <Arduino.h>

// =============================================================================
// Layout
// Source frames: 32×32 px with alpha.
//
// We use the image-level API: lv_image_set_scale / lv_image_set_pivot.
// This is critical — lv_image_t ignores style transform_scale for its draw
// descriptor and instead reads img->scale_x/y directly.  Using style
// transforms triggers LVGL's layer-compositing path; a negative style
// scale_x (for flip) causes an infinite loop in lv_timer_handler().
//
// Image-level scale is uint32_t only → no negative/flip.  Horizontal flip
// is not supported; set_facing() is a no-op until pre-flipped frames exist.
//
// Pivot at (16, 16) = centre of the 32×32 source frame.
// Scale 3× (768 = 3 × LV_SCALE_NONE) → rendered size 96×96.
//
// Rendered region for an object at (obj_x, obj_y):
//   x : [obj_x + 16 − 48, obj_x + 16 + 48] = [obj_x − 32, obj_x + 64]
//   y : [obj_y + 16 − 48, obj_y + 16 + 48] = [obj_y − 32, obj_y + 64]
//
// ext_draw_size (dirty-region guard) = 32 px on each side (image-level path
// uses lv_image_buf_get_transformed_area which computes actual corners).
//
// Walk / jump bounds (ext_draw = 32 extends dirty region by the rendered overhang):
//   dirty right edge:  obj_x + 64 ≤ 239  →  obj_x ≤ 175  (use 170)
//   dirty left  edge:  obj_x − 32 ≥   0  →  obj_x ≥  32  (use 38)
//
// Position constants derived from:
//   Sprite zone: 240 × 178 px  (SPRITE_H after HINTS_H was halved to 28)
//   Rendered centre x = ZONE_W / 2 = 120  →  obj_x = 104  (120 − pivot 16)
//   Rendered centre y = ZONE_H / 2 =  89  →  obj_y =  73  ( 89 − pivot 16)
//   Walk left  bound: obj_x = 38  → rendered left  =   6  (dirty =  6 ≥ 0 ✓)
//   Walk right bound: obj_x = 170 → rendered right = 234  (dirty = 234 ≤ 239 ✓)
//   Jump top   bound: obj_y = 52  → rendered top   =  20  (dirty =  20 ≥ 0 ✓)
// =============================================================================
static const int32_t    SCALE     = 768;
static const lv_coord_t CENTER_X  = 104;
static const lv_coord_t CENTER_Y  =  73;
static const lv_coord_t WALK_XMIN =  38;
static const lv_coord_t WALK_XMAX = 170;
static const lv_coord_t JUMP_YMIN =  52;

// =============================================================================
// State
// =============================================================================
enum class MoveMode : uint8_t { CENTER, WALK, JUMP };

static lv_obj_t*                    _img_obj     = nullptr;
static lv_timer_t*                  _anim_timer  = nullptr;
static lv_timer_t*                  _move_timer  = nullptr;
static const lv_image_dsc_t* const* _frame_table = nullptr;
static uint8_t                      _frame_count = 0;
static uint8_t                      _cur_frame   = 0;

static MoveMode _move_mode  = MoveMode::CENTER;
static float    _pos_x      = (float)CENTER_X;
static float    _pos_y      = (float)CENTER_Y;
static bool     _walk_right = true;
static float    _jump_phase = 0.0f;

// Cached frame tables for both walk directions; populated by sprite_engine_set_state()
// when the active state uses the WALK move mode.  Left and right always have
// the same frame count so _frame_count does not need updating on a facing flip.
static const lv_image_dsc_t* const* _walk_right_table = nullptr;
static const lv_image_dsc_t* const* _walk_left_table  = nullptr;

static uint8_t  _last_stage   = 0xFF;
static uint8_t  _last_emotion = 0xFF;
static uint8_t  _last_quality = 0xFF;

// =============================================================================
// Facing helper — swaps the active frame table to the appropriate direction.
// Left and right tables have identical frame counts; _frame_count is unchanged.
// =============================================================================
static void set_facing(bool right) {
    const lv_image_dsc_t* const* t = right ? _walk_right_table : _walk_left_table;
    if (!t || !_img_obj) return;
    _frame_table = t;
    _cur_frame   = 0;
    lv_image_set_src(_img_obj, t[0]);
}

// =============================================================================
// Frame animation timer (400 ms per frame)
// =============================================================================
static void anim_timer_cb(lv_timer_t*) {
    if (!_frame_table || _frame_count == 0) return;
    _cur_frame = (_cur_frame + 1) % _frame_count;
    lv_image_set_src(_img_obj, _frame_table[_cur_frame]);
}

// =============================================================================
// Movement timer (50 ms — smooth position update)
// =============================================================================
static void move_timer_cb(lv_timer_t*) {
    if (!_img_obj) return;

    switch (_move_mode) {

    case MoveMode::WALK: {
        // 0.8 px / 50 ms ≈ 16 px/s  →  ~8 s to cross the zone
        const float SPEED = 0.8f;
        _pos_x += _walk_right ? SPEED : -SPEED;

        if (_walk_right && _pos_x >= (float)WALK_XMAX) {
            _pos_x = (float)WALK_XMAX;
            _walk_right = false;
            set_facing(false);          // flip to face left
        } else if (!_walk_right && _pos_x <= (float)WALK_XMIN) {
            _pos_x = (float)WALK_XMIN;
            _walk_right = true;
            set_facing(true);           // flip back to face right
        }
        lv_obj_set_x(_img_obj, (lv_coord_t)_pos_x);
        break;
    }

    case MoveMode::JUMP: {
        // abs(sin) gives a double-bounce feel;  ~2.0 s per full cycle
        const float PHASE_STEP = 0.16f;
        _jump_phase += PHASE_STEP;
        if (_jump_phase > 6.2832f) _jump_phase -= 6.2832f;

        const float JUMP_H = (float)(CENTER_Y - JUMP_YMIN);  // ~21 px
        _pos_y = (float)CENTER_Y - JUMP_H * fabsf(sinf(_jump_phase));
        lv_obj_set_y(_img_obj, (lv_coord_t)_pos_y);
        break;
    }

    case MoveMode::CENTER:
    default:
        break;
    }
}

// =============================================================================
// Mode transitions
// =============================================================================
static void enter_mode(MoveMode m) {
    _move_mode = m;
    switch (m) {
    case MoveMode::WALK:
        _pos_y = (float)CENTER_Y;
        _walk_right = true;
        set_facing(true);
        lv_obj_set_y(_img_obj, CENTER_Y);
        break;

    case MoveMode::JUMP:
        _pos_x      = (float)CENTER_X;
        _pos_y      = (float)CENTER_Y;
        _jump_phase = 0.0f;
        set_facing(true);
        lv_obj_set_pos(_img_obj, CENTER_X, CENTER_Y);
        break;

    case MoveMode::CENTER:
    default:
        _pos_x = (float)CENTER_X;
        _pos_y = (float)CENTER_Y;
        set_facing(true);
        lv_obj_set_pos(_img_obj, CENTER_X, CENTER_Y);
        break;
    }
}

// =============================================================================
// Public API
// =============================================================================
void sprite_engine_init(lv_obj_t* parent) {
    _img_obj = lv_image_create(parent);

    // Image-level scale/pivot (NOT style transforms).
    // lv_image_t reads img->scale_x/y directly; style transform_scale is
    // ignored by the image draw handler and instead triggers the layer-
    // compositing path (which hangs on negative/flip scale values).
    // Pivot at (16, 16) = centre of the 32×32 source image.
    lv_image_set_pivot(_img_obj, 16, 16);
    lv_image_set_scale(_img_obj, (uint32_t)SCALE);

    _pos_x = (float)CENTER_X;
    _pos_y = (float)CENTER_Y;
    lv_obj_set_pos(_img_obj, CENTER_X, CENTER_Y);

    grot_frames_init();

    _frame_table = grot_get_frames(
        static_cast<uint8_t>(LifeStage::EGG),
        static_cast<uint8_t>(GrotEmotion::NORMAL),
        static_cast<uint8_t>(EvoQuality::EXCELLENT));
    _frame_count = grot_get_frame_count(
        static_cast<uint8_t>(LifeStage::EGG),
        static_cast<uint8_t>(GrotEmotion::NORMAL),
        static_cast<uint8_t>(EvoQuality::EXCELLENT));
    _cur_frame = 0;

    if (_frame_table && _frame_count > 0)
        lv_image_set_src(_img_obj, _frame_table[0]);

    _anim_timer = lv_timer_create(anim_timer_cb, 400, nullptr);
    _move_timer = lv_timer_create(move_timer_cb,  50, nullptr);
}

void sprite_engine_set_state(LifeStage stage, GrotEmotion emotion, EvoQuality quality) {
    uint8_t s = static_cast<uint8_t>(stage);
    uint8_t e = static_cast<uint8_t>(emotion);
    uint8_t q = static_cast<uint8_t>(quality);

    if (s == _last_stage && e == _last_emotion && q == _last_quality) return;
    _last_stage   = s;
    _last_emotion = e;
    _last_quality = q;

    _frame_table = grot_get_frames(s, e, q);
    _frame_count = grot_get_frame_count(s, e, q);
    _cur_frame   = 0;

    if (_frame_table && _frame_count > 0 && _img_obj)
        lv_image_set_src(_img_obj, _frame_table[0]);

    const bool is_walk = (emotion == GrotEmotion::NORMAL &&
        (stage == LifeStage::CHILD  || stage == LifeStage::TEEN  ||
         stage == LifeStage::ADULT  || stage == LifeStage::SENIOR));
    const bool is_jump = (emotion == GrotEmotion::HAPPY ||
                          emotion == GrotEmotion::DIZZY);

    // Cache both walk directions so set_facing() can swap tables without a
    // full state lookup.  grot_get_frames() returns the right-facing table;
    // grot_get_walk_left_frames() returns the matching left-facing table.
    if (is_walk) {
        _walk_right_table = _frame_table;
        _walk_left_table  = grot_get_walk_left_frames(s, q);
    } else {
        _walk_right_table = nullptr;
        _walk_left_table  = nullptr;
    }

    MoveMode new_mode = is_walk ? MoveMode::WALK
                      : is_jump ? MoveMode::JUMP
                      :           MoveMode::CENTER;

    if (new_mode != _move_mode)
        enter_mode(new_mode);
}

lv_obj_t* sprite_engine_get_obj() {
    return _img_obj;
}

void sprite_engine_test_next() {
    // Ordered test sequence: covers every distinct animation + movement mode
    struct TestState {
        LifeStage  stage;
        GrotEmotion emotion;
        EvoQuality quality;
        const char* label;
    };
    static const TestState STATES[] = {
        { LifeStage::EGG,    GrotEmotion::NORMAL,   EvoQuality::EXCELLENT, "EGG idle (bob)"          },
        { LifeStage::BABY,   GrotEmotion::NORMAL,   EvoQuality::EXCELLENT, "BABY idle (bob)"         },
        { LifeStage::BABY,   GrotEmotion::HAPPY,    EvoQuality::EXCELLENT, "BABY happy (wave)"       },
        { LifeStage::BABY,   GrotEmotion::SAD,      EvoQuality::EXCELLENT, "BABY sad (thinks)"       },
        { LifeStage::CHILD,  GrotEmotion::NORMAL,   EvoQuality::EXCELLENT, "CHILD idle → walk"       },
        { LifeStage::CHILD,  GrotEmotion::HAPPY,    EvoQuality::EXCELLENT, "CHILD happy → jump"      },
        { LifeStage::CHILD,  GrotEmotion::SAD,      EvoQuality::EXCELLENT, "CHILD sad (thinks)"      },
        { LifeStage::CHILD,  GrotEmotion::SICK,     EvoQuality::EXCELLENT, "CHILD sick (thinks)"     },
        { LifeStage::ADULT,  GrotEmotion::NORMAL,   EvoQuality::EXCELLENT, "ADULT/Exc idle → walk"   },
        { LifeStage::ADULT,  GrotEmotion::HAPPY,    EvoQuality::EXCELLENT, "ADULT/Exc happy → jump"  },
        { LifeStage::ADULT,  GrotEmotion::NORMAL,   EvoQuality::GOOD,      "ADULT/Good idle → walk"  },
        { LifeStage::ADULT,  GrotEmotion::HAPPY,    EvoQuality::GOOD,      "ADULT/Good happy → jump" },
        { LifeStage::ADULT,  GrotEmotion::NORMAL,   EvoQuality::TIRED,     "ADULT/Tired idle (bob)"  },
        { LifeStage::ADULT,  GrotEmotion::SLEEPING, EvoQuality::EXCELLENT, "SLEEPING (blink)"        },
        { LifeStage::ADULT,  GrotEmotion::SICK,     EvoQuality::EXCELLENT, "SICK (thinks)"           },
        { LifeStage::ADULT,  GrotEmotion::DIZZY,    EvoQuality::EXCELLENT, "DIZZY → jump"            },
        { LifeStage::DEAD,   GrotEmotion::DEAD,     EvoQuality::EXCELLENT, "DEAD (thinks)"           },
    };
    static const int N = sizeof(STATES) / sizeof(STATES[0]);
    static int idx = 0;

    const TestState& s = STATES[idx];
    Serial.printf("[sprite test] %d/%d: %s\n", idx + 1, N, s.label);

    // Force state (bypass the "skip if unchanged" check)
    _last_stage   = 0xFF;
    _last_emotion = 0xFF;
    _last_quality = 0xFF;
    sprite_engine_set_state(s.stage, s.emotion, s.quality);

    idx = (idx + 1) % N;
}
