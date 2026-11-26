#include "model/settings_manager.h"
#ifndef UNIT_TEST
#include <Arduino.h>
#include <Preferences.h>
#include "hal/lvgl_hal.h"
#include "hal/buzzer_controller.h"
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

static Preferences s_prefs;
static portMUX_TYPE s_settingsMux = portMUX_INITIALIZER_UNLOCKED;

struct SettingsLock {
    SettingsLock() { portENTER_CRITICAL(&s_settingsMux); }
    ~SettingsLock() { portEXIT_CRITICAL(&s_settingsMux); }
};
#else
#include <mutex>
static std::mutex s_settingsMutex;

struct SettingsLock {
    std::unique_lock<std::mutex> lock{s_settingsMutex};
};
#endif

#include <algorithm>
#include <cstring>
#include "hal/buzzer_controller.h"
#include "obd/universal_pid_catalog.h"

SettingsManager::SettingsManager() {
    resetToDefaults();
}

void SettingsManager::begin() {
    load();
}

AppSettings SettingsManager::get() const {
    SettingsLock lock;
    return settings_;
}

void SettingsManager::resetToDefaults() {
    SettingsLock lock;
    settings_.brightnessMode = BrightnessMode::MANUAL;
    settings_.manualBrightness = 70;
    settings_.buzzerMode = BuzzerMode::ALL_ON;
    settings_.buzzerVolume = 100;
    BuzzerController::instance().setMode(BuzzerMode::ALL_ON);
    BuzzerController::instance().setVolume(100);
    settings_.themeIndex = 0;
    settings_.savedBtMac[0] = '\0';
    settings_.selectedProtocol = 0;
    const uint8_t defSlots[6] = { 0, 1, 6, 4, 7, 18 };
    std::memcpy(settings_.liveSlots, defSlots, sizeof(defSlots));
    const uint8_t defDashSlots[4] = { 6, 16, 11, 7 };
    std::memcpy(settings_.dashSlots, defDashSlots, sizeof(defDashSlots));
    settings_.maxRpm = 8000;
    settings_.redlineRpm = 6200;
    settings_.warnRpm = 5400;
    settings_.navRailMode = NavRailMode::AUTO_HIDE;
    settings_.operationMode = OperationMode::LAUNCH;
    settings_.screenRotation = ScreenRotation::NORMAL;
    settings_.speedFont = SpeedFont::DEFAULT_FONT;
    isDirty_ = false;
    version_.fetch_add(1, std::memory_order_release);
}

void SettingsManager::reset() {
    resetToDefaults();
}

void SettingsManager::saveIfDirty() {
    bool dirty = false;
    {
        SettingsLock lock;
        dirty = isDirty_;
    }
    if (dirty) {
        save();
    }
}

