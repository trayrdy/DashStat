#pragma once
#include <lvgl.h>
#include <stdint.h>
#include "ui/fonts/ui_fonts.h"
#include "ui/fonts/ui_icons.h"
#include "ui/theme/theme_manager.h"
#include "model/settings.h"

namespace ui {

// ---- Layout Tokens (320 x 240 Landscape) ------------------------------------
namespace layout {
constexpr int32_t kScreenW = 320;
constexpr int32_t kScreenH = 240;
constexpr int32_t kRailW = 56;     ///< Left vertical navigation rail
constexpr int32_t kStatusH = 28;   ///< Top status bar height
constexpr int32_t kContentW = kScreenW - kRailW; // 264 px
constexpr int32_t kContentH = kScreenH - kStatusH; // 212 px
constexpr int32_t kNavItemH = kScreenH / 5; // 48 px

constexpr int32_t kPad = 8;
constexpr int32_t kGap = 6;
constexpr int32_t kRadius = 10;
constexpr int32_t kRadiusSm = 6;
constexpr int32_t kTouchMin = 40;
constexpr int32_t kRowH = 44;
constexpr int32_t kButtonH = 40;
}  // namespace layout

// ---- Color Tokens -----------------------------------------------------------
namespace color {
inline lv_color_t bg() { return lv_color_hex(0x06080C); }
inline lv_color_t surface() { return lv_color_hex(0x0C1016); }
inline lv_color_t card() { return lv_color_hex(0x131922); }
inline lv_color_t card_hi() { return lv_color_hex(0x1C2430); }
inline lv_color_t stroke() { return lv_color_hex(0x232C39); }
inline lv_color_t track() { return lv_color_hex(0x1A212B); }
inline lv_color_t text() { return lv_color_hex(0xE9EEF4); }
inline lv_color_t text_dim() { return lv_color_hex(0x8B97A7); }
inline lv_color_t text_faint() { return lv_color_hex(0x4F5B6C); }
inline lv_color_t ok() { return lv_color_hex(0x2ED47A); }
inline lv_color_t info() { return lv_color_hex(0x3DA5FF); }
inline lv_color_t warn() { return lv_color_hex(0xFFB020); }
inline lv_color_t crit() { return lv_color_hex(0xFF4B4B); }
inline lv_color_t on_accent() { return lv_color_hex(0x071017); }

inline lv_color_t accent() {
    return lv_color_hex(ThemeManager::getPalette().accent);
}
}  // namespace color

// ---- Typography Tokens ------------------------------------------------------
namespace font {
inline const lv_font_t* caption() { return &ui_font_12; }
inline const lv_font_t* body() { return &ui_font_14; }
inline const lv_font_t* title() { return &ui_font_16; }
inline const lv_font_t* subtitle() { return &ui_font_18; }
inline const lv_font_t* value() { return &ui_font_20; }
inline const lv_font_t* large() { return &ui_font_28; }
inline const lv_font_t* speed() { return &ui_font_speed; }
inline const lv_font_t* icon24() { return &ui_icons_24; }
inline const lv_font_t* icon48() { return &ui_icons_48; }

inline const lv_font_t* speedFont(SpeedFont font) {
    switch (font) {
        case SpeedFont::BOLDPIXELS:  return &ui_font_speed_boldpixels;
        case SpeedFont::DIGITTECH:   return &ui_font_speed_digittech;
        case SpeedFont::FASTER_ONE:  return &ui_font_speed_fasterone;
        case SpeedFont::KINESTHESIA: return &ui_font_speed_kinesthesia;
        case SpeedFont::PIXEL:       return &ui_font_speed_pixel;
        case SpeedFont::REN:         return &ui_font_speed_ren;
        case SpeedFont::UNIWARS:     return &ui_font_speed_uniwars;
        case SpeedFont::DEFAULT_FONT:
        default:                     return &ui_font_speed;
    }
}

inline const char* speedFontName(SpeedFont font) {
    switch (font) {
        case SpeedFont::DEFAULT_FONT: return "[ DEFAULT ]";
        case SpeedFont::BOLDPIXELS:   return "[ BOLD PIXEL ]";
        case SpeedFont::DIGITTECH:    return "[ DIGITTECH ]";
        case SpeedFont::FASTER_ONE:   return "[ FASTER ONE ]";
        case SpeedFont::KINESTHESIA:  return "[ KINESTHESIA ]";
        case SpeedFont::PIXEL:        return "[ PIXEL ]";
        case SpeedFont::REN:          return "[ REN ]";
        case SpeedFont::UNIWARS:      return "[ UNIWARS ]";
        default:                      return "[ DEFAULT ]";
    }
}
}  // namespace font

}  // namespace ui
