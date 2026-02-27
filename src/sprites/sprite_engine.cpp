#include "sprite_engine.h"
#include "grot_frames.h"

static lv_obj_t*                      _img_obj     = nullptr;
static lv_timer_t*                    _anim_timer  = nullptr;
static const lv_image_dsc_t* const*   _frame_table = nullptr;
static uint8_t                      _frame_count = 0;
static uint8_t                      _cur_frame   = 0;

// Cached state (avoid redundant updates)
static uint8_t _last_stage   = 0xFF;
static uint8_t _last_emotion = 0xFF;
static uint8_t _last_quality = 0xFF;

static void anim_timer_cb(lv_timer_t* /*t*/) {
    if (!_frame_table || _frame_count == 0) return;
    _cur_frame = (_cur_frame + 1) % _frame_count;
    lv_image_set_src(_img_obj, _frame_table[_cur_frame]);
}

void sprite_engine_init(lv_obj_t* parent) {
    _img_obj = lv_image_create(parent);

    // Scale 32×32 sprite to fill the 170 px sprite zone height.
    // 170 / 32 * 256 = 1360.  Pivot at sprite centre so lv_obj_center()
    // keeps the rendered image centred in the zone.
    lv_image_set_scale(_img_obj, 1360);
    lv_image_set_pivot(_img_obj, 16, 16);
    lv_obj_center(_img_obj);

    // Fill placeholder pixel data (must happen before first lv_image_set_src call)
    grot_frames_init();

    // Default: egg idle, frame 0
    const lv_image_dsc_t* const* frames = grot_get_frames(
        static_cast<uint8_t>(LifeStage::EGG),
        static_cast<uint8_t>(GrotEmotion::NORMAL),
        static_cast<uint8_t>(EvoQuality::EXCELLENT));

    _frame_table = frames;
    _frame_count = grot_get_frame_count(
        static_cast<uint8_t>(LifeStage::EGG),
        static_cast<uint8_t>(GrotEmotion::NORMAL),
        static_cast<uint8_t>(EvoQuality::EXCELLENT));
    _cur_frame = 0;

    if (_frame_table && _frame_count > 0) {
        lv_image_set_src(_img_obj, _frame_table[0]);
    }

    // 400 ms per frame → ~2.5 FPS animation
    _anim_timer = lv_timer_create(anim_timer_cb, 400, nullptr);
}

void sprite_engine_set_state(LifeStage stage, GrotEmotion emotion, EvoQuality quality) {
    uint8_t s = static_cast<uint8_t>(stage);
    uint8_t e = static_cast<uint8_t>(emotion);
    uint8_t q = static_cast<uint8_t>(quality);

    // Skip if nothing changed
    if (s == _last_stage && e == _last_emotion && q == _last_quality) return;

    _last_stage   = s;
    _last_emotion = e;
    _last_quality = q;

    _frame_table = grot_get_frames(s, e, q);
    _frame_count = grot_get_frame_count(s, e, q);
    _cur_frame   = 0;

    if (_frame_table && _frame_count > 0 && _img_obj) {
        lv_image_set_src(_img_obj, _frame_table[0]);
    }
}

lv_obj_t* sprite_engine_get_obj() {
    return _img_obj;
}