void SettingsManager::load() {
#ifndef UNIT_TEST
    if (s_prefs.begin("dashstat", true)) {
        AppSettings loaded = get();

        uint8_t bMode = s_prefs.getUChar("b_mode", 0);
        loaded.brightnessMode = (bMode <= 1) ? static_cast<BrightnessMode>(bMode) : BrightnessMode::MANUAL;

        uint8_t bLvl = s_prefs.getUChar("b_lvl", 70);
        if (bLvl < 10) bLvl = 10;
        if (bLvl > 100) bLvl = 100;
        loaded.manualBrightness = bLvl;

        uint8_t sMode = s_prefs.getUChar("s_mode", 0);
        loaded.buzzerMode = (sMode <= 2) ? static_cast<BuzzerMode>(sMode) : BuzzerMode::ALL_ON;

        uint8_t sVol = s_prefs.getUChar("s_vol", 100);
        if (sVol > 100) sVol = 100;
        if (sVol < 25) sVol = 25;
        loaded.buzzerVolume = sVol;

        uint8_t tIdx = s_prefs.getUChar("t_idx", 0);
        loaded.themeIndex = (tIdx < 6) ? tIdx : 0;

        uint8_t proto = s_prefs.getUChar("proto", 0);
        loaded.selectedProtocol = (proto <= 9) ? proto : 0;

        uint16_t maxR = s_prefs.getUShort("max_rpm", 8000);
        uint16_t redR = s_prefs.getUShort("red_rpm", 6200);
        uint16_t warnR = s_prefs.getUShort("warn_rpm", 5400);
        if (maxR < 3000 || maxR > 12000) maxR = 8000;
        if (redR < 2000 || redR > maxR) redR = (maxR > 1000) ? maxR - 1000 : maxR;
        if (warnR < 1000 || warnR >= redR) warnR = (redR > 500) ? redR - 500 : redR;
        loaded.maxRpm = maxR;
        loaded.redlineRpm = redR;
        loaded.warnRpm = warnR;

        uint8_t nrMode = s_prefs.getUChar("nav_rail", 0);
        loaded.navRailMode = (nrMode <= 2) ? static_cast<NavRailMode>(nrMode) : NavRailMode::AUTO_HIDE;

        uint8_t opMode = s_prefs.getUChar("op_mode", static_cast<uint8_t>(OperationMode::LAUNCH));
        loaded.operationMode = (opMode <= 1) ? static_cast<OperationMode>(opMode) : OperationMode::LAUNCH;

        uint8_t rotMode = s_prefs.getUChar("scr_rot", 0);
        loaded.screenRotation = (rotMode <= 1) ? static_cast<ScreenRotation>(rotMode) : ScreenRotation::NORMAL;

        uint8_t spdFont = s_prefs.getUChar("spd_font", 0);
        loaded.speedFont = (spdFont < static_cast<uint8_t>(SpeedFont::COUNT)) ? static_cast<SpeedFont>(spdFont) : SpeedFont::DEFAULT_FONT;

        uint8_t catVer = s_prefs.getUChar("cat_ver", 1);

        size_t readDash = s_prefs.getBytes("dash_slots", loaded.dashSlots, sizeof(loaded.dashSlots));
        if (readDash != sizeof(loaded.dashSlots)) {
            const uint8_t defDashSlots[4] = { 6, 16, 11, 7 };
            std::memcpy(loaded.dashSlots, defDashSlots, sizeof(defDashSlots));
        }

        size_t readLen = s_prefs.getBytes("live_slots", loaded.liveSlots, sizeof(loaded.liveSlots));
        if (readLen != sizeof(loaded.liveSlots)) {
            const uint8_t defSlots[6] = { 0, 1, 6, 4, 7, 18 };
            std::memcpy(loaded.liveSlots, defSlots, sizeof(defSlots));
        }

        String mac = s_prefs.getString("bt_mac", "");
        if (mac.length() > 0 && mac.length() < sizeof(loaded.savedBtMac)) {
            std::strncpy(loaded.savedBtMac, mac.c_str(), sizeof(loaded.savedBtMac) - 1);
            loaded.savedBtMac[sizeof(loaded.savedBtMac) - 1] = '\0';
        }

        s_prefs.end();

        {
            SettingsLock lock;
            settings_ = loaded;
        }
        version_.fetch_add(1, std::memory_order_release);
        if (validateAndMigrateSlots(catVer)) {
            save();
        }
        BuzzerController::instance().setMode(loaded.buzzerMode);
        BuzzerController::instance().setVolume(loaded.buzzerVolume);
    }
#endif
}

void SettingsManager::save() {
#ifndef UNIT_TEST
    BuzzerController::instance().mute();
    AppSettings snap = get();
    if (s_prefs.begin("dashstat", false)) {
        s_prefs.putUChar("b_mode", static_cast<uint8_t>(snap.brightnessMode));
        s_prefs.putUChar("b_lvl", snap.manualBrightness);
        s_prefs.putUChar("s_mode", static_cast<uint8_t>(snap.buzzerMode));
        s_prefs.putUChar("s_vol", snap.buzzerVolume);
        s_prefs.putUChar("t_idx", snap.themeIndex);
        s_prefs.putUChar("proto", snap.selectedProtocol);
        s_prefs.putUShort("max_rpm", snap.maxRpm);
        s_prefs.putUShort("red_rpm", snap.redlineRpm);
        s_prefs.putUShort("warn_rpm", snap.warnRpm);
        s_prefs.putUChar("nav_rail", static_cast<uint8_t>(snap.navRailMode));
        s_prefs.putUChar("op_mode", static_cast<uint8_t>(snap.operationMode));
        s_prefs.putUChar("scr_rot", static_cast<uint8_t>(snap.screenRotation));
        s_prefs.putUChar("spd_font", static_cast<uint8_t>(snap.speedFont));
        s_prefs.putUChar("cat_ver", kCatalogSchemaVer);
        s_prefs.putBytes("dash_slots", snap.dashSlots, sizeof(snap.dashSlots));
        s_prefs.putBytes("live_slots", snap.liveSlots, sizeof(snap.liveSlots));
        s_prefs.putString("bt_mac", snap.savedBtMac);
        s_prefs.end();
    }
#endif
    {
        SettingsLock lock;
        isDirty_ = false;
    }
    version_.fetch_add(1, std::memory_order_release);
}

