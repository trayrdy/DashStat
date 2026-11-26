#pragma once
#include <lvgl.h>
#include "ui/pages/page.h"

namespace ui {

class NavRail {
public:
    using SelectFn = void (*)(PageId page);

    void create(lv_obj_t* parent, SelectFn onSelect);
    void setActive(PageId page);
    void applyTheme();
    void setBadge(PageId page, bool visible, lv_color_t color);

    lv_obj_t* root() const { return root_; }
    void setPos(int32_t x, int32_t y) {
        if (root_ != nullptr) lv_obj_set_pos(root_, x, y);
    }

private:
    static void clickedCb(lv_event_t* e);

    static constexpr int kItems = static_cast<int>(PageId::Count);
    PageId currentPage_ = PageId::Dashboard;
    lv_obj_t* root_ = nullptr;
    lv_obj_t* items_[kItems] = {};
    lv_obj_t* icons_[kItems] = {};
    lv_obj_t* markers_[kItems] = {};
    lv_obj_t* badges_[kItems] = {};
    SelectFn onSelect_ = nullptr;
};

}  // namespace ui
