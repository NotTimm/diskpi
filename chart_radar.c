/*
 * Two shapes that answer different questions about the same numbers.
 *
 * The radar plots each entry against the largest one, so the biggest reaches
 * the outer ring and everything else is read as a fraction of it. The stacked
 * bar plots each entry against the total, so the whole width is 100%.
 *
 * Both have to put text where a canvas cannot: canvas_present() paints cells
 * and then writes the caller's text to the right of them, so nothing can be
 * printed on top of the drawing. The radar therefore labels its axes in a
 * gutter between the circle and the legend, and the stacked bar is split into
 * an upper canvas, one row of terminal text painted in the segment colours,
 * and a lower canvas - the text row is a real slice of the bar rather than a
 * caption under it.
 */

#define _DEFAULT_SOURCE

#include "chart.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* More spokes than this and neither the polygon nor its labels survive. */
#define RADAR_SPOKES  10
#define RADAR_ROWS    20
#define LABEL_LEN     24

/* ------------------------------------------------------------ small utils */

/* Axes and rules are drawn out of the background, not painted onto it. */
static RGB guide_color(const Term *t)
{
    if (!t->have_bg) {
        RGB fixed = { 110, 110, 120 };
        return fixed;
    }

    RGB mid = { 128, 128, 134 };
    RGB out = {
        (unsigned char)((t->bg.r + mid.r) / 2),
        (unsigned char)((t->bg.g + mid.g) / 2),
        (unsigned char)((t->bg.b + mid.b) / 2)
    };
    return out;
}

static RGB ink_on(RGB fill)
{
    double lum = 0.299 * fill.r + 0.587 * fill.g + 0.114 * fill.b;
    RGB dark = { 24, 24, 28 }, light = { 248, 248, 250 };
    return lum > 150.0 ? dark : light;
}

/* Columns legend_line() really occupies: it clamps its own name field at six,
 * which widens the line past the width it was asked for. */
static int legend_span(int width, bool show_files)
{
    int name_w = width - 2 - 2 - (show_files ? 11 : 0) - 8 - 1 - 6;
    return name_w < 6 ? width + (6 - name_w) : width;
}

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

/* Last resort: no canvas, or a cell budget too small to draw anything in.
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
        legend_line(x->t, x->data, idx[i], width, x->mode, show_files);
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
    int        owner[RADAR_ROWS];       /* spoke labelled on this row, or -1 */
    char       label[RADAR_ROWS][LABEL_LEN];
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
                    r->x->mode, r->show_files);
    }
}

/* Outer ring radius, with room left for a vertex dot sitting on it. */
static double radar_radius(const Canvas *cv, double *dot_out)
{
    double r = (cv->w < cv->h ? cv->w : cv->h) / 2.0;
    double dot = r * 0.06;

    if (dot < 1.2) dot = 1.2;
    if (dot > 4.0) dot = 4.0;

    r -= dot + 1.0;
    if (r < 2.0)
        r = 2.0;

    if (dot_out)
        *dot_out = dot;
    return r;
}

