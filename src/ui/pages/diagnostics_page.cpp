#ifndef UNIT_TEST
#include "ui/pages/diagnostics_page.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "hal/buzzer_controller.h"
#include "model/settings_manager.h"
#include "obd/elm327_engine.h"
#include "obd/mock_obd.h"
#include "obd/obd_parser.h"
#include <Arduino.h>
#include <cstdio>
#include <cstring>

namespace ui {

void DiagnosticsPage::create(lv_obj_t* parent) {
    if (root_ != nullptr) return;
    state_ = DiagState::IDLE;
    currentPage_ = 0;

    root_ = box(parent);
    lv_obj_set_pos(root_, 0, 0);
    lv_obj_set_size(root_, layout::kContentW, layout::kContentH);
    lv_obj_set_style_bg_color(root_, color::bg(), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    // =========================================================================
    // 1. IDLE VIEW
    // =========================================================================
    idleContainer_ = box(root_);
    lv_obj_set_size(idleContainer_, layout::kContentW, layout::kContentH);
    lv_obj_set_style_pad_all(idleContainer_, 6, 0);
    lv_obj_set_flex_flow(idleContainer_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(idleContainer_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(idleContainer_, 6, 0);

    idleCard_ = card(idleContainer_);
    lv_obj_set_size(idleCard_, layout::kContentW - 12, 196);
    lv_obj_set_flex_flow(idleCard_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(idleCard_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(idleCard_, 8, 0);

    // Entry Icon: Same car-wrench glyph as sidebar menu bar (ICON_DIAG) in circular badge
    lv_obj_t* iconBadge = box(idleCard_);
    lv_obj_set_size(iconBadge, 46, 46);
    lv_obj_set_style_radius(iconBadge, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(iconBadge, color::card_hi(), 0);
    lv_obj_set_style_border_color(iconBadge, color::stroke(), 0);
    lv_obj_set_style_border_width(iconBadge, 1, 0);
    lv_obj_t* icn = icon(iconBadge, ICON_DIAG, font::icon24(), color::accent());
    lv_obj_center(icn);

    label(idleCard_, font::title(), color::text(), "ECU TROUBLE CODES");
    idleStatusLbl_ = label(idleCard_, font::body(), color::text_dim(), "LINK: OK | MIL: OFF");

    idleScanBtn_ = button(idleCard_, ICON_DIAG, "START DTC SCAN", ButtonStyle::Primary, onScanClicked, this);
    lv_obj_set_size(idleScanBtn_, 200, 38);

    idleChip_ = chip(idleCard_, "READ-ONLY", color::warn());

    // =========================================================================
    // 2. SCANNING VIEW
    // =========================================================================
    scanContainer_ = box(root_);
    lv_obj_set_size(scanContainer_, layout::kContentW, layout::kContentH);
    lv_obj_set_style_pad_all(scanContainer_, 6, 0);
    lv_obj_set_flex_flow(scanContainer_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scanContainer_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    set_visible(scanContainer_, false);

    scanCard_ = card(scanContainer_);
    lv_obj_set_size(scanCard_, layout::kContentW - 12, 196);
    lv_obj_set_flex_flow(scanCard_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scanCard_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(scanCard_, 12, 0);

    icon(scanCard_, ICON_SCAN, font::icon48(), color::accent());
    label(scanCard_, font::title(), color::text(), "QUERYING VEHICLE ECU...");
    scanLbl_ = label(scanCard_, font::caption(), color::text_dim(), "Reading Mode 03 / 07 DTCs");

    scanBar_ = lv_bar_create(scanCard_);
    lv_obj_set_size(scanBar_, layout::kContentW - 48, 10);
    lv_obj_set_style_bg_color(scanBar_, color::track(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scanBar_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(scanBar_, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(scanBar_, color::accent(), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(scanBar_, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(scanBar_, 4, LV_PART_INDICATOR);
    lv_bar_set_range(scanBar_, 0, 100);

    // =========================================================================
    // 3. NO_FAULT VIEW
    // =========================================================================
    noFaultContainer_ = box(root_);
    lv_obj_set_size(noFaultContainer_, layout::kContentW, layout::kContentH);
    lv_obj_set_style_pad_all(noFaultContainer_, 6, 0);
    lv_obj_set_flex_flow(noFaultContainer_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(noFaultContainer_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    set_visible(noFaultContainer_, false);

    noFaultCard_ = card(noFaultContainer_);
    lv_obj_set_size(noFaultCard_, layout::kContentW - 12, 196);
    lv_obj_set_flex_flow(noFaultCard_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(noFaultCard_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(noFaultCard_, 8, 0);

    // Clean 48px checkmark in circle (ICON_CHECK_CIRCLE)
    icon(noFaultCard_, ICON_CHECK_CIRCLE, font::icon48(), color::ok());
    label(noFaultCard_, font::large(), color::ok(), "SYSTEM HEALTH OK");
    label(noFaultCard_, font::body(), color::text_dim(), "Zero stored or pending DTCs reported.");
    chip(noFaultCard_, "EMISSION MONITORS PASSED", color::ok());

    noFaultBtn_ = button(noFaultCard_, ICON_RESTORE, "RE-SCAN ECU", ButtonStyle::Secondary, onScanClicked, this);
    lv_obj_set_size(noFaultBtn_, 180, 36);

    // =========================================================================
    // 4. FAULT_LIST VIEW (Paginated: 3 DTCs per page, up to 10 max)
    // =========================================================================
    listContainer_ = box(root_);
    lv_obj_set_size(listContainer_, layout::kContentW, layout::kContentH);
    lv_obj_set_style_pad_all(listContainer_, 4, 0);
    lv_obj_remove_flag(listContainer_, LV_OBJ_FLAG_SCROLLABLE);
    set_visible(listContainer_, false);

    // Warning Header Banner
    listHeader_ = box(listContainer_);
    lv_obj_set_pos(listHeader_, 2, 2);
    lv_obj_set_size(listHeader_, layout::kContentW - 12, 26);
    lv_obj_set_style_bg_color(listHeader_, color::card_hi(), 0);
    lv_obj_set_style_border_color(listHeader_, color::warn(), 0);
    lv_obj_set_style_border_width(listHeader_, 1, 0);
    lv_obj_set_style_radius(listHeader_, 4, 0);

    lv_obj_t* warnIcon = icon(listHeader_, ICON_ALERT_CIRCLE, font::body(), color::warn());
    lv_obj_align(warnIcon, LV_ALIGN_LEFT_MID, 8, 0);

    listHeaderTitle_ = label(listHeader_, font::caption(), color::warn(), "TROUBLE CODES DETECTED");
    lv_obj_align(listHeaderTitle_, LV_ALIGN_LEFT_MID, 28, 0);

    for (uint8_t i = 0; i < DtcPagination::kDtcPerPage; ++i) {
        lv_obj_t* row = box(listContainer_);
        lv_obj_set_pos(row, 2, 32 + i * 42);
        lv_obj_set_size(row, layout::kContentW - 12, 38);
        lv_obj_set_style_bg_color(row, color::card(), 0);
        lv_obj_set_style_border_color(row, color::stroke(), 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_radius(row, 6, 0);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_user_data(row, reinterpret_cast<void*>(static_cast<intptr_t>(i)));
        lv_obj_add_event_cb(row, onRowClicked, LV_EVENT_CLICKED, this);
        add_touch_feedback(row);

        // Warning Triangle in each row
        listIcons_[i] = icon(row, ICON_ALERT, font::icon24(), color::warn());
        lv_obj_align(listIcons_[i], LV_ALIGN_LEFT_MID, 8, 0);

        listCodes_[i] = label(row, font::title(), color::text(), "P0000");
        lv_obj_align(listCodes_[i], LV_ALIGN_LEFT_MID, 36, 0);

        listDescs_[i] = label(row, font::caption(), color::text_dim(), "DTC Description");
        lv_obj_align(listDescs_[i], LV_ALIGN_LEFT_MID, 96, 0);
        single_line(listDescs_[i]);
        lv_obj_set_width(listDescs_[i], layout::kContentW - 175);

        // Status chip badge (box + label)
        lv_obj_t* chipBox = box(row);
        lv_obj_set_style_radius(chipBox, layout::kRadiusSm, 0);
        lv_obj_set_style_bg_color(chipBox, color::crit(), 0);
        lv_obj_set_style_bg_opa(chipBox, LV_OPA_20, 0);
        lv_obj_set_style_pad_hor(chipBox, 6, 0);
        lv_obj_set_style_pad_ver(chipBox, 2, 0);
        listChipLabels_[i] = label(chipBox, font::caption(), color::crit(), "STORED");
        lv_obj_align(chipBox, LV_ALIGN_RIGHT_MID, -6, 0);

        listChips_[i] = chipBox;
        listRows_[i] = row;
    }

    // Pagination Footer Control Bar
    listFooter_ = box(listContainer_);
    lv_obj_set_pos(listFooter_, 2, 162);
    lv_obj_set_size(listFooter_, layout::kContentW - 12, 38);
    lv_obj_set_flex_flow(listFooter_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(listFooter_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(listFooter_, 0, 0);
    lv_obj_remove_flag(listFooter_, LV_OBJ_FLAG_SCROLLABLE);

    pagePrevBtn_ = button(listFooter_, nullptr, "<", ButtonStyle::Secondary, onPagePrevClicked, this);
    lv_obj_set_size(pagePrevBtn_, 36, 36);
    lv_obj_set_style_pad_hor(pagePrevBtn_, 0, 0);

    pageLbl_ = label(listFooter_, font::body(), color::text_dim(), "1/1");
    lv_obj_set_width(pageLbl_, 44);
    lv_obj_set_style_text_align(pageLbl_, LV_TEXT_ALIGN_CENTER, 0);

    pageNextBtn_ = button(listFooter_, nullptr, ">", ButtonStyle::Secondary, onPageNextClicked, this);
    lv_obj_set_size(pageNextBtn_, 36, 36);
    lv_obj_set_style_pad_hor(pageNextBtn_, 0, 0);

    listRescanBtn_ = button(listFooter_, ICON_RESTORE, "RE-SCAN", ButtonStyle::Secondary, onScanClicked, this);
    lv_obj_set_size(listRescanBtn_, 120, 36);
    lv_obj_set_style_pad_hor(listRescanBtn_, 8, 0);

    // =========================================================================
    // 5. FAULT_DETAIL VIEW
    // =========================================================================
    detailContainer_ = box(root_);
    lv_obj_set_size(detailContainer_, layout::kContentW, layout::kContentH);
    lv_obj_set_style_pad_all(detailContainer_, 6, 0);
    lv_obj_remove_flag(detailContainer_, LV_OBJ_FLAG_SCROLLABLE);
    set_visible(detailContainer_, false);

    detailBackBtn_ = button(detailContainer_, ICON_BACK, "BACK TO LIST", ButtonStyle::Ghost, onDetailBackClicked, this);
    lv_obj_set_pos(detailBackBtn_, 2, 2);
    lv_obj_set_size(detailBackBtn_, 130, 32);

    detailCard_ = card(detailContainer_);
    lv_obj_set_pos(detailCard_, 2, 38);
    lv_obj_set_size(detailCard_, layout::kContentW - 12, 164);
    lv_obj_set_flex_flow(detailCard_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(detailCard_, 10, 0);
    lv_obj_set_style_pad_row(detailCard_, 6, 0);

    lv_obj_t* detailHeaderRow = box(detailCard_);
    lv_obj_set_size(detailHeaderRow, layout::kContentW - 32, 28);
    lv_obj_set_flex_flow(detailHeaderRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(detailHeaderRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(detailHeaderRow, 8, 0);

    icon(detailHeaderRow, ICON_ALERT_CIRCLE, font::icon24(), color::crit());
    detailCodeLbl_ = label(detailHeaderRow, font::large(), color::accent(), "P0100");

    detailDescLbl_ = label(detailCard_, font::body(), color::text(), "Mass or Volume Air Flow Circuit Malfunction");
    chip(detailCard_, "SAE J2012 STANDARDS (READ ONLY)", color::warn());
    detailFrameLbl_ = label(detailCard_, font::caption(), color::text_dim(),
                            "FREEZE FRAME: CLT 85C | RPM 2150 | SPD 42 km/h | LOAD 38%");

    renderCurrentState();
}

void DiagnosticsPage::destroy() {
    if (root_ != nullptr) {
        lv_obj_delete(root_);
        root_ = nullptr;
        idleContainer_ = nullptr;
        idleCard_ = nullptr;
        idleStatusLbl_ = nullptr;
        idleScanBtn_ = nullptr;
        idleChip_ = nullptr;

        scanContainer_ = nullptr;
        scanCard_ = nullptr;
        scanBar_ = nullptr;
        scanLbl_ = nullptr;

        noFaultContainer_ = nullptr;
        noFaultCard_ = nullptr;
        noFaultBtn_ = nullptr;

        listContainer_ = nullptr;
        listHeader_ = nullptr;
        listHeaderTitle_ = nullptr;
        for (uint8_t i = 0; i < DtcPagination::kDtcPerPage; ++i) {
            listRows_[i] = nullptr;
            listIcons_[i] = nullptr;
            listCodes_[i] = nullptr;
            listDescs_[i] = nullptr;
            listChips_[i] = nullptr;
            listChipLabels_[i] = nullptr;
        }
        listFooter_ = nullptr;
        pagePrevBtn_ = nullptr;
        pageLbl_ = nullptr;
        pageNextBtn_ = nullptr;
        listRescanBtn_ = nullptr;

        detailContainer_ = nullptr;
        detailCard_ = nullptr;
        detailCodeLbl_ = nullptr;
        detailDescLbl_ = nullptr;
        detailFrameLbl_ = nullptr;
        detailBackBtn_ = nullptr;
    }
}

void DiagnosticsPage::onShow() {
    if (root_ == nullptr) return;
    renderCurrentState();
}

void DiagnosticsPage::onHide() {
}

bool DiagnosticsPage::onBack() {
    if (root_ == nullptr) return false;
    if (state_ == DiagState::FAULT_DETAIL) {
        setState(DiagState::FAULT_LIST);
        return true;
    }
    if (state_ == DiagState::FAULT_LIST || state_ == DiagState::NO_FAULT) {
        setState(DiagState::IDLE);
        return true;
    }
    return false;
}

void DiagnosticsPage::setState(DiagState s) {
    if (state_ == s) return;
    state_ = s;
    if (s == DiagState::SCANNING) {
        scanStartMs_ = millis();
        if (scanBar_ != nullptr) lv_bar_set_value(scanBar_, 0, LV_ANIM_OFF);
    }
    renderCurrentState();
}

void DiagnosticsPage::renderCurrentState() {
    if (root_ == nullptr) return;
    set_visible(idleContainer_, state_ == DiagState::IDLE);
    set_visible(scanContainer_, state_ == DiagState::SCANNING);
    set_visible(noFaultContainer_, state_ == DiagState::NO_FAULT);
    set_visible(listContainer_, state_ == DiagState::FAULT_LIST);
    set_visible(detailContainer_, state_ == DiagState::FAULT_DETAIL);

    if (state_ == DiagState::FAULT_LIST) {
        renderFaultList();
    }
}

void DiagnosticsPage::startScan() {
    scannedDtcCount_ = 0;
    scanStartMs_ = millis();
    setState(DiagState::SCANNING);

    const AppSettings& settings = SettingsManager::instance().get();
    if (settings.operationMode == OperationMode::DEMO) {
        char buf03[64]{0};
        char buf07[64]{0};
        MockOBD::instance().query("03", buf03, sizeof(buf03));
        MockOBD::instance().query("07", buf07, sizeof(buf07));

        size_t count03 = 0;
        ObdParser::parseDtcResponse(buf03, false, scannedDtcs_, DtcPagination::kMaxDtcCodes, count03);
        scannedDtcCount_ = static_cast<uint8_t>(count03);

        DtcCodeInfo pending[DtcPagination::kMaxDtcCodes];
        size_t count07 = 0;
        ObdParser::parseDtcResponse(buf07, true, pending, DtcPagination::kMaxDtcCodes, count07);
        for (size_t i = 0; i < count07 && scannedDtcCount_ < DtcPagination::kMaxDtcCodes; ++i) {
            bool dup = false;
            for (size_t j = 0; j < scannedDtcCount_; ++j) {
                if (std::strcmp(scannedDtcs_[j].code, pending[i].code) == 0) {
                    dup = true;
                    break;
                }
            }
            if (!dup) {
                scannedDtcs_[scannedDtcCount_++] = pending[i];
            }
        }
    } else {
        if (Elm327Engine::instance()) {
            Elm327Engine::instance()->startDtcScan();
        }
    }
}

void DiagnosticsPage::renderFaultList() {
    if (root_ == nullptr || listContainer_ == nullptr) return;

    uint8_t totalDtc = (scannedDtcCount_ > DtcPagination::kMaxDtcCodes)
                       ? DtcPagination::kMaxDtcCodes
                       : scannedDtcCount_;
    uint8_t totalPages = DtcPagination::calculateTotalPages(totalDtc);

    if (currentPage_ >= totalPages) {
        currentPage_ = totalPages > 0 ? totalPages - 1 : 0;
    }

    // Update Header
    if (listHeaderTitle_ != nullptr) {
        char buf[48];
        snprintf(buf, sizeof(buf), "TROUBLE CODES DETECTED (%u FAULTS)", totalDtc);
        set_text(listHeaderTitle_, buf);
    }

    // Update Rows for current page
    uint8_t startIdx = DtcPagination::getStartIndex(currentPage_);
    for (uint8_t i = 0; i < DtcPagination::kDtcPerPage; ++i) {
        uint8_t dtcIdx = startIdx + i;
        if (dtcIdx < totalDtc && listRows_[i] != nullptr) {
            set_visible(listRows_[i], true);
            const auto& dtc = scannedDtcs_[dtcIdx];

            set_text(listCodes_[i], dtc.code);
            set_text(listDescs_[i], dtc.desc ? dtc.desc : "Diagnostic Trouble Code");

            const char* chipText = dtc.isPending ? "PENDING" : "STORED";
            lv_color_t chipColor = dtc.isPending ? color::info() : color::crit();
            lv_color_t iconColor = (dtc.severity == DtcSeverity::CRITICAL) ? color::crit() :
                                   (dtc.isPending ? color::info() : color::warn());

            if (listChipLabels_[i] != nullptr) {
                set_text(listChipLabels_[i], chipText);
                lv_obj_set_style_text_color(listChipLabels_[i], chipColor, 0);
            }
            if (listChips_[i] != nullptr) {
                lv_obj_set_style_bg_color(listChips_[i], chipColor, 0);
            }
            if (listIcons_[i] != nullptr) {
                lv_obj_set_style_text_color(listIcons_[i], iconColor, 0);
            }

            lv_obj_set_user_data(listRows_[i], reinterpret_cast<void*>(static_cast<intptr_t>(dtcIdx)));
        } else if (listRows_[i] != nullptr) {
            set_visible(listRows_[i], false);
        }
    }

    updatePaginationControls();
}

void DiagnosticsPage::updatePaginationControls() {
    if (listFooter_ == nullptr) return;

    uint8_t totalDtc = (scannedDtcCount_ > DtcPagination::kMaxDtcCodes)
                       ? DtcPagination::kMaxDtcCodes
                       : scannedDtcCount_;
    uint8_t totalPages = DtcPagination::calculateTotalPages(totalDtc);

    if (totalPages <= 1) {
        if (pagePrevBtn_ != nullptr) set_visible(pagePrevBtn_, false);
        if (pageLbl_ != nullptr) set_visible(pageLbl_, false);
        if (pageNextBtn_ != nullptr) set_visible(pageNextBtn_, false);
        if (listRescanBtn_ != nullptr) lv_obj_set_width(listRescanBtn_, currentContentW_ - 12);
    } else {
        if (pagePrevBtn_ != nullptr) {
            set_visible(pagePrevBtn_, true);
            if (currentPage_ == 0) {
                lv_obj_add_state(pagePrevBtn_, LV_STATE_DISABLED);
                lv_obj_set_style_opa(pagePrevBtn_, LV_OPA_40, 0);
            } else {
                lv_obj_clear_state(pagePrevBtn_, LV_STATE_DISABLED);
                lv_obj_set_style_opa(pagePrevBtn_, LV_OPA_COVER, 0);
            }
        }

        if (pageLbl_ != nullptr) {
            set_visible(pageLbl_, true);
            char buf[16];
            snprintf(buf, sizeof(buf), "%u / %u", currentPage_ + 1, totalPages);
            set_text(pageLbl_, buf);
        }

        if (pageNextBtn_ != nullptr) {
            set_visible(pageNextBtn_, true);
            if (currentPage_ + 1 >= totalPages) {
                lv_obj_add_state(pageNextBtn_, LV_STATE_DISABLED);
                lv_obj_set_style_opa(pageNextBtn_, LV_OPA_40, 0);
            } else {
                lv_obj_clear_state(pageNextBtn_, LV_STATE_DISABLED);
                lv_obj_set_style_opa(pageNextBtn_, LV_OPA_COVER, 0);
            }
        }

        if (listRescanBtn_ != nullptr) lv_obj_set_width(listRescanBtn_, 120);
    }
}

void DiagnosticsPage::update(const VehicleState& state, uint32_t nowMs) {
    if (root_ == nullptr) return;
    if (state_ == DiagState::IDLE) {
        char buf[48];
        snprintf(buf, sizeof(buf), "LINK: %s | MIL: %s (%u DTCs)",
                 state.isConnected ? "OK" : "NO LINK",
                 state.milActive ? "ACTIVE" : "OFF",
                 state.dtcCount);
        set_text(idleStatusLbl_, buf);
    } else if (state_ == DiagState::SCANNING) {
        uint32_t elapsed = nowMs - scanStartMs_;
        const AppSettings& settings = SettingsManager::instance().get();

        if (settings.operationMode == OperationMode::DEMO) {
            int32_t progress = (elapsed * 100) / 1000;
            if (progress > 100) progress = 100;
            if (scanBar_ != nullptr) lv_bar_set_value(scanBar_, progress, LV_ANIM_OFF);

            if (elapsed >= 1000) {
                BuzzerController::instance().playSuccessTone();
                if (scannedDtcCount_ > 0) {
                    setState(DiagState::FAULT_LIST);
                } else {
                    setState(DiagState::NO_FAULT);
                }
            }
        } else {
            bool complete = false;
            if (Elm327Engine::instance()) {
                complete = Elm327Engine::instance()->isDtcScanComplete();
            } else {
                complete = (elapsed >= 1000);
            }

            int32_t progress = complete ? 100 : ((elapsed * 90) / 3000);
            if (progress > 100) progress = 100;
            if (scanBar_ != nullptr) lv_bar_set_value(scanBar_, progress, LV_ANIM_OFF);

            if (complete || elapsed >= 3500) {
                if (Elm327Engine::instance()) {
                    uint8_t count = Elm327Engine::instance()->getScannedDtcCount();
                    const DtcCodeInfo* dtcs = Elm327Engine::instance()->getScannedDtcs();
                    if (count > DtcPagination::kMaxDtcCodes) count = DtcPagination::kMaxDtcCodes;
                    scannedDtcCount_ = count;
                    for (uint8_t i = 0; i < count; ++i) {
                        scannedDtcs_[i] = dtcs[i];
                    }
                }
                BuzzerController::instance().playSuccessTone();
                if (scannedDtcCount_ > 0) {
                    setState(DiagState::FAULT_LIST);
                } else {
                    setState(DiagState::NO_FAULT);
                }
            }
        }
    } else if (state_ == DiagState::FAULT_DETAIL && detailFrameLbl_ != nullptr) {
        char frameBuf[80];
        std::snprintf(frameBuf, sizeof(frameBuf),
                      "LIVE DATA: CLT %dC | RPM %u | SPD %u km/h | LOAD %u%%",
                      state.coolantTempC, state.rpm, state.speedKmh, state.engineLoad);
        set_text(detailFrameLbl_, frameBuf);
    }
}

void DiagnosticsPage::onLayout(int32_t contentW) {
    if (root_ == nullptr) return;
    currentContentW_ = contentW;

    lv_obj_set_size(root_, contentW, layout::kContentH);
    if (idleContainer_ != nullptr) lv_obj_set_size(idleContainer_, contentW, layout::kContentH);
    if (scanContainer_ != nullptr) lv_obj_set_size(scanContainer_, contentW, layout::kContentH);
    if (noFaultContainer_ != nullptr) lv_obj_set_size(noFaultContainer_, contentW, layout::kContentH);
    if (listContainer_ != nullptr) lv_obj_set_size(listContainer_, contentW, layout::kContentH);
    if (detailContainer_ != nullptr) lv_obj_set_size(detailContainer_, contentW, layout::kContentH);

    const int32_t cardW = contentW - 12;

    if (idleCard_ != nullptr) lv_obj_set_width(idleCard_, cardW);
    if (scanCard_ != nullptr) lv_obj_set_width(scanCard_, cardW);
    if (scanBar_ != nullptr) lv_obj_set_width(scanBar_, contentW - 48);
    if (noFaultCard_ != nullptr) lv_obj_set_width(noFaultCard_, cardW);
    if (detailCard_ != nullptr) {
        lv_obj_set_width(detailCard_, cardW);
        if (detailDescLbl_ != nullptr) lv_obj_set_width(detailDescLbl_, cardW - 20);
        if (detailFrameLbl_ != nullptr) lv_obj_set_width(detailFrameLbl_, cardW - 20);
    }

    if (listHeader_ != nullptr) lv_obj_set_width(listHeader_, cardW);
    if (listFooter_ != nullptr) lv_obj_set_width(listFooter_, cardW);

    for (uint8_t i = 0; i < DtcPagination::kDtcPerPage; ++i) {
        if (listRows_[i] != nullptr) {
            lv_obj_set_width(listRows_[i], cardW);
            if (listDescs_[i] != nullptr) {
                lv_obj_set_width(listDescs_[i], cardW - 170);
            }
            if (listChips_[i] != nullptr) {
                lv_obj_align(listChips_[i], LV_ALIGN_RIGHT_MID, -6, 0);
            }
        }
    }

    updatePaginationControls();
}

void DiagnosticsPage::onScanClicked(lv_event_t* e) {
    auto* self = static_cast<DiagnosticsPage*>(lv_event_get_user_data(e));
    if (self) {
        BuzzerController::instance().playButtonSelect();
        self->currentPage_ = 0;
        self->startScan();
    }
}

void DiagnosticsPage::onPagePrevClicked(lv_event_t* e) {
    auto* self = static_cast<DiagnosticsPage*>(lv_event_get_user_data(e));
    if (self && self->currentPage_ > 0) {
        BuzzerController::instance().playButtonUpDown();
        self->currentPage_--;
        self->renderFaultList();
    }
}

void DiagnosticsPage::onPageNextClicked(lv_event_t* e) {
    auto* self = static_cast<DiagnosticsPage*>(lv_event_get_user_data(e));
    if (self) {
        uint8_t totalDtc = (self->scannedDtcCount_ > DtcPagination::kMaxDtcCodes)
                           ? DtcPagination::kMaxDtcCodes
                           : self->scannedDtcCount_;
        uint8_t totalPages = DtcPagination::calculateTotalPages(totalDtc);
        if (self->currentPage_ + 1 < totalPages) {
            BuzzerController::instance().playButtonUpDown();
            self->currentPage_++;
            self->renderFaultList();
        }
    }
}

void DiagnosticsPage::onDetailBackClicked(lv_event_t* e) {
    auto* self = static_cast<DiagnosticsPage*>(lv_event_get_user_data(e));
    if (self) {
        BuzzerController::instance().playButtonBack();
        self->setState(DiagState::FAULT_LIST);
    }
}

void DiagnosticsPage::onRowClicked(lv_event_t* e) {
    auto* self = static_cast<DiagnosticsPage*>(lv_event_get_user_data(e));
    lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    intptr_t dtcIdx = target != nullptr ? reinterpret_cast<intptr_t>(lv_obj_get_user_data(target)) : 0;
    if (self && self->scannedDtcCount_ > 0) {
        BuzzerController::instance().playButtonSelect();
        self->selectedDtc_ = static_cast<uint8_t>(dtcIdx < self->scannedDtcCount_ ? dtcIdx : 0);
        if (self->detailCodeLbl_) set_text(self->detailCodeLbl_, self->scannedDtcs_[self->selectedDtc_].code);
        if (self->detailDescLbl_) set_text(self->detailDescLbl_, self->scannedDtcs_[self->selectedDtc_].desc ? self->scannedDtcs_[self->selectedDtc_].desc : "Diagnostic Trouble Code");
        self->setState(DiagState::FAULT_DETAIL);
    }
}

void DiagnosticsPage::applyTheme() {
    if (root_ == nullptr) return;
    lv_obj_set_style_bg_color(root_, color::bg(), 0);
    if (scanBar_ != nullptr) {
        lv_obj_set_style_bg_color(scanBar_, color::accent(), LV_PART_INDICATOR);
    }
    if (detailCodeLbl_ != nullptr) {
        lv_obj_set_style_text_color(detailCodeLbl_, color::accent(), 0);
    }
}

}  // namespace ui
#endif  // !UNIT_TEST
