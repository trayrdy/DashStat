#pragma once
#include <lvgl.h>
#include <stdint.h>
#include "model/vehicle_state.h"

namespace ui {

class StatusBar {
public:
    void create(lv_obj_t* parent, lv_event_cb_t onBack = nullptr, void* user = nullptr);
    void setTitle(const char* title);
    void showBack(bool show);
    void setDrivingLock(bool locked, bool flashing);
    void update(const VehicleState& state, uint32_t nowMs);
    void setLayout(int32_t x, int32_t w);

    lv_obj_t* root() const { return root_; }

private:
    lv_obj_t* root_ = nullptr;
    lv_obj_t* back_ = nullptr;
    lv_obj_t* title_ = nullptr;
    lv_obj_t* mil_ = nullptr;
    lv_obj_t* lock_ = nullptr;
    lv_obj_t* alert_ = nullptr;
    lv_obj_t* link_ = nullptr;
    lv_obj_t* clock_ = nullptr;
    uint32_t lastUpdateMs_ = 0;
    bool blink_ = false;
};

}  // namespace ui
