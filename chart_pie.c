/*
 * Pie and doughnut, drawn as a circle of braille dots (dots.c) with the
 * legend beside it. The circle takes every row and column the legend does
 * not need, so a big window gets a big chart.
 *
 * Wedges run clockwise from twelve o'clock and are inset at both ends by a
 * hairline so neighbouring fills never touch. A wedge too thin to survive
 * its own gap keeps its full span instead of disappearing.
 *
 * Any wedge with room for it is labelled with its share, written over the
 * dots by the row callback: the cursor is rewound across the columns it has
 * just printed, the text goes down, and it is wound forward again before
 * the legend. That is safe because the label only ever covers cells of its
 * own wedge.
 */

#define _DEFAULT_SOURCE

#include "chart.h"
#include "dots.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define GAP_SUB     1.5     /* hairline between wedges, in subpixels */
#define GAP_MIN_R  20.0     /* under this radius the hairline costs more
                             * than the separation is worth */
#define HOLE_R      0.55    /* doughnut inner radius, in outer radii */
#define GUTTER      3       /* columns between the circle and the legend */
#define LEGEND_MIN 26       /* narrower than this, legend_line overflows */
#define MAX_ROWS   44       /* past this a circle is just a lot of dots */
#define LABEL_MIN_PCT 0.035 /* below this a wedge label is clutter */

/* ------------------------------------------------------------- text bits */

/* Legend for terminals too narrow for the shared legend line. */
static void compact_line(const ChartCtx *x, int i, int width)
{
    const Entry *e = x->data->slice[i];
    char size_buf[32], raw[600], name_buf[600];

    human_size(e->size, size_buf, sizeof(size_buf));
    int size_w = display_width(size_buf);
    int name_w = width - 2 - size_w - 1;
    if (name_w < 3)
        name_w = 3;

    snprintf(raw, sizeof(raw), "%s%s", e->name, e->is_dir ? "/" : "");
    fit(raw, name_buf, sizeof(name_buf), name_w);

    if (!x->t->color) {
        printf("%c ", e->sym);
    } else {
        t_fg(x->t, e->color);
        fputs("\xe2\x97\x8f ", stdout);          /* U+25CF black circle */
        t_reset(x->t);
    }

    fputs(name_buf, stdout);
    for (int k = display_width(name_buf); k < name_w + 1; k++)
        fputc(' ', stdout);
    fputs(size_buf, stdout);
}

static void more_line(const ChartCtx *x, int more)
{
    t_dim(x->t);
    printf("%d more", more);
    t_reset(x->t);
}

/* No room for a circle worth drawing: just the numbers. */
static void pie_fallback(const ChartCtx *x)
{
    int width = x->cols - x->indent;
    if (width < 8)
        width = 8;
    if (width > 60)
        width = 60;

    int rows = x->rows;
    if (rows <= 0)
        return;

    int n = x->data->n;
    int listed = n, more = 0;
    if (n > rows) {
        listed = rows - 1;
        more = n - listed;
    }

    for (int i = 0; i < listed; i++) {
        for (int k = 0; k < x->indent; k++)
            fputc(' ', stdout);
        if (width >= LEGEND_MIN)
            legend_line(x->t, x->data, i, width, width >= 52);
        else
            compact_line(x, i, width);
        fputc('\n', stdout);
    }

    if (more > 0) {
        for (int k = 0; k < x->indent; k++)
            fputc(' ', stdout);
        more_line(x, more);
        fputc('\n', stdout);
    }
}

/* --------------------------------------------------------------- overlays */

typedef struct {
    int  row, col;
    RGB  color;
    bool bold;
    char text[24];
} Overlay;

typedef struct {
    const ChartCtx *x;
    int      canvas_cols;
    int      legend_start;
    int      legend_n;        /* entries actually listed */
    int      more;            /* entries folded into a trailing summary */
    bool     show_files;
    Overlay *over;
    int      n_over;
} PieRows;

/* Ink that stays readable on top of the wedge it sits in. */
static RGB ink_on(RGB fill)
{
    double lum = 0.299 * fill.r + 0.587 * fill.g + 0.114 * fill.b;
    RGB dark = { 18, 18, 22 }, light = { 250, 250, 252 };
    return lum > 145.0 ? dark : light;
}

static int cmp_overlay(const void *a, const void *b)
{
    const Overlay *oa = a, *ob = b;
    if (oa->row != ob->row)
        return oa->row < ob->row ? -1 : 1;
    return oa->col < ob->col ? -1 : (oa->col > ob->col ? 1 : 0);
}

/* Rewind across the dots just printed, drop the labels for this row in
 * left-to-right order, and wind forward again for the legend. */
