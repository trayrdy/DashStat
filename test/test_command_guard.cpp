#include <unity.h>
#include <cstring>
#include "obd/command_guard.h"
#include "obd/mock_obd.h"
#include "model/vehicle_state.h"
#include "ui/theme/theme_manager.h"
#include "hal/buzzer_controller.h"
#include "model/settings_manager.h"
#include "transport/mock_transport.h"
#include "obd/obd_parser.h"
#include "obd/pid_scheduler.h"
#include "obd/elm327_engine.h"
#include "obd/universal_pid_catalog.h"
#include "model/trip_computer.h"
#include "ui/pages/diagnostics_page.h"
#include "ui/pages/settings_page.h"
#include "model/alert_monitor.h"

void setUp(void) {
    CommandGuard::resetBlockedCount();
}

void tearDown(void) {
}

// ================= CommandGuard Allowed Whitelist =================

void test_allowed_mode_01_realtime(void) {
    TEST_ASSERT_EQUAL(GuardResult::ALLOWED, CommandGuard::validate("010C"));       // RPM
    TEST_ASSERT_EQUAL(GuardResult::ALLOWED, CommandGuard::validate("010D\r"));     // Speed + CR
    TEST_ASSERT_EQUAL(GuardResult::ALLOWED, CommandGuard::validate(" 0105 \n"));   // Coolant + spaces
    TEST_ASSERT_EQUAL(GuardResult::ALLOWED, CommandGuard::validate("0111"));       // Throttle
    TEST_ASSERT_EQUAL(GuardResult::ALLOWED, CommandGuard::validate("010C1"));      // RPM with count hint
    TEST_ASSERT_EQUAL(GuardResult::ALLOWED, CommandGuard::validate("010C0D051"));  // Multi-PID CAN
    TEST_ASSERT_EQUAL(0, CommandGuard::getBlockedCount());
}

void test_allowed_mode_03_07_read_dtc(void) {
    TEST_ASSERT_EQUAL(GuardResult::ALLOWED, CommandGuard::validate("03"));
    TEST_ASSERT_EQUAL(GuardResult::ALLOWED, CommandGuard::validate("07"));
    TEST_ASSERT_EQUAL(0, CommandGuard::getBlockedCount());
}

void test_allowed_mode_09_vin(void) {
    TEST_ASSERT_EQUAL(GuardResult::ALLOWED, CommandGuard::validate("0902"));
    TEST_ASSERT_EQUAL(0, CommandGuard::getBlockedCount());
}

void test_allowed_at_commands(void) {
    TEST_ASSERT_EQUAL(GuardResult::ALLOWED, CommandGuard::validate("ATZ"));
    TEST_ASSERT_EQUAL(GuardResult::ALLOWED, CommandGuard::validate("ate0"));
    TEST_ASSERT_EQUAL(GuardResult::ALLOWED, CommandGuard::validate("ATL0"));
    TEST_ASSERT_EQUAL(GuardResult::ALLOWED, CommandGuard::validate("ATS0"));
    TEST_ASSERT_EQUAL(GuardResult::ALLOWED, CommandGuard::validate("ATH0"));
    TEST_ASSERT_EQUAL(GuardResult::ALLOWED, CommandGuard::validate("ATSP6"));
    TEST_ASSERT_EQUAL(GuardResult::ALLOWED, CommandGuard::validate("ATSPA"));
    TEST_ASSERT_EQUAL(GuardResult::ALLOWED, CommandGuard::validate("ATSP0"));
    TEST_ASSERT_EQUAL(GuardResult::ALLOWED, CommandGuard::validate("ATAT2"));
    TEST_ASSERT_EQUAL(GuardResult::ALLOWED, CommandGuard::validate("ATST32"));
    TEST_ASSERT_EQUAL(GuardResult::ALLOWED, CommandGuard::validate("ATRV"));
    TEST_ASSERT_EQUAL(GuardResult::ALLOWED, CommandGuard::validate("ATI"));
    TEST_ASSERT_EQUAL(GuardResult::ALLOWED, CommandGuard::validate("AT@1"));
    // Soft break / prompt refresh (Rule R8 safe)
    TEST_ASSERT_EQUAL(GuardResult::ALLOWED, CommandGuard::validate("\r"));
    TEST_ASSERT_EQUAL(GuardResult::ALLOWED, CommandGuard::validate("\r\r"));
    TEST_ASSERT_EQUAL(0, CommandGuard::getBlockedCount());
}

void test_protocol_formatting_and_guard(void) {
    for (uint8_t proto = 0; proto <= 10; ++proto) {
        char protoCmd[12];
        if (proto == 10) {
            std::snprintf(protoCmd, sizeof(protoCmd), "ATSPA");
        } else {
            std::snprintf(protoCmd, sizeof(protoCmd), "ATSP%u", proto);
        }
        TEST_ASSERT_EQUAL(GuardResult::ALLOWED, CommandGuard::validate(protoCmd));
    }
}

void test_universal_pid_dashboard_names(void) {
    // 1. Verify primary dashboard corner slot names
    TEST_ASSERT_EQUAL_STRING("COOLANT", UniversalPidCatalog::getDashboardName(0x05));
    TEST_ASSERT_EQUAL_STRING("BATTERY", UniversalPidCatalog::getDashboardName(0xFF));
    TEST_ASSERT_EQUAL_STRING("BATTERY", UniversalPidCatalog::getDashboardName(0x42));
    TEST_ASSERT_EQUAL_STRING("INTAKE AIR", UniversalPidCatalog::getDashboardName(0x0F));

    // 2. Verify common secondary telemetry parameters
    TEST_ASSERT_EQUAL_STRING("ENG LOAD", UniversalPidCatalog::getDashboardName(0x04));
    TEST_ASSERT_EQUAL_STRING("THROTTLE", UniversalPidCatalog::getDashboardName(0x11));
    TEST_ASSERT_EQUAL_STRING("BOOST", UniversalPidCatalog::getDashboardName(0xF0));
    TEST_ASSERT_EQUAL_STRING("RPM", UniversalPidCatalog::getDashboardName(0x0C));
    TEST_ASSERT_EQUAL_STRING("SPEED", UniversalPidCatalog::getDashboardName(0x0D));

    // 3. Verify that every catalog PID produces a concise non-empty name (<= 12 chars)
    for (size_t i = 0; i < UniversalPidCatalog::kTotalPids; ++i) {
        const auto& def = UniversalPidCatalog::get(i);
        const char* dName = UniversalPidCatalog::getDashboardName(def.pidHex);
        TEST_ASSERT_NOT_NULL(dName);
        TEST_ASSERT_TRUE(std::strlen(dName) > 0);
        TEST_ASSERT_TRUE(std::strlen(dName) <= 12);
    }
}

// ================= CRITICAL R8 SAFETY TESTS =================

void test_blocked_mode_04_clear_dtc(void) {
    TEST_ASSERT_EQUAL(GuardResult::BLOCKED_BLACKLIST, CommandGuard::validate("04"));
    TEST_ASSERT_EQUAL(GuardResult::BLOCKED_BLACKLIST, CommandGuard::validate("0400"));
    TEST_ASSERT_EQUAL(GuardResult::BLOCKED_BLACKLIST, CommandGuard::validate(" 04 \r\n"));
    TEST_ASSERT_EQUAL(GuardResult::BLOCKED_BLACKLIST, CommandGuard::validate("14"));
    TEST_ASSERT_GREATER_THAN(0, CommandGuard::getBlockedCount());
}

void test_blocked_mode_08_actuator_control(void) {
    TEST_ASSERT_EQUAL(GuardResult::BLOCKED_BLACKLIST, CommandGuard::validate("08"));
    TEST_ASSERT_EQUAL(GuardResult::BLOCKED_BLACKLIST, CommandGuard::validate("080102"));
}

void test_blocked_dangerous_uds_modes(void) {
    TEST_ASSERT_EQUAL(GuardResult::BLOCKED_BLACKLIST, CommandGuard::validate("1001")); // Session control
    TEST_ASSERT_EQUAL(GuardResult::BLOCKED_BLACKLIST, CommandGuard::validate("1101")); // ECU reset
    TEST_ASSERT_EQUAL(GuardResult::BLOCKED_BLACKLIST, CommandGuard::validate("2701")); // Security access
    TEST_ASSERT_EQUAL(GuardResult::BLOCKED_BLACKLIST, CommandGuard::validate("2800")); // Comm control
    TEST_ASSERT_EQUAL(GuardResult::BLOCKED_BLACKLIST, CommandGuard::validate("2E0102")); // Write data
    TEST_ASSERT_EQUAL(GuardResult::BLOCKED_BLACKLIST, CommandGuard::validate("2F0102")); // IO control
    TEST_ASSERT_EQUAL(GuardResult::BLOCKED_BLACKLIST, CommandGuard::validate("3101")); // Routine control
    TEST_ASSERT_EQUAL(GuardResult::BLOCKED_BLACKLIST, CommandGuard::validate("3400")); // Request download
    TEST_ASSERT_EQUAL(GuardResult::BLOCKED_BLACKLIST, CommandGuard::validate("8500")); // Control DTC setting
}

void test_blocked_raw_sniffers_and_transmitters(void) {
    TEST_ASSERT_EQUAL(GuardResult::BLOCKED_BLACKLIST, CommandGuard::validate("ATMA"));
    TEST_ASSERT_EQUAL(GuardResult::BLOCKED_BLACKLIST, CommandGuard::validate("ATBD"));
    TEST_ASSERT_EQUAL(GuardResult::BLOCKED_BLACKLIST, CommandGuard::validate("STPX"));
}

void test_blocked_unknown_unwhitelisted(void) {
    TEST_ASSERT_EQUAL(GuardResult::BLOCKED_NOT_WHITELISTED, CommandGuard::validate("FOOBAR"));
    TEST_ASSERT_EQUAL(GuardResult::BLOCKED_NOT_WHITELISTED, CommandGuard::validate("CUSTOM_CMD"));
    TEST_ASSERT_EQUAL(GuardResult::BLOCKED_MALFORMED, CommandGuard::validate(""));
    TEST_ASSERT_EQUAL(GuardResult::BLOCKED_MALFORMED, CommandGuard::validate("   \r\n"));
}

