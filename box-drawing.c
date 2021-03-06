#include "box-drawing.h"

#include <stdio.h>
#include <errno.h>

#define LOG_MODULE "box-drawing"
#define LOG_ENABLE_DBG 0
#include "log.h"
#include "config.h"
#include "macros.h"
#include "stride.h"
#include "terminal.h"
#include "util.h"
#include "xmalloc.h"

enum thickness {
    LIGHT,
    HEAVY,
};

struct buf {
    uint8_t *data;
    pixman_image_t *pix;
    int width;
    int height;
    int stride;
    int dpi;
    float cell_size;
    float base_thickness;
    bool solid_shades;
    int thickness[2];
};

static const pixman_color_t white = {0xffff, 0xffff, 0xffff, 0xffff};

static void
change_buffer_format(struct buf *buf, pixman_format_code_t new_format)
{
    int stride = stride_for_format_and_width(new_format, buf->width);
    uint8_t *new_data = xcalloc(buf->height * stride, 1);
    pixman_image_t *new_pix = pixman_image_create_bits_no_clear(
        new_format, buf->width, buf->height, (uint32_t *)new_data, stride);

    if (new_pix == NULL) {
        errno = ENOMEM;
        perror(__func__);
        abort();
    }

    pixman_image_unref(buf->pix);
    free(buf->data);

    buf->data = new_data;
    buf->pix = new_pix;
    buf->stride = stride;
}

static int NOINLINE
_thickness(struct buf *buf, enum thickness thick)
{
    int multiplier = thick * 2 + 1;

    xassert((thick == LIGHT && multiplier == 1) ||
            (thick == HEAVY && multiplier == 3));

    return
        max(
            (int)(buf->base_thickness * buf->dpi / 72.0 * buf->cell_size), 1)
        * multiplier;
}
#define thickness(thick) buf->thickness[thick]

static void NOINLINE
_hline(struct buf *buf, int x1, int x2, int y, int thick)
{
    pixman_box32_t box = {
        .x1 = min(max(x1, 0), buf->width),
        .x2 = min(max(x2, 0), buf->width),
        .y1 = min(max(y, 0), buf->height),
        .y2 = min(max(y + thick, 0), buf->height),
    };
    pixman_image_fill_boxes(PIXMAN_OP_SRC, buf->pix, &white, 1, &box);
}

#define hline(x1, x2, y, thick) _hline(buf, x1, x2, y, thick)

static void NOINLINE
_vline(struct buf *buf, int y1, int y2, int x, int thick)
{
    pixman_box32_t box = {
        .x1 = min(max(x, 0), buf->width),
        .x2 = min(max(x + thick, 0), buf->width),
        .y1 = min(max(y1, 0), buf->height),
        .y2 = min(max(y2, 0), buf->height),
    };
    pixman_image_fill_boxes(PIXMAN_OP_SRC, buf->pix, &white, 1, &box);
}

#define vline(y1, y2, x, thick) _vline(buf, y1, y2, x, thick)

static void NOINLINE
_rect(struct buf *buf, int x1, int y1, int x2, int y2)
{
    pixman_box32_t box = {
        .x1 = min(max(x1, 0), buf->width),
        .y1 = min(max(y1, 0), buf->height),
        .x2 = min(max(x2, 0), buf->width),
        .y2 = min(max(y2, 0), buf->height),
    };
    pixman_image_fill_boxes(PIXMAN_OP_SRC, buf->pix, &white, 1, &box);
}

#define rect(x1, y1, x2, y2) _rect(buf, x1, y1, x2, y2)

static void NOINLINE
_hline_middle(struct buf *buf, enum thickness _thick)
{
    int thick = thickness(_thick);
    hline(0, buf->width, (buf->height - thick) / 2, thick);
}

static void NOINLINE
_hline_middle_left(struct buf *buf, enum thickness _vthick, enum thickness _hthick)
{
    int vthick = thickness(_vthick);
    int hthick = thickness(_hthick);
    _hline(buf, 0, (buf->width + vthick) / 2, (buf->height - hthick) / 2, hthick);
}

static void NOINLINE
_hline_middle_right(struct buf *buf, enum thickness _vthick, enum thickness _hthick)
{
    int vthick = thickness(_vthick);
    int hthick = thickness(_hthick);
    hline((buf->width - vthick) / 2, buf->width, (buf->height - hthick) / 2, hthick);
}

static void NOINLINE
_vline_middle(struct buf *buf, enum thickness _thick)
{
    int thick = thickness(_thick);
    vline(0, buf->height, (buf->width - thick) / 2, thick);
}

static void NOINLINE
_vline_middle_up(struct buf *buf, enum thickness _vthick, enum thickness _hthick)
{
    int vthick = thickness(_vthick);
    int hthick = thickness(_hthick);
    vline(0, (buf->height + hthick) / 2, (buf->width - vthick) / 2, vthick);
}

static void NOINLINE
_vline_middle_down(struct buf *buf, enum thickness _vthick, enum thickness _hthick)
{
    int vthick = thickness(_vthick);
    int hthick = thickness(_hthick);
    vline((buf->height - hthick) / 2, buf->height, (buf->width - vthick) / 2, vthick);
}

#define hline_middle(thick) _hline_middle(buf, thick)
#define hline_middle_left(thick) _hline_middle_left(buf, thick, thick)
#define hline_middle_right(thick) _hline_middle_right(buf, thick, thick)
#define hline_middle_left_mixed(vthick, hthick) _hline_middle_left(buf, vthick, hthick)
#define hline_middle_right_mixed(vthick, hthick) _hline_middle_right(buf, vthick, hthick)
#define vline_middle(thick) _vline_middle(buf, thick)
#define vline_middle_up(thick) _vline_middle_up(buf, thick, thick)
#define vline_middle_down(thick) _vline_middle_down(buf, thick, thick)
#define vline_middle_up_mixed(vthick, hthick) _vline_middle_up(buf, vthick, hthick)
#define vline_middle_down_mixed(vthick, hthick) _vline_middle_down(buf, vthick, hthick)

static void
draw_box_drawings_light_horizontal(struct buf *buf)
{
    hline_middle(LIGHT);
}

static void
draw_box_drawings_heavy_horizontal(struct buf *buf)
{
    hline_middle(HEAVY);
}

static void
draw_box_drawings_light_vertical(struct buf *buf)
{
    vline_middle(LIGHT);
}

static void
draw_box_drawings_heavy_vertical(struct buf *buf)
{
    vline_middle(HEAVY);
}

static void
draw_box_drawings_dash_horizontal(struct buf *buf, int count, int thick, int gap)
{
    int width = buf->width;
    int height = buf->height;

    xassert(count >= 2 && count <= 4);
    const int gap_count = count - 1;

    int dash_width = (width - (gap_count * gap)) / count;
    while (dash_width <= 0 && gap > 1) {
        gap--;
        dash_width = (width - (gap_count * gap)) / count;
    }

    if (dash_width <= 0) {
        hline_middle(LIGHT);
        return;
    }

    xassert(count * dash_width + gap_count * gap <= width);

    int remaining = width - count * dash_width - gap_count * gap;

    int x[4] = {0};
    int w[4] = {dash_width, dash_width, dash_width, dash_width};

    x[0] = 0;

    x[1] = x[0] + w[0] + gap;
    if (count == 2)
        w[1] = width - x[1];
    else if (count == 3)
        w[1] += remaining;
    else
        w[1] += remaining / 2;

    if (count >= 3) {
        x[2] = x[1] + w[1] + gap;
        if (count == 3)
            w[2] = width - x[2];
        else
            w[2] += remaining - remaining / 2;
    }

    if (count >= 4) {
        x[3] = x[2] + w[2] + gap;
        w[3] = width - x[3];
    }

    hline(x[0], x[0] + w[0], (height - thick) / 2, thick);
    hline(x[1], x[1] + w[1], (height - thick) / 2, thick);
    if (count >= 3)
        hline(x[2], x[2] + w[2], (height - thick) / 2, thick);
    if (count >= 4)
        hline(x[3], x[3] + w[3], (height - thick) / 2, thick);
}

static void
draw_box_drawings_dash_vertical(struct buf *buf, int count, int thick, int gap)
{
    int width = buf->width;
    int height = buf->height;

    xassert(count >= 2 && count <= 4);
    const int gap_count = count - 1;

    int dash_height = (height - (gap_count * gap)) / count;
    while (dash_height <= 0 && gap > 1) {
        gap--;
        dash_height = (height - (gap_count * gap)) / count;
    }

    if (dash_height <= 0) {
        vline_middle(LIGHT);
        return;
    }

    xassert(count * dash_height + gap_count * gap <= height);

    int remaining = height - count * dash_height - gap_count * gap;

    int y[4] = {0};
    int h[4] = {dash_height, dash_height, dash_height, dash_height};

    y[0] = 0;

    y[1] = y[0] + h[0] + gap;
    if (count == 2)
        h[1] = height - y[1];
    else if (count == 3)
        h[1] += remaining;
    else
        h[1] += remaining / 2;

    if (count >= 3) {
        y[2] = y[1] + h[1] + gap;
        if (count == 3)
            h[2] = height - y[2];
        else
            h[2] += remaining - remaining / 2;
    }

    if (count >= 4) {
        y[3] = y[2] + h[2] + gap;
        h[3] = height - y[3];
    }

    vline(y[0], y[0] + h[0], (width - thick) / 2, thick);
    vline(y[1], y[1] + h[1], (width - thick) / 2, thick);
    if (count >= 3)
        vline(y[2], y[2] + h[2], (width - thick) / 2, thick);
    if (count >= 4)
        vline(y[3], y[3] + h[3], (width - thick) / 2, thick);
}

static void
draw_box_drawings_light_triple_dash_horizontal(struct buf *buf)
{
    draw_box_drawings_dash_horizontal(buf, 3, thickness(LIGHT), thickness(LIGHT));
}

static void
draw_box_drawings_heavy_triple_dash_horizontal(struct buf *buf)
{
    draw_box_drawings_dash_horizontal(buf, 3, thickness(HEAVY), thickness(LIGHT));
}

static void
draw_box_drawings_light_triple_dash_vertical(struct buf *buf)
{
    draw_box_drawings_dash_vertical(buf, 3, thickness(LIGHT), thickness(HEAVY));
}

static void
draw_box_drawings_heavy_triple_dash_vertical(struct buf *buf)
{
    draw_box_drawings_dash_vertical(buf, 3, thickness(HEAVY), thickness(HEAVY));
}

static void
draw_box_drawings_light_quadruple_dash_horizontal(struct buf *buf)
{
    draw_box_drawings_dash_horizontal(buf, 4, thickness(LIGHT), thickness(LIGHT));
}

