#ifndef UNIT_TEST
#include <Arduino.h>
#include <cstring>
#include "ui/widgets.h"
#include "hal/buzzer_controller.h"

namespace ui {
namespace {

void feedback_cb(lv_event_t*) {
    BuzzerController::instance().playTapTick();
}

}  // namespace

lv_obj_t* box(lv_obj_t* parent) {
    lv_obj_t* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_scrollable(obj, false);
    lv_obj_set_clickable(obj, false);
    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    return obj;
}

lv_obj_t* row(lv_obj_t* parent, int32_t gap) {
    lv_obj_t* obj = box(parent);
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(obj, gap, 0);
    return obj;
}

lv_obj_t* column(lv_obj_t* parent, int32_t gap) {
    lv_obj_t* obj = box(parent);
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(obj, gap, 0);
    return obj;
}

lv_obj_t* label(lv_obj_t* parent, const lv_font_t* font, lv_color_t color, const char* text) {
    lv_obj_t* obj = lv_label_create(parent);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, color, 0);
    lv_label_set_text(obj, text);
    return obj;
}

void single_line(lv_obj_t* label) {
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(label, lv_font_get_line_height(lv_obj_get_style_text_font(label, LV_PART_MAIN)));
}

lv_obj_t* icon(lv_obj_t* parent, const char* glyph, const lv_font_t* font, lv_color_t color) {
    return label(parent, font, color, glyph);
}

lv_obj_t* card(lv_obj_t* parent) {
    lv_obj_t* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_scrollable(obj, false);
    lv_obj_set_clickable(obj, false);
    lv_obj_set_style_bg_color(obj, color::card(), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, layout::kRadius, 0);
    lv_obj_set_style_pad_all(obj, layout::kPad, 0);
    return obj;
}

lv_obj_t* button(lv_obj_t* parent, const char* glyph, const char* text, ButtonStyle style,
                 lv_event_cb_t on_click, void* user) {
    lv_obj_t* btn = lv_button_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_height(btn, layout::kButtonH);
    lv_obj_set_style_radius(btn, layout::kRadiusSm + 2, 0);
    lv_obj_set_style_pad_hor(btn, 14, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btn, 6, 0);

    lv_color_t bg = color::card_hi();
    lv_color_t fg = color::text();
    switch (style) {
        case ButtonStyle::Primary:
            bg = color::accent();
            fg = color::on_accent();
            break;
        case ButtonStyle::Danger:
            bg = color::crit();
            fg = color::text();
            break;
        case ButtonStyle::Ghost:
            lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
            break;
        case ButtonStyle::Secondary:
        default:
            break;
    }
    lv_obj_set_style_bg_color(btn, bg, 0);

    if (glyph != nullptr && glyph[0] != '\0') {
        icon(btn, glyph, font::title(), fg);
    }
    if (text != nullptr && text[0] != '\0') {
        label(btn, font::title(), fg, text);
    }

    if (on_click != nullptr) {
        lv_obj_add_event_cb(btn, on_click, LV_EVENT_CLICKED, user);
    }
    add_touch_feedback(btn);
    return btn;
}

lv_obj_t* icon_button(lv_obj_t* parent, const char* glyph, int32_t size, lv_event_cb_t on_click, void* user) {
    lv_obj_t* btn = lv_button_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, size, size);
    lv_obj_set_style_radius(btn, layout::kRadiusSm, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(btn, color::card_hi(), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_STATE_PRESSED);

    lv_obj_t* icn = icon(btn, glyph, font::icon24(), color::text());
    lv_obj_center(icn);

    if (on_click != nullptr) {
        lv_obj_add_event_cb(btn, on_click, LV_EVENT_CLICKED, user);
    }
    add_touch_feedback(btn);
    return btn;
}

lv_obj_t* chip(lv_obj_t* parent, const char* text, lv_color_t c) {
    lv_obj_t* obj = box(parent);
    lv_obj_set_style_radius(obj, layout::kRadiusSm, 0);
    lv_obj_set_style_bg_color(obj, c, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_20, 0);
    lv_obj_set_style_pad_hor(obj, 6, 0);
    lv_obj_set_style_pad_ver(obj, 2, 0);
    label(obj, font::caption(), c, text);
    return obj;
}

void add_touch_feedback(lv_obj_t* obj) {
    lv_obj_add_event_cb(obj, feedback_cb, LV_EVENT_CLICKED, nullptr);
}

void set_text(lv_obj_t* lbl, const char* text) {
    if (lbl == nullptr || text == nullptr) return;
    const char* current = lv_label_get_text(lbl);
    if (current == nullptr || strcmp(current, text) != 0) {
        lv_label_set_text(lbl, text);
    }
}

void set_text_color(lv_obj_t* obj, lv_color_t c) {
    if (obj == nullptr) return;
    lv_obj_set_style_text_color(obj, c, 0);
}

void set_visible(lv_obj_t* obj, bool visible) {
    if (obj == nullptr) return;
    if (visible) {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

}  // namespace ui
#endif // UNIT_TEST
