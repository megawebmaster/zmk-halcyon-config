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
#include <zmk/ble.h>
#include <zmk/display.h>
#include <zmk/endpoints.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/usb.h>

#if IS_ENABLED(CONFIG_ZMK_WPM)
#include <zmk/events/wpm_state_changed.h>
#include <zmk/wpm.h>
#endif

#include "status.h"

LV_IMG_DECLARE(Forest);

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

#define CANVAS_STATUS 0
#define CANVAS_LAYER 1
#define CANVAS_WPM 3

struct output_status_state {
    struct zmk_endpoint_instance selected_endpoint;
    bool active_profile_connected;
    bool active_profile_bonded;
};

struct layer_status_state {
    zmk_keymap_layer_index_t index;
};

// --- Battery: this half locally, the other half from the split link -------------------

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
    const struct zmk_battery_state_changed *ev = as_zmk_battery_state_changed(eh);

    return (struct battery_status_state){
        .level = (ev != NULL) ? ev->state_of_charge : zmk_battery_state_of_charge(),
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

#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)

struct peer_battery_state {
    uint8_t level;
};

static void set_peer_battery_status(struct zmk_widget_status *widget,
                                    struct peer_battery_state state) {
    widget->state.peer_battery = state.level;
    widget->state.peer_battery_valid = state.level > 0;

    draw_status_row(lv_obj_get_child(widget->obj, CANVAS_STATUS), &widget->state);
}

static void peer_battery_update_cb(struct peer_battery_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_peer_battery_status(widget, state); }
}

static struct peer_battery_state peer_battery_get_state(const zmk_event_t *eh) {
    const struct zmk_peripheral_battery_state_changed *ev =
        as_zmk_peripheral_battery_state_changed(eh);

    return (struct peer_battery_state){.level = (ev != NULL) ? ev->state_of_charge : 0};
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_peer_battery, struct peer_battery_state,
                            peer_battery_update_cb, peer_battery_get_state)
ZMK_SUBSCRIPTION(widget_peer_battery, zmk_peripheral_battery_state_changed);

#endif /* IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING) */

// --- Endpoint -------------------------------------------------------------------------

static void set_output_status(struct zmk_widget_status *widget,
                              const struct output_status_state *state) {
    widget->state.selected_endpoint = state->selected_endpoint;
    widget->state.active_profile_connected = state->active_profile_connected;
    widget->state.active_profile_bonded = state->active_profile_bonded;

    draw_status_row(lv_obj_get_child(widget->obj, CANVAS_STATUS), &widget->state);
}

static void output_status_update_cb(struct output_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_output_status(widget, &state); }
}

static struct output_status_state output_status_get_state(const zmk_event_t *_eh) {
    return (struct output_status_state){
        .selected_endpoint = zmk_endpoint_get_selected(),
        .active_profile_connected = zmk_ble_active_profile_is_connected(),
        .active_profile_bonded = !zmk_ble_active_profile_is_open(),
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_output_status, struct output_status_state,
                            output_status_update_cb, output_status_get_state)
ZMK_SUBSCRIPTION(widget_output_status, zmk_endpoint_changed);

#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(widget_output_status, zmk_usb_conn_state_changed);
#endif
#if IS_ENABLED(CONFIG_ZMK_BLE)
ZMK_SUBSCRIPTION(widget_output_status, zmk_ble_active_profile_changed);
#endif

// --- Layer ------------------------------------------------------------------------------

static void set_layer_status(struct zmk_widget_status *widget, struct layer_status_state state) {
    widget->state.layer_index = state.index;

    draw_layer_row(lv_obj_get_child(widget->obj, CANVAS_LAYER), &widget->state);
}

static void layer_status_update_cb(struct layer_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_layer_status(widget, state); }
}

static struct layer_status_state layer_status_get_state(const zmk_event_t *eh) {
    return (struct layer_status_state){.index = zmk_keymap_highest_layer_active()};
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_layer_status, struct layer_status_state, layer_status_update_cb,
                            layer_status_get_state)

ZMK_SUBSCRIPTION(widget_layer_status, zmk_layer_state_changed);

// --- WPM ---------------------------------------------------------------------------------

#if IS_ENABLED(CONFIG_ZMK_WPM)

struct wpm_status_state {
    uint8_t wpm;
};

static void set_wpm_status(struct zmk_widget_status *widget, struct wpm_status_state state) {
    if (widget->state.wpm_value == state.wpm) {
        // The WPM event fires once a second. Redrawing an ePaper panel that often is not
        // worth it, so only redraw when the number actually changed.
        return;
    }

    widget->state.wpm_value = state.wpm;

    draw_wpm_row(lv_obj_get_child(widget->obj, CANVAS_WPM), &widget->state);
}

static void wpm_status_update_cb(struct wpm_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_wpm_status(widget, state); }
}

static struct wpm_status_state wpm_status_get_state(const zmk_event_t *eh) {
    const struct zmk_wpm_state_changed *ev = as_zmk_wpm_state_changed(eh);

    return (struct wpm_status_state){
        .wpm = (uint8_t)CLAMP((ev != NULL) ? ev->state : zmk_wpm_get_state(), 0, UINT8_MAX)};
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_wpm_status, struct wpm_status_state, wpm_status_update_cb,
                            wpm_status_get_state)
ZMK_SUBSCRIPTION(widget_wpm_status, zmk_wpm_state_changed);

#endif /* IS_ENABLED(CONFIG_ZMK_WPM) */

// --- Widget --------------------------------------------------------------------------------

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
#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
    widget_peer_battery_init();
#endif
    widget_output_status_init();
    widget_layer_status_init();
#if IS_ENABLED(CONFIG_ZMK_WPM)
    widget_wpm_status_init();
#endif

    return 0;
}

lv_obj_t *zmk_widget_status_obj(struct zmk_widget_status *widget) { return widget->obj; }