static void
draw_box_drawings_heavy_quadruple_dash_horizontal(struct buf *buf)
{
    draw_box_drawings_dash_horizontal(buf, 4, thickness(HEAVY), thickness(LIGHT));
}

static void
draw_box_drawings_light_quadruple_dash_vertical(struct buf *buf)
{
    draw_box_drawings_dash_vertical(buf, 4, thickness(LIGHT), thickness(LIGHT));
}

static void
draw_box_drawings_heavy_quadruple_dash_vertical(struct buf *buf)
{
    draw_box_drawings_dash_vertical(buf, 4, thickness(HEAVY), thickness(LIGHT));
}

static void
draw_box_drawings_light_down_and_right(struct buf *buf)
{
    hline_middle_right(LIGHT);
    vline_middle_down(LIGHT);
}

static void
draw_box_drawings_down_light_and_right_heavy(struct buf *buf)
{
    hline_middle_right_mixed(LIGHT, HEAVY);
    vline_middle_down(LIGHT);
}

static void
draw_box_drawings_down_heavy_and_right_light(struct buf *buf)
{
    hline_middle_right(LIGHT);
    vline_middle_down_mixed(HEAVY, LIGHT);
}

static void
draw_box_drawings_heavy_down_and_right(struct buf *buf)
{
    hline_middle_right(HEAVY);
    vline_middle_down(HEAVY);
}

static void
draw_box_drawings_light_down_and_left(struct buf *buf)
{
    hline_middle_left(LIGHT);
    vline_middle_down(LIGHT);
}

static void
draw_box_drawings_down_light_and_left_heavy(struct buf *buf)
{
    hline_middle_left_mixed(LIGHT, HEAVY);
    vline_middle_down(LIGHT);
}

static void
draw_box_drawings_down_heavy_and_left_light(struct buf *buf)
{
    hline_middle_left(LIGHT);
    vline_middle_down_mixed(HEAVY, LIGHT);
}

static void
draw_box_drawings_heavy_down_and_left(struct buf *buf)
{
    hline_middle_left(HEAVY);
    vline_middle_down(HEAVY);
}

static void
draw_box_drawings_light_up_and_right(struct buf *buf)
{
    hline_middle_right(LIGHT);
    vline_middle_up(LIGHT);
}

static void
draw_box_drawings_up_light_and_right_heavy(struct buf *buf)
{
    hline_middle_right_mixed(LIGHT, HEAVY);
    vline_middle_up(LIGHT);
}

static void
draw_box_drawings_up_heavy_and_right_light(struct buf *buf)
{
    hline_middle_right(LIGHT);
    vline_middle_up_mixed(HEAVY, LIGHT);
}

static void
draw_box_drawings_heavy_up_and_right(struct buf *buf)
{
    hline_middle_right(HEAVY);
    vline_middle_up(HEAVY);
}

static void
draw_box_drawings_light_up_and_left(struct buf *buf)
{
    hline_middle_left(LIGHT);
    vline_middle_up(LIGHT);
}

static void
draw_box_drawings_up_light_and_left_heavy(struct buf *buf)
{
    hline_middle_left_mixed(LIGHT, HEAVY);
    vline_middle_up(LIGHT);
}

static void
draw_box_drawings_up_heavy_and_left_light(struct buf *buf)
{
    hline_middle_left(LIGHT);
    vline_middle_up_mixed(HEAVY, LIGHT);
}

static void
draw_box_drawings_heavy_up_and_left(struct buf *buf)
{
    hline_middle_left(HEAVY);
    vline_middle_up(HEAVY);
}

static void
draw_box_drawings_light_vertical_and_right(struct buf *buf)
{
    hline_middle_right(LIGHT);
    vline_middle(LIGHT);
}

static void
draw_box_drawings_vertical_light_and_right_heavy(struct buf *buf)
{
    hline_middle_right_mixed(LIGHT, HEAVY);
    vline_middle(LIGHT);
}

static void
draw_box_drawings_up_heavy_and_right_down_light(struct buf *buf)
{
    hline_middle_right(LIGHT);
    vline_middle_up_mixed(HEAVY, LIGHT);
    vline_middle_down(LIGHT);
}

static void
draw_box_drawings_down_heavy_and_right_up_light(struct buf *buf)
{
    hline_middle_right(LIGHT);
    vline_middle_up(LIGHT);
    vline_middle_down_mixed(HEAVY, LIGHT);
}

static void
draw_box_drawings_vertical_heavy_and_right_light(struct buf *buf)
{
    hline_middle_right(LIGHT);
    vline_middle(HEAVY);
}

static void
draw_box_drawings_down_light_and_right_up_heavy(struct buf *buf)
{
    hline_middle_right(HEAVY);
    vline_middle_up(HEAVY);
    vline_middle_down(LIGHT);
}

static void
draw_box_drawings_up_light_and_right_down_heavy(struct buf *buf)
{
    hline_middle_right(HEAVY);
    vline_middle_up(LIGHT);
    vline_middle_down(HEAVY);
}

static void
draw_box_drawings_heavy_vertical_and_right(struct buf *buf)
{
    hline_middle_right(HEAVY);
    vline_middle(HEAVY);
}

static void
draw_box_drawings_light_vertical_and_left(struct buf *buf)
{
    hline_middle_left(LIGHT);
    vline_middle(LIGHT);
}

static void
draw_box_drawings_vertical_light_and_left_heavy(struct buf *buf)
{
    hline_middle_left_mixed(LIGHT, HEAVY);
    vline_middle(LIGHT);
}

static void
draw_box_drawings_up_heavy_and_left_down_light(struct buf *buf)
{
    hline_middle_left(LIGHT);
    vline_middle_up_mixed(HEAVY, LIGHT);
    vline_middle_down(LIGHT);
}

static void
draw_box_drawings_down_heavy_and_left_up_light(struct buf *buf)
{
    hline_middle_left(LIGHT);
    vline_middle_up(LIGHT);
    vline_middle_down_mixed(HEAVY, LIGHT);
}

static void
draw_box_drawings_vertical_heavy_and_left_light(struct buf *buf)
{
    hline_middle_left(LIGHT);
    vline_middle(HEAVY);
}

static void
draw_box_drawings_down_light_and_left_up_heavy(struct buf *buf)
{
    hline_middle_left(HEAVY);
    vline_middle_up(HEAVY);
    vline_middle_down(LIGHT);
}

static void
draw_box_drawings_up_light_and_left_down_heavy(struct buf *buf)
{
    hline_middle_left(HEAVY);
    vline_middle_up(LIGHT);
    vline_middle_down(HEAVY);
}

static void
draw_box_drawings_heavy_vertical_and_left(struct buf *buf)
{
    hline_middle_left(HEAVY);
    vline_middle(HEAVY);
}

static void
draw_box_drawings_light_down_and_horizontal(struct buf *buf)
{
    hline_middle(LIGHT);
    vline_middle_down(LIGHT);
}

static void
draw_box_drawings_left_heavy_and_right_down_light(struct buf *buf)
{
    hline_middle_left_mixed(LIGHT, HEAVY);
    hline_middle_right(LIGHT);
    vline_middle_down(LIGHT);
}

static void
draw_box_drawings_right_heavy_and_left_down_light(struct buf *buf)
{
    hline_middle_left(LIGHT);
    hline_middle_right_mixed(LIGHT, HEAVY);
    vline_middle_down(LIGHT);
}

static void
draw_box_drawings_down_light_and_horizontal_heavy(struct buf *buf)
{
    hline_middle(HEAVY);
    vline_middle_down(LIGHT);
}

static void
draw_box_drawings_down_heavy_and_horizontal_light(struct buf *buf)
{
    hline_middle(LIGHT);
    vline_middle_down_mixed(HEAVY, LIGHT);
}

static void
draw_box_drawings_right_light_and_left_down_heavy(struct buf *buf)
{
    hline_middle_left(HEAVY);
    hline_middle_right(LIGHT);
    vline_middle_down(HEAVY);
}

static void
draw_box_drawings_left_light_and_right_down_heavy(struct buf *buf)
{
    hline_middle_left(LIGHT);
    hline_middle_right(HEAVY);
    vline_middle_down(HEAVY);
}

static void
draw_box_drawings_heavy_down_and_horizontal(struct buf *buf)
{
    hline_middle(HEAVY);
    vline_middle_down(HEAVY);
}

static void
draw_box_drawings_light_up_and_horizontal(struct buf *buf)
{
    hline_middle(LIGHT);
    vline_middle_up(LIGHT);
}

static void
draw_box_drawings_left_heavy_and_right_up_light(struct buf *buf)
{
    hline_middle_left_mixed(LIGHT, HEAVY);
    hline_middle_right(LIGHT);
    vline_middle_up(LIGHT);
}

static void
draw_box_drawings_right_heavy_and_left_up_light(struct buf *buf)
{
    hline_middle_left(LIGHT);
    hline_middle_right_mixed(LIGHT, HEAVY);
    vline_middle_up(LIGHT);
}

static void
draw_box_drawings_up_light_and_horizontal_heavy(struct buf *buf)
{
    hline_middle(HEAVY);
    vline_middle_up(LIGHT);
}

static void
draw_box_drawings_up_heavy_and_horizontal_light(struct buf *buf)
{
    hline_middle(LIGHT);
    vline_middle_up_mixed(HEAVY, LIGHT);
}

static void
draw_box_drawings_right_light_and_left_up_heavy(struct buf *buf)
{
    hline_middle_left(HEAVY);
    hline_middle_right(LIGHT);
    vline_middle_up(HEAVY);
}

static void
draw_box_drawings_left_light_and_right_up_heavy(struct buf *buf)
{
    hline_middle_left(LIGHT);
    hline_middle_right(HEAVY);
    vline_middle_up(HEAVY);
}

static void
draw_box_drawings_heavy_up_and_horizontal(struct buf *buf)
{
    hline_middle(HEAVY);
    vline_middle_up(HEAVY);
}

static void
draw_box_drawings_light_vertical_and_horizontal(struct buf *buf)
{
    hline_middle(LIGHT);
    vline_middle(LIGHT);
}

static void
draw_box_drawings_left_heavy_and_right_vertical_light(struct buf *buf)
{
    hline_middle_left_mixed(LIGHT, HEAVY);
    hline_middle_right(LIGHT);
    vline_middle(LIGHT);
}

static void
draw_box_drawings_right_heavy_and_left_vertical_light(struct buf *buf)
{
    hline_middle_left(LIGHT);
    hline_middle_right_mixed(LIGHT, HEAVY);
    vline_middle(LIGHT);
}