static void draw_radar(Canvas *cv, const Chart *d, const int *idx, int k,
                       Mode mode, RGB guide)
{
    double cx = cv->w / 2.0, cy = cv->h / 2.0;
    double dot;
    double r = radar_radius(cv, &dot);

    /* Quarter rings, one canvas pixel each - the thinnest rule the surface
     * can express. Below a certain radius they crowd into a smudge, so a
     * small radar keeps the outer ring and the halfway mark only. */
    int step = r < 12.0 ? 2 : 1;
    for (int g = step; g <= 4; g += step) {
        double rr = r * g / 4.0;
        canvas_ring(cv, cx, cy, rr - 0.5, rr + 0.5, guide,
                    g == 4 ? 0.55 : 0.32);
    }
    for (int i = 0; i < k; i++) {
        double a = 2.0 * M_PI * i / k;
        canvas_line(cv, cx, cy, cx + r * sin(a), cy - r * cos(a),
                    1.0, guide, 0.38);
    }

    long long peak = 0;
    for (int i = 0; i < k; i++)
        if (d->slice[idx[i]]->size > peak)
            peak = d->slice[idx[i]]->size;

    Pt pts[RADAR_SPOKES + 1];
    for (int i = 0; i < k; i++) {
        double v = peak > 0 ? (double)d->slice[idx[i]]->size / (double)peak
                            : 0.0;
        double a = 2.0 * M_PI * i / k;
        pts[i].x = cx + r * v * sin(a);
        pts[i].y = cy - r * v * cos(a);
    }
    pts[k] = pts[0];

    /* The fill belongs to the entry that defines the outer ring. */
    RGB body = d->slice[idx[0]]->color;
    double stroke = mode == MODE_PIXELS ? 2.0 : 1.0;

    if (k >= 3)
        canvas_polygon(cv, pts, k, body, 0.30);
    canvas_polyline(cv, pts, k + 1, stroke, body, 0.95);

    for (int i = 0; i < k; i++)
        canvas_circle(cv, pts[i].x, pts[i].y, dot,
                      d->slice[idx[i]]->color, 1.0);
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

    int avail = x->cols - x->indent;
    int legend_w = x->legend_w;
    if (legend_w > avail)
        legend_w = avail;
    bool show_files = legend_w >= 52;
    int span = legend_span(legend_w, show_files);

    bool side = legend_w >= 20 && avail >= span + 27;
    int gutter = 0;
    /* Two columns of air before the legend, and one column never used, so a
     * full-width legend line cannot end on the last cell of the terminal. */
    int room = side ? avail - span - 3 : avail;

    /* Cells are taller than they are wide, so a round circle needs more
     * columns than rows in whatever proportion this terminal reports. */
    double aspect = 2.0;
    if (x->mode == MODE_PIXELS && x->t->cell_w > 0 && x->t->cell_h > 0)
        aspect = (double)x->t->cell_h / (double)x->t->cell_w;
    if (aspect < 1.0) aspect = 1.0;
    if (aspect > 3.0) aspect = 3.0;

    int budget = side ? x->rows : x->rows - k;
    if (budget > RADAR_ROWS)
        budget = RADAR_ROWS;

    int crows = (int)(room / aspect);
    if (crows > budget)
        crows = budget;

    int ccols = (int)(crows * aspect + 0.5);
    if (ccols > room)
        ccols = room;

    Canvas *cv = NULL;
    if (crows >= 6 && ccols >= 14)
        cv = canvas_new(x->t, x->mode, ccols, crows);

    if (!cv) {
        text_list(x, idx, k, x->rows);
        return;
    }

    if (side) {
        gutter = room - ccols;
        if (gutter > 14)
            gutter = 14;
        if (gutter < 7)
            gutter = 0;
    }

    draw_radar(cv, d, idx, k, x->mode, guide_color(x->t));

    RadarRows rr;
    memset(&rr, 0, sizeof(rr));
    rr.x = x;
    rr.idx = idx;
    rr.k = k;
    rr.gutter = gutter;
    rr.legend_w = legend_w;
    rr.label_rows = crows;
    rr.show_files = show_files;
    for (int i = 0; i < crows; i++)
        rr.owner[i] = -1;

    /* Only the top spoke and the right-hand ones can be labelled: the gutter
     * is the sole place text can sit, and a label there has to be on the row
     * its axis actually ends on. The top spoke is the largest entry and the
     * one the whole shape is scaled against, so it is never dropped: its
     * vertex lands on the first row, level with the top of the gutter.
     * One label per row, first spoke wins. */
    if (gutter > 0) {
        double cy = cv->h / 2.0;
        double r = radar_radius(cv, NULL);

        for (int i = 0; i < k; i++) {
            double a = 2.0 * M_PI * i / k;
            if (i > 0 && sin(a) < 0.25)
                continue;

            int row = (int)((cy - r * cos(a)) / cv->px_h);
            if (row < 0) row = 0;
            if (row >= crows) row = crows - 1;
            if (rr.owner[row] >= 0)
                continue;

            fit(d->slice[idx[i]]->name, rr.label[row], LABEL_LEN, gutter - 1);
            rr.owner[row] = idx[i];
        }
    }

    if (side) {
        int total = crows > k ? crows : k;
        if (total > x->rows)
            total = x->rows;
        canvas_present(cv, x->t, x->mode, x->indent, total, radar_row, &rr);
    } else {
        /* No gutter and no side legend: nothing belongs to the right. */
        canvas_present(cv, x->t, x->mode, x->indent, crows, NULL, NULL);
        text_list(x, idx, k, x->rows - crows);
    }

    canvas_free(cv);
}

/* =========================================================== stacked bar */

/* An entry of size zero occupies no width at all, so the ends of the bar are
 * the first and last entries that actually get drawn, not 0 and n - 1. */
static void drawn_range(const Chart *d, int *first, int *last)
{
    *first = *last = -1;
    for (int i = 0; i < d->n; i++) {
        double raw = d->cum[i] - (i > 0 ? d->cum[i - 1] : 0.0);
        if (raw <= 0.0)
            continue;
        if (*first < 0)
            *first = i;
        *last = i;
    }
}

/* Where segment i sits along a bar `w` pixels wide, insets included. The
 * label row measures itself with this too, so the two can never disagree. */