void test_mock_obd_allowed_queries(void) {
    MockOBD mock;
    mock.update(5000); // 5s into cycle (accelerating)

    char buf[32]{};
    TEST_ASSERT_TRUE(mock.query("010C1", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_UINT32(0, std::strncmp(buf, "41 0C", 5));

    TEST_ASSERT_TRUE(mock.query("010D1", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_UINT32(0, std::strncmp(buf, "41 0D", 5));

    TEST_ASSERT_TRUE(mock.query("ATRV", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_UINT32(0, std::strncmp(buf, "14.1V", 5));
}

void test_mock_obd_blocks_dangerous_command(void) {
    MockOBD mock;
    char buf[32]{};
    TEST_ASSERT_FALSE(mock.query("04", buf, sizeof(buf))); // Mode 04 Clear DTC (Illegal under R8)
    TEST_ASSERT_EQUAL_STRING("ERROR", buf);
    TEST_ASSERT_GREATER_THAN(0, CommandGuard::getBlockedCount());
}

// ================= VehicleStateManager & Concurrency Tests =================

void test_vehicle_state_initial_values(void) {
    VehicleStateManager mgr;
    VehicleState s = mgr.getSnapshot();

    TEST_ASSERT_EQUAL_UINT16(0, s.rpm);
    TEST_ASSERT_EQUAL_UINT8(0, s.speedKmh);
    TEST_ASSERT_EQUAL_INT8(0, s.coolantTempC);
    TEST_ASSERT_EQUAL_UINT8(0, s.engineLoad);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, s.batteryVoltage);
    TEST_ASSERT_FALSE(s.isConnected);
    TEST_ASSERT_EQUAL_CHAR(' ', s.gear);
    TEST_ASSERT_FALSE(s.handbrakeActive);
    TEST_ASSERT_FALSE(s.isDemoMode);
}

void test_vehicle_state_update_and_snapshot(void) {
    VehicleStateManager mgr;

    VehicleState newState{};
    newState.timestampMs = 12345;
    newState.rpm = 2450;
    newState.speedKmh = 68;
    newState.coolantTempC = 88;
    newState.batteryVoltage = 14.15f;
    newState.throttlePercent = 32;
    newState.engineLoad = 48;
    newState.isConnected = true;
    newState.isEngineRunning = true;
    newState.gear = ' ';
    newState.handbrakeActive = true;
    newState.isDemoMode = true;

    mgr.update(newState);

    VehicleState snapshot = mgr.getSnapshot();
    TEST_ASSERT_EQUAL_UINT32(12345, snapshot.timestampMs);
    TEST_ASSERT_EQUAL_UINT16(2450, snapshot.rpm);
    TEST_ASSERT_EQUAL_UINT8(68, snapshot.speedKmh);
    TEST_ASSERT_EQUAL_INT8(88, snapshot.coolantTempC);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 14.15f, snapshot.batteryVoltage);
    TEST_ASSERT_EQUAL_UINT8(32, snapshot.throttlePercent);
    TEST_ASSERT_EQUAL_UINT8(48, snapshot.engineLoad);
    TEST_ASSERT_TRUE(snapshot.isConnected);
    TEST_ASSERT_TRUE(snapshot.isEngineRunning);
    TEST_ASSERT_EQUAL_CHAR(' ', snapshot.gear);
    TEST_ASSERT_TRUE(snapshot.handbrakeActive);
    TEST_ASSERT_TRUE(snapshot.isDemoMode);
}

void test_vehicle_state_double_buffer_alternation(void) {
    VehicleStateManager mgr;

    for (uint16_t r = 1000; r <= 3000; r += 500) {
        VehicleState s{};
        s.rpm = r;
        mgr.update(s);
        TEST_ASSERT_EQUAL_UINT16(r, mgr.getSnapshot().rpm);
    }
}

void test_mock_obd_interface_and_guard(void) {
    MockOBD mock;

    TEST_ASSERT_TRUE(mock.begin());
    TEST_ASSERT_TRUE(mock.isConnected());
    TEST_ASSERT_NOT_NULL(mock.getName());

    char buf[32] = {0};
    bool res = mock.query("010C", buf, sizeof(buf));
    TEST_ASSERT_TRUE(res);
    TEST_ASSERT_NOT_EQUAL(0, std::strlen(buf));

    res = mock.query("04", buf, sizeof(buf));
    TEST_ASSERT_FALSE(res);
    TEST_ASSERT_EQUAL_STRING("ERROR", buf);
}

void test_mock_obd_simulation(void) {
    MockOBD mock;

    // Test voltage across entire alternator cycle to verify no unsigned underflow
    for (uint32_t t = 0; t < 5000; t += 200) {
        mock.update(200);
        char rx[32] = {0};
        TEST_ASSERT_TRUE(mock.query("ATRV", rx, sizeof(rx)));
        float v = 0.0f;
        TEST_ASSERT_EQUAL(1, std::sscanf(rx, "%fV", &v));
        TEST_ASSERT_TRUE(v >= 13.8f && v <= 14.5f);
    }

    char rx[32] = {0};
    TEST_ASSERT_TRUE(mock.query("010C", rx, sizeof(rx)));
    TEST_ASSERT_TRUE(mock.query("010D", rx, sizeof(rx)));
    TEST_ASSERT_TRUE(mock.query("0105", rx, sizeof(rx)));
    TEST_ASSERT_TRUE(mock.query("0111", rx, sizeof(rx)));
}

void test_mock_obd_parked_mode(void) {
    MockOBD mock;
    mock.setParkedMode(true);
    TEST_ASSERT_TRUE(mock.isParkedMode());

    mock.update(1000);
    const auto& telem = mock.getTelemetry();
    TEST_ASSERT_EQUAL_UINT8(0, telem.speed);
    TEST_ASSERT_EQUAL_INT8('P', telem.gear);
    TEST_ASSERT_EQUAL_UINT8(0, telem.throttle);
    TEST_ASSERT_TRUE(telem.rpm >= 740 && telem.rpm <= 770);
    TEST_ASSERT_EQUAL_INT16(88, telem.coolantTemp);
}

// ================= ThemeManager Tests =================

void test_theme_manager_default_theme(void) {
    ThemeManager::setTheme(ThemeId::NITROUS_BLUE);
    TEST_ASSERT_EQUAL(ThemeId::NITROUS_BLUE, ThemeManager::getTheme());
    const ThemePalette& pal = ThemeManager::getPalette();
    TEST_ASSERT_EQUAL_HEX32(0x06080C, pal.bg);
    TEST_ASSERT_EQUAL_HEX32(0x00D2FF, pal.accent);
    TEST_ASSERT_EQUAL_STRING("Nitrous Blue", pal.name);
}

void test_theme_manager_palette_values(void) {
    ThemeManager::setTheme(ThemeId::NEON_GREEN);
    TEST_ASSERT_EQUAL(ThemeId::NEON_GREEN, ThemeManager::getTheme());
    const ThemePalette& green = ThemeManager::getPalette();
    TEST_ASSERT_EQUAL_HEX32(0x060A08, green.bg);
    TEST_ASSERT_EQUAL_HEX32(0x00E676, green.accent);
    TEST_ASSERT_EQUAL_STRING("Neon Green", green.name);

    ThemeManager::setTheme(ThemeId::RACEING_AMBER);
    TEST_ASSERT_EQUAL(ThemeId::RACEING_AMBER, ThemeManager::getTheme());
    const ThemePalette& amber = ThemeManager::getPalette();
    TEST_ASSERT_EQUAL_HEX32(0x0C0906, amber.bg);
    TEST_ASSERT_EQUAL_HEX32(0xFF9100, amber.accent);
    TEST_ASSERT_EQUAL_STRING("Raceing Amber", amber.name);

    ThemeManager::setTheme(ThemeId::TOKYO_PINK);
    TEST_ASSERT_EQUAL(ThemeId::TOKYO_PINK, ThemeManager::getTheme());
    const ThemePalette& pink = ThemeManager::getPalette();
    TEST_ASSERT_EQUAL_HEX32(0x0B060C, pink.bg);
    TEST_ASSERT_EQUAL_HEX32(0xFF2A85, pink.accent);
    TEST_ASSERT_EQUAL_STRING("Tokyo Pink", pink.name);

    ThemeManager::setTheme(ThemeId::THRUSTERS_RED);
    TEST_ASSERT_EQUAL(ThemeId::THRUSTERS_RED, ThemeManager::getTheme());
    const ThemePalette& red = ThemeManager::getPalette();
    TEST_ASSERT_EQUAL_HEX32(0x0C0607, red.bg);
    TEST_ASSERT_EQUAL_HEX32(0xFF1744, red.accent);
    TEST_ASSERT_EQUAL_STRING("Thrusters Red", red.name);

    ThemeManager::setTheme(ThemeId::MILITARY_GRAY);
    TEST_ASSERT_EQUAL(ThemeId::MILITARY_GRAY, ThemeManager::getTheme());
    const ThemePalette& gray = ThemeManager::getPalette();
    TEST_ASSERT_EQUAL_HEX32(0x0A0B0D, gray.bg);
    TEST_ASSERT_EQUAL_HEX32(0x9EACB8, gray.accent);
    TEST_ASSERT_EQUAL_STRING("Military Gray", gray.name);
}

void test_theme_manager_cycle_themes(void) {
    ThemeManager::setTheme(ThemeId::NITROUS_BLUE);
    ThemeManager::nextTheme();
    TEST_ASSERT_EQUAL(ThemeId::NEON_GREEN, ThemeManager::getTheme());
    ThemeManager::nextTheme();
    TEST_ASSERT_EQUAL(ThemeId::RACEING_AMBER, ThemeManager::getTheme());
    ThemeManager::nextTheme();
    TEST_ASSERT_EQUAL(ThemeId::TOKYO_PINK, ThemeManager::getTheme());
    ThemeManager::nextTheme();
    TEST_ASSERT_EQUAL(ThemeId::THRUSTERS_RED, ThemeManager::getTheme());
    ThemeManager::nextTheme();
    TEST_ASSERT_EQUAL(ThemeId::MILITARY_GRAY, ThemeManager::getTheme());
    ThemeManager::nextTheme();
    TEST_ASSERT_EQUAL(ThemeId::NITROUS_BLUE, ThemeManager::getTheme());
}

// ================= BuzzerController Tests =================

void test_buzzer_controller_tones(void) {
    BuzzerController& bz = BuzzerController::instance();
    bz.init(26);
    bz.resetTestCounters();

    bz.playTapTick();
    TEST_ASSERT_EQUAL(BuzzerTone::TAP_TICK, bz.getLastTone());
    TEST_ASSERT_EQUAL(1, bz.getPlayCount());

    bz.playWarningBeep();
    TEST_ASSERT_EQUAL(BuzzerTone::WARNING_BEEP, bz.getLastTone());
    TEST_ASSERT_EQUAL(2, bz.getPlayCount());

    bz.playSuccessTone();
    TEST_ASSERT_EQUAL(BuzzerTone::SUCCESS_TONE, bz.getLastTone());
    TEST_ASSERT_EQUAL(3, bz.getPlayCount());

    bz.setVolume(75);
    TEST_ASSERT_EQUAL(75, bz.getVolume());
    bz.setVolume(150); // Clamped to 100
    TEST_ASSERT_EQUAL(100, bz.getVolume());
}

void test_settings_manager_defaults_and_clamping(void) {
    SettingsManager& sm = SettingsManager::instance();
    sm.resetToDefaults();

    TEST_ASSERT_EQUAL(BrightnessMode::MANUAL, sm.get().brightnessMode);
    TEST_ASSERT_EQUAL(70, sm.get().manualBrightness);
    TEST_ASSERT_EQUAL(BuzzerMode::ALL_ON, sm.get().buzzerMode);
    TEST_ASSERT_EQUAL(100, sm.get().buzzerVolume);
    TEST_ASSERT_EQUAL(0, sm.get().themeIndex);
    TEST_ASSERT_EQUAL(OperationMode::LAUNCH, sm.get().operationMode);
    TEST_ASSERT_EQUAL(ScreenRotation::NORMAL, sm.get().screenRotation);
    TEST_ASSERT_EQUAL(SpeedFont::DEFAULT_FONT, sm.get().speedFont);

    // Clamping speed font
    sm.setSpeedFont(static_cast<SpeedFont>(99));
    TEST_ASSERT_EQUAL(SpeedFont::DEFAULT_FONT, sm.get().speedFont);

    // Clamping manual brightness between 10% and 100%
    sm.setManualBrightness(5);
    TEST_ASSERT_EQUAL(10, sm.get().manualBrightness);

    sm.setManualBrightness(150);
    TEST_ASSERT_EQUAL(100, sm.get().manualBrightness);

    sm.setManualBrightness(50);
    TEST_ASSERT_EQUAL(50, sm.get().manualBrightness);
}

void test_settings_manager_cycles(void) {
    SettingsManager& sm = SettingsManager::instance();
    sm.resetToDefaults();

    // Brightness mode cycle: MANUAL -> AUTO -> MANUAL
    sm.cycleBrightnessMode();
    TEST_ASSERT_EQUAL(BrightnessMode::AUTO, sm.get().brightnessMode);
    sm.cycleBrightnessMode();
    TEST_ASSERT_EQUAL(BrightnessMode::MANUAL, sm.get().brightnessMode);

    // Step manual brightness: 70 -> 90 -> 20 -> 40
    sm.stepManualBrightness();
    TEST_ASSERT_EQUAL(90, sm.get().manualBrightness);
    sm.stepManualBrightness();
    TEST_ASSERT_EQUAL(20, sm.get().manualBrightness);

    // Buzzer mode cycle: ALL_ON -> ALARM_ONLY -> MUTE -> ALL_ON
    sm.cycleBuzzerMode();
    TEST_ASSERT_EQUAL(BuzzerMode::ALARM_ONLY, sm.get().buzzerMode);
    TEST_ASSERT_EQUAL(BuzzerMode::ALARM_ONLY, BuzzerController::instance().getMode());
    sm.cycleBuzzerMode();
    TEST_ASSERT_EQUAL(BuzzerMode::MUTE, sm.get().buzzerMode);
    TEST_ASSERT_EQUAL(BuzzerMode::MUTE, BuzzerController::instance().getMode());
    sm.cycleBuzzerMode();
    TEST_ASSERT_EQUAL(BuzzerMode::ALL_ON, sm.get().buzzerMode);
    TEST_ASSERT_EQUAL(BuzzerMode::ALL_ON, BuzzerController::instance().getMode());

    // Buzzer volume cycle: 100 -> 25 -> 50 -> 75 -> 100
    TEST_ASSERT_EQUAL(100, sm.get().buzzerVolume);
    TEST_ASSERT_EQUAL(100, BuzzerController::instance().getVolume());
    sm.cycleBuzzerVolume();
    TEST_ASSERT_EQUAL(25, sm.get().buzzerVolume);
    TEST_ASSERT_EQUAL(25, BuzzerController::instance().getVolume());
    sm.cycleBuzzerVolume();
    TEST_ASSERT_EQUAL(50, sm.get().buzzerVolume);
    TEST_ASSERT_EQUAL(50, BuzzerController::instance().getVolume());
    sm.cycleBuzzerVolume();
    TEST_ASSERT_EQUAL(75, sm.get().buzzerVolume);
    TEST_ASSERT_EQUAL(75, BuzzerController::instance().getVolume());
    sm.cycleBuzzerVolume();
    TEST_ASSERT_EQUAL(100, sm.get().buzzerVolume);
    TEST_ASSERT_EQUAL(100, BuzzerController::instance().getVolume());

    // Screen rotation cycle: NORMAL -> INVERTED -> NORMAL
    sm.cycleScreenRotation();
    TEST_ASSERT_EQUAL(ScreenRotation::INVERTED, sm.get().screenRotation);
    sm.cycleScreenRotation();
    TEST_ASSERT_EQUAL(ScreenRotation::NORMAL, sm.get().screenRotation);

    // Speed font cycle: 0 -> 1 -> 2 -> ... -> 8 -> 0
    TEST_ASSERT_EQUAL(SpeedFont::DEFAULT_FONT, sm.get().speedFont);
    sm.cycleSpeedFont();
    TEST_ASSERT_EQUAL(SpeedFont::BOLDPIXELS, sm.get().speedFont);
    sm.cycleSpeedFont();
    TEST_ASSERT_EQUAL(SpeedFont::DIGITTECH, sm.get().speedFont);
    for (int i = 2; i < static_cast<int>(SpeedFont::COUNT); ++i) {
        sm.cycleSpeedFont();
    }
    TEST_ASSERT_EQUAL(SpeedFont::DEFAULT_FONT, sm.get().speedFont);

    // Operation mode & session-only mode
    sm.setOperationMode(OperationMode::LAUNCH);
    TEST_ASSERT_EQUAL(OperationMode::LAUNCH, sm.get().operationMode);
    sm.setSessionOperationMode(OperationMode::DEMO);
    TEST_ASSERT_EQUAL(OperationMode::DEMO, sm.get().operationMode);
}

void test_buzzer_controller_mode_filtering(void) {
    BuzzerController& bz = BuzzerController::instance();
    bz.resetTestCounters();

    // 1. ALL_ON mode: both tap tick and warnings play
    bz.setMode(BuzzerMode::ALL_ON);
    bz.playTapTick();
    TEST_ASSERT_EQUAL(1, bz.getPlayCount());
    bz.playWarningBeep();
    TEST_ASSERT_EQUAL(2, bz.getPlayCount());

    // 2. ALARM_ONLY mode: tap tick suppressed, warnings still play
    bz.setMode(BuzzerMode::ALARM_ONLY);
    bz.playTapTick(); // suppressed!
    TEST_ASSERT_EQUAL(2, bz.getPlayCount());
    bz.playWarningBeep(); // played!
    TEST_ASSERT_EQUAL(3, bz.getPlayCount());

    // 3. MUTE mode: all sounds suppressed
    bz.setMode(BuzzerMode::MUTE);
    bz.playTapTick(); // suppressed!
    bz.playWarningBeep(); // suppressed!
    bz.playSuccessTone(); // suppressed!
    TEST_ASSERT_EQUAL(3, bz.getPlayCount());

    // Restore to ALL_ON
    bz.setMode(BuzzerMode::ALL_ON);
}

void test_settings_manager_profile_and_bt_mac(void) {
    SettingsManager& sm = SettingsManager::instance();
    sm.resetToDefaults();

    TEST_ASSERT_EQUAL_UINT8(0, sm.get().selectedProtocol);
    TEST_ASSERT_EQUAL_STRING("", sm.get().savedBtMac);

    // BT MAC storage
    sm.setSavedBtMac("7C:D5:44:3C:1C:0C");
    TEST_ASSERT_EQUAL_STRING("7C:D5:44:3C:1C:0C", sm.get().savedBtMac);

    sm.clearSavedBtMac();
    TEST_ASSERT_EQUAL_STRING("", sm.get().savedBtMac);
}

void test_mock_transport_lifecycle(void) {
    MockTransport transport;
    TEST_ASSERT_FALSE(transport.isConnected());
    TEST_ASSERT_EQUAL(TransportState::DISCONNECTED, transport.getState());

    // Connect
    TEST_ASSERT_TRUE(transport.connect("7C:D5:44:3C:1C:0C"));
    TEST_ASSERT_TRUE(transport.isConnected());
    TEST_ASSERT_EQUAL(TransportState::CONNECTED, transport.getState());
    TEST_ASSERT_EQUAL_STRING("7C:D5:44:3C:1C:0C", transport.getConnectedAddress());

    // Write
    const char* cmd = "010C\r";
    TEST_ASSERT_EQUAL(5, transport.write(reinterpret_cast<const uint8_t*>(cmd), 5));
    TEST_ASSERT_EQUAL_STRING("010C\r", transport.getLastWritten().c_str());

    // Feed response & read
    transport.feedResponse("41 0C 1A F8\r\n>");
    TEST_ASSERT_EQUAL(14, transport.available());
    uint8_t buf[32];
    size_t n = transport.read(buf, sizeof(buf));
    TEST_ASSERT_EQUAL(14, n);
    buf[n] = '\0';
    TEST_ASSERT_EQUAL_STRING("41 0C 1A F8\r\n>", reinterpret_cast<char*>(buf));

    // Disconnect
    transport.disconnect();
    TEST_ASSERT_FALSE(transport.isConnected());
    TEST_ASSERT_EQUAL(TransportState::DISCONNECTED, transport.getState());
}

// ================= Task 2: ObdParser Tests =================

void test_obd_parser_mode01_core_pids(void) {
    uint8_t data[8]{};
    size_t len = 0;
    bool isNeg = false;

    TEST_ASSERT_EQUAL(ParseStatus::OK, ObdParser::parseMode01PidResponse("41 0C 1A F8", 0x0C, data, sizeof(data), len, isNeg));
    TEST_ASSERT_EQUAL_UINT32(2, len);
    TEST_ASSERT_EQUAL_UINT16(1726, static_cast<uint16_t>(((data[0] * 256U) + data[1]) / 4U));

    TEST_ASSERT_EQUAL(ParseStatus::OK, ObdParser::parseMode01PidResponse("410C1AF8\r>", 0x0C, data, sizeof(data), len, isNeg));
    TEST_ASSERT_EQUAL_UINT32(2, len);
    TEST_ASSERT_EQUAL_UINT16(1726, static_cast<uint16_t>(((data[0] * 256U) + data[1]) / 4U));

    TEST_ASSERT_EQUAL(ParseStatus::NO_DATA, ObdParser::parseMode01PidResponse("NO DATA\r>", 0x0C, data, sizeof(data), len, isNeg));
    TEST_ASSERT_EQUAL(ParseStatus::ERROR_RESPONSE, ObdParser::parseMode01PidResponse("CAN ERROR", 0x0C, data, sizeof(data), len, isNeg));
}

void test_obd_parser_battery_voltage_atrv(void) {
    float volt = 0.0f;
    TEST_ASSERT_EQUAL(ParseStatus::OK, ObdParser::parseBatteryVoltage("14.2V\r>", volt));
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 14.2f, volt);

    TEST_ASSERT_EQUAL(ParseStatus::OK, ObdParser::parseBatteryVoltage("12.6V", volt));
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 12.6f, volt);
}

void test_obd_parser_noise_and_multiline(void) {
    uint8_t bytes[16]{};
    const char* noisy = "0: 41 0C 1A F8\r\n1: 0D 3C\r\n>";
    size_t count = ObdParser::extractHexBytes(noisy, bytes, sizeof(bytes));
    TEST_ASSERT_EQUAL_UINT32(6, count);
    TEST_ASSERT_EQUAL_HEX8(0x41, bytes[0]);
    TEST_ASSERT_EQUAL_HEX8(0x0C, bytes[1]);
    TEST_ASSERT_EQUAL_HEX8(0x0D, bytes[4]);
    TEST_ASSERT_EQUAL_HEX8(0x3C, bytes[5]);
}

void test_obd_parser_can_header_and_echo_stripping(void) {
    uint8_t bytes[16];
    // Case 1: 3-character CAN ID prefix followed by multi-line payload
    const char* canWithId = "7E8 03 41 2F 82\r\n>";
    size_t count = ObdParser::extractHexBytes(canWithId, bytes, sizeof(bytes));
    TEST_ASSERT_EQUAL_UINT32(3, count);
    TEST_ASSERT_EQUAL_HEX8(0x41, bytes[0]);
    TEST_ASSERT_EQUAL_HEX8(0x2F, bytes[1]);
    TEST_ASSERT_EQUAL_HEX8(0x82, bytes[2]);

    // Case 2: Echo of command '012F1' before response
    const char* echoedResp = "012F1\r41 2F 82\r>";
    count = ObdParser::extractHexBytes(echoedResp, bytes, sizeof(bytes));
    TEST_ASSERT_EQUAL_UINT32(3, count);
    TEST_ASSERT_EQUAL_HEX8(0x41, bytes[0]);
    TEST_ASSERT_EQUAL_HEX8(0x2F, bytes[1]);
    TEST_ASSERT_EQUAL_HEX8(0x82, bytes[2]);
}

// ================= Task 3: PidScheduler Tests =================

void test_pid_scheduler_tier1_multi_pid_selection(void) {
    // Universal single-PID Tier 1 polling on CAN protocol 6 without '1' count hint (broadcast-safe)
    PidScheduler scheduler;
    scheduler.setProtocol(6);

    ScheduledQuery query;
    TEST_ASSERT_TRUE(scheduler.getNextQuery(100, query));
    TEST_ASSERT_EQUAL(QueryAction::SEND_COMMAND, query.action);
    TEST_ASSERT_EQUAL_STRING("010C", query.command);
    TEST_ASSERT_EQUAL(ScheduledTier::TIER_1_REALTIME, query.tier);
    scheduler.notifyResponseReceived(query.tier);

    TEST_ASSERT_TRUE(scheduler.getNextQuery(130, query));
    TEST_ASSERT_EQUAL_STRING("010D", query.command);
    scheduler.notifyResponseReceived(query.tier);

    TEST_ASSERT_TRUE(scheduler.getNextQuery(160, query));
    TEST_ASSERT_EQUAL_STRING("0111", query.command);
    scheduler.notifyResponseReceived(query.tier);

    TEST_ASSERT_TRUE(scheduler.getNextQuery(190, query));
    TEST_ASSERT_EQUAL_STRING("010C", query.command);
}

void test_pid_scheduler_tier1_single_pid_fallback(void) {
    // Unconfirmed/Auto protocol (0) or legacy K-Line (3) omits '1' suffix for universal safety
    PidScheduler scheduler;
    scheduler.setProtocol(0);

    ScheduledQuery q1, q2, q3;
    TEST_ASSERT_TRUE(scheduler.getNextQuery(100, q1));
    TEST_ASSERT_EQUAL_STRING("010C", q1.command);
    scheduler.notifyResponseReceived(q1.tier);

    TEST_ASSERT_TRUE(scheduler.getNextQuery(110, q2));
    TEST_ASSERT_EQUAL_STRING("010D", q2.command);
    scheduler.notifyResponseReceived(q2.tier);

    TEST_ASSERT_TRUE(scheduler.getNextQuery(120, q3));
    TEST_ASSERT_EQUAL_STRING("0111", q3.command);
}

void test_pid_scheduler_skips_unsupported_pids(void) {
    PidScheduler sched;
    sched.setProtocol(6);
    sched.setCatalogUnsupported(13, true); // STFT 1 (0x06) unsupported
    sched.setCatalogUnsupported(14, true); // LTFT 1 (0x07) unsupported
    sched.setActivePage(1); // Live Page
    uint8_t slots[6] = { 13, 14, 6, 7, 16, 18 };
    sched.setLiveSlots(slots);

    ScheduledQuery q;
    for (int i = 0; i < 30; ++i) {
        if (sched.getNextQuery(1000 + i * 1000, q)) {
            TEST_ASSERT_NOT_EQUAL(0, std::strcmp(q.command, "0106"));
            TEST_ASSERT_NOT_EQUAL(0, std::strcmp(q.command, "0107"));
        }
    }
}

void test_pid_scheduler_trip_page_focus_speed_and_mil(void) {
    PidScheduler scheduler;
    scheduler.setProtocol(6);
    scheduler.setActivePage(2); // PageId::Trip

    ScheduledQuery q;
    // 1. Initial query at 3000ms triggers MIL / DTC check
    TEST_ASSERT_TRUE(scheduler.getNextQuery(3000, q));
    TEST_ASSERT_EQUAL_STRING("0101", q.command);
    scheduler.notifyResponseReceived(q.tier);

    // 2. Real-time query focuses on speed (single-PID)
    TEST_ASSERT_TRUE(scheduler.getNextQuery(3030, q));
    TEST_ASSERT_EQUAL_STRING("010D", q.command);
    scheduler.notifyResponseReceived(q.tier);

    // 3. Continues single-PID speed priority before periodic RPM
    TEST_ASSERT_TRUE(scheduler.getNextQuery(3060, q));
    TEST_ASSERT_EQUAL_STRING("010D", q.command);
}

// ================= Task 4: Elm327Engine Tests =================

void test_elm327_engine_init_and_probing(void) {
    MockTransport transport;
    VehicleStateManager stateMgr;
    Elm327Engine engine(transport, stateMgr);

    SettingsManager::instance().resetToDefaults();
    transport.connect("00:11:22:33:44:55");
    engine.update(100);
    TEST_ASSERT_EQUAL(ElmState::INIT_ADAPTER, engine.getState());
    TEST_ASSERT_EQUAL_STRING("ATZ\r", transport.getLastWritten().c_str());

    // Step through all 8 universal AT init commands -> POLLING directly
    for (int i = 0; i < 8; ++i) {
        engine.update(100 + i * 20);
        transport.feedResponse("OK\r\n>");
        engine.update(110 + i * 20);
    }
    TEST_ASSERT_EQUAL(ElmState::POLLING, engine.getState());
}

void test_elm327_engine_350ms_soft_break_timeout(void) {
    MockTransport transport;
    VehicleStateManager stateMgr;
    Elm327Engine engine(transport, stateMgr);
    transport.connect("00:11:22:33:44:55");

    // Step past ATZ (initStep_ 0) which has >= 1500ms timeout
    engine.update(100);
    TEST_ASSERT_EQUAL(ElmState::INIT_ADAPTER, engine.getState());
    TEST_ASSERT_EQUAL_STRING("ATZ\r", transport.getLastWritten().c_str());

    // 400ms without prompt must NOT time out ATZ!
    engine.update(500);
    TEST_ASSERT_EQUAL(ElmState::INIT_ADAPTER, engine.getState());

    // Complete ATZ -> advances to ATE0 (initStep_ 1, 350ms timeout)
    transport.feedResponse("ELM327 v1.5\r\n>");
    engine.update(510);
    engine.update(520);
    TEST_ASSERT_EQUAL_STRING("ATE0\r", transport.getLastWritten().c_str());

    // Advance 400ms without prompt '>' on ATE0 -> 350ms soft break triggers
    engine.update(920);
    TEST_ASSERT_EQUAL(ElmState::TIMEOUT_RECOVERY, engine.getState());
}

#include "ui/pages/page.h"
#include "core/util.h"

void test_core_util_math_helpers() {
    // 1. clamp
    TEST_ASSERT_EQUAL(10, core::clamp(5, 10, 20));
    TEST_ASSERT_EQUAL(20, core::clamp(25, 10, 20));
    TEST_ASSERT_EQUAL(15, core::clamp(15, 10, 20));

    // 2. lerp
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 15.0f, core::lerp(10.0f, 20.0f, 0.5f));

    // 3. map_clamped
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 100.0f, core::map_clamped(50.0f, 0.0f, 100.0f, 0.0f, 200.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 200.0f, core::map_clamped(150.0f, 0.0f, 100.0f, 0.0f, 200.0f));

    // 4. ema & ema_alpha
    float alpha = core::ema_alpha(33, 90);
    TEST_ASSERT_TRUE(alpha > 0.0f && alpha < 1.0f);
    float filtered = core::ema(1000.0f, 2000.0f, alpha);
    TEST_ASSERT_TRUE(filtered > 1000.0f && filtered < 2000.0f);

    // 5. elapsed with wrap-around
    TEST_ASSERT_EQUAL_UINT32(50, core::elapsed(150, 100));
    TEST_ASSERT_EQUAL_UINT32(10, core::elapsed(5, 0xFFFFFFFB)); // wrap check
}

void test_core_util_nice_range() {
    int32_t lo = 0, hi = 0;
    // Battery 14.1 - 14.6
    core::nice_range(14.1f, 14.6f, lo, hi);
    TEST_ASSERT_TRUE(lo <= 14);
    TEST_ASSERT_TRUE(hi >= 15);

    // Throttle 10 - 85 with 0..100 bounds
    core::nice_range(10.0f, 85.0f, lo, hi, 0.0f, 100.0f);
    TEST_ASSERT_EQUAL_INT32(0, lo);
    TEST_ASSERT_TRUE(hi >= 90);
    TEST_ASSERT_TRUE(hi <= 100);

    // Identical min and max (flatline)
    core::nice_range(50.0f, 50.0f, lo, hi);
    TEST_ASSERT_TRUE(hi > lo);
}

void test_ui_page_enum_count_and_routing() {
    TEST_ASSERT_EQUAL_UINT8(5, static_cast<uint8_t>(ui::PageId::Count));
    TEST_ASSERT_EQUAL_UINT8(0, static_cast<uint8_t>(ui::PageId::Dashboard));
    TEST_ASSERT_EQUAL_UINT8(1, static_cast<uint8_t>(ui::PageId::Live));
    TEST_ASSERT_EQUAL_UINT8(2, static_cast<uint8_t>(ui::PageId::Trip));
    TEST_ASSERT_EQUAL_UINT8(3, static_cast<uint8_t>(ui::PageId::Settings));
    TEST_ASSERT_EQUAL_UINT8(4, static_cast<uint8_t>(ui::PageId::Diagnostics));

    // SettingsView enum integrity
    TEST_ASSERT_EQUAL_UINT8(0, static_cast<uint8_t>(ui::SettingsView::ROOT_MENU));
    TEST_ASSERT_EQUAL_UINT8(8, static_cast<uint8_t>(ui::SettingsView::TOUCH_CALIBRATE));
    TEST_ASSERT_EQUAL_UINT8(9, static_cast<uint8_t>(ui::SettingsView::OBD_LOG));
}

void test_ui_safety_policy_stationary_and_parking() {
    // 1. Driving (Speed > 0): UI menu interaction strictly blocked
    TEST_ASSERT_FALSE(core::is_safe_to_operate_ui(60, 'D', false, 0));
    TEST_ASSERT_FALSE(core::is_safe_to_operate_ui(1, 'P', true, 10000));
    TEST_ASSERT_FALSE(core::is_safe_to_operate_ui(120, ' ', true, 5000));

    // 2. Stationary with Gear PID: Safe on P, N, or handbrake active
    TEST_ASSERT_TRUE(core::is_safe_to_operate_ui(0, 'P', false, 0));
    TEST_ASSERT_TRUE(core::is_safe_to_operate_ui(0, 'N', false, 0));
    TEST_ASSERT_TRUE(core::is_safe_to_operate_ui(0, 'D', true, 0));
    TEST_ASSERT_FALSE(core::is_safe_to_operate_ui(0, 'D', false, 5000)); // Foot on brake at traffic light
    TEST_ASSERT_FALSE(core::is_safe_to_operate_ui(0, 'R', false, 5000));

    // 3. Stationary without Gear PID (' ' or '\0'): Handbrake OR 3 sec still required
    TEST_ASSERT_FALSE(core::is_safe_to_operate_ui(0, ' ', false, 2900)); // Stopped < 3 sec without handbrake
    TEST_ASSERT_TRUE(core::is_safe_to_operate_ui(0, ' ', false, 3000));  // Stopped >= 3 sec without handbrake (safe!)
    TEST_ASSERT_TRUE(core::is_safe_to_operate_ui(0, ' ', true, 0));      // Handbrake active -> safe immediately
    TEST_ASSERT_TRUE(core::is_safe_to_operate_ui(0, '\0', true, 5000));
}

void test_universal_pid_catalog_total_and_lookup() {
    TEST_ASSERT_EQUAL_UINT32(24, UniversalPidCatalog::kTotalPids);

    int rpmIdx = UniversalPidCatalog::findIndexByPid(0x0C);
    TEST_ASSERT_EQUAL(0, rpmIdx);
    const auto& rpmDef = UniversalPidCatalog::get(rpmIdx);
    TEST_ASSERT_EQUAL_HEX8(0x0C, rpmDef.pidHex);
    TEST_ASSERT_EQUAL_STRING("ENGINE RPM", rpmDef.name);
    TEST_ASSERT_EQUAL_STRING("RPM", rpmDef.unit);

    int coolantIdx = UniversalPidCatalog::findIndexByPid(0x05);
    TEST_ASSERT_EQUAL(6, coolantIdx);
    const auto& cltDef = UniversalPidCatalog::get(coolantIdx);
    TEST_ASSERT_EQUAL_HEX8(0x05, cltDef.pidHex);
    TEST_ASSERT_EQUAL_STRING("COOLANT TEMP", cltDef.name);

    int boostIdx = UniversalPidCatalog::findIndexByPid(0xF0);
    TEST_ASSERT_EQUAL(11, boostIdx);
    const auto& boostDef = UniversalPidCatalog::get(boostIdx);
    TEST_ASSERT_EQUAL_HEX8(0xF0, boostDef.pidHex);
    TEST_ASSERT_EQUAL_STRING("TURBO BOOST", boostDef.name);

    int timingIdx = UniversalPidCatalog::findIndexByPid(0x0E);
    TEST_ASSERT_EQUAL(18, timingIdx);
    const auto& timDef = UniversalPidCatalog::get(timingIdx);
    TEST_ASSERT_EQUAL_HEX8(0x0E, timDef.pidHex);
    TEST_ASSERT_EQUAL_STRING("TIMING ADVANCE", timDef.name);

    // Removed Group 3 / Bank 2 / Cat PIDs must not exist in 24-PID catalog
    TEST_ASSERT_EQUAL(-1, UniversalPidCatalog::findIndexByPid(0x2F));
    TEST_ASSERT_EQUAL(-1, UniversalPidCatalog::findIndexByPid(0x5C));
    TEST_ASSERT_EQUAL(-1, UniversalPidCatalog::findIndexByPid(0x46));
    TEST_ASSERT_EQUAL(-1, UniversalPidCatalog::findIndexByPid(0x5E));
}

void test_universal_pid_catalog_decoding() {
    // RPM: 0x0C -> ((A * 256) + B) / 4. Example: A=0x0F, B=0xA0 -> 1000 RPM
    uint8_t rpmBytes[] = { 0x0F, 0xA0 };
    int rpmIdx = UniversalPidCatalog::findIndexByPid(0x0C);
    float rpm = UniversalPidCatalog::decodeValue(UniversalPidCatalog::get(rpmIdx), rpmBytes, 2);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 1000.0f, rpm);

    // Coolant: 0x05 -> A - 40. Example: A=0x5A (90) -> 50°C
    uint8_t cltBytes[] = { 0x5A };
    int cltIdx = UniversalPidCatalog::findIndexByPid(0x05);
    float clt = UniversalPidCatalog::decodeValue(UniversalPidCatalog::get(cltIdx), cltBytes, 1);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 50.0f, clt);

    // Timing Advance: 0x0E -> (A / 2) - 64. Example: A=0x96 (150) -> 11.0 deg
    uint8_t timBytes[] = { 0x96 };
    int timIdx = UniversalPidCatalog::findIndexByPid(0x0E);
    float tim = UniversalPidCatalog::decodeValue(UniversalPidCatalog::get(timIdx), timBytes, 1);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 11.0f, tim);
}

