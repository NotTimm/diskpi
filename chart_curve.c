/*
 * Cumulative share by rank - the Pareto curve.
 *
 * x is entry rank, biggest first; y is the running share of the total, so the
 * height of the curve at rank k is how much of the directory the k largest
 * things hold. The knee, and the ranks where the curve passes 50 and 80
 * percent, are the reason to draw it at all.
 *
 * Text that has to sit outside the legend column - the percent labels in the
 * left gutter, the crossing captions under the plot - is written from the
 * row callback with CHA, which is safe because canvas_present has already
 * filled those cells with spaces and, in MODE_PIXELS, the image lands only on
 * the canvas rows and columns, never on the gutter or the caption row.
 */

#define _DEFAULT_SOURCE

#include "chart.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ROWS   64          /* chart_rows is capped well below this */
#define N_LEVELS   5           /* 100, 0, 50, 25, 75 - in claim order */

typedef struct {
    ChartCtx *x;
    int       legend_rows;
    bool      show_files;
    int       gutter;                  /* left label column, 0 when dropped */
    int       ylab_row[N_LEVELS];      /* -1 when the row was already taken */
    char      ylab[N_LEVELS][8];
    int       caption_row;             /* -1 when there is no room */
    int       cap_col[2];              /* absolute column, -1 when skipped */
    char      cap_text[2][32];
} CurveCtx;

/* ------------------------------------------------------------- neutrals */

static RGB mix(RGB a, RGB b, double f)
{
    RGB out = {
        (unsigned char)(a.r + (b.r - a.r) * f + 0.5),
        (unsigned char)(a.g + (b.g - a.g) * f + 0.5),
        (unsigned char)(a.b + (b.b - a.b) * f + 0.5)
    };
    return out;
}

/* Rules are drawn out of the background so they read as paper, not data. */
static RGB rule_color(const Term *t, double toward_grey)
{
    RGB grey = { 128, 128, 132 };
    if (t->have_bg)
        return mix(t->bg, grey, toward_grey);
    return (RGB){ 110, 110, 120 };
}

/* Centre of the pixel row (or column) a coordinate falls in. A one-pixel rule
 * left on a pixel boundary splits its coverage in two, and a terminal that
 * never told us its background drops anything under half alpha outright - so
 * an unsnapped rule simply vanishes there. */
static double snap(double v)
{
    return floor(v) + 0.5;
}

/* One gridline. Half blocks have no faintness to spare - the canvas drops
 * anything under half alpha when the terminal never named its background -
 * so a solid rule there comes out as loud as the curve. Dotting it keeps the
 * reference and gives the weight back. */
static void rule_line(Canvas *c, double x0, double y0, double x1, double y1,
                      RGB col, double alpha, double step)
{
    if (step <= 0.0) {
        canvas_line(c, x0, y0, x1, y1, 1.0, col, alpha);
        return;
    }

    double dx = x1 - x0, dy = y1 - y0;
    double len = sqrt(dx * dx + dy * dy);
    if (len <= 0.0)
        return;

    for (double t = 0.0; t <= len; t += step) {
        double f = t / len;
        double px = snap(x0 + dx * f), py = snap(y0 + dy * f);
        canvas_line(c, px, py, px, py, 1.0, col, alpha);
    }
}

/* --------------------------------------------------------------- shared */

/* Cumulative fractions, clamped and forced monotone: a caller that hands us
 * a zero total leaves NaNs in cum[], and a flat zero curve is a truthful
 * picture of a directory with nothing in it. */
static double *cumulative(const Chart *d)
{
    double *cum = malloc((size_t)d->n * sizeof(double));
    if (!cum)
        return NULL;

    double prev = 0.0;
    for (int i = 0; i < d->n; i++) {
        double v = d->cum[i];
        if (!(v >= 0.0))
            v = prev;
        if (v > 1.0)
            v = 1.0;
        if (v < prev)
            v = prev;
        cum[i] = prev = v;
    }
    return cum;
}

/* Smallest rank whose cumulative share reaches thr, or 0 if none does. */
static int rank_at(const double *cum, int n, double thr)
{
    for (int i = 0; i < n; i++)
        if (cum[i] >= thr)
            return i + 1;
    return 0;
}

/* -------------------------------------------------------- text fallback */

