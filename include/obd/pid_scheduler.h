#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>

enum class ScheduledTier : uint8_t {
    NONE = 0,
    TIER_1_REALTIME,
    TIER_2_THERMAL,
    TIER_3_ENV
};

enum class QueryAction : uint8_t {
    SEND_COMMAND = 0,
    SWITCH_HEADER,
    RESTORE_HEADER
};

struct ScheduledQuery {
    QueryAction action{QueryAction::SEND_COMMAND};
    char command[16]{0};
    ScheduledTier tier{ScheduledTier::NONE};
};

class PidScheduler {
public:
    PidScheduler();

    void setProtocol(uint8_t proto);
    uint8_t getProtocol() const { return protocol_; }
    bool isLegacyBus() const { return (protocol_ >= 1 && protocol_ <= 5); }
    bool isCan29Bus() const { return (protocol_ == 7 || protocol_ == 9 || protocol_ == 12); }
    const char* defaultHeader() const { return isCan29Bus() ? "18DB33F1" : "7DF"; }

    void setCatalogUnsupported(uint8_t catIdx, bool unsupported);
    bool isCatalogUnsupported(uint8_t catIdx) const;
    uint64_t getUnsupportedBits() const { return unsupportedBits_; }
    void clearUnsupportedBits() { unsupportedBits_ = 0; }

    void setActivePage(uint8_t pageId);
    void setBoostedSlot(int8_t slotIdx);
    void setLiveSlots(const uint8_t slots[6]);
    void setDashSlots(const uint8_t slots[4]);
    const uint8_t* getDashSlots() const { return dashSlots_; }
    const uint8_t* getLiveSlots() const { return liveSlots_; }

    bool getNextQuery(uint32_t nowMs, ScheduledQuery& outQuery);
    void notifyResponseReceived(ScheduledTier tier);
    void notifyTimeout();
    void reset();

    bool isHeaderDefault() const { return true; }
    void forceHeaderDefault() {}
    void setCanCountHintEnabled(bool en) { useCanCountHint_ = en; }
    bool isCanCountHintEnabled() const { return useCanCountHint_; }

private:
    uint8_t protocol_{0};
    uint64_t unsupportedBits_{0};
    bool hasResponded_{false};
    bool useCanCountHint_{true};

    static constexpr uint32_t TIER_1_INTERVAL_MS = 0;
    static constexpr uint32_t TIER_2_INTERVAL_MS = 1000;
    static constexpr uint32_t TIER_3_INTERVAL_MS = 2000;

    uint32_t lastTier1Time_{0};
    uint32_t lastTier2Time_{0};
    uint32_t lastTier3Time_{0};
    uint32_t lastMilCheckMs_{0};
    uint32_t lastBaroPollMs_{0};
    bool hasBaroPolled_{false};
    uint32_t lastLiveRpmHeartbeatMs_{0};
    uint32_t lastLiveVoltHeartbeatMs_{0};

    uint8_t tier1Seq_{0};
    uint8_t tripSeq_{0};

    uint8_t activePage_{0};
    int8_t boostedSlot_{-1};
    uint8_t liveSlots_[6]{ 0, 1, 6, 4, 7, 18 };
    uint8_t dashSlots_[4]{ 6, 16, 11, 7 };
    uint32_t slotLastPollMs_[6]{ 0, 0, 0, 0, 0, 0 };
    bool slotHasPolled_[6]{ false, false, false, false, false, false };
    uint8_t liveSlotSeq_{0};
    uint8_t dashSlotSeq_{0};
    int8_t idxSpd_{-1};
    int8_t idxThr_{-1};
    int8_t idxClt_{-1};
    int8_t idxBaro_{-1};

    void formatMode01Cmd(char* dst, size_t dstSize, uint8_t pidHex, bool allowCountHint = true) const;
    bool buildBoostQuery(uint32_t nowMs, ScheduledQuery& outQuery);
};
