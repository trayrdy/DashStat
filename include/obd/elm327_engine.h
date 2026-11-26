#pragma once

#include <cstdint>
#include <cstddef>
#include <atomic>
#ifndef UNIT_TEST
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#else
#include <mutex>
#endif
#include "transport/transport_interface.h"
#include "model/vehicle_state.h"
#include "obd/command_guard.h"
#include "obd/pid_scheduler.h"
#include "obd/obd_parser.h"
#include "obd/universal_pid_catalog.h"
#include "model/dtc_database.h"
#include "model/settings.h"

struct ObdLogEntry {
    uint32_t ms{0};
    char tx[20]{0};
    char rx[64]{0};
};

enum class ElmState : uint8_t {
    DISCONNECTED = 0,
    INIT_ADAPTER,
    POLLING,
    TIMEOUT_RECOVERY
};

class Elm327Engine {
public:
    static constexpr size_t kMaxLogEntries = 12;

    Elm327Engine(ITransport& transport, VehicleStateManager& stateMgr);

    void begin();
    void update(uint32_t nowMs);
    void reset();

    ElmState getState() const { return state_; }
    bool isConnected() const { return transport_.isConnected(); }
    bool isEcuStandby() const { return ecuStandby_; }
    uint8_t getResolvedProtocol() const { return resolvedProtocol_; }

    uint32_t getAvgRttMs() const { return avgRttMs_; }
    float getRequestRate() const { return reqRate_; }

    PidScheduler& getScheduler() { return scheduler_; }

    bool isProtocolDetectFailed() const { return protocolDetectFailed_; }
    void clearProtocolDetectFailed() { protocolDetectFailed_ = false; }

    static Elm327Engine* instance() { return s_instance_; }

    void startDtcScan();
    bool isDtcScanning() const;
    bool isDtcScanComplete() const;
    uint8_t getScannedDtcCount() const { return scannedDtcCount_; }
    const DtcCodeInfo* getScannedDtcs() const { return scannedDtcs_; }
    void clearDtcScanResults();

    size_t getLogSnapshot(ObdLogEntry* outEntries, size_t maxEntries) const;
    void clearLogs();

private:
    static inline Elm327Engine* s_instance_{nullptr};
    ITransport& transport_;
    VehicleStateManager& stateMgr_;

    ElmState state_{ElmState::DISCONNECTED};
    bool protocolDetectFailed_{false};
    uint8_t activeProtocol_{0xFF};
    uint8_t resolvedProtocol_{0xFF};
    bool firstQueryPending_{false};
    bool atdpnPending_{false};
    bool atdpnSent_{false};
    bool awaitingAtdpn_{false};
    PidScheduler scheduler_;

    char rxLineBuf_[512]{0};
    size_t rxLineLen_{0};

    uint8_t initStep_{0};
    uint32_t queryStartTime_{0};
    bool waitingForPrompt_{false};
    uint8_t consecutiveTimeouts_{0};
    ElmState stateBeforeTimeout_{ElmState::DISCONNECTED};
    uint32_t nonDataStartTime_{0};
    bool ecuStandby_{false};
    uint32_t lastStandbyProbeMs_{0};

    uint8_t pidFailCount_[UniversalPidCatalog::kTotalPids]{0};
    uint8_t lastDashSlots_[4]{ 6, 16, 11, 7 };
    uint8_t lastLiveSlots_[6]{ 0, 1, 6, 4, 7, 18 };
    int8_t catIdxRpm_{-1};
    int8_t catIdxBoost_{-1};
    int8_t catIdxAtrv_{-1};
    AppSettings cachedSettings_{};
    uint32_t lastSettingsVer_{0};

    float lastBaroKpa_{101.3f};
    float lastMapKpa_{101.3f};
    bool hasMapReading_{false};

    ScheduledQuery currentScheduledQuery_{};

    // Profiler metrics
    uint32_t minRttMs_{0xFFFFFFFF};
    uint32_t maxRttMs_{0};
    uint32_t avgRttMs_{0};
    uint32_t rttSum_{0};
    uint32_t rttCount_{0};
    uint32_t lastProfileLogTime_{0};
    float reqRate_{0.0f};

    // Ring buffer of latest TX -> RX exchanges for in-device OBD LOG
    ObdLogEntry logEntries_[kMaxLogEntries]{};
    uint8_t logHead_{0};
    uint8_t logCount_{0};
    char lastTxCmd_[20]{0};
#ifndef UNIT_TEST
    mutable portMUX_TYPE logMux_ = portMUX_INITIALIZER_UNLOCKED;
#else
    mutable std::mutex logMutex_;
#endif

    enum class DtcScanStep : uint8_t {
        IDLE = 0,
        SEND_MODE_03,
        WAIT_MODE_03,
        SEND_MODE_07,
        WAIT_MODE_07,
        COMPLETE
    };
    DtcScanStep dtcScanStep_{DtcScanStep::IDLE};
    std::atomic<bool> dtcScanRequested_{false};
    std::atomic<bool> dtcScanComplete_{false};
    static constexpr size_t kMaxScannedDtcs = 10;
    DtcCodeInfo scannedDtcs_[kMaxScannedDtcs]{};
    uint8_t scannedDtcCount_{0};

    void sendCommandGuarded(const char* cmd);
    void handleResponse(const char* response, uint32_t nowMs);
    void recordRtt(uint32_t rttMs);
    void logProfileMetrics(uint32_t nowMs);
    void triggerSoftBreak(uint32_t nowMs);
    void appendLog(uint32_t nowMs, const char* tx, const char* rx);
    void markPidUnsupported(uint8_t catIdx, bool unsupported);
    void handleStandbySilence(uint32_t nowMs);
    bool parseExpectedMode01Pid(uint8_t& outPid) const;
};