static void curve_text(ChartCtx *x)
{
    const Chart *d = x->data;
    double *cum = cumulative(d);
    int w = x->cols - x->indent;

    if (w < 12)
        w = 12;

    int bar_w = w / 4;
    if (bar_w > 18)
        bar_w = 18;
    int mark_w = 8;
    int name_w = w - 3 - 1 - 7 - (bar_w + 3) - mark_w;
    if (name_w < 8) {
        mark_w = 0;
        name_w = w - 3 - 1 - 7 - (bar_w + 3);
    }
    if (name_w < 8) {
        bar_w = 0;
        name_w = w - 11;
    }
    if (name_w < 4)
        name_w = 4;

    int r50 = cum ? rank_at(cum, d->n, 0.5) : 0;
    int r80 = cum ? rank_at(cum, d->n, 0.8) : 0;

    int lines = d->n;
    if (lines > x->rows)
        lines = x->rows;

    for (int i = 0; i < lines; i++) {
        const Entry *e = d->slice[i];
        char raw[600], name[512];
        double share = cum ? cum[i] : 0.0;

        snprintf(raw, sizeof(raw), "%s%s", e->name, e->is_dir ? "/" : "");
        fit(raw, name, sizeof(name), name_w);

        for (int k = 0; k < x->indent; k++)
            fputc(' ', stdout);

        printf("%2d %s", i + 1, name);
        for (int k = display_width(name); k < name_w + 1; k++)
            fputc(' ', stdout);
        printf("%6.1f%%", share * 100.0);

        if (bar_w > 0) {
            int fill = (int)(share * bar_w + 0.5);
            if (fill > bar_w)
                fill = bar_w;
            fputs(" [", stdout);
            for (int k = 0; k < bar_w; k++)
                fputc(k < fill ? '#' : '.', stdout);
            fputc(']', stdout);
        }

        if (mark_w > 0) {
            if (i + 1 == r50)
                fputs(" <- 50%", stdout);
            else if (i + 1 == r80)
                fputs(" <- 80%", stdout);
        }
        fputc('\n', stdout);
    }

    free(cum);
}

/* ---------------------------------------------------------- row callback */

static void curve_row(void *v, int row)
{
    CurveCtx *cc = v;
    const ChartCtx *x = cc->x;

    if (row < cc->legend_rows) {
        fputs("   ", stdout);
        legend_line(x->t, x->data, row, x->legend_w, x->mode, cc->show_files);
    }

    if (cc->gutter > 0) {
        for (int i = 0; i < N_LEVELS; i++)
            if (cc->ylab_row[i] == row) {
                printf("\033[%dG", x->indent + 1);
                t_dim(x->t);
                printf("%*s", cc->gutter - 1, cc->ylab[i]);
                t_reset(x->t);
            }
    }

    if (row == cc->caption_row) {
        for (int i = 0; i < 2; i++)
            if (cc->cap_col[i] >= 0) {
                printf("\033[%dG", cc->cap_col[i] + 1);
                t_dim(x->t);
                fputs(cc->cap_text[i], stdout);
                t_reset(x->t);
            }
    }
}

/* ---------------------------------------------------------------- public */