static void
draw_box_drawings_vertical_light_and_horizontal_heavy(struct buf *buf)
{
    hline_middle(HEAVY);
    vline_middle(LIGHT);
}

static void
draw_box_drawings_up_heavy_and_down_horizontal_light(struct buf *buf)
{
    hline_middle(LIGHT);
    vline_middle_up_mixed(HEAVY, LIGHT);
    vline_middle_down(LIGHT);
}

static void
draw_box_drawings_down_heavy_and_up_horizontal_light(struct buf *buf)
{
    hline_middle(LIGHT);
    vline_middle_up(LIGHT);
    vline_middle_down_mixed(HEAVY, LIGHT);
}

static void
draw_box_drawings_vertical_heavy_and_horizontal_light(struct buf *buf)
{
    hline_middle(LIGHT);
    vline_middle(HEAVY);
}

static void
draw_box_drawings_left_up_heavy_and_right_down_light(struct buf *buf)
{
    hline_middle_left(HEAVY);
    hline_middle_right(LIGHT);
    vline_middle_up(HEAVY);
    vline_middle_down(LIGHT);
}

static void
draw_box_drawings_right_up_heavy_and_left_down_light(struct buf *buf)
{
    hline_middle_left(LIGHT);
    hline_middle_right(HEAVY);
    vline_middle_up(HEAVY);
    vline_middle_down(LIGHT);
}

static void
draw_box_drawings_left_down_heavy_and_right_up_light(struct buf *buf)
{
    hline_middle_left(HEAVY);
    hline_middle_right(LIGHT);
    vline_middle_up(LIGHT);
    vline_middle_down(HEAVY);
}

static void
draw_box_drawings_right_down_heavy_and_left_up_light(struct buf *buf)
{
    hline_middle_left(LIGHT);
    hline_middle_right(HEAVY);
    vline_middle_up(LIGHT);
    vline_middle_down(HEAVY);
}

static void
draw_box_drawings_down_light_and_up_horizontal_heavy(struct buf *buf)
{
    hline_middle(HEAVY);
    vline_middle_up(HEAVY);
    vline_middle_down(LIGHT);
}

static void
draw_box_drawings_up_light_and_down_horizontal_heavy(struct buf *buf)
{
    hline_middle(HEAVY);
    vline_middle_up(LIGHT);
    vline_middle_down(HEAVY);
}

static void
draw_box_drawings_right_light_and_left_vertical_heavy(struct buf *buf)
{
    hline_middle_left(HEAVY);
    hline_middle_right(LIGHT);
    vline_middle(HEAVY);
}

static void
draw_box_drawings_left_light_and_right_vertical_heavy(struct buf *buf)
{
    hline_middle_left(LIGHT);
    hline_middle_right(HEAVY);
    vline_middle(HEAVY);
}

static void
draw_box_drawings_heavy_vertical_and_horizontal(struct buf *buf)
{
    hline_middle(HEAVY);
    vline_middle(HEAVY);
}

static void
draw_box_drawings_light_double_dash_horizontal(struct buf *buf)
{
    draw_box_drawings_dash_horizontal(buf, 2, thickness(LIGHT), thickness(LIGHT));
}

static void
draw_box_drawings_heavy_double_dash_horizontal(struct buf *buf)
{
    draw_box_drawings_dash_horizontal(buf, 2, thickness(HEAVY), thickness(LIGHT));
}

static void
draw_box_drawings_light_double_dash_vertical(struct buf *buf)
{
    draw_box_drawings_dash_vertical(buf, 2, thickness(LIGHT), thickness(HEAVY));
}

static void
draw_box_drawings_heavy_double_dash_vertical(struct buf *buf)
{
    draw_box_drawings_dash_vertical(buf, 2, thickness(HEAVY), thickness(HEAVY));
}

static void
draw_box_drawings_double_horizontal(struct buf *buf)
{
    int thick = thickness(LIGHT);
    int mid = (buf->height - thick * 3) / 2;

    hline(0, buf->width, mid, thick);
    hline(0, buf->width, mid + 2 * thick, thick);
}

static void
draw_box_drawings_double_vertical(struct buf *buf)
{
    int thick = thickness(LIGHT);
    int mid = (buf->width - thick * 3) / 2;

    vline(0, buf->height, mid, thick);
    vline(0, buf->height, mid + 2 * thick, thick);
}

static void
draw_box_drawings_down_single_and_right_double(struct buf *buf)
{
    int thick = thickness(LIGHT);
    int hmid = (buf->height - thick * 3) / 2;
    int vmid = (buf->width - thick) / 2;

    vline_middle_down(LIGHT);

    hline(vmid, buf->width, hmid, thick);
    hline(vmid, buf->width, hmid + 2 * thick, thick);
}

static void
draw_box_drawings_down_double_and_right_single(struct buf *buf)
{
    int thick = thickness(LIGHT);
    int hmid = (buf->height - thick) / 2;
    int vmid = (buf->width - thick * 3) / 2;

    hline_middle_right(LIGHT);

    vline(hmid, buf->height, vmid, thick);
    vline(hmid, buf->height, vmid + 2 * thick, thick);
}

static void
draw_box_drawings_double_down_and_right(struct buf *buf)
{
    int thick = thickness(LIGHT);
    int hmid = (buf->height - thick * 3) / 2;
    int vmid = (buf->width - thick * 3) / 2;

    vline(hmid, buf->height, vmid, thick);
    vline(hmid + 2 * thick, buf->height, vmid + 2 * thick, thick);

    hline(vmid, buf->width, hmid, thick);
    hline(vmid + 2 * thick, buf->width, hmid + 2 * thick, thick);
}

static void
draw_box_drawings_down_single_and_left_double(struct buf *buf)
{
    int thick = thickness(LIGHT);
    int hmid = (buf->height - thick * 3) / 2;
    int vmid = (buf->width + thick) / 2;

    vline_middle_down(LIGHT);

    hline(0, vmid, hmid, thick);
    hline(0, vmid, hmid + 2 * thick, thick);
}

static void
draw_box_drawings_down_double_and_left_single(struct buf *buf)
{
    int thick = thickness(LIGHT);
    int hmid = (buf->height - thick) / 2;
    int vmid = (buf->width - thick * 3) / 2;

    hline_middle_left(LIGHT);

    vline(hmid, buf->height, vmid, thick);
    vline(hmid, buf->height, vmid + 2 * thick, thick);
}

static void
draw_box_drawings_double_down_and_left(struct buf *buf)
{
    int thick = thickness(LIGHT);
    int hmid = (buf->height - thick * 3) / 2;
    int vmid = (buf->width - thick * 3) / 2;

    vline(hmid + 2 * thick, buf->height, vmid, thick);
    vline(hmid, buf->height, vmid + 2 * thick, thick);

    hline(0, vmid + 2 * thick, hmid, thick);
    hline(0, vmid, hmid + 2 * thick, thick);
}

static void
draw_box_drawings_up_single_and_right_double(struct buf *buf)
{
    int thick = thickness(LIGHT);
    int hmid = (buf->height - thick * 3) / 2;
    int vmid = (buf->width - thick) / 2;

    vline_middle_up(LIGHT);

    hline(vmid, buf->width, hmid, thick);
    hline(vmid, buf->width, hmid + 2 * thick, thick);
}

static void
draw_box_drawings_up_double_and_right_single(struct buf *buf)
{
    int thick = thickness(LIGHT);
    int hmid = (buf->height + thick) / 2;
    int vmid = (buf->width - thick * 3) / 2;

    hline_middle_right(LIGHT);

    vline(0, hmid, vmid, thick);
    vline(0, hmid, vmid + 2 * thick, thick);
}

static void
draw_box_drawings_double_up_and_right(struct buf *buf)
{
    int thick = thickness(LIGHT);
    int hmid = (buf->height - thick * 3) / 2;
    int vmid = (buf->width - thick * 3) / 2;

    vline(0, hmid + 2 * thick, vmid, thick);
    vline(0, hmid, vmid + 2 * thick, thick);

    hline(vmid + 2 * thick, buf->width, hmid, thick);
    hline(vmid, buf->width, hmid + 2 * thick, thick);
}

static void
draw_box_drawings_up_single_and_left_double(struct buf *buf)
{
    int thick = thickness(LIGHT);
    int hmid = (buf->height - thick * 3) / 2;
    int vmid = (buf->width + thick) / 2;

    vline_middle_up(LIGHT);

    hline(0, vmid, hmid, thick);
    hline(0, vmid, hmid + 2 * thick, thick);
}

static void
draw_box_drawings_up_double_and_left_single(struct buf *buf)
{
    int thick = thickness(LIGHT);
    int hmid = (buf->height + thick) / 2;
    int vmid = (buf->width - thick * 3) / 2;

    hline_middle_left(LIGHT);

    vline(0, hmid, vmid, thick);
    vline(0, hmid, vmid + 2 * thick, thick);
}

static void
draw_box_drawings_double_up_and_left(struct buf *buf)
{
    int thick = thickness(LIGHT);
    int hmid = (buf->height - thick * 3) / 2;
    int vmid = (buf->width - thick * 3) / 2;

    vline(0, hmid + 0 * thick + thick, vmid, thick);
    vline(0, hmid + 2 * thick + thick, vmid + 2 * thick, thick);

    hline(0, vmid, hmid, thick);
    hline(0, vmid + 2 * thick, hmid + 2 * thick, thick);
}

static void
draw_box_drawings_vertical_single_and_right_double(struct buf *buf)
{
    int thick = thickness(LIGHT);
    int hmid = (buf->height - thick * 3) / 2;
    int vmid = (buf->width - thick) / 2;

    vline_middle(LIGHT);

    hline(vmid, buf->width, hmid, thick);
    hline(vmid, buf->width, hmid + 2 * thick, thick);
}

static void
draw_box_drawings_vertical_double_and_right_single(struct buf *buf)
{
    int thick = thickness(LIGHT);
    int vmid = (buf->width - thick * 3) / 2;

    hline(vmid + 2 * thick, buf->width, (buf->height - thick) / 2, thick);

    vline(0, buf->height, vmid, thick);
    vline(0, buf->height, vmid + 2 * thick, thick);
}

static void
draw_box_drawings_double_vertical_and_right(struct buf *buf)
{
    int thick = thickness(LIGHT);
    int hmid = (buf->height - thick * 3) / 2;
    int vmid = (buf->width - thick * 3) / 2;

    vline(0, buf->height, vmid, thick);
    vline(0, hmid, vmid + 2 * thick, thick);
    vline(hmid + 2 * thick, buf->height, vmid + 2 * thick, thick);

    hline(vmid + 2 * thick, buf->width, hmid, thick);
    hline(vmid + 2 * thick, buf->width, hmid + 2 * thick, thick);
}

