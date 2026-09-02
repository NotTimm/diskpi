/*
 * See dots.h. Every shape works the same way: walk the subpixels in its
 * bounding box, test each one against the shape, and set the dot if it
 * lands inside. There is no antialiasing beyond that eight-subpixels-per-
 * cell resolution - wedges do not overlap, so a plain in/out test already
 * gives a clean edge, and the per-cell colour average smooths the one row
 * of cells where two shapes actually meet.
 */

#define _DEFAULT_SOURCE

#include "dots.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

struct Dots {
    int cols, rows;
    int subw, subh;
    unsigned char *mask;    /* cols*rows, one braille dot-bitmask per cell */
    double        *rsum, *gsum, *bsum;
    int           *count;
};

Dots *dots_new(int cols, int rows)
{
    if (cols <= 0 || rows <= 0)
        return NULL;

    Dots *d = calloc(1, sizeof(*d));
    if (!d)
        return NULL;

    d->cols = cols;
    d->rows = rows;
    d->subw = cols * 2;
    d->subh = rows * 4;

    size_t n = (size_t)cols * (size_t)rows;
    d->mask  = calloc(n, sizeof(*d->mask));
    d->rsum  = calloc(n, sizeof(*d->rsum));
    d->gsum  = calloc(n, sizeof(*d->gsum));
    d->bsum  = calloc(n, sizeof(*d->bsum));
    d->count = calloc(n, sizeof(*d->count));

    if (!d->mask || !d->rsum || !d->gsum || !d->bsum || !d->count) {
        dots_free(d);
        return NULL;
    }
    return d;
}

void dots_free(Dots *d)
{
    if (!d)
        return;
    free(d->mask);
    free(d->rsum);
    free(d->gsum);
    free(d->bsum);
    free(d->count);
    free(d);
}

/* Unicode braille dot numbering: 1 2 3 7 down the left column, 4 5 6 8 down
 * the right, each mapped to its bit (dot N sets bit N-1). */
static unsigned bit_for(int col, int row)
{
    static const int left[4]  = { 0, 1, 2, 6 };
    static const int right[4] = { 3, 4, 5, 7 };
    return 1u << (col == 0 ? left[row] : right[row]);
}

static void set_dot(Dots *d, int sx, int sy, RGB color)
{
    int cellc = sx / 2, cellr = sy / 4;
    size_t idx = (size_t)cellr * d->cols + cellc;

    d->mask[idx] |= bit_for(sx % 2, sy % 4);
    d->rsum[idx] += color.r;
    d->gsum[idx] += color.g;
    d->bsum[idx] += color.b;
    d->count[idx]++;
}

typedef bool (*InsideFn)(const void *ctx, double px, double py);

static void fill_region(Dots *d, double bx0, double by0, double bx1,
                        double by1, InsideFn inside, const void *ctx,
                        RGB color)
{
    if (!d)
        return;

    int x0 = (int)floor(bx0), x1 = (int)ceil(bx1);
    int y0 = (int)floor(by0), y1 = (int)ceil(by1);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > d->subw) x1 = d->subw;
    if (y1 > d->subh) y1 = d->subh;

    for (int sy = y0; sy < y1; sy++)
        for (int sx = x0; sx < x1; sx++)
            if (inside(ctx, sx + 0.5, sy + 0.5))
                set_dot(d, sx, sy, color);
}

/* ------------------------------------------------------------------ wedge */

typedef struct { double cx, cy, r_in, r_out, a0, a1; } WedgeCtx;

static bool wedge_inside(const void *v, double px, double py)
{
    const WedgeCtx *w = v;
    double dx = px - w->cx, dy = py - w->cy;
    double r = sqrt(dx * dx + dy * dy);
    if (r < w->r_in || r > w->r_out)
        return false;

    double a = atan2(dx, -dy) * 180.0 / M_PI;
    if (a < 0.0)
        a += 360.0;
    return a >= w->a0 && a < w->a1;
}

void dots_wedge(Dots *d, double cx, double cy, double r_in, double r_out,
               double a0, double a1, RGB color)
{
    if (!d || a1 <= a0 || r_out <= 0.0)
        return;
    WedgeCtx w = { cx, cy, r_in, r_out, a0, a1 };
    fill_region(d, cx - r_out, cy - r_out, cx + r_out, cy + r_out,
               wedge_inside, &w, color);
}

/* -------------------------------------------------------------- rect box */

typedef struct { double cx, cy, hx, hy, r; } RectCtx;

static bool rect_inside(const void *v, double px, double py)
{
    const RectCtx *b = v;
    double dx = fabs(px - b->cx), dy = fabs(py - b->cy);
    if (dx > b->hx || dy > b->hy)
        return false;
    if (b->r <= 0.0)
        return true;

    double qx = dx - (b->hx - b->r);
    double qy = dy - (b->hy - b->r);
    if (qx < 0.0) qx = 0.0;
    if (qy < 0.0) qy = 0.0;
    return qx * qx + qy * qy <= b->r * b->r;
}

void dots_rect(Dots *d, double x, double y, double w, double h, RGB color)
{
    dots_round_rect(d, x, y, w, h, 0.0, color);
}

void dots_round_rect(Dots *d, double x, double y, double w, double h,
                     double r, RGB color)
{
    if (!d || w <= 0.0 || h <= 0.0)
        return;
    double hx = w / 2.0, hy = h / 2.0;
    if (r > hx) r = hx;
    if (r > hy) r = hy;
    if (r < 0.0) r = 0.0;

    RectCtx b = { x + hx, y + hy, hx, hy, r };
    fill_region(d, x, y, x + w, y + h, rect_inside, &b, color);
}

