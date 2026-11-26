#ifndef UNIT_TEST
#include "ui/components/nav_rail.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace ui {
namespace {

const char* const kIcons[] = {
    ICON_DASHBOARD,
    ICON_LIVE,
    ICON_TRIP,
    ICON_SETTINGS,
    ICON_DIAG
};

}  // namespace

void NavRail::create(lv_obj_t* parent, SelectFn onSelect) {
    onSelect_ = onSelect;

    root_ = box(parent);
    lv_obj_set_pos(root_, 0, 0);
    lv_obj_set_size(root_, layout::kRailW, layout::kScreenH);
    lv_obj_set_style_bg_color(root_, color::surface(), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(root_, LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_border_width(root_, 1, 0);
    lv_obj_set_style_border_color(root_, color::stroke(), 0);

    for (int i = 0; i < kItems; ++i) {
        lv_obj_t* item = lv_button_create(root_);
        items_[i] = item;
        lv_obj_remove_style_all(item);
        lv_obj_set_pos(item, 0, i * layout::kNavItemH);
        lv_obj_set_size(item, layout::kRailW - 1, layout::kNavItemH);
        lv_obj_set_style_bg_color(item, color::card_hi(), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(item, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_user_data(item, this);
        lv_obj_add_event_cb(item, clickedCb, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<intptr_t>(i)));

        // Active indicator bar (left side 3px wide)
        markers_[i] = box(item);
        lv_obj_set_size(markers_[i], 3, 24);
        lv_obj_align(markers_[i], LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_radius(markers_[i], 2, 0);
        lv_obj_set_style_bg_color(markers_[i], color::accent(), 0);
        lv_obj_set_style_bg_opa(markers_[i], LV_OPA_COVER, 0);

        // Center Icon
        icons_[i] = icon(item, kIcons[i], font::icon24(), color::text_dim());
        lv_obj_center(icons_[i]);

        // Status badge dot
        badges_[i] = box(item);
        lv_obj_set_size(badges_[i], 8, 8);
        lv_obj_align(badges_[i], LV_ALIGN_CENTER, 12, -11);
        lv_obj_set_style_radius(badges_[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(badges_[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(badges_[i], 2, 0);
        lv_obj_set_style_border_color(badges_[i], color::surface(), 0);
        set_visible(badges_[i], false);
    }
}

void NavRail::setActive(PageId page) {
    currentPage_ = page;
    for (int i = 0; i < kItems; ++i) {
        const bool active = (i == static_cast<int>(page));
        set_visible(markers_[i], active);
        set_text_color(icons_[i], active ? color::accent() : color::text_dim());
        lv_obj_set_style_bg_color(items_[i], color::accent(), 0);
        lv_obj_set_style_bg_opa(items_[i], active ? LV_OPA_10 : LV_OPA_TRANSP, 0);
    }
}

void NavRail::applyTheme() {
    if (root_ == nullptr) return;
    lv_obj_set_style_bg_color(root_, color::surface(), 0);
    lv_obj_set_style_border_color(root_, color::stroke(), 0);
    for (int i = 0; i < kItems; ++i) {
        if (markers_[i] != nullptr) {
            lv_obj_set_style_bg_color(markers_[i], color::accent(), 0);
        }
    }
    setActive(currentPage_);
}

void NavRail::setBadge(PageId page, bool visible, lv_color_t c) {
    int idx = static_cast<int>(page);
    if (idx >= 0 && idx < kItems && badges_[idx] != nullptr) {
        lv_obj_set_style_bg_color(badges_[idx], c, 0);
        set_visible(badges_[idx], visible);
    }
}

void NavRail::clickedCb(lv_event_t* e) {
    auto* self = static_cast<NavRail*>(lv_obj_get_user_data(lv_event_get_current_target_obj(e)));
    const int index = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    if (self != nullptr && self->onSelect_ != nullptr) {
        self->onSelect_(static_cast<PageId>(index));
    }
}

}  // namespace ui
#endif // UNIT_TEST
