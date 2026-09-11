/*
 *
 * Copyright (c) 2023 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 */

#include <stdio.h>

#include <zephyr/kernel.h>
#include "util.h"

LV_IMG_DECLARE(bolt);

void rotate_canvas(lv_obj_t *canvas) {
    uint8_t *buf = lv_canvas_get_draw_buf(canvas)->data;
    static uint8_t buf_copy[CANVAS_BUF_SIZE];
    memcpy(buf_copy, buf, sizeof(buf_copy));

    const uint32_t stride = lv_draw_buf_width_to_stride(CANVAS_SIZE, CANVAS_COLOR_FORMAT);
    lv_draw_sw_rotate(buf_copy, buf, CANVAS_SIZE, CANVAS_SIZE, stride, stride,
                      LV_DISPLAY_ROTATION_90, CANVAS_COLOR_FORMAT);
}

// 28px wide overall: 25px body, 3px terminal nub. The upstream 33px glyph does not
// leave room for two batteries plus the connection symbol in one 88px row.
void draw_battery_at(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y, uint8_t level,
                     bool charging) {
    lv_draw_rect_dsc_t rect_black_dsc;
    init_rect_dsc(&rect_black_dsc, LVGL_BACKGROUND);
    lv_draw_rect_dsc_t rect_white_dsc;
    init_rect_dsc(&rect_white_dsc, LVGL_FOREGROUND);

    canvas_draw_rect(canvas, x, y + 2, 25, 12, &rect_white_dsc);
    canvas_draw_rect(canvas, x + 1, y + 3, 23, 10, &rect_black_dsc);
    canvas_draw_rect(canvas, x + 2, y + 4, level * 21 / 100, 8, &rect_white_dsc);
    canvas_draw_rect(canvas, x + 25, y + 5, 3, 6, &rect_white_dsc);
    canvas_draw_rect(canvas, x + 26, y + 6, 1, 4, &rect_black_dsc);

    if (charging) {
        lv_draw_image_dsc_t img_dsc;
        lv_draw_image_dsc_init(&img_dsc);
        canvas_draw_img(canvas, x + 7, y - 1, &bolt, &img_dsc);
    }
}

void init_label_dsc(lv_draw_label_dsc_t *label_dsc, lv_color_t color, const lv_font_t *font,
                    lv_text_align_t align) {
    lv_draw_label_dsc_init(label_dsc);
    label_dsc->color = color;
    label_dsc->font = font;
    label_dsc->align = align;
}

void init_rect_dsc(lv_draw_rect_dsc_t *rect_dsc, lv_color_t bg_color) {
    lv_draw_rect_dsc_init(rect_dsc);
    rect_dsc->bg_color = bg_color;
}

void init_line_dsc(lv_draw_line_dsc_t *line_dsc, lv_color_t color, uint8_t width) {
    lv_draw_line_dsc_init(line_dsc);
    line_dsc->color = color;
    line_dsc->width = width;
}

void init_arc_dsc(lv_draw_arc_dsc_t *arc_dsc, lv_color_t color, uint8_t width) {
    lv_draw_arc_dsc_init(arc_dsc);
    arc_dsc->color = color;
    arc_dsc->width = width;
}

void canvas_draw_line(lv_obj_t *canvas, const lv_point_t points[], uint32_t point_cnt,
                      lv_draw_line_dsc_t *draw_dsc) {
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);

    for (uint32_t i = 1; i < point_cnt; ++i) {
        draw_dsc->p1.x = points[i - 1].x;
        draw_dsc->p1.y = points[i - 1].y;
        draw_dsc->p2.x = points[i].x;
        draw_dsc->p2.y = points[i].y;
        lv_draw_line(&layer, draw_dsc);
    }

    lv_canvas_finish_layer(canvas, &layer);
}
void canvas_draw_rect(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h,
                      lv_draw_rect_dsc_t *draw_dsc) {
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);

    lv_area_t coords = {x, y, x + w - 1, y + h - 1};
    lv_draw_rect(&layer, draw_dsc, &coords);

    lv_canvas_finish_layer(canvas, &layer);
}

