#include "obd/pid_scheduler.h"
#include "obd/universal_pid_catalog.h"
#include <cstdio>

PidScheduler::PidScheduler() {
    idxSpd_  = static_cast<int8_t>(UniversalPidCatalog::findIndexByPid(0x0D));
    idxThr_  = static_cast<int8_t>(UniversalPidCatalog::findIndexByPid(0x11));
    idxClt_  = static_cast<int8_t>(UniversalPidCatalog::findIndexByPid(0x05));
    idxBaro_ = static_cast<int8_t>(UniversalPidCatalog::findIndexByPid(0x33));
    reset();
}

void PidScheduler::reset() {
    protocol_ = 0;
    unsupportedBits_ = 0;
    hasResponded_ = false;
    useCanCountHint_ = true;
    lastTier1Time_ = 0;
    lastTier2Time_ = 0;
    lastTier3Time_ = 0;
    lastMilCheckMs_ = 0;
    lastBaroPollMs_ = 0;
    hasBaroPolled_ = false;
    lastLiveRpmHeartbeatMs_ = 0;
    lastLiveVoltHeartbeatMs_ = 0;
    tier1Seq_ = 0;
    tripSeq_ = 0;
    activePage_ = 0;
    boostedSlot_ = -1;
    const uint8_t defSlots[6] = { 0, 1, 6, 4, 7, 18 };
    std::memcpy(liveSlots_, defSlots, sizeof(defSlots));
    const uint8_t defDashSlots[4] = { 6, 16, 11, 7 };
    std::memcpy(dashSlots_, defDashSlots, sizeof(dashSlots_));
    for (uint8_t i = 0; i < 6; ++i) {
        slotLastPollMs_[i] = 0;
        slotHasPolled_[i] = false;
    }
    liveSlotSeq_ = 0;
    dashSlotSeq_ = 0;
}

void PidScheduler::setProtocol(uint8_t proto) {
    protocol_ = proto;
}

void PidScheduler::setCatalogUnsupported(uint8_t catIdx, bool unsupported) {
    if (catIdx >= 64) return;
    if (unsupported) {
        unsupportedBits_ |= (1ULL << catIdx);
    } else {
        unsupportedBits_ &= ~(1ULL << catIdx);
    }
}

bool PidScheduler::isCatalogUnsupported(uint8_t catIdx) const {
    if (catIdx >= 64) return false;
    return (unsupportedBits_ & (1ULL << catIdx)) != 0;
}

void PidScheduler::setActivePage(uint8_t pageId) { activePage_ = pageId; }
void PidScheduler::setBoostedSlot(int8_t slotIdx) { boostedSlot_ = slotIdx; }

void PidScheduler::setLiveSlots(const uint8_t slots[6]) {
    if (!slots) return;
    for (uint8_t i = 0; i < 6; ++i) {
        if (liveSlots_[i] != slots[i]) {
            liveSlots_[i] = slots[i];
            slotHasPolled_[i] = false;
            slotLastPollMs_[i] = 0;
        }
    }
}

void PidScheduler::setDashSlots(const uint8_t slots[4]) {
    if (slots) std::memcpy(dashSlots_, slots, sizeof(dashSlots_));
}

void PidScheduler::notifyResponseReceived(ScheduledTier tier) {
    (void)tier;
    hasResponded_ = true;
}

void PidScheduler::notifyTimeout() {
}

void PidScheduler::formatMode01Cmd(char* dst, size_t dstSize, uint8_t pidHex, bool allowCountHint) const {
    if (allowCountHint && useCanCountHint_ && hasResponded_ && protocol_ >= 6 && protocol_ != 0xFF) {
        std::snprintf(dst, dstSize, "01%02X1", pidHex);
    } else {
        std::snprintf(dst, dstSize, "01%02X", pidHex);
    }
}

bool PidScheduler::buildBoostQuery(uint32_t nowMs, ScheduledQuery& outQuery) {
    bool baroAllowed = (idxBaro_ < 0) || !isCatalogUnsupported(static_cast<uint8_t>(idxBaro_));
    if ((!hasBaroPolled_ || (nowMs - lastBaroPollMs_ >= 15000)) && baroAllowed) {
        formatMode01Cmd(outQuery.command, sizeof(outQuery.command), 0x33);
        lastBaroPollMs_ = nowMs;
        hasBaroPolled_ = true;
        return true;
    }
    formatMode01Cmd(outQuery.command, sizeof(outQuery.command), 0x0B);
    return true;
}