static void
draw_box_drawings_vertical_single_and_left_double(struct buf *buf)
{
    int thick = thickness(LIGHT);
    int hmid = (buf->height - thick * 3) / 2;
    int vmid = (buf->width + thick) / 2;

    vline_middle(LIGHT);

    hline(0, vmid, hmid, thick);
    hline(0, vmid, hmid + 2 * thick, thick);
}

static void
draw_box_drawings_vertical_double_and_left_single(struct buf *buf)
{
    int thick = thickness(LIGHT);
    int vmid = (buf->width - thick * 3) / 2;

    hline(0, vmid, (buf->height - thick) / 2, thick);

    vline(0, buf->height, vmid, thick);
    vline(0, buf->height, vmid + 2 * thick, thick);
}

static void
draw_box_drawings_double_vertical_and_left(struct buf *buf)
{
    int thick = thickness(LIGHT);
    int hmid = (buf->height - thick * 3) / 2;
    int vmid = (buf->width - thick * 3) / 2;

    vline(0, buf->height, vmid + 2 * thick, thick);
    vline(0, hmid, vmid, thick);
    vline(hmid + 2 * thick, buf->height, vmid, thick);

    hline(0, vmid + thick, hmid, thick);
    hline(0, vmid, hmid + 2 * thick, thick);
}

static void
draw_box_drawings_down_single_and_horizontal_double(struct buf *buf)
{
    int thick = thickness(LIGHT);
    int hmid = (buf->height - thick * 3) / 2;

    vline(hmid + 2 * thick, buf->height, (buf->width - thick) / 2, thick);

    hline(0, buf->width, hmid, thick);
    hline(0, buf->width, hmid + 2 * thick, thick);
}

static void
draw_box_drawings_down_double_and_horizontal_single(struct buf *buf)
{
    int thick = thickness(LIGHT);
    int hmid = (buf->height - thick) / 2;
    int vmid = (buf->width - thick * 3) / 2;

    hline_middle(LIGHT);

    vline(hmid, buf->height, vmid, thick);
    vline(hmid, buf->height, vmid + 2 * thick, thick);
}

static void
draw_box_drawings_double_down_and_horizontal(struct buf *buf)
{
    int thick = thickness(LIGHT);
    int hmid = (buf->height - thick * 3) / 2;
    int vmid = (buf->width - thick * 3) / 2;

    hline(0, buf->width, hmid, thick);
    hline(0, vmid, hmid + 2 * thick, thick);
    hline(vmid + 2 * thick, buf->width, hmid + 2 * thick, thick);

    vline(hmid + 2 * thick, buf->height, vmid, thick);
    vline(hmid + 2 * thick, buf->height, vmid + 2 * thick, thick);
}

static void
draw_box_drawings_up_single_and_horizontal_double(struct buf *buf)
{
    int thick = thickness(LIGHT);
    int hmid = (buf->height - thick * 3) / 2;
    int vmid = (buf->width - thick) / 2;

    vline(0, hmid, vmid, thick);

    hline(0, buf->width, hmid, thick);
    hline(0, buf->width, hmid + 2 * thick, thick);
}

static void
draw_box_drawings_up_double_and_horizontal_single(struct buf *buf)
{
    int thick = thickness(LIGHT);
    int hmid = (buf->height - thick) / 2;
    int vmid = (buf->width - thick * 3) / 2;

    hline_middle(LIGHT);

    vline(0, hmid, vmid, thick);
    vline(0, hmid, vmid + 2 * thick, thick);
}

static void
draw_box_drawings_double_up_and_horizontal(struct buf *buf)
{
    int thick = thickness(LIGHT);
    int hmid = (buf->height - thick * 3) / 2;
    int vmid = (buf->width - thick * 3) / 2;

    vline(0, hmid, vmid, thick);
    vline(0, hmid, vmid + 2 * thick, thick);

    hline(0, vmid + thick, hmid, thick);
    hline(vmid + 2 * thick, buf->width, hmid, thick);
    hline(0, buf->width, hmid + 2 * thick, thick);
}

static void
draw_box_drawings_vertical_single_and_horizontal_double(struct buf *buf)
{
    int thick = thickness(LIGHT);
    int hmid = (buf->height - thick * 3) / 2;

    vline_middle(LIGHT);

    hline(0, buf->width, hmid, thick);
    hline(0, buf->width, hmid + 2 * thick, thick);
}

static void
draw_box_drawings_vertical_double_and_horizontal_single(struct buf *buf)
{
    int thick = thickness(LIGHT);
    int vmid = (buf->width - thick * 3) / 2;

    hline_middle(LIGHT);

    vline(0, buf->height, vmid, thick);
    vline(0, buf->height, vmid + 2 * thick, thick);
}

static void
draw_box_drawings_double_vertical_and_horizontal(struct buf *buf)
{
    int thick = thickness(LIGHT);
    int hmid = (buf->height - thick * 3) / 2;
    int vmid = (buf->width - thick * 3) / 2;

    hline(0, vmid, hmid, thick);
    hline(vmid + 2 * thick, buf->width, hmid, thick);
    hline(0, vmid, hmid + 2 * thick, thick);
    hline(vmid + 2 * thick, buf->width, hmid + 2 * thick, thick);

    vline(0, hmid + thick, vmid, thick);
    vline(0, hmid, vmid + 2 * thick, thick);
    vline(hmid + 2 * thick, buf->height, vmid, thick);
    vline(hmid + 2 * thick, buf->height, vmid + 2 * thick, thick);
}

static void
draw_box_drawings_light_arc(struct buf *buf, wchar_t wc)
{
    const pixman_format_code_t fmt = pixman_image_get_format(buf->pix);
    const int supersample = fmt == PIXMAN_a8 ? 4 : 1;
    const int height = buf->height * supersample;
    const int width = buf->width * supersample;
    const int stride = fmt == PIXMAN_a8
        ? stride_for_format_and_width(PIXMAN_a8, width) : buf->stride;
    uint8_t *data = supersample > 1 ? xcalloc(height * stride, 1) : buf->data;

    const int thick = thickness(LIGHT) * supersample;

    const bool thick_is_odd = (thick / supersample) % 2;
    const bool height_is_odd = buf->height % 2;
    const bool width_is_odd = buf->width % 2;

    const double a = (width - thick) / 2;
    const double b = (height - thick) / 2;

    const double a2 = a * a;
    const double b2 = b * b;

    const int num_samples = height * 16;

    for (int i = 0; i < num_samples; i++) {
        double y = i / 16.;
        double x = sqrt(a2 * (1. - y * y / b2));

        const int row = round(y);
        const int col = round(x);

        if (col < 0)
            continue;

        int row_start = 0;
        int row_end = 0;
        int col_start = 0;
        int col_end = 0;

        /*
         * At this point, row/col is only correct for ???. For the other
         * arcs, we need to mirror the arc around either the x-, y- or
         * both axis.
         *
         * When doing so, we need to adjust for asymmetrical cell
         * dimensions.
         *
         * The amazing box drawing art below represents the lower part
         * of a cell, with the beginning of a vertical line in the
         * middle. Each column represents one pixel.
         *
         *
         *             Even cell            Odd cell
         *
         *             ???       ???           ???         ???
         *  Even line  ??? ???   ??? ???           ??? ???   ???   ???
         *             ??? ???   ??? ???           ??? ???   ???   ???
         *             ???????????????????????????           ?????????????????????????????????
         *
         *
         *             ???       ???           ???         ???
         *   Odd line  ??? ??? ???   ???           ???   ??? ???   ???
         *             ??? ??? ???   ???           ???   ??? ???   ???
         *             ???????????????????????????           ?????????????????????????????????
         *
         * As can be seen(?), the resulting line is asymmetrical when
         * *either* the cell is odd sized, *or* the line is odd
         * sized. But not when both are.
         *
         * Hence the ???thick % 2 ^ width % 2??? in the expressions below.
         */
        switch (wc) {
        case  L'???':
            row_end = height - row - (thick_is_odd ^ height_is_odd);
            row_start = row_end - thick;
            col_end = width - col - (thick_is_odd ^ width_is_odd);
            col_start = col_end - thick;
            break;

        case L'???':
            row_end = height - row - (thick_is_odd ^ height_is_odd);
            row_start = row_end - thick;
            col_start = col - ((thick_is_odd ^ width_is_odd) ? supersample / 2 : 0);
            col_end = col_start + thick;
            break;

        case L'???':
            row_start = row - ((thick_is_odd ^ height_is_odd) ? supersample / 2 : 0);
            row_end = row_start + thick;
            col_end = width - col - (thick_is_odd ^ width_is_odd);
            col_start = col_end - thick;
            break;

        case L'???':
            row_start = row - ((thick_is_odd ^ height_is_odd) ? supersample / 2 : 0);
            row_end = row_start + thick;
            col_start = col - ((thick_is_odd ^ width_is_odd) ? supersample / 2 : 0);
            col_end = col_start + thick;
            break;
        }

        xassert(row_end > row_start);
        xassert(col_end > col_start);

        for (int r = max(row_start, 0); r < max(min(row_end, height), 0); r++) {
            for (int c = max(col_start, 0); c < max(min(col_end, width), 0); c++) {
                if (fmt == PIXMAN_a1) {
                    size_t idx = c / 8;
                    size_t bit_no = c % 8;
                    data[r * stride + idx] |= 1 << bit_no;
                } else
                    data[r * stride + c] = 0xff;
            }
        }
    }

    /*
     * Since a cell may not be completely symmetrical around its y-
     * and x-axis, the mirroring done above may result in the last
     * col/row of the arc not being filled in. This code ensures they
     * are.
     */

    if (wc == L'???' || wc == L'???') {
        for (int y = 0; y < thick; y++) {
            int row = (height - thick) / 2 + y - ((thick_is_odd ^ height_is_odd) ? supersample / 2 : 0);
            for (int col = width - supersample; col < width; col++) {
                if (row >= 0 && row < height && col >= 0) {
                    if (fmt == PIXMAN_a1) {
                        size_t ofs = col / 8;
                        size_t bit_no = col % 8;
                        data[row * stride + ofs] |= 1 << bit_no;
                    } else
                        data[row * stride + col] = 0xff;
                }
            }
        }
    }

    if (wc == L'???' || wc == L'???') {
        for (int x = 0; x < thick; x++) {
            int col = (width - thick) / 2 + x - ((thick_is_odd ^ width_is_odd) ? supersample / 2 : 0);
            for (int row = height - supersample; row < height; row++) {
                if (row >= 0 && col >= 0 && col < width) {
                    if (fmt == PIXMAN_a1) {
                        size_t ofs = col / 8;
                        size_t bit_no = col % 8;
                        data[row * stride + ofs] |= 1 << bit_no;
                    } else
                        data[row * stride + col] = 0xff;
                }
            }
        }
    }

    if (fmt == PIXMAN_a8) {
        xassert(data != buf->data);

        /* Downsample */
        for (size_t r = 0; r < buf->height; r++) {
            for (size_t c = 0; c < buf->width; c++) {
                uint32_t total = 0;
                for (size_t i = 0; i < supersample; i++) {
                    for (size_t j = 0; j < supersample; j++)
                        total += data[(r * supersample + i) * stride + c * supersample + j];
                }
                uint8_t average = min(total / (supersample * supersample), 0xff);
                buf->data[r * buf->stride + c] = average;
            }
        }

        free(data);
    }
}

