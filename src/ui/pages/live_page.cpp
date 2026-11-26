#ifndef UNIT_TEST
#include <Arduino.h>
#include <cstdio>
#include <cmath>
#include "ui/pages/live_page.h"
#include "ui/shell/ui_shell.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "core/util.h"
#include "hal/buzzer_controller.h"
#include "obd/universal_pid_catalog.h"
#include "model/settings_manager.h"
#include "model/telemetry_sync.h"

namespace ui {

namespace {

constexpr uint32_t kGridUpdateMs = 100;    // 10 Hz telemetry update
constexpr uint32_t kSampleMs = 100;        // 10 Hz chart & detail update

static uint8_t getSensorDecimals(const UniversalPidDef& def) {
    if (def.formula == FormulaType::LAMBDA_AB || def.formula == FormulaType::O2_VOLTAGE) {
        return 2;
    }
    if (def.pidHex == 0xF0 ||
        def.formula == FormulaType::FUEL_RATE ||
        def.formula == FormulaType::SPECIAL_ATRV ||
        def.formula == FormulaType::VOLTAGE_AB_DIV_1000 ||
        def.formula == FormulaType::PERCENT_A_TRIM ||
        def.formula == FormulaType::AB_DIV_100) {
        return 1;
    }
    return 0;
}

static inline uint8_t getSensorAlert(const UniversalPidDef& def, float val) {
    return UniversalPidCatalog::getAlertLevel(def.pidHex, val);
}

static void formatSensorValue(char* buf, size_t bufSize, float val, uint8_t decimals) {
    if (decimals == 2) {
        snprintf(buf, bufSize, "%.2f", val);
    } else if (decimals == 1) {
        snprintf(buf, bufSize, "%.1f", val);
    } else {
        snprintf(buf, bufSize, "%d", static_cast<int>(roundf(val)));
    }
}

static const char* getSensorIcon(const UniversalPidDef& def) {
    // 1. Tier 1: Exact specific PIDs
    switch (def.pidHex) {
        case 0x05: return ICON_COOLANT;       // Coolant Temp
        case 0xFF: return ICON_BATTERY;       // Battery Voltage (ATRV)
        case 0x42: return ICON_BATTERY;       // Control Module Voltage
        case 0x11: return ICON_THROTTLE;      // Throttle Position
        case 0xF0: return ICON_TURBO;         // Virtual Turbo Boost
        case 0x06: case 0x07: return ICON_TRIM; // Fuel Trims
        case 0x0E: return ICON_TIMING;        // Timing Advance
        case 0x0D: return ICON_SPEED;         // Speed
        case 0x1F: return ICON_TIMER;         // Engine Run Time
        default: break;
    }

    // 2. Tier 2: Category Fallback
    switch (def.category) {
        case PidCategory::TEMPERATURE:  return ICON_THERMOMETER;
        case PidCategory::AIR_PRESSURE: return ICON_AIR;
        case PidCategory::FUEL_SYSTEM:  return ICON_FUEL;
        case PidCategory::ELECTRICAL:   return ICON_FLASH;
        case PidCategory::IGNITION:     return ICON_TIMING;
        case PidCategory::OXYGEN:       return ICON_TRIM;
        case PidCategory::DIAGNOSTICS:  return ICON_DIAG;
        case PidCategory::ENGINE:       return ICON_ENGINE;
        default:                        return ICON_GAUGE;
    }
}

}  // namespace

void LivePage::onCardClicked(lv_event_t* e) {
    if (UiShell::instance().isDrivingLocked()) {
        UiShell::instance().triggerLockFlash();
        return;
    }
    auto* self = static_cast<LivePage*>(lv_obj_get_user_data(lv_event_get_current_target_obj(e)));
    uint8_t index = static_cast<uint8_t>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    Serial.printf("[LivePage] onCardClicked: index=%u\n", index);
    if (self != nullptr && index < kSensorCount) {
        self->openDetail(index);
    }
}

void LivePage::onResetClicked(lv_event_t* e) {
    if (UiShell::instance().isDrivingLocked()) {
        UiShell::instance().triggerLockFlash();
        return;
    }
    auto* self = static_cast<LivePage*>(lv_event_get_user_data(e));
    if (self != nullptr) {
        self->resetStats();
    }
}

void LivePage::create(lv_obj_t* parent) {
    root_ = box(parent);
    lv_obj_set_size(root_, layout::kContentW, layout::kContentH);
    lv_obj_set_style_pad_all(root_, 0, 0);

    // =========================================================================
    // 1. Grid View Container (264 x 212 px, 2 columns x 3 rows)
    // =========================================================================
    gridContainer_ = box(root_);
    lv_obj_set_size(gridContainer_, layout::kContentW, layout::kContentH);
    lv_obj_set_style_pad_all(gridContainer_, 0, 0);

    for (uint8_t i = 0; i < kSensorCount; ++i) {
        const uint8_t col = i % 2;
        const uint8_t rowIdx = i / 2;
        const int32_t x = 6 + col * (123 + 6);
        const int32_t y = 6 + rowIdx * (60 + 6);

        cards_[i].container = box(gridContainer_);
        lv_obj_set_pos(cards_[i].container, x, y);
        lv_obj_set_size(cards_[i].container, 123, 60);
        lv_obj_set_style_bg_color(cards_[i].container, color::card_hi(), 0);
        lv_obj_set_style_bg_opa(cards_[i].container, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(cards_[i].container, layout::kRadiusSm, 0);
        lv_obj_set_style_border_width(cards_[i].container, 1, 0);
        lv_obj_set_style_border_color(cards_[i].container, color::stroke(), 0);
        lv_obj_set_style_pad_all(cards_[i].container, 0, 0);
        lv_obj_set_clickable(cards_[i].container, true);
        lv_obj_set_style_bg_color(cards_[i].container, color::track(), LV_STATE_PRESSED);
        lv_obj_set_user_data(cards_[i].container, this);
        lv_obj_add_event_cb(cards_[i].container, onCardClicked, LV_EVENT_CLICKED,
                            reinterpret_cast<void*>(static_cast<intptr_t>(i)));
        add_touch_feedback(cards_[i].container);

        uint8_t catIdx = SettingsManager::instance().get().liveSlots[i];
        const auto& def = UniversalPidCatalog::get(catIdx);

        // Top Row: Icon + Label
        lv_obj_t* topRow = row(cards_[i].container, 4);
        cards_[i].topRow = topRow;
        lv_obj_set_pos(topRow, 8, 6);
        lv_obj_set_size(topRow, 107, 16);
        cards_[i].iconLabel = icon(topRow, getSensorIcon(def), font::body(), color::accent());
        cards_[i].nameLabel = label(topRow, font::caption(), color::text_dim(), def.name);
        lv_obj_set_flex_grow(cards_[i].nameLabel, 1);
        single_line(cards_[i].nameLabel);

        // Bottom Row: Large Value + Unit
        lv_obj_t* valRow = row(cards_[i].container, 4);
        cards_[i].valRow = valRow;
        lv_obj_set_pos(valRow, 8, 26);
        lv_obj_set_size(valRow, 107, 28);
        cards_[i].valueLabel = label(valRow, font::value(), color::text(), "--");
        cards_[i].unitLabel = label(valRow, font::caption(), color::text_faint(), def.unit);
        lv_obj_set_style_pad_top(cards_[i].unitLabel, 6, 0);
    }

    // =========================================================================
    // 2. Detail View Container (264 x 212 px, zero memory allocation)
    // =========================================================================
    detailContainer_ = box(root_);
    lv_obj_set_size(detailContainer_, layout::kContentW, layout::kContentH);
    lv_obj_set_style_pad_all(detailContainer_, 0, 0);
    set_visible(detailContainer_, false);

    // Top Header Card (252 x 58 px)
    detailHeader_ = card(detailContainer_);
    lv_obj_set_pos(detailHeader_, 6, 6);
    lv_obj_set_size(detailHeader_, 252, 58);
    lv_obj_set_style_pad_hor(detailHeader_, 10, 0);
    lv_obj_set_style_pad_ver(detailHeader_, 4, 0);

    lv_obj_t* leftCol = column(detailHeader_, 0);
    lv_obj_set_pos(leftCol, 8, 4);
    lv_obj_set_size(leftCol, 120, 50);

    detailName_ = label(leftCol, font::caption(), color::text_dim(), "");
    lv_obj_set_width(detailName_, 120);
    single_line(detailName_);

    lv_obj_t* valBox = row(leftCol, 4);
    detailVal_ = label(valBox, font::large(), color::accent(), "--");
    detailUnit_ = label(valBox, font::body(), color::text_dim(), "");
    lv_obj_set_style_pad_top(detailUnit_, 6, 0);

    // Right Stats 3-Column Box (MIN / AVG / MAX)
    lv_obj_t* statsBox = row(detailHeader_, 6);
    lv_obj_align(statsBox, LV_ALIGN_RIGHT_MID, -8, 0);
    lv_obj_set_size(statsBox, 112, 50);

    lv_obj_t* minCol = column(statsBox, 0);
    lv_obj_set_width(minCol, 34);
    label(minCol, font::caption(), color::text_faint(), "MIN");
    statMin_ = label(minCol, font::title(), color::text_dim(), "--");

    lv_obj_t* avgCol = column(statsBox, 0);
    lv_obj_set_width(avgCol, 34);
    label(avgCol, font::caption(), color::text_faint(), "AVG");
    statAvg_ = label(avgCol, font::title(), color::text_dim(), "--");

    lv_obj_t* maxCol = column(statsBox, 0);
    lv_obj_set_width(maxCol, 34);
    label(maxCol, font::caption(), color::text_faint(), "MAX");
    statMax_ = label(maxCol, font::title(), color::text_dim(), "--");

    // Bottom Chart Card (252 x 136 px)
    chartCard_ = card(detailContainer_);
    lv_obj_set_pos(chartCard_, 6, 70);
    lv_obj_set_size(chartCard_, 252, 136);
    lv_obj_set_style_pad_all(chartCard_, 4, 0);

    chart_ = lv_chart_create(chartCard_);
    lv_obj_set_size(chart_, 244, 128);
    lv_obj_set_pos(chart_, 0, 0);
    lv_obj_set_style_bg_opa(chart_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(chart_, 0, 0);
    lv_obj_set_style_pad_all(chart_, 2, 0);
    lv_obj_set_style_line_color(chart_, color::stroke(), LV_PART_MAIN);
    lv_obj_set_style_line_width(chart_, 1, LV_PART_MAIN);
    lv_obj_set_style_line_width(chart_, 2, LV_PART_ITEMS);
    lv_obj_set_style_size(chart_, 0, 0, LV_PART_INDICATOR);
    lv_chart_set_type(chart_, LV_CHART_TYPE_LINE);
    lv_chart_set_update_mode(chart_, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_point_count(chart_, 60);
    lv_chart_set_div_line_count(chart_, 3, 0);
    lv_obj_set_clickable(chart_, false);

    series_ = lv_chart_add_series(chart_, color::accent(), LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_all_values(chart_, series_, LV_CHART_POINT_NONE);

    resetBtn_ = button(chartCard_, ICON_RESTORE, nullptr, ButtonStyle::Ghost, onResetClicked, this);
    lv_obj_set_size(resetBtn_, 34, 30);
    lv_obj_set_style_pad_hor(resetBtn_, 0, 0);
    lv_obj_align(resetBtn_, LV_ALIGN_TOP_RIGHT, 0, 0);
}

void LivePage::onShow() {
    lastGridUpdateMs_ = 0;
    lastSampleMs_ = 0;
    const auto& settings = SettingsManager::instance().get();
    for (uint8_t i = 0; i < kSensorCount; ++i) {
        applySlotSensor(i, settings.liveSlots[i]);
    }
}

void LivePage::onHide() {
    if (isDetailOpen_) {
        closeDetail();
    }
}

bool LivePage::onBack() {
    if (isDetailOpen_) {
        closeDetail();
        return true;
    }
    return false;
}

void LivePage::openDetail(uint8_t index) {
    if (index >= kSensorCount) return;
    Serial.printf("[LivePage] openDetail: index=%u\n", index);
    selectedSensor_ = index;
    isDetailOpen_ = true;

    TelemetrySync::boostedSlot.store(static_cast<int8_t>(index)); // Smart Chart Boost!

    set_visible(gridContainer_, false);
    set_visible(detailContainer_, true);

    uint8_t catIdx = SettingsManager::instance().get().liveSlots[index];
    const auto& def = UniversalPidCatalog::get(catIdx);
    const char* pidName = def.name;

    set_text(detailName_, pidName);
    set_text(detailUnit_, def.unit);

    UiShell::instance().showBackButton(true);
    UiShell::instance().setTitle(pidName);

    resetStats();
}

void LivePage::closeDetail() {
    Serial.printf("[LivePage] closeDetail: isDetailOpen_=%d\n", isDetailOpen_);
    if (!isDetailOpen_) return;
    isDetailOpen_ = false;

    TelemetrySync::boostedSlot.store(-1); // Restore normal intervals!

    set_visible(detailContainer_, false);
    set_visible(gridContainer_, true);

    UiShell::instance().showBackButton(false);
    UiShell::instance().setTitle(title());
    lastGridUpdateMs_ = 0;
}

void LivePage::resetStats() {
    samples_ = 0;
    sum_ = 0.0f;
    min_ = 0.0f;
    max_ = 0.0f;
    lastSampleMs_ = 0;
    detailAlertState_ = 0xFF;
    detailConnState_ = -1;

    uint8_t catIdx = SettingsManager::instance().get().liveSlots[selectedSensor_];
    const auto& def = UniversalPidCatalog::get(catIdx);
    const uint8_t decimals = getSensorDecimals(def);
    const float scale = (decimals == 2) ? 100.0f : ((decimals == 1) ? 10.0f : 1.0f);

    axisMin_ = static_cast<int32_t>(def.minVal * scale);
    axisMax_ = static_cast<int32_t>(def.maxVal * scale);
    if (axisMax_ <= axisMin_) axisMax_ = axisMin_ + 10;

    if (chart_ != nullptr && series_ != nullptr) {
        lv_chart_set_range(chart_, LV_CHART_AXIS_PRIMARY_Y, axisMin_, axisMax_);
        lv_chart_set_all_values(chart_, series_, LV_CHART_POINT_NONE);
        lv_chart_refresh(chart_);
    }

    set_text(statMin_, "--");
    set_text(statAvg_, "--");
    set_text(statMax_, "--");

    const auto& settings = SettingsManager::instance().get();
    if (settings.operationMode == OperationMode::LAUNCH) {
        char buf[16];
        formatSensorValue(buf, sizeof(buf), 0.0f, decimals);
        set_text(detailVal_, buf);
        set_text_color(detailVal_, color::text_dim());
    } else {
        set_text(detailVal_, "--");
        set_text_color(detailVal_, color::accent());
    }
}

float LivePage::getSensorValue(uint8_t index, const VehicleState& state) const {
    if (index < kSensorCount) {
        return state.liveSlotValues[index];
    }
    return 0.0f;
}

void LivePage::applySlotSensor(uint8_t slotIdx, uint8_t catalogIdx) {
    if (slotIdx >= kSensorCount || catalogIdx >= UniversalPidCatalog::kTotalPids) return;
    const auto& def = UniversalPidCatalog::get(catalogIdx);
    const char* pidName = def.name;
    set_text(cards_[slotIdx].iconLabel, getSensorIcon(def));
    set_text(cards_[slotIdx].nameLabel, pidName);
    set_text(cards_[slotIdx].unitLabel, def.unit);
    cards_[slotIdx].lastVal = -9999.0f;
    cards_[slotIdx].alertState = 0xFF;
    cards_[slotIdx].connState = -1;

    if (isDetailOpen_ && selectedSensor_ == slotIdx) {
        set_text(detailName_, pidName);
        set_text(detailUnit_, def.unit);
        UiShell::instance().setTitle(pidName);
        resetStats();
    }
}

void LivePage::update(const VehicleState& state, uint32_t nowMs) {
    if (isDetailOpen_) {
        updateDetail(state, nowMs);
    } else {
        updateGrid(state, nowMs);
    }
}

void LivePage::updateGrid(const VehicleState& state, uint32_t nowMs) {
    if (lastGridUpdateMs_ != 0 && nowMs - lastGridUpdateMs_ < kGridUpdateMs) {
        return;
    }
    lastGridUpdateMs_ = nowMs;

    char buf[16];
    const auto& settings = SettingsManager::instance().get();
    const bool isLaunch = (settings.operationMode == OperationMode::LAUNCH);

    for (uint8_t i = 0; i < kSensorCount; ++i) {
        const float val = getSensorValue(i, state);
        const auto& def = UniversalPidCatalog::get(settings.liveSlots[i]);
        const uint8_t catIdx = settings.liveSlots[i];
        const bool isValid = state.isConnected;
        const bool isSupported = UniversalPidCatalog::isCatalogIndexSupported(catIdx, state.unsupportedCatalogBits);

        if (isValid && !isSupported) {
            if (cards_[i].connState != 2) {
                cards_[i].connState = 2;
                cards_[i].alertState = 0;
                cards_[i].lastVal = -9999.0f;
                lv_obj_set_style_border_color(cards_[i].container, color::stroke(), 0);
                lv_obj_set_style_border_width(cards_[i].container, 1, 0);
                set_text_color(cards_[i].valueLabel, color::text_faint());
                set_text_color(cards_[i].iconLabel, color::text_faint());
                set_text(cards_[i].valueLabel, "--");
                set_text(cards_[i].unitLabel, "N/A");
            }
        } else if (isValid) {
            uint8_t alert = getSensorAlert(def, val);
            const bool stateJustConnected = (cards_[i].connState != 1);

            if (alert != cards_[i].alertState || stateJustConnected) {
                if (cards_[i].connState == 2) {
                    set_text(cards_[i].unitLabel, def.unit);
                }
                cards_[i].alertState = alert;
                cards_[i].connState = 1;
                lv_color_t c = (alert == 2) ? color::crit() : ((alert == 1) ? color::warn() : color::text());
                lv_color_t borderC = (alert == 0) ? color::stroke() : c;
                lv_obj_set_style_border_color(cards_[i].container, borderC, 0);
                lv_obj_set_style_border_width(cards_[i].container, (alert > 0) ? 2 : 1, 0);
                set_text_color(cards_[i].valueLabel, c);
                set_text_color(cards_[i].iconLabel, (alert > 0) ? c : color::accent());
            }

            if (stateJustConnected || fabsf(val - cards_[i].lastVal) >= 0.05f) {
                cards_[i].lastVal = val;
                formatSensorValue(buf, sizeof(buf), val, getSensorDecimals(def));
                set_text(cards_[i].valueLabel, buf);
            }
        } else {
            if (cards_[i].connState != 0) {
                if (cards_[i].connState == 2) {
                    set_text(cards_[i].unitLabel, def.unit);
                }
                cards_[i].connState = 0;
                cards_[i].alertState = 0;
                cards_[i].lastVal = -9999.0f;
                lv_obj_set_style_border_color(cards_[i].container, color::stroke(), 0);
                lv_obj_set_style_border_width(cards_[i].container, 1, 0);

                if (isLaunch) {
                    set_text_color(cards_[i].valueLabel, color::text_dim());
                    set_text_color(cards_[i].iconLabel, color::text_dim());
                    formatSensorValue(buf, sizeof(buf), 0.0f, getSensorDecimals(def));
                    set_text(cards_[i].valueLabel, buf);
                } else {
                    set_text_color(cards_[i].valueLabel, color::text_faint());
                    set_text_color(cards_[i].iconLabel, color::accent());
                    set_text(cards_[i].valueLabel, "--");
                }
            }
        }
    }
}

void LivePage::updateDetail(const VehicleState& state, uint32_t nowMs) {
    if (lastSampleMs_ != 0 && nowMs - lastSampleMs_ < kSampleMs) {
        return;
    }
    lastSampleMs_ = nowMs;

    const uint8_t idx = selectedSensor_;
    const float val = getSensorValue(idx, state);
    const bool isValid = state.isConnected;

    if (!isValid) {
        if (detailConnState_ != 0) {
            detailConnState_ = 0;
            detailAlertState_ = 0xFF;
            const auto& settings = SettingsManager::instance().get();
            if (settings.operationMode == OperationMode::LAUNCH) {
                char buf[16];
                const auto& def = UniversalPidCatalog::get(settings.liveSlots[idx]);
                formatSensorValue(buf, sizeof(buf), 0.0f, getSensorDecimals(def));
                set_text(detailVal_, buf);
                set_text_color(detailVal_, color::text_dim());
            } else {
                set_text(detailVal_, "--");
                set_text_color(detailVal_, color::accent());
            }
        }
        if (chart_ != nullptr && series_ != nullptr) {
            lv_chart_set_next_value(chart_, series_, LV_CHART_POINT_NONE);
        }
        return;
    }

    uint8_t catIdx = SettingsManager::instance().get().liveSlots[idx];
    const auto& def = UniversalPidCatalog::get(catIdx);
    bool isSupported = UniversalPidCatalog::isCatalogIndexSupported(catIdx, state.unsupportedCatalogBits);

    if (!isSupported) {
        if (detailConnState_ != 2) {
            detailConnState_ = 2;
            detailAlertState_ = 0xFF;
            set_text(detailVal_, "--");
            set_text(detailUnit_, "N/A");
            set_text_color(detailVal_, color::text_faint());
        }
        if (chart_ != nullptr && series_ != nullptr) {
            lv_chart_set_next_value(chart_, series_, LV_CHART_POINT_NONE);
        }
        return;
    }
    const uint8_t decimals = getSensorDecimals(def);
    const float scale = (decimals == 2) ? 100.0f : ((decimals == 1) ? 10.0f : 1.0f);

    char buf[16];
    formatSensorValue(buf, sizeof(buf), val, decimals);
    set_text(detailVal_, buf);

    uint8_t alert = getSensorAlert(def, val);
    if (detailConnState_ != 1 || alert != detailAlertState_) {
        if (detailConnState_ == 2) {
            set_text(detailUnit_, def.unit);
        }
        detailConnState_ = 1;
        detailAlertState_ = alert;
        lv_color_t alertColor = (alert == 2) ? color::crit() : ((alert == 1) ? color::warn() : color::accent());
        set_text_color(detailVal_, alertColor);
        if (chart_ != nullptr && series_ != nullptr) {
            lv_chart_set_series_color(chart_, series_, alertColor);
        }
    }

    // Update running statistics
    if (samples_ == 0) {
        min_ = max_ = val;
        sum_ = val;
        samples_ = 1;
    } else {
        min_ = fminf(min_, val);
        max_ = fmaxf(max_, val);
        sum_ += val;
        samples_++;
    }
    const float avg = sum_ / static_cast<float>(samples_);

    formatSensorValue(buf, sizeof(buf), min_, decimals);
    set_text(statMin_, buf);
    formatSensorValue(buf, sizeof(buf), avg, decimals);
    set_text(statAvg_, buf);
    formatSensorValue(buf, sizeof(buf), max_, decimals);
    set_text(statMax_, buf);

    // Automotive Telemetry Y-axis scaling:
    // Fixed [0, 100] for standard percentage ranges so baseline is always at bottom.
    // Adaptive [minVal, maxVal] for Temps and others so lines sit centered with headroom.
    int32_t lo = 0;
    int32_t hi = 0;

    if (def.minVal >= 0.0f && (def.maxVal == 100.0f || def.formula == FormulaType::PERCENT_A_255)) {
        lo = 0;
        hi = static_cast<int32_t>(100.0f * scale);
    } else {
        float loF = fminf(min_, def.minVal);
        float hiF = fmaxf(max_, def.maxVal);
        core::nice_range(loF * scale, hiF * scale, lo, hi);
    }

    if (lo != axisMin_ || hi != axisMax_) {
        axisMin_ = lo;
        axisMax_ = hi;
        lv_chart_set_range(chart_, LV_CHART_AXIS_PRIMARY_Y, lo, hi);
    }

    lv_chart_set_next_value(chart_, series_, static_cast<int32_t>(roundf(val * scale)));
}

void LivePage::onLayout(int32_t contentW) {
    if (root_ == nullptr) return;
    lv_obj_set_size(root_, contentW, layout::kContentH);
    if (gridContainer_ != nullptr) lv_obj_set_size(gridContainer_, contentW, layout::kContentH);
    if (detailContainer_ != nullptr) lv_obj_set_size(detailContainer_, contentW, layout::kContentH);

    const int32_t gap = 6;
    const int32_t cardW = (contentW - 3 * gap) / 2;
    for (uint8_t i = 0; i < kSensorCount; ++i) {
        if (cards_[i].container == nullptr) continue;
        const uint8_t col = i % 2;
        const uint8_t rowIdx = i / 2;
        const int32_t x = gap + col * (cardW + gap);
        const int32_t y = gap + rowIdx * (60 + gap);

        lv_obj_set_pos(cards_[i].container, x, y);
        lv_obj_set_size(cards_[i].container, cardW, 60);
        if (cards_[i].topRow != nullptr) {
            lv_obj_set_size(cards_[i].topRow, cardW - 16, 16);
        }
        if (cards_[i].valRow != nullptr) {
            lv_obj_set_size(cards_[i].valRow, cardW - 16, 28);
        }
    }

    if (detailHeader_ != nullptr) {
        lv_obj_set_size(detailHeader_, contentW - 12, 58);
    }
    if (chartCard_ != nullptr) {
        lv_obj_set_size(chartCard_, contentW - 12, 136);
    }
    if (chart_ != nullptr) {
        lv_obj_set_size(chart_, contentW - 20, 128);
    }
}

void LivePage::applyTheme() {
    if (root_ == nullptr) return;
    detailAlertState_ = 0xFF;
    lv_obj_set_style_bg_color(root_, color::bg(), 0);
    for (uint8_t i = 0; i < kSensorCount; ++i) {
        if (cards_[i].iconLabel != nullptr) {
            lv_obj_set_style_text_color(cards_[i].iconLabel, color::accent(), 0);
        }
    }
    if (detailVal_ != nullptr) {
        lv_obj_set_style_text_color(detailVal_, color::accent(), 0);
    }
    if (chart_ != nullptr && series_ != nullptr) {
        lv_chart_set_series_color(chart_, series_, color::accent());
    }
}

}  // namespace ui
#endif  // !UNIT_TEST
