#pragma once
#include <stdint.h>
#include "model/settings.h"

enum class BuzzerTone : uint8_t {
    NONE = 0,
    TAP_TICK,
    WARNING_BEEP,
    SUCCESS_TONE,
    ALERT,            // Aleart * (-- -- --)
    ARMED,            // Armed * (-)
    FINISH,           // finish * (-)
    NEW_RECORD,       // Newrecord * (-)
    NAVRAIL_TOUCH,    // NavrailTouch (-)
    BUTTON_BACK,      // Button_back (-)
    BUTTON_SELECT,    // Button_select (-)
    BUTTON_UP_DOWN,   // Button_Up_Down (-)
    SPEED_ALERT,      // Speed_aleart * (-- -- --)
    TOUCH_WHEN_LOCK   // TouchwhenLock (-)
};

class BuzzerController {
public:
    struct BuzzerStep {
        uint16_t freq;       // 0 for silence
        uint16_t durationMs;
    };
    static constexpr uint8_t kMaxPatternSteps = 16;

    static BuzzerController& instance() {
        static BuzzerController inst;
        return inst;
    }

    void init(int pin = 26);
    void playTapTick();
    void playWarningBeep();
    void playSuccessTone();

    // 10 Specific Sound Triggers
    void playAlert();            // Aleart * (-- -- --)
    void playArmed();            // Armed * (-)
    void playFinish();           // finish * (-)
    void playNewRecord();        // Newrecord * (-)
    void playNavrailTouch();     // NavrailTouch (-)
    void playButtonBack();       // Button_back (-)
    void playButtonSelect();     // Button_select (-)
    void playButtonUpDown();     // Button_Up_Down (-)
    void playSpeedAlert();       // Speed_aleart * (-- -- --)
    void playTouchWhenLock();    // TouchwhenLock (-)

    void update(uint32_t nowMs);
    void mute();
    void stop() { mute(); }

    void setMode(BuzzerMode mode) {
        mode_ = mode;
        if (mode_ == BuzzerMode::MUTE) {
            mute();
        }
    }
    BuzzerMode getMode() const { return mode_; }

    void setVolume(uint8_t volume);
    uint8_t getVolume() const { return volume_; }

    BuzzerTone getLastTone() const { return lastTone_; }
    uint32_t getPlayCount() const { return playCount_; }
    void resetTestCounters() {
        playCount_ = 0;
        lastTone_ = BuzzerTone::NONE;
        patternStep_ = 0;
        patternTotalSteps_ = 0;
        nextStepTime_ = 0;
        lastNowMs_ = 0;
    }

    bool canPlay(bool isAlarm) const {
        if (mode_ == BuzzerMode::MUTE) return false;
        if (mode_ == BuzzerMode::ALARM_ONLY && !isAlarm) return false;
        return true;
    }

    static bool isAlarmTone(BuzzerTone tone) {
        switch (tone) {
            case BuzzerTone::WARNING_BEEP:
            case BuzzerTone::SUCCESS_TONE:
            case BuzzerTone::ALERT:
            case BuzzerTone::ARMED:
            case BuzzerTone::FINISH:
            case BuzzerTone::NEW_RECORD:
            case BuzzerTone::SPEED_ALERT:
                return true;
            default:
                return false;
        }
    }

    bool isPatternActive() const { return patternTotalSteps_ > 0; }
    uint8_t getPatternStep() const { return patternStep_; }
    uint8_t getPatternTotalSteps() const { return patternTotalSteps_; }

private:
    BuzzerController() = default;
    int pin_ = 26;
    BuzzerMode mode_ = BuzzerMode::ALL_ON;
    uint8_t volume_ = 100;
    BuzzerTone lastTone_ = BuzzerTone::NONE;
    uint32_t playCount_ = 0;

    // Pattern state machine
    BuzzerStep currentPattern_[kMaxPatternSteps]{};
    uint8_t patternStep_ = 0;
    uint8_t patternTotalSteps_ = 0;
    uint32_t nextStepTime_ = 0;
    uint32_t lastNowMs_ = 0;

    void startPattern(const BuzzerStep* steps, uint8_t count, BuzzerTone tone);
    void playSingleTone(uint16_t freq, uint16_t durationMs, BuzzerTone tone, bool isAlarm);
};
