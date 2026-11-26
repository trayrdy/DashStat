#ifndef UNIT_TEST
#include "ui/pages/trip_page.h"
#include <cstdio>
#include "ui/shell/ui_shell.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "hal/buzzer_controller.h"

namespace ui {

namespace {

const char* const kModeTitles[] = {
    "0-100 KM/H",
    "1/4 MILE",
    "1 MILE"
};

const char* const kModeIcons[] = {
    ICON_STOPWATCH,
    ICON_ROAD,
    ICON_SPEED
};

}  // namespace

void TripPage::create(lv_obj_t* parent) {
    root_ = box(parent);
    lv_obj_set_size(root_, layout::kContentW, layout::kContentH);
    lv_obj_set_pos(root_, 0, 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_TRANSP, 0);

    // =========================================================================
    // 1. DEFAULT SPLIT VIEW (Record Hub on Left, Trip Telemetry on Right)
    // =========================================================================
    splitContainer_ = box(root_);
    lv_obj_set_size(splitContainer_, layout::kContentW, layout::kContentH);
    lv_obj_set_pos(splitContainer_, 0, 0);
    lv_obj_set_style_pad_all(splitContainer_, layout::kPad, 0);
    lv_obj_set_flex_flow(splitContainer_, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(splitContainer_, layout::kGap, 0);

    // ---- Left Column: Record Hub (3 Mode Buttons) ----
    leftCol_ = column(splitContainer_, 5);
    lv_obj_t* leftCol = leftCol_;
    lv_obj_set_size(leftCol, 120, 196);

    for (uint8_t i = 0; i < static_cast<uint8_t>(SprintMode::COUNT); ++i) {
        lv_obj_t* btn = lv_button_create(leftCol);
        modeButtons_[i] = btn;
        lv_obj_remove_style_all(btn);
        lv_obj_set_size(btn, 120, 60);
        lv_obj_set_style_radius(btn, layout::kRadiusSm + 2, 0);
        lv_obj_set_style_bg_color(btn, color::card(), 0);
        lv_obj_set_style_bg_color(btn, color::card_hi(), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_hor(btn, 5, 0);
        lv_obj_set_style_pad_ver(btn, 4, 0);
        lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_row(btn, 1, 0);

        // Header Row: Icon + Title
        lv_obj_t* btnRow = row(btn, 4);
        icon(btnRow, kModeIcons[i], font::title(), color::text());
        label(btnRow, font::subtitle(), color::text(), kModeTitles[i]);

        // Subtitle: Best time
        modeSubLabels_[i] = label(btn, font::title(), color::accent(), "BEST: --.- s");

        lv_obj_set_user_data(btn, reinterpret_cast<void*>(static_cast<intptr_t>(i)));
        lv_obj_add_event_cb(btn, onModeSelectClicked, LV_EVENT_CLICKED, this);
        add_touch_feedback(btn);
    }

    // ---- Right Column: Trip Telemetry (3 Cards + 1 Button) ----
    rightCol_ = column(splitContainer_, 4);
    lv_obj_t* rightCol = rightCol_;
    lv_obj_set_size(rightCol, 122, 196);

    // Card 1: Distance
    telemetryCards_[0] = card(rightCol);
    lv_obj_t* c1 = telemetryCards_[0];
    lv_obj_set_size(c1, 122, 48);
    lv_obj_set_style_pad_hor(c1, 4, 0);
    lv_obj_set_style_pad_ver(c1, 3, 0);
    lv_obj_set_flex_flow(c1, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(c1, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(c1, 1, 0);
    label(c1, font::title(), color::text_dim(), "DISTANCE");
    distValLabel_ = label(c1, font::subtitle(), color::text(), "0.0 km");

    // Card 2: Avg / Max Speed
    telemetryCards_[1] = card(rightCol);
    lv_obj_t* c2 = telemetryCards_[1];
    lv_obj_set_size(c2, 122, 48);
    lv_obj_set_style_pad_hor(c2, 4, 0);
    lv_obj_set_style_pad_ver(c2, 3, 0);
    lv_obj_set_flex_flow(c2, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(c2, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(c2, 1, 0);
    label(c2, font::title(), color::text_dim(), "AVG / MAX SPEED");
    speedValLabel_ = label(c2, font::subtitle(), color::text(), "0 / 0 km/h");

    // Card 3: Drive Time
    telemetryCards_[2] = card(rightCol);
    lv_obj_t* c3 = telemetryCards_[2];
    lv_obj_set_size(c3, 122, 48);
    lv_obj_set_style_pad_hor(c3, 4, 0);
    lv_obj_set_style_pad_ver(c3, 3, 0);
    lv_obj_set_flex_flow(c3, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(c3, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(c3, 1, 0);
    label(c3, font::title(), color::text_dim(), "DRIVE TIME");
    timeValLabel_ = label(c3, font::subtitle(), color::text(), "00:00:00");

    // Reset Trip Button
    resetTripBtn_ = button(rightCol, ICON_RESTORE, "RESET TRIP", ButtonStyle::Secondary, onResetTripClicked, this);
    lv_obj_set_size(resetTripBtn_, 122, 36);
    lv_obj_set_style_pad_hor(resetTripBtn_, 6, 0);

    // =========================================================================
    // 2. FULL SCREEN SUB-VIEW (Motorsport Telemetry HUD)
    // =========================================================================
    fullContainer_ = box(root_);
    lv_obj_set_size(fullContainer_, layout::kContentW, layout::kContentH);
    lv_obj_set_pos(fullContainer_, 0, 0);
    lv_obj_set_style_pad_all(fullContainer_, layout::kPad, 0);
    lv_obj_set_flex_flow(fullContainer_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(fullContainer_, 4, 0);
    set_visible(fullContainer_, false);

    // ---- Top Bar: Back Button + Title + Status Chip ----
    hudTopBar_ = row(fullContainer_, 4);
    lv_obj_t* topBar = hudTopBar_;
    lv_obj_set_size(topBar, 248, 32);
    lv_obj_set_flex_align(topBar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    hudBackBtn_ = button(topBar, ICON_BACK, "BACK", ButtonStyle::Ghost, onBackClicked, this);
    lv_obj_set_height(hudBackBtn_, 30);
    lv_obj_set_style_pad_hor(hudBackBtn_, 5, 0);

    hudTitleLabel_ = label(topBar, font::title(), color::text(), "0-100 KM/H");
    lv_obj_set_flex_grow(hudTitleLabel_, 1);

    hudStatusChip_ = box(topBar);
    lv_obj_set_style_radius(hudStatusChip_, layout::kRadiusSm, 0);
    lv_obj_set_style_bg_color(hudStatusChip_, color::text_dim(), 0);
    lv_obj_set_style_bg_opa(hudStatusChip_, LV_OPA_30, 0);
    lv_obj_set_style_pad_hor(hudStatusChip_, 6, 0);
    lv_obj_set_style_pad_ver(hudStatusChip_, 3, 0);
    hudStatusLabel_ = label(hudStatusChip_, font::title(), color::text_dim(), "READY");

    // ---- Center Section: Digital Stopwatch & Progress & Trap Speed ----
    hudCenterBox_ = card(fullContainer_);
    lv_obj_t* centerBox = hudCenterBox_;
    lv_obj_set_size(centerBox, 248, 118);
    lv_obj_set_style_pad_all(centerBox, 4, 0);
    lv_obj_set_flex_flow(centerBox, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(centerBox, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(centerBox, 4, 0);

    // Stopwatch Digits Row
    lv_obj_t* digitsRow = row(centerBox, 4);
    lv_obj_set_flex_align(digitsRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
    hudDigitsLabel_ = label(digitsRow, font::speed(), color::text(), "00.00");
    hudUnitLabel_ = label(digitsRow, font::value(), color::accent(), "s");

    // Progress Bar
    hudProgressBar_ = lv_bar_create(centerBox);
    lv_obj_set_size(hudProgressBar_, 224, 8);
    lv_obj_set_style_bg_color(hudProgressBar_, color::track(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(hudProgressBar_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(hudProgressBar_, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(hudProgressBar_, color::accent(), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(hudProgressBar_, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(hudProgressBar_, 4, LV_PART_INDICATOR);

    // Trap Speed Label
    hudTrapLabel_ = label(centerBox, font::subtitle(), color::text_dim(), "TRAP SPEED: --.- km/h");

    // ---- Bottom Bar: Best vs Last & Reset Best Button ----
    hudBotBar_ = row(fullContainer_, 4);
    lv_obj_t* botBar = hudBotBar_;
    lv_obj_set_size(botBar, 248, 32);
    lv_obj_set_flex_align(botBar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* statsRow = row(botBar, 6);
    hudBestLabel_ = label(statsRow, font::title(), color::accent(), "BEST: --.-s");
    hudLastLabel_ = label(statsRow, font::title(), color::text_dim(), "LAST: --.-s");

    hudResetBestBtn_ = button(botBar, ICON_RESTORE, "RESET", ButtonStyle::Ghost, onResetBestClicked, this);
    lv_obj_set_height(hudResetBestBtn_, 30);
    lv_obj_set_style_pad_hor(hudResetBestBtn_, 6, 0);
}

void TripPage::onShow() {
    lastSplitUpdateMs_ = 0;
    if (isFullView_) {
        updateSprintHud(VehicleState{}, 0, true);
    } else {
        updateSplitView(VehicleState{}, 0);
    }
}

void TripPage::onHide() {
    if (isFullView_) {
        closeSprintMode();
    }
}

bool TripPage::onBack() {
    if (isFullView_) {
        closeSprintMode();
        return true;
    }
    return false;
}

void TripPage::openSprintMode(SprintMode mode) {
    if (mode >= SprintMode::COUNT) return;
    activeMode_ = mode;
    TripComputer::instance().setSprintMode(mode);

    // Set HUD title & progress bar range according to mode
    if (mode == SprintMode::SPEED_0_100) {
        set_text(hudTitleLabel_, "0-100 KM/H");
        lv_bar_set_range(hudProgressBar_, 0, 100);
    } else if (mode == SprintMode::DISTANCE_QUARTER_MILE) {
        set_text(hudTitleLabel_, "1/4 MILE (402m)");
        lv_bar_set_range(hudProgressBar_, 0, 402);
    } else if (mode == SprintMode::DISTANCE_ONE_MILE) {
        set_text(hudTitleLabel_, "1 MILE (1.6km)");
        lv_bar_set_range(hudProgressBar_, 0, 1609);
    }

    set_visible(splitContainer_, false);
    set_visible(fullContainer_, true);
    isFullView_ = true;
    updateSprintHud(VehicleState{}, 0, true);
}

void TripPage::closeSprintMode() {
    set_visible(fullContainer_, false);
    set_visible(splitContainer_, true);
    isFullView_ = false;
    lastSplitUpdateMs_ = 0;
    TripComputer::instance().setSprintActive(false);
}

void TripPage::resetTrip() {
    TripComputer::instance().resetTrip();
    lastSplitUpdateMs_ = 0;
}

void TripPage::resetSprintBest() {
    TripComputer::instance().resetSprintBest(activeMode_);
    updateSprintHud(VehicleState{}, 0, true);
}

void TripPage::onModeSelectClicked(lv_event_t* e) {
    if (UiShell::instance().isDrivingLocked()) {
        UiShell::instance().triggerLockFlash();
        return;
    }
    auto* page = static_cast<TripPage*>(lv_event_get_user_data(e));
    if (page == nullptr) return;
    auto* btn = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    auto modeIdx = reinterpret_cast<intptr_t>(lv_obj_get_user_data(btn));
    page->openSprintMode(static_cast<SprintMode>(modeIdx));
}

void TripPage::onBackClicked(lv_event_t* e) {
    auto* page = static_cast<TripPage*>(lv_event_get_user_data(e));
    if (page == nullptr) return;
    page->closeSprintMode();
}

void TripPage::onResetTripClicked(lv_event_t* e) {
    if (UiShell::instance().isDrivingLocked()) {
        UiShell::instance().triggerLockFlash();
        return;
    }
    auto* page = static_cast<TripPage*>(lv_event_get_user_data(e));
    if (page == nullptr) return;
    page->resetTrip();
}

void TripPage::onResetBestClicked(lv_event_t* e) {
    if (UiShell::instance().isDrivingLocked()) {
        UiShell::instance().triggerLockFlash();
        return;
    }
    auto* page = static_cast<TripPage*>(lv_event_get_user_data(e));
    if (page == nullptr) return;
    page->resetSprintBest();
}

void TripPage::update(const VehicleState& state, uint32_t nowMs) {
    if (isFullView_) {
        updateSprintHud(state, nowMs);
    } else {
        if (nowMs - lastSplitUpdateMs_ >= 500) {
            lastSplitUpdateMs_ = nowMs;
            updateSplitView(state, nowMs);
        }
    }
}

void TripPage::updateSplitView(const VehicleState&, uint32_t) {
    const auto& telem = TripComputer::instance().getTelemetry();
    char buf[32];

    // Distance
    snprintf(buf, sizeof(buf), "%.1f km", telem.distanceKm);
    set_text(distValLabel_, buf);

    // Avg / Max Speed
    snprintf(buf, sizeof(buf), "%.0f / %.0f km/h", TripComputer::instance().getAverageSpeedKmh(), telem.maxSpeedKmh);
    set_text(speedValLabel_, buf);

    // Drive Time (HH:MM:SS)
    uint32_t s = telem.driveTimeSec;
    uint32_t hh = s / 3600;
    uint32_t mm = (s % 3600) / 60;
    uint32_t ss = s % 60;
    snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu", (unsigned long)hh, (unsigned long)mm, (unsigned long)ss);
    set_text(timeValLabel_, buf);

    // Mode Buttons: Best Records Preview
    for (uint8_t i = 0; i < static_cast<uint8_t>(SprintMode::COUNT); ++i) {
        const auto& rec = TripComputer::instance().getSprintRecord(static_cast<SprintMode>(i));
        if (rec.bestTimeSec > 0.0f) {
            snprintf(buf, sizeof(buf), "BEST: %.2f s", rec.bestTimeSec);
        } else {
            snprintf(buf, sizeof(buf), "BEST: --.- s");
        }
        set_text(modeSubLabels_[i], buf);
    }
}

void TripPage::updateSprintHud(const VehicleState& state, uint32_t nowMs, bool force) {
    AccelTimerState curState = TripComputer::instance().getAccelState();
    const auto& rec = TripComputer::instance().getSprintRecord(activeMode_);
    char buf[32];

    // 1. Status Chip Badge Update (only on state change or force)
    if (curState != lastAccelState_ || force) {
        lastAccelState_ = curState;
        lv_color_t chipColor = color::text_dim();
        const char* statusStr = "READY";

        switch (curState) {
            case AccelTimerState::IDLE:
                chipColor = color::text_dim();
                statusStr = "READY";
                break;
            case AccelTimerState::ARMED:
                chipColor = color::warn();
                statusStr = "ARMED";
                break;
            case AccelTimerState::RUNNING:
                chipColor = color::ok();
                statusStr = "RUNNING";
                break;
            case AccelTimerState::FINISHED:
                chipColor = color::accent();
                statusStr = "FINISHED";
                break;
        }

        set_text(hudStatusLabel_, statusStr);
        set_text_color(hudStatusLabel_, chipColor);
        lv_obj_set_style_bg_color(hudStatusChip_, chipColor, 0);
    }

    // Diagnostic Check Engine (MIL) Warning Alert
    if (state.milActive) {
        set_text(hudStatusLabel_, "MIL ALERT!");
        set_text_color(hudStatusLabel_, color::crit());
        lv_obj_set_style_bg_color(hudStatusChip_, color::crit(), 0);
    }

    // 2. Stopwatch Digits, Progress Bar, & Trap Speed
    if (curState == AccelTimerState::RUNNING) {
        float elapsedSec = TripComputer::instance().getCurrentRunSeconds(nowMs);
        snprintf(buf, sizeof(buf), "%05.2f", elapsedSec);
        set_text(hudDigitsLabel_, buf);

        if (activeMode_ == SprintMode::SPEED_0_100) {
            lv_bar_set_value(hudProgressBar_, state.speedKmh, LV_ANIM_OFF);
        } else {
            float distM = TripComputer::instance().getCurrentRunDistanceM();
            lv_bar_set_value(hudProgressBar_, static_cast<int32_t>(distM), LV_ANIM_OFF);
        }

        snprintf(buf, sizeof(buf), "SPEED: %u km/h", state.speedKmh);
        set_text(hudTrapLabel_, buf);
    } else if (curState == AccelTimerState::FINISHED) {
        snprintf(buf, sizeof(buf), "%05.2f", rec.lastTimeSec);
        set_text(hudDigitsLabel_, buf);

        if (activeMode_ == SprintMode::SPEED_0_100) {
            lv_bar_set_value(hudProgressBar_, 100, LV_ANIM_OFF);
        } else if (activeMode_ == SprintMode::DISTANCE_QUARTER_MILE) {
            lv_bar_set_value(hudProgressBar_, 402, LV_ANIM_OFF);
        } else if (activeMode_ == SprintMode::DISTANCE_ONE_MILE) {
            lv_bar_set_value(hudProgressBar_, 1609, LV_ANIM_OFF);
        }

        snprintf(buf, sizeof(buf), "TRAP SPEED: %.1f km/h", rec.lastTrapSpeedKmh);
        set_text(hudTrapLabel_, buf);
    } else if (curState == AccelTimerState::ARMED) {
        set_text(hudDigitsLabel_, "00.00");
        lv_bar_set_value(hudProgressBar_, 0, LV_ANIM_OFF);
        set_text(hudTrapLabel_, "LAUNCH READY - HIT GAS");
    } else { // IDLE
        if (rec.lastTimeSec > 0.0f) {
            snprintf(buf, sizeof(buf), "%05.2f", rec.lastTimeSec);
            set_text(hudDigitsLabel_, buf);
            snprintf(buf, sizeof(buf), "LAST TRAP: %.1f km/h", rec.lastTrapSpeedKmh);
            set_text(hudTrapLabel_, buf);
        } else {
            set_text(hudDigitsLabel_, "00.00");
            set_text(hudTrapLabel_, "STOP CAR TO ARM TIMER");
        }
        lv_bar_set_value(hudProgressBar_, 0, LV_ANIM_OFF);
    }

    // 3. Bottom Bar: Best & Last Labels
    if (rec.bestTimeSec > 0.0f) {
        snprintf(buf, sizeof(buf), "BEST: %.2f s", rec.bestTimeSec);
    } else {
        snprintf(buf, sizeof(buf), "BEST: --.- s");
    }
    set_text(hudBestLabel_, buf);

    if (rec.lastTimeSec > 0.0f) {
        snprintf(buf, sizeof(buf), "LAST: %.2f s", rec.lastTimeSec);
    } else {
        snprintf(buf, sizeof(buf), "LAST: --.- s");
    }
    set_text(hudLastLabel_, buf);
}

void TripPage::onLayout(int32_t contentW) {
    if (root_ == nullptr) return;
    lv_obj_set_size(root_, contentW, layout::kContentH);
    if (splitContainer_ != nullptr) lv_obj_set_size(splitContainer_, contentW, layout::kContentH);
    if (fullContainer_ != nullptr) lv_obj_set_size(fullContainer_, contentW, layout::kContentH);

    // Split View column widths
    const int32_t leftW = (contentW >= 320) ? 148 : 120;
    const int32_t rightW = (contentW >= 320) ? 150 : 122;

    if (leftCol_ != nullptr) lv_obj_set_width(leftCol_, leftW);
    for (uint8_t i = 0; i < static_cast<uint8_t>(SprintMode::COUNT); ++i) {
        if (modeButtons_[i] != nullptr) lv_obj_set_width(modeButtons_[i], leftW);
    }

    if (rightCol_ != nullptr) lv_obj_set_width(rightCol_, rightW);
    for (uint8_t i = 0; i < 3; ++i) {
        if (telemetryCards_[i] != nullptr) lv_obj_set_width(telemetryCards_[i], rightW);
    }
    if (resetTripBtn_ != nullptr) lv_obj_set_width(resetTripBtn_, rightW);

    // Full HUD container elements
    const int32_t hudW = contentW - 16;
    if (hudTopBar_ != nullptr) lv_obj_set_width(hudTopBar_, hudW);
    if (hudCenterBox_ != nullptr) lv_obj_set_width(hudCenterBox_, hudW);
    if (hudProgressBar_ != nullptr) lv_obj_set_width(hudProgressBar_, hudW - 24);
    if (hudBotBar_ != nullptr) lv_obj_set_width(hudBotBar_, hudW);
}

void TripPage::applyTheme() {
    if (root_ == nullptr) return;
    lv_obj_set_style_bg_color(root_, color::bg(), 0);
    for (uint8_t i = 0; i < static_cast<uint8_t>(SprintMode::COUNT); ++i) {
        if (modeSubLabels_[i] != nullptr) {
            lv_obj_set_style_text_color(modeSubLabels_[i], color::accent(), 0);
        }
    }
    if (hudUnitLabel_ != nullptr) {
        lv_obj_set_style_text_color(hudUnitLabel_, color::accent(), 0);
    }
    if (hudProgressBar_ != nullptr) {
        lv_obj_set_style_bg_color(hudProgressBar_, color::accent(), LV_PART_INDICATOR);
    }
    if (hudBestLabel_ != nullptr) {
        lv_obj_set_style_text_color(hudBestLabel_, color::accent(), 0);
    }
}

}  // namespace ui
#endif  // !UNIT_TEST