static void
draw_box_drawings_light_diagonal_upper_right_to_lower_left(struct buf *buf)
{
    pixman_trapezoid_t trap = {
        .top = pixman_int_to_fixed(0),
        .bottom = pixman_int_to_fixed(buf->height),
        .left = {
            .p1 = {
                .x = pixman_double_to_fixed(buf->width - thickness(LIGHT) / 2.),
                .y = pixman_int_to_fixed(0),
            },
            .p2 = {
                .x = pixman_double_to_fixed(0 - thickness(LIGHT) / 2.),
                .y = pixman_int_to_fixed(buf->height),
            },
        },
        .right = {
            .p1 = {
                .x = pixman_double_to_fixed(buf->width + thickness(LIGHT) / 2.),
                .y = pixman_int_to_fixed(0),
            },
            .p2 = {
                .x = pixman_double_to_fixed(0 + thickness(LIGHT) / 2.),
                .y = pixman_int_to_fixed(buf->height),
            },
        },
    };

    pixman_rasterize_trapezoid(buf->pix, &trap, 0, 0);
}

static void
draw_box_drawings_light_diagonal_upper_left_to_lower_right(struct buf *buf)
{
    pixman_trapezoid_t trap = {
        .top = pixman_int_to_fixed(0),
        .bottom = pixman_int_to_fixed(buf->height),
        .left = {
            .p1 = {
                .x = pixman_double_to_fixed(0 - thickness(LIGHT) / 2.),
                .y = pixman_int_to_fixed(0),
            },
            .p2 = {
                .x = pixman_double_to_fixed(buf->width - thickness(LIGHT) / 2.),
                .y = pixman_int_to_fixed(buf->height),
            },
        },
        .right = {
            .p1 = {
                .x = pixman_double_to_fixed(0 + thickness(LIGHT) / 2.),
                .y = pixman_int_to_fixed(0),
            },
            .p2 = {
                .x = pixman_double_to_fixed(buf->width + thickness(LIGHT) / 2.),
                .y = pixman_int_to_fixed(buf->height),
            },
        },
    };

    pixman_rasterize_trapezoid(buf->pix, &trap, 0, 0);
}

static void
draw_box_drawings_light_diagonal_cross(struct buf *buf)
{
    draw_box_drawings_light_diagonal_upper_right_to_lower_left(buf);
    draw_box_drawings_light_diagonal_upper_left_to_lower_right(buf);
}

static void
draw_box_drawings_light_left(struct buf *buf)
{
    hline_middle_left(LIGHT);
}

static void
draw_box_drawings_light_up(struct buf *buf)
{
    vline_middle_up(LIGHT);
}

static void
draw_box_drawings_light_right(struct buf *buf)
{
    hline_middle_right(LIGHT);
}

static void
draw_box_drawings_light_down(struct buf *buf)
{
    vline_middle_down(LIGHT);
}

static void
draw_box_drawings_heavy_left(struct buf *buf)
{
    hline_middle_left(HEAVY);
}

static void
draw_box_drawings_heavy_up(struct buf *buf)
{
    vline_middle_up(HEAVY);
}

static void
draw_box_drawings_heavy_right(struct buf *buf)
{
    hline_middle_right(HEAVY);
}

static void
draw_box_drawings_heavy_down(struct buf *buf)
{
    vline_middle_down(HEAVY);
}

static void
draw_box_drawings_light_left_and_heavy_right(struct buf *buf)
{
    hline_middle_left(LIGHT);
    hline_middle_right(HEAVY);
}

static void
draw_box_drawings_light_up_and_heavy_down(struct buf *buf)
{
    vline_middle_up(LIGHT);
    vline_middle_down(HEAVY);
}

static void
draw_box_drawings_heavy_left_and_light_right(struct buf *buf)
{
    hline_middle_left(HEAVY);
    hline_middle_right(LIGHT);
}

static void
draw_box_drawings_heavy_up_and_light_down(struct buf *buf)
{
    vline_middle_up(HEAVY);
    vline_middle_down(LIGHT);
}

static void
draw_upper_half_block(struct buf *buf)
{
    rect(0, 0, buf->width, round(buf->height / 2.));
}

static void
draw_lower_one_eighth_block(struct buf *buf)
{
    rect(0, buf->height - round(buf->height / 8.), buf->width, buf->height);
}

static void
draw_lower_one_quarter_block(struct buf *buf)
{
    rect(0, buf->height - round(buf->height / 4.), buf->width, buf->height);
}

static void
draw_lower_three_eighths_block(struct buf *buf)
{
    rect(0, buf->height - round(3. * buf->height / 8.), buf->width, buf->height);
}

static void
draw_lower_half_block(struct buf *buf)
{
    rect(0, buf->height - round(buf->height / 2.), buf->width, buf->height);
}

static void
draw_lower_five_eighths_block(struct buf *buf)
{
    rect(0, buf->height - round(5. * buf->height / 8.), buf->width, buf->height);
}

static void
draw_lower_three_quarters_block(struct buf *buf)
{
    rect(0, buf->height - round(3. * buf->height / 4.), buf->width, buf->height);
}

static void
draw_lower_seven_eighths_block(struct buf *buf)
{
    rect(0, buf->height - round(7. * buf->height / 8.), buf->width, buf->height);
}

static void
draw_upper_one_quarter_block(struct buf *buf)
{
    rect(0, 0, buf->width, round(buf->height / 4.));
}

static void
draw_upper_three_eighths_block(struct buf *buf)
{
    rect(0, 0, buf->width, round(3. * buf->height / 8.));
}

static void
draw_upper_five_eighths_block(struct buf *buf)
{
    rect(0, 0, buf->width, round(5. * buf->height / 8.));
}

static void
draw_upper_three_quarters_block(struct buf *buf)
{
    rect(0, 0, buf->width, round(3. * buf->height / 4.));
}

static void
draw_upper_seven_eighths_block(struct buf *buf)
{
    rect(0, 0, buf->width, round(7. * buf->height / 8.));
}

static void
draw_full_block(struct buf *buf)
{
    rect(0, 0, buf->width, buf->height);
}

static void
draw_left_seven_eighths_block(struct buf *buf)
{
    rect(0, 0, round(7. * buf->width / 8.), buf->height);
}

static void
draw_left_three_quarters_block(struct buf *buf)
{
    rect(0, 0, round(3. * buf->width / 4.), buf->height);
}

static void
draw_left_five_eighths_block(struct buf *buf)
{
    rect(0, 0, round(5. * buf->width / 8.), buf->height);
}

static void
draw_left_half_block(struct buf *buf)
{
    rect(0, 0, round(buf->width / 2.), buf->height);
}

static void
draw_left_three_eighths_block(struct buf *buf)
{
    rect(0, 0, round(3. * buf->width / 8.), buf->height);
}

static void
draw_left_one_quarter_block(struct buf *buf)
{
    rect(0, 0, round(buf->width / 4.), buf->height);
}

static void
draw_vertical_one_eighth_block_n(struct buf *buf, int n)
{
    double x = round((double)n * buf->width / 8.);
    double w = round(buf->width / 8.);
    rect(x, 0, x + w, buf->height);
}

static void
draw_left_one_eighth_block(struct buf *buf)
{
    draw_vertical_one_eighth_block_n(buf, 0);
}

static void
draw_vertical_one_eighth_block_2(struct buf *buf)
{
    draw_vertical_one_eighth_block_n(buf, 1);
}

static void
draw_vertical_one_eighth_block_3(struct buf *buf)
{
    draw_vertical_one_eighth_block_n(buf, 2);
}

static void
draw_vertical_one_eighth_block_4(struct buf *buf)
{
    draw_vertical_one_eighth_block_n(buf, 3);
}

static void
draw_vertical_one_eighth_block_5(struct buf *buf)
{
    draw_vertical_one_eighth_block_n(buf, 4);
}

static void
draw_vertical_one_eighth_block_6(struct buf *buf)
{
    draw_vertical_one_eighth_block_n(buf, 5);
}

static void
draw_vertical_one_eighth_block_7(struct buf *buf)
{
    draw_vertical_one_eighth_block_n(buf, 6);
}

static void
draw_right_half_block(struct buf *buf)
{
    rect(round(buf->width / 2.), 0, buf->width, buf->height);
}

static void NOINLINE
draw_pixman_shade(struct buf *buf, uint16_t v)
{
    pixman_color_t shade = {.red = 0, .green = 0, .blue = 0, .alpha = v};
    pixman_image_fill_rectangles(
        PIXMAN_OP_SRC, buf->pix, &shade, 1,
        (pixman_rectangle16_t []){{0, 0, buf->width, buf->height}});
}

static void
draw_light_shade(struct buf *buf)
{
    pixman_format_code_t fmt = pixman_image_get_format(buf->pix);

    if (buf->solid_shades && fmt == PIXMAN_a1)
        change_buffer_format(buf, PIXMAN_a8);
    else if (!buf->solid_shades && fmt == PIXMAN_a8)
        change_buffer_format(buf, PIXMAN_a1);

    if (buf->solid_shades)
        draw_pixman_shade(buf, 0x4000);
    else {
        for (size_t row = 0; row < buf->height; row += 2) {
            for (size_t col = 0; col < buf->width; col += 2) {
                size_t idx = col / 8;
                size_t bit_no = col % 8;
                buf->data[row * buf->stride + idx] |= 1 << bit_no;
            }
        }
    }
}

static void
draw_medium_shade(struct buf *buf)
{
    pixman_format_code_t fmt = pixman_image_get_format(buf->pix);

    if (buf->solid_shades && fmt == PIXMAN_a1)
        change_buffer_format(buf, PIXMAN_a8);
    else if (!buf->solid_shades && fmt == PIXMAN_a8)
        change_buffer_format(buf, PIXMAN_a1);

    if (buf->solid_shades)
        draw_pixman_shade(buf, 0x8000);
    else {
        for (size_t row = 0; row < buf->height; row++) {
            for (size_t col = row % 2; col < buf->width; col += 2) {
                size_t idx = col / 8;
                size_t bit_no = col % 8;
                buf->data[row * buf->stride + idx] |= 1 << bit_no;
            }
        }
    }
}