void SettingsManager::saveLiveSlots() {
#ifndef UNIT_TEST
    BuzzerController::instance().mute();
    uint8_t slots[6];
    {
        SettingsLock lock;
        std::memcpy(slots, settings_.liveSlots, sizeof(slots));
    }
    if (s_prefs.begin("dashstat", false)) {
        s_prefs.putUChar("cat_ver", kCatalogSchemaVer);
        s_prefs.putBytes("live_slots", slots, sizeof(slots));
        s_prefs.end();
    }
#endif
    version_.fetch_add(1, std::memory_order_release);
}

void SettingsManager::setBrightnessMode(BrightnessMode mode) {
    bool changed = false;
    {
        SettingsLock lock;
        if (settings_.brightnessMode != mode) {
            settings_.brightnessMode = mode;
            changed = true;
        }
    }
    if (changed) save();
}

void SettingsManager::cycleBrightnessMode() {
    BrightnessMode cur;
    {
        SettingsLock lock;
        cur = settings_.brightnessMode;
    }
    BrightnessMode next = (cur == BrightnessMode::MANUAL) ? BrightnessMode::AUTO : BrightnessMode::MANUAL;
    setBrightnessMode(next);
}

void SettingsManager::setManualBrightness(uint8_t percent) {
    percent = std::max<uint8_t>(10, std::min<uint8_t>(100, percent));
    bool changed = false;
    {
        SettingsLock lock;
        if (settings_.manualBrightness != percent) {
            settings_.manualBrightness = percent;
            changed = true;
        }
    }
    if (changed) save();
}

void SettingsManager::stepManualBrightness() {
    uint8_t cur = 0;
    {
        SettingsLock lock;
        cur = settings_.manualBrightness;
    }
    uint8_t next = cur + 20;
    if (next > 100) next = 20;
    setManualBrightness(next);
}

void SettingsManager::cycleCombinedBrightness() {
    bool changed = false;
    {
        SettingsLock lock;
        BrightnessMode bMode = settings_.brightnessMode;
        uint8_t bVal = settings_.manualBrightness;
        BrightnessMode nextMode = bMode;
        uint8_t nextVal = bVal;

        if (bMode == BrightnessMode::AUTO) {
            nextMode = BrightnessMode::MANUAL;
            nextVal = 40;
        } else if (bVal < 60) {
            nextVal = 60;
        } else if (bVal < 80) {
            nextVal = 80;
        } else if (bVal < 100) {
            nextVal = 100;
        } else {
            nextMode = BrightnessMode::AUTO;
        }

        if (settings_.brightnessMode != nextMode || settings_.manualBrightness != nextVal) {
            settings_.brightnessMode = nextMode;
            settings_.manualBrightness = nextVal;
            changed = true;
        }
    }
    if (changed) save();
}

void SettingsManager::setBuzzerMode(BuzzerMode mode) {
    bool changed = false;
    {
        SettingsLock lock;
        if (settings_.buzzerMode != mode) {
            settings_.buzzerMode = mode;
            changed = true;
        }
    }
    if (changed) {
        BuzzerController::instance().setMode(mode);
        save();
    }
}

void SettingsManager::cycleBuzzerMode() {
    uint8_t cur = 0;
    {
        SettingsLock lock;
        cur = static_cast<uint8_t>(settings_.buzzerMode);
    }
    uint8_t next = (cur + 1) % 3;
    setBuzzerMode(static_cast<BuzzerMode>(next));
}

