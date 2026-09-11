/*
 *
 * Copyright (c) 2025 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 */

#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/battery.h>
#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/split_peripheral_status_data_changed.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/split/bluetooth/peripheral.h>
#include <zmk/split/transport/types.h>
#include <zmk/usb.h>

#include "peripheral_status.h"

LV_IMG_DECLARE(Forest);

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

struct peripheral_status_state {
    bool connected;
};

#define CANVAS_STATUS 0
#define CANVAS_LAYER 1
#define CANVAS_WPM 3

// --- This half's own battery, read locally -----------------------------------------

static void set_battery_status(struct zmk_widget_status *widget,
                               struct battery_status_state state) {
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    widget->state.charging = state.usb_present;
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */

    widget->state.battery = state.level;

    draw_status_row(lv_obj_get_child(widget->obj, CANVAS_STATUS), &widget->state);
}

static void battery_status_update_cb(struct battery_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_battery_status(widget, state); }
}

static struct battery_status_state battery_status_get_state(const zmk_event_t *eh) {
    return (struct battery_status_state){
        .level = zmk_battery_state_of_charge(),
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
        .usb_present = zmk_usb_is_powered(),
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_battery_status, struct battery_status_state,
                            battery_status_update_cb, battery_status_get_state)

ZMK_SUBSCRIPTION(widget_battery_status, zmk_battery_state_changed);
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(widget_battery_status, zmk_usb_conn_state_changed);
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */

// --- Split link up/down -------------------------------------------------------------

static struct peripheral_status_state get_state(const zmk_event_t *_eh) {
    return (struct peripheral_status_state){.connected = zmk_split_bt_peripheral_is_connected()};
}

static void set_connection_status(struct zmk_widget_status *widget,
                                  struct peripheral_status_state state) {
    widget->state.connected = state.connected;

    draw_status_row(lv_obj_get_child(widget->obj, CANVAS_STATUS), &widget->state);
}

static void connection_status_update_cb(struct peripheral_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_connection_status(widget, state); }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_peripheral_status, struct peripheral_status_state,
                            connection_status_update_cb, get_state)
ZMK_SUBSCRIPTION(widget_peripheral_status, zmk_split_peripheral_status_changed);

// --- Everything the central forwards -------------------------------------------------

static void set_central_status(struct zmk_widget_status *widget,
                               struct zmk_split_peripheral_status_data_changed state) {
    const struct status_state prev = widget->state;

    // There are exactly two halves, so "the other half" is the first battery slot that
    // is not this peripheral's own. This does not generalise past two peripherals, and
    // BLE source ids are assigned by connection order rather than by physical side.
    widget->state.peer_battery_valid = false;
    for (uint8_t i = 0; i < ZMK_SPLIT_STATUS_MAX_PERIPHERALS; i++) {
        if (i == state.source) {
            continue;
        }

        if (state.battery_levels[i] > 0) {
            widget->state.peer_battery = state.battery_levels[i];
            widget->state.peer_battery_valid = true;
            break;
        }
    }

    widget->state.selected_endpoint.transport = state.endpoint_transport;
    widget->state.active_profile_connected =
        (state.profile_flags & ZMK_SPLIT_STATUS_PROFILE_CONNECTED) != 0;
    widget->state.active_profile_bonded =
        (state.profile_flags & ZMK_SPLIT_STATUS_PROFILE_BONDED) != 0;
    widget->state.layer_index = state.layer_index;
    widget->state.wpm_value = state.wpm;

    // This arrives on a timer as well as on change, so repaint only what moved:
    // refreshing an unchanged ePaper row costs a visible flicker for nothing.
    if (prev.peer_battery != widget->state.peer_battery ||
        prev.peer_battery_valid != widget->state.peer_battery_valid ||
        prev.selected_endpoint.transport != widget->state.selected_endpoint.transport ||
        prev.active_profile_connected != widget->state.active_profile_connected ||
        prev.active_profile_bonded != widget->state.active_profile_bonded) {
        draw_status_row(lv_obj_get_child(widget->obj, CANVAS_STATUS), &widget->state);
    }

    if (prev.layer_index != widget->state.layer_index) {
        draw_layer_row(lv_obj_get_child(widget->obj, CANVAS_LAYER), &widget->state);
    }

    if (prev.wpm_value != widget->state.wpm_value) {
        draw_wpm_row(lv_obj_get_child(widget->obj, CANVAS_WPM), &widget->state);
    }
}

static void central_status_update_cb(struct zmk_split_peripheral_status_data_changed state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_central_status(widget, state); }
}

static struct zmk_split_peripheral_status_data_changed
central_status_get_state(const zmk_event_t *eh) {
    const struct zmk_split_peripheral_status_data_changed *ev =
        as_zmk_split_peripheral_status_data_changed(eh);

    return ev != NULL ? *ev : (struct zmk_split_peripheral_status_data_changed){0};
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_central_status,
                            struct zmk_split_peripheral_status_data_changed,
                            central_status_update_cb, central_status_get_state)
ZMK_SUBSCRIPTION(widget_central_status, zmk_split_peripheral_status_data_changed);

// --- Widget ---------------------------------------------------------------------------

int zmk_widget_status_init(struct zmk_widget_status *widget, lv_obj_t *parent) {
    widget->obj = lv_obj_create(parent);
    lv_obj_set_size(widget->obj, 184, 88);

    lv_obj_t *status = lv_canvas_create(widget->obj);
    lv_obj_align(status, LV_ALIGN_TOP_LEFT, ROW_STATUS_Y, 0);
    lv_canvas_set_buffer(status, widget->cbuf, CANVAS_SIZE, CANVAS_SIZE, CANVAS_COLOR_FORMAT);

    lv_obj_t *layer = lv_canvas_create(widget->obj);
    lv_obj_align(layer, LV_ALIGN_TOP_LEFT, ROW_LAYER_Y, 0);
    lv_canvas_set_buffer(layer, widget->cbuf2, CANVAS_SIZE, CANVAS_SIZE, CANVAS_COLOR_FORMAT);

    // The art is clipped by the right edge of the widget and then again by the WPM
    // canvas, which is a later child and so paints over it. Only its top shows.
    lv_obj_t *art = lv_img_create(widget->obj);
    lv_image_set_src(art, &Forest);
    lv_obj_align(art, LV_ALIGN_TOP_LEFT, ROW_ART_Y, 0);

    lv_obj_t *wpm = lv_canvas_create(widget->obj);
    lv_obj_align(wpm, LV_ALIGN_TOP_LEFT, ROW_WPM_Y, 0);
    lv_canvas_set_buffer(wpm, widget->cbuf3, CANVAS_SIZE, CANVAS_SIZE, CANVAS_COLOR_FORMAT);

    // Paint every row once: the listeners below only repaint what changed, so a row
    // whose first event carries the zero state would otherwise stay uninitialised.
    draw_status_row(status, &widget->state);
    draw_layer_row(layer, &widget->state);
    draw_wpm_row(wpm, &widget->state);

    sys_slist_append(&widgets, &widget->node);
    widget_battery_status_init();
    widget_peripheral_status_init();
    widget_central_status_init();

    return 0;
}

lv_obj_t *zmk_widget_status_obj(struct zmk_widget_status *widget) { return widget->obj; }