void test_universal_pid_catalog_clamping(void) {
    int throtIdx = UniversalPidCatalog::findIndexByPid(0x11);
    const auto& throtDef = UniversalPidCatalog::get(throtIdx); // 0x11 Throttle Pos %
    uint8_t rawValid[1] = { 0x82 }; // 130 -> 50.98%
    float val = UniversalPidCatalog::decodeValue(throtDef, rawValid, 1);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 51.0f, val);

    // Ensure 0..100% clamping
    uint8_t rawMax[2] = { 0xFF, 0xFF };
    val = UniversalPidCatalog::decodeValue(throtDef, rawMax, 2);
    TEST_ASSERT_TRUE(val <= 100.0f);
    TEST_ASSERT_TRUE(val >= 0.0f);
}

void test_universal_pid_catalog_bitmask_support() {
    uint64_t unsupportedBits = (1ULL << 13) | (1ULL << 14); // Index 13 (0x06), 14 (0x07) failed 3x
    TEST_ASSERT_TRUE(UniversalPidCatalog::isCatalogIndexSupported(0, unsupportedBits));   // RPM supported
    TEST_ASSERT_FALSE(UniversalPidCatalog::isCatalogIndexSupported(13, unsupportedBits)); // STFT1 unsupported
    TEST_ASSERT_FALSE(UniversalPidCatalog::isCatalogIndexSupported(14, unsupportedBits)); // LTFT1 unsupported
    TEST_ASSERT_FALSE(UniversalPidCatalog::isCatalogIndexSupported(24, 0));               // Out of bounds

    // SAE J1979 0x43 Absolute Load Value: ((256*A + B) * 100) / 255 -> A=0x01, B=0xFF (511) = 200.4%
    int absLoadIdx = UniversalPidCatalog::findIndexByPid(0x43);
    TEST_ASSERT_TRUE(absLoadIdx >= 0);
    const auto& absLoadDef = UniversalPidCatalog::get(static_cast<uint8_t>(absLoadIdx));
    TEST_ASSERT_EQUAL(FormulaType::PERCENT_AB_255, absLoadDef.formula);
    uint8_t absRaw[2] = { 0x01, 0xFF };
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 200.39f, UniversalPidCatalog::decodeValue(absLoadDef, absRaw, 2));

    // SAE J1979 0x1F (RUN TIME) raw unsigned seconds (256*A + B)
    int runTimeIdx = UniversalPidCatalog::findIndexByPid(0x1F);
    TEST_ASSERT_TRUE(runTimeIdx >= 0);
    TEST_ASSERT_EQUAL_STRING("RUN TIME", UniversalPidCatalog::get(runTimeIdx).name);
    TEST_ASSERT_EQUAL(FormulaType::RAW_AB, UniversalPidCatalog::get(runTimeIdx).formula);
    uint8_t secRaw[2] = { 0x01, 0x2C }; // 300 sec
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 300.0f, UniversalPidCatalog::decodeValue(UniversalPidCatalog::get(runTimeIdx), secRaw, 2));
}

void test_settings_manager_universal_live_slots_and_protocol() {
    auto& mgr = SettingsManager::instance();
    mgr.resetToDefaults();
    const auto& s = mgr.get();

    // Default protocol is 0 (Auto ATSP0)
    TEST_ASSERT_EQUAL_UINT8(0, s.selectedProtocol);

    // Default 6 slots: { 0 (RPM), 1 (Speed), 6 (Coolant), 4 (Throttle), 7 (IAT), 18 (Timing) }
    TEST_ASSERT_EQUAL_UINT8(0, s.liveSlots[0]);
    TEST_ASSERT_EQUAL_UINT8(1, s.liveSlots[1]);
    TEST_ASSERT_EQUAL_UINT8(6, s.liveSlots[2]);
    TEST_ASSERT_EQUAL_UINT8(4, s.liveSlots[3]);
    TEST_ASSERT_EQUAL_UINT8(7, s.liveSlots[4]);
    TEST_ASSERT_EQUAL_UINT8(18, s.liveSlots[5]);

    mgr.setSelectedProtocol(6);
    TEST_ASSERT_EQUAL_UINT8(6, mgr.get().selectedProtocol);

    mgr.setLiveSlot(0, 8);
    TEST_ASSERT_EQUAL_UINT8(8, mgr.get().liveSlots[0]);

    mgr.resetToDefaults();
}

void test_trip_computer_initial_and_auto_arming(void) {
    auto& tc = TripComputer::instance();
    tc.resetTrip();
    tc.setSprintMode(SprintMode::SPEED_0_100);

    TEST_ASSERT_EQUAL(AccelTimerState::IDLE, tc.getAccelState());

    // Vehicle stationary at 0 km/h for 200ms -> Still IDLE (needs 500ms)
    tc.update(0, true, 1000);
    tc.update(0, true, 1200);
    TEST_ASSERT_EQUAL(AccelTimerState::IDLE, tc.getAccelState());

    // Vehicle stationary at 0 km/h reaches 500ms -> ARMED!
    tc.update(0, true, 1600);
    TEST_ASSERT_EQUAL(AccelTimerState::ARMED, tc.getAccelState());
}

void test_trip_computer_0_100_sprint_run(void) {
    auto& tc = TripComputer::instance();
    tc.resetTrip();
    tc.resetSprintBest(SprintMode::SPEED_0_100);
    tc.setSprintMode(SprintMode::SPEED_0_100);

    // Arm it: vehicle stopped at 0 km/h for > 500ms
    tc.update(0, true, 2000);
    tc.update(0, true, 2600);
    TEST_ASSERT_EQUAL(AccelTimerState::ARMED, tc.getAccelState());

    // Launch! Step time realistically at 100ms cadence from 0 to 100 km/h in 9.5s
    uint32_t t = 3000;
    tc.update(5, true, t); // Wheel rolls!
    TEST_ASSERT_EQUAL(AccelTimerState::RUNNING, tc.getAccelState());

    while (t < 12500) {
        t += 100;
        uint8_t spd = 5 + static_cast<uint8_t>((t - 3000) * 95 / 9500);
        tc.update(spd, true, t);
    }

    TEST_ASSERT_EQUAL(AccelTimerState::FINISHED, tc.getAccelState());

    const auto& rec = tc.getSprintRecord(SprintMode::SPEED_0_100);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 9.5f, rec.lastTimeSec);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 9.5f, rec.bestTimeSec);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 100.0f, rec.lastTrapSpeedKmh);
}

void test_trip_computer_quarter_mile_run(void) {
    auto& tc = TripComputer::instance();
    tc.resetTrip();
    tc.resetSprintBest(SprintMode::DISTANCE_QUARTER_MILE);
    tc.setSprintMode(SprintMode::DISTANCE_QUARTER_MILE);

    // Arm it with realistic steps
    tc.update(0, true, 14000);
    tc.update(0, true, 14600);
    TEST_ASSERT_EQUAL(AccelTimerState::ARMED, tc.getAccelState());

    // Launch at 15000ms
    uint32_t t = 15000;
    tc.update(20, true, t);
    TEST_ASSERT_EQUAL(AccelTimerState::RUNNING, tc.getAccelState());

    // Simulate steady high speed 150 km/h (41.67 m/s) in 100ms steps
    // 402.3m / 41.67 = ~9.65 seconds
    while (t < 26000 && tc.getAccelState() == AccelTimerState::RUNNING) {
        t += 100;
        tc.update(150, true, t);
    }

    TEST_ASSERT_EQUAL(AccelTimerState::FINISHED, tc.getAccelState());
    const auto& rec = tc.getSprintRecord(SprintMode::DISTANCE_QUARTER_MILE);
    TEST_ASSERT_TRUE(rec.lastTimeSec > 8.0f && rec.lastTimeSec < 12.0f);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 150.0f, rec.lastTrapSpeedKmh);
}

void test_trip_computer_trip_odometer(void) {
    auto& tc = TripComputer::instance();
    tc.resetTrip();

    // 72 km/h = 20 m/s
    tc.update(72, true, 30000);
    tc.update(72, true, 31000); // 1 second elapsed -> 20m = 0.020 km
    tc.update(72, true, 32000); // 1 second elapsed -> 20m = 0.040 km

    const auto& telem = tc.getTelemetry();
    TEST_ASSERT_FLOAT_WITHIN(0.005f, 0.040f, telem.distanceKm);
    TEST_ASSERT_EQUAL_UINT32(2, telem.driveTimeSec);
}

void test_driving_lock_state_machine(void) {
    core::DrivingLockState lockState;
    TEST_ASSERT_FALSE(lockState.isLocked);

    // Initial motion below 5 km/h (slow creep / parking crawl) - should not lock
    TEST_ASSERT_FALSE(lockState.update(3, 1000));
    TEST_ASSERT_FALSE(lockState.update(4, 4000));
    TEST_ASSERT_FALSE(lockState.isLocked);

    // Speed >= 5 km/h starts 2s motion timer
    TEST_ASSERT_FALSE(lockState.update(20, 5000));
    TEST_ASSERT_FALSE(lockState.update(25, 6500)); // 1.5s elapsed -> still false
    TEST_ASSERT_TRUE(lockState.update(30, 7100));  // 2.1s elapsed -> LOCKED!
    TEST_ASSERT_TRUE(lockState.isLocked);

    // Touch attempt while locked triggers subtle lock flash
    TEST_ASSERT_FALSE(lockState.isFlashing(7200));
    lockState.triggerTouchAttempt(7200);
    TEST_ASSERT_TRUE(lockState.isFlashing(7400));
    TEST_ASSERT_FALSE(lockState.isFlashing(7900)); // after 600ms, flash clears

    // Slow down but not stopped
    TEST_ASSERT_TRUE(lockState.update(10, 8000));
    TEST_ASSERT_TRUE(lockState.isLocked);

    // Complete stop (speed == 0) starts 3s stationary unlock timer
    TEST_ASSERT_TRUE(lockState.update(0, 9000));
    TEST_ASSERT_TRUE(lockState.update(0, 11000)); // 2s stationary -> still locked
    TEST_ASSERT_FALSE(lockState.update(0, 12100)); // 3.1s stationary -> UNLOCKED!
    TEST_ASSERT_FALSE(lockState.isLocked);
}

void test_vehicle_parameters_model_and_clamping(void) {
    auto& sm = SettingsManager::instance();
    sm.resetToDefaults();

    // Default RPM scales
    TEST_ASSERT_EQUAL_UINT16(8000, sm.get().maxRpm);
    TEST_ASSERT_EQUAL_UINT16(6200, sm.get().redlineRpm);
    TEST_ASSERT_EQUAL_UINT16(5400, sm.get().warnRpm);

    // Apply Presets
    sm.applyRpmPreset(RpmPreset::DIESEL_6K);
    TEST_ASSERT_EQUAL_UINT16(6000, sm.get().maxRpm);
    TEST_ASSERT_EQUAL_UINT16(4500, sm.get().redlineRpm);
    TEST_ASSERT_EQUAL_UINT16(4000, sm.get().warnRpm);

    sm.applyRpmPreset(RpmPreset::SPORT_9K);
    TEST_ASSERT_EQUAL_UINT16(9000, sm.get().maxRpm);
    TEST_ASSERT_EQUAL_UINT16(8200, sm.get().redlineRpm);
    TEST_ASSERT_EQUAL_UINT16(7500, sm.get().warnRpm);

    // Strict 100-RPM Clamping Test:
    // Warn cannot exceed Redline - 100
    sm.setWarnRpm(8100); // exactly redline - 100
    TEST_ASSERT_EQUAL_UINT16(8100, sm.get().warnRpm);
    sm.stepWarnRpm(true); // Attempt to step above limit -> blocked
    TEST_ASSERT_EQUAL_UINT16(8100, sm.get().warnRpm);

    // Redline cannot drop below Warn + 100
    sm.stepRedlineRpm(false); // Attempt to step down to 8100 -> blocked
    TEST_ASSERT_EQUAL_UINT16(8200, sm.get().redlineRpm);

    // Redline cannot exceed Max RPM
    sm.setRedlineRpm(9000);
    TEST_ASSERT_EQUAL_UINT16(9000, sm.get().redlineRpm);
    sm.stepRedlineRpm(true); // Attempt to step above max -> blocked
    TEST_ASSERT_EQUAL_UINT16(9000, sm.get().redlineRpm);

    // Step Max RPM up and down
    sm.setMaxRpm(9500);
    sm.stepMaxRpm(true);
    TEST_ASSERT_EQUAL_UINT16(9600, sm.get().maxRpm);
    sm.stepMaxRpm(false);
    TEST_ASSERT_EQUAL_UINT16(9500, sm.get().maxRpm);

    // NavRail Mode
    TEST_ASSERT_EQUAL(NavRailMode::AUTO_HIDE, sm.get().navRailMode);
    sm.cycleNavRailMode();
    TEST_ASSERT_EQUAL(NavRailMode::ALWAYS_ON, sm.get().navRailMode);
    sm.cycleNavRailMode();
    TEST_ASSERT_EQUAL(NavRailMode::TAP_TO_CALL, sm.get().navRailMode);
}

