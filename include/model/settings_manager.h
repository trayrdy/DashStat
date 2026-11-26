#pragma once
#include <atomic>
#include "model/settings.h"

class SettingsManager {
public:
    static SettingsManager& instance() {
        static SettingsManager inst;
        return inst;
    }

    void begin();
    void load();
    void save();
    void saveLiveSlots();

    AppSettings get() const;
    uint32_t version() const { return version_.load(std::memory_order_acquire); }
#ifdef UNIT_TEST
    void setRawDashSlotForTest(uint8_t slotIdx, uint8_t rawIdx) {
        if (slotIdx < 4) {
            settings_.dashSlots[slotIdx] = rawIdx;
            version_.fetch_add(1, std::memory_order_release);
        }
    }
#endif
    
    void setBrightnessMode(BrightnessMode mode);
    void cycleBrightnessMode();
    void setManualBrightness(uint8_t percent);
    void stepManualBrightness();
    void cycleCombinedBrightness();
    void setBuzzerMode(BuzzerMode mode);
    void cycleBuzzerMode();
    void setBuzzerVolume(uint8_t volume);
    void cycleBuzzerVolume();
    void setThemeIndex(uint8_t idx);
    void cycleTheme();
    void setSavedBtMac(const char* mac);
    void clearSavedBtMac();

    void setSelectedProtocol(uint8_t proto);
    void setLiveSlot(uint8_t slotIdx, uint8_t catalogIdx);
    void setDashSlot(uint8_t slotIdx, uint8_t catalogIdx);
    void resetDashSlotsToDefaults();
    void resetLiveSlotsToDefaults();
    void saveDashSlots();

    static constexpr uint8_t kCatalogSchemaVer = 2;
    bool validateAndMigrateSlots(uint8_t catVer);

    void setMaxRpm(uint16_t maxRpm, bool persist = true);
    void stepMaxRpm(bool up, bool persist = true);
    void setRedlineRpm(uint16_t redlineRpm, bool persist = true);
    void stepRedlineRpm(bool up, bool persist = true);
    void setWarnRpm(uint16_t warnRpm, bool persist = true);
    void stepWarnRpm(bool up, bool persist = true);
    void applyRpmPreset(RpmPreset preset);
    void saveIfDirty();

    void setNavRailMode(NavRailMode mode);
    void cycleNavRailMode();

    void setOperationMode(OperationMode mode);
    void setSessionOperationMode(OperationMode mode);
    void cycleOperationMode();

    void setScreenRotation(ScreenRotation rot);
    void cycleScreenRotation();

    void setSpeedFont(SpeedFont font);
    void cycleSpeedFont();

    void resetToDefaults();
    void reset();

private:
    SettingsManager();
    AppSettings settings_;
    bool isDirty_ = false;
    std::atomic<uint32_t> version_{1};
};