static void
draw_dark_shade(struct buf *buf)
{
    pixman_format_code_t fmt = pixman_image_get_format(buf->pix);

    if (buf->solid_shades && fmt == PIXMAN_a1)
        change_buffer_format(buf, PIXMAN_a8);
    else if (!buf->solid_shades && fmt == PIXMAN_a8)
        change_buffer_format(buf, PIXMAN_a1);

    if (buf->solid_shades)
        draw_pixman_shade(buf, 0xc000);
    else {
        for (size_t row = 0; row < buf->height; row++) {
            for (size_t col = 0; col < buf->width; col += 1 + row % 2) {
                size_t idx = col / 8;
                size_t bit_no = col % 8;
                buf->data[row * buf->stride + idx] |= 1 << bit_no;
            }
        }
    }
}

static void
draw_horizontal_one_eighth_block_n(struct buf *buf, int n)
{
    double y = round((double)n * buf->height / 8.);
    double h = round(buf->height / 8.);
    rect(0, y, buf->width, y + h);
}

static void
draw_upper_one_eighth_block(struct buf *buf)
{
    draw_horizontal_one_eighth_block_n(buf, 0);
}

static void
draw_horizontal_one_eighth_block_2(struct buf *buf)
{
    draw_horizontal_one_eighth_block_n(buf, 1);
}

static void
draw_horizontal_one_eighth_block_3(struct buf *buf)
{
    draw_horizontal_one_eighth_block_n(buf, 2);
}

static void
draw_horizontal_one_eighth_block_4(struct buf *buf)
{
    draw_horizontal_one_eighth_block_n(buf, 3);
}

static void
draw_horizontal_one_eighth_block_5(struct buf *buf)
{
    draw_horizontal_one_eighth_block_n(buf, 4);
}

static void
draw_horizontal_one_eighth_block_6(struct buf *buf)
{
    draw_horizontal_one_eighth_block_n(buf, 5);
}

static void
draw_horizontal_one_eighth_block_7(struct buf *buf)
{
    draw_horizontal_one_eighth_block_n(buf, 6);
}

static void
draw_right_one_eighth_block(struct buf *buf)
{
    rect(buf->width - round(buf->width / 8.), 0, buf->width, buf->height);
}

static void NOINLINE
quad_upper_left(struct buf *buf)
{
    rect(0, 0, ceil(buf->width / 2.), ceil(buf->height / 2.));
}

static void NOINLINE
quad_upper_right(struct buf *buf)
{
    rect(floor(buf->width / 2.), 0, buf->width, ceil(buf->height / 2.));
}

static void NOINLINE
quad_lower_left(struct buf *buf)
{
    rect(0, floor(buf->height / 2.), ceil(buf->width / 2.), buf->height);
}

static void NOINLINE
quad_lower_right(struct buf *buf)
{
    rect(floor(buf->width / 2.), floor(buf->height / 2.), buf->width, buf->height);
}

static void NOINLINE
draw_quadrant_lower_left(struct buf *buf)
{
    quad_lower_left(buf);
}

static void NOINLINE
draw_quadrant_lower_right(struct buf *buf)
{
    quad_lower_right(buf);
}

static void
draw_quadrant_upper_left(struct buf *buf)
{
    quad_upper_left(buf);
}

static void
draw_quadrant_upper_left_and_lower_left_and_lower_right(struct buf *buf)
{
    quad_upper_left(buf);
    quad_lower_left(buf);
    quad_lower_right(buf);
}

static void
draw_quadrant_upper_left_and_lower_right(struct buf *buf)
{
    quad_upper_left(buf);
    quad_lower_right(buf);
}

static void
draw_quadrant_upper_left_and_upper_right_and_lower_left(struct buf *buf)
{
    quad_upper_left(buf);
    quad_upper_right(buf);
    quad_lower_left(buf);
}

static void
draw_quadrant_upper_left_and_upper_right_and_lower_right(struct buf *buf)
{
    quad_upper_left(buf);
    quad_upper_right(buf);
    quad_lower_right(buf);
}

static void
draw_quadrant_upper_right(struct buf *buf)
{
    quad_upper_right(buf);
}

static void
draw_quadrant_upper_right_and_lower_left(struct buf *buf)
{
    quad_upper_right(buf);
    quad_lower_left(buf);
}

static void
draw_quadrant_upper_right_and_lower_left_and_lower_right(struct buf *buf)
{
    quad_upper_right(buf);
    quad_lower_left(buf);
    quad_lower_right(buf);
}

static void NOINLINE
sextant_upper_left(struct buf *buf)
{
    rect(0, 0, round(buf->width / 2.), round(buf->height / 3.));
}

static void NOINLINE
sextant_middle_left(struct buf *buf)
{
    rect(0, buf->height / 3, round(buf->width / 2.), round(2. * buf->height / 3.));
}

static void NOINLINE
sextant_lower_left(struct buf *buf)
{
    rect(0, 2 * buf->height / 3, round(buf->width / 2.), buf->height);
}

static void NOINLINE
sextant_upper_right(struct buf *buf)
{
    rect(buf->width / 2, 0, buf->width, round(buf->height / 3.));
}

static void NOINLINE
sextant_middle_right(struct buf *buf)
{
    rect(buf->width / 2, buf->height / 3, buf->width, round(2. * buf->height / 3.));
}

static void NOINLINE
sextant_lower_right(struct buf *buf)
{
    rect(buf->width / 2, 2 * buf->height / 3, buf->width, buf->height);
}

static void
draw_sextant(struct buf *buf, wchar_t wc)
{
    /*
     * Each byte encodes one sextant:
     *
     * Bit     sextant
     *   0      upper left
     *   1      middle left
     *   2      lower left
     *   3      upper right
     *   4      middle right
     *   5      lower right
     */
    enum {
        UPPER_LEFT = 1 << 0,
        MIDDLE_LEFT = 1 << 1,
        LOWER_LEFT = 1 << 2,
        UPPER_RIGHT = 1 << 3,
        MIDDLE_RIGHT = 1 << 4,
        LOWER_RIGHT = 1 << 5,
    };

    static const uint8_t matrix[60] = {
        /* U+1fb00 - U+1fb0f */
        UPPER_LEFT,
        UPPER_RIGHT,
        UPPER_LEFT | UPPER_RIGHT,
        MIDDLE_LEFT,
        UPPER_LEFT | MIDDLE_LEFT,
        UPPER_RIGHT | MIDDLE_LEFT,
        UPPER_LEFT | UPPER_RIGHT | MIDDLE_LEFT,
        MIDDLE_RIGHT,
        UPPER_LEFT | MIDDLE_RIGHT,
        UPPER_RIGHT | MIDDLE_RIGHT,
        UPPER_LEFT | UPPER_RIGHT | MIDDLE_RIGHT,
        MIDDLE_LEFT | MIDDLE_RIGHT,
        UPPER_LEFT | MIDDLE_LEFT | MIDDLE_RIGHT,
        UPPER_RIGHT | MIDDLE_LEFT | MIDDLE_RIGHT,
        UPPER_LEFT | UPPER_RIGHT | MIDDLE_LEFT | MIDDLE_RIGHT,
        LOWER_LEFT,

        /* U+1fb10 - U+1fb1f */
        UPPER_LEFT | LOWER_LEFT,
        UPPER_RIGHT | LOWER_LEFT,
        UPPER_LEFT | UPPER_RIGHT | LOWER_LEFT,
        MIDDLE_LEFT | LOWER_LEFT,
        UPPER_RIGHT | MIDDLE_LEFT | LOWER_LEFT,
        UPPER_LEFT | UPPER_RIGHT | MIDDLE_LEFT | LOWER_LEFT,
        MIDDLE_RIGHT | LOWER_LEFT,
        UPPER_LEFT | MIDDLE_RIGHT | LOWER_LEFT,
        UPPER_RIGHT | MIDDLE_RIGHT | LOWER_LEFT,
        UPPER_LEFT | UPPER_RIGHT | MIDDLE_RIGHT | LOWER_LEFT,
        MIDDLE_LEFT | MIDDLE_RIGHT | LOWER_LEFT,
        UPPER_LEFT | MIDDLE_LEFT | MIDDLE_RIGHT | LOWER_LEFT,
        UPPER_RIGHT | MIDDLE_LEFT | MIDDLE_RIGHT | LOWER_LEFT,
        UPPER_LEFT | UPPER_RIGHT | MIDDLE_LEFT | MIDDLE_RIGHT | LOWER_LEFT,
        LOWER_RIGHT,
        UPPER_LEFT | LOWER_RIGHT,

        /* U+1fb20 - U+1fb2f */
        UPPER_RIGHT | LOWER_RIGHT,
        UPPER_LEFT | UPPER_RIGHT | LOWER_RIGHT,
        MIDDLE_LEFT | LOWER_RIGHT,
        UPPER_LEFT | MIDDLE_LEFT | LOWER_RIGHT,
        UPPER_RIGHT | MIDDLE_LEFT | LOWER_RIGHT,
        UPPER_LEFT | UPPER_RIGHT | MIDDLE_LEFT | LOWER_RIGHT,
        MIDDLE_RIGHT | LOWER_RIGHT,
        UPPER_LEFT | MIDDLE_RIGHT | LOWER_RIGHT,
        UPPER_LEFT | UPPER_RIGHT | MIDDLE_RIGHT | LOWER_RIGHT,
        MIDDLE_LEFT | MIDDLE_RIGHT | LOWER_RIGHT,
        UPPER_LEFT | MIDDLE_LEFT | MIDDLE_RIGHT | LOWER_RIGHT,
        UPPER_RIGHT | MIDDLE_LEFT | MIDDLE_RIGHT | LOWER_RIGHT,
        UPPER_LEFT | UPPER_RIGHT | MIDDLE_LEFT | MIDDLE_RIGHT | LOWER_RIGHT,
        LOWER_LEFT | LOWER_RIGHT,
        UPPER_LEFT | LOWER_LEFT | LOWER_RIGHT,
        UPPER_RIGHT | LOWER_LEFT | LOWER_RIGHT,

        /* U+1fb30 - U+1fb3b */
        UPPER_LEFT | UPPER_RIGHT | LOWER_LEFT | LOWER_RIGHT,
        MIDDLE_LEFT | LOWER_LEFT | LOWER_RIGHT,
        UPPER_LEFT | MIDDLE_LEFT | LOWER_LEFT | LOWER_RIGHT,
        UPPER_RIGHT | MIDDLE_LEFT | LOWER_LEFT | LOWER_RIGHT,
        UPPER_LEFT | UPPER_RIGHT | MIDDLE_LEFT | LOWER_LEFT | LOWER_RIGHT,
        MIDDLE_RIGHT | LOWER_LEFT | LOWER_RIGHT,
        UPPER_LEFT | MIDDLE_RIGHT | LOWER_LEFT | LOWER_RIGHT,
        UPPER_RIGHT | MIDDLE_RIGHT | LOWER_LEFT | LOWER_RIGHT,
        UPPER_LEFT | UPPER_RIGHT | MIDDLE_RIGHT | LOWER_LEFT | LOWER_RIGHT,
        MIDDLE_LEFT | MIDDLE_RIGHT | LOWER_LEFT | LOWER_RIGHT,
        UPPER_LEFT | MIDDLE_LEFT | MIDDLE_RIGHT | LOWER_LEFT | LOWER_RIGHT,
        UPPER_RIGHT | MIDDLE_LEFT | MIDDLE_RIGHT | LOWER_LEFT | LOWER_RIGHT,
    };

    xassert(wc >= 0x1fb00 && wc <= 0x1fb3b);
    const size_t idx = wc - 0x1fb00;

    xassert(idx < ALEN(matrix));
    uint8_t encoded = matrix[idx];

    if (encoded & UPPER_LEFT)
        sextant_upper_left(buf);

    if (encoded & MIDDLE_LEFT)
        sextant_middle_left(buf);

    if (encoded & LOWER_LEFT)
        sextant_lower_left(buf);

    if (encoded & UPPER_RIGHT)
        sextant_upper_right(buf);

    if (encoded & MIDDLE_RIGHT)
        sextant_middle_right(buf);

    if (encoded & LOWER_RIGHT)
        sextant_lower_right(buf);
}

