#ifndef UNIT_TEST
#include <Arduino.h>
#include <cstdio>
#include <cstring>
#include "ui/pages/settings_page.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "ui/fonts/ui_icons.h"
#include "ui/shell/ui_shell.h"
#include "hal/buzzer_controller.h"
#include "hal/lvgl_hal.h"
#include "config/app_config.h"
#include "config/hardware_pins.h"
#include "transport/ble_transport.h"
#include "obd/elm327_engine.h"

namespace ui {

namespace {

struct ProtocolDisplayInfo {
    const char* name;
    const char* cmd;
    uint8_t id;
};

static const ProtocolDisplayInfo kProtocols[10] = {
    { "AUTO (SEARCH)",        "ATSP0", 0  },
    { "CAN 11-bit / 500k",    "ATSP6", 6  },
    { "CAN 29-bit / 500k",    "ATSP7", 7  },
    { "CAN 11-bit / 250k",    "ATSP8", 8  },
    { "CAN 29-bit / 250k",    "ATSP9", 9  },
    { "KWP2000 (FAST INIT)",  "ATSP5", 5  },
    { "KWP2000 (5-BAUD)",     "ATSP4", 4  },
    { "ISO 9141-2 (K-LINE)",  "ATSP3", 3  },
    { "SAE J1850 PWM",        "ATSP1", 1  },
    { "SAE J1850 VPW",        "ATSP2", 2  }
};

} // namespace

void SettingsPage::setRow(uint8_t i, const char* icon, const char* labelText, const char* subText, const char* valText) {
    if (i >= 3) return;
    set_visible(rows_[i], true);
    lv_label_set_text(rowIcons_[i], icon ? icon : "");
    lv_label_set_text(rowLabels_[i], labelText ? labelText : "");
    if (subText && subText[0] != '\0') {
        lv_label_set_text(rowSubLabels_[i], subText);
        set_visible(rowSubLabels_[i], true);
        lv_obj_align(rowLabels_[i], LV_ALIGN_TOP_LEFT, 36, 6);
    } else {
        lv_label_set_text(rowSubLabels_[i], "");
        set_visible(rowSubLabels_[i], false);
        lv_obj_align(rowLabels_[i], LV_ALIGN_LEFT_MID, 36, 0);
    }
    lv_label_set_text(rowValues_[i], valText ? valText : "");
}

void SettingsPage::highlightRow(uint8_t i, bool isSelected) {
    if (i >= 3 || rows_[i] == nullptr) return;
    if (isSelected) {
        lv_obj_set_style_bg_color(rows_[i], color::card_hi(), 0);
        lv_obj_set_style_border_color(rows_[i], color::accent(), 0);
        lv_obj_set_style_border_width(rows_[i], 2, 0);
        lv_obj_set_style_text_color(rowLabels_[i], color::text(), 0);
        if (rowSubLabels_[i]) lv_obj_set_style_text_color(rowSubLabels_[i], color::text_dim(), 0);
        lv_obj_set_style_text_color(rowIcons_[i], color::accent(), 0);
        lv_obj_set_style_text_color(rowValues_[i], color::text(), 0);
    } else {
        lv_obj_set_style_bg_color(rows_[i], color::card(), 0);
        lv_obj_set_style_border_color(rows_[i], color::stroke(), 0);
        lv_obj_set_style_border_width(rows_[i], 1, 0);
        lv_obj_set_style_text_color(rowLabels_[i], color::text_dim(), 0);
        if (rowSubLabels_[i]) lv_obj_set_style_text_color(rowSubLabels_[i], color::text_faint(), 0);
        lv_obj_set_style_text_color(rowIcons_[i], color::text_faint(), 0);
        lv_obj_set_style_text_color(rowValues_[i], color::text_faint(), 0);
    }
    if (rowValues_[i] != nullptr) {
        lv_obj_align(rowValues_[i], LV_ALIGN_RIGHT_MID, -8, 0);
    }
}

void SettingsPage::create(lv_obj_t* parent) {
    if (root_ != nullptr) return;

    // Content area: 264x212 px
    root_ = box(parent);
    lv_obj_set_pos(root_, 0, 0);
    lv_obj_set_size(root_, layout::kContentW, layout::kContentH);
    lv_obj_set_style_bg_color(root_, color::bg(), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    // 1. Create 3 Large Static Rows (y = 4, 56, 108; height = 48 px)
    for (uint8_t i = 0; i < 3; ++i) {
        lv_obj_t* row = box(root_);
        lv_obj_set_pos(row, 4, 4 + (i * 52));
        lv_obj_set_size(row, layout::kContentW - 8, 48);
        lv_obj_set_style_bg_color(row, color::card(), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(row, color::stroke(), 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_radius(row, 6, 0);
        // Note: Row touch is disabled per user request to prevent accidental touches.
        // Navigation is via footer < ^ v SELECT buttons.

        // Icon (24px)
        lv_obj_t* icn = label(row, font::icon24(), color::accent(), "");
        lv_obj_align(icn, LV_ALIGN_LEFT_MID, 8, 0);

        // Label (font::title() 16px - line height 18px)
        lv_obj_t* lbl = label(row, font::title(), color::text(), "");
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 36, 0);
        lv_obj_set_width(lbl, 126);
        single_line(lbl);

        // Sublabel (font::caption() 12px)
        lv_obj_t* sub = label(row, font::caption(), color::text_dim(), "");
        lv_obj_align(sub, LV_ALIGN_TOP_LEFT, 36, 26);
        lv_obj_set_width(sub, 126);
        single_line(sub);
        set_visible(sub, false);

        // Value / Right text (font::body() 14px)
        lv_obj_t* val = label(row, font::body(), color::text_dim(), "");
        lv_obj_align(val, LV_ALIGN_RIGHT_MID, -8, 0);
        lv_obj_set_width(val, 84);
        single_line(val);
        lv_obj_set_style_text_align(val, LV_TEXT_ALIGN_RIGHT, 0);

        rows_[i] = row;
        rowIcons_[i] = icn;
        rowLabels_[i] = lbl;
        rowSubLabels_[i] = sub;
        rowValues_[i] = val;
    }

    // 1b. Create About Card (y = 4, height = 152 px — covers rows 0..2 area)
    aboutCard_ = card(root_);
    lv_obj_set_pos(aboutCard_, 4, 4);
    lv_obj_set_size(aboutCard_, layout::kContentW - 8, 152);
    lv_obj_set_style_border_color(aboutCard_, color::stroke(), 0);
    lv_obj_set_style_border_width(aboutCard_, 1, 0);
    lv_obj_set_style_radius(aboutCard_, 6, 0);
    lv_obj_set_flex_flow(aboutCard_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(aboutCard_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(aboutCard_, 12, 0);

    aboutVersionLbl_ = label(aboutCard_, font::value(), color::text(), DASHSTAT_VERSION_FULL);

    aboutDivider_ = box(aboutCard_);
    lv_obj_set_size(aboutDivider_, 140, 2);
    lv_obj_set_style_bg_color(aboutDivider_, color::accent(), 0);
    lv_obj_set_style_bg_opa(aboutDivider_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(aboutDivider_, 1, 0);

    lv_obj_t* repoRow = row(aboutCard_, 8);
    aboutGithubIcon_ = icon(repoRow, ICON_GITHUB, font::icon24(), color::accent());
    aboutRepoLbl_ = label(repoRow, font::title(), color::text(), DASHSTAT_REPO);

    set_visible(aboutCard_, false);

    // 2. Create Footer Bar (y = 162, height = 46 px)
    footerContainer_ = box(root_);
    lv_obj_set_pos(footerContainer_, 4, 162);
    lv_obj_set_size(footerContainer_, layout::kContentW - 8, 46);
    lv_obj_set_style_bg_opa(footerContainer_, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(footerContainer_, LV_OBJ_FLAG_SCROLLABLE);

    // [ < ] Back button (Left) - 48 x 42 px
    btnBack_ = box(footerContainer_);
    lv_obj_set_pos(btnBack_, 0, 2);
    lv_obj_set_size(btnBack_, 48, 42);
    lv_obj_set_style_bg_color(btnBack_, color::card(), 0);
    lv_obj_set_style_border_color(btnBack_, color::stroke(), 0);
    lv_obj_set_style_border_width(btnBack_, 1, 0);
    lv_obj_set_style_radius(btnBack_, 6, 0);
    lv_obj_add_flag(btnBack_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(btnBack_, onBackClicked, LV_EVENT_CLICKED, this);
    lv_obj_set_user_data(btnBack_, this);
    lblBack_ = label(btnBack_, font::icon24(), color::text(), ICON_BACK);
    lv_obj_center(lblBack_);

    // [ ^ ] Up button (Center-left) - 48 x 42 px
    btnUp_ = box(footerContainer_);
    lv_obj_set_pos(btnUp_, 54, 2);
    lv_obj_set_size(btnUp_, 48, 42);
    lv_obj_set_style_bg_color(btnUp_, color::card(), 0);
    lv_obj_set_style_border_color(btnUp_, color::stroke(), 0);
    lv_obj_set_style_border_width(btnUp_, 1, 0);
    lv_obj_set_style_radius(btnUp_, 6, 0);
    lv_obj_add_flag(btnUp_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(btnUp_, onUpClicked, LV_EVENT_CLICKED, this);
    lv_obj_set_user_data(btnUp_, this);
    lblUp_ = label(btnUp_, font::icon24(), color::text(), ICON_CHEVRON_UP);
    lv_obj_center(lblUp_);

    // [ v ] Down button (Center-right) - 48 x 42 px
    btnDown_ = box(footerContainer_);
    lv_obj_set_pos(btnDown_, 108, 2);
    lv_obj_set_size(btnDown_, 48, 42);
    lv_obj_set_style_bg_color(btnDown_, color::card(), 0);
    lv_obj_set_style_border_color(btnDown_, color::stroke(), 0);
    lv_obj_set_style_border_width(btnDown_, 1, 0);
    lv_obj_set_style_radius(btnDown_, 6, 0);
    lv_obj_add_flag(btnDown_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(btnDown_, onDownClicked, LV_EVENT_CLICKED, this);
    lv_obj_set_user_data(btnDown_, this);
    lblDown_ = label(btnDown_, font::icon24(), color::text(), ICON_CHEVRON_DOWN);
    lv_obj_center(lblDown_);

    // [ SELECT ] Action button (Right) - 94 x 42 px
    btnSelect_ = box(footerContainer_);
    lv_obj_set_pos(btnSelect_, 162, 2);
    lv_obj_set_size(btnSelect_, 94, 42);
    lv_obj_set_style_bg_color(btnSelect_, color::card_hi(), 0);
    lv_obj_set_style_border_color(btnSelect_, color::accent(), 0);
    lv_obj_set_style_border_width(btnSelect_, 1, 0);
    lv_obj_set_style_radius(btnSelect_, 6, 0);
    lv_obj_add_flag(btnSelect_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(btnSelect_, onSelectEvent, LV_EVENT_ALL, this);
    lv_obj_set_user_data(btnSelect_, this);
    lblSelect_ = label(btnSelect_, font::icon24(), color::accent(), ICON_SELECT);
    lv_obj_center(lblSelect_);

    renderCurrentView();
}

const char* SettingsPage::title() const {
    const char* base = "SETTINGS";
    uint8_t curPage = selectedIndex_ / 3;
    uint8_t totalPages = 1;

    switch (currentView_) {
        case SettingsView::ROOT_MENU:
            base = "SETTINGS";
            totalPages = 6;
            break;
        case SettingsView::PROTOCOL_PICKER:
            base = "OBD PROTOCOL";
            totalPages = 4;
            break;
        case SettingsView::VEHICLE_PARAMS:
            base = "VEHICLE PARAMS";
            totalPages = 2;
            break;
        case SettingsView::PID_SLOTS_DASH:
            base = "DASHBOARD PIDS";
            totalPages = 2;
            break;
        case SettingsView::PID_SLOTS_LIVE:
            base = "LIVE STAT PIDS";
            totalPages = 3;
            break;
        case SettingsView::PID_GROUP_PICKER:
            base = "SELECT GROUP";
            totalPages = 2;
            break;
        case SettingsView::PID_SENSOR_PICKER: {
            base = UniversalPidCatalog::getSuperGroupName(targetGroup_);
            size_t count = UniversalPidCatalog::getSuperGroupCount(targetGroup_);
            totalPages = static_cast<uint8_t>((count + 2) / 3);
            break;
        }
        case SettingsView::BLUETOOTH_DEVICES: {
            base = "BLUETOOTH OBD";
            size_t devCount = BleTransport::instance().getDiscoveredCount();
            size_t total = (devCount == 0) ? 2 : (2 + devCount);
            totalPages = static_cast<uint8_t>((total + 2) / 3);
            break;
        }
        case SettingsView::TOUCH_CALIBRATE: {
            base = "CALIBRATE";
            totalPages = 1;
            break;
        }
        case SettingsView::OBD_LOG: {
            base = "OBD LOG";
            totalPages = 1;
            break;
        }
        case SettingsView::ABOUT: {
            base = "ABOUT";
            totalPages = 1;
            break;
        }
    }

    if (totalPages > 1) {
        snprintf(titleBuf_, sizeof(titleBuf_), "%s  [%u/%u]", base, curPage + 1, totalPages);
    } else {
        snprintf(titleBuf_, sizeof(titleBuf_), "%s", base);
    }
    return titleBuf_;
}

void SettingsPage::onShow() {
    renderCurrentView();
}

void SettingsPage::onHide() {
    rpmLongPressActive_ = false;
    SettingsManager::instance().saveIfDirty();
}

bool SettingsPage::onBack() {
    if (currentView_ != SettingsView::ROOT_MENU) {
        handleBack(false);
        return true;
    }
    return false;
}

void SettingsPage::update(const VehicleState& state, uint32_t nowMs) {
    (void)state;
    // Reset 2-tap confirm timeout after 5 seconds
    if (resetStage_ == ResetConfirmStage::CONFIRM && (nowMs - resetConfirmStartMs_) > 5000) {
        resetStage_ = ResetConfirmStage::NORMAL;
        renderCurrentView();
    }

    // Dynamic scan & connection status sync when in BLUETOOTH_DEVICES view
    if (currentView_ == SettingsView::BLUETOOTH_DEVICES) {
        size_t curCount = BleTransport::instance().getDiscoveredCount();
        bool curScanning = BleTransport::instance().isScanning();
        uint8_t curState = static_cast<uint8_t>(BleTransport::instance().getState());
        if (curCount != lastBtCount_ || curScanning != lastBtScanning_ || curState != lastBtState_ || (curScanning && (nowMs - lastBtAnimMs_ >= 500))) {
            lastBtCount_ = curCount;
            lastBtScanning_ = curScanning;
            lastBtState_ = curState;
            lastBtAnimMs_ = nowMs;
            renderCurrentView();
        }
    }

    if (currentView_ == SettingsView::OBD_LOG && !logPaused_) {
        if (nowMs - lastLogRefreshMs_ >= 250) {
            lastLogRefreshMs_ = nowMs;
            renderCurrentView();
        }
    }
}

void SettingsPage::renderCurrentView() {
    if (UiShell::instance().currentPage() == PageId::Settings) {
        UiShell::instance().setTitle(title());
    }
    set_visible(aboutCard_, currentView_ == SettingsView::ABOUT);
    switch (currentView_) {
        case SettingsView::ROOT_MENU:
            renderRootMenu();
            break;
        case SettingsView::PROTOCOL_PICKER:
            renderProtocolPicker();
            break;
        case SettingsView::VEHICLE_PARAMS:
            renderVehicleParams();
            break;
        case SettingsView::PID_SLOTS_DASH:
        case SettingsView::PID_SLOTS_LIVE:
            renderPidSlots();
            break;
        case SettingsView::PID_GROUP_PICKER:
            renderPidGroupPicker();
            break;
        case SettingsView::PID_SENSOR_PICKER:
            renderPidSensorPicker();
            break;
        case SettingsView::BLUETOOTH_DEVICES:
            renderBluetoothDevices();
            break;
        case SettingsView::TOUCH_CALIBRATE:
            renderTouchCalibrate();
            break;
        case SettingsView::OBD_LOG:
            renderObdLog();
            break;
        case SettingsView::ABOUT:
            renderAbout();
            break;
    }
}

void SettingsPage::renderRootMenu() {
    auto& sm = SettingsManager::instance();
    const auto& s = sm.get();

    uint8_t page = selectedIndex_ / 3;
    uint8_t selectedRow = selectedIndex_ % 3;

    if (page == 0) {
        // Items 0..2
        setRow(0, ICON_VIEW_DASH, "DASH PIDS (4)", "Dashboard 4-slot display", "CONFIG >");
        setRow(1, ICON_LIVE, "LIVE PIDS (6)", "Real-time 6-slot display", "CONFIG >");

        const char* protoCmd = "ATSP0";
        const char* protoName = "AUTO (SEARCH)";
        for (const auto& p : kProtocols) {
            if (p.id == s.selectedProtocol) {
                protoCmd = p.cmd;
                protoName = p.name;
                break;
            }
        }
        setRow(2, ICON_GAUGE, "OBD PROTOCOL", protoName, protoCmd);

    } else if (page == 1) {
        // Items 3..5
        char rpmRootBuf[16];
        snprintf(rpmRootBuf, sizeof(rpmRootBuf), "%u RPM >", s.maxRpm);
        setRow(0, ICON_CAR, "VEHICLE PARAMS",
               "Tachometer scale & limits",
               rpmRootBuf);

        if (s.brightnessMode == BrightnessMode::AUTO) {
            setRow(1, ICON_FLASH, "BRIGHTNESS", "Auto ambient light sensor", "[ AUTO ]");
        } else {
            char bBuf[16];
            snprintf(bBuf, sizeof(bBuf), "[ %u%% ]", s.manualBrightness);
            setRow(1, ICON_FLASH, "BRIGHTNESS", "Fixed display backlight", bBuf);
        }

        char vBuf[16];
        snprintf(vBuf, sizeof(vBuf), "[ %u%% ]", s.buzzerVolume);
        setRow(2, ICON_VOLUME, "BUZZER VOLUME", "Audio playback loudness", vBuf);

    } else if (page == 2) {
        // Items 6..8
        switch (s.buzzerMode) {
            case BuzzerMode::ALL_ON:
                setRow(0, ICON_ALERT, "BUZZER SOUND", "Key clicks & audio alarms", "[ ALL ON ]");
                break;
            case BuzzerMode::ALARM_ONLY:
                setRow(0, ICON_ALERT, "BUZZER SOUND", "Critical warnings only", "[ ALARM ]");
                break;
            case BuzzerMode::MUTE:
                setRow(0, ICON_ALERT, "BUZZER SOUND", "All sound feedback muted", "[ MUTE ]");
                break;
        }

        const char* themeName = "NITROUS BLUE";
        switch (s.themeIndex) {
            case 0: themeName = "NITROUS BLUE"; break;
            case 1: themeName = "NEON GREEN"; break;
            case 2: themeName = "RACING AMBER"; break;
            case 3: themeName = "TOKYO PINK"; break;
            case 4: themeName = "THRUSTERS RED"; break;
            case 5: themeName = "MILITARY GRAY"; break;
        }
        char tBuf[24];
        snprintf(tBuf, sizeof(tBuf), "[ %s ]", themeName);
        setRow(1, ICON_TRIM, "THEME COLOR", "UI accent & gauge styling", tBuf);

        const char* railDesc = "Auto-hide after 5s";
        const char* railVal = "[ AUTO HIDE ]";
        switch (s.navRailMode) {
            case NavRailMode::AUTO_HIDE:
                railDesc = "Auto-hide after 5s";
                railVal = "[ AUTO HIDE ]";
                break;
            case NavRailMode::ALWAYS_ON:
                railDesc = "Persistent left rail";
                railVal = "[ ALWAYS ON ]";
                break;
            case NavRailMode::TAP_TO_CALL:
                railDesc = "Tap header to toggle";
                railVal = "[ TAP BAR ]";
                break;
        }
        setRow(2, ICON_VIEW_GRID, "MENU BAR", railDesc, railVal);

    } else if (page == 3) {
        // Page 3 (Items 9..11)
        setRow(0, ICON_LINK, "BLUETOOTH OBD",
               (s.savedBtMac[0] != '\0') ? s.savedBtMac : "No adapter paired",
               (s.savedBtMac[0] != '\0') ? "[ PAIRED ]" : "PAIR >");

        setRow(1, ICON_DASHBOARD, "OPERATION MODE",
               (s.operationMode == OperationMode::DEMO) ? "Synthetic OBD simulation" : "Live vehicle telemetry",
               (s.operationMode == OperationMode::DEMO) ? "[ DEMO ]" : "[ LAUNCH ]");

        setRow(2, ICON_ROTATE, "SCREEN ROTATE",
               "180 deg display orientation",
               (s.screenRotation == ScreenRotation::NORMAL) ? "[ NORMAL ]" : "[ INVERTED ]");

    } else if (page == 4) {
        // Page 4 (Items 12..14)
        setRow(0, ICON_TOUCH, "TOUCH CALIBRATE",
               "Resistive touch 4-point matrix",
               "CONFIG >");

        setRow(1, ICON_SPEED, "SPEED FONT",
               "Speedometer typography",
               font::speedFontName(s.speedFont));

        if (resetStage_ == ResetConfirmStage::CONFIRM) {
            setRow(2, ICON_RESTORE, "CONFIRM RESET",
                   "Press SELECT to wipe all NVS",
                   "[ CONFIRM ]");
        } else {
            setRow(2, ICON_RESTORE, "FACTORY RESET",
                   "Restore all default values",
                   "RESET >");
        }
    } else {
        // Page 5 (Items 15..16)
        setRow(0, ICON_DIAG, "OBD LOG",
               "Live TX/RX bus monitor",
               "VIEW >");
        setRow(1, ICON_INFO, "ABOUT",
               "DashStat Stable " DASHSTAT_VERSION,
               "VIEW >");
        set_visible(rows_[2], false);
    }

    for (int i = 0; i < 3; ++i) {
        if (page == 5 && i > 1) continue;
        highlightRow(i, (i == selectedRow));
    }
}

void SettingsPage::renderProtocolPicker() {
    auto& sm = SettingsManager::instance();
    uint8_t cur = sm.get().selectedProtocol;
    uint8_t page = selectedIndex_ / 3;
    uint8_t selectedRow = selectedIndex_ % 3;

    for (int i = 0; i < 3; ++i) {
        uint8_t protoIdx = page * 3 + i;
        if (protoIdx < 10) {
            const auto& p = kProtocols[protoIdx];
            bool isCurrent = (p.id == cur);
            setRow(i, isCurrent ? ICON_CHECK : ICON_GAUGE,
                   p.name,
                   p.cmd,
                   isCurrent ? "[✓] ACTIVE" : "SELECT >");
            highlightRow(i, (i == selectedRow));
        } else {
            set_visible(rows_[i], false);
        }
    }
}

void SettingsPage::renderVehicleParams() {
    auto& sm = SettingsManager::instance();
    const auto& s = sm.get();
    uint8_t page = selectedIndex_ / 3;
    uint8_t selectedRow = selectedIndex_ % 3;

    if (page == 0) {
        // Items 0..2: RPM PRESET, MAX RPM, REDLINE RPM
        const char* rpmDesc = "Custom tachometer limits";
        const char* rpmVal = "[ CUSTOM ] >";
        if (s.maxRpm == 8000 && s.redlineRpm == 6200) {
            rpmDesc = "Petrol 8000 / 6200";
            rpmVal = "[ PETROL 8K ] >";
        } else if (s.maxRpm == 6000 && s.redlineRpm == 4500) {
            rpmDesc = "Diesel 6000 / 4500";
            rpmVal = "[ DIESEL 6K ] >";
        } else if (s.maxRpm == 9000 && s.redlineRpm == 8200) {
            rpmDesc = "Sport / Bike 9000 / 8200";
            rpmVal = "[ SPORT 9K ] >";
        }
        setRow(0, ICON_PISTON, "RPM PRESET", rpmDesc, rpmVal);

        char buf[24];
        snprintf(buf, sizeof(buf), "[-] %u [+]", s.maxRpm);
        setRow(1, ICON_SPEED_LIMIT, "MAX RPM", "Tachometer gauge ceiling", buf);

        snprintf(buf, sizeof(buf), "[-] %u [+]", s.redlineRpm);
        setRow(2, ICON_ALERT_CIRCLE, "REDLINE RPM", "Redline zone warning start", buf);

        for (int i = 0; i < 3; ++i) {
            highlightRow(i, (i == selectedRow));
        }
    } else {
        // Page 1 (Item 3: WARN RPM)
        char buf[24];
        snprintf(buf, sizeof(buf), "[-] %u [+]", s.warnRpm);
        setRow(0, ICON_ALERT, "WARN RPM", "Shift alert threshold", buf);
        set_visible(rows_[1], false);
        set_visible(rows_[2], false);
        highlightRow(0, (selectedRow == 0));
    }
}

void SettingsPage::renderPidSlots() {
    auto& sm = SettingsManager::instance();
    const auto& s = sm.get();
    bool isDash = (currentView_ == SettingsView::PID_SLOTS_DASH);
    uint8_t slotCount = isDash ? 4 : 6;

    uint8_t page = selectedIndex_ / 3;
    uint8_t selectedRow = selectedIndex_ % 3;

    for (int i = 0; i < 3; ++i) {
        uint8_t itemIdx = page * 3 + i;
        if (itemIdx < slotCount) {
            uint8_t catIdx = isDash ? s.dashSlots[itemIdx] : s.liveSlots[itemIdx];

            char titleBuf[16];
            snprintf(titleBuf, sizeof(titleBuf), "SLOT %u", itemIdx + 1);

            if (catIdx == 0xFF) {
                setRow(i, ICON_CLOSE, titleBuf, "No PID assigned", "[---] EMPTY");
            } else {
                const auto& def = UniversalPidCatalog::get(catIdx);
                char valBuf[24];
                snprintf(valBuf, sizeof(valBuf), "[ %s ] >", def.unit);
                setRow(i, ICON_GAUGE, titleBuf, def.name, valBuf);
            }
            highlightRow(i, (i == selectedRow));
        } else if (itemIdx == slotCount) {
            // Reset option
            setRow(i, ICON_RESTORE,
                   isDash ? "RESET 4 SLOTS" : "RESET 6 SLOTS",
                   "Restore default slot assignments",
                   "[ DEFAULT ]");
            highlightRow(i, (i == selectedRow));
        } else {
            set_visible(rows_[i], false);
        }
    }
}

void SettingsPage::renderPidGroupPicker() {
    uint8_t page = selectedIndex_ / 3;
    uint8_t selectedRow = selectedIndex_ % 3;

    if (page == 0) {
        setRow(0, ICON_ENGINE, "1. ENGINE & THERMAL", "RPM, Speed, Load, Coolant, IAT", "(8) >");
        setRow(1, ICON_AIR, "2. AIR, FUEL & O2", "Boost, MAP, MAF, Trims, O2", "(8) >");
        setRow(2, ICON_FLASH, "3. VOLT, PEDAL & DIAG", "Battery, ECU V, Timing, Pedal", "(8) >");
    } else {
        setRow(0, ICON_CLOSE, "CLEAR THIS SLOT", "Leave slot unassigned", "[ EMPTY ]");
        set_visible(rows_[1], false);
        set_visible(rows_[2], false);
    }

    for (int i = 0; i < 3; ++i) {
        highlightRow(i, (i == selectedRow));
    }
}

void SettingsPage::renderPidSensorPicker() {
    size_t count = UniversalPidCatalog::getSuperGroupCount(targetGroup_);
    uint8_t page = selectedIndex_ / 3;
    uint8_t selectedRow = selectedIndex_ % 3;

    for (int i = 0; i < 3; ++i) {
        size_t groupIdx = page * 3 + i;
        if (groupIdx < count) {
            uint8_t pidIdx = UniversalPidCatalog::getSuperGroupPidIndex(targetGroup_, groupIdx);
            const auto& def = UniversalPidCatalog::get(pidIdx);
            char subBuf[32];
            if (def.pidHex == 0xF0) {
                snprintf(subBuf, sizeof(subBuf), "Virtual MAP-BARO (%s)", def.unit);
            } else {
                snprintf(subBuf, sizeof(subBuf), "Mode 01 PID %02X (%s)", def.pidHex, def.unit);
            }
            setRow(i, ICON_GAUGE, def.name, subBuf, "SELECT >");
            highlightRow(i, (i == selectedRow));
        } else {
            set_visible(rows_[i], false);
        }
    }
}

void SettingsPage::renderBluetoothDevices() {
    auto& sm = SettingsManager::instance();
    const auto& s = sm.get();
    size_t devCount = BleTransport::instance().getDiscoveredCount();
    size_t total = (devCount == 0) ? 2 : (2 + devCount);

    if (selectedIndex_ >= total) {
        selectedIndex_ = 0;
    }

    uint8_t page = selectedIndex_ / 3;
    uint8_t selectedRow = selectedIndex_ % 3;
    bool isScanning = BleTransport::instance().isScanning();

    for (int i = 0; i < 3; ++i) {
        size_t itemIdx = page * 3 + i;
        if (itemIdx < total) {
            if (itemIdx == 0) {
                setRow(i, ICON_SCAN, "SCAN / RESCAN",
                       "Discover BLE OBD adapters",
                       isScanning ? "[ SCANNING... ]" : "[ START SCAN ]");
            } else if (itemIdx == 1) {
                setRow(i, ICON_DELETE, "FORGET SAVED MAC",
                       (s.savedBtMac[0] != '\0') ? s.savedBtMac : "No adapter paired",
                       (s.savedBtMac[0] != '\0') ? "[ CLEAR ]" : "[ NONE ]");
            } else {
                size_t devIdx = itemIdx - 2;
                char devName[32]{};
                char devMac[18]{};
                BleTransport::instance().getDiscoveredInfo(devIdx, devName, sizeof(devName), devMac, sizeof(devMac));
                bool isSaved = (s.savedBtMac[0] != '\0' && devMac[0] != '\0' && strcasecmp(s.savedBtMac, devMac) == 0);

                const char* icn = ICON_LINK;
                const char* val = (devMac[0] != '\0') ? devMac : "--:--:--";
                if (isSaved) {
                    TransportState st = BleTransport::instance().getState();
                    if (st == TransportState::CONNECTED) {
                        icn = ICON_CHECK;
                        val = "[ CONNECTED ]";
                    } else if (st == TransportState::CONNECTING) {
                        icn = ICON_AIR;
                        val = "[ CONNECTING... ]";
                    } else {
                        icn = ICON_CHECK;
                        val = "[ PAIRED ]";
                    }
                }
                setRow(i, icn,
                       (devName[0] != '\0') ? devName : "BLE OBD Adapter",
                       (devMac[0] != '\0') ? devMac : "--:--:--",
                       val);
            }
            highlightRow(i, (i == selectedRow));
        } else if (page == 0 && i == 2 && devCount == 0) {
            setRow(i, isScanning ? ICON_AIR : ICON_ALERT_CIRCLE,
                   isScanning ? "SEARCHING OBD..." : "NO DEVICES FOUND",
                   isScanning ? "Listening for BLE broadcast" : "Power on adapter & retry",
                   isScanning ? "[ WAIT ]" : "[ RESCAN ]");
            highlightRow(i, false);
        } else {
            set_visible(rows_[i], false);
        }
    }
}

void SettingsPage::renderTouchCalibrate() {
    uint8_t selectedRow = selectedIndex_ % 3;

    setRow(0, ICON_TARGET, "TOUCH CALIBRATE", "4-point screen calibration", "[ START ]");
    setRow(1, ICON_RESTORE, "DEFAULT CONFIG", "Reset touch matrix to default", "[ RESET ]");
    set_visible(rows_[2], false);

    for (int i = 0; i < 2; ++i) {
        highlightRow(i, (i == selectedRow));
    }
}

void SettingsPage::refreshLogCache() {
    auto* eng = Elm327Engine::instance();
    if (!eng) {
        cachedLogCount_ = 0;
        return;
    }
    if (logPaused_ && cachedLogCount_ > 0) {
        return;
    }
    cachedLogCount_ = eng->getLogSnapshot(cachedLogs_, kMaxCachedLogs);
}

void SettingsPage::renderObdLog() {
    auto* eng = Elm327Engine::instance();
    if (!eng) {
        setRow(0, ICON_ALERT, "OBD ENGINE", "Not initialized", "[ OFF ]");
        set_visible(rows_[1], false);
        set_visible(rows_[2], false);
        highlightRow(0, true);
        return;
    }

    const char* stStr = "DISC";
    switch (eng->getState()) {
        case ElmState::INIT_ADAPTER: stStr = "INIT"; break;
        case ElmState::POLLING: stStr = eng->isEcuStandby() ? "STBY" : "POLL"; break;
        case ElmState::TIMEOUT_RECOVERY: stStr = "RCVR"; break;
        default: stStr = "DISC"; break;
    }

    uint8_t proto = eng->getResolvedProtocol();
    char hdrBuf[40];
    if (proto == 0xFF) {
        snprintf(hdrBuf, sizeof(hdrBuf), "STATE: %s | AUTO", stStr);
    } else {
        snprintf(hdrBuf, sizeof(hdrBuf), "STATE: %s | ATSP%X", stStr, static_cast<unsigned>(proto));
    }

    char subBuf[48];
    snprintf(subBuf, sizeof(subBuf), "RTT %lums | %.1f req/s (HOLD=CLR)",
             static_cast<unsigned long>(eng->getAvgRttMs()), eng->getRequestRate());
    setRow(0, ICON_LINK, hdrBuf, subBuf, logPaused_ ? "[ PAUSE ]" : "[ LIVE ]");
    highlightRow(0, false);

    refreshLogCache();
    if (cachedLogCount_ == 0) {
        setRow(1, ICON_DIAG, "NO LOG ENTRIES", "Waiting for OBD traffic...", "--");
        set_visible(rows_[2], false);
        highlightRow(1, true);
        return;
    }

    if (selectedIndex_ >= cachedLogCount_) selectedIndex_ = 0;
    for (int r = 0; r < 2; ++r) {
        size_t idx = selectedIndex_ + static_cast<size_t>(r);
        if (idx < cachedLogCount_) {
            char txBuf[32];
            char idxBuf[16];
            snprintf(txBuf, sizeof(txBuf), "TX: %s", cachedLogs_[idx].tx);
            snprintf(idxBuf, sizeof(idxBuf), "#%u/%u",
                     static_cast<unsigned>(idx + 1), static_cast<unsigned>(cachedLogCount_));
            setRow(r + 1, ICON_DIAG, txBuf, cachedLogs_[idx].rx, idxBuf);
            highlightRow(r + 1, (r == 0));
        } else {
            set_visible(rows_[r + 1], false);
        }
    }
}

void SettingsPage::renderAbout() {
    for (int i = 0; i < 3; ++i) {
        set_visible(rows_[i], false);
    }
    set_visible(aboutCard_, true);
}

void SettingsPage::onBackClicked(lv_event_t* e) {
    auto* self = static_cast<SettingsPage*>(lv_event_get_user_data(e));
    if (self) self->handleBack();
}

void SettingsPage::onUpClicked(lv_event_t* e) {
    auto* self = static_cast<SettingsPage*>(lv_event_get_user_data(e));
    if (self) self->handleUp();
}

void SettingsPage::onDownClicked(lv_event_t* e) {
    auto* self = static_cast<SettingsPage*>(lv_event_get_user_data(e));
    if (self) self->handleDown();
}

void SettingsPage::onSelectEvent(lv_event_t* e) {
    auto* self = static_cast<SettingsPage*>(lv_event_get_user_data(e));
    if (!self) return;

    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_SHORT_CLICKED) {
        self->handleSelect();
    } else if (code == LV_EVENT_LONG_PRESSED || code == LV_EVENT_LONG_PRESSED_REPEAT) {
        self->handleSelectLongPress();
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (self->rpmLongPressActive_) {
            self->rpmLongPressActive_ = false;
            SettingsManager::instance().saveIfDirty();
        }
    }
}

void SettingsPage::handleSelectLongPress() {
    if (currentView_ == SettingsView::VEHICLE_PARAMS) {
        if (selectedIndex_ >= 1 && selectedIndex_ <= 3) {
            rpmLongPressActive_ = true;
            BuzzerController::instance().playButtonUpDown();
            auto& sm = SettingsManager::instance();
            switch (selectedIndex_) {
                case 1: sm.stepMaxRpm(false, false); break;
                case 2: sm.stepRedlineRpm(false, false); break;
                case 3: sm.stepWarnRpm(false, false); break;
            }
            renderCurrentView();
        }
    } else if (currentView_ == SettingsView::OBD_LOG) {
        if (Elm327Engine::instance()) {
            Elm327Engine::instance()->clearLogs();
            cachedLogCount_ = 0;
            selectedIndex_ = 0;
            BuzzerController::instance().playButtonBack();
            renderCurrentView();
        }
    }
}

void SettingsPage::handleUp() {
    if (currentView_ == SettingsView::ABOUT) return;
    BuzzerController::instance().playButtonUpDown();
    uint8_t total = 1;
    switch (currentView_) {
        case SettingsView::ROOT_MENU: total = 17; break;
        case SettingsView::PROTOCOL_PICKER: total = 10; break;
        case SettingsView::VEHICLE_PARAMS: total = 4; break;
        case SettingsView::PID_SLOTS_DASH: total = 5; break;
        case SettingsView::PID_SLOTS_LIVE: total = 7; break;
        case SettingsView::PID_GROUP_PICKER: total = 4; break;
        case SettingsView::PID_SENSOR_PICKER:
            total = UniversalPidCatalog::getSuperGroupCount(targetGroup_);
            break;
        case SettingsView::BLUETOOTH_DEVICES: {
            size_t devCount = BleTransport::instance().getDiscoveredCount();
            total = static_cast<uint8_t>((devCount == 0) ? 2 : (2 + devCount));
            break;
        }
        case SettingsView::TOUCH_CALIBRATE: total = 2; break;
        case SettingsView::OBD_LOG: {
            refreshLogCache();
            total = static_cast<uint8_t>(cachedLogCount_ > 0 ? cachedLogCount_ : 1);
            break;
        }
        case SettingsView::ABOUT:
            break;
    }
    if (total > 0) {
        selectedIndex_ = (selectedIndex_ == 0) ? (total - 1) : (selectedIndex_ - 1);
    }
    renderCurrentView();
}

void SettingsPage::handleDown() {
    if (currentView_ == SettingsView::ABOUT) return;
    BuzzerController::instance().playButtonUpDown();
    uint8_t total = 1;
    switch (currentView_) {
        case SettingsView::ROOT_MENU: total = 17; break;
        case SettingsView::PROTOCOL_PICKER: total = 10; break;
        case SettingsView::VEHICLE_PARAMS: total = 4; break;
        case SettingsView::PID_SLOTS_DASH: total = 5; break;
        case SettingsView::PID_SLOTS_LIVE: total = 7; break;
        case SettingsView::PID_GROUP_PICKER: total = 4; break;
        case SettingsView::PID_SENSOR_PICKER:
            total = UniversalPidCatalog::getSuperGroupCount(targetGroup_);
            break;
        case SettingsView::BLUETOOTH_DEVICES: {
            size_t devCount = BleTransport::instance().getDiscoveredCount();
            total = static_cast<uint8_t>((devCount == 0) ? 2 : (2 + devCount));
            break;
        }
        case SettingsView::TOUCH_CALIBRATE: total = 2; break;
        case SettingsView::OBD_LOG: {
            refreshLogCache();
            total = static_cast<uint8_t>(cachedLogCount_ > 0 ? cachedLogCount_ : 1);
            break;
        }
        case SettingsView::ABOUT:
            break;
    }
    if (total > 0) {
        selectedIndex_ = (selectedIndex_ + 1) % total;
    }
    renderCurrentView();
}

void SettingsPage::handleSelect() {
    bool playSelectTone = true;
    auto& sm = SettingsManager::instance();

    if (currentView_ == SettingsView::ROOT_MENU) {
        switch (selectedIndex_) {
            case 0:
                currentView_ = SettingsView::PID_SLOTS_DASH;
                selectedIndex_ = 0;
                break;
            case 1:
                currentView_ = SettingsView::PID_SLOTS_LIVE;
                selectedIndex_ = 0;
                break;
            case 2:
                currentView_ = SettingsView::PROTOCOL_PICKER;
                selectedIndex_ = 0;
                break;
            case 3:
                currentView_ = SettingsView::VEHICLE_PARAMS;
                selectedIndex_ = 0;
                break;
            case 4:
                sm.cycleCombinedBrightness();
                if (sm.get().brightnessMode == BrightnessMode::MANUAL) {
                    LvglHal::setBrightness(static_cast<uint8_t>(sm.get().manualBrightness * 2.55f));
                } else {
                    uint16_t ldrRaw = analogRead(CYD_LDR_PIN);
                    uint8_t targetDuty = map(constrain(ldrRaw, 200, 3800), 200, 3800, 255, 40);
                    LvglHal::setBrightness(targetDuty);
                }
                break;
            case 5:
                sm.cycleBuzzerVolume();
                break;
            case 6:
                sm.cycleBuzzerMode();
                break;
            case 7:
                sm.cycleTheme();
                ThemeManager::setTheme(static_cast<ThemeId>(sm.get().themeIndex));
                UiShell::instance().applyTheme();
                break;
            case 8:
                sm.cycleNavRailMode();
                break;
            case 9:
                currentView_ = SettingsView::BLUETOOTH_DEVICES;
                selectedIndex_ = 0;
                lastBtCount_ = 0;
                lastBtScanning_ = true;
                lastBtAnimMs_ = millis();
                BleTransport::instance().startScan(5);
                break;
            case 10:
                sm.cycleOperationMode();
                renderCurrentView();
                break;
            case 11:
                sm.cycleScreenRotation();
                renderCurrentView();
                break;
            case 12:
                currentView_ = SettingsView::TOUCH_CALIBRATE;
                selectedIndex_ = 0;
                break;
            case 13:
                sm.cycleSpeedFont();
                renderCurrentView();
                break;
            case 14:
                if (resetStage_ == ResetConfirmStage::NORMAL) {
                    resetStage_ = ResetConfirmStage::CONFIRM;
                    resetConfirmStartMs_ = millis();
                } else if (resetStage_ == ResetConfirmStage::CONFIRM) {
                    sm.resetToDefaults();
                    sm.save();
                    LvglHal::resetTouchCalibration();
                    resetStage_ = ResetConfirmStage::NORMAL;
                    selectedIndex_ = 0;
                }
                break;
            case 15:
                currentView_ = SettingsView::OBD_LOG;
                selectedIndex_ = 0;
                logPaused_ = false;
                cachedLogCount_ = 0;
                lastLogRefreshMs_ = 0;
                break;
            case 16:
                currentView_ = SettingsView::ABOUT;
                selectedIndex_ = 0;
                break;
        }
    } else if (currentView_ == SettingsView::PROTOCOL_PICKER) {
        if (selectedIndex_ < 10) {
            sm.setSelectedProtocol(kProtocols[selectedIndex_].id);
        }
    } else if (currentView_ == SettingsView::VEHICLE_PARAMS) {
        switch (selectedIndex_) {
            case 0: {
                if (sm.get().maxRpm == 8000) {
                    sm.applyRpmPreset(RpmPreset::DIESEL_6K);
                } else if (sm.get().maxRpm == 6000) {
                    sm.applyRpmPreset(RpmPreset::SPORT_9K);
                } else {
                    sm.applyRpmPreset(RpmPreset::PETROL_8K);
                }
                break;
            }
            case 1: sm.stepMaxRpm(true); break;
            case 2: sm.stepRedlineRpm(true); break;
            case 3: sm.stepWarnRpm(true); break;
        }
    } else if (currentView_ == SettingsView::PID_SLOTS_DASH) {
        if (selectedIndex_ < 4) {
            isDashSlots_ = true;
            targetSlot_ = selectedIndex_;
            currentView_ = SettingsView::PID_GROUP_PICKER;
            selectedIndex_ = 0;
        } else if (selectedIndex_ == 4) {
            sm.resetDashSlotsToDefaults();
        }
    } else if (currentView_ == SettingsView::PID_SLOTS_LIVE) {
        if (selectedIndex_ < 6) {
            isDashSlots_ = false;
            targetSlot_ = selectedIndex_;
            currentView_ = SettingsView::PID_GROUP_PICKER;
            selectedIndex_ = 0;
        } else if (selectedIndex_ == 6) {
            sm.resetLiveSlotsToDefaults();
        }
    } else if (currentView_ == SettingsView::PID_GROUP_PICKER) {
        switch (selectedIndex_) {
            case 0:
                targetGroup_ = SuperPidGroup::ENGINE_THERMAL;
                currentView_ = SettingsView::PID_SENSOR_PICKER;
                selectedIndex_ = 0;
                break;
            case 1:
                targetGroup_ = SuperPidGroup::FUEL_AIR_TURBO;
                currentView_ = SettingsView::PID_SENSOR_PICKER;
                selectedIndex_ = 0;
                break;
            case 2:
                targetGroup_ = SuperPidGroup::ELECTRIC_DIAG;
                currentView_ = SettingsView::PID_SENSOR_PICKER;
                selectedIndex_ = 0;
                break;
            case 3:
                if (isDashSlots_) {
                    sm.setDashSlot(targetSlot_, 0xFF);
                } else {
                    sm.setLiveSlot(targetSlot_, 0xFF);
                }
                currentView_ = isDashSlots_ ? SettingsView::PID_SLOTS_DASH : SettingsView::PID_SLOTS_LIVE;
                selectedIndex_ = targetSlot_;
                break;
        }
    } else if (currentView_ == SettingsView::PID_SENSOR_PICKER) {
        size_t count = UniversalPidCatalog::getSuperGroupCount(targetGroup_);
        if (selectedIndex_ < count) {
            uint8_t pidIdx = UniversalPidCatalog::getSuperGroupPidIndex(targetGroup_, selectedIndex_);
            if (isDashSlots_) {
                sm.setDashSlot(targetSlot_, pidIdx);
                currentView_ = SettingsView::PID_SLOTS_DASH;
            } else {
                sm.setLiveSlot(targetSlot_, pidIdx);
                currentView_ = SettingsView::PID_SLOTS_LIVE;
            }
            selectedIndex_ = targetSlot_;
        }
    } else if (currentView_ == SettingsView::BLUETOOTH_DEVICES) {
        if (selectedIndex_ == 0) {
            lastBtCount_ = 0;
            lastBtScanning_ = true;
            lastBtAnimMs_ = millis();
            BleTransport::instance().startScan(5);
        } else if (selectedIndex_ == 1) {
            sm.setSavedBtMac("");
            BleTransport::instance().disconnect();
        } else {
            size_t devIdx = selectedIndex_ - 2;
            char mac[18]{};
            if (BleTransport::instance().getDiscoveredInfo(devIdx, nullptr, 0, mac, sizeof(mac)) && mac[0] != '\0') {
                sm.setSavedBtMac(mac);
                if (sm.get().operationMode != OperationMode::LAUNCH) {
                    sm.setOperationMode(OperationMode::LAUNCH);
                }
                BleTransport::instance().stopScan();
                // Driven asynchronously by Core 0 OBD Task to prevent Core 1 UI lockup & TWDT reboot
            }
        }
    } else if (currentView_ == SettingsView::TOUCH_CALIBRATE) {
        playSelectTone = false;
        if (selectedIndex_ == 0) {
            // Start touch calibration (Rule R8: Stationed / parked only)
            if (UiShell::instance().isDrivingLocked()) {
                BuzzerController::instance().playWarningBeep();
                return;
            }
            LvglHal::calibrateTouch();
            BuzzerController::instance().playSuccessTone();
        } else if (selectedIndex_ == 1) {
            // Reset to default hardware calibration
            LvglHal::resetTouchCalibration();
            BuzzerController::instance().playSuccessTone();
        }
    } else if (currentView_ == SettingsView::OBD_LOG) {
        logPaused_ = !logPaused_;
    } else if (currentView_ == SettingsView::ABOUT) {
        return;
    }
    if (playSelectTone) {
        BuzzerController::instance().playButtonSelect();
    }
    renderCurrentView();
}

void SettingsPage::handleBack(bool playTone) {
    if (playTone) {
        BuzzerController::instance().playButtonBack();
    }
    if (currentView_ == SettingsView::PID_SENSOR_PICKER) {
        currentView_ = SettingsView::PID_GROUP_PICKER;
        selectedIndex_ = 0;
    } else if (currentView_ == SettingsView::PID_GROUP_PICKER) {
        currentView_ = isDashSlots_ ? SettingsView::PID_SLOTS_DASH : SettingsView::PID_SLOTS_LIVE;
        selectedIndex_ = targetSlot_;
    } else if (currentView_ == SettingsView::PID_SLOTS_DASH) {
        currentView_ = SettingsView::ROOT_MENU;
        selectedIndex_ = 0;
    } else if (currentView_ == SettingsView::PID_SLOTS_LIVE) {
        currentView_ = SettingsView::ROOT_MENU;
        selectedIndex_ = 1;
    } else if (currentView_ == SettingsView::PROTOCOL_PICKER) {
        currentView_ = SettingsView::ROOT_MENU;
        selectedIndex_ = 2;
    } else if (currentView_ == SettingsView::VEHICLE_PARAMS) {
        rpmLongPressActive_ = false;
        SettingsManager::instance().saveIfDirty();
        currentView_ = SettingsView::ROOT_MENU;
        selectedIndex_ = 3;
    } else if (currentView_ == SettingsView::BLUETOOTH_DEVICES) {
        BleTransport::instance().stopScan();
        currentView_ = SettingsView::ROOT_MENU;
        selectedIndex_ = 9;
    } else if (currentView_ == SettingsView::TOUCH_CALIBRATE) {
        currentView_ = SettingsView::ROOT_MENU;
        selectedIndex_ = 12;
    } else if (currentView_ == SettingsView::OBD_LOG) {
        currentView_ = SettingsView::ROOT_MENU;
        selectedIndex_ = 15;
    } else if (currentView_ == SettingsView::ABOUT) {
        currentView_ = SettingsView::ROOT_MENU;
        selectedIndex_ = 16;
    } else {
        // At ROOT_MENU: back button switches back to DASHBOARD page
        UiShell::instance().navigate(PageId::Dashboard);
        return;
    }
    renderCurrentView();
}

void SettingsPage::onLayout(int32_t contentW) {
    if (root_ == nullptr) return;
    lv_obj_set_size(root_, contentW, layout::kContentH);

    const int32_t containerW = contentW - 8;
    const int32_t valW = 84;
    const int32_t lblW = containerW - 36 - valW - 8; // remaining width for label & sublabel

    for (int i = 0; i < 3; ++i) {
        if (rows_[i] != nullptr) {
            lv_obj_set_width(rows_[i], containerW);
            if (rowIcons_[i] != nullptr) {
                lv_obj_align(rowIcons_[i], LV_ALIGN_LEFT_MID, 8, 0);
            }
            if (rowLabels_[i] != nullptr) {
                lv_obj_set_width(rowLabels_[i], lblW);
            }
            if (rowSubLabels_[i] != nullptr) {
                lv_obj_set_width(rowSubLabels_[i], lblW);
            }
            if (rowValues_[i] != nullptr) {
                lv_obj_set_width(rowValues_[i], valW);
                lv_obj_align(rowValues_[i], LV_ALIGN_RIGHT_MID, -8, 0);
            }
        }
    }
    if (footerContainer_ != nullptr) {
        lv_obj_set_width(footerContainer_, containerW);
    }
    if (aboutCard_ != nullptr) {
        lv_obj_set_width(aboutCard_, containerW);
    }

    const int32_t gap = 6;
    const int32_t btnW = (contentW >= 320) ? 60 : 48;
    const int32_t selectW = containerW - (3 * btnW + 3 * gap);

    if (btnBack_ != nullptr) {
        lv_obj_set_pos(btnBack_, 0, 2);
        lv_obj_set_size(btnBack_, btnW, 42);
        if (lblBack_ != nullptr) lv_obj_center(lblBack_);
    }
    if (btnUp_ != nullptr) {
        lv_obj_set_pos(btnUp_, btnW + gap, 2);
        lv_obj_set_size(btnUp_, btnW, 42);
        if (lblUp_ != nullptr) lv_obj_center(lblUp_);
    }
    if (btnDown_ != nullptr) {
        lv_obj_set_pos(btnDown_, 2 * (btnW + gap), 2);
        lv_obj_set_size(btnDown_, btnW, 42);
        if (lblDown_ != nullptr) lv_obj_center(lblDown_);
    }
    if (btnSelect_ != nullptr) {
        lv_obj_set_pos(btnSelect_, 3 * (btnW + gap), 2);
        lv_obj_set_size(btnSelect_, selectW, 42);
        if (lblSelect_ != nullptr) lv_obj_center(lblSelect_);
    }
}

void SettingsPage::applyTheme() {
    if (root_ == nullptr) return;
    lv_obj_set_style_bg_color(root_, color::bg(), 0);
    if (btnSelect_ != nullptr) {
        lv_obj_set_style_border_color(btnSelect_, color::accent(), 0);
    }
    if (lblSelect_ != nullptr) {
        lv_obj_set_style_text_color(lblSelect_, color::accent(), 0);
    }
    if (aboutDivider_ != nullptr) {
        lv_obj_set_style_bg_color(aboutDivider_, color::accent(), 0);
    }
    if (aboutGithubIcon_ != nullptr) {
        lv_obj_set_style_text_color(aboutGithubIcon_, color::accent(), 0);
    }
    if (UiShell::instance().currentPage() == PageId::Settings) {
        renderCurrentView();
    }
}

} // namespace ui
#endif
