#pragma once
#include <stdint.h>

enum class BrightnessMode : uint8_t {
    MANUAL = 0,
    AUTO = 1
};

enum class BuzzerMode : uint8_t {
    ALL_ON = 0,     // Touch click + warning alarms
    ALARM_ONLY = 1, // Only critical alarms, touch clicks muted
    MUTE = 2        // Silent
};

enum class NavRailMode : uint8_t {
    AUTO_HIDE = 0,
    ALWAYS_ON = 1,
    TAP_TO_CALL = 2
};

enum class RpmPreset : uint8_t {
    PETROL_8K = 0,
    DIESEL_6K = 1,
    SPORT_9K = 2
};

enum class OperationMode : uint8_t {
    LAUNCH = 0,
    DEMO = 1
};

enum class ScreenRotation : uint8_t {
    NORMAL = 0,    // 0 = Normal Landscape (Rotation 1)
    INVERTED = 1   // 1 = Inverted Landscape 180° (Rotation 3)
};

enum class SpeedFont : uint8_t {
    DEFAULT_FONT = 0, // Barlow Condensed 64px
    BOLDPIXELS = 1,   // Boldpixels 64px
    DIGITTECH = 2,    // Digittech (7-Segment) 64px
    FASTER_ONE = 3,   // FasterOne 64px
    KINESTHESIA = 4,  // Kinesthesia 64px
    PIXEL = 5,        // Pixel 64px
    REN = 6,          // Ren 64px
    UNIWARS = 7,      // Uniwars 64px
    COUNT = 8
};

struct AppSettings {
    BrightnessMode brightnessMode = BrightnessMode::MANUAL;
    uint8_t manualBrightness = 70; // 10% - 100%
    BuzzerMode buzzerMode = BuzzerMode::ALL_ON;
    uint8_t buzzerVolume = 100; // 25% - 100%
    uint8_t themeIndex = 0; // 0=Nitrous Blue, 1=Neon Green, 2=Raceing Amber, 3=Tokyo Pink, 4=Thrusters Red, 5=Military Gray
    char savedBtMac[18] = "";
    uint8_t selectedProtocol = 0; // 0=Auto (ATSP0), 6=CAN11/500, etc.
    uint8_t liveSlots[6] = { 0, 1, 6, 4, 7, 18 }; // RPM, Speed, Coolant, Throttle, IAT, Timing
    uint8_t dashSlots[4] = { 6, 16, 11, 7 };       // Coolant, Batt, Boost, IAT
    uint16_t maxRpm = 8000;
    uint16_t redlineRpm = 6200;
    uint16_t warnRpm = 5400;
    NavRailMode navRailMode = NavRailMode::AUTO_HIDE;
    OperationMode operationMode = OperationMode::LAUNCH;
    ScreenRotation screenRotation = ScreenRotation::NORMAL;
    SpeedFont speedFont = SpeedFont::DEFAULT_FONT;
};