static void
draw_left_and_lower_one_eighth_block(struct buf *buf)
{
    draw_left_one_eighth_block(buf);
    draw_lower_one_eighth_block(buf);
}

static void
draw_left_and_upper_one_eighth_block(struct buf *buf)
{
    draw_left_one_eighth_block(buf);
    draw_upper_one_eighth_block(buf);
}

static void
draw_right_and_upper_one_eighth_block(struct buf *buf)
{
    draw_right_one_eighth_block(buf);
    draw_upper_one_eighth_block(buf);
}

static void
draw_right_and_lower_one_eighth_block(struct buf *buf)
{
    draw_right_one_eighth_block(buf);
    draw_lower_one_eighth_block(buf);
}

static void
draw_upper_and_lower_one_eighth_block(struct buf *buf)
{
    draw_upper_one_eighth_block(buf);
    draw_lower_one_eighth_block(buf);
}

static void
draw_horizontal_one_eighth_1358_block(struct buf *buf)
{
    draw_upper_one_eighth_block(buf);
    draw_horizontal_one_eighth_block_3(buf);
    draw_horizontal_one_eighth_block_5(buf);
    draw_lower_one_eighth_block(buf);
}

static void
draw_right_one_quarter_block(struct buf *buf)
{
    rect(buf->width - round(buf->width / 4.), 0, buf->width, buf->height);
}

static void
draw_right_three_eighths_block(struct buf *buf)
{
    rect(buf->width - round(3. * buf->width / 8.), 0, buf->width, buf->height);
}

static void
draw_right_five_eighths_block(struct buf *buf)
{
    rect(buf->width - round(5. * buf->width / 8.), 0, buf->width, buf->height);
}

static void
draw_right_three_quarters_block(struct buf *buf)
{
    rect(buf->width - round(3. * buf->width / 4.), 0, buf->width, buf->height);
}

static void
draw_right_seven_eighths_block(struct buf *buf)
{
    rect(buf->width - round(7. * buf->width / 8.), 0, buf->width, buf->height);
}

