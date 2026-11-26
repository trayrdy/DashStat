#ifndef UNIT_TEST
#include <Arduino.h>
#include <cstdio>
#include "ui/components/status_bar.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace ui {
namespace {
constexpr uint32_t kUpdatePeriodMs = 500;
}  // namespace

void StatusBar::create(lv_obj_t* parent, lv_event_cb_t onBack, void* user) {
    root_ = box(parent);
    lv_obj_set_pos(root_, layout::kRailW, 0);
    lv_obj_set_size(root_, layout::kContentW, layout::kStatusH);
    lv_obj_set_style_bg_color(root_, color::surface(), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(root_, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(root_, 1, 0);
    lv_obj_set_style_border_color(root_, color::stroke(), 0);
    lv_obj_set_style_pad_left(root_, 10, 0);
    lv_obj_set_style_pad_right(root_, 10, 0);
    lv_obj_set_flex_flow(root_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(root_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(root_, 8, 0);

    back_ = icon_button(root_, ICON_BACK, 26, onBack, user);
    lv_obj_set_ext_click_area(back_, 10);
    set_visible(back_, false);

    title_ = label(root_, font::title(), color::text(), "DASHSTAT");
    lv_obj_set_flex_grow(title_, 1);
    single_line(title_);

    mil_ = icon(root_, ICON_ENGINE, font::body(), color::warn());
    lock_ = label(root_, font::caption(), color::text_faint(), "[Safety Lock]");
    alert_ = icon(root_, ICON_ALERT, font::body(), color::warn());
    link_ = icon(root_, ICON_LINK_OFF, font::icon24(), color::text_faint());
    clock_ = label(root_, font::title(), color::text(), "00:00");

    set_visible(mil_, false);
    set_visible(lock_, false);
    set_visible(alert_, false);
    set_visible(link_, true);
    set_visible(clock_, true);
}

void StatusBar::setTitle(const char* title) {
    set_text(title_, title);
}

void StatusBar::showBack(bool show) {
    set_visible(back_, show);
}

void StatusBar::setDrivingLock(bool locked, bool flashing) {
    if (lock_ == nullptr) return;
    set_visible(lock_, locked);
    if (locked) {
        lv_obj_set_style_text_color(lock_, flashing ? lv_color_hex(0xFFB300) : color::text_faint(), 0);
    }
}

void StatusBar::setLayout(int32_t x, int32_t w) {
    if (root_ != nullptr) {
        lv_obj_set_pos(root_, x, 0);
        lv_obj_set_size(root_, w, layout::kStatusH);
    }
}

void StatusBar::update(const VehicleState& state, uint32_t nowMs) {
    if (nowMs - lastUpdateMs_ < kUpdatePeriodMs) return;
    lastUpdateMs_ = nowMs;
    blink_ = !blink_;

    // 1. Bluetooth Link Status
    if (state.isConnected) {
        set_text(link_, ICON_LINK);
        set_text_color(link_, color::ok());
    } else {
        set_text(link_, ICON_LINK_OFF);
        set_text_color(link_, color::text_faint());
    }

    // 2. Uptime Clock (mm:ss)
    char timeBuf[12];
    uint32_t sec = nowMs / 1000;
    uint32_t min = (sec / 60) % 60;
    uint32_t hr = (sec / 3600);
    if (hr > 0) {
        snprintf(timeBuf, sizeof(timeBuf), "%u:%02u:%02u", (unsigned)hr, (unsigned)min, (unsigned)(sec % 60));
    } else {
        snprintf(timeBuf, sizeof(timeBuf), "%02u:%02u", (unsigned)min, (unsigned)(sec % 60));
    }
    set_text(clock_, timeBuf);

    // 3. Check Engine Lamp (MIL)
    set_visible(mil_, state.milActive);
    if (state.milActive) {
        set_text_color(mil_, blink_ ? color::crit() : color::warn());
    }

    // 4. Coolant warning indicator
    bool highTemp = (state.coolantTempC >= 105);
    set_visible(alert_, highTemp);
    if (highTemp) {
        set_text_color(alert_, blink_ ? color::crit() : color::text_faint());
    }
}

}  // namespace ui
#endif // UNIT_TEST
