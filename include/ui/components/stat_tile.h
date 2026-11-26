/**
 * @file stat_tile.h
 * Compact corner telemetry tile used in dashboard corners:
 *
 *   Line 1: 92 °C
 *   Line 2: [icon] COOLANT
 *
 * Lightweight, zero dynamic allocation, supports left/right alignment.
 */
#pragma once

#include <stdint.h>

#ifndef UNIT_TEST
#include <lvgl.h>

namespace ui {

class StatTile {
public:
    static constexpr int32_t kWidth = 76;
    static constexpr int32_t kHeight = 38;

    void create(lv_obj_t* parent, bool align_right = false, int32_t width = kWidth, bool title_on_top = false);
    void set_title(const char* caption, const char* icon_glyph = nullptr);
    void set_value(const char* value, const char* unit = nullptr);
    void set_value(const char* value, const char* unit, lv_color_t value_color);
    void set_value_color(lv_color_t color);

    lv_obj_t* root() const { return root_; }

private:
    lv_obj_t* root_ = nullptr;
    lv_obj_t* icon_ = nullptr;
    lv_obj_t* value_ = nullptr;
    lv_obj_t* unit_ = nullptr;
    lv_obj_t* caption_ = nullptr;
    uint32_t lastColorU32_ = 0xFFFFFFFF;
};

}  // namespace ui
#endif  // UNIT_TEST