static void draw_overlays(const PieRows *p, int row)
{
    int cur = p->canvas_cols;
    bool moved = false;

    for (int i = 0; i < p->n_over; i++) {
        const Overlay *o = &p->over[i];
        if (o->row != row)
            continue;

        int len = display_width(o->text);
        if (len <= 0 || o->col < 0 || o->col + len > p->canvas_cols)
            continue;

        int delta = cur - o->col;
        if (delta > 0)
            printf("\033[%dD", delta);
        else if (delta < 0)
            printf("\033[%dC", -delta);

        if (o->bold)
            t_bold(p->x->t);
        else
            t_fg(p->x->t, o->color);
        fputs(o->text, stdout);
        t_reset(p->x->t);

        cur = o->col + len;
        moved = true;
    }

    if (moved && cur < p->canvas_cols)
        printf("\033[%dC", p->canvas_cols - cur);
}

static void pie_row(void *ctx, int row)
{
    const PieRows *p = ctx;

    draw_overlays(p, row);

    int li = row - p->legend_start;
    if (li < 0)
        return;

    if (li < p->legend_n) {
        for (int k = 0; k < GUTTER; k++)
            fputc(' ', stdout);
        legend_line(p->x->t, p->x->data, li, p->x->legend_w, p->show_files);
    } else if (p->more > 0 && li == p->legend_n) {
        for (int k = 0; k < GUTTER; k++)
            fputc(' ', stdout);
        more_line(p->x, p->more);
    }
}

/* ---------------------------------------------------------------- drawing */

static void draw_wedges(Dots *dt, const Chart *c, double cx, double cy,
                        double r_in, double r_out)
{
    double r_ref = r_in > 0.0 ? (r_in + r_out) / 2.0 : r_out;
    double gap_deg = 0.0;
    if (c->n > 1 && r_out >= GAP_MIN_R && r_ref > 0.0)
        gap_deg = GAP_SUB / r_ref * 180.0 / M_PI;

    double prev = 0.0;
    for (int i = 0; i < c->n; i++) {
        double a0 = prev * 360.0;
        double a1 = c->cum[i] * 360.0;
        prev = c->cum[i];

        double span = a1 - a0;
        if (span <= 0.0)
            continue;

        if (gap_deg > 0.0 && span < 360.0 && span > 6.0 * gap_deg) {
            a0 += gap_deg / 2.0;
            a1 -= gap_deg / 2.0;
        }

        dots_wedge(dt, cx, cy, r_in, r_out, a0, a1, c->slice[i]->color);
    }
}

/* A share written into the middle of its own wedge, where the wedge is wide
 * enough at that radius to hold it without touching either edge. */
static int place_labels(Overlay *over, const ChartCtx *x, int cols, int rows,
                        double cxs, double cys, double r_in, double r_out)
{
    const Chart *c = x->data;
    int n_over = 0;
    double r_label = r_in > 0.0 ? (r_in + r_out) / 2.0 : r_out * 0.62;

    /* One label per row keeps them from running into each other. */
    bool *taken = calloc((size_t)rows, sizeof(bool));
    if (!taken)
        return 0;

    double prev = 0.0;
    for (int i = 0; i < c->n; i++) {
        double a0 = prev * 360.0, a1 = c->cum[i] * 360.0;
        prev = c->cum[i];

        double span = a1 - a0;
        if (span <= 0.0 || c->pct[i] < LABEL_MIN_PCT)
            continue;

        char text[24];
        snprintf(text, sizeof(text), "%.0f%%", c->pct[i] * 100.0);
        int len = display_width(text);

        /* The chord the wedge spans at the label's radius, in cells. */
        double chord = 2.0 * r_label * sin(span * M_PI / 360.0);
        if (span >= 360.0)
            chord = 2.0 * r_label;
        if (chord / 2.0 < len + 1)
            continue;

        double mid = (a0 + a1) / 2.0 * M_PI / 180.0;
        double px = cxs + r_label * sin(mid);
        double py = cys - r_label * cos(mid);

        int row = (int)(py / 4.0);
        int col = (int)lround(px / 2.0 - len / 2.0);
        if (row < 0 || row >= rows || taken[row])
            continue;
        if (col < 0)
            col = 0;
        if (col + len > cols)
            col = cols - len;
        if (col < 0)
            continue;

        taken[row] = true;
        over[n_over].row = row;
        over[n_over].col = col;
        over[n_over].color = ink_on(c->slice[i]->color);
        over[n_over].bold = false;
        snprintf(over[n_over].text, sizeof(over[n_over].text), "%s", text);
        n_over++;
    }

    free(taken);
    return n_over;
}

/* Two lines centred in the doughnut's hole, if the chord at their outer
 * edge is wide enough to hold them. Subpixels are 2 wide by 4 tall. */
