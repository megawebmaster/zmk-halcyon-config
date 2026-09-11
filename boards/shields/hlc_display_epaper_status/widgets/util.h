/*
 *
 * Copyright (c) 2025 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once

#include <lvgl.h>
#include <zmk/endpoints.h>

#define CANVAS_SIZE 88
#define CANVAS_COLOR_FORMAT LV_COLOR_FORMAT_L8 // smallest type supported by sw_rotate
#define CANVAS_BUF_SIZE                                                                            \
    LV_CANVAS_BUF_SIZE(CANVAS_SIZE, CANVAS_SIZE, LV_COLOR_FORMAT_GET_BPP(CANVAS_COLOR_FORMAT),     \
                       LV_DRAW_BUF_STRIDE_ALIGN)

#define LVGL_BACKGROUND                                                                            \
    IS_ENABLED(CONFIG_HLC_EPAPER_WIDGET_INVERTED) ? lv_color_black() : lv_color_white()
#define LVGL_FOREGROUND                                                                            \
    IS_ENABLED(CONFIG_HLC_EPAPER_WIDGET_INVERTED) ? lv_color_white() : lv_color_black()

// Two batteries and the connection symbol share one 88px row, so the battery glyph is
// narrower than the upstream 33px one: 25px body plus a 3px terminal nub.
#define BATTERY_WIDTH 28

// Both variants of the widget draw the same thing. The central sources it all locally;
// the peripheral gets everything but its own battery forwarded over the split link.
struct status_state {
    // This half's own battery.
    uint8_t battery;
    bool charging;

    // The other half's battery. Not valid until the other half has reported in.
    uint8_t peer_battery;
    bool peer_battery_valid;

    struct zmk_endpoint_instance selected_endpoint;
    bool active_profile_connected;
    bool active_profile_bonded;

    uint8_t layer_index;
    uint8_t wpm_value;

#if IS_ENABLED(CONFIG_ZMK_SPLIT) && !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    // Whether this peripheral is linked to the central at all. Everything above is
    // forwarded from the central and is meaningless while this is false.
    bool connected;
#endif
};

struct battery_status_state {
    uint8_t level;
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    bool usb_present;
#endif
};

// Layout, in the reading frame. Each canvas is CANVAS_SIZE square and is rotated 90 deg
// by rotate_canvas() to compensate for the panel being mounted sideways, so canvas
// coordinates ARE reading coordinates: canvas x runs left-to-right across the 88px
// width, canvas y runs down from wherever the canvas is aligned on the screen.
//
//   y   0 -  15   status row: own battery, connection symbol, other half's battery
//   y  20 -  77   layer number
//   y  84 - 167   artwork (clipped: the widget ends at 184, so only the top of it shows)
//   y 168 - 183   WPM row
//
// Children are added in that order so each one paints over the tail of the one before.
#define ROW_STATUS_Y 0
#define ROW_LAYER_Y 20
#define ROW_ART_Y 84
#define ROW_WPM_Y 168

// Status row: two batteries with at least 5px clear either side of the symbol.
#define STATUS_BATTERY_LEFT_X 0
#define STATUS_SYMBOL_X 30
#define STATUS_SYMBOL_W 28
#define STATUS_BATTERY_RIGHT_X 60

void rotate_canvas(lv_obj_t *canvas);

// Shared row painters, so the central and peripheral variants of the widget cannot
// drift apart visually.
void draw_status_row(lv_obj_t *canvas, const struct status_state *state);
void draw_layer_row(lv_obj_t *canvas, const struct status_state *state);
void draw_wpm_row(lv_obj_t *canvas, const struct status_state *state);
void draw_battery_at(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y, uint8_t level, bool charging);
void init_label_dsc(lv_draw_label_dsc_t *label_dsc, lv_color_t color, const lv_font_t *font,
                    lv_text_align_t align);
void init_rect_dsc(lv_draw_rect_dsc_t *rect_dsc, lv_color_t bg_color);
void init_line_dsc(lv_draw_line_dsc_t *line_dsc, lv_color_t color, uint8_t width);
void init_arc_dsc(lv_draw_arc_dsc_t *arc_dsc, lv_color_t color, uint8_t width);

void canvas_draw_line(lv_obj_t *canvas, const lv_point_t points[], uint32_t point_cnt,
                      lv_draw_line_dsc_t *draw_dsc);
void canvas_draw_rect(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h,
                      lv_draw_rect_dsc_t *draw_dsc);
void canvas_draw_arc(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y, lv_coord_t r,
                     int32_t start_angle, int32_t end_angle, lv_draw_arc_dsc_t *draw_dsc);
void canvas_draw_text(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y, lv_coord_t max_w,
                      lv_draw_label_dsc_t *draw_dsc, const char *txt);
void canvas_draw_img(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y, const lv_image_dsc_t *src,
                     lv_draw_image_dsc_t *draw_dsc);
