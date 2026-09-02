/*
 * Radar: the largest entries plotted as spokes off a shared centre, drawn as
 * a filled braille polygon, biggest entry reaching the outer ring. Stacked
 * stays a row of proportional symbols followed by its own legend - there is
 * no shape a stacked bar needs beyond that.
 */

#define _DEFAULT_SOURCE

#include "chart.h"
#include "dots.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* More spokes than this and neither the polygon nor its labels survive. */
#define RADAR_SPOKES  10
#define MAX_ROWS      16
#define LABEL_LEN     24

static void spaces(int n)
{
    for (int i = 0; i < n; i++)
        fputc(' ', stdout);
}

/* Whichever slice covers this fraction of the total. */
static int slice_at(const Chart *d, double f)
{
    for (int i = 0; i < d->n; i++)
        if (f < d->cum[i])
            return i;
    return d->n - 1;
}

/* Last resort: no room for a circle, or too few spokes to be worth one.
 * Prints at most `rows` lines and returns how many it used. */
static int text_list(ChartCtx *x, const int *idx, int k, int rows)
{
    int width = x->cols - x->indent;
    if (width > x->legend_w)
        width = x->legend_w;
    if (width < 1)
        return 0;

    bool show_files = width >= 52;
    int used = 0;

    for (int i = 0; i < k && used < rows; i++, used++) {
        spaces(x->indent);
        legend_line(x->t, x->data, idx[i], width, show_files);
        fputc('\n', stdout);
    }
    return used;
}

/* ================================================================== radar */

typedef struct {
    ChartCtx *x;
    const int *idx;
    int        k;
    int        gutter;                  /* columns of axis label, 0 for none */
    int        legend_w;
    int        label_rows;
    int        owner[MAX_ROWS];         /* spoke labelled on this row, or -1 */
    char       label[MAX_ROWS][LABEL_LEN];
    bool       show_files;
} RadarRows;

static void radar_row(void *ctx, int row)
{
    RadarRows *r = ctx;

    if (r->gutter > 0) {
        int owner = row < r->label_rows ? r->owner[row] : -1;
        int used = 1;

        fputc(' ', stdout);
        if (owner >= 0) {
            t_fg(r->x->t, r->x->data->slice[owner]->color);
            fputs(r->label[row], stdout);
            t_reset(r->x->t);
            used += display_width(r->label[row]);
        }
        spaces(r->gutter - used);
    }

    if (row < r->k) {
        fputs("  ", stdout);
        legend_line(r->x->t, r->x->data, r->idx[row], r->legend_w,
                    r->show_files);
    }
}

/* Outer ring radius (subpixels), with room left for a vertex dot on it. */
static double radar_radius(double subw, double subh, double *dot_out)
{
    double r = (subw < subh ? subw : subh) / 2.0;
    double dot = r * 0.06;
    if (dot < 1.5) dot = 1.5;
    if (dot > 4.0) dot = 4.0;

    r -= dot + 1.0;
    if (r < 3.0)
        r = 3.0;
    if (dot_out)
        *dot_out = dot;
    return r;
}

static void draw_radar(Dots *dt, const Chart *d, const int *idx, int k,
                       double subw, double subh, RGB guide)
{
    double cx = subw / 2.0, cy = subh / 2.0;
    double dot;
    double r = radar_radius(subw, subh, &dot);

    /* Quarter rings as thin lines. Below a certain radius they crowd into a
     * smudge, so a small radar keeps the outer ring and the halfway mark
     * only. */
    int step = r < 16.0 ? 2 : 1;
    for (int g = step; g <= 4; g += step) {
        double rr = r * g / 4.0;
        for (double a = 0.0; a < 360.0; a += 3.0) {
            double rad = a * M_PI / 180.0;
            double px = cx + rr * sin(rad), py = cy - rr * cos(rad);
            dots_line(dt, px, py, px, py, 1.0, guide);
        }
    }
    for (int i = 0; i < k; i++) {
        double a = 2.0 * M_PI * i / k;
        dots_line(dt, cx, cy, cx + r * sin(a), cy - r * cos(a), 1.0, guide);
    }

    long long peak = 0;
    for (int i = 0; i < k; i++)
        if (d->slice[idx[i]]->size > peak)
            peak = d->slice[idx[i]]->size;

    DotPt pts[RADAR_SPOKES];
    for (int i = 0; i < k; i++) {
        double v = peak > 0 ? (double)d->slice[idx[i]]->size / (double)peak
                            : 0.0;
        double a = 2.0 * M_PI * i / k;
        pts[i].x = cx + r * v * sin(a);
        pts[i].y = cy - r * v * cos(a);
    }

    RGB body = d->slice[idx[0]]->color;

    if (k >= 3)
        dots_polygon(dt, pts, k, body);
    for (int i = 0; i < k; i++) {
        int j = (i + 1) % k;
        dots_line(dt, pts[i].x, pts[i].y, pts[j].x, pts[j].y, 1.5, body);
    }
    for (int i = 0; i < k; i++)
        dots_circle(dt, pts[i].x, pts[i].y, dot, d->slice[idx[i]]->color);
}

