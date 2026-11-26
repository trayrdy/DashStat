#pragma once
#include <stdint.h>
#include "model/vehicle_state.h"

#ifndef UNIT_TEST
#include <lvgl.h>
#endif

namespace ui {

enum class PageId : uint8_t {
    Dashboard = 0,
    Live,
    Trip,
    Settings,
    Diagnostics,
    Count
};

#ifndef UNIT_TEST
class Page {
public:
    virtual ~Page() = default;

    virtual void create(lv_obj_t* parent) = 0;
    virtual const char* title() const = 0;
    virtual void onShow() {}
    virtual void onHide() {}
    virtual bool onBack() { return false; }
    virtual void update(const VehicleState& state, uint32_t nowMs) = 0;
    virtual void onLayout(int32_t contentW) {}
    virtual void applyTheme() {}

    lv_obj_t* root() const { return root_; }

protected:
    lv_obj_t* root_ = nullptr;
};
#endif

}  // namespace ui