bool PidScheduler::getNextQuery(uint32_t nowMs, ScheduledQuery& outQuery) {
    // 1. Live Stat Page (pageId == 1): poll configured 6 slots directly + heartbeats
    if (activePage_ == 1) {
        bool hasRpmSlot = false;
        bool hasAtrvSlot = false;
        for (uint8_t i = 0; i < 6; ++i) {
            uint8_t catIdx = liveSlots_[i];
            if (catIdx >= UniversalPidCatalog::kTotalPids) continue;
            const auto& d = UniversalPidCatalog::get(catIdx);
            if (d.pidHex == 0x0C) hasRpmSlot = true;
            else if (d.formula == FormulaType::SPECIAL_ATRV) hasAtrvSlot = true;
        }

        for (uint8_t i = 0; i < 6; ++i) {
            uint8_t slotIdx = (liveSlotSeq_ + i) % 6;
            uint8_t catIdx = liveSlots_[slotIdx];
            if (catIdx >= UniversalPidCatalog::kTotalPids) continue;
            const auto& def = UniversalPidCatalog::get(catIdx);

            if (def.pidHex == 0x00) continue;
            if (def.formula != FormulaType::SPECIAL_ATRV && isCatalogUnsupported(catIdx)) continue;

            uint32_t interval = def.intervalMs;
            if (boostedSlot_ >= 0) {
                if (boostedSlot_ == static_cast<int8_t>(slotIdx)) {
                    interval = 100; // Full 10 Hz chart priority when viewing single sensor
                } else if (interval < 2000) {
                    interval = 2000; // Throttle hidden grid cards while chart is open
                }
            }

            if (!slotHasPolled_[slotIdx] || (nowMs - slotLastPollMs_[slotIdx] >= interval)) {
                outQuery.action = QueryAction::SEND_COMMAND;
                if (def.formula == FormulaType::SPECIAL_ATRV) {
                    std::strcpy(outQuery.command, "ATRV");
                } else if (def.pidHex == 0xF0) {
                    buildBoostQuery(nowMs, outQuery);
                } else {
                    formatMode01Cmd(outQuery.command, sizeof(outQuery.command), def.pidHex);
                }
                outQuery.tier = (interval <= 200) ? ScheduledTier::TIER_1_REALTIME : ScheduledTier::TIER_2_THERMAL;
                slotHasPolled_[slotIdx] = true;
                slotLastPollMs_[slotIdx] = nowMs;
                liveSlotSeq_ = (slotIdx + 1) % 6;
                return true;
            }
        }

        // Low-rate heartbeat keeps ECU standby detection & voltage alive on LivePage
        if (!hasRpmSlot && (nowMs - lastLiveRpmHeartbeatMs_ >= 1500)) {
            outQuery.action = QueryAction::SEND_COMMAND;
            formatMode01Cmd(outQuery.command, sizeof(outQuery.command), 0x0C);
            outQuery.tier = ScheduledTier::TIER_1_REALTIME;
            lastLiveRpmHeartbeatMs_ = nowMs;
            return true;
        }
        if (!hasAtrvSlot && (nowMs - lastLiveVoltHeartbeatMs_ >= 2000)) {
            outQuery.action = QueryAction::SEND_COMMAND;
            std::strcpy(outQuery.command, "ATRV");
            outQuery.tier = ScheduledTier::TIER_3_ENV;
            lastLiveVoltHeartbeatMs_ = nowMs;
            return true;
        }
        return false;
    }

    // 2. Trip & Record Page (pageId == 2): Speed + RPM + MIL
    if (activePage_ == 2) {
        if (nowMs - lastMilCheckMs_ >= 3000) {
            outQuery.action = QueryAction::SEND_COMMAND;
            formatMode01Cmd(outQuery.command, sizeof(outQuery.command), 0x01, false);
            outQuery.tier = ScheduledTier::TIER_3_ENV;
            lastMilCheckMs_ = nowMs;
            return true;
        }

        if (tripSeq_ != 0 || (nowMs - lastTier1Time_ >= TIER_1_INTERVAL_MS)) {
            bool spdAllowed = (idxSpd_ < 0) || !isCatalogUnsupported(static_cast<uint8_t>(idxSpd_));
            outQuery.action = QueryAction::SEND_COMMAND;
            if (spdAllowed && tripSeq_ < 4) {
                formatMode01Cmd(outQuery.command, sizeof(outQuery.command), 0x0D);
                tripSeq_++;
            } else {
                formatMode01Cmd(outQuery.command, sizeof(outQuery.command), 0x0C);
                tripSeq_ = 0;
            }
            outQuery.tier = ScheduledTier::TIER_1_REALTIME;
            lastTier1Time_ = nowMs;
            return true;
        }
        return false;
    }

    // 3. Dashboard Page (pageId == 0)
    // Tier 2 (1000ms): rotate dashSlots_ with 0105 fallback
    if (nowMs - lastTier2Time_ >= TIER_2_INTERVAL_MS) {
        for (uint8_t i = 0; i < 4; ++i) {
            uint8_t idx = (dashSlotSeq_ + i) % 4;
            uint8_t catIdx = dashSlots_[idx];
            if (catIdx >= UniversalPidCatalog::kTotalPids) continue;
            const auto& def = UniversalPidCatalog::get(catIdx);
            if (def.pidHex == 0x00 || def.formula == FormulaType::SPECIAL_ATRV ||
                def.pidHex == 0x0C || def.pidHex == 0x0D || def.pidHex == 0x11) {
                continue;
            }
            if (isCatalogUnsupported(catIdx)) continue;

            outQuery.action = QueryAction::SEND_COMMAND;
            if (def.pidHex == 0xF0) {
                buildBoostQuery(nowMs, outQuery);
            } else {
                formatMode01Cmd(outQuery.command, sizeof(outQuery.command), def.pidHex);
            }
            outQuery.tier = ScheduledTier::TIER_2_THERMAL;
            lastTier2Time_ = nowMs;
            dashSlotSeq_ = (idx + 1) % 4;
            return true;
        }

        if (idxClt_ < 0 || !isCatalogUnsupported(static_cast<uint8_t>(idxClt_))) {
            outQuery.action = QueryAction::SEND_COMMAND;
            formatMode01Cmd(outQuery.command, sizeof(outQuery.command), 0x05);
            outQuery.tier = ScheduledTier::TIER_2_THERMAL;
            lastTier2Time_ = nowMs;
            return true;
        }
        lastTier2Time_ = nowMs;
    }

    // Tier 3 (2000ms): ATRV adapter voltage only
    if (nowMs - lastTier3Time_ >= TIER_3_INTERVAL_MS) {
        outQuery.action = QueryAction::SEND_COMMAND;
        std::strcpy(outQuery.command, "ATRV");
        outQuery.tier = ScheduledTier::TIER_3_ENV;
        lastTier3Time_ = nowMs;
        return true;
    }

    // Tier 1 (~30ms): Single-PID RPM -> Speed -> Throttle cycle
    if (tier1Seq_ != 0 || (nowMs - lastTier1Time_ >= TIER_1_INTERVAL_MS)) {
        bool spdAllowed = (idxSpd_ < 0) || !isCatalogUnsupported(static_cast<uint8_t>(idxSpd_));
        bool thrAllowed = (idxThr_ < 0) || !isCatalogUnsupported(static_cast<uint8_t>(idxThr_));

        outQuery.action = QueryAction::SEND_COMMAND;
        outQuery.tier = ScheduledTier::TIER_1_REALTIME;
        lastTier1Time_ = nowMs;

        if (tier1Seq_ == 0) {
            formatMode01Cmd(outQuery.command, sizeof(outQuery.command), 0x0C);
            tier1Seq_ = spdAllowed ? 1 : (thrAllowed ? 2 : 0);
            return true;
        } else if (tier1Seq_ == 1 && spdAllowed) {
            formatMode01Cmd(outQuery.command, sizeof(outQuery.command), 0x0D);
            tier1Seq_ = thrAllowed ? 2 : 0;
            return true;
        } else if (thrAllowed) {
            formatMode01Cmd(outQuery.command, sizeof(outQuery.command), 0x11);
            tier1Seq_ = 0;
            return true;
        } else {
            formatMode01Cmd(outQuery.command, sizeof(outQuery.command), 0x0C);
            tier1Seq_ = 0;
            return true;
        }
    }

    return false;
}
