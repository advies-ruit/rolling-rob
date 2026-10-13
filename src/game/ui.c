#include "ui.h"

static float s_k = 1.0f;

void rr_ui_init(int32_t screen_w)
{
    s_k = (float)screen_w / 480.0f;
    if(s_k < 1.0f) s_k = 1.0f;
}

void rr_ui_set_scale(float k) { s_k = k < 1.0f ? 1.0f : k; }

float rr_ui_scale(void) { return s_k; }

static const struct { int px; const lv_font_t * font; } fonts[] = {
#if LV_FONT_MONTSERRAT_48
    { 48, &lv_font_montserrat_48 },
#endif
#if LV_FONT_MONTSERRAT_42
    { 42, &lv_font_montserrat_42 },
#endif
#if LV_FONT_MONTSERRAT_40
    { 40, &lv_font_montserrat_40 },
#endif
#if LV_FONT_MONTSERRAT_36
    { 36, &lv_font_montserrat_36 },
#endif
#if LV_FONT_MONTSERRAT_32
    { 32, &lv_font_montserrat_32 },
#endif
#if LV_FONT_MONTSERRAT_30
    { 30, &lv_font_montserrat_30 },
#endif
#if LV_FONT_MONTSERRAT_28
    { 28, &lv_font_montserrat_28 },
#endif
#if LV_FONT_MONTSERRAT_24
    { 24, &lv_font_montserrat_24 },
#endif
#if LV_FONT_MONTSERRAT_26
    { 26, &lv_font_montserrat_26 },
#endif
#if LV_FONT_MONTSERRAT_22
    { 22, &lv_font_montserrat_22 },
#endif
#if LV_FONT_MONTSERRAT_20
    { 20, &lv_font_montserrat_20 },
#endif
#if LV_FONT_MONTSERRAT_18
    { 18, &lv_font_montserrat_18 },
#endif
#if LV_FONT_MONTSERRAT_16
    { 16, &lv_font_montserrat_16 },
#endif
    { 14, &lv_font_montserrat_14 },
    { 12, &lv_font_montserrat_12 },
    { 10, &lv_font_montserrat_10 },
};

/* nearest size, a tie going to the smaller one (the original 10/12/14
 * choice: up to 11 -> 10, up to 13 -> 12, above -> 14) */
static unsigned pick(float px)
{
    unsigned best = 0;
    float best_d = 1e9f;
    for(unsigned i = 0; i < sizeof(fonts) / sizeof(fonts[0]); i++) {
        float d = px - (float)fonts[i].px;
        if(d < 0) d = -d;
        if(d <= best_d) {
            best_d = d;
            best = i;
        }
    }
    return best;
}

const lv_font_t * rr_ui_font(float px) { return fonts[pick(px)].font; }

int32_t rr_ui_font_size(float px) { return fonts[pick(px)].px; }