void SettingsManager::setBuzzerVolume(uint8_t volume) {
    volume = std::min<uint8_t>(100, std::max<uint8_t>(25, volume));
    bool changed = false;
    {
        SettingsLock lock;
        if (settings_.buzzerVolume != volume) {
            settings_.buzzerVolume = volume;
            changed = true;
        }
    }
    if (changed) {
        BuzzerController::instance().setVolume(volume);
        save();
    }
}

void SettingsManager::cycleBuzzerVolume() {
    uint8_t cur = 100;
    {
        SettingsLock lock;
        cur = settings_.buzzerVolume;
    }
    uint8_t next = 25;
    if (cur < 50) next = 50;
    else if (cur < 75) next = 75;
    else if (cur < 100) next = 100;
    else next = 25;
    setBuzzerVolume(next);
}

void SettingsManager::setThemeIndex(uint8_t idx) {
    if (idx >= 6) idx = 0;
    bool changed = false;
    {
        SettingsLock lock;
        if (settings_.themeIndex != idx) {
            settings_.themeIndex = idx;
            changed = true;
        }
    }
    if (changed) save();
}

void SettingsManager::cycleTheme() {
    uint8_t cur = 0;
    {
        SettingsLock lock;
        cur = settings_.themeIndex;
    }
    uint8_t next = (cur + 1) % 6;
    setThemeIndex(next);
}

void SettingsManager::setSavedBtMac(const char* mac) {
    if (!mac) return;
    {
        SettingsLock lock;
        std::strncpy(settings_.savedBtMac, mac, sizeof(settings_.savedBtMac) - 1);
        settings_.savedBtMac[sizeof(settings_.savedBtMac) - 1] = '\0';
    }
    save();
}

void SettingsManager::clearSavedBtMac() {
    {
        SettingsLock lock;
        settings_.savedBtMac[0] = '\0';
    }
    save();
}

void SettingsManager::setSelectedProtocol(uint8_t proto) {
    bool changed = false;
    {
        SettingsLock lock;
        if (settings_.selectedProtocol != proto) {
            settings_.selectedProtocol = proto;
            changed = true;
        }
    }
    if (changed) save();
}

void SettingsManager::setLiveSlot(uint8_t slotIdx, uint8_t catalogIdx) {
    if (slotIdx < 6 && (catalogIdx < UniversalPidCatalog::kTotalPids || catalogIdx == 0xFF)) {
        bool changed = false;
        {
            SettingsLock lock;
            if (settings_.liveSlots[slotIdx] != catalogIdx) {
                settings_.liveSlots[slotIdx] = catalogIdx;
                changed = true;
            }
        }
        if (changed) saveLiveSlots();
    }
}

void SettingsManager::setDashSlot(uint8_t slotIdx, uint8_t catalogIdx) {
    if (slotIdx < 4 && (catalogIdx < UniversalPidCatalog::kTotalPids || catalogIdx == 0xFF)) {
        bool changed = false;
        {
            SettingsLock lock;
            if (settings_.dashSlots[slotIdx] != catalogIdx) {
                settings_.dashSlots[slotIdx] = catalogIdx;
                changed = true;
            }
        }
        if (changed) saveDashSlots();
    }
}

bool SettingsManager::validateAndMigrateSlots(uint8_t catVer) {
    bool needsReset = (catVer != kCatalogSchemaVer);
    SettingsLock lock;
    if (!needsReset) {
        for (uint8_t idx : settings_.dashSlots) {
            if (idx != 0xFF && idx >= UniversalPidCatalog::kTotalPids) {
                needsReset = true;
                break;
            }
        }
    }
    if (!needsReset) {
        for (uint8_t idx : settings_.liveSlots) {
            if (idx != 0xFF && idx >= UniversalPidCatalog::kTotalPids) {
                needsReset = true;
                break;
            }
        }
    }
    if (needsReset) {
        const uint8_t defDash[4] = { 6, 16, 11, 7 };
        const uint8_t defLive[6] = { 0, 1, 6, 4, 7, 18 };
        std::memcpy(settings_.dashSlots, defDash, sizeof(defDash));
        std::memcpy(settings_.liveSlots, defLive, sizeof(defLive));
        version_.fetch_add(1, std::memory_order_release);
    }
    return needsReset;
}

