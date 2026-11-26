#pragma once
#include <stdint.h>

enum class ThemeId : uint8_t {
    NITROUS_BLUE = 0,
    NEON_GREEN = 1,
    RACEING_AMBER = 2,
    TOKYO_PINK = 3,
    THRUSTERS_RED = 4,
    MILITARY_GRAY = 5,
    COUNT = 6,

    // Aliases for backwards compatibility
    STEALTH_DARK = NITROUS_BLUE,
    RACING_AMBER = RACEING_AMBER,
    CYBERPUNK_BLUE = NITROUS_BLUE
};

struct ThemePalette {
    uint32_t bg;
    uint32_t cardBg;
    uint32_t cardBorder;
    uint32_t textPrimary;
    uint32_t textSecondary;
    uint32_t accent;
    uint32_t warning;
    uint32_t critical;
    const char* name;
};

class ThemeManager {
public:
    static void setTheme(ThemeId id);
    static ThemeId getTheme();
    static const ThemePalette& getPalette();
    static void nextTheme();

private:
    static ThemeId currentTheme_;
    static const ThemePalette palettes_[static_cast<uint8_t>(ThemeId::COUNT)];
};