/* ------------------------------------------------------------------ circle */

typedef struct { double cx, cy, r; } CircleCtx;

static bool circle_inside(const void *v, double px, double py)
{
    const CircleCtx *c = v;
    double dx = px - c->cx, dy = py - c->cy;
    return dx * dx + dy * dy <= c->r * c->r;
}

void dots_circle(Dots *d, double cx, double cy, double r, RGB color)
{
    if (!d || r <= 0.0)
        return;
    CircleCtx c = { cx, cy, r };
    fill_region(d, cx - r, cy - r, cx + r, cy + r, circle_inside, &c, color);
}

/* -------------------------------------------------------------------- line */

typedef struct { double x0, y0, dx, dy, len2, half; } LineCtx;

static bool line_inside(const void *v, double px, double py)
{
    const LineCtx *l = v;
    double t = l->len2 > 0.0
             ? ((px - l->x0) * l->dx + (py - l->y0) * l->dy) / l->len2 : 0.0;
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;

    double nx = l->x0 + t * l->dx, ny = l->y0 + t * l->dy;
    double ex = px - nx, ey = py - ny;
    return ex * ex + ey * ey <= l->half * l->half;
}

void dots_line(Dots *d, double x0, double y0, double x1, double y1,
              double thick, RGB color)
{
    if (!d)
        return;
    if (thick < 1.0)
        thick = 1.0;

    double dx = x1 - x0, dy = y1 - y0;
    LineCtx l = { x0, y0, dx, dy, dx * dx + dy * dy, thick / 2.0 };
    double half = thick / 2.0;
    double bx0 = (x0 < x1 ? x0 : x1) - half, bx1 = (x0 > x1 ? x0 : x1) + half;
    double by0 = (y0 < y1 ? y0 : y1) - half, by1 = (y0 > y1 ? y0 : y1) + half;

    fill_region(d, bx0, by0, bx1, by1, line_inside, &l, color);
}

/* --------------------------------------------------------------- polygon */

typedef struct { const DotPt *pts; int n; } PolyCtx;

/* Standard even-odd ray cast: count edges crossing a ray cast to +x. */
static bool poly_inside(const void *v, double px, double py)
{
    const PolyCtx *p = v;
    bool in = false;

    for (int i = 0, j = p->n - 1; i < p->n; j = i++) {
        double xi = p->pts[i].x, yi = p->pts[i].y;
        double xj = p->pts[j].x, yj = p->pts[j].y;

        if ((yi > py) != (yj > py)) {
            double xcross = xi + (py - yi) * (xj - xi) / (yj - yi);
            if (px < xcross)
                in = !in;
        }
    }
    return in;
}

void dots_polygon(Dots *d, const DotPt *pts, int n, RGB color)
{
    if (!d || !pts || n < 3)
        return;

    double x0 = pts[0].x, x1 = pts[0].x, y0 = pts[0].y, y1 = pts[0].y;
    for (int i = 1; i < n; i++) {
        if (pts[i].x < x0) x0 = pts[i].x;
        if (pts[i].x > x1) x1 = pts[i].x;
        if (pts[i].y < y0) y0 = pts[i].y;
        if (pts[i].y > y1) y1 = pts[i].y;
    }

    PolyCtx p = { pts, n };
    fill_region(d, x0, y0, x1, y1, poly_inside, &p, color);
}

/* ---------------------------------------------------------------- present */

static void put_braille(unsigned mask)
{
    unsigned cp = 0x2800u + mask;
    /* 0x2800-0x28FF always encodes as three UTF-8 bytes. */
    fputc((int)(0xE0 | (cp >> 12)), stdout);
    fputc((int)(0x80 | ((cp >> 6) & 0x3F)), stdout);
    fputc((int)(0x80 | (cp & 0x3F)), stdout);
}

void dots_present(const Dots *d, const Term *t, int indent, int rows_used,
                  DotsRowFn row_cb, void *ctx)
{
    for (int r = 0; r < rows_used; r++) {
        for (int k = 0; k < indent; k++)
            fputc(' ', stdout);

        if (d && r < d->rows) {
            for (int c = 0; c < d->cols; ) {
                size_t idx = (size_t)r * d->cols + c;
                unsigned char m = d->mask[idx];

                if (m == 0) {
                    fputc(' ', stdout);
                    c++;
                    continue;
                }

                RGB col = {
                    (unsigned char)(d->rsum[idx] / d->count[idx] + 0.5),
                    (unsigned char)(d->gsum[idx] / d->count[idx] + 0.5),
                    (unsigned char)(d->bsum[idx] / d->count[idx] + 0.5)
                };

                int end = c + 1;
                while (end < d->cols) {
                    size_t idx2 = (size_t)r * d->cols + end;
                    if (d->mask[idx2] == 0)
                        break;
                    RGB c2 = {
                        (unsigned char)(d->rsum[idx2] / d->count[idx2] + 0.5),
                        (unsigned char)(d->gsum[idx2] / d->count[idx2] + 0.5),
                        (unsigned char)(d->bsum[idx2] / d->count[idx2] + 0.5)
                    };
                    if (c2.r != col.r || c2.g != col.g || c2.b != col.b)
                        break;
                    end++;
                }

                t_fg(t, col);
                for (int i = c; i < end; i++)
                    put_braille(d->mask[(size_t)r * d->cols + i]);
                t_reset(t);
                c = end;
            }
        }

        if (row_cb)
            row_cb(ctx, r);
        fputc('\n', stdout);
    }
}