static int place_hole_text(Overlay *over, const ChartCtx *x, int cols,
                           int rows, double cxs, double cys, double r_in)
{
    if (r_in <= 4.0)
        return 0;

    int row_a = (int)lround(cys / 4.0) - 1;
    if (row_a < 0 || row_a + 1 >= rows)
        return 0;

    double half = sqrt(r_in * r_in - 4.0 * 4.0);
    int room = (int)floor(2.0 * half / 2.0);
    if (room > cols)
        room = cols;
    if (room < 3)
        return 0;

    char size_buf[32], count_buf[48];
    human_size(x->total, size_buf, sizeof(size_buf));
    snprintf(count_buf, sizeof(count_buf), "%d entr%s", x->entries,
             x->entries == 1 ? "y" : "ies");

    const char *src[2] = { size_buf, count_buf };
    int n_over = 0;

    for (int k = 0; k < 2; k++) {
        int len = display_width(src[k]);
        if (len > room)
            break;

        int col = (int)lround(cxs / 2.0 - len / 2.0);
        if (col + len > cols)
            col = cols - len;
        if (col < 0)
            break;

        over[n_over].row = row_a + k;
        over[n_over].col = col;
        over[n_over].bold = (k == 0);
        over[n_over].color = (RGB){ 200, 200, 210 };
        snprintf(over[n_over].text, sizeof(over[n_over].text), "%s", src[k]);
        n_over++;
    }
    return n_over;
}

/* ------------------------------------------------------------------ entry */

void chart_pie(ChartCtx *x, bool doughnut)
{
    if (!x || !x->t || !x->data || x->data->n <= 0 || x->rows <= 0 ||
        x->cols <= 0)
        return;

    int legend_w = x->legend_w < LEGEND_MIN ? LEGEND_MIN : x->legend_w;
    int avail = x->cols - x->indent - legend_w - GUTTER;

    /* Braille dots are close to square once a cell's 2x4 subgrid is spread
     * over its usual on-screen proportions, so a round circle just needs
     * twice as many columns of cells as rows. The chart takes whatever the
     * legend leaves, in both directions. */
    int rows = x->rows > MAX_ROWS ? MAX_ROWS : x->rows;
    int cols = rows * 2;
    if (cols > avail) {
        cols = avail;
        rows = cols / 2;
    }
    if (rows > x->rows)
        rows = x->rows;

    Dots *dt = (rows >= 4 && cols >= 8) ? dots_new(cols, rows) : NULL;
    if (!dt) {
        pie_fallback(x);
        return;
    }

    double subw = cols * 2.0, subh = rows * 4.0;
    double cxs = subw / 2.0, cys = subh / 2.0;
    double r_out = (subw < subh ? subw : subh) / 2.0 - 1.0;
    if (r_out < 3.0) {
        dots_free(dt);
        pie_fallback(x);
        return;
    }
    double r_in = doughnut ? r_out * HOLE_R : 0.0;

    draw_wedges(dt, x->data, cxs, cys, r_in, r_out);

    PieRows p;
    memset(&p, 0, sizeof(p));
    p.x           = x;
    p.canvas_cols = cols;
    p.show_files  = x->legend_w >= 52;

    /* Room for a label per slice, plus the doughnut's two hole lines. */
    p.over = calloc((size_t)x->data->n + 2, sizeof(Overlay));
    if (p.over && x->t->color) {
        p.n_over = place_labels(p.over, x, cols, rows, cxs, cys, r_in, r_out);
        if (doughnut)
            p.n_over += place_hole_text(p.over + p.n_over, x, cols, rows,
                                        cxs, cys, r_in);
        qsort(p.over, (size_t)p.n_over, sizeof(Overlay), cmp_overlay);
    }

    int n = x->data->n;
    p.legend_n = n;
    if (n > x->rows) {
        p.legend_n = x->rows - 1;
        p.more = n - p.legend_n;
    }
    if (p.legend_n < 0)
        p.legend_n = 0;

    int legend_rows = p.legend_n + (p.more > 0 ? 1 : 0);
    p.legend_start = (rows - legend_rows) / 2;
    if (p.legend_start < 0)
        p.legend_start = 0;
    if (p.legend_start + legend_rows > x->rows)
        p.legend_start = x->rows - legend_rows;
    if (p.legend_start < 0)
        p.legend_start = 0;

    int total_rows = p.legend_start + legend_rows;
    if (total_rows < rows)
        total_rows = rows;
    if (total_rows > x->rows)
        total_rows = x->rows;

    dots_present(dt, x->t, x->indent, total_rows, pie_row, &p);
    dots_free(dt);
    free(p.over);
}