void test_dash_slots_settings_model(void) {
    auto& sm = SettingsManager::instance();
    sm.resetToDefaults();

    // Default 4 dash slots: Coolant (6), Batt (16), Boost (11), IAT (7)
    TEST_ASSERT_EQUAL_UINT8(6, sm.get().dashSlots[0]);
    TEST_ASSERT_EQUAL_UINT8(16, sm.get().dashSlots[1]);
    TEST_ASSERT_EQUAL_UINT8(11, sm.get().dashSlots[2]);
    TEST_ASSERT_EQUAL_UINT8(7, sm.get().dashSlots[3]);

    // Update dash slot with 0xFF (empty slot allowed)
    sm.setDashSlot(2, 0xFF);
    TEST_ASSERT_EQUAL_UINT8(0xFF, sm.get().dashSlots[2]);

    // Set custom PID
    sm.setDashSlot(0, 2); // Calc Engine Load
    TEST_ASSERT_EQUAL_UINT8(2, sm.get().dashSlots[0]);
}

void test_universal_pid_super_groups() {
    size_t c0 = UniversalPidCatalog::getSuperGroupCount(SuperPidGroup::ENGINE_THERMAL);
    size_t c1 = UniversalPidCatalog::getSuperGroupCount(SuperPidGroup::FUEL_AIR_TURBO);
    size_t c2 = UniversalPidCatalog::getSuperGroupCount(SuperPidGroup::ELECTRIC_DIAG);

    TEST_ASSERT_EQUAL_UINT32(8, c0);
    TEST_ASSERT_EQUAL_UINT32(8, c1);
    TEST_ASSERT_EQUAL_UINT32(8, c2);
    TEST_ASSERT_EQUAL_UINT32(24, c0 + c1 + c2);

    bool seen[UniversalPidCatalog::kTotalPids]{false};
    for (size_t i = 0; i < c0; ++i) {
        uint8_t idx = UniversalPidCatalog::getSuperGroupPidIndex(SuperPidGroup::ENGINE_THERMAL, i);
        TEST_ASSERT_TRUE(idx < 24);
        seen[idx] = true;
    }
    for (size_t i = 0; i < c1; ++i) {
        uint8_t idx = UniversalPidCatalog::getSuperGroupPidIndex(SuperPidGroup::FUEL_AIR_TURBO, i);
        TEST_ASSERT_TRUE(idx < 24);
        seen[idx] = true;
    }
    for (size_t i = 0; i < c2; ++i) {
        uint8_t idx = UniversalPidCatalog::getSuperGroupPidIndex(SuperPidGroup::ELECTRIC_DIAG, i);
        TEST_ASSERT_TRUE(idx < 24);
        seen[idx] = true;
    }
    for (size_t i = 0; i < UniversalPidCatalog::kTotalPids; ++i) {
        TEST_ASSERT_TRUE(seen[i]);
    }

    TEST_ASSERT_NOT_NULL(UniversalPidCatalog::getSuperGroupName(SuperPidGroup::ENGINE_THERMAL));
    TEST_ASSERT_NOT_NULL(UniversalPidCatalog::getSuperGroupName(SuperPidGroup::FUEL_AIR_TURBO));
    TEST_ASSERT_NOT_NULL(UniversalPidCatalog::getSuperGroupName(SuperPidGroup::ELECTRIC_DIAG));
}

void test_settings_carousel_and_cycles() {
    auto& sm = SettingsManager::instance();

    // 1. Theme Cycle (6 themes: 0..5)
    sm.setThemeIndex(0);
    sm.cycleTheme();
    TEST_ASSERT_EQUAL_UINT8(1, sm.get().themeIndex);
    sm.cycleTheme();
    TEST_ASSERT_EQUAL_UINT8(2, sm.get().themeIndex);
    sm.cycleTheme();
    TEST_ASSERT_EQUAL_UINT8(3, sm.get().themeIndex);
    sm.cycleTheme();
    TEST_ASSERT_EQUAL_UINT8(4, sm.get().themeIndex);
    sm.cycleTheme();
    TEST_ASSERT_EQUAL_UINT8(5, sm.get().themeIndex);
    sm.cycleTheme();
    TEST_ASSERT_EQUAL_UINT8(0, sm.get().themeIndex);

    // 2. Combined Brightness Cycle
    sm.setBrightnessMode(BrightnessMode::AUTO);
    sm.cycleCombinedBrightness();
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(BrightnessMode::MANUAL), static_cast<uint8_t>(sm.get().brightnessMode));
    TEST_ASSERT_EQUAL_UINT8(40, sm.get().manualBrightness);
    sm.cycleCombinedBrightness();
    TEST_ASSERT_EQUAL_UINT8(60, sm.get().manualBrightness);
    sm.cycleCombinedBrightness();
    TEST_ASSERT_EQUAL_UINT8(80, sm.get().manualBrightness);
    sm.cycleCombinedBrightness();
    TEST_ASSERT_EQUAL_UINT8(100, sm.get().manualBrightness);
    sm.cycleCombinedBrightness();
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(BrightnessMode::AUTO), static_cast<uint8_t>(sm.get().brightnessMode));

    // 3. Buzzer Cycle
    sm.setBuzzerMode(BuzzerMode::ALL_ON);
    sm.cycleBuzzerMode();
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(BuzzerMode::ALARM_ONLY), static_cast<uint8_t>(sm.get().buzzerMode));
    sm.cycleBuzzerMode();
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(BuzzerMode::MUTE), static_cast<uint8_t>(sm.get().buzzerMode));
    sm.cycleBuzzerMode();
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(BuzzerMode::ALL_ON), static_cast<uint8_t>(sm.get().buzzerMode));

    // 3.1 Buzzer Volume Cycle
    sm.setBuzzerVolume(100);
    sm.cycleBuzzerVolume();
    TEST_ASSERT_EQUAL_UINT8(25, sm.get().buzzerVolume);
    sm.cycleBuzzerVolume();
    TEST_ASSERT_EQUAL_UINT8(50, sm.get().buzzerVolume);
    sm.cycleBuzzerVolume();
    TEST_ASSERT_EQUAL_UINT8(75, sm.get().buzzerVolume);
    sm.cycleBuzzerVolume();
    TEST_ASSERT_EQUAL_UINT8(100, sm.get().buzzerVolume);

    // 4. NavRail Cycle
    sm.setNavRailMode(NavRailMode::AUTO_HIDE);
    sm.cycleNavRailMode();
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NavRailMode::ALWAYS_ON), static_cast<uint8_t>(sm.get().navRailMode));
    sm.cycleNavRailMode();
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NavRailMode::TAP_TO_CALL), static_cast<uint8_t>(sm.get().navRailMode));
    sm.cycleNavRailMode();
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NavRailMode::AUTO_HIDE), static_cast<uint8_t>(sm.get().navRailMode));

    // 5. OperationMode Cycle
    sm.setOperationMode(OperationMode::LAUNCH);
    sm.cycleOperationMode();
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(OperationMode::DEMO), static_cast<uint8_t>(sm.get().operationMode));
    sm.cycleOperationMode();
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(OperationMode::LAUNCH), static_cast<uint8_t>(sm.get().operationMode));
}

void test_universal_pid_alert_levels(void) {
    // 1. Coolant Temp (0x05)
    TEST_ASSERT_EQUAL_UINT8(0, UniversalPidCatalog::getAlertLevel(0x05, 90.0f));
    TEST_ASSERT_EQUAL_UINT8(1, UniversalPidCatalog::getAlertLevel(0x05, 102.0f));
    TEST_ASSERT_EQUAL_UINT8(2, UniversalPidCatalog::getAlertLevel(0x05, 110.0f));

    // 2. IAT (0x0F)
    TEST_ASSERT_EQUAL_UINT8(0, UniversalPidCatalog::getAlertLevel(0x0F, 35.0f));
    TEST_ASSERT_EQUAL_UINT8(1, UniversalPidCatalog::getAlertLevel(0x0F, 58.0f));
    TEST_ASSERT_EQUAL_UINT8(2, UniversalPidCatalog::getAlertLevel(0x0F, 68.0f));

    // 3. Battery Voltage (0xFF)
    TEST_ASSERT_EQUAL_UINT8(0, UniversalPidCatalog::getAlertLevel(0xFF, 14.1f));
    TEST_ASSERT_EQUAL_UINT8(1, UniversalPidCatalog::getAlertLevel(0xFF, 11.9f));
    TEST_ASSERT_EQUAL_UINT8(2, UniversalPidCatalog::getAlertLevel(0xFF, 11.2f));
    TEST_ASSERT_EQUAL_UINT8(1, UniversalPidCatalog::getAlertLevel(0xFF, 15.0f));
    TEST_ASSERT_EQUAL_UINT8(2, UniversalPidCatalog::getAlertLevel(0xFF, 15.5f));

    // 4. Fuel Trims (0x06)
    TEST_ASSERT_EQUAL_UINT8(0, UniversalPidCatalog::getAlertLevel(0x06, 2.5f));
    TEST_ASSERT_EQUAL_UINT8(1, UniversalPidCatalog::getAlertLevel(0x06, 14.0f));
    TEST_ASSERT_EQUAL_UINT8(1, UniversalPidCatalog::getAlertLevel(0x06, -15.0f));
    TEST_ASSERT_EQUAL_UINT8(2, UniversalPidCatalog::getAlertLevel(0x06, 22.0f));
    TEST_ASSERT_EQUAL_UINT8(2, UniversalPidCatalog::getAlertLevel(0x06, -24.0f));

    // 5. Turbo Boost PSI (0xF0) & MAP (0x0B)
    TEST_ASSERT_EQUAL_UINT8(0, UniversalPidCatalog::getAlertLevel(0xF0, 12.0f));
    TEST_ASSERT_EQUAL_UINT8(1, UniversalPidCatalog::getAlertLevel(0xF0, 19.0f));
    TEST_ASSERT_EQUAL_UINT8(2, UniversalPidCatalog::getAlertLevel(0xF0, 23.0f));
    TEST_ASSERT_EQUAL_UINT8(0, UniversalPidCatalog::getAlertLevel(0x0B, 105.0f));
    TEST_ASSERT_EQUAL_UINT8(1, UniversalPidCatalog::getAlertLevel(0x0B, 215.0f));
    TEST_ASSERT_EQUAL_UINT8(2, UniversalPidCatalog::getAlertLevel(0x0B, 245.0f));
}

void test_dtc_pagination_calculations(void) {
    using namespace ui;

    // Constants verification
    TEST_ASSERT_EQUAL_UINT8(10, DtcPagination::kMaxDtcCodes);
    TEST_ASSERT_EQUAL_UINT8(3, DtcPagination::kDtcPerPage);

    // Total pages calculation
    TEST_ASSERT_EQUAL_UINT8(1, DtcPagination::calculateTotalPages(0));
    TEST_ASSERT_EQUAL_UINT8(1, DtcPagination::calculateTotalPages(1));
    TEST_ASSERT_EQUAL_UINT8(1, DtcPagination::calculateTotalPages(3));
    TEST_ASSERT_EQUAL_UINT8(2, DtcPagination::calculateTotalPages(4));
    TEST_ASSERT_EQUAL_UINT8(2, DtcPagination::calculateTotalPages(6));
    TEST_ASSERT_EQUAL_UINT8(3, DtcPagination::calculateTotalPages(7));
    TEST_ASSERT_EQUAL_UINT8(4, DtcPagination::calculateTotalPages(10));
    // Capping at kMaxDtcCodes (10 codes -> 4 pages max)
    TEST_ASSERT_EQUAL_UINT8(4, DtcPagination::calculateTotalPages(15));
    TEST_ASSERT_EQUAL_UINT8(4, DtcPagination::calculateTotalPages(255));

    // Start index for each page
    TEST_ASSERT_EQUAL_UINT8(0, DtcPagination::getStartIndex(0));
    TEST_ASSERT_EQUAL_UINT8(3, DtcPagination::getStartIndex(1));
    TEST_ASSERT_EQUAL_UINT8(6, DtcPagination::getStartIndex(2));
    TEST_ASSERT_EQUAL_UINT8(9, DtcPagination::getStartIndex(3));

    // Items on page for 7 DTCs
    TEST_ASSERT_EQUAL_UINT8(3, DtcPagination::getItemsOnPage(0, 7));
    TEST_ASSERT_EQUAL_UINT8(3, DtcPagination::getItemsOnPage(1, 7));
    TEST_ASSERT_EQUAL_UINT8(1, DtcPagination::getItemsOnPage(2, 7));
    TEST_ASSERT_EQUAL_UINT8(0, DtcPagination::getItemsOnPage(3, 7));

    // Items on page for 10 DTCs
    TEST_ASSERT_EQUAL_UINT8(3, DtcPagination::getItemsOnPage(0, 10));
    TEST_ASSERT_EQUAL_UINT8(3, DtcPagination::getItemsOnPage(1, 10));
    TEST_ASSERT_EQUAL_UINT8(3, DtcPagination::getItemsOnPage(2, 10));
    TEST_ASSERT_EQUAL_UINT8(1, DtcPagination::getItemsOnPage(3, 10));
    TEST_ASSERT_EQUAL_UINT8(0, DtcPagination::getItemsOnPage(4, 10));

    // Items on page when exceeding max (15 DTCs clamped to 10)
    TEST_ASSERT_EQUAL_UINT8(1, DtcPagination::getItemsOnPage(3, 15));
    TEST_ASSERT_EQUAL_UINT8(0, DtcPagination::getItemsOnPage(4, 15));
}

void test_pid_scheduler_dash_slots(void) {
    PidScheduler scheduler;

    // Default 4 dash slots in scheduler: { 6 (Coolant), 16 (Batt), 11 (Boost), 7 (IAT) }
    const uint8_t* slots = scheduler.getDashSlots();
    TEST_ASSERT_NOT_NULL(slots);
    TEST_ASSERT_EQUAL_UINT8(6, slots[0]);
    TEST_ASSERT_EQUAL_UINT8(16, slots[1]);
    TEST_ASSERT_EQUAL_UINT8(11, slots[2]);
    TEST_ASSERT_EQUAL_UINT8(7, slots[3]);

    // Custom dash slots
    const uint8_t customSlots[4] = { 2, 4, 6, 8 };
    scheduler.setDashSlots(customSlots);
    TEST_ASSERT_EQUAL_UINT8(2, scheduler.getDashSlots()[0]);
    TEST_ASSERT_EQUAL_UINT8(4, scheduler.getDashSlots()[1]);
    TEST_ASSERT_EQUAL_UINT8(6, scheduler.getDashSlots()[2]);
    TEST_ASSERT_EQUAL_UINT8(8, scheduler.getDashSlots()[3]);
}

void test_gear_ratio_default_rx(void) {
    // Gear calculation from RPM and speed has been removed in favor of direct TCM data.
    // Default / unpopulated gear is ' '
    VehicleState s;
    TEST_ASSERT_EQUAL_CHAR(' ', s.gear);
}

void test_elm327_engine_dual_gear_fallback_and_throttle_sentinel(void) {
    SettingsManager::instance().resetToDefaults(); // proto = 0
    SettingsManager::instance().setSelectedProtocol(6);

    MockTransport transport;
    VehicleStateManager stateMgr;
    Elm327Engine engine(transport, stateMgr);
    transport.connect("00:11:22:33:44:55");

    uint32_t t = 100;
    for (int i = 0; i < 8; ++i) {
        engine.update(t);
        t += 10;
        transport.feedResponse("OK\r\n>");
        engine.update(t);
        t += 10;
    }
    TEST_ASSERT_EQUAL(ElmState::POLLING, engine.getState());

    // Tier 1 single-PID cycle: RPM -> Speed -> Throttle
    engine.update(t); // 010C
    TEST_ASSERT_EQUAL_STRING("010C\r", transport.getLastWritten().c_str());
    transport.feedResponse("41 0C 0C 80\r\n>"); // 800 RPM
    t += 10; engine.update(t);

    t += 30; engine.update(t); // 010D1 (confirmed CAN uses '1' count hint after first response)
    TEST_ASSERT_EQUAL_STRING("010D1\r", transport.getLastWritten().c_str());
    transport.feedResponse("41 0D 00\r\n>"); // 0 km/h
    t += 10; engine.update(t);

    t += 30; engine.update(t); // 01111
    TEST_ASSERT_EQUAL_STRING("01111\r", transport.getLastWritten().c_str());
    transport.feedResponse("41 11 27\r\n>"); // ~15%
    t += 10; engine.update(t);

    VehicleState s = stateMgr.getSnapshot();
    TEST_ASSERT_EQUAL_CHAR(' ', s.gear);
    TEST_ASSERT_EQUAL_UINT16(800, s.rpm);
    TEST_ASSERT_EQUAL_UINT8(15, s.throttlePercent);

    // Move to 20 km/h and verify RPM/Speed update without wiping throttle
    t += 30; engine.update(t); // 010C1
    transport.feedResponse("41 0C 13 60\r\n>"); // 1240 RPM
    t += 10; engine.update(t);

    t += 30; engine.update(t); // 010D1
    transport.feedResponse("41 0D 14\r\n>"); // 20 km/h
    t += 10; engine.update(t);

    s = stateMgr.getSnapshot();
    TEST_ASSERT_EQUAL_CHAR(' ', s.gear);
    TEST_ASSERT_EQUAL_UINT16(1240, s.rpm);
    TEST_ASSERT_EQUAL_UINT8(20, s.speedKmh);
    TEST_ASSERT_EQUAL_UINT8(15, s.throttlePercent);

    SettingsManager::instance().resetToDefaults();
}

void test_pid_scheduler_tier2_rotates_dash_slots(void) {
    PidScheduler scheduler;
    scheduler.setProtocol(6);

    // Configured dashSlots: { 6 (Coolant), 16 (Batt), 11 (Boost), 7 (IAT) }
    const uint8_t slots[4] = { 6, 16, 11, 7 };
    scheduler.setDashSlots(slots);

    ScheduledQuery q;
    // t=1000ms: Tier 2 cycle 1 - Slot 0: Coolant 0x05 -> "0105"
    TEST_ASSERT_TRUE(scheduler.getNextQuery(1000, q));
    TEST_ASSERT_EQUAL(QueryAction::SEND_COMMAND, q.action);
    TEST_ASSERT_EQUAL_STRING("0105", q.command);
    scheduler.notifyResponseReceived(q.tier);

    // t=2000ms: Tier 2 cycle 2 - Slot 1 is ATRV (skipped), Slot 2 is Boost 0xF0 -> "0133" (BARO) first
    TEST_ASSERT_TRUE(scheduler.getNextQuery(2000, q));
    TEST_ASSERT_EQUAL(QueryAction::SEND_COMMAND, q.action);
    TEST_ASSERT_EQUAL_STRING("0133", q.command);
    scheduler.notifyResponseReceived(q.tier);

    // t=3000ms: Tier 2 cycle 3 - Slot 3: IAT 0x0F -> "010F"
    TEST_ASSERT_TRUE(scheduler.getNextQuery(3000, q));
    TEST_ASSERT_EQUAL(QueryAction::SEND_COMMAND, q.action);
    TEST_ASSERT_EQUAL_STRING("010F", q.command);
    scheduler.notifyResponseReceived(q.tier);
}

void test_settings_manager_reset_and_load_dash_slots(void) {
    SettingsManager& sm = SettingsManager::instance();
    sm.resetToDefaults();
    TEST_ASSERT_EQUAL_UINT8(6, sm.get().dashSlots[0]);  // Coolant
    TEST_ASSERT_EQUAL_UINT8(16, sm.get().dashSlots[1]); // Battery Voltage
    TEST_ASSERT_EQUAL_UINT8(11, sm.get().dashSlots[2]); // Boost
    TEST_ASSERT_EQUAL_UINT8(7, sm.get().dashSlots[3]);  // Intake Air Temp

    // Reset dash slots method
    sm.setDashSlot(1, 20); // Modify slot 1
    TEST_ASSERT_EQUAL_UINT8(20, sm.get().dashSlots[1]);
    sm.resetDashSlotsToDefaults();
    TEST_ASSERT_EQUAL_UINT8(16, sm.get().dashSlots[1]); // Must restore to 16!
}

