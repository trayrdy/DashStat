#pragma once
#include <stdint.h>
#include "model/vehicle_state.h"
#include "model/dtc_database.h"

namespace ui {

enum class DiagState : uint8_t {
    IDLE = 0,         ///< Ready to scan, shows vehicle status & "START SCAN" button
    SCANNING,         ///< Scan in progress (reading Mode 03/07/0A)
    NO_FAULT,         ///< Zero DTCs detected ("SYSTEM HEALTH OK")
    FAULT_LIST,       ///< List of detected DTCs with severity
    FAULT_DETAIL      ///< Deep detail on selected DTC (freeze frame & description)
};

using DtcCodeInfo = ::DtcCodeInfo;

struct DtcPagination {
    static constexpr uint8_t kMaxDtcCodes = 10;
    static constexpr uint8_t kDtcPerPage = 3;

    static uint8_t calculateTotalPages(uint8_t totalDtc) {
        if (totalDtc == 0) return 1;
        uint8_t clamped = totalDtc > kMaxDtcCodes ? kMaxDtcCodes : totalDtc;
        return (clamped + kDtcPerPage - 1) / kDtcPerPage;
    }

    static uint8_t getStartIndex(uint8_t page) {
        return page * kDtcPerPage;
    }

    static uint8_t getItemsOnPage(uint8_t page, uint8_t totalDtc) {
        uint8_t clamped = totalDtc > kMaxDtcCodes ? kMaxDtcCodes : totalDtc;
        uint8_t start = page * kDtcPerPage;
        if (start >= clamped) return 0;
        uint8_t rem = clamped - start;
        return rem > kDtcPerPage ? kDtcPerPage : rem;
    }
};

}  // namespace ui

#ifndef UNIT_TEST
#include <lvgl.h>
#include "ui/pages/page.h"

namespace ui {

class DiagnosticsPage : public Page {
public:
    void create(lv_obj_t* parent) override;
    void destroy();
    const char* title() const override { return "DIAGNOSTICS"; }
    void onShow() override;
    void onHide() override;
    bool onBack() override;
    void update(const VehicleState& state, uint32_t nowMs) override;
    void onLayout(int32_t contentW) override;
    void applyTheme() override;

    DiagState state() const { return state_; }
    void setState(DiagState s);

private:
    static void onScanClicked(lv_event_t* e);
    static void onDetailBackClicked(lv_event_t* e);
    static void onRowClicked(lv_event_t* e);
    static void onPagePrevClicked(lv_event_t* e);
    static void onPageNextClicked(lv_event_t* e);

    void renderCurrentState();
    void renderFaultList();
    void updatePaginationControls();
    void startScan();

    DiagState state_ = DiagState::IDLE;
    uint32_t scanStartMs_ = 0;
    uint8_t selectedDtc_ = 0;
    uint8_t currentPage_ = 0;
    int32_t currentContentW_ = 264;
    DtcCodeInfo scannedDtcs_[DtcPagination::kMaxDtcCodes]{};
    uint8_t scannedDtcCount_ = 0;

    // View 1: IDLE / Ready to scan
    lv_obj_t* idleContainer_ = nullptr;
    lv_obj_t* idleCard_ = nullptr;
    lv_obj_t* idleStatusLbl_ = nullptr;
    lv_obj_t* idleScanBtn_ = nullptr;
    lv_obj_t* idleChip_ = nullptr;

    // View 2: SCANNING
    lv_obj_t* scanContainer_ = nullptr;
    lv_obj_t* scanCard_ = nullptr;
    lv_obj_t* scanBar_ = nullptr;
    lv_obj_t* scanLbl_ = nullptr;

    // View 3: NO_FAULT
    lv_obj_t* noFaultContainer_ = nullptr;
    lv_obj_t* noFaultCard_ = nullptr;
    lv_obj_t* noFaultBtn_ = nullptr;

    // View 4: FAULT_LIST
    lv_obj_t* listContainer_ = nullptr;
    lv_obj_t* listHeader_ = nullptr;
    lv_obj_t* listHeaderTitle_ = nullptr;
    lv_obj_t* listRows_[3]{ nullptr };
    lv_obj_t* listIcons_[3]{ nullptr };
    lv_obj_t* listCodes_[3]{ nullptr };
    lv_obj_t* listDescs_[3]{ nullptr };
    lv_obj_t* listChips_[3]{ nullptr };
    lv_obj_t* listChipLabels_[3]{ nullptr };
    lv_obj_t* listFooter_ = nullptr;
    lv_obj_t* pagePrevBtn_ = nullptr;
    lv_obj_t* pageLbl_ = nullptr;
    lv_obj_t* pageNextBtn_ = nullptr;
    lv_obj_t* listRescanBtn_ = nullptr;

    // View 5: FAULT_DETAIL
    lv_obj_t* detailContainer_ = nullptr;
    lv_obj_t* detailCard_ = nullptr;
    lv_obj_t* detailCodeLbl_ = nullptr;
    lv_obj_t* detailDescLbl_ = nullptr;
    lv_obj_t* detailFrameLbl_ = nullptr;
    lv_obj_t* detailBackBtn_ = nullptr;
};

}  // namespace ui
#endif  // !UNIT_TEST
