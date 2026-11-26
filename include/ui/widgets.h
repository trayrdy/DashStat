#pragma once
#include <lvgl.h>
#include "ui/theme.h"

namespace ui {

lv_obj_t* box(lv_obj_t* parent);
lv_obj_t* row(lv_obj_t* parent, int32_t gap = layout::kGap);
lv_obj_t* column(lv_obj_t* parent, int32_t gap = layout::kGap);

lv_obj_t* label(lv_obj_t* parent, const lv_font_t* font, lv_color_t color, const char* text = "");
void single_line(lv_obj_t* label);

lv_obj_t* icon(lv_obj_t* parent, const char* glyph, const lv_font_t* font, lv_color_t color);
lv_obj_t* card(lv_obj_t* parent);

enum class ButtonStyle : uint8_t { Primary, Secondary, Danger, Ghost };
lv_obj_t* button(lv_obj_t* parent, const char* glyph, const char* text, ButtonStyle style,
                 lv_event_cb_t on_click, void* user);
lv_obj_t* icon_button(lv_obj_t* parent, const char* glyph, int32_t size, lv_event_cb_t on_click, void* user);

lv_obj_t* chip(lv_obj_t* parent, const char* text, lv_color_t color);

void add_touch_feedback(lv_obj_t* obj);
void set_text(lv_obj_t* label, const char* text);
void set_text_color(lv_obj_t* obj, lv_color_t color);
void set_visible(lv_obj_t* obj, bool visible);

}  // namespace ui
