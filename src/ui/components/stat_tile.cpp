#ifndef UNIT_TEST
#include <Arduino.h>
#include <cstdio>

#include "ui/components/stat_tile.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace ui {

void StatTile::create(lv_obj_t* parent, bool align_right, int32_t width, bool title_on_top) {
    const lv_flex_align_t side = align_right ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START;

    root_ = column(parent, 0);
    lv_obj_set_size(root_, width, kHeight);
    lv_obj_set_clickable(root_, false);
    lv_obj_set_flex_align(root_, LV_FLEX_ALIGN_START, side, side);

    auto make_caption = [&]() {
        lv_obj_t* caption_line = row(root_, 3);
        lv_obj_set_flex_align(caption_line, side, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        icon_ = icon(caption_line, ICON_INFO, font::caption(), color::text_dim());
        caption_ = label(caption_line, font::caption(), color::text_faint(), "");
        lv_label_set_long_mode(caption_, LV_LABEL_LONG_MODE_CLIP);
    };

    auto make_value = [&]() {
        lv_obj_t* line = row(root_, 2);
        lv_obj_set_flex_align(line, side, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
        value_ = label(line, font::value(), color::text(), "--");
        unit_ = label(line, font::caption(), color::text_dim(), "");
        lv_obj_set_style_pad_bottom(unit_, 3, 0);
    };

    if (title_on_top) {
        make_caption();
        make_value();
    } else {
        make_value();
        make_caption();
    }
}

void StatTile::set_title(const char* caption, const char* icon_glyph) {
    if (caption != nullptr) {
        set_text(caption_, caption);
    }
    if (icon_glyph != nullptr) {
        set_text(icon_, icon_glyph);
    }
}

void StatTile::set_value(const char* value, const char* unit) {
    set_text(value_, value != nullptr ? value : "--");
    if (unit != nullptr) {
        set_text(unit_, unit);
    }
}

void StatTile::set_value(const char* value, const char* unit, lv_color_t value_color) {
    set_value(value, unit);
    set_text_color(value_, value_color);
}

void StatTile::set_value_color(lv_color_t color) {
    uint32_t u32 = lv_color_to_u32(color);
    if (u32 != lastColorU32_) {
        lastColorU32_ = u32;
        set_text_color(value_, color);
    }
}

}  // namespace ui
#endif  // UNIT_TEST