void canvas_draw_arc(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y, lv_coord_t r,
                     int32_t start_angle, int32_t end_angle, lv_draw_arc_dsc_t *draw_dsc) {
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);

    draw_dsc->center.x = x;
    draw_dsc->center.y = y;
    draw_dsc->radius = r;
    draw_dsc->start_angle = start_angle;
    draw_dsc->end_angle = end_angle;
    lv_draw_arc(&layer, draw_dsc);

    lv_canvas_finish_layer(canvas, &layer);
}

void canvas_draw_text(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y, lv_coord_t max_w,
                      lv_draw_label_dsc_t *draw_dsc, const char *txt) {
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);

    draw_dsc->text = txt;
    lv_area_t coords = {x, y, x + max_w, y + CANVAS_SIZE};
    lv_draw_label(&layer, draw_dsc, &coords);

    lv_canvas_finish_layer(canvas, &layer);
}

void canvas_draw_img(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y, const lv_image_dsc_t *src,
                     lv_draw_image_dsc_t *draw_dsc) {
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);

    draw_dsc->src = src;
    lv_area_t coords = {x, y, x + src->header.w - 1, y + src->header.h - 1};
    lv_draw_image(&layer, draw_dsc, &coords);

    lv_canvas_finish_layer(canvas, &layer);
}

static const char *connection_symbol(const struct status_state *state) {
#if IS_ENABLED(CONFIG_ZMK_SPLIT) && !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    // Everything forwarded from the central is stale the moment the link drops, so a
    // disconnected peripheral says so rather than showing the central's last endpoint.
    if (!state->connected) {
        return LV_SYMBOL_CLOSE;
    }
#endif

    switch (state->selected_endpoint.transport) {
    case ZMK_TRANSPORT_USB:
        return LV_SYMBOL_USB;
    case ZMK_TRANSPORT_BLE:
        if (!state->active_profile_bonded) {
            return LV_SYMBOL_SETTINGS;
        }
        return state->active_profile_connected ? LV_SYMBOL_WIFI : LV_SYMBOL_CLOSE;
    case ZMK_TRANSPORT_NONE:
    default:
        return LV_SYMBOL_CLOSE;
    }
}

void draw_status_row(lv_obj_t *canvas, const struct status_state *state) {
    lv_draw_label_dsc_t label_dsc;
    init_label_dsc(&label_dsc, LVGL_FOREGROUND, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);

    lv_canvas_fill_bg(canvas, LVGL_BACKGROUND, LV_OPA_COVER);

    // This half on the left, the other half on the right.
    draw_battery_at(canvas, STATUS_BATTERY_LEFT_X, 0, state->battery, state->charging);

    if (state->peer_battery_valid) {
        draw_battery_at(canvas, STATUS_BATTERY_RIGHT_X, 0, state->peer_battery, false);
    }

    canvas_draw_text(canvas, STATUS_SYMBOL_X, 0, STATUS_SYMBOL_W, &label_dsc,
                     connection_symbol(state));

    rotate_canvas(canvas);
}

void draw_layer_row(lv_obj_t *canvas, const struct status_state *state) {
    lv_draw_label_dsc_t label_dsc;
    init_label_dsc(&label_dsc, LVGL_FOREGROUND, &lv_font_montserrat_48, LV_TEXT_ALIGN_CENTER);

    lv_canvas_fill_bg(canvas, LVGL_BACKGROUND, LV_OPA_COVER);

    char text[4];
    snprintf(text, sizeof(text), "%d", state->layer_index);
    canvas_draw_text(canvas, 0, 0, CANVAS_SIZE, &label_dsc, text);

    rotate_canvas(canvas);
}

void draw_wpm_row(lv_obj_t *canvas, const struct status_state *state) {
    lv_draw_label_dsc_t label_dsc;
    init_label_dsc(&label_dsc, LVGL_FOREGROUND, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);

    lv_canvas_fill_bg(canvas, LVGL_BACKGROUND, LV_OPA_COVER);

    char text[12];
    snprintf(text, sizeof(text), "WPM: %d", state->wpm_value);
    canvas_draw_text(canvas, 0, 0, CANVAS_SIZE, &label_dsc, text);

    rotate_canvas(canvas);
}