void chart_curve(ChartCtx *x, bool filled)
{
    const Chart *d = x ? x->data : NULL;

    if (!x || !d || d->n <= 0 || x->rows < 1 || x->cols < 1)
        return;

    int n = d->n;
    int indent = x->indent > 0 ? x->indent : 0;
    int avail = x->cols - indent;
    int legend_w = x->legend_w > 20 ? x->legend_w : 20;

    int gutter = 5;                          /* "100%" plus one space */
    int plot = avail - legend_w - 3 - gutter;
    if (plot < 14) {
        gutter = 0;                          /* no room to label the rules */
        plot = avail - legend_w - 3;
    }
    if (plot > 64)
        plot = 64;

    int rows = x->rows;
    int chart_rows = rows > 20 ? 20 : rows;
    int caption_row = -1;
    if (rows > chart_rows)
        caption_row = chart_rows;
    else if (chart_rows >= 4)
        caption_row = --chart_rows;

    Canvas *c = NULL;
    if (plot >= 10 && chart_rows >= 2)
        c = canvas_new(x->t, x->mode, plot, chart_rows);

    double *cum = c ? cumulative(d) : NULL;
    if (!c || !cum) {
        canvas_free(c);
        free(cum);
        curve_text(x);
        return;
    }

    /* A gridline lands on a text row's centre line, so its label in the
     * gutter sits on the row the rule passes through. */
    double ytop = 0.5 * c->px_h;
    double ybot = (chart_rows - 0.5) * c->px_h;
    double span = ybot - ytop;
    double ax = 0.5;

    double dot_r = 0.9;
    double stroke = 1.3;
    if (x->mode == MODE_PIXELS) {
        dot_r = c->px_h * 0.16;
        if (dot_r < 1.6) dot_r = 1.6;
        if (dot_r > 3.2) dot_r = 3.2;
        stroke = 2.0;
    }

    double xr = c->w - 1.0 - dot_r;
    if (xr <= ax + 1.0)
        xr = ax + 1.0;

    RGB grid = rule_color(x->t, 0.40);
    RGB axis = rule_color(x->t, 0.70);

    double base = snap(ybot);
    double step = (x->mode == MODE_PIXELS) ? 0.0 : 2.0;
    double grid_a = (x->mode == MODE_PIXELS) ? 0.55 : 0.80;

    for (int k = 1; k <= 4; k++) {
        double y = snap(ybot - span * (k / 4.0));
        rule_line(c, ax, y, c->w - 0.5, y, grid, grid_a, step);
    }
    canvas_line(c, snap(ax), 0.5, snap(ax), base, 1.0, axis, 0.80);
    canvas_line(c, ax, base, c->w - 0.5, base, 1.0, axis, 0.80);

    /* Rank k sits at k/n of the way across, so rank 0 is on the axis and the
     * last entry lands on the right edge however few entries there are. */
    Pt *pts = malloc((size_t)(n + 2) * sizeof(Pt));
    if (!pts) {
        canvas_free(c);
        free(cum);
        curve_text(x);
        return;
    }

    pts[0].x = ax;
    pts[0].y = ybot;
    for (int i = 0; i < n; i++) {
        pts[i + 1].x = ax + (xr - ax) * (double)(i + 1) / (double)n;
        pts[i + 1].y = ybot - span * cum[i];
    }

    if (filled) {
        pts[n + 1].x = pts[n].x;
        pts[n + 1].y = ybot;
        canvas_polygon(c, pts, n + 2, d->slice[0]->color, 0.35);
    }
    canvas_polyline(c, pts, n + 1, stroke, d->slice[0]->color, 0.95);

    /* Vertices only get dots while they are far enough apart to read as
     * separate marks; past that the polyline alone carries the shape. */
    if ((xr - ax) / n >= 2.4 * dot_r) {
        for (int i = 0; i < n; i++)
            canvas_circle(c, pts[i + 1].x, pts[i + 1].y, dot_r,
                          d->slice[i]->color, 1.0);
    }

    CurveCtx cc;
    memset(&cc, 0, sizeof(cc));
    cc.x           = x;
    cc.show_files  = legend_w >= 52;
    cc.gutter      = gutter;
    cc.caption_row = caption_row;
    cc.cap_col[0]  = cc.cap_col[1] = -1;

    cc.legend_rows = n > rows ? rows : n;

    for (int i = 0; i < N_LEVELS; i++)
        cc.ylab_row[i] = -1;

    if (gutter > 0) {
        static const int level[N_LEVELS] = { 100, 0, 50, 25, 75 };
        bool taken[MAX_ROWS];

        memset(taken, 0, sizeof(taken));
        for (int i = 0; i < N_LEVELS; i++) {
            double y = snap(ybot - span * (level[i] / 100.0));
            int r = (int)(y / c->px_h);
            if (r < 0) r = 0;
            if (r >= chart_rows) r = chart_rows - 1;
            if (taken[r])
                continue;
            taken[r] = true;
            cc.ylab_row[i] = r;
            snprintf(cc.ylab[i], sizeof(cc.ylab[i]), "%d%%", level[i]);
        }
    }

    /* Crossings: the curve reaches thr somewhere inside the segment that
     * ends at rank k, and cum[k-2] < thr <= cum[k-1] makes the denominator
     * strictly positive, so zero-sized entries cannot divide by zero. */
    if (cum[n - 1] > 0.0) {
        static const double thr[2] = { 0.5, 0.8 };
        int end = -1;

        for (int i = 0; i < 2; i++) {
            int k = rank_at(cum, n, thr[i]);
            if (k <= 0)
                continue;

            double lo = k > 1 ? cum[k - 2] : 0.0;
            double hi = cum[k - 1];
            double f = hi > lo ? (thr[i] - lo) / (hi - lo) : 1.0;
            double cx = pts[k - 1].x + (pts[k].x - pts[k - 1].x) * f;
            double cy = ybot - span * thr[i];

            canvas_line(c, snap(cx), ybot, snap(cx), cy, 1.0, axis, 0.55);
            canvas_ring(c, cx, cy, dot_r * 0.8, dot_r * 1.7, axis, 0.90);

            if (caption_row < 0)
                continue;

            char text[32];
            snprintf(text, sizeof(text), "%d%% in %d",
                     (int)(thr[i] * 100.0 + 0.5), k);
            int len = display_width(text);
            if (len > plot) {
                snprintf(text, sizeof(text), "%d%%",
                         (int)(thr[i] * 100.0 + 0.5));
                len = display_width(text);
            }
            if (len > plot)
                continue;

            int left = indent + gutter;
            int col = left + (int)(cx / c->px_w) - len / 2;
            if (col < left)
                col = left;
            if (col + len > left + plot)
                col = left + plot - len;

            /* Never let the second caption touch the first: slide it right
             * rather than drop a label the reader came for. */
            if (end >= 0 && col < end + 1)
                col = end + 1;
            if (col + len > left + plot)
                continue;
            end = col + len;

            cc.cap_col[i] = col;
            snprintf(cc.cap_text[i], sizeof(cc.cap_text[i]), "%s", text);
        }
    }

    int total_rows = chart_rows + (caption_row >= 0 ? 1 : 0);
    if (cc.legend_rows > total_rows)
        total_rows = cc.legend_rows;
    if (total_rows > rows)
        total_rows = rows;

    canvas_present(c, x->t, x->mode, indent + gutter, total_rows,
                   curve_row, &cc);

    free(pts);
    free(cum);
    canvas_free(c);
}