void test_obd_parser_dtc_modes_and_formats(void) {
    // 1. Single DTC with ISO 15765-4 count byte: 43 01 01 00 00 00 00 -> P0100
    uint16_t codes[8]{0};
    size_t count = 0;
    ParseStatus st = ObdParser::parseDtcCodes("43 01 01 00 00 00 00", codes, 8, count);
    TEST_ASSERT_EQUAL(ParseStatus::OK, st);
    TEST_ASSERT_EQUAL_UINT32(1, count);
    TEST_ASSERT_EQUAL_HEX16(0x0100, codes[0]);

    // 2. High-level DtcCodeInfo parsing
    DtcCodeInfo info[8]{};
    size_t infoCount = 0;
    st = ObdParser::parseDtcResponse("43 01 01 00 00 00 00", false, info, 8, infoCount);
    TEST_ASSERT_EQUAL(ParseStatus::OK, st);
    TEST_ASSERT_EQUAL_UINT32(1, infoCount);
    TEST_ASSERT_EQUAL_STRING("P0100", info[0].code);
    TEST_ASSERT_FALSE(info[0].isPending);
    TEST_ASSERT_EQUAL(DtcSeverity::WARNING, info[0].severity);
    TEST_ASSERT_NOT_NULL(info[0].desc);

    // 3. Mode 07 Pending DTC: 47 01 04 20 00 00 00 -> P0420, isPending=true
    st = ObdParser::parseDtcResponse("47 01 04 20 00 00 00", true, info, 8, infoCount);
    TEST_ASSERT_EQUAL(ParseStatus::OK, st);
    TEST_ASSERT_EQUAL_UINT32(1, infoCount);
    TEST_ASSERT_EQUAL_STRING("P0420", info[0].code);
    TEST_ASSERT_TRUE(info[0].isPending);
    TEST_ASSERT_EQUAL(DtcSeverity::WARNING, info[0].severity);

    // 4. Zero DTCs: 43 00 00 00 00 00 00
    st = ObdParser::parseDtcResponse("43 00 00 00 00 00 00", false, info, 8, infoCount);
    TEST_ASSERT_EQUAL(ParseStatus::OK, st);
    TEST_ASSERT_EQUAL_UINT32(0, infoCount);

    // 5. NO DATA response
    st = ObdParser::parseDtcResponse("NO DATA", false, info, 8, infoCount);
    TEST_ASSERT_EQUAL(ParseStatus::NO_DATA, st);
    TEST_ASSERT_EQUAL_UINT32(0, infoCount);

    // 6. Multiple DTCs: 43 02 01 00 03 00 00 -> P0100, P0300
    st = ObdParser::parseDtcResponse("43 02 01 00 03 00 00", false, info, 8, infoCount);
    TEST_ASSERT_EQUAL(ParseStatus::OK, st);
    TEST_ASSERT_EQUAL_UINT32(2, infoCount);
    TEST_ASSERT_EQUAL_STRING("P0100", info[0].code);
    TEST_ASSERT_EQUAL_STRING("P0300", info[1].code);
    TEST_ASSERT_EQUAL(DtcSeverity::CRITICAL, info[1].severity);

    // 7. Network U-Code DTC: 43 01 C1 00 00 00 00 -> U0100
    st = ObdParser::parseDtcResponse("43 01 C1 00 00 00 00", false, info, 8, infoCount);
    TEST_ASSERT_EQUAL(ParseStatus::OK, st);
    TEST_ASSERT_EQUAL_UINT32(1, infoCount);
    TEST_ASSERT_EQUAL_STRING("U0100", info[0].code);

    // 8. Raw pairs format (SAE J1979 K-Line with padding): 43 02 01 03 00 00 00 -> P0201, P0300
    st = ObdParser::parseDtcResponse("43 02 01 03 00 00 00", false, info, 8, infoCount);
    TEST_ASSERT_EQUAL(ParseStatus::OK, st);
    TEST_ASSERT_EQUAL_UINT32(2, infoCount);
    TEST_ASSERT_EQUAL_STRING("P0201", info[0].code);
    TEST_ASSERT_EQUAL_STRING("P0300", info[1].code);

    // 9. Raw single DTC without count prefix: 43 01 00 00 00 00 00 -> P0100
    st = ObdParser::parseDtcResponse("43 01 00 00 00 00 00", false, info, 8, infoCount);
    TEST_ASSERT_EQUAL(ParseStatus::OK, st);
    TEST_ASSERT_EQUAL_UINT32(1, infoCount);
    TEST_ASSERT_EQUAL_STRING("P0100", info[0].code);

    // 10. Raw single DTC: 43 03 01 00 00 00 00 -> P0301 (Cylinder 1 Misfire)
    st = ObdParser::parseDtcResponse("43 03 01 00 00 00 00", false, info, 8, infoCount);
    TEST_ASSERT_EQUAL(ParseStatus::OK, st);
    TEST_ASSERT_EQUAL_UINT32(1, infoCount);
    TEST_ASSERT_EQUAL_STRING("P0301", info[0].code);

    // 11. Short K-Line response without padding: 43 01 71 -> P0171
    st = ObdParser::parseDtcResponse("43 01 71", false, info, 8, infoCount);
    TEST_ASSERT_EQUAL(ParseStatus::OK, st);
    TEST_ASSERT_EQUAL_UINT32(1, infoCount);
    TEST_ASSERT_EQUAL_STRING("P0171", info[0].code);
}

