/*
 * Scatter and bubble plots: file count against total size.
 *
 * Both quantities span orders of magnitude, so both axes are logarithmic.
 * The count axis steps in decades; the size axis steps in size units, which
 * is a log too - one step is a factor of 1024 - and it lets every horizontal
 * rule carry an exact label ("1.0 M") instead of a rounded power of ten.
 *
 * The canvas covers the plot rectangle only. Size labels live in a fixed
 * gutter printed to the right of each canvas row, so they land on the same
 * text row as the rule they name; count labels go in a text row printed
 * under the canvas, where one output column is one canvas cell column.
 */

#define _DEFAULT_SOURCE

#include "chart.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GUT            8    /* columns between the plot and the legend */
#define MAX_PLOT_ROWS 24
#define MAX_PLOT_COLS 100

/* ------------------------------------------------------------ small utils */

static void spaces(int n)
{
    while (n-- > 0)
        fputc(' ', stdout);
}

/* Rules are drawn from the terminal background so they sit under the data. */
static RGB rule_rgb(const Term *t)
{
    RGB grey = { 110, 110, 120 };
    if (!t->have_bg)
        return grey;

    RGB out;
    out.r = (unsigned char)(t->bg.r + (128.0 - t->bg.r) * 0.55 + 0.5);
    out.g = (unsigned char)(t->bg.g + (128.0 - t->bg.g) * 0.55 + 0.5);
    out.b = (unsigned char)(t->bg.b + (128.0 - t->bg.b) * 0.55 + 0.5);
    return out;
}

/* Centre of the pixel row (or column) a coordinate falls in. A one-pixel rule
 * left on a pixel boundary splits its coverage in two, and a terminal that
 * never told us its background drops anything under half alpha outright - so
 * an unsnapped rule simply vanishes there. */
static double snap(double v)
{
    return floor(v) + 0.5;
}

/* One interior rule. Half blocks have no faintness to spare - the canvas
 * drops anything under half alpha when the terminal never named its
 * background - so a solid rule there comes out louder than the data it
 * frames. Dotting it keeps the reference and gives the weight back. */
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

/* 10^d as a count, for d in 0..15. */
static void count_label(int d, char *out, size_t len)
{
    double v = pow(10.0, (double)d);

    if (v < 1000.0)
        snprintf(out, len, "%.0f", v);
    else if (v < 1.0e6)
        snprintf(out, len, "%.0f k", v / 1.0e3);
    else if (v < 1.0e9)
        snprintf(out, len, "%.0f M", v / 1.0e6);
    else
        snprintf(out, len, "%.0f G", v / 1.0e9);
}

/* 1024^u as a size. Capped at u = 6 so the value stays inside long long. */
static void unit_label(int u, char *out, size_t len)
{
    if (u < 0)
        u = 0;
    if (u > 6)
        u = 6;
    human_size((long long)(pow(1024.0, (double)u) + 0.5), out, len);
}

/* --------------------------------------------------------- text fallback */

/* No canvas, or no room for one: the same two numbers as plain columns. */
static void points_text(ChartCtx *x)
{
    const Chart *d = x->data;
    int avail = x->cols - x->indent;
    if (avail < 8)
        avail = 8;

    int budget = x->rows;
    int shown = d->n;
    bool header = budget > shown && avail >= 29;
    if (header)
        budget--;
    if (shown > budget)
        shown = budget;
    if (shown < 0)
        shown = 0;

    int name_w = avail - 21;
    bool wide = name_w >= 8;
    if (!wide)
        name_w = avail - 2;

    if (header) {
        spaces(x->indent);
        t_dim(x->t);
        printf("%-*s %8s  %8s", name_w + 2, "entry", "files", "size");
        t_reset(x->t);
        fputc('\n', stdout);
    }

    for (int i = 0; i < shown; i++) {
        const Entry *e = d->slice[i];
        char raw[600], name[512], size_buf[32];

        snprintf(raw, sizeof(raw), "%s%s", e->name, e->is_dir ? "/" : "");
        fit(raw, name, sizeof(name), name_w);
        human_size(e->size, size_buf, sizeof(size_buf));

        spaces(x->indent);
        if (x->mode == MODE_ASCII || !x->t->color) {
            printf("%c ", e->sym);
        } else {
            t_fg(x->t, e->color);
            fputs("\xe2\x97\x8f ", stdout);          /* U+25CF */
            t_reset(x->t);
        }

        t_fg(x->t, e->color);
        fputs(name, stdout);
        t_reset(x->t);
        spaces(name_w - display_width(name));

        if (wide) {
            t_dim(x->t);
            printf(" %8lld", e->files);
            t_reset(x->t);
            printf("  %8s", size_buf);
        }
        fputc('\n', stdout);
    }
}

