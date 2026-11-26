#include "ui/theme/theme_manager.h"

ThemeId ThemeManager::currentTheme_ = ThemeId::NITROUS_BLUE;

const ThemePalette ThemeManager::palettes_[static_cast<uint8_t>(ThemeId::COUNT)] = {
    // 0: Nitrous Blue (ฟ้า)
    {
        0x06080C,  // bg
        0x131922,  // cardBg
        0x1E2B3C,  // cardBorder
        0xE9EEF4,  // textPrimary
        0x8B97A7,  // textSecondary
        0x00D2FF,  // accent (Electric Nitrous Blue)
        0xFFB020,  // warning (Amber)
        0xFF4B4B,  // critical (Red)
        "Nitrous Blue"
    },
    // 1: Neon Green (เขียว)
    {
        0x060A08,  // bg
        0x101C14,  // cardBg
        0x1A3826,  // cardBorder
        0xE9F5EE,  // textPrimary
        0x8BA896,  // textSecondary
        0x00E676,  // accent (Vibrant Neon Green)
        0xFFC107,  // warning (Amber)
        0xFF4B4B,  // critical (Red)
        "Neon Green"
    },
    // 2: Raceing Amber (ส้ม)
    {
        0x0C0906,  // bg
        0x1C150E,  // cardBg
        0x382818,  // cardBorder
        0xF7EFE6,  // textPrimary
        0xA69383,  // textSecondary
        0xFF9100,  // accent (High-octane Racing Amber)
        0xFFCC00,  // warning (Yellow/Gold)
        0xFF3322,  // critical (Red)
        "Raceing Amber"
    },
    // 3: Tokyo Pink (ชมพู)
    {
        0x0B060C,  // bg
        0x1A0E1C,  // cardBg
        0x381A3C,  // cardBorder
        0xF8E8F5,  // textPrimary
        0xA88EA6,  // textSecondary
        0xFF2A85,  // accent (Tokyo Pink / Synthwave Magenta)
        0xFFA726,  // warning (Orange)
        0xFF1744,  // critical (Crimson Red)
        "Tokyo Pink"
    },
    // 4: Thrusters Red (แดง)
    {
        0x0C0607,  // bg
        0x1C0E10,  // cardBg
        0x3A181C,  // cardBorder
        0xF8E8E9,  // textPrimary
        0xA88B8E,  // textSecondary
        0xFF1744,  // accent (Laser Thrusters Crimson Red)
        0xFF9800,  // warning (Orange)
        0xFF1744,  // critical (Red)
        "Thrusters Red"
    },
    // 5: Military Gray (เทา)
    {
        0x0A0B0D,  // bg
        0x15181C,  // cardBg
        0x292E36,  // cardBorder
        0xF0F2F5,  // textPrimary
        0x8A94A1,  // textSecondary
        0x9EACB8,  // accent (Tactical Battleship Gray)
        0xFFB300,  // warning (Amber)
        0xFF453A,  // critical (Red)
        "Military Gray"
    }
};

void ThemeManager::setTheme(ThemeId id) {
    if (static_cast<uint8_t>(id) < static_cast<uint8_t>(ThemeId::COUNT)) {
        currentTheme_ = id;
    }
}

ThemeId ThemeManager::getTheme() {
    return currentTheme_;
}

const ThemePalette& ThemeManager::getPalette() {
    return palettes_[static_cast<uint8_t>(currentTheme_)];
}

void ThemeManager::nextTheme() {
    uint8_t next = (static_cast<uint8_t>(currentTheme_) + 1) % static_cast<uint8_t>(ThemeId::COUNT);
    currentTheme_ = static_cast<ThemeId>(next);
}