void test_dtc_database_lookup_and_formatting(void) {
    char buf[8]{0};
    DtcDatabase::formatCode(0x0100, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("P0100", buf);

    DtcDatabase::formatCode(0x0301, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("P0301", buf);

    DtcDatabase::formatCode(0xC100, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("U0100", buf);

    uint16_t parsed = 0;
    TEST_ASSERT_TRUE(DtcDatabase::parseCode("P0100", parsed));
    TEST_ASSERT_EQUAL_HEX16(0x0100, parsed);

    TEST_ASSERT_TRUE(DtcDatabase::parseCode("U0100", parsed));
    TEST_ASSERT_EQUAL_HEX16(0xC100, parsed);

    TEST_ASSERT_EQUAL(DtcSeverity::CRITICAL, DtcDatabase::getSeverity(0x0300));
    TEST_ASSERT_NOT_NULL(DtcDatabase::describe(0x0100));
}

void test_elm327_engine_dtc_scan_lifecycle(void) {
    SettingsManager::instance().resetToDefaults();
    MockTransport transport;
    VehicleStateManager stateMgr;
    Elm327Engine engine(transport, stateMgr);

    TEST_ASSERT_FALSE(engine.isDtcScanning());
    TEST_ASSERT_FALSE(engine.isDtcScanComplete());
    TEST_ASSERT_EQUAL_UINT8(0, engine.getScannedDtcCount());

    transport.connect("11:22:33:44:55:66");

    // Advance through 8 AT init commands -> POLLING
    uint32_t t = 100;
    for (int i = 0; i < 8; ++i) {
        engine.update(t);
        t += 10;
        transport.feedResponse("OK\r\n>");
        engine.update(t);
        t += 10;
    }
    TEST_ASSERT_EQUAL(ElmState::POLLING, engine.getState());

    // First query on ATSP0 is 010C; respond so engine queries ATDPN
    engine.update(t);
    TEST_ASSERT_EQUAL_STRING("010C\r", transport.getLastWritten().c_str());
    t += 10;
    transport.feedResponse("41 0C 1A F8\r\n>");
    engine.update(t);

    // Next update queries ATDPN -> resolve CAN 11-bit (6)
    t += 5;
    engine.update(t);
    TEST_ASSERT_EQUAL_STRING("ATDPN\r", transport.getLastWritten().c_str());
    t += 10;
    transport.feedResponse("A6\r\n>");
    engine.update(t);
    TEST_ASSERT_EQUAL_UINT8(6, engine.getResolvedProtocol());

    // Advance time past TIER_1_INTERVAL_MS (30ms) so scheduler dispatches next query into flight
    t += 35;
    engine.update(t);
    std::string inFlightCmd = transport.getLastWritten();
    TEST_ASSERT_TRUE(inFlightCmd.length() > 0);
    TEST_ASSERT_NOT_EQUAL(0, std::strcmp("03\r", inFlightCmd.c_str()));

    // Trigger DTC scan while query is in flight
    engine.startDtcScan();
    TEST_ASSERT_TRUE(engine.isDtcScanning());
    TEST_ASSERT_FALSE(engine.isDtcScanComplete());

    // Next update while query still in-flight must NOT clobber in-flight command with 03\r!
    t += 5;
    engine.update(t);
    TEST_ASSERT_EQUAL_STRING(inFlightCmd.c_str(), transport.getLastWritten().c_str());

    // Feed in-flight query response
    t += 5;
    transport.feedResponse("41 0D 3C\r\n>");
    engine.update(t);

    // Next update dispatches Mode 03 directly (adapter remains on default broadcast header)
    t += 5;
    engine.update(t);
    TEST_ASSERT_EQUAL_STRING("03\r", transport.getLastWritten().c_str());

    // Feed Mode 03 response
    t += 10;
    transport.feedResponse("43 01 01 00 00 00 00\r\n>");
    engine.update(t);

    // Next update should dispatch Mode 07
    t += 10;
    engine.update(t);
    TEST_ASSERT_EQUAL_STRING("07\r", transport.getLastWritten().c_str());

    // Feed Mode 07 response (pending P0420) -> complete immediately (7DF already broadcast default!)
    t += 10;
    transport.feedResponse("47 01 04 20 00 00 00\r\n>");
    engine.update(t);

    TEST_ASSERT_TRUE(engine.isDtcScanComplete());
    TEST_ASSERT_FALSE(engine.isDtcScanning());
    TEST_ASSERT_EQUAL_UINT8(2, engine.getScannedDtcCount());

    const DtcCodeInfo* dtcs = engine.getScannedDtcs();
    TEST_ASSERT_NOT_NULL(dtcs);
    TEST_ASSERT_EQUAL_STRING("P0100", dtcs[0].code);
    TEST_ASSERT_FALSE(dtcs[0].isPending);
    TEST_ASSERT_EQUAL_STRING("P0420", dtcs[1].code);
    TEST_ASSERT_TRUE(dtcs[1].isPending);
}

void test_settings_manager_concurrency_and_snapshot(void) {
    SettingsManager& sm = SettingsManager::instance();
    sm.resetToDefaults();

    AppSettings snap1 = sm.get();
    TEST_ASSERT_EQUAL_UINT8(70, snap1.manualBrightness);

    sm.setManualBrightness(50);
    AppSettings snap2 = sm.get();
    TEST_ASSERT_EQUAL_UINT8(50, snap2.manualBrightness);
    TEST_ASSERT_EQUAL_UINT8(70, snap1.manualBrightness);
}

void test_atomic_ring_buffer_concurrency(void) {
    constexpr size_t kBufSize = 16;
    uint8_t ringBuf[kBufSize]{};
    std::atomic<size_t> head{0};
    std::atomic<size_t> tail{0};

    auto available = [&]() -> size_t {
        size_t h = head.load(std::memory_order_acquire);
        size_t t = tail.load(std::memory_order_relaxed);
        if (h >= t) return h - t;
        return kBufSize - (t - h);
    };

    auto write = [&](const uint8_t* data, size_t len) -> size_t {
        size_t h = head.load(std::memory_order_relaxed);
        size_t t = tail.load(std::memory_order_acquire);
        size_t written = 0;
        for (size_t i = 0; i < len; ++i) {
            size_t nextH = (h + 1) % kBufSize;
            if (nextH != t) {
                ringBuf[h] = data[i];
                h = nextH;
                written++;
            } else {
                break;
            }
        }
        head.store(h, std::memory_order_release);
        return written;
    };

    auto read = [&](uint8_t* out, size_t maxLen) -> size_t {
        size_t t = tail.load(std::memory_order_relaxed);
        size_t h = head.load(std::memory_order_acquire);
        size_t count = 0;
        while (count < maxLen && t != h) {
            out[count++] = ringBuf[t];
            t = (t + 1) % kBufSize;
        }
        tail.store(t, std::memory_order_release);
        return count;
    };

    TEST_ASSERT_EQUAL_UINT32(0, available());

    // Write 5 bytes
    const uint8_t wData1[] = { 1, 2, 3, 4, 5 };
    TEST_ASSERT_EQUAL_UINT32(5, write(wData1, 5));
    TEST_ASSERT_EQUAL_UINT32(5, available());

    // Read 3 bytes
    uint8_t rBuf[16]{};
    TEST_ASSERT_EQUAL_UINT32(3, read(rBuf, 3));
    TEST_ASSERT_EQUAL_UINT8(1, rBuf[0]);
    TEST_ASSERT_EQUAL_UINT8(2, rBuf[1]);
    TEST_ASSERT_EQUAL_UINT8(3, rBuf[2]);
    TEST_ASSERT_EQUAL_UINT32(2, available());

    // Write enough to wrap around the circular buffer (16 bytes size)
    const uint8_t wData2[] = { 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
    TEST_ASSERT_EQUAL_UINT32(10, write(wData2, 10));
    TEST_ASSERT_EQUAL_UINT32(12, available());

    // Read remaining 12 bytes
    TEST_ASSERT_EQUAL_UINT32(12, read(rBuf, sizeof(rBuf)));
    TEST_ASSERT_EQUAL_UINT8(4, rBuf[0]);
    TEST_ASSERT_EQUAL_UINT8(5, rBuf[1]);
    TEST_ASSERT_EQUAL_UINT8(6, rBuf[2]);
    TEST_ASSERT_EQUAL_UINT8(15, rBuf[11]);
    TEST_ASSERT_EQUAL_UINT32(0, available());
}

void test_command_guard_removed_tcm_and_fc_blocked(void) {
    // Removed manufacturer Mode 21/22 & ATFC commands are no longer whitelisted
    TEST_ASSERT_EQUAL(GuardResult::BLOCKED_NOT_WHITELISTED, CommandGuard::validate("2101"));
    TEST_ASSERT_EQUAL(GuardResult::BLOCKED_NOT_WHITELISTED, CommandGuard::validate("2182"));
    TEST_ASSERT_EQUAL(GuardResult::BLOCKED_NOT_WHITELISTED, CommandGuard::validate("222201"));
    TEST_ASSERT_EQUAL(GuardResult::BLOCKED_NOT_WHITELISTED, CommandGuard::validate("221200"));
    TEST_ASSERT_FALSE(CommandGuard::isAllowed("ATFCSH7E1"));
    TEST_ASSERT_FALSE(CommandGuard::isAllowed("ATFCSD300000"));
    TEST_ASSERT_FALSE(CommandGuard::isAllowed("ATFCSM1"));
    TEST_ASSERT_FALSE(CommandGuard::isAllowed("ATFCSM0"));

    // Broadcast CAN Headers remain allowed for Mode 03/07 DTC scans
    TEST_ASSERT_EQUAL(GuardResult::ALLOWED, CommandGuard::validate("ATSH7DF"));
    TEST_ASSERT_EQUAL(GuardResult::ALLOWED, CommandGuard::validate("ATSH18DB33F1"));

    // Ensure dangerous / blacklisted commands are strictly rejected under R8
    TEST_ASSERT_EQUAL(GuardResult::BLOCKED_BLACKLIST, CommandGuard::validate("04"));
    TEST_ASSERT_EQUAL(GuardResult::BLOCKED_BLACKLIST, CommandGuard::validate("14"));
    TEST_ASSERT_EQUAL(GuardResult::BLOCKED_BLACKLIST, CommandGuard::validate("08"));
    TEST_ASSERT_EQUAL(GuardResult::BLOCKED_BLACKLIST, CommandGuard::validate("2E0102"));
}

void test_obd_parser_multi_frame_11bit_and_29bit_can_headers(void) {
    uint8_t bytes[32]{};
    // 1. 11-bit CAN header multi-frame (ISO-TP First Frame 10 14, Consecutive 21, 22)
    const char* raw11 = "7E8 10 14 49 02 01 4D 4D 53\r\n"
                        "7E8 21 5A 43 37 32 53 30 30\r\n"
                        "7E8 22 30 31 32 33 34 35 36\r\n>";
    size_t count11 = ObdParser::extractHexBytes(raw11, bytes, sizeof(bytes));
    TEST_ASSERT_EQUAL_UINT32(20, count11);
    TEST_ASSERT_EQUAL_HEX8(0x49, bytes[0]);
    TEST_ASSERT_EQUAL_HEX8(0x36, bytes[19]);

    // 2. 29-bit CAN header multi-frame (ISO-TP First Frame 10 14, Consecutive 21, 22)
    const char* raw29 = "18DAF110 10 14 49 02 01 4D 52 30\r\n"
                        "18DAF110 21 42 41 33 46 53 30 30\r\n"
                        "18DAF110 22 30 31 32 33 34 35 36\r\n>";
    size_t count29 = ObdParser::extractHexBytes(raw29, bytes, sizeof(bytes));
    TEST_ASSERT_EQUAL_UINT32(20, count29);
    TEST_ASSERT_EQUAL_HEX8(0x49, bytes[0]);
    TEST_ASSERT_EQUAL_HEX8(0x36, bytes[19]);

    // 3. Compact unspaced 11-bit CAN ID prefix: 7E803412F82\r\n>
    size_t count = ObdParser::extractHexBytes("7E803412F82\r\n>", bytes, sizeof(bytes));
    TEST_ASSERT_EQUAL_UINT32(3, count);
    TEST_ASSERT_EQUAL_HEX8(0x41, bytes[0]);
    TEST_ASSERT_EQUAL_HEX8(0x2F, bytes[1]);
    TEST_ASSERT_EQUAL_HEX8(0x82, bytes[2]);
}

void test_settings_redline_rpm_warn_adjustment(void) {
    SettingsManager& sm = SettingsManager::instance();
    sm.reset(); // max 8000, red 6200, warn 5400
    TEST_ASSERT_EQUAL_UINT16(5400, sm.get().warnRpm);

    // Setting redline to 4500 must adjust warnRpm below 4500 and NOT inflate redline to 5500
    sm.setRedlineRpm(4500);
    TEST_ASSERT_EQUAL_UINT16(4500, sm.get().redlineRpm);
    TEST_ASSERT_EQUAL_UINT16(4000, sm.get().warnRpm);
}

void test_settings_screen_rotation(void) {
    SettingsManager& sm = SettingsManager::instance();
    sm.reset();
    TEST_ASSERT_EQUAL(ScreenRotation::NORMAL, sm.get().screenRotation);

    sm.setScreenRotation(ScreenRotation::INVERTED);
    TEST_ASSERT_EQUAL(ScreenRotation::INVERTED, sm.get().screenRotation);

    sm.cycleScreenRotation();
    TEST_ASSERT_EQUAL(ScreenRotation::NORMAL, sm.get().screenRotation);
}

void test_buzzer_controller_all_10_tones(void) {
    BuzzerController& bz = BuzzerController::instance();
    bz.setMode(BuzzerMode::ALL_ON);
    bz.resetTestCounters();

    // 1. Aleart *
    bz.playAlert();
    TEST_ASSERT_EQUAL(BuzzerTone::ALERT, bz.getLastTone());
    TEST_ASSERT_EQUAL(1, bz.getPlayCount());
    TEST_ASSERT_TRUE(bz.isPatternActive());
    TEST_ASSERT_EQUAL(11, bz.getPatternTotalSteps());

    // 2. Armed *
    bz.playArmed();
    TEST_ASSERT_EQUAL(BuzzerTone::ARMED, bz.getLastTone());
    TEST_ASSERT_EQUAL(2, bz.getPlayCount());
    TEST_ASSERT_FALSE(bz.isPatternActive());

    // 3. finish *
    bz.playFinish();
    TEST_ASSERT_EQUAL(BuzzerTone::FINISH, bz.getLastTone());
    TEST_ASSERT_EQUAL(3, bz.getPlayCount());

    // 4. Newrecord *
    bz.playNewRecord();
    TEST_ASSERT_EQUAL(BuzzerTone::NEW_RECORD, bz.getLastTone());
    TEST_ASSERT_EQUAL(4, bz.getPlayCount());

    // 5. NavrailTouch
    bz.playNavrailTouch();
    TEST_ASSERT_EQUAL(BuzzerTone::NAVRAIL_TOUCH, bz.getLastTone());
    TEST_ASSERT_EQUAL(5, bz.getPlayCount());

    // 6. Button_back
    bz.playButtonBack();
    TEST_ASSERT_EQUAL(BuzzerTone::BUTTON_BACK, bz.getLastTone());
    TEST_ASSERT_EQUAL(6, bz.getPlayCount());

    // 7. Button_select
    bz.playButtonSelect();
    TEST_ASSERT_EQUAL(BuzzerTone::BUTTON_SELECT, bz.getLastTone());
    TEST_ASSERT_EQUAL(7, bz.getPlayCount());

    // 8. Button_Up_Down
    bz.playButtonUpDown();
    TEST_ASSERT_EQUAL(BuzzerTone::BUTTON_UP_DOWN, bz.getLastTone());
    TEST_ASSERT_EQUAL(8, bz.getPlayCount());

    // 9. Speed_aleart *
    bz.playSpeedAlert();
    TEST_ASSERT_EQUAL(BuzzerTone::SPEED_ALERT, bz.getLastTone());
    TEST_ASSERT_EQUAL(9, bz.getPlayCount());
    TEST_ASSERT_TRUE(bz.isPatternActive());
    TEST_ASSERT_EQUAL(11, bz.getPatternTotalSteps());

    // 10. TouchwhenLock
    bz.mute();
    bz.playTouchWhenLock();
    TEST_ASSERT_EQUAL(BuzzerTone::TOUCH_WHEN_LOCK, bz.getLastTone());
    TEST_ASSERT_EQUAL(10, bz.getPlayCount());
}

void test_buzzer_controller_mode_filtering_comprehensive(void) {
    BuzzerController& bz = BuzzerController::instance();

    // Verify isAlarmTone static classification
    TEST_ASSERT_TRUE(BuzzerController::isAlarmTone(BuzzerTone::ALERT));
    TEST_ASSERT_TRUE(BuzzerController::isAlarmTone(BuzzerTone::ARMED));
    TEST_ASSERT_TRUE(BuzzerController::isAlarmTone(BuzzerTone::FINISH));
    TEST_ASSERT_TRUE(BuzzerController::isAlarmTone(BuzzerTone::NEW_RECORD));
    TEST_ASSERT_TRUE(BuzzerController::isAlarmTone(BuzzerTone::SPEED_ALERT));
    TEST_ASSERT_TRUE(BuzzerController::isAlarmTone(BuzzerTone::WARNING_BEEP));
    TEST_ASSERT_TRUE(BuzzerController::isAlarmTone(BuzzerTone::SUCCESS_TONE));

    TEST_ASSERT_FALSE(BuzzerController::isAlarmTone(BuzzerTone::NAVRAIL_TOUCH));
    TEST_ASSERT_FALSE(BuzzerController::isAlarmTone(BuzzerTone::BUTTON_BACK));
    TEST_ASSERT_FALSE(BuzzerController::isAlarmTone(BuzzerTone::BUTTON_SELECT));
    TEST_ASSERT_FALSE(BuzzerController::isAlarmTone(BuzzerTone::BUTTON_UP_DOWN));
    TEST_ASSERT_FALSE(BuzzerController::isAlarmTone(BuzzerTone::TOUCH_WHEN_LOCK));
    TEST_ASSERT_FALSE(BuzzerController::isAlarmTone(BuzzerTone::TAP_TICK));
    TEST_ASSERT_FALSE(BuzzerController::isAlarmTone(BuzzerTone::NONE));

    // Test ALARM_ONLY mode: only tones with * play
    bz.setMode(BuzzerMode::ALARM_ONLY);
    bz.resetTestCounters();

    // The 5 Non-Alarm UI sounds must be MUTED in ALARM_ONLY mode:
    bz.playNavrailTouch();
    TEST_ASSERT_EQUAL(0, bz.getPlayCount());
    bz.playButtonBack();
    TEST_ASSERT_EQUAL(0, bz.getPlayCount());
    bz.playButtonSelect();
    TEST_ASSERT_EQUAL(0, bz.getPlayCount());
    bz.playButtonUpDown();
    TEST_ASSERT_EQUAL(0, bz.getPlayCount());
    bz.playTouchWhenLock();
    TEST_ASSERT_EQUAL(0, bz.getPlayCount());

    // The 5 Alarm sounds MUST play in ALARM_ONLY mode:
    bz.playAlert();
    TEST_ASSERT_EQUAL(1, bz.getPlayCount());
    bz.playArmed();
    TEST_ASSERT_EQUAL(2, bz.getPlayCount());
    bz.playFinish();
    TEST_ASSERT_EQUAL(3, bz.getPlayCount());
    bz.playNewRecord();
    TEST_ASSERT_EQUAL(4, bz.getPlayCount());
    bz.playSpeedAlert();
    TEST_ASSERT_EQUAL(5, bz.getPlayCount());

    // Test MUTE mode: all 10 sounds are MUTED
    bz.setMode(BuzzerMode::MUTE);
    bz.resetTestCounters();

    bz.playAlert();
    bz.playArmed();
    bz.playFinish();
    bz.playNewRecord();
    bz.playSpeedAlert();
    bz.playNavrailTouch();
    bz.playButtonBack();
    bz.playButtonSelect();
    bz.playButtonUpDown();
    bz.playTouchWhenLock();
    TEST_ASSERT_EQUAL(0, bz.getPlayCount());

    // Restore to ALL_ON
    bz.setMode(BuzzerMode::ALL_ON);
}

void test_buzzer_controller_pattern_progression(void) {
    BuzzerController& bz = BuzzerController::instance();
    bz.setMode(BuzzerMode::ALL_ON);
    bz.resetTestCounters();

    bz.playAlert(); // 11 steps
    TEST_ASSERT_TRUE(bz.isPatternActive());
    TEST_ASSERT_EQUAL(1, bz.getPatternStep());
    TEST_ASSERT_EQUAL(11, bz.getPatternTotalSteps());

    // Advance steps via update(time)
    uint32_t t = 100;
    bz.update(t); // Step 1 executed -> step becomes 2
    TEST_ASSERT_EQUAL(2, bz.getPatternStep());

    // Try playing a UI click while alert pattern is running - must not interrupt
    bz.playNavrailTouch();
    TEST_ASSERT_EQUAL(BuzzerTone::ALERT, bz.getLastTone()); // still Alert!
    TEST_ASSERT_TRUE(bz.isPatternActive());

    // Advance to end of pattern
    for (int i = 0; i < 15; ++i) {
        t += 200;
        bz.update(t);
    }
    TEST_ASSERT_FALSE(bz.isPatternActive());
    TEST_ASSERT_EQUAL(0, bz.getPatternTotalSteps());
}

void test_alert_monitor_speed_alert_edge_trigger(void) {
    AlertMonitor& mon = AlertMonitor::instance();
    BuzzerController& bz = BuzzerController::instance();
    bz.setMode(BuzzerMode::ALL_ON);
    bz.resetTestCounters();
    mon.reset();

    VehicleState s;
    s.isConnected = true;
    s.speedKmh = 80;
    mon.update(s, 100);
    TEST_ASSERT_EQUAL(0, mon.getSpeedAlertCount());

    // Speed rises to 89: below 90, no alert
    s.speedKmh = 89;
    mon.update(s, 200);
    TEST_ASSERT_EQUAL(0, mon.getSpeedAlertCount());

    // Speed rises from 89 to 90: RISING EDGE TRIGGER!
    s.speedKmh = 90;
    mon.update(s, 300);
    TEST_ASSERT_EQUAL(1, mon.getSpeedAlertCount());
    TEST_ASSERT_EQUAL(BuzzerTone::SPEED_ALERT, bz.getLastTone());

    // Speed continues accelerating: 91, 95 km/h -> NO repeat trigger (1 loop only)
    s.speedKmh = 91;
    mon.update(s, 400);
    TEST_ASSERT_EQUAL(1, mon.getSpeedAlertCount());

    s.speedKmh = 95;
    mon.update(s, 500);
    TEST_ASSERT_EQUAL(1, mon.getSpeedAlertCount());

    // Speed decelerates from 90 to 89: MUST NOT ALERT on return
    s.speedKmh = 89;
    mon.update(s, 600);
    TEST_ASSERT_EQUAL(1, mon.getSpeedAlertCount());

    s.speedKmh = 85;
    mon.update(s, 700);
    TEST_ASSERT_EQUAL(1, mon.getSpeedAlertCount());

    // Speed accelerates from 85 to 90 again: RISING EDGE TRIGGERS AGAIN
    s.speedKmh = 90;
    mon.update(s, 800);
    TEST_ASSERT_EQUAL(2, mon.getSpeedAlertCount());
}

void test_alert_monitor_speed_alert_sprint_suppression(void) {
    AlertMonitor& mon = AlertMonitor::instance();
    TripComputer& tc = TripComputer::instance();
    BuzzerController& bz = BuzzerController::instance();
    bz.setMode(BuzzerMode::ALL_ON);
    bz.resetTestCounters();
    mon.reset();

    // 1. Arm sprint mode
    tc.resetTrip();
    tc.setSprintMode(SprintMode::SPEED_0_100);
    tc.update(0, true, 20000);
    tc.update(0, true, 20600); // 600ms stationary >= kArmDelayMs (500ms) -> ARMED
    TEST_ASSERT_EQUAL(AccelTimerState::ARMED, tc.getAccelState());

    // 2. Launch sprint
    tc.update(20, true, 20700); // rolling >= 1.0 km/h -> RUNNING
    TEST_ASSERT_EQUAL(AccelTimerState::RUNNING, tc.getAccelState());

    // 3. During sprint run, speed crosses 89 -> 90 km/h
    VehicleState s;
    s.isConnected = true;
    s.speedKmh = 89;
    mon.update(s, 20800);
    s.speedKmh = 95; // crosses > 90 km/h while sprinting!
    mon.update(s, 20900);

    // MUST BE SUPPRESSED in sprint mode!
    TEST_ASSERT_EQUAL(0, mon.getSpeedAlertCount());

    // Complete sprint run
    tc.update(102, true, 21000);
    TEST_ASSERT_EQUAL(AccelTimerState::FINISHED, tc.getAccelState());
}

void test_alert_monitor_mil_and_pid_warning_alerts(void) {
    AlertMonitor& mon = AlertMonitor::instance();
    BuzzerController& bz = BuzzerController::instance();
    bz.setMode(BuzzerMode::ALL_ON);
    bz.resetTestCounters();
    mon.reset();

    VehicleState s;
    s.isConnected = true;
    s.milActive = false;
    s.speedKmh = 40;
    s.rpm = 2000;
    s.coolantTempC = 85;
    s.batteryVoltage = 13.8f;
    mon.update(s, 100);
    TEST_ASSERT_EQUAL(0, mon.getMilAlertCount());
    TEST_ASSERT_EQUAL(0, mon.getPidAlertCount());

    // 1. Check Engine (MIL) turns ON: False -> True rising edge
    s.milActive = true;
    mon.update(s, 200);
    TEST_ASSERT_EQUAL(1, mon.getMilAlertCount());
    TEST_ASSERT_EQUAL(BuzzerTone::ALERT, bz.getLastTone());

    // MIL remains ON: no duplicate trigger
    mon.update(s, 300);
    TEST_ASSERT_EQUAL(1, mon.getMilAlertCount());

    // 2. Coolant temp enters warning zone (>= 100°C)
    bz.resetTestCounters();
    s.coolantTempC = 101;
    mon.update(s, 400);
    TEST_ASSERT_EQUAL(1, mon.getPidAlertCount());
    TEST_ASSERT_EQUAL(BuzzerTone::ALERT, bz.getLastTone());

    // Coolant temp remains hot: no duplicate trigger
    s.coolantTempC = 104;
    mon.update(s, 500);
    TEST_ASSERT_EQUAL(1, mon.getPidAlertCount());

    // 3. RPM enters warning zone (>= 5400) while coolant is still hot
    bz.resetTestCounters();
    s.rpm = 5500;
    mon.update(s, 600);
    TEST_ASSERT_EQUAL(2, mon.getPidAlertCount());
    TEST_ASSERT_EQUAL(BuzzerTone::ALERT, bz.getLastTone());

    // RPM drops back to normal
    s.rpm = 3000;
    mon.update(s, 700);
    TEST_ASSERT_EQUAL(2, mon.getPidAlertCount());

    // RPM rises into warning zone again: triggers new alert!
    bz.resetTestCounters();
    s.rpm = 5600;
    mon.update(s, 800);
    TEST_ASSERT_EQUAL(3, mon.getPidAlertCount());
    TEST_ASSERT_EQUAL(BuzzerTone::ALERT, bz.getLastTone());
}

void test_trip_computer_sprint_buzzer_tones(void) {
    TripComputer& tc = TripComputer::instance();
    BuzzerController& bz = BuzzerController::instance();
    bz.setMode(BuzzerMode::ALL_ON);
    bz.resetTestCounters();

    // Reset best record and trip
    tc.resetTrip();
    tc.resetSprintBest(SprintMode::SPEED_0_100);

    // 1. Stop vehicle -> auto-arm -> playArmed()
    tc.setSprintMode(SprintMode::SPEED_0_100);
    tc.update(0, true, 30000);
    tc.update(0, true, 30600); // >= 500ms stopped
    TEST_ASSERT_EQUAL(AccelTimerState::ARMED, tc.getAccelState());
    TEST_ASSERT_EQUAL(BuzzerTone::ARMED, bz.getLastTone());

    // 2. Roll to launch
    tc.update(5, true, 30700);
    TEST_ASSERT_EQUAL(AccelTimerState::RUNNING, tc.getAccelState());

    // 3. Reach 100 km/h with a new best record -> playNewRecord()
    bz.resetTestCounters();
    uint32_t t = 30700;
    while (t < 35000) {
        t += 500;
        uint8_t spd = 5 + static_cast<uint8_t>((t - 30700) * 95 / 4300);
        tc.update(spd, true, t);
    }
    TEST_ASSERT_EQUAL(AccelTimerState::FINISHED, tc.getAccelState());
    TEST_ASSERT_EQUAL(BuzzerTone::NEW_RECORD, bz.getLastTone());

    // 4. Do another run, but slower than best -> playFinish()
    t += 500;
    tc.update(0, true, t);
    t += 600;
    tc.update(0, true, t); // re-arm
    TEST_ASSERT_EQUAL(AccelTimerState::ARMED, tc.getAccelState());
    t += 100;
    tc.update(5, true, t); // launch
    TEST_ASSERT_EQUAL(AccelTimerState::RUNNING, tc.getAccelState());

    bz.resetTestCounters();
    uint32_t start2 = t;
    while (t < start2 + 8000) {
        t += 500;
        uint8_t spd = 5 + static_cast<uint8_t>((t - start2) * 95 / 8000);
        tc.update(spd, true, t);
    }
    TEST_ASSERT_EQUAL(AccelTimerState::FINISHED, tc.getAccelState());
    TEST_ASSERT_EQUAL(BuzzerTone::FINISH, bz.getLastTone());
}

void test_safety_lock_touch_buzz(void) {
    BuzzerController& bz = BuzzerController::instance();
    bz.setMode(BuzzerMode::ALL_ON);
    bz.resetTestCounters();

    core::DrivingLockState lock;
    // Stationary -> not locked
    lock.update(0, 1000);
    TEST_ASSERT_FALSE(lock.isLocked);

    // Drive >= 5 km/h for > 2000ms -> locked
    lock.update(20, 2000);
    lock.update(20, 4500);
    TEST_ASSERT_TRUE(lock.isLocked);

    // Tap attempt while locked: triggers touch attempt and playTouchWhenLock
    lock.triggerTouchAttempt(4600);
    TEST_ASSERT_TRUE(lock.isFlashing(4700));

    bz.playTouchWhenLock();
    TEST_ASSERT_EQUAL(BuzzerTone::TOUCH_WHEN_LOCK, bz.getLastTone());
    TEST_ASSERT_EQUAL(1, bz.getPlayCount());

    // In ALARM_ONLY mode, TouchwhenLock is muted
    bz.setMode(BuzzerMode::ALARM_ONLY);
    bz.playTouchWhenLock();
    TEST_ASSERT_EQUAL(1, bz.getPlayCount()); // unchanged!

    bz.setMode(BuzzerMode::ALL_ON);
}

void test_normal_driving_does_not_arm_and_plays_speed_alert(void) {
    TripComputer& tc = TripComputer::instance();
    AlertMonitor& mon = AlertMonitor::instance();
    BuzzerController& bz = BuzzerController::instance();
    bz.setMode(BuzzerMode::ALL_ON);
    bz.resetTestCounters();
    tc.resetTrip();
    mon.reset();

    // In normal driving: sprint is NOT active
    TEST_ASSERT_FALSE(tc.isSprintActive());

    // 1. Stopped at a red light for 2000ms: MUST NOT ARM AND MUST NOT BEEP ARMED
    tc.update(0, true, 1000);
    tc.update(0, true, 3000);
    TEST_ASSERT_EQUAL(AccelTimerState::IDLE, tc.getAccelState());
    TEST_ASSERT_EQUAL(0, bz.getPlayCount());

    // 2. Drive away from stoplight and accelerate to 90 km/h: MUST PLAY SPEED ALERT!
    VehicleState s;
    s.isConnected = true;
    s.speedKmh = 50;
    tc.update(s.speedKmh, true, 4000);
    mon.update(s, 4000);
    TEST_ASSERT_EQUAL(0, mon.getSpeedAlertCount());

    s.speedKmh = 89;
    tc.update(s.speedKmh, true, 5000);
    mon.update(s, 5000);
    TEST_ASSERT_EQUAL(0, mon.getSpeedAlertCount());

    s.speedKmh = 90;
    tc.update(s.speedKmh, true, 6000);
    mon.update(s, 6000);
    TEST_ASSERT_EQUAL(1, mon.getSpeedAlertCount());
    TEST_ASSERT_EQUAL(BuzzerTone::SPEED_ALERT, bz.getLastTone());

    // 3. Drive past 100 km/h on highway: MUST NOT TRIGGER FINISH OR NEW RECORD!
    bz.resetTestCounters();
    s.speedKmh = 105;
    tc.update(s.speedKmh, true, 7000);
    mon.update(s, 7000);
    TEST_ASSERT_EQUAL(AccelTimerState::IDLE, tc.getAccelState());
    TEST_ASSERT_EQUAL(0, bz.getPlayCount());
}

void test_speed_alert_hysteresis_and_empty_slots_guard(void) {
    AlertMonitor& mon = AlertMonitor::instance();
    BuzzerController& bz = BuzzerController::instance();
    bz.setMode(BuzzerMode::ALL_ON);
    bz.resetTestCounters();
    mon.reset();

    VehicleState s;
    s.isConnected = true;
    s.speedKmh = 80;
    mon.update(s, 100);

    // Accelerate to 90: first alert
    s.speedKmh = 90;
    mon.update(s, 200);
    TEST_ASSERT_EQUAL(1, mon.getSpeedAlertCount());

    // Flutter: 90 -> 89 -> 90 -> 89 -> 90 (does not drop < 88, so NO re-arming!)
    s.speedKmh = 89;
    mon.update(s, 300);
    TEST_ASSERT_EQUAL(1, mon.getSpeedAlertCount());

    s.speedKmh = 90;
    mon.update(s, 400);
    TEST_ASSERT_EQUAL(1, mon.getSpeedAlertCount());

    s.speedKmh = 89;
    mon.update(s, 500);
    TEST_ASSERT_EQUAL(1, mon.getSpeedAlertCount());

    s.speedKmh = 90;
    mon.update(s, 600);
    TEST_ASSERT_EQUAL(1, mon.getSpeedAlertCount());

    // Slow down to 87 (< 88 km/h): RE-ARMS!
    s.speedKmh = 87;
    mon.update(s, 700);
    TEST_ASSERT_EQUAL(1, mon.getSpeedAlertCount());

    // Accelerate back to 90: TRIGGERS SECOND ALERT
    s.speedKmh = 90;
    mon.update(s, 800);
    TEST_ASSERT_EQUAL(2, mon.getSpeedAlertCount());

    // Empty slot (0xFF) guard: configure slots with 0xFF
    auto& sm = SettingsManager::instance();
    sm.setDashSlot(0, 0xFF);
    sm.setLiveSlot(0, 0xFF);
    s.dashSlotValues[0] = 0.0f;
    s.liveSlotValues[0] = 0.0f;
    mon.update(s, 900);
    TEST_ASSERT_EQUAL(0, mon.getPidAlertCount()); // 0xFF must not trigger warning alert!
}

void test_elm327_auto_protocol_kline_timeouts(void) {
    SettingsManager::instance().resetToDefaults(); // selectedProtocol = 0 (ATSP0)
    MockTransport transport;
    VehicleStateManager stateMgr;
    Elm327Engine engine(transport, stateMgr);
    transport.connect("00:11:22:33:44:55");

    // Advance through 8 AT init commands -> POLLING directly with firstQueryPending_ = true
    uint32_t t = 100;
    for (int i = 0; i < 8; ++i) {
        engine.update(t);
        t += 10;
        transport.feedResponse("OK\r\n>");
        engine.update(t);
        t += 10;
    }
    engine.update(t);
    TEST_ASSERT_EQUAL(ElmState::POLLING, engine.getState());
    TEST_ASSERT_EQUAL_STRING("010C\r", transport.getLastWritten().c_str());

    // Advance 2500ms without prompt: must NOT timeout yet because firstQueryPending_ gives 6000ms for ATSP0!
    t += 2500;
    engine.update(t);
    TEST_ASSERT_EQUAL(ElmState::POLLING, engine.getState());

    // Advance to >= 6000ms: triggers soft break -> TIMEOUT_RECOVERY -> POLLING on next tick
    t += 3600;
    engine.update(t);
    TEST_ASSERT_EQUAL(ElmState::TIMEOUT_RECOVERY, engine.getState());

    t += 10;
    engine.update(t);
    TEST_ASSERT_EQUAL(ElmState::POLLING, engine.getState());

    SettingsManager::instance().resetToDefaults();
}

void test_elm327_ecu_power_off_ignition_off(void) {
    SettingsManager::instance().resetToDefaults();
    SettingsManager::instance().setSelectedProtocol(6);
    MockTransport transport;
    VehicleStateManager stateMgr;
    Elm327Engine engine(transport, stateMgr);
    transport.connect("00:11:22:33:44:55");

    // Advance through 8 init commands to POLLING
    uint32_t t = 100;
    for (int i = 0; i < 8; ++i) {
        engine.update(t);
        t += 10;
        transport.feedResponse("OK\r\n>");
        engine.update(t);
        t += 10;
    }

    // Healthy polling response with RPM=2000, Speed=60
    engine.update(t); // 010C1
    t += 10;
    transport.feedResponse("41 0C 1F 40\r\n>");
    engine.update(t);

    t += 30;
    engine.update(t); // 010D1
    t += 10;
    transport.feedResponse("41 0D 3C\r\n>");
    engine.update(t);

    t += 30;
    engine.update(t); // 01111
    t += 10;
    transport.feedResponse("41 11 20\r\n>");
    engine.update(t);

    VehicleState s = stateMgr.getSnapshot();
    TEST_ASSERT_EQUAL_UINT16(2000, s.rpm);
    TEST_ASSERT_EQUAL_UINT8(60, s.speedKmh);
    TEST_ASSERT_TRUE(s.isEngineRunning);
    TEST_ASSERT_EQUAL(ElmState::POLLING, engine.getState());

    // Ignition turned off: 010C returns NO DATA repeatedly
    t += 30;
    engine.update(t); // 010C
    TEST_ASSERT_EQUAL_STRING("010C\r", transport.getLastWritten().c_str());
    transport.feedResponse("NO DATA\r\n>");
    t += 10;
    engine.update(t); // records nonDataStartTime_

    // After 1000ms (< 2000ms): RPM still intact
    t = 1350;
    while (transport.getLastWritten() != "010C\r") {
        if (transport.getLastWritten().length() > 0) {
            transport.feedResponse("NO DATA\r\n>");
            t += 10; engine.update(t);
        }
        t += 30; engine.update(t);
    }
    transport.feedResponse("NO DATA\r\n>");
    t += 10; engine.update(t);
    s = stateMgr.getSnapshot();
    TEST_ASSERT_EQUAL_UINT16(2000, s.rpm);

    // After >= 2000ms of 010C NO DATA: RPM and Speed zeroed, isEngineRunning false!
    t = 2500;
    while (true) {
        engine.update(t);
        if (transport.getLastWritten() == "010C\r") {
            transport.feedResponse("NO DATA\r\n>");
            t += 10; engine.update(t);
            break;
        }
        if (transport.getLastWritten() == "ATRV\r") {
            transport.feedResponse("12.4V\r\n>");
        } else if (transport.getLastWritten().length() > 0) {
            transport.feedResponse("NO DATA\r\n>");
        }
        t += 30;
    }
    s = stateMgr.getSnapshot();
    TEST_ASSERT_EQUAL_UINT16(0, s.rpm);
    TEST_ASSERT_EQUAL_UINT8(0, s.speedKmh);
    TEST_ASSERT_FALSE(s.isEngineRunning);
    TEST_ASSERT_TRUE(transport.isConnected());

    // Persist 010C1 NO DATA past 6000ms (with ATRV still replying 12.4V!): enters standby without dropping BLE!
    while (t < 9000 && !engine.isEcuStandby()) {
        t += 40;
        engine.update(t);
        if (transport.getLastWritten() == "ATRV\r") {
            transport.feedResponse("12.4V\r\n>");
        } else if (transport.getLastWritten().length() > 0) {
            transport.feedResponse("NO DATA\r\n>");
        }
        t += 10;
        engine.update(t);
    }
    TEST_ASSERT_TRUE(transport.isConnected());
    TEST_ASSERT_TRUE(engine.isEcuStandby());
    TEST_ASSERT_EQUAL(ElmState::POLLING, engine.getState());
    s = stateMgr.getSnapshot();
    TEST_ASSERT_FALSE(s.isConnected);
    TEST_ASSERT_FALSE(s.isEngineRunning);

    // In standby, probes 010C every >= 1500ms and recovers when ignition returns
    t += 1600;
    engine.update(t);
    TEST_ASSERT_EQUAL_STRING("010C\r", transport.getLastWritten().c_str());
    transport.feedResponse("41 0C 1F 40\r\n>");
    t += 10;
    engine.update(t);
    TEST_ASSERT_FALSE(engine.isEcuStandby());
    s = stateMgr.getSnapshot();
    TEST_ASSERT_TRUE(s.isConnected);
    TEST_ASSERT_EQUAL_UINT16(2000, s.rpm);

    SettingsManager::instance().resetToDefaults();
}

void test_turbo_boost_pid_0x00_guard(void) {
    // 1. findIndexByPid(0x00) must return -1 (reserved)
    TEST_ASSERT_EQUAL(-1, UniversalPidCatalog::findIndexByPid(0x00));

    // 2. Virtual Turbo Boost is 0xF0 at index 11 and obeys unsupportedCatalogBits
    int boostIdx = UniversalPidCatalog::findIndexByPid(0xF0);
    TEST_ASSERT_EQUAL(11, boostIdx);
    TEST_ASSERT_TRUE(UniversalPidCatalog::isCatalogIndexSupported(11, 0));
    TEST_ASSERT_FALSE(UniversalPidCatalog::isCatalogIndexSupported(11, (1ULL << 11)));

    // 3. PidScheduler polls 0133 (BARO) then 010B (MAP) for slot 11, never 0100 or 01F0
    PidScheduler scheduler;
    scheduler.setProtocol(6);
    uint8_t liveSlots[6] = { 11, 11, 11, 11, 11, 11 };
    scheduler.setLiveSlots(liveSlots);
    scheduler.setActivePage(1);

    ScheduledQuery q;
    TEST_ASSERT_TRUE(scheduler.getNextQuery(100, q));
    TEST_ASSERT_EQUAL_STRING("0133", q.command);
    TEST_ASSERT_TRUE(scheduler.getNextQuery(200, q));
    TEST_ASSERT_EQUAL_STRING("010B", q.command);
}

void test_scheduler_get_live_slots_and_trip_computer_nvs_protection(void) {
    PidScheduler scheduler;
    uint8_t customLiveSlots[6] = { 1, 2, 3, 4, 5, 6 };
    scheduler.setLiveSlots(customLiveSlots);

    const uint8_t* liveSlots = scheduler.getLiveSlots();
    for (uint8_t i = 0; i < 6; ++i) {
        TEST_ASSERT_EQUAL_UINT8(customLiveSlots[i], liveSlots[i]);
    }

    // Verify trip computer resetSprintBest executes cleanly with buzzer muted
    BuzzerController::instance().playArmed();
    TripComputer::instance().resetSprintBest(SprintMode::SPEED_0_100);
    TEST_ASSERT_EQUAL_UINT8(0, BuzzerController::instance().getPatternTotalSteps());
}

void test_elm327_per_pid_3_strike_isolation(void) {
    SettingsManager::instance().resetToDefaults();
    SettingsManager::instance().setSelectedProtocol(6);

    MockTransport transport;
    VehicleStateManager stateMgr;
    Elm327Engine engine(transport, stateMgr);
    transport.connect("00:11:22:33:44:55");

    uint32_t t = 100;
    for (int i = 0; i < 8; ++i) {
        engine.update(t); t += 10;
        transport.feedResponse("OK\r\n>");
        engine.update(t); t += 10;
    }

    // Configure Live Page with slot 0 = STFT1 (0x06, catIdx=13), slot 1 = MAP (0x0B, catIdx=8),
    // boost slot 2 = 11 (0xF0), plus Coolant(6)/IAT(7)/RPM(0).
    uint8_t slots[6] = { 13, 8, 11, 6, 7, 0 };
    engine.getScheduler().setLiveSlots(slots);
    engine.getScheduler().setActivePage(1);

    // 3x NO DATA on STFT1 (0x06) -> catIdx 13 marked unsupported, without triggering standby!
    int stftFails = 0;
    int mapNegs = 0;
    for (int step = 0; step < 60 && (!engine.getScheduler().isCatalogUnsupported(13) ||
                                    !engine.getScheduler().isCatalogUnsupported(8)); ++step) {
        t += 1000;
        engine.update(t);
        std::string w = transport.getLastWritten();
        if (w == "0106\r") {
            stftFails++;
            transport.feedResponse("NO DATA\r\n>");
        } else if (w == "010B\r") {
            mapNegs++;
            transport.feedResponse("7F 01 12\r\n>");
        } else if (w == "010C\r") {
            transport.feedResponse("41 0C 1A F8\r\n>");
        } else if (w == "ATRV\r") {
            transport.feedResponse("13.8V\r\n>");
        } else {
            transport.feedResponse("41 05 78\r\n>");
        }
        t += 10;
        engine.update(t);
    }

    TEST_ASSERT_EQUAL(3, stftFails);
    TEST_ASSERT_TRUE(engine.getScheduler().isCatalogUnsupported(13));
    TEST_ASSERT_EQUAL(3, mapNegs);
    TEST_ASSERT_TRUE(engine.getScheduler().isCatalogUnsupported(8));
    // Mirror MAP (0x0B) failure to Virtual Boost (0xF0, catIdx 11)
    TEST_ASSERT_TRUE(engine.getScheduler().isCatalogUnsupported(11));
    TEST_ASSERT_FALSE(engine.isEcuStandby());
    VehicleState s = stateMgr.getSnapshot();
    TEST_ASSERT_TRUE((s.unsupportedCatalogBits & (1ULL << 13)) != 0);
    TEST_ASSERT_TRUE((s.unsupportedCatalogBits & (1ULL << 8)) != 0);

    // Ensure 0106 and 010B are no longer scheduled after 3 strikes
    ScheduledQuery nextQ;
    for (int i = 0; i < 12; ++i) {
        t += 2000;
        if (engine.getScheduler().getNextQuery(t, nextQ)) {
            TEST_ASSERT_NOT_EQUAL(0, std::strcmp(nextQ.command, "0106"));
            TEST_ASSERT_NOT_EQUAL(0, std::strcmp(nextQ.command, "010B"));
        }
    }

    SettingsManager::instance().resetToDefaults();
}

void test_kline_bus_init_ok_and_init_sequence(void) {
    TEST_ASSERT_FALSE(ObdParser::isErrorResponse("BUS INIT: ... OK\r\n41 00 BE 3E B8 11\r\n>"));
    TEST_ASSERT_TRUE(ObdParser::isErrorResponse("BUS INIT: ERROR"));
    TEST_ASSERT_TRUE(ObdParser::isErrorResponse("BUS INIT: ...ERROR"));

    SettingsManager::instance().resetToDefaults();
    auto& sm = SettingsManager::instance();
    sm.setSelectedProtocol(3); // ISO 9141-2 K-Line

    MockTransport transport;
    VehicleStateManager stateMgr;
    Elm327Engine engine(transport, stateMgr);
    transport.connect("00:11:22:33:44:55");

    uint32_t t = 100;
    std::string cmds[8];
    for (int i = 0; i < 8; ++i) {
        engine.update(t);
        cmds[i] = transport.getLastWritten();
        t += 10;
        transport.feedResponse("OK\r\n>");
        engine.update(t);
        t += 10;
    }
    TEST_ASSERT_EQUAL_STRING("ATSP3\r", cmds[5].c_str());
    TEST_ASSERT_EQUAL_STRING("ATAT2\r", cmds[6].c_str());
    TEST_ASSERT_EQUAL_STRING("ATST64\r", cmds[7].c_str());

    // Direct entry to POLLING on K-Line; first query is 010C without '1' suffix
    TEST_ASSERT_EQUAL(ElmState::POLLING, engine.getState());
    engine.update(t);
    TEST_ASSERT_EQUAL_STRING("010C\r", transport.getLastWritten().c_str());
    transport.feedResponse("BUS INIT: ...OK\r\n41 0C 1A F8\r\n>");
    t += 10;
    engine.update(t);

    VehicleState s = stateMgr.getSnapshot();
    TEST_ASSERT_TRUE(s.isConnected);
    TEST_ASSERT_EQUAL_UINT16(1726, s.rpm);

    sm.resetToDefaults();
}

void test_obd_parser_isotp_ats0_21_22_frames_not_dropped(void) {
    const char* raw = "100E6101500078\r\n21AABB01\r\n22CCDD\r\n>";
    uint8_t bytes[32];
    size_t count = ObdParser::extractHexBytes(raw, bytes, sizeof(bytes));
    TEST_ASSERT_EQUAL_UINT32(14, count);
    TEST_ASSERT_EQUAL_HEX8(0x21, bytes[7]);
    TEST_ASSERT_EQUAL_HEX8(0x01, bytes[10]);
    TEST_ASSERT_EQUAL_HEX8(0x22, bytes[11]);

    const char* echoRaw = "2101\r\n6101040178\r\n>";
    count = ObdParser::extractHexBytes(echoRaw, bytes, sizeof(bytes));
    TEST_ASSERT_EQUAL_UINT32(5, count);
    TEST_ASSERT_EQUAL_HEX8(0x61, bytes[0]);
}

void test_settings_deferred_rpm_save_and_single_brightness_save(void) {
    SettingsManager& sm = SettingsManager::instance();
    sm.resetToDefaults();

    sm.stepMaxRpm(true, false);
    TEST_ASSERT_EQUAL_UINT16(8100, sm.get().maxRpm);
    sm.saveIfDirty();
    TEST_ASSERT_EQUAL_UINT16(8100, sm.get().maxRpm);

    sm.setBrightnessMode(BrightnessMode::AUTO);
    sm.cycleCombinedBrightness();
    TEST_ASSERT_EQUAL(BrightnessMode::MANUAL, sm.get().brightnessMode);
    TEST_ASSERT_EQUAL_UINT8(40, sm.get().manualBrightness);
}

void test_dtc_scan_29bit_and_kline_headers(void) {
    // 1. 29-bit CAN sends Mode 03 -> 07 directly on default broadcast header (never ATSH18DB33F1)
    {
        SettingsManager::instance().resetToDefaults();
        SettingsManager::instance().setSelectedProtocol(7);
        MockTransport transport;
        VehicleStateManager stateMgr;
        Elm327Engine engine(transport, stateMgr);
        transport.connect("00:11:22:33:44:55");
        uint32_t t = 100;
        for (int i = 0; i < 8; ++i) {
            engine.update(t); t += 10;
            transport.feedResponse("OK\r\n>");
            engine.update(t); t += 10;
        }
        TEST_ASSERT_EQUAL(ElmState::POLLING, engine.getState());

        engine.startDtcScan();
        t += 10; engine.update(t);
        TEST_ASSERT_EQUAL_STRING("03\r", transport.getLastWritten().c_str());
        transport.feedResponse("43 00\r\n>");
        t += 10; engine.update(t);
        t += 10; engine.update(t);
        TEST_ASSERT_EQUAL_STRING("07\r", transport.getLastWritten().c_str());
        transport.feedResponse("47 00\r\n>");
        t += 10; engine.update(t);
        TEST_ASSERT_TRUE(engine.isDtcScanComplete());
    }

    // 2. K-Line sends 03 -> 07 directly and decodes unprefixed P0011 + P0100 + P0301 correctly
    {
        SettingsManager::instance().resetToDefaults();
        SettingsManager::instance().setSelectedProtocol(3);
        MockTransport transport;
        VehicleStateManager stateMgr;
        Elm327Engine engine(transport, stateMgr);
        transport.connect("00:11:22:33:44:55");
        uint32_t t = 100;
        for (int i = 0; i < 8; ++i) {
            engine.update(t); t += 10;
            transport.feedResponse("OK\r\n>");
            engine.update(t); t += 10;
        }
        TEST_ASSERT_EQUAL(ElmState::POLLING, engine.getState());

        engine.startDtcScan();
        t += 10; engine.update(t);
        TEST_ASSERT_EQUAL_STRING("03\r", transport.getLastWritten().c_str());
        transport.feedResponse("43 00 11 01 00 03 01\r\n>");
        t += 10; engine.update(t);
        t += 10; engine.update(t);
        TEST_ASSERT_EQUAL_STRING("07\r", transport.getLastWritten().c_str());
        transport.feedResponse("47 00 00 00 00 00 00\r\n>");
        t += 10; engine.update(t);
        TEST_ASSERT_TRUE(engine.isDtcScanComplete());
        TEST_ASSERT_EQUAL_UINT8(3, engine.getScannedDtcCount());
        TEST_ASSERT_EQUAL_STRING("P0011", engine.getScannedDtcs()[0].code);
        TEST_ASSERT_EQUAL_STRING("P0100", engine.getScannedDtcs()[1].code);
        TEST_ASSERT_EQUAL_STRING("P0301", engine.getScannedDtcs()[2].code);
    }

    SettingsManager::instance().resetToDefaults();
}

void test_protocol_audit_kline_dtc_and_atst64_init(void) {
    // 1. Direct ObdParser K-Line vs CAN DTC decoding
    DtcCodeInfo out[8]{};
    size_t count = 0;
    TEST_ASSERT_EQUAL(ParseStatus::OK, ObdParser::parseDtcResponse("43 00 11 00 00 00 00\r\n>", false, out, 8, count, false));
    TEST_ASSERT_EQUAL_UINT32(1, count);
    TEST_ASSERT_EQUAL_STRING("P0011", out[0].code);

    TEST_ASSERT_EQUAL(ParseStatus::OK, ObdParser::parseDtcResponse("43 01 00 03 01 00 00\r\n>", false, out, 8, count, false));
    TEST_ASSERT_EQUAL_UINT32(2, count);
    TEST_ASSERT_EQUAL_STRING("P0100", out[0].code);
    TEST_ASSERT_EQUAL_STRING("P0301", out[1].code);

    // CAN count-prefixed still reports 0 DTCs on "43 00" and 2 DTCs on "43 02 01 00 03 01"
    TEST_ASSERT_EQUAL(ParseStatus::OK, ObdParser::parseDtcResponse("43 00\r\n>", false, out, 8, count, true));
    TEST_ASSERT_EQUAL_UINT32(0, count);
    TEST_ASSERT_EQUAL(ParseStatus::OK, ObdParser::parseDtcResponse("43 02 01 00 03 01\r\n>", false, out, 8, count, true));
    TEST_ASSERT_EQUAL_UINT32(2, count);
    TEST_ASSERT_EQUAL_STRING("P0100", out[0].code);
    TEST_ASSERT_EQUAL_STRING("P0301", out[1].code);

    // 2. Mode 03/07 echo lines stripped cleanly
    uint8_t bytes[16]{};
    size_t hexCount = ObdParser::extractHexBytes("03\r\n43 00 11\r\n>", bytes, sizeof(bytes));
    TEST_ASSERT_EQUAL_UINT32(3, hexCount);
    TEST_ASSERT_EQUAL_HEX8(0x43, bytes[0]);

    // 3. ATSP0 (Auto) & ATSP1 (J1850) emit ATST64; ATSP6 (CAN) emits ATST32
    auto checkInitTimeoutCmd = [](uint8_t proto, const char* expectedCmd) {
        SettingsManager::instance().resetToDefaults();
        SettingsManager::instance().setSelectedProtocol(proto);
        MockTransport transport;
        VehicleStateManager stateMgr;
        Elm327Engine engine(transport, stateMgr);
        transport.connect("00:11:22:33:44:55");
        uint32_t t = 100;
        std::string lastCmd;
        for (int i = 0; i < 8; ++i) {
            engine.update(t);
            lastCmd = transport.getLastWritten();
            t += 10;
            transport.feedResponse("OK\r\n>");
            engine.update(t);
            t += 10;
        }
        TEST_ASSERT_EQUAL_STRING(expectedCmd, lastCmd.c_str());
    };
    checkInitTimeoutCmd(0, "ATST64\r");
    checkInitTimeoutCmd(1, "ATST64\r");
    checkInitTimeoutCmd(6, "ATST32\r");

    // 4. PidScheduler 29-bit CAN check aligns with 7, 9, 12 (not 10 J1939)
    PidScheduler sched;
    sched.setProtocol(12);
    TEST_ASSERT_TRUE(sched.isCan29Bus());
    sched.setProtocol(10);
    TEST_ASSERT_FALSE(sched.isCan29Bus());

    SettingsManager::instance().resetToDefaults();
}

void test_elm327_virtual_turbo_boost_calculation_and_command_guard_overflow(void) {
    // 1. CommandGuard rejects overly long commands (> 63 non-space chars) without heap allocation
    char longCmd[100];
    std::memset(longCmd, '0', sizeof(longCmd) - 1);
    longCmd[sizeof(longCmd) - 1] = '\0';
    TEST_ASSERT_EQUAL(GuardResult::BLOCKED_MALFORMED, CommandGuard::validate(longCmd));
    TEST_ASSERT_GREATER_THAN(0, CommandGuard::getBlockedCount());

    // 2. Virtual Turbo Boost (0xF0): derived from MAP (0x0B) - BARO (0x33) with 101.3 kPa fallback
    auto& sm = SettingsManager::instance();
    sm.resetToDefaults();
    sm.setSelectedProtocol(6);
    sm.setLiveSlot(0, 11); // 0xF0 Turbo Boost
    MockTransport transport;
    VehicleStateManager stateMgr;
    Elm327Engine engine(transport, stateMgr);
    const uint8_t boostDash[4] = { 11, 11, 11, 11 };
    engine.getScheduler().setLiveSlots(sm.get().liveSlots);
    engine.getScheduler().setDashSlots(boostDash);
    transport.connect("00:11:22:33:44:55");

    uint32_t t = 100;
    for (int i = 0; i < 8; ++i) {
        engine.update(t); t += 10;
        transport.feedResponse("OK\r\n>");
        engine.update(t); t += 10;
    }

    // Trigger Tier 2 dashSlot 0 (0xF0 -> BARO 0133 first, then MAP 010B)
    t = 1200;
    engine.update(t);
    TEST_ASSERT_EQUAL_STRING("0133\r", transport.getLastWritten().c_str());
    transport.feedResponse("41 33 64\r\n>"); // BARO = 100 kPa
    t += 10;
    engine.update(t);

    t = 2300;
    engine.update(t);
    TEST_ASSERT_EQUAL_STRING("010B\r", transport.getLastWritten().c_str());
    transport.feedResponse("41 0B C8\r\n>"); // MAP = 200 kPa -> (200 - 100) * 0.1450377 = 14.50 PSI
    t += 10;
    engine.update(t);

    VehicleState s = stateMgr.getSnapshot();
    TEST_ASSERT_FLOAT_WITHIN(0.15f, 14.50f, s.dashSlotValues[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.15f, 14.50f, s.liveSlotValues[0]);

    sm.resetToDefaults();
}

void test_multi_ecu_dtc_and_baro_t0(void) {
    // 1. Multi-ECU broadcast DTC responses: ECM 0 DTCs + TCM P0700, and ECM P0100 + TCM P0700
    DtcCodeInfo info[8]{};
    size_t count = 0;
    TEST_ASSERT_EQUAL(ParseStatus::OK, ObdParser::parseDtcResponse(
        "43 00 00 00 00 00 00\r\n43 01 07 00 00 00 00\r\n>", false, info, 8, count));
    TEST_ASSERT_EQUAL_UINT32(1, count);
    TEST_ASSERT_EQUAL_STRING("P0700", info[0].code);

    TEST_ASSERT_EQUAL(ParseStatus::OK, ObdParser::parseDtcResponse(
        "7E8 04 43 01 01 00\r\n7E9 04 43 01 07 00\r\n>", false, info, 8, count));
    TEST_ASSERT_EQUAL_UINT32(2, count);
    TEST_ASSERT_EQUAL_STRING("P0100", info[0].code);
    TEST_ASSERT_EQUAL_STRING("P0700", info[1].code);

    // 2. PidScheduler virtual boost 0xF0 at nowMs=0 must not re-poll 0133 on next call
    PidScheduler sched;
    sched.setProtocol(6);
    uint8_t liveSlots[6] = { 11, 11, 11, 11, 11, 11 };
    sched.setLiveSlots(liveSlots);
    sched.setActivePage(1);
    ScheduledQuery q;
    TEST_ASSERT_TRUE(sched.getNextQuery(0, q));
    TEST_ASSERT_EQUAL_STRING("0133", q.command);
    TEST_ASSERT_TRUE(sched.getNextQuery(100, q));
    TEST_ASSERT_EQUAL_STRING("010B", q.command);
}

void test_universal_obd_parser_multi_ecu_and_protocol_num(void) {
    // 1. Multi-ECU negative + positive arbitration on Mode 01 PID 0x04
    uint8_t data[8]{};
    size_t len = 0;
    bool isNeg = false;
    const char* multi = "7F 01 12\r\n41 04 32\r\n>";
    TEST_ASSERT_EQUAL(ParseStatus::OK, ObdParser::parseMode01PidResponse(multi, 0x04, data, sizeof(data), len, isNeg));
    TEST_ASSERT_FALSE(isNeg);
    TEST_ASSERT_EQUAL_UINT32(1, len);
    TEST_ASSERT_EQUAL_HEX8(0x32, data[0]);

    // 2. Structural 0x7F negative vs coolant 41 05 7F (87C)
    TEST_ASSERT_EQUAL(ParseStatus::OK, ObdParser::parseMode01PidResponse("41 05 7F\r\n>", 0x05, data, sizeof(data), len, isNeg));
    TEST_ASSERT_FALSE(isNeg);
    TEST_ASSERT_EQUAL_HEX8(0x7F, data[0]);

    TEST_ASSERT_EQUAL(ParseStatus::ERROR_RESPONSE, ObdParser::parseMode01PidResponse("7F 01 12\r\n>", 0x05, data, sizeof(data), len, isNeg));
    TEST_ASSERT_TRUE(isNeg);

    // 3. ATDPN protocol number parsing
    uint8_t proto = 0;
    TEST_ASSERT_EQUAL(ParseStatus::OK, ObdParser::parseProtocolNumber("A6\r\n>", proto));
    TEST_ASSERT_EQUAL_UINT8(6, proto);
    TEST_ASSERT_EQUAL(ParseStatus::OK, ObdParser::parseProtocolNumber("3\r\n>", proto));
    TEST_ASSERT_EQUAL_UINT8(3, proto);
    TEST_ASSERT_EQUAL(ParseStatus::OK, ObdParser::parseProtocolNumber("0C\r\n>", proto));
    TEST_ASSERT_EQUAL_UINT8(12, proto);
}

void test_elm327_atdpn_resolution_and_log_ring_buffer(void) {
    // 1. ATSP0 -> ATDPN "A6" updates scheduler protocol to 6 (subsequent Mode 01 gets '1' hint)
    SettingsManager::instance().resetToDefaults();
    MockTransport transport;
    VehicleStateManager stateMgr;
    Elm327Engine engine(transport, stateMgr);
    transport.connect("00:11:22:33:44:55");

    uint32_t t = 100;
    for (int i = 0; i < 8; ++i) {
        engine.update(t); t += 10;
        transport.feedResponse("OK\r\n>");
        engine.update(t); t += 10;
    }

    // First query is 010C (proto 0 unconfirmed)
    engine.update(t);
    TEST_ASSERT_EQUAL_STRING("010C\r", transport.getLastWritten().c_str());
    transport.feedResponse("41 0C 1A F8\r\n>");
    t += 10; engine.update(t);

    // Next update dispatches ATDPN
    t += 5; engine.update(t);
    TEST_ASSERT_EQUAL_STRING("ATDPN\r", transport.getLastWritten().c_str());
    transport.feedResponse("A6\r\n>");
    t += 10; engine.update(t);
    TEST_ASSERT_EQUAL_UINT8(6, engine.getResolvedProtocol());

    // Subsequent Tier 1 query appends '1' count hint on confirmed CAN protocol 6 (010D1)
    t += 5; engine.update(t);
    TEST_ASSERT_EQUAL_STRING("010D1\r", transport.getLastWritten().c_str());

    // Auto-fallback: if adapter/ECU returns '?'/ERROR on '1' hint, disable hint for subsequent queries
    transport.feedResponse("?\r\n>");
    t += 5; engine.update(t);
    TEST_ASSERT_FALSE(engine.getScheduler().isCanCountHintEnabled());
    t += 5; engine.update(t);
    TEST_ASSERT_EQUAL_STRING("0111\r", transport.getLastWritten().c_str());

    // LivePage single-chart boost (100ms chart priority, 2000ms hidden grid)
    PidScheduler liveSched;
    liveSched.setProtocol(6);
    liveSched.setActivePage(1);
    liveSched.setBoostedSlot(2); // Slot 2 = Coolant (normally 5000ms)
    const uint8_t liveSlots[6] = { 0, 1, 6, 4, 7, 18 }; // RPM, Speed, Coolant, Throttle, IAT, Timing
    liveSched.setLiveSlots(liveSlots);
    ScheduledQuery lq;
    for (int i = 0; i < 6; ++i) {
        TEST_ASSERT_TRUE(liveSched.getNextQuery(1000, lq));
        liveSched.notifyResponseReceived(lq.tier);
    }
    // At t=1150ms (+150ms): ONLY boosted slot 2 (01051) should fire (>=100ms), hidden slots throttled (>=2000ms)
    TEST_ASSERT_TRUE(liveSched.getNextQuery(1150, lq));
    TEST_ASSERT_EQUAL_STRING("01051", lq.command);
    TEST_ASSERT_FALSE(liveSched.getNextQuery(1150, lq));

    // 2. Log ring buffer stores newest first up to kMaxLogEntries and clears properly
    ObdLogEntry logs[Elm327Engine::kMaxLogEntries]{};
    size_t count = engine.getLogSnapshot(logs, Elm327Engine::kMaxLogEntries);
    TEST_ASSERT_GREATER_THAN(0, count);
    TEST_ASSERT_EQUAL_STRING("ATDPN", logs[0].tx);

    engine.clearLogs();
    TEST_ASSERT_EQUAL_UINT32(0, engine.getLogSnapshot(logs, Elm327Engine::kMaxLogEntries));
    SettingsManager::instance().resetToDefaults();
}

void test_universal_24pid_catalog_migration_and_800ms_timeout(void) {
    // 1. Verify legacy NVS slot migration resets >= 24 indices and preserves valid v2 slots
    SettingsManager& sm = SettingsManager::instance();
    sm.resetToDefaults();
    sm.setDashSlot(2, 13);
    sm.setLiveSlot(1, 14);
    TEST_ASSERT_FALSE(sm.validateAndMigrateSlots(SettingsManager::kCatalogSchemaVer));
    TEST_ASSERT_EQUAL_UINT8(13, sm.get().dashSlots[2]);
    TEST_ASSERT_EQUAL_UINT8(14, sm.get().liveSlots[1]);

    // Stale v1 schema triggers reset back to 24-PID defaults {6, 16, 11, 7} / {0, 1, 6, 4, 7, 18}
    TEST_ASSERT_TRUE(sm.validateAndMigrateSlots(1));
    TEST_ASSERT_EQUAL_UINT8(11, sm.get().dashSlots[2]);
    TEST_ASSERT_EQUAL_UINT8(1, sm.get().liveSlots[1]);

    // Runtime setDashSlot rejects >= 24; raw out-of-bounds index triggers migration reset
    sm.setDashSlot(0, 31);
    TEST_ASSERT_EQUAL_UINT8(6, sm.get().dashSlots[0]);
    sm.setRawDashSlotForTest(0, 31);
    TEST_ASSERT_TRUE(sm.validateAndMigrateSlots(SettingsManager::kCatalogSchemaVer));
    TEST_ASSERT_EQUAL_UINT8(6, sm.get().dashSlots[0]);

    // 2. PidScheduler Tier 3 polls ATRV only (never 012F or TCM commands)
    PidScheduler sched;
    sched.setProtocol(6);
    ScheduledQuery q;
    for (int step = 0; step < 30; ++step) {
        if (sched.getNextQuery(2000 + step * 500, q)) {
            TEST_ASSERT_NOT_EQUAL(0, std::strcmp(q.command, "012F"));
            TEST_ASSERT_NOT_EQUAL('2', q.command[0]);
            sched.notifyResponseReceived(q.tier);
        }
    }

    // 3. Elm327Engine waits up to 800ms for CAN POLLING and zeroes dashSlotValues[3] on swap
    sm.resetToDefaults();
    sm.setSelectedProtocol(6);
    MockTransport transport;
    VehicleStateManager stateMgr;
    Elm327Engine engine(transport, stateMgr);
    transport.connect("00:11:22:33:44:55");

    uint32_t t = 100;
    for (int i = 0; i < 8; ++i) {
        engine.update(t); t += 10;
        transport.feedResponse("OK\r\n>");
        engine.update(t); t += 10;
    }

    engine.update(t);
    TEST_ASSERT_TRUE(transport.getLastWritten().rfind("01", 0) == 0);
    transport.clearLastWritten();
    engine.update(t + 450); // < 800ms: still waiting
    TEST_ASSERT_EQUAL_STRING("", transport.getLastWritten().c_str());
    engine.update(t + 850); // >= 800ms: soft-break triggers
    TEST_ASSERT_EQUAL_STRING("\r", transport.getLastWritten().c_str());
    transport.feedResponse(">");
    t += 900;
    engine.update(t);

    // Slot swap clearing
    VehicleState snap = stateMgr.getSnapshot();
    snap.dashSlotValues[3] = 99.0f;
    stateMgr.update(snap);
    sm.setDashSlot(3, 18); // Switch slot 3 -> Timing Advance
    t += 20;
    engine.update(t);
    snap = stateMgr.getSnapshot();
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, snap.dashSlotValues[3]);

    sm.resetToDefaults();
}

int run_all_tests(void) {
    UNITY_BEGIN();
    RUN_TEST(test_allowed_mode_01_realtime);
    RUN_TEST(test_allowed_mode_03_07_read_dtc);
    RUN_TEST(test_allowed_mode_09_vin);
    RUN_TEST(test_allowed_at_commands);
    RUN_TEST(test_blocked_mode_04_clear_dtc);
    RUN_TEST(test_blocked_mode_08_actuator_control);
    RUN_TEST(test_blocked_dangerous_uds_modes);
    RUN_TEST(test_blocked_raw_sniffers_and_transmitters);
    RUN_TEST(test_blocked_unknown_unwhitelisted);
    RUN_TEST(test_mock_obd_allowed_queries);
    RUN_TEST(test_mock_obd_blocks_dangerous_command);
    RUN_TEST(test_vehicle_state_initial_values);
    RUN_TEST(test_vehicle_state_update_and_snapshot);
    RUN_TEST(test_vehicle_state_double_buffer_alternation);
    RUN_TEST(test_mock_obd_interface_and_guard);
    RUN_TEST(test_mock_obd_simulation);
    RUN_TEST(test_mock_obd_parked_mode);
    RUN_TEST(test_theme_manager_default_theme);
    RUN_TEST(test_theme_manager_palette_values);
    RUN_TEST(test_theme_manager_cycle_themes);
    RUN_TEST(test_buzzer_controller_tones);
    RUN_TEST(test_settings_manager_defaults_and_clamping);
    RUN_TEST(test_settings_manager_cycles);
    RUN_TEST(test_buzzer_controller_mode_filtering);
    RUN_TEST(test_settings_manager_profile_and_bt_mac);
    RUN_TEST(test_mock_transport_lifecycle);
    RUN_TEST(test_obd_parser_mode01_core_pids);
    RUN_TEST(test_obd_parser_battery_voltage_atrv);
    RUN_TEST(test_obd_parser_noise_and_multiline);
    RUN_TEST(test_obd_parser_can_header_and_echo_stripping);
    RUN_TEST(test_pid_scheduler_tier1_multi_pid_selection);
    RUN_TEST(test_pid_scheduler_tier1_single_pid_fallback);
    RUN_TEST(test_pid_scheduler_skips_unsupported_pids);
    RUN_TEST(test_pid_scheduler_trip_page_focus_speed_and_mil);
    RUN_TEST(test_pid_scheduler_dash_slots);
    RUN_TEST(test_gear_ratio_default_rx);
    RUN_TEST(test_elm327_engine_dual_gear_fallback_and_throttle_sentinel);
    RUN_TEST(test_pid_scheduler_tier2_rotates_dash_slots);
    RUN_TEST(test_settings_manager_reset_and_load_dash_slots);
    RUN_TEST(test_elm327_engine_init_and_probing);
    RUN_TEST(test_elm327_engine_350ms_soft_break_timeout);
    RUN_TEST(test_ui_page_enum_count_and_routing);
    RUN_TEST(test_core_util_math_helpers);
    RUN_TEST(test_core_util_nice_range);
    RUN_TEST(test_ui_safety_policy_stationary_and_parking);
    RUN_TEST(test_universal_pid_catalog_total_and_lookup);
    RUN_TEST(test_universal_pid_catalog_decoding);
    RUN_TEST(test_universal_pid_catalog_clamping);
    RUN_TEST(test_universal_pid_catalog_bitmask_support);
    RUN_TEST(test_settings_manager_universal_live_slots_and_protocol);
    RUN_TEST(test_trip_computer_initial_and_auto_arming);
    RUN_TEST(test_trip_computer_0_100_sprint_run);
    RUN_TEST(test_trip_computer_quarter_mile_run);
    RUN_TEST(test_trip_computer_trip_odometer);
    RUN_TEST(test_driving_lock_state_machine);
    RUN_TEST(test_vehicle_parameters_model_and_clamping);
    RUN_TEST(test_dash_slots_settings_model);
    RUN_TEST(test_universal_pid_super_groups);
    RUN_TEST(test_settings_carousel_and_cycles);
    RUN_TEST(test_universal_pid_alert_levels);
    RUN_TEST(test_dtc_pagination_calculations);
    RUN_TEST(test_protocol_formatting_and_guard);
    RUN_TEST(test_universal_pid_dashboard_names);
    RUN_TEST(test_obd_parser_dtc_modes_and_formats);
    RUN_TEST(test_dtc_database_lookup_and_formatting);
    RUN_TEST(test_elm327_engine_dtc_scan_lifecycle);
    RUN_TEST(test_settings_manager_concurrency_and_snapshot);
    RUN_TEST(test_atomic_ring_buffer_concurrency);
    RUN_TEST(test_command_guard_removed_tcm_and_fc_blocked);
    RUN_TEST(test_obd_parser_multi_frame_11bit_and_29bit_can_headers);
    RUN_TEST(test_settings_redline_rpm_warn_adjustment);
    RUN_TEST(test_settings_screen_rotation);
    RUN_TEST(test_buzzer_controller_all_10_tones);
    RUN_TEST(test_buzzer_controller_mode_filtering_comprehensive);
    RUN_TEST(test_buzzer_controller_pattern_progression);
    RUN_TEST(test_alert_monitor_speed_alert_edge_trigger);
    RUN_TEST(test_alert_monitor_speed_alert_sprint_suppression);
    RUN_TEST(test_alert_monitor_mil_and_pid_warning_alerts);
    RUN_TEST(test_trip_computer_sprint_buzzer_tones);
    RUN_TEST(test_safety_lock_touch_buzz);
    RUN_TEST(test_normal_driving_does_not_arm_and_plays_speed_alert);
    RUN_TEST(test_speed_alert_hysteresis_and_empty_slots_guard);
    RUN_TEST(test_elm327_auto_protocol_kline_timeouts);
    RUN_TEST(test_elm327_ecu_power_off_ignition_off);
    RUN_TEST(test_turbo_boost_pid_0x00_guard);
    RUN_TEST(test_scheduler_get_live_slots_and_trip_computer_nvs_protection);
    RUN_TEST(test_elm327_per_pid_3_strike_isolation);
    RUN_TEST(test_kline_bus_init_ok_and_init_sequence);
    RUN_TEST(test_obd_parser_isotp_ats0_21_22_frames_not_dropped);
    RUN_TEST(test_settings_deferred_rpm_save_and_single_brightness_save);
    RUN_TEST(test_dtc_scan_29bit_and_kline_headers);
    RUN_TEST(test_protocol_audit_kline_dtc_and_atst64_init);
    RUN_TEST(test_elm327_virtual_turbo_boost_calculation_and_command_guard_overflow);
    RUN_TEST(test_multi_ecu_dtc_and_baro_t0);
    RUN_TEST(test_universal_obd_parser_multi_ecu_and_protocol_num);
    RUN_TEST(test_elm327_atdpn_resolution_and_log_ring_buffer);
    RUN_TEST(test_universal_24pid_catalog_migration_and_800ms_timeout);
    return UNITY_END();
}

#ifdef ARDUINO
#include <Arduino.h>
void setup() {
    delay(2000);
    run_all_tests();
}
void loop() {}
#else
int main(int argc, char **argv) {
    return run_all_tests();
}
#endif