static void
draw_glyph(struct buf *buf, wchar_t wc)
{
    IGNORE_WARNING("-Wpedantic")

    switch (wc) {
    case 0x2500: draw_box_drawings_light_horizontal(buf); break;
    case 0x2501: draw_box_drawings_heavy_horizontal(buf); break;
    case 0x2502: draw_box_drawings_light_vertical(buf); break;
    case 0x2503: draw_box_drawings_heavy_vertical(buf); break;
    case 0x2504: draw_box_drawings_light_triple_dash_horizontal(buf); break;
    case 0x2505: draw_box_drawings_heavy_triple_dash_horizontal(buf); break;
    case 0x2506: draw_box_drawings_light_triple_dash_vertical(buf); break;
    case 0x2507: draw_box_drawings_heavy_triple_dash_vertical(buf); break;
    case 0x2508: draw_box_drawings_light_quadruple_dash_horizontal(buf); break;
    case 0x2509: draw_box_drawings_heavy_quadruple_dash_horizontal(buf); break;
    case 0x250a: draw_box_drawings_light_quadruple_dash_vertical(buf); break;
    case 0x250b: draw_box_drawings_heavy_quadruple_dash_vertical(buf); break;
    case 0x250c: draw_box_drawings_light_down_and_right(buf); break;
    case 0x250d: draw_box_drawings_down_light_and_right_heavy(buf); break;
    case 0x250e: draw_box_drawings_down_heavy_and_right_light(buf); break;
    case 0x250f: draw_box_drawings_heavy_down_and_right(buf); break;

    case 0x2510: draw_box_drawings_light_down_and_left(buf); break;
    case 0x2511: draw_box_drawings_down_light_and_left_heavy(buf); break;
    case 0x2512: draw_box_drawings_down_heavy_and_left_light(buf); break;
    case 0x2513: draw_box_drawings_heavy_down_and_left(buf); break;
    case 0x2514: draw_box_drawings_light_up_and_right(buf); break;
    case 0x2515: draw_box_drawings_up_light_and_right_heavy(buf); break;
    case 0x2516: draw_box_drawings_up_heavy_and_right_light(buf); break;
    case 0x2517: draw_box_drawings_heavy_up_and_right(buf); break;
    case 0x2518: draw_box_drawings_light_up_and_left(buf); break;
    case 0x2519: draw_box_drawings_up_light_and_left_heavy(buf); break;
    case 0x251a: draw_box_drawings_up_heavy_and_left_light(buf); break;
    case 0x251b: draw_box_drawings_heavy_up_and_left(buf); break;
    case 0x251c: draw_box_drawings_light_vertical_and_right(buf); break;
    case 0x251d: draw_box_drawings_vertical_light_and_right_heavy(buf); break;
    case 0x251e: draw_box_drawings_up_heavy_and_right_down_light(buf); break;
    case 0x251f: draw_box_drawings_down_heavy_and_right_up_light(buf); break;

    case 0x2520: draw_box_drawings_vertical_heavy_and_right_light(buf); break;
    case 0x2521: draw_box_drawings_down_light_and_right_up_heavy(buf); break;
    case 0x2522: draw_box_drawings_up_light_and_right_down_heavy(buf); break;
    case 0x2523: draw_box_drawings_heavy_vertical_and_right(buf); break;
    case 0x2524: draw_box_drawings_light_vertical_and_left(buf); break;
    case 0x2525: draw_box_drawings_vertical_light_and_left_heavy(buf); break;
    case 0x2526: draw_box_drawings_up_heavy_and_left_down_light(buf); break;
    case 0x2527: draw_box_drawings_down_heavy_and_left_up_light(buf); break;
    case 0x2528: draw_box_drawings_vertical_heavy_and_left_light(buf); break;
    case 0x2529: draw_box_drawings_down_light_and_left_up_heavy(buf); break;
    case 0x252a: draw_box_drawings_up_light_and_left_down_heavy(buf); break;
    case 0x252b: draw_box_drawings_heavy_vertical_and_left(buf); break;
    case 0x252c: draw_box_drawings_light_down_and_horizontal(buf); break;
    case 0x252d: draw_box_drawings_left_heavy_and_right_down_light(buf); break;
    case 0x252e: draw_box_drawings_right_heavy_and_left_down_light(buf); break;
    case 0x252f: draw_box_drawings_down_light_and_horizontal_heavy(buf); break;

    case 0x2530: draw_box_drawings_down_heavy_and_horizontal_light(buf); break;
    case 0x2531: draw_box_drawings_right_light_and_left_down_heavy(buf); break;
    case 0x2532: draw_box_drawings_left_light_and_right_down_heavy(buf); break;
    case 0x2533: draw_box_drawings_heavy_down_and_horizontal(buf); break;
    case 0x2534: draw_box_drawings_light_up_and_horizontal(buf); break;
    case 0x2535: draw_box_drawings_left_heavy_and_right_up_light(buf); break;
    case 0x2536: draw_box_drawings_right_heavy_and_left_up_light(buf); break;
    case 0x2537: draw_box_drawings_up_light_and_horizontal_heavy(buf); break;
    case 0x2538: draw_box_drawings_up_heavy_and_horizontal_light(buf); break;
    case 0x2539: draw_box_drawings_right_light_and_left_up_heavy(buf); break;
    case 0x253a: draw_box_drawings_left_light_and_right_up_heavy(buf); break;
    case 0x253b: draw_box_drawings_heavy_up_and_horizontal(buf); break;
    case 0x253c: draw_box_drawings_light_vertical_and_horizontal(buf); break;
    case 0x253d: draw_box_drawings_left_heavy_and_right_vertical_light(buf); break;
    case 0x253e: draw_box_drawings_right_heavy_and_left_vertical_light(buf); break;
    case 0x253f: draw_box_drawings_vertical_light_and_horizontal_heavy(buf); break;

    case 0x2540: draw_box_drawings_up_heavy_and_down_horizontal_light(buf); break;
    case 0x2541: draw_box_drawings_down_heavy_and_up_horizontal_light(buf); break;
    case 0x2542: draw_box_drawings_vertical_heavy_and_horizontal_light(buf); break;
    case 0x2543: draw_box_drawings_left_up_heavy_and_right_down_light(buf); break;
    case 0x2544: draw_box_drawings_right_up_heavy_and_left_down_light(buf); break;
    case 0x2545: draw_box_drawings_left_down_heavy_and_right_up_light(buf); break;
    case 0x2546: draw_box_drawings_right_down_heavy_and_left_up_light(buf); break;
    case 0x2547: draw_box_drawings_down_light_and_up_horizontal_heavy(buf); break;
    case 0x2548: draw_box_drawings_up_light_and_down_horizontal_heavy(buf); break;
    case 0x2549: draw_box_drawings_right_light_and_left_vertical_heavy(buf); break;
    case 0x254a: draw_box_drawings_left_light_and_right_vertical_heavy(buf); break;
    case 0x254b: draw_box_drawings_heavy_vertical_and_horizontal(buf); break;
    case 0x254c: draw_box_drawings_light_double_dash_horizontal(buf); break;
    case 0x254d: draw_box_drawings_heavy_double_dash_horizontal(buf); break;
    case 0x254e: draw_box_drawings_light_double_dash_vertical(buf); break;
    case 0x254f: draw_box_drawings_heavy_double_dash_vertical(buf); break;

    case 0x2550: draw_box_drawings_double_horizontal(buf); break;
    case 0x2551: draw_box_drawings_double_vertical(buf); break;
    case 0x2552: draw_box_drawings_down_single_and_right_double(buf); break;
    case 0x2553: draw_box_drawings_down_double_and_right_single(buf); break;
    case 0x2554: draw_box_drawings_double_down_and_right(buf); break;
    case 0x2555: draw_box_drawings_down_single_and_left_double(buf); break;
    case 0x2556: draw_box_drawings_down_double_and_left_single(buf); break;
    case 0x2557: draw_box_drawings_double_down_and_left(buf); break;
    case 0x2558: draw_box_drawings_up_single_and_right_double(buf); break;
    case 0x2559: draw_box_drawings_up_double_and_right_single(buf); break;
    case 0x255a: draw_box_drawings_double_up_and_right(buf); break;
    case 0x255b: draw_box_drawings_up_single_and_left_double(buf); break;
    case 0x255c: draw_box_drawings_up_double_and_left_single(buf); break;
    case 0x255d: draw_box_drawings_double_up_and_left(buf); break;
    case 0x255e: draw_box_drawings_vertical_single_and_right_double(buf); break;
    case 0x255f: draw_box_drawings_vertical_double_and_right_single(buf); break;

    case 0x2560: draw_box_drawings_double_vertical_and_right(buf); break;
    case 0x2561: draw_box_drawings_vertical_single_and_left_double(buf); break;
    case 0x2562: draw_box_drawings_vertical_double_and_left_single(buf); break;
    case 0x2563: draw_box_drawings_double_vertical_and_left(buf); break;
    case 0x2564: draw_box_drawings_down_single_and_horizontal_double(buf); break;
    case 0x2565: draw_box_drawings_down_double_and_horizontal_single(buf); break;
    case 0x2566: draw_box_drawings_double_down_and_horizontal(buf); break;
    case 0x2567: draw_box_drawings_up_single_and_horizontal_double(buf); break;
    case 0x2568: draw_box_drawings_up_double_and_horizontal_single(buf); break;
    case 0x2569: draw_box_drawings_double_up_and_horizontal(buf); break;
    case 0x256a: draw_box_drawings_vertical_single_and_horizontal_double(buf); break;
    case 0x256b: draw_box_drawings_vertical_double_and_horizontal_single(buf); break;
    case 0x256c: draw_box_drawings_double_vertical_and_horizontal(buf); break;
    case 0x256d ... 0x2570: draw_box_drawings_light_arc(buf, wc); break;

    case 0x2571: draw_box_drawings_light_diagonal_upper_right_to_lower_left(buf); break;
    case 0x2572: draw_box_drawings_light_diagonal_upper_left_to_lower_right(buf); break;
    case 0x2573: draw_box_drawings_light_diagonal_cross(buf); break;
    case 0x2574: draw_box_drawings_light_left(buf); break;
    case 0x2575: draw_box_drawings_light_up(buf); break;
    case 0x2576: draw_box_drawings_light_right(buf); break;
    case 0x2577: draw_box_drawings_light_down(buf); break;
    case 0x2578: draw_box_drawings_heavy_left(buf); break;
    case 0x2579: draw_box_drawings_heavy_up(buf); break;
    case 0x257a: draw_box_drawings_heavy_right(buf); break;
    case 0x257b: draw_box_drawings_heavy_down(buf); break;
    case 0x257c: draw_box_drawings_light_left_and_heavy_right(buf); break;
    case 0x257d: draw_box_drawings_light_up_and_heavy_down(buf); break;
    case 0x257e: draw_box_drawings_heavy_left_and_light_right(buf); break;
    case 0x257f: draw_box_drawings_heavy_up_and_light_down(buf); break;

    case 0x2580: draw_upper_half_block(buf); break;
    case 0x2581: draw_lower_one_eighth_block(buf); break;
    case 0x2582: draw_lower_one_quarter_block(buf); break;
    case 0x2583: draw_lower_three_eighths_block(buf); break;
    case 0x2584: draw_lower_half_block(buf); break;
    case 0x2585: draw_lower_five_eighths_block(buf); break;
    case 0x2586: draw_lower_three_quarters_block(buf); break;
    case 0x2587: draw_lower_seven_eighths_block(buf); break;
    case 0x2588: draw_full_block(buf); break;
    case 0x2589: draw_left_seven_eighths_block(buf); break;
    case 0x258a: draw_left_three_quarters_block(buf); break;
    case 0x258b: draw_left_five_eighths_block(buf); break;
    case 0x258c: draw_left_half_block(buf); break;
    case 0x258d: draw_left_three_eighths_block(buf); break;
    case 0x258e: draw_left_one_quarter_block(buf); break;
    case 0x258f: draw_left_one_eighth_block(buf); break;

    case 0x2590: draw_right_half_block(buf); break;
    case 0x2591: draw_light_shade(buf); break;
    case 0x2592: draw_medium_shade(buf); break;
    case 0x2593: draw_dark_shade(buf); break;
    case 0x2594: draw_upper_one_eighth_block(buf); break;
    case 0x2595: draw_right_one_eighth_block(buf); break;
    case 0x2596: draw_quadrant_lower_left(buf); break;
    case 0x2597: draw_quadrant_lower_right(buf); break;
    case 0x2598: draw_quadrant_upper_left(buf); break;
    case 0x2599: draw_quadrant_upper_left_and_lower_left_and_lower_right(buf); break;
    case 0x259a: draw_quadrant_upper_left_and_lower_right(buf); break;
    case 0x259b: draw_quadrant_upper_left_and_upper_right_and_lower_left(buf); break;
    case 0x259c: draw_quadrant_upper_left_and_upper_right_and_lower_right(buf); break;
    case 0x259d: draw_quadrant_upper_right(buf); break;
    case 0x259e: draw_quadrant_upper_right_and_lower_left(buf); break;
    case 0x259f: draw_quadrant_upper_right_and_lower_left_and_lower_right(buf); break;

    case 0x1fb00 ... 0x1fb3b: draw_sextant(buf, wc); break;

    case 0x1fb70: draw_vertical_one_eighth_block_2(buf); break;
    case 0x1fb71: draw_vertical_one_eighth_block_3(buf); break;
    case 0x1fb72: draw_vertical_one_eighth_block_4(buf); break;
    case 0x1fb73: draw_vertical_one_eighth_block_5(buf); break;
    case 0x1fb74: draw_vertical_one_eighth_block_6(buf); break;
    case 0x1fb75: draw_vertical_one_eighth_block_7(buf); break;

    case 0x1fb76: draw_horizontal_one_eighth_block_2(buf); break;
    case 0x1fb77: draw_horizontal_one_eighth_block_3(buf); break;
    case 0x1fb78: draw_horizontal_one_eighth_block_4(buf); break;
    case 0x1fb79: draw_horizontal_one_eighth_block_5(buf); break;
    case 0x1fb7a: draw_horizontal_one_eighth_block_6(buf); break;
    case 0x1fb7b: draw_horizontal_one_eighth_block_7(buf); break;

    case 0x1fb82: draw_upper_one_quarter_block(buf); break;
    case 0x1fb83: draw_upper_three_eighths_block(buf); break;
    case 0x1fb84: draw_upper_five_eighths_block(buf); break;
    case 0x1fb85: draw_upper_three_quarters_block(buf); break;
    case 0x1fb86: draw_upper_seven_eighths_block(buf); break;

    case 0x1fb7c: draw_left_and_lower_one_eighth_block(buf); break;
    case 0x1fb7d: draw_left_and_upper_one_eighth_block(buf); break;
    case 0x1fb7e: draw_right_and_upper_one_eighth_block(buf); break;
    case 0x1fb7f: draw_right_and_lower_one_eighth_block(buf); break;
    case 0x1fb80: draw_upper_and_lower_one_eighth_block(buf); break;
    case 0x1fb81: draw_horizontal_one_eighth_1358_block(buf); break;

    case 0x1fb87: draw_right_one_quarter_block(buf); break;
    case 0x1fb88: draw_right_three_eighths_block(buf); break;
    case 0x1fb89: draw_right_five_eighths_block(buf); break;
    case 0x1fb8a: draw_right_three_quarters_block(buf); break;
    case 0x1fb8b: draw_right_seven_eighths_block(buf); break;
    }

    UNIGNORE_WARNINGS
}

struct fcft_glyph * COLD
box_drawing(const struct terminal *term, wchar_t wc)
{
    int width = term->cell_width;
    int height = term->cell_height;

    pixman_format_code_t fmt =
        term->fonts[0]->antialias ? PIXMAN_a8 : PIXMAN_a1;

    int stride = stride_for_format_and_width(fmt, width);
    uint8_t *data = xcalloc(height * stride, 1);

    pixman_image_t *pix = pixman_image_create_bits_no_clear(
        fmt, width, height, (uint32_t*)data, stride);

    if (pix == NULL) {
        errno = ENOMEM;
        perror(__func__);
        abort();
    }

    struct buf buf = {
        .data = data,
        .pix = pix,
        .width = width,
        .height = height,
        .stride = stride,
        .dpi = term->font_dpi,
        .cell_size = sqrt(pow(term->cell_width, 2) + pow(term->cell_height, 2)),
        .base_thickness = term->conf->tweak.box_drawing_base_thickness,
        .solid_shades = term->conf->tweak.box_drawing_solid_shades,
    };

    buf.thickness[LIGHT] = _thickness(&buf, LIGHT);
    buf.thickness[HEAVY] = _thickness(&buf, HEAVY);

    LOG_DBG("LIGHT=%d, HEAVY=%d",
            _thickness(&buf, LIGHT), _thickness(&buf, HEAVY));

    draw_glyph(&buf, wc);

    struct fcft_glyph *glyph = xmalloc(sizeof(*glyph));
    *glyph = (struct fcft_glyph){
        .wc = wc,
        .cols = 1,
        .pix = buf.pix,
        .x = -term->font_x_ofs,
        .y = term->font_y_ofs + term->fonts[0]->ascent,
        .width = width,
        .height = height,
        .advance = {
            .x = width,
            .y = height,
        },
    };
    return glyph;
}