static bool seg_span(const Chart *d, int i, int first, int last, double w,
                     double *sx, double *sw)
{
    double x0 = (i > 0 ? d->cum[i - 1] : 0.0) * w;
    double raw = d->cum[i] * w - x0;
    if (raw <= 0.0)
        return false;

    /* A hairline of background between neighbours, never more than a quarter
     * of the segment, so a thin slice still shows. Segments narrower than a
     * pixel and a half get none at all: there the gap would take most of what
     * the surface can show of them, and a run of them frays the bar to dots
     * instead of separating anything. */
    double hair = raw >= 1.5 ? fmin(0.5, raw / 4.0) : 0.0;
    double in0 = i > first ? hair : 0.0;
    double in1 = i < last  ? hair : 0.0;

    *sx = x0 + in0;
    *sw = raw - in0 - in1;
    return *sw > 0.0;
}

/* Both halves of the bar are drawn in one coordinate system measured in
 * output rows, then shifted by the row each canvas starts on; the rounded
 * ends run off the edge of their canvas and are clipped there. */
static void draw_stack(Canvas *cv, const Chart *d, int origin,
                       int bar_top, int bar_cells)
{
    double y = (bar_top - origin) * cv->px_h;
    double h = bar_cells * cv->px_h;
    double rad = h / 2.0;
    int first, last;

    drawn_range(d, &first, &last);

    for (int i = 0; i < d->n; i++) {
        double sx, sw;
        if (!seg_span(d, i, first, last, cv->w, &sx, &sw))
            continue;

        RGB col = d->slice[i]->color;

        if (i != first && i != last) {
            canvas_rect(cv, sx, y, sw, h, col, 1.0);
            continue;
        }

        /* Round the outer ends only: the rounded rectangle rounds all four
         * corners, so a square patch is laid back over the inner ones. */
        canvas_round_rect(cv, sx, y, sw, h, rad, col, 1.0);
        if (i == first && i != last)
            canvas_rect(cv, sx + rad, y, sw - rad, h, col, 1.0);
        else if (i == last && i != first)
            canvas_rect(cv, sx, y, sw - rad, h, col, 1.0);
    }
}

static void pct_text(double pct, char *out, size_t len)
{
    double v = pct * 100.0;
    if (v >= 9.95)
        snprintf(out, len, "%.0f%%", v);
    else if (v >= 0.95)
        snprintf(out, len, "%.1f%%", v);
    else
        snprintf(out, len, "%.2f%%", v);
}

/* The row through the middle of the bar, painted as coloured cells with each
 * wide-enough segment's share centred in it. A cell is resolved the way the
 * canvas resolves a pixel - by how much of it each segment covers - so the
 * hairline gaps carry through this row at whatever strength one cell of
 * width can express. */
static void stack_label_row(ChartCtx *x, int cols, double w)
{
    const Chart *d = x->data;
    int *owner = malloc((size_t)cols * sizeof(int));
    RGB *paint = malloc((size_t)cols * sizeof(RGB));
    char *text = malloc((size_t)cols);
    if (!owner || !paint || !text) {
        free(owner);
        free(paint);
        free(text);
        fputc('\n', stdout);
        return;
    }

    int first, last;
    double cell = w / cols;
    drawn_range(d, &first, &last);

    for (int c = 0; c < cols; c++) {
        double x0 = c * cell, x1 = x0 + cell;
        double r = 0.0, g = 0.0, b = 0.0, cov = 0.0, best = 0.0;

        owner[c] = -1;
        text[c] = ' ';

        for (int i = 0; i < d->n; i++) {
            double sx, sw;
            if (!seg_span(d, i, first, last, w, &sx, &sw))
                continue;

            double lo = sx > x0 ? sx : x0;
            double hi = sx + sw < x1 ? sx + sw : x1;
            if (hi <= lo)
                continue;

            RGB col = d->slice[i]->color;
            r += col.r * (hi - lo);
            g += col.g * (hi - lo);
            b += col.b * (hi - lo);
            cov += hi - lo;
            if (hi - lo > best) {
                best = hi - lo;
                owner[c] = i;
            }
        }

        if (cov <= 0.0)
            continue;

        r /= cov; g /= cov; b /= cov;
        if (x->t->have_bg && cov < cell) {
            double a = cov / cell;
            r = r * a + x->t->bg.r * (1.0 - a);
            g = g * a + x->t->bg.g * (1.0 - a);
            b = b * a + x->t->bg.b * (1.0 - a);
        }
        paint[c].r = (unsigned char)(r + 0.5);
        paint[c].g = (unsigned char)(g + 0.5);
        paint[c].b = (unsigned char)(b + 0.5);
    }

    for (int i = 0; i < d->n; i++) {
        int c0 = -1, c1 = -1;
        for (int c = 0; c < cols; c++)
            if (owner[c] == i) {
                if (c0 < 0)
                    c0 = c;
                c1 = c;
            }
        if (c0 < 0)
            continue;

        char buf[16];
        pct_text(d->pct[i], buf, sizeof(buf));
        int len = (int)strlen(buf);
        if (len + 2 > c1 - c0 + 1)
            continue;                   /* would touch the edges: leave it */

        int at = c0 + (c1 - c0 + 1 - len) / 2;
        memcpy(text + at, buf, (size_t)len);
    }

    spaces(x->indent);
    for (int c = 0; c < cols; ) {
        if (owner[c] < 0) {
            fputc(' ', stdout);
            c++;
            continue;
        }

        int run = c + 1;
        while (run < cols && owner[run] >= 0 &&
               paint[run].r == paint[c].r && paint[run].g == paint[c].g &&
               paint[run].b == paint[c].b)
            run++;

        t_bg(x->t, paint[c]);
        t_fg(x->t, ink_on(paint[c]));
        for (int i = c; i < run; i++)
            fputc(text[i], stdout);
        t_reset(x->t);
        c = run;
    }
    fputc('\n', stdout);

    free(owner);
    free(paint);
    free(text);
}