void chart_radar(ChartCtx *x)
{
    const Chart *d = x->data;
    if (!d || d->n <= 0 || x->rows <= 0)
        return;

    /* Spokes come off the front of the list. When there are more entries than
     * spokes and the data already pools its tail, that pooled slice keeps the
     * last spoke; nothing is pooled here that was not pooled upstream. */
    int idx[RADAR_SPOKES];
    int k = 0;

    if (d->n <= RADAR_SPOKES) {
        for (int i = 0; i < d->n; i++)
            idx[k++] = i;
    } else {
        bool pooled = d->slice[d->n - 1]->aggregate;
        int take = pooled ? RADAR_SPOKES - 1 : RADAR_SPOKES;
        for (int i = 0; i < take; i++)
            idx[k++] = i;
        if (pooled)
            idx[k++] = d->n - 1;
    }
    if (k < 3) {
        text_list(x, idx, k, x->rows);
        return;
    }

    int avail = x->cols - x->indent;
    int legend_w = x->legend_w;
    if (legend_w > avail)
        legend_w = avail;
    bool show_files = legend_w >= 52;

    bool side = legend_w >= 20 && avail >= legend_w + 27;
    int room = side ? avail - legend_w - 3 : avail;

    int budget = side ? x->rows : x->rows - k;
    if (budget > MAX_ROWS)
        budget = MAX_ROWS;

    /* A round shape needs twice as many columns of cells as rows (braille
     * subpixels are close to square once spread over a cell's usual
     * proportions). */
    int crows = room / 2;
    if (crows > budget)
        crows = budget;
    int ccols = crows * 2;
    if (ccols > room)
        ccols = room;

    Dots *dt = (crows >= 5 && ccols >= 10) ? dots_new(ccols, crows) : NULL;
    if (!dt) {
        text_list(x, idx, k, x->rows);
        return;
    }

    int gutter = 0;
    if (side) {
        gutter = room - ccols;
        if (gutter > 14)
            gutter = 14;
        if (gutter < 7)
            gutter = 0;
    }

    RGB guide = { 100, 100, 112 };
    draw_radar(dt, d, idx, k, ccols * 2.0, crows * 4.0, guide);

    RadarRows rr;
    memset(&rr, 0, sizeof(rr));
    rr.x = x;
    rr.idx = idx;
    rr.k = k;
    rr.gutter = gutter;
    rr.legend_w = legend_w;
    rr.label_rows = crows;
    rr.show_files = show_files;
    for (int i = 0; i < crows && i < MAX_ROWS; i++)
        rr.owner[i] = -1;

    /* Only the top spoke and the right-hand ones can be labelled: the gutter
     * is the sole place text can sit, and a label there has to be on the row
     * its axis actually ends on. */
    if (gutter > 0) {
        double cy = crows * 4.0 / 2.0;
        double r = radar_radius(ccols * 2.0, crows * 4.0, NULL);

        for (int i = 0; i < k; i++) {
            double a = 2.0 * M_PI * i / k;
            if (i > 0 && sin(a) < 0.25)
                continue;

            int row = (int)((cy - r * cos(a)) / 4.0);
            if (row < 0) row = 0;
            if (row >= crows) row = crows - 1;
            if (row >= MAX_ROWS || rr.owner[row] >= 0)
                continue;

            fit(d->slice[idx[i]]->name, rr.label[row], LABEL_LEN, gutter - 1);
            rr.owner[row] = idx[i];
        }
    }

    if (side) {
        int total = crows > k ? crows : k;
        if (total > x->rows)
            total = x->rows;
        dots_present(dt, x->t, x->indent, total, radar_row, &rr);
    } else {
        dots_present(dt, x->t, x->indent, crows, NULL, NULL);
        text_list(x, idx, k, x->rows - crows);
    }

    dots_free(dt);
}

/* =========================================================== stacked bar */

void chart_stacked(ChartCtx *x)
{
    const Chart *d = x->data;
    if (!d || d->n <= 0 || x->rows <= 0)
        return;

    int cols = x->cols - x->indent - 1;
    int rows = x->rows;

    if (cols >= 8 && rows > 0) {
        spaces(x->indent);
        for (int c = 0; c < cols; c++) {
            const Entry *e = d->slice[slice_at(d, (c + 0.5) / cols)];
            if (x->t->color) {
                t_fg(x->t, e->color);
                fputs("\xe2\x96\x88", stdout);       /* U+2588 full block */
                t_reset(x->t);
            } else {
                fputc(e->sym, stdout);
            }
        }
        fputc('\n', stdout);
        rows--;
    }

    int idx[64];
    int k = d->n < 64 ? d->n : 64;
    for (int i = 0; i < k; i++)
        idx[i] = i;
    text_list(x, idx, k, rows);
}