/* ------------------------------------------------------------- the plot */

typedef struct {
    ChartCtx *x;
    char    (*ylab)[8];
    int      plot_rows;
    bool     side;
    int      legend_start, legend_n, legend_w, more;
    bool     show_files;
} RowCtx;

static void points_row(void *ctx, int row)
{
    RowCtx *r = ctx;
    int li = row - r->legend_start;
    bool has_legend = r->side && li >= 0 &&
                      li < r->legend_n + (r->more > 0 ? 1 : 0);
    const char *lab = (row >= 0 && row < r->plot_rows) ? r->ylab[row] : "";

    if (!lab[0] && !has_legend)
        return;

    t_dim(r->x->t);
    printf(" %-6s", lab);
    t_reset(r->x->t);
    fputc(' ', stdout);

    if (!has_legend)
        return;

    if (li < r->legend_n) {
        legend_line(r->x->t, r->x->data, li, r->legend_w, r->x->mode,
                    r->show_files);
    } else {
        t_dim(r->x->t);
        printf("+%d more", r->more);
        t_reset(r->x->t);
    }
}

void chart_points(ChartCtx *x, bool bubble)
{
    if (!x || !x->data || x->data->n <= 0 || x->rows <= 0 || x->cols <= 0)
        return;

    const Chart *d = x->data;
    int n = d->n;
    int avail = x->cols - x->indent;
    if (avail < 1)
        avail = 1;

    /* legend_line writes exactly `width` columns only while its name field
     * has room; below 25 it overruns, so that is the floor. */
    int lw = x->legend_w < 25 ? 25 : x->legend_w;

    int plot_cols = avail - GUT - lw;
    bool side = plot_cols >= 24;
    if (!side)
        plot_cols = avail - GUT;
    if (plot_cols > MAX_PLOT_COLS)
        plot_cols = MAX_PLOT_COLS;

    /* Two rows under the plot: count labels, then the axis caption. */
    int plot_rows = x->rows - 2;
    int below = 0;
    if (!side) {
        below = plot_rows - 4;
        if (below > n)
            below = n;
        if (below < 0)
            below = 0;
        plot_rows -= below;
    }

    /* Canvas pixels are twice as tall as they are wide in blocks mode, and
     * a cell is roughly 2:1 in pixels mode, so half the columns is square. */
    int cap = plot_cols / 2;
    if (cap < 4)
        cap = 4;
    if (plot_rows > cap)
        plot_rows = cap;
    if (plot_rows > MAX_PLOT_ROWS)
        plot_rows = MAX_PLOT_ROWS;

    Canvas *c = NULL;
    if (plot_cols >= 12 && plot_rows >= 4)
        c = canvas_new(x->t, x->mode, plot_cols, plot_rows);
    if (!c) {
        points_text(x);
        return;
    }

    /* ------------------------------------------------------------ scales */

    const double LB = log(1024.0);
    double minx = 0, maxx = 0, miny = 0, maxy = 0;
    long long max_size = 0;

    for (int i = 0; i < n; i++) {
        const Entry *e = d->slice[i];
        double vx = log10((double)(e->files > 0 ? e->files : 1));
        double vy = log((double)(e->size > 0 ? e->size : 1)) / LB;

        if (i == 0 || vx < minx) minx = vx;
        if (i == 0 || vx > maxx) maxx = vx;
        if (i == 0 || vy < miny) miny = vy;
        if (i == 0 || vy > maxy) maxy = vy;
        if (e->size > max_size)
            max_size = e->size;
    }

    /* The ends land on half steps: rounding them out to whole ones opens a
     * band up to a full decade wide with nothing in it, and the data then
     * reads as sitting in a corner of its own frame. Rules and labels still
     * fall on whole steps, and the span is never under two of them, so one
     * entry - or a set that all lands on the same value - still gets a
     * readable frame with at least two labelled rules in it. */
    double lx0 = floor(minx * 2.0) / 2.0, lx1 = ceil(maxx * 2.0) / 2.0;
    double ly0 = floor(miny * 2.0) / 2.0, ly1 = ceil(maxy * 2.0) / 2.0;

    while (lx1 - lx0 < 2.0) {
        lx1 += 0.5;
        if (lx1 - lx0 < 2.0 && lx0 >= 0.5)
            lx0 -= 0.5;
    }
    while (ly1 - ly0 < 2.0) {
        ly1 += 0.5;
        if (ly1 - ly0 < 2.0 && ly0 >= 0.5)
            ly0 -= 0.5;
    }

    /* Whole steps inside the span - the ones that get a rule and a label. */
    int x0 = (int)ceil(lx0 - 1e-9), x1 = (int)floor(lx1 + 1e-9);
    int y0 = (int)ceil(ly0 - 1e-9), y1 = (int)floor(ly1 + 1e-9);

    /* ---------------------------------------------------------- geometry */

    double r_min = (x->mode == MODE_PIXELS) ? 2.0 : 1.0;
    double r_dot = (x->mode == MODE_PIXELS)
                 ? (c->px_h * 0.18 > 3.0 ? c->px_h * 0.18 : 3.0) : 1.0;
    double shorter = c->w < c->h ? c->w : c->h;
    double r_max = bubble ? shorter / 10.0 : r_dot;
    if (r_max < r_min)
        r_max = r_min;

    double m_l = c->px_w;               /* left axis, one cell in */
    double m_b = c->px_h * 0.5;         /* bottom axis, half a row up */
    double ax = m_l;
    double ay = c->h - m_b;

    double pad = r_max;
    double room_w = (c->w - 1) - ax;
    if (pad > room_w * 0.3)
        pad = room_w * 0.3;
    if (pad > ay * 0.3)
        pad = ay * 0.3;
    if (pad < 0.0)
        pad = 0.0;

    double px0 = ax + pad, px1 = c->w - 1 - pad;
    double py0 = pad,      py1 = ay - pad;
    if (px1 - px0 < 1.0) { px0 = ax; px1 = c->w - 1; }
    if (py1 - py0 < 1.0) { py0 = 0.0; py1 = ay; }

    double sx = (px1 - px0) / (lx1 - lx0);
    double sy = (py1 - py0) / (ly1 - ly0);

    /* ------------------------------------------------------------- rules */

    RGB rule = rule_rgb(x->t);

    /* A half block cannot hold a faint tint: without a known background the
     * canvas keeps a pixel only past half alpha, so outside MODE_PIXELS the
     * grid has to be drawn hard enough to survive that cut. */
    bool fine = (x->mode == MODE_PIXELS);
    double grid_a = fine ? 0.20 : 0.80;
    double axis_a = fine ? 0.55 : 0.90;
    double step   = fine ? 0.0 : 2.0;

    /* A rule has to reach the centre of its last pixel to light it, and the
     * dots want the same spacing both ways - which is two canvas pixels
     * across and, outside MODE_PIXELS, four down. */
    double top = 0.5, right = c->w - 0.5, base = snap(ay), left = snap(ax);

    for (int u = y0; u <= y1; u++) {
        double yy = snap(py1 - (u - ly0) * sy);
        rule_line(c, ax, yy, right, yy, rule, grid_a, step);
    }
    for (int e = x0; e <= x1; e++) {
        double xx = snap(px0 + (e - lx0) * sx);
        rule_line(c, xx, top, xx, base, rule, grid_a,
                  fine ? 0.0 : step * (c->px_h / c->px_w));
    }
    canvas_line(c, left, top, left, base, 1.0, rule, axis_a);
    canvas_line(c, ax, base, right, base, 1.0, rule, axis_a);

    /* -------------------------------------------------------------- data */

    int *order = malloc((size_t)n * sizeof(int));
    if (order) {
        for (int i = 0; i < n; i++)
            order[i] = i;
        /* Largest first, so the small ones land on top and stay visible. */
        for (int i = 1; i < n; i++) {
            int key = order[i], j = i - 1;
            while (j >= 0 && d->slice[order[j]]->size < d->slice[key]->size) {
                order[j + 1] = order[j];
                j--;
            }
            order[j + 1] = key;
        }
    }

    double alpha = bubble ? 0.75 : 1.0;

    for (int k = 0; k < n; k++) {
        int i = order ? order[k] : k;
        const Entry *e = d->slice[i];

        double vx = log10((double)(e->files > 0 ? e->files : 1));
        double vy = log((double)(e->size > 0 ? e->size : 1)) / LB;
        if (vx < lx0) vx = lx0;
        if (vx > lx1) vx = lx1;
        if (vy < ly0) vy = ly0;
        if (vy > ly1) vy = ly1;

        double cx = px0 + (vx - lx0) * sx;
        double cy = py1 - (vy - ly0) * sy;

        double r = r_dot;
        if (bubble) {
            /* Area, not radius, carries the size. */
            r = max_size > 0
              ? r_max * sqrt((double)e->size / (double)max_size) : r_min;
            if (r < r_min)
                r = r_min;
        }

        canvas_circle(c, cx, cy, r, e->color, alpha);
        if (bubble && r >= 2.0)
            canvas_ring(c, cx, cy, r - 0.7, r, e->color, 0.95);
    }

    free(order);

    /* ------------------------------------------------------- size labels */

    char ylab[MAX_PLOT_ROWS][8];
    for (int i = 0; i < plot_rows; i++)
        ylab[i][0] = '\0';

    for (int u = y0; u <= y1; u++) {
        double yy = snap(py1 - (u - ly0) * sy);
        int row = (int)(yy / c->px_h);
        if (row < 0 || row >= plot_rows || ylab[row][0])
            continue;
        unit_label(u, ylab[row], sizeof(ylab[row]));
    }

    /* ----------------------------------------------------------- present */

    RowCtx rc;
    rc.x           = x;
    rc.ylab        = ylab;
    rc.plot_rows   = plot_rows;
    rc.side        = side;
    rc.legend_w    = lw;
    rc.show_files  = lw >= 52;
    rc.legend_n    = n;
    rc.more        = 0;

    if (side && rc.legend_n > plot_rows) {
        rc.legend_n = plot_rows - 1;
        if (rc.legend_n < 0)
            rc.legend_n = 0;
        rc.more = n - rc.legend_n;
    }
    rc.legend_start = (plot_rows - (rc.legend_n + (rc.more > 0 ? 1 : 0))) / 2;
    if (rc.legend_start < 0)
        rc.legend_start = 0;

    canvas_present(c, x->t, x->mode, x->indent, plot_rows, points_row, &rc);

    /* ------------------------------------------------------ count labels */

    /* Nothing else is printed on this row, so it may spill into the gutter
     * that separates the plot from the legend. Without that the rightmost
     * label - the widest one - gets shoved left off its own rule. */
    int tick_w = plot_cols + GUT - 1;
    if (tick_w > avail)
        tick_w = avail;

    char ticks[MAX_PLOT_COLS + GUT];
    memset(ticks, ' ', (size_t)tick_w);
    ticks[tick_w] = '\0';

    int last_end = -2;
    for (int e = x0; e <= x1; e++) {
        char lab[16];
        count_label(e, lab, sizeof(lab));

        int len = (int)strlen(lab);
        if (len > tick_w)
            continue;

        int col = (int)(snap(px0 + (e - lx0) * sx) / c->px_w);
        int start = col - (len - 1) / 2;
        if (start < 0)
            start = 0;
        if (start + len > tick_w)
            start = tick_w - len;
        if (start < last_end + 1)
            continue;

        memcpy(ticks + start, lab, (size_t)len);
        last_end = start + len;
    }

    int end = tick_w;
    while (end > 0 && ticks[end - 1] == ' ')
        end--;
    ticks[end] = '\0';

    spaces(x->indent);
    t_dim(x->t);
    fputs(ticks, stdout);
    t_reset(x->t);
    fputc('\n', stdout);

    /* ---------------------------------------------------------- caption */

    const char *caption;
    if (avail >= 58)
        caption = "across: files per entry    up: total size (log scales)";
    else if (avail >= 32)
        caption = "across: files    up: size (log)";
    else
        caption = "files / size";

    char cap_buf[128];
    fit(caption, cap_buf, sizeof(cap_buf), avail);

    spaces(x->indent);
    t_dim(x->t);
    fputs(cap_buf, stdout);
    t_reset(x->t);
    fputc('\n', stdout);

    canvas_free(c);

    /* ------------------------------------------------- legend underneath */

    if (below > 0) {
        int w2 = avail > 54 ? 54 : avail;
        bool sf = w2 >= 52;
        for (int i = 0; i < below && i < n; i++) {
            spaces(x->indent);
            if (w2 >= 25) {
                legend_line(x->t, d, i, w2, x->mode, sf);
            } else {
                char raw[600], name[512];
                snprintf(raw, sizeof(raw), "%s%s", d->slice[i]->name,
                         d->slice[i]->is_dir ? "/" : "");
                fit(raw, name, sizeof(name), avail);
                t_fg(x->t, d->slice[i]->color);
                fputs(name, stdout);
                t_reset(x->t);
            }
            fputc('\n', stdout);
        }
    }
}