/* No canvas: one row of symbols at the right proportions, then the legend. */
static void stack_text(ChartCtx *x)
{
    const Chart *d = x->data;
    int cols = x->cols - x->indent - 1;
    int rows = x->rows;

    if (cols >= 8 && rows > 0) {
        spaces(x->indent);
        for (int c = 0; c < cols; c++) {
            const Entry *e = d->slice[slice_at(d, (c + 0.5) / cols)];
            if (x->mode != MODE_ASCII && x->t->color) {
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

void chart_stacked(ChartCtx *x)
{
    const Chart *d = x->data;
    if (!d || d->n <= 0 || x->rows <= 0)
        return;

    /* One column stays clear on the right: a row that fills the last cell of
     * the terminal wraps on some of them. */
    int cols = x->cols - x->indent - 1;
    int legend_w = x->legend_w;
    if (legend_w > cols)
        legend_w = cols;
    if (legend_w < 1)
        legend_w = 1;
    bool show_files = legend_w >= 52;
    int span = legend_span(legend_w, show_files);

    int lcols = (cols + 2) / (span + 2);
    if (lcols < 1) lcols = 1;
    if (lcols > 3) lcols = 3;

    int legend_rows = (d->n + lcols - 1) / lcols;
    int draw_rows = x->rows - legend_rows - 1;

    /* Three rows is the thinnest bar that still has a middle to write in.
     * Below that the legend gives ground first, then loses entries. */
    if (draw_rows < 3) {
        draw_rows = 3;
        legend_rows = x->rows - draw_rows - 1;
    }
    if (draw_rows > 9)
        draw_rows = 9;
    if (legend_rows < 0)
        legend_rows = 0;

    /* An odd bar has a middle row to write the shares in; an even one would
     * put the text off centre, with more bar above it than below. */
    int bar_cells = draw_rows / 2;
    bar_cells |= 1;
    if (bar_cells < 3)
        bar_cells = 3;
    if (bar_cells > draw_rows)
        bar_cells = draw_rows;

    int bar_top = (draw_rows - bar_cells) / 2;
    int label_row = bar_top + bar_cells / 2;    /* crosses the bar's middle */
    int top_rows = label_row;
    int bot_rows = draw_rows - label_row - 1;

    Canvas *top = NULL, *bot = NULL;
    if (cols >= 12 && x->rows >= 5 && top_rows >= 1 && bot_rows >= 1) {
        top = canvas_new(x->t, x->mode, cols, top_rows);
        bot = canvas_new(x->t, x->mode, cols, bot_rows);
    }
    if (!top || !bot) {
        canvas_free(top);
        canvas_free(bot);
        stack_text(x);
        return;
    }

    draw_stack(top, d, 0, bar_top, bar_cells);
    draw_stack(bot, d, label_row + 1, bar_top, bar_cells);

    canvas_present(top, x->t, x->mode, x->indent, top_rows, NULL, NULL);
    stack_label_row(x, cols, top->w);
    canvas_present(bot, x->t, x->mode, x->indent, bot_rows, NULL, NULL);

    canvas_free(top);
    canvas_free(bot);

    int left = x->rows - draw_rows;
    if (left > 0 && legend_rows > 0) {
        fputc('\n', stdout);
        left--;
    }

    for (int r = 0; r < legend_rows && r < left; r++) {
        spaces(x->indent);
        for (int c = 0; c < lcols; c++) {
            int i = r * lcols + c;
            if (i >= d->n)
                break;
            if (c > 0)
                fputs("  ", stdout);
            legend_line(x->t, d, i, legend_w, x->mode, show_files);
        }
        fputc('\n', stdout);
    }
}