void SettingsManager::resetDashSlotsToDefaults() {
    const uint8_t def[4] = { 6, 16, 11, 7 };
    {
        SettingsLock lock;
        std::memcpy(settings_.dashSlots, def, sizeof(def));
    }
    saveDashSlots();
}

void SettingsManager::resetLiveSlotsToDefaults() {
    const uint8_t def[6] = { 0, 1, 6, 4, 7, 18 };
    {
        SettingsLock lock;
        std::memcpy(settings_.liveSlots, def, sizeof(def));
    }
    saveLiveSlots();
}

void SettingsManager::saveDashSlots() {
#ifndef UNIT_TEST
    BuzzerController::instance().mute();
    uint8_t slots[4];
    {
        SettingsLock lock;
        std::memcpy(slots, settings_.dashSlots, sizeof(slots));
    }
    if (s_prefs.begin("dashstat", false)) {
        s_prefs.putUChar("cat_ver", kCatalogSchemaVer);
        s_prefs.putBytes("dash_slots", slots, sizeof(slots));
        s_prefs.end();
    }
#endif
    version_.fetch_add(1, std::memory_order_release);
}

void SettingsManager::setMaxRpm(uint16_t maxRpm, bool persist) {
    if (maxRpm < 3000) maxRpm = 3000;
    if (maxRpm > 12000) maxRpm = 12000;
    {
        SettingsLock lock;
        settings_.maxRpm = maxRpm;
        if (settings_.redlineRpm > maxRpm) settings_.redlineRpm = maxRpm;
        if (settings_.warnRpm >= settings_.redlineRpm) {
            settings_.warnRpm = (settings_.redlineRpm >= 100) ? settings_.redlineRpm - 100 : 0;
        }
        if (!persist) isDirty_ = true;
    }
    if (persist) save();
    else version_.fetch_add(1, std::memory_order_release);
}

void SettingsManager::stepMaxRpm(bool up, bool persist) {
    uint16_t curMax = 0;
    uint16_t curRed = 0;
    {
        SettingsLock lock;
        curMax = settings_.maxRpm;
        curRed = settings_.redlineRpm;
    }
    if (up) {
        if (curMax + 100 <= 12000) setMaxRpm(curMax + 100, persist);
    } else {
        if (curMax >= 3100 && curMax - 100 >= curRed) {
            setMaxRpm(curMax - 100, persist);
        }
    }
}

void SettingsManager::setRedlineRpm(uint16_t redlineRpm, bool persist) {
    {
        SettingsLock lock;
        if (redlineRpm > settings_.maxRpm) redlineRpm = settings_.maxRpm;
        if (settings_.warnRpm >= redlineRpm) {
            settings_.warnRpm = (redlineRpm >= 500) ? redlineRpm - 500 : 0;
        }
        settings_.redlineRpm = redlineRpm;
        if (!persist) isDirty_ = true;
    }
    if (persist) save();
    else version_.fetch_add(1, std::memory_order_release);
}

void SettingsManager::stepRedlineRpm(bool up, bool persist) {
    uint16_t curRed = 0;
    uint16_t curMax = 0;
    uint16_t curWarn = 0;
    {
        SettingsLock lock;
        curRed = settings_.redlineRpm;
        curMax = settings_.maxRpm;
        curWarn = settings_.warnRpm;
    }
    if (up) {
        if (curRed + 100 <= curMax) setRedlineRpm(curRed + 100, persist);
    } else {
        if (curRed >= 100 && curRed - 100 > curWarn) {
            setRedlineRpm(curRed - 100, persist);
        }
    }
}

void SettingsManager::setWarnRpm(uint16_t warnRpm, bool persist) {
    {
        SettingsLock lock;
        if (warnRpm >= settings_.redlineRpm) {
            warnRpm = (settings_.redlineRpm >= 100) ? settings_.redlineRpm - 100 : 0;
        }
        settings_.warnRpm = warnRpm;
        if (!persist) isDirty_ = true;
    }
    if (persist) save();
    else version_.fetch_add(1, std::memory_order_release);
}

