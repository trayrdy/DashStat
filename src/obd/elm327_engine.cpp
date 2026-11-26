#include "obd/elm327_engine.h"
#include "model/settings_manager.h"
#include "config/app_config.h"
#include <cstdio>
#include <cstring>

#ifndef UNIT_TEST
#include <Arduino.h>
#endif

static const char* kInitCommands[] = {
    "ATZ",
    "ATE0",
    "ATL0",
    "ATS0",
    "ATH0",
    "ATSP0",
    "ATAT2",
    "ATST32"
};
static constexpr size_t kInitCommandCount = sizeof(kInitCommands) / sizeof(kInitCommands[0]);

static bool isKLineProto(uint8_t proto) { return (proto >= 3 && proto <= 5); }
static bool isLegacyProto(uint8_t proto) { return (proto >= 1 && proto <= 5); }

static void sanitizeText(const char* src, char* dst, size_t maxLen) {
    if (!dst || maxLen == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t j = 0;
    bool prevSpace = true;
    for (size_t i = 0; src[i] != '\0' && j + 1 < maxLen; ++i) {
        char c = src[i];
        if (c == '\r' || c == '\n' || c == '\t' || c == '>') {
            if (!prevSpace && j + 1 < maxLen) {
                dst[j++] = ' ';
                prevSpace = true;
            }
        } else if (static_cast<unsigned char>(c) >= 32) {
            dst[j++] = c;
            prevSpace = (c == ' ');
        }
    }
    while (j > 0 && dst[j - 1] == ' ') j--;
    dst[j] = '\0';
}

Elm327Engine::Elm327Engine(ITransport& transport, VehicleStateManager& stateMgr)
    : transport_(transport),
      stateMgr_(stateMgr) {
    s_instance_ = this;
    catIdxRpm_   = static_cast<int8_t>(UniversalPidCatalog::findIndexByPid(0x0C));
    catIdxBoost_ = static_cast<int8_t>(UniversalPidCatalog::findIndexByPid(0xF0));
    catIdxAtrv_  = static_cast<int8_t>(UniversalPidCatalog::findIndexByPid(0xFF));
    reset();
}

void Elm327Engine::begin() {
    reset();
}

void Elm327Engine::startDtcScan() {
    scannedDtcCount_ = 0;
    dtcScanComplete_.store(false);
    dtcScanRequested_.store(true);
}

bool Elm327Engine::isDtcScanning() const {
    return dtcScanRequested_.load() ||
           (dtcScanStep_ != DtcScanStep::IDLE && dtcScanStep_ != DtcScanStep::COMPLETE);
}

bool Elm327Engine::isDtcScanComplete() const {
    return dtcScanComplete_.load();
}

void Elm327Engine::clearDtcScanResults() {
    scannedDtcCount_ = 0;
    for (auto& d : scannedDtcs_) d = DtcCodeInfo{};
    dtcScanComplete_.store(false);
    dtcScanStep_ = DtcScanStep::IDLE;
}

void Elm327Engine::reset() {
    state_ = ElmState::DISCONNECTED;
    rxLineLen_ = 0;
    initStep_ = 0;
    waitingForPrompt_ = false;
    consecutiveTimeouts_ = 0;
    queryStartTime_ = 0;
    minRttMs_ = 0xFFFFFFFF;
    maxRttMs_ = 0;
    avgRttMs_ = 0;
    rttSum_ = 0;
    rttCount_ = 0;
    lastProfileLogTime_ = 0;
    reqRate_ = 0.0f;
    protocolDetectFailed_ = false;
    resolvedProtocol_ = 0xFF;
    firstQueryPending_ = false;
    atdpnPending_ = false;
    atdpnSent_ = false;
    awaitingAtdpn_ = false;
    cachedSettings_ = SettingsManager::instance().get();
    lastSettingsVer_ = SettingsManager::instance().version();
    std::memcpy(lastDashSlots_, cachedSettings_.dashSlots, sizeof(lastDashSlots_));
    std::memcpy(lastLiveSlots_, cachedSettings_.liveSlots, sizeof(lastLiveSlots_));
    dtcScanStep_ = DtcScanStep::IDLE;
    dtcScanRequested_.store(false);
    dtcScanComplete_.store(false);
    scannedDtcCount_ = 0;
    for (auto& d : scannedDtcs_) d = DtcCodeInfo{};
    for (auto& f : pidFailCount_) f = 0;
    nonDataStartTime_ = 0;
    ecuStandby_ = false;
    lastStandbyProbeMs_ = 0;
    lastBaroKpa_ = 101.3f;
    lastMapKpa_ = 101.3f;
    hasMapReading_ = false;
    lastTxCmd_[0] = '\0';
    stateBeforeTimeout_ = ElmState::DISCONNECTED;
    scheduler_.reset();
}

void Elm327Engine::appendLog(uint32_t nowMs, const char* tx, const char* rx) {
    ObdLogEntry entry{};
    entry.ms = nowMs;
    sanitizeText(tx, entry.tx, sizeof(entry.tx));
    sanitizeText(rx, entry.rx, sizeof(entry.rx));
    if (entry.tx[0] == '\0' && entry.rx[0] == '\0') return;

#if !defined(UNIT_TEST) && OBD_VERBOSE_LOG
    Serial.printf("[OBD] TX: %s | RX: %s\n", entry.tx, entry.rx);
#endif
#ifndef UNIT_TEST
    portENTER_CRITICAL(&logMux_);
#else
    std::lock_guard<std::mutex> lock(logMutex_);
#endif
    logEntries_[logHead_] = entry;
    logHead_ = (logHead_ + 1) % kMaxLogEntries;
    if (logCount_ < kMaxLogEntries) logCount_++;
#ifndef UNIT_TEST
    portEXIT_CRITICAL(&logMux_);
#endif
}

size_t Elm327Engine::getLogSnapshot(ObdLogEntry* outEntries, size_t maxEntries) const {
    if (!outEntries || maxEntries == 0) return 0;
#ifndef UNIT_TEST
    portENTER_CRITICAL(&logMux_);
#else
    std::lock_guard<std::mutex> lock(logMutex_);
#endif
    size_t n = (logCount_ < maxEntries) ? logCount_ : maxEntries;
    for (size_t i = 0; i < n; ++i) {
        size_t idx = (logHead_ + kMaxLogEntries - 1 - i) % kMaxLogEntries;
        outEntries[i] = logEntries_[idx];
    }
#ifndef UNIT_TEST
    portEXIT_CRITICAL(&logMux_);
#endif
    return n;
}

void Elm327Engine::clearLogs() {
#ifndef UNIT_TEST
    portENTER_CRITICAL(&logMux_);
#else
    std::lock_guard<std::mutex> lock(logMutex_);
#endif
    logHead_ = 0;
    logCount_ = 0;
#ifndef UNIT_TEST
    portEXIT_CRITICAL(&logMux_);
#endif
}

void Elm327Engine::sendCommandGuarded(const char* cmd) {
    if (!cmd || !CommandGuard::isAllowed(cmd)) {
        return; // Blocked by Rule R8
    }

    if (cmd[0] != '\r') {
        std::strncpy(lastTxCmd_, cmd, sizeof(lastTxCmd_) - 1);
        lastTxCmd_[sizeof(lastTxCmd_) - 1] = '\0';
    }

    char buf[32];
    int len = std::snprintf(buf, sizeof(buf), "%s\r", cmd);
    if (len > 0) {
        transport_.write(reinterpret_cast<const uint8_t*>(buf), static_cast<size_t>(len));
    }
}

void Elm327Engine::recordRtt(uint32_t rttMs) {
    if (rttMs < minRttMs_) minRttMs_ = rttMs;
    if (rttMs > maxRttMs_) maxRttMs_ = rttMs;
    rttSum_ += rttMs;
    rttCount_++;
    avgRttMs_ = rttSum_ / rttCount_;
}

void Elm327Engine::logProfileMetrics(uint32_t nowMs) {
    if (lastProfileLogTime_ == 0) {
        lastProfileLogTime_ = nowMs;
        return;
    }
    uint32_t elapsed = nowMs - lastProfileLogTime_;
    if (elapsed >= 3000) {
        reqRate_ = (rttCount_ * 1000.0f) / static_cast<float>(elapsed);
#if !defined(UNIT_TEST) && OBD_VERBOSE_LOG
        Serial.printf("[OBD_PROF] RTT: min=%lu avg=%lu max=%lu ms | Rate: %.1f req/s | Proto: %u\n",
                      (unsigned long)(minRttMs_ == 0xFFFFFFFF ? 0 : minRttMs_),
                      (unsigned long)avgRttMs_,
                      (unsigned long)maxRttMs_,
                      reqRate_,
                      static_cast<unsigned>(resolvedProtocol_));
#endif
        minRttMs_ = 0xFFFFFFFF;
        maxRttMs_ = 0;
        rttSum_ = 0;
        rttCount_ = 0;
        lastProfileLogTime_ = nowMs;
    }
}

void Elm327Engine::markPidUnsupported(uint8_t catIdx, bool unsupported) {
    if (catIdx >= UniversalPidCatalog::kTotalPids) return;
    bool prev = scheduler_.isCatalogUnsupported(catIdx);
    scheduler_.setCatalogUnsupported(catIdx, unsupported);

    // Mirror MAP (0x0B) availability to Virtual Turbo Boost (0xF0)
    const auto& def = UniversalPidCatalog::get(catIdx);
    if (def.pidHex == 0x0B && catIdxBoost_ >= 0) {
        scheduler_.setCatalogUnsupported(static_cast<uint8_t>(catIdxBoost_), unsupported);
    }

    if (prev != unsupported) {
        VehicleState s = stateMgr_.getSnapshot();
        s.unsupportedCatalogBits = scheduler_.getUnsupportedBits();
        stateMgr_.update(s);
    }
}

void Elm327Engine::handleStandbySilence(uint32_t nowMs) {
    if (nonDataStartTime_ == 0) {
        nonDataStartTime_ = nowMs;
    }
    uint32_t elapsed = nowMs - nonDataStartTime_;
    if (elapsed >= 6000) {
        ecuStandby_ = true;
        lastStandbyProbeMs_ = nowMs;
        VehicleState s = stateMgr_.getSnapshot();
        s.isConnected = false;
        s.isEngineRunning = false;
        s.rpm = 0;
        s.speedKmh = 0;
        stateMgr_.update(s);
    } else if (elapsed >= 2000) {
        VehicleState s = stateMgr_.getSnapshot();
        s.rpm = 0;
        s.speedKmh = 0;
        s.isEngineRunning = false;
        int catRpm = UniversalPidCatalog::findIndexByPid(0x0C);
        int catSpd = UniversalPidCatalog::findIndexByPid(0x0D);
        const uint8_t* dashSlots = scheduler_.getDashSlots();
        for (uint8_t i = 0; i < 4; ++i) {
            if (dashSlots[i] == catRpm || dashSlots[i] == catSpd) s.dashSlotValues[i] = 0.0f;
        }
        const uint8_t* liveSlots = scheduler_.getLiveSlots();
        for (uint8_t i = 0; i < 6; ++i) {
            if (liveSlots[i] == catRpm || liveSlots[i] == catSpd) s.liveSlotValues[i] = 0.0f;
        }
        stateMgr_.update(s);
    }
}

bool Elm327Engine::parseExpectedMode01Pid(uint8_t& outPid) const {
    const char* cmd = currentScheduledQuery_.command;
    if (cmd[0] != '0' || cmd[1] != '1') return false;
    if (!ObdParser::isHexChar(cmd[2]) || !ObdParser::isHexChar(cmd[3])) return false;
    outPid = ObdParser::hexToByte(cmd[2], cmd[3]);
    return true;
}

void Elm327Engine::triggerSoftBreak(uint32_t nowMs) {
    appendLog(nowMs, lastTxCmd_, "[TIMEOUT]");

    consecutiveTimeouts_++;
    stateBeforeTimeout_ = state_;
    state_ = ElmState::TIMEOUT_RECOVERY;
    waitingForPrompt_ = false;
    rxLineLen_ = 0;
    firstQueryPending_ = false;

    bool wasAwaitingAtdpn = awaitingAtdpn_;
    awaitingAtdpn_ = false;
    if (wasAwaitingAtdpn) {
        resolvedProtocol_ = 6; // Fallback if adapter fails ATDPN
        scheduler_.setProtocol(6);
    }

    scheduler_.notifyTimeout();

    if (dtcScanStep_ == DtcScanStep::WAIT_MODE_03) {
        dtcScanStep_ = DtcScanStep::SEND_MODE_07;
    } else if (dtcScanStep_ == DtcScanStep::WAIT_MODE_07) {
        dtcScanStep_ = DtcScanStep::COMPLETE;
        dtcScanComplete_.store(true);
    }

    sendCommandGuarded("\r");

    if (consecutiveTimeouts_ >= 3) {
        VehicleState s = stateMgr_.getSnapshot();
        s.isConnected = false;
        stateMgr_.update(s);
        transport_.disconnect();
        state_ = ElmState::DISCONNECTED;
        consecutiveTimeouts_ = 0;
        protocolDetectFailed_ = true;
    }
}

void Elm327Engine::handleResponse(const char* response, uint32_t nowMs) {
    uint32_t rtt = (nowMs >= queryStartTime_) ? (nowMs - queryStartTime_) : 0;
    recordRtt(rtt);
    waitingForPrompt_ = false;
    consecutiveTimeouts_ = 0;

    appendLog(nowMs, lastTxCmd_, response);

    if (awaitingAtdpn_) {
        awaitingAtdpn_ = false;
        uint8_t proto = 6;
        if (ObdParser::parseProtocolNumber(response, proto) == ParseStatus::OK) {
            resolvedProtocol_ = proto;
            scheduler_.setProtocol(proto);
        }
        return;
    }

    if (dtcScanStep_ == DtcScanStep::WAIT_MODE_03) {
        uint8_t proto = (resolvedProtocol_ != 0xFF) ? resolvedProtocol_ : cachedSettings_.selectedProtocol;
        bool hasCountPrefix = !isLegacyProto(proto);
        size_t count = 0;
        ObdParser::parseDtcResponse(response, false, scannedDtcs_, kMaxScannedDtcs, count, hasCountPrefix);
        scannedDtcCount_ = static_cast<uint8_t>(count);
        dtcScanStep_ = DtcScanStep::SEND_MODE_07;
        return;
    }
    if (dtcScanStep_ == DtcScanStep::WAIT_MODE_07) {
        uint8_t proto = (resolvedProtocol_ != 0xFF) ? resolvedProtocol_ : cachedSettings_.selectedProtocol;
        bool hasCountPrefix = !isLegacyProto(proto);
        DtcCodeInfo pending[kMaxScannedDtcs];
        size_t pCount = 0;
        ObdParser::parseDtcResponse(response, true, pending, kMaxScannedDtcs, pCount, hasCountPrefix);
        for (size_t i = 0; i < pCount && scannedDtcCount_ < kMaxScannedDtcs; ++i) {
            bool dup = false;
            for (size_t j = 0; j < scannedDtcCount_; ++j) {
                if (std::strcmp(scannedDtcs_[j].code, pending[i].code) == 0) {
                    dup = true;
                    break;
                }
            }
            if (!dup) scannedDtcs_[scannedDtcCount_++] = pending[i];
        }
        dtcScanStep_ = DtcScanStep::COMPLETE;
        dtcScanComplete_.store(true);
        return;
    }

    switch (state_) {
        case ElmState::INIT_ADAPTER:
            initStep_++;
            if (initStep_ >= kInitCommandCount) {
                uint8_t userProto = cachedSettings_.selectedProtocol;
                if (userProto != 0) {
                    resolvedProtocol_ = userProto;
                    scheduler_.setProtocol(userProto);
                } else {
                    resolvedProtocol_ = 0xFF;
                    scheduler_.setProtocol(0);
                }
                firstQueryPending_ = true;
                state_ = ElmState::POLLING;
            }
            break;

        case ElmState::POLLING: {
            scheduler_.notifyResponseReceived(currentScheduledQuery_.tier);
            firstQueryPending_ = false;

            const char* cmd = currentScheduledQuery_.command;
            bool isBusFailure = (std::strstr(response, "CAN ERROR") != nullptr) ||
                                (std::strstr(response, "UNABLE TO CONNECT") != nullptr) ||
                                (std::strstr(response, "BUS INIT") != nullptr && ObdParser::isErrorResponse(response));
            bool isNoData = ObdParser::isNoDataResponse(response);

            // 1. ATRV adapter-local voltage: NEVER resets ECU standby timer or wakes ecuStandby_
            if (cmd[0] == 'A' && cmd[1] == 'T' && cmd[2] == 'R' && cmd[3] == 'V') {
                float volt = 0.0f;
                if (ObdParser::parseBatteryVoltage(response, volt) == ParseStatus::OK) {
                    VehicleState s = stateMgr_.getSnapshot();
                    s.batteryVoltage = volt;
                    s.timestampMs = nowMs;
                    s.isConnected = !ecuStandby_;
                    if (catIdxAtrv_ >= 0) {
                        const uint8_t* liveSlots = scheduler_.getLiveSlots();
                        for (uint8_t i = 0; i < 6; ++i) {
                            if (liveSlots[i] == static_cast<uint8_t>(catIdxAtrv_)) s.liveSlotValues[i] = volt;
                        }
                        const uint8_t* dashSlots = scheduler_.getDashSlots();
                        for (uint8_t i = 0; i < 4; ++i) {
                            if (dashSlots[i] == static_cast<uint8_t>(catIdxAtrv_)) s.dashSlotValues[i] = volt;
                        }
                    }
                    stateMgr_.update(s);
                }
                return;
            }

            // 2. Standard Mode 01 PIDs
            uint8_t expectedPid = 0;
            if (!parseExpectedMode01Pid(expectedPid)) return;

            if (expectedPid == 0x0C) {
                if (isNoData || isBusFailure || ObdParser::isErrorResponse(response)) {
                    handleStandbySilence(nowMs);
                    return;
                }
                uint8_t data[8];
                size_t dataLen = 0;
                bool isNeg = false;
                if (ObdParser::parseMode01PidResponse(response, 0x0C, data, sizeof(data), dataLen, isNeg) == ParseStatus::OK &&
                    dataLen >= 2) {
                    ecuStandby_ = false;
                    nonDataStartTime_ = 0;
                    if (cachedSettings_.selectedProtocol == 0 && resolvedProtocol_ == 0xFF && !atdpnSent_) {
                        atdpnPending_ = true;
                        atdpnSent_ = true;
                    }

                    float val = (catIdxRpm_ >= 0) ? UniversalPidCatalog::decodeValue(UniversalPidCatalog::get(catIdxRpm_), data, dataLen) : 0.0f;
                    VehicleState s = stateMgr_.getSnapshot();
                    s.timestampMs = nowMs;
                    s.isConnected = true;
                    s.rpm = static_cast<uint16_t>(val);
                    s.isEngineRunning = (s.rpm > 300);

                    const uint8_t* liveSlots = scheduler_.getLiveSlots();
                    for (uint8_t i = 0; i < 6; ++i) {
                        if (catIdxRpm_ >= 0 && liveSlots[i] == static_cast<uint8_t>(catIdxRpm_)) s.liveSlotValues[i] = val;
                    }
                    const uint8_t* dashSlots = scheduler_.getDashSlots();
                    for (uint8_t i = 0; i < 4; ++i) {
                        if (catIdxRpm_ >= 0 && dashSlots[i] == static_cast<uint8_t>(catIdxRpm_)) s.dashSlotValues[i] = val;
                    }
                    stateMgr_.update(s);
                } else {
                    handleStandbySilence(nowMs);
                }
                return;
            }

            if (isBusFailure) {
                handleStandbySilence(nowMs);
                return;
            }

            uint8_t data[8];
            size_t dataLen = 0;
            bool isNeg = false;
            ParseStatus st = ObdParser::parseMode01PidResponse(response, expectedPid, data, sizeof(data), dataLen, isNeg);
            int catIdx = UniversalPidCatalog::findIndexByPid(expectedPid);

            // Auto-fallback: if '1' count-hint triggered protocol/format error (not NO DATA / 7F), disable hint
            bool usedCountHint = (std::strlen(cmd) == 5 && cmd[4] == '1');
            if (usedCountHint && !isNeg && (st == ParseStatus::ERROR_RESPONSE || st == ParseStatus::INVALID_FORMAT)) {
                scheduler_.setCanCountHintEnabled(false);
            }

            if (st == ParseStatus::OK && dataLen >= 1) {
                ecuStandby_ = false;
                nonDataStartTime_ = 0;
                if (cachedSettings_.selectedProtocol == 0 && resolvedProtocol_ == 0xFF && !atdpnSent_) {
                    atdpnPending_ = true;
                    atdpnSent_ = true;
                }

                if (expectedPid == 0x01) {
                    VehicleState s = stateMgr_.getSnapshot();
                    s.milActive = (data[0] & 0x80) != 0;
                    s.dtcCount = (data[0] & 0x7F);
                    s.timestampMs = nowMs;
                    s.isConnected = true;
                    stateMgr_.update(s);
                } else if (catIdx >= 0) {
                    pidFailCount_[catIdx] = 0;
                    if (scheduler_.isCatalogUnsupported(static_cast<uint8_t>(catIdx))) {
                        markPidUnsupported(static_cast<uint8_t>(catIdx), false);
                    }

                    const auto& def = UniversalPidCatalog::get(catIdx);
                    float val = UniversalPidCatalog::decodeValue(def, data, dataLen);
                    VehicleState s = stateMgr_.getSnapshot();
                    s.timestampMs = nowMs;
                    s.isConnected = true;

                    const uint8_t* liveSlots = scheduler_.getLiveSlots();
                    for (uint8_t i = 0; i < 6; ++i) {
                        if (liveSlots[i] == static_cast<uint8_t>(catIdx)) s.liveSlotValues[i] = val;
                    }
                    const uint8_t* dashSlots = scheduler_.getDashSlots();
                    for (uint8_t i = 0; i < 4; ++i) {
                        if (dashSlots[i] == static_cast<uint8_t>(catIdx)) s.dashSlotValues[i] = val;
                    }

                    if (expectedPid == 0x0D) s.speedKmh = static_cast<uint8_t>(val);
                    else if (expectedPid == 0x05) s.coolantTempC = static_cast<int8_t>(val);
                    else if (expectedPid == 0x0F) s.intakeAirTempC = static_cast<int8_t>(val);
                    else if (expectedPid == 0x11) s.throttlePercent = static_cast<uint8_t>(val);
                    else if (expectedPid == 0x04) s.engineLoad = static_cast<uint8_t>(val);
                    else if (expectedPid == 0x33) lastBaroKpa_ = val;
                    else if (expectedPid == 0x0B) {
                        lastMapKpa_ = val;
                        hasMapReading_ = true;
                    }

                    if ((expectedPid == 0x0B || (expectedPid == 0x33 && hasMapReading_)) && catIdxBoost_ >= 0) {
                        float boostPsi = (lastMapKpa_ - lastBaroKpa_) * 0.1450377f;
                        for (uint8_t i = 0; i < 6; ++i) {
                            if (liveSlots[i] == static_cast<uint8_t>(catIdxBoost_)) s.liveSlotValues[i] = boostPsi;
                        }
                        for (uint8_t i = 0; i < 4; ++i) {
                            if (dashSlots[i] == static_cast<uint8_t>(catIdxBoost_)) s.dashSlotValues[i] = boostPsi;
                        }
                    }
                    stateMgr_.update(s);
                }
            } else if ((st == ParseStatus::NO_DATA || isNeg) && catIdx >= 0) {
                // Per-PID 3-strike isolation: does NOT advance ECU standby timer
                if (pidFailCount_[catIdx] < 3) {
                    pidFailCount_[catIdx]++;
                    if (pidFailCount_[catIdx] >= 3) {
                        markPidUnsupported(static_cast<uint8_t>(catIdx), true);
                    }
                }
            }
            break;
        }

        default:
            break;
    }
}

void Elm327Engine::update(uint32_t nowMs) {
    if (!transport_.isConnected()) {
        if (state_ != ElmState::DISCONNECTED) {
            reset();
        }
        if (dtcScanRequested_.load() || (dtcScanStep_ != DtcScanStep::IDLE && dtcScanStep_ != DtcScanStep::COMPLETE)) {
            dtcScanComplete_.store(true);
            dtcScanStep_ = DtcScanStep::COMPLETE;
            dtcScanRequested_.store(false);
        }
        return;
    }

    uint32_t curVer = SettingsManager::instance().version();
    if (curVer != lastSettingsVer_) {
        cachedSettings_ = SettingsManager::instance().get();
        lastSettingsVer_ = curVer;
    }
    const AppSettings& settings = cachedSettings_;
    uint8_t currentProto = settings.selectedProtocol;
    if (activeProtocol_ == 0xFF) {
        activeProtocol_ = currentProto;
    } else if (currentProto != activeProtocol_) {
        activeProtocol_ = currentProto;
        if (state_ != ElmState::DISCONNECTED) {
            reset();
            return;
        }
    }

    // Clear stale UI slot values immediately whenever user swaps a slot assignment
    bool slotChanged = false;
    for (uint8_t i = 0; i < 4; ++i) {
        if (settings.dashSlots[i] != lastDashSlots_[i]) {
            slotChanged = true;
            break;
        }
    }
    for (uint8_t i = 0; i < 6 && !slotChanged; ++i) {
        if (settings.liveSlots[i] != lastLiveSlots_[i]) {
            slotChanged = true;
        }
    }
    if (slotChanged) {
        VehicleState slotState = stateMgr_.getSnapshot();
        for (uint8_t i = 0; i < 4; ++i) {
            if (settings.dashSlots[i] != lastDashSlots_[i]) {
                lastDashSlots_[i] = settings.dashSlots[i];
                slotState.dashSlotValues[i] = 0.0f;
                if (lastDashSlots_[i] < UniversalPidCatalog::kTotalPids) {
                    pidFailCount_[lastDashSlots_[i]] = 0;
                    scheduler_.setCatalogUnsupported(lastDashSlots_[i], false);
                }
            }
        }
        for (uint8_t i = 0; i < 6; ++i) {
            if (settings.liveSlots[i] != lastLiveSlots_[i]) {
                lastLiveSlots_[i] = settings.liveSlots[i];
                slotState.liveSlotValues[i] = 0.0f;
                if (lastLiveSlots_[i] < UniversalPidCatalog::kTotalPids) {
                    pidFailCount_[lastLiveSlots_[i]] = 0;
                    scheduler_.setCatalogUnsupported(lastLiveSlots_[i], false);
                }
            }
        }
        slotState.unsupportedCatalogBits = scheduler_.getUnsupportedBits();
        stateMgr_.update(slotState);
    }

    if (state_ == ElmState::DISCONNECTED) {
        state_ = ElmState::INIT_ADAPTER;
        initStep_ = 0;
        waitingForPrompt_ = false;
        consecutiveTimeouts_ = 0;
        rxLineLen_ = 0;
    }

    // 1. Read incoming stream until '>'
    while (transport_.available() > 0) {
        uint8_t byte = 0;
        if (transport_.read(&byte, 1) == 1) {
            if (rxLineLen_ < sizeof(rxLineBuf_) - 1) {
                rxLineBuf_[rxLineLen_++] = static_cast<char>(byte);
            }
            if (byte == '>') {
                rxLineBuf_[rxLineLen_] = '\0';
                handleResponse(rxLineBuf_, nowMs);
                rxLineLen_ = 0;
                break;
            }
        }
    }

    // 2. Soft break timeout enforcement
    if (waitingForPrompt_) {
        uint8_t proto = (resolvedProtocol_ != 0xFF) ? resolvedProtocol_ : currentProto;
        uint32_t timeoutLimit = 350;
        if (state_ == ElmState::INIT_ADAPTER) {
            if (initStep_ == 0) timeoutLimit = 2000;
            else if (isKLineProto(currentProto)) timeoutLimit = 2500;
        } else if (awaitingAtdpn_) {
            timeoutLimit = 500;
        } else if (dtcScanStep_ == DtcScanStep::WAIT_MODE_03 || dtcScanStep_ == DtcScanStep::WAIT_MODE_07) {
            timeoutLimit = isKLineProto(proto) ? 2000 : 1500;
        } else if (firstQueryPending_ || proto == 0) {
            timeoutLimit = (proto == 0 || isKLineProto(proto)) ? 6000 : 1500;
        } else if (state_ == ElmState::POLLING) {
            if (isLegacyProto(proto)) timeoutLimit = 1500;
            else timeoutLimit = 800;
        }
        if (nowMs - queryStartTime_ >= timeoutLimit) {
            triggerSoftBreak(nowMs);
            return;
        }
    }

    // 3. DTC scan request (adapter stays on default OBD broadcast header, so send 03 -> 07 directly)
    if (dtcScanRequested_.load() && !waitingForPrompt_ && state_ == ElmState::POLLING && !atdpnPending_) {
        dtcScanRequested_.store(false);
        dtcScanComplete_.store(false);
        scannedDtcCount_ = 0;
        for (auto& d : scannedDtcs_) d = DtcCodeInfo{};
        dtcScanStep_ = DtcScanStep::SEND_MODE_03;
    }

    if (dtcScanStep_ == DtcScanStep::SEND_MODE_03 && !waitingForPrompt_) {
        sendCommandGuarded("03");
        waitingForPrompt_ = true;
        queryStartTime_ = nowMs;
        dtcScanStep_ = DtcScanStep::WAIT_MODE_03;
        logProfileMetrics(nowMs);
        return;
    }
    if (dtcScanStep_ == DtcScanStep::SEND_MODE_07 && !waitingForPrompt_) {
        sendCommandGuarded("07");
        waitingForPrompt_ = true;
        queryStartTime_ = nowMs;
        dtcScanStep_ = DtcScanStep::WAIT_MODE_07;
        logProfileMetrics(nowMs);
        return;
    }
    if (dtcScanStep_ == DtcScanStep::WAIT_MODE_03 ||
        dtcScanStep_ == DtcScanStep::WAIT_MODE_07) {
        logProfileMetrics(nowMs);
        return;
    }

    // 4. Main State Machine Dispatch
    if (!waitingForPrompt_) {
        switch (state_) {
            case ElmState::INIT_ADAPTER:
                if (initStep_ < kInitCommandCount) {
                    if (initStep_ == 5) {
                        char protoCmd[12];
                        std::snprintf(protoCmd, sizeof(protoCmd), "ATSP%X", currentProto);
                        sendCommandGuarded(protoCmd);
                    } else if (initStep_ == 7 && (currentProto == 0 || isLegacyProto(currentProto))) {
                        sendCommandGuarded("ATST64");
                    } else {
                        sendCommandGuarded(kInitCommands[initStep_]);
                    }
                    waitingForPrompt_ = true;
                    queryStartTime_ = nowMs;
                } else {
                    uint8_t userProto = currentProto;
                    if (userProto != 0) {
                        resolvedProtocol_ = userProto;
                        scheduler_.setProtocol(userProto);
                    } else {
                        resolvedProtocol_ = 0xFF;
                        scheduler_.setProtocol(0);
                    }
                    firstQueryPending_ = true;
                    state_ = ElmState::POLLING;
                }
                break;

            case ElmState::POLLING:
                if (atdpnPending_) {
                    atdpnPending_ = false;
                    awaitingAtdpn_ = true;
                    sendCommandGuarded("ATDPN");
                    waitingForPrompt_ = true;
                    queryStartTime_ = nowMs;
                } else if (ecuStandby_) {
                    if (nowMs - lastStandbyProbeMs_ >= 1500) {
                        lastStandbyProbeMs_ = nowMs;
                        currentScheduledQuery_.action = QueryAction::SEND_COMMAND;
                        std::strcpy(currentScheduledQuery_.command, "010C");
                        currentScheduledQuery_.tier = ScheduledTier::TIER_1_REALTIME;
                        sendCommandGuarded(currentScheduledQuery_.command);
                        waitingForPrompt_ = true;
                        queryStartTime_ = nowMs;
                    }
                } else if (scheduler_.getNextQuery(nowMs, currentScheduledQuery_)) {
                    sendCommandGuarded(currentScheduledQuery_.command);
                    waitingForPrompt_ = true;
                    queryStartTime_ = nowMs;
                }
                break;

            case ElmState::TIMEOUT_RECOVERY:
                if (stateBeforeTimeout_ == ElmState::INIT_ADAPTER) {
                    initStep_++;
                    if (initStep_ < kInitCommandCount) {
                        state_ = ElmState::INIT_ADAPTER;
                    } else {
                        if (currentProto != 0) {
                            resolvedProtocol_ = currentProto;
                            scheduler_.setProtocol(currentProto);
                        }
                        firstQueryPending_ = true;
                        state_ = ElmState::POLLING;
                    }
                } else {
                    state_ = ElmState::POLLING;
                }
                break;

            default:
                break;
        }
    }

    logProfileMetrics(nowMs);
}