void SettingsManager::stepWarnRpm(bool up, bool persist) {
    uint16_t curWarn = 0;
    uint16_t curRed = 0;
    {
        SettingsLock lock;
        curWarn = settings_.warnRpm;
        curRed = settings_.redlineRpm;
    }
    if (up) {
        if (curWarn + 100 < curRed) setWarnRpm(curWarn + 100, persist);
    } else {
        if (curWarn >= 100) setWarnRpm(curWarn - 100, persist);
    }
}

void SettingsManager::applyRpmPreset(RpmPreset preset) {
    {
        SettingsLock lock;
        switch (preset) {
            case RpmPreset::PETROL_8K:
                settings_.maxRpm = 8000;
                settings_.redlineRpm = 6200;
                settings_.warnRpm = 5400;
                break;
            case RpmPreset::DIESEL_6K:
                settings_.maxRpm = 6000;
                settings_.redlineRpm = 4500;
                settings_.warnRpm = 4000;
                break;
            case RpmPreset::SPORT_9K:
                settings_.maxRpm = 9000;
                settings_.redlineRpm = 8200;
                settings_.warnRpm = 7500;
                break;
        }
    }
    save();
}

void SettingsManager::setNavRailMode(NavRailMode mode) {
    bool changed = false;
    {
        SettingsLock lock;
        if (settings_.navRailMode != mode) {
            settings_.navRailMode = mode;
            changed = true;
        }
    }
    if (changed) save();
}

void SettingsManager::cycleNavRailMode() {
    uint8_t cur = 0;
    {
        SettingsLock lock;
        cur = static_cast<uint8_t>(settings_.navRailMode);
    }
    uint8_t next = (cur + 1) % 3;
    setNavRailMode(static_cast<NavRailMode>(next));
}

void SettingsManager::setOperationMode(OperationMode mode) {
    bool changed = false;
    {
        SettingsLock lock;
        if (settings_.operationMode != mode) {
            settings_.operationMode = mode;
            changed = true;
        }
    }
    if (changed) save();
}

void SettingsManager::setSessionOperationMode(OperationMode mode) {
    {
        SettingsLock lock;
        settings_.operationMode = mode;
    }
    version_.fetch_add(1, std::memory_order_release);
    // Runtime session only: do not persist to NVS
}

void SettingsManager::cycleOperationMode() {
    uint8_t cur = 0;
    {
        SettingsLock lock;
        cur = static_cast<uint8_t>(settings_.operationMode);
    }
    uint8_t next = (cur + 1) % 2;
    setOperationMode(static_cast<OperationMode>(next));
}

void SettingsManager::setScreenRotation(ScreenRotation rot) {
    bool changed = false;
    {
        SettingsLock lock;
        if (settings_.screenRotation != rot) {
            settings_.screenRotation = rot;
            changed = true;
        }
    }
    if (changed) {
        save();
#ifndef UNIT_TEST
        LvglHal::setRotation((rot == ScreenRotation::INVERTED) ? 3 : 1);
#endif
    }
}

void SettingsManager::cycleScreenRotation() {
    ScreenRotation cur;
    {
        SettingsLock lock;
        cur = settings_.screenRotation;
    }
    ScreenRotation next = (cur == ScreenRotation::NORMAL)
                          ? ScreenRotation::INVERTED : ScreenRotation::NORMAL;
    setScreenRotation(next);
}

void SettingsManager::setSpeedFont(SpeedFont font) {
    if (font >= SpeedFont::COUNT) font = SpeedFont::DEFAULT_FONT;
    bool changed = false;
    {
        SettingsLock lock;
        if (settings_.speedFont != font) {
            settings_.speedFont = font;
            changed = true;
        }
    }
    if (changed) save();
}

void SettingsManager::cycleSpeedFont() {
    uint8_t cur = 0;
    {
        SettingsLock lock;
        cur = static_cast<uint8_t>(settings_.speedFont);
    }
    uint8_t next = (cur + 1) % static_cast<uint8_t>(SpeedFont::COUNT);
    setSpeedFont(static_cast<SpeedFont>(next));
}
