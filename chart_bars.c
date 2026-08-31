/*
 * Bars and columns.
 *
 * Both variants are the same drawing seen from two sides: a proportional
 * rounded rectangle sitting on a faint full-length track, with a hairline
 * axis along the baseline. The largest entry fills the track, so the shapes
 * compare sizes against each other rather than against the total; the
 * percentages in the legend carry the share of the whole.
 *
 * Text can only be printed to the right of a canvas, so every label that
 * belongs beside a bar is placed by the row callback and every label that
 * belongs under a column is printed after the canvas has been presented.
 * Column geometry is therefore derived from integer cell boundaries: a
 * column and its label share the same span of cells and cannot drift apart.
 */

#define _DEFAULT_SOURCE

#include "chart.h"

#include <stdio.h>
#include <string.h>

/* legend_line prints exactly this many columns and no fewer; below it the
 * name would be squeezed out entirely. */
#define LEGEND_MIN 26
#define LEGEND_MAX 54
#define FILES_AT   52       /* legend wide enough to earn a file count */

/* Narrowest column worth drawing: three cells of paint and one of air, so
 * the label under it is three characters rather than two. */
#define COL_MIN    4
#define PLOT_ROWS  20       /* a column chart taller than this is a tower */

/* ------------------------------------------------------------ small utils */

static void spaces(int n)
{
    for (int i = 0; i < n; i++)
        fputc(' ', stdout);
}

/* Axes and tracks are drawn from the terminal background so they sit under
 * the data instead of competing with it. */
static RGB axis_rgb(const Term *t)
{
    RGB out = { 110, 110, 120 };

    if (t->have_bg) {
        out.r = (unsigned char)(t->bg.r * 0.58 + 128 * 0.42 + 0.5);
        out.g = (unsigned char)(t->bg.g * 0.58 + 128 * 0.42 + 0.5);
        out.b = (unsigned char)(t->bg.b * 0.58 + 128 * 0.42 + 0.5);
    }
    return out;
}

/* Blocks mode without a known background snaps coverage to on or off, so a
 * faint wash there would either vanish or shout. Skip it instead. */
static double faint_alpha(const ChartCtx *x)
{
    return (x->mode == MODE_PIXELS || x->t->have_bg) ? 0.18 : 0.0;
}

/* Hard truncation on a code point boundary, no ellipsis: column labels are
 * three or four cells wide and cannot spare one. Returns the width used,
 * measured the way the terminal will measure it so the strip cannot drift
 * out of step with the columns above it. */
static int clip(const char *src, char *dst, size_t cap, int w)
{
    size_t i = 0;
    int used = 0;

    while (src[i]) {
        size_t j = i + 1;
        while ((src[j] & 0xC0) == 0x80)
            j++;
        if (j >= cap)
            break;

        memcpy(dst, src, j);
        dst[j] = '\0';
        int wide = display_width(dst);
        if (wide > w)
            break;

        i = j;
        used = wide;
    }
    memcpy(dst, src, i);
    dst[i] = '\0';
    return used;
}

static long long largest(const Chart *d, int n)
{
    long long max = 0;
    for (int i = 0; i < n; i++)
        if (d->slice[i]->size > max)
            max = d->slice[i]->size;
    return max;
}

static void print_note(const Term *t, int hidden, int width)
{
    char raw[64], buf[64];

    snprintf(raw, sizeof(raw), "+%d more not shown", hidden);
    fit(raw, buf, sizeof(buf), width);
    t_dim(t);
    fputs(buf, stdout);
    t_reset(t);
}

/* ------------------------------------------------------------ plain text */

/* No canvas: one line per entry, hash marks for the bar. Used for
 * MODE_ASCII, for an allocation failure, and for terminals too narrow to
 * hold a drawing and a legend side by side. */
static void plain_bars(ChartCtx *x)
{
    const Chart *d = x->data;
    int width = x->cols - x->indent;
    if (width < 8)
        width = 8;

    int shown = d->n;
    bool note = false;
    if (shown > x->rows) {
        shown = x->rows > 1 ? x->rows - 1 : 1;
        note = shown < d->n && shown + 1 <= x->rows;
    }
    if (shown < 1)
        shown = 1;

    /* Under 26 columns nothing lines up, so the line collapses to a name
     * and a size trimmed together to whatever there is. */
    bool compact = width < 26;

    int name_w = width / 3;
    if (name_w > 24) name_w = 24;
    if (name_w < 8)  name_w = 8;

    int bar_w = width - (name_w + 19);
    if (bar_w < 4)
        bar_w = 0;

    long long max = largest(d, shown);

    for (int i = 0; i < shown; i++) {
        const Entry *e = d->slice[i];
        char raw[600], name[600], size_buf[32];

        human_size(e->size, size_buf, sizeof(size_buf));

        if (compact) {
            char line[700];
            snprintf(raw, sizeof(raw), "%s%s", e->name,
                     e->is_dir ? "/" : "");
            snprintf(line, sizeof(line), "%s  %s", raw, size_buf);
            fit(line, name, sizeof(name), width);
            spaces(x->indent);
            fputs(name, stdout);
            fputc('\n', stdout);
            continue;
        }

        snprintf(raw, sizeof(raw), "%s%s", e->name, e->is_dir ? "/" : "");
        fit(raw, name, sizeof(name), name_w);

        spaces(x->indent);
        printf("%c ", e->sym ? e->sym : '#');
        fputs(name, stdout);
        spaces(name_w - display_width(name) + 1);
        printf("%8s %5.1f%%", size_buf, d->pct[i] * 100.0);

        if (bar_w > 0) {
            int fill = 0;
            if (max > 0)
                fill = (int)((double)bar_w * (double)e->size /
                             (double)max + 0.5);
            if (fill == 0 && e->size > 0)
                fill = 1;
            if (fill > bar_w)
                fill = bar_w;

            fputc(' ', stdout);
            for (int k = 0; k < fill; k++)
                fputc('#', stdout);
        }
        fputc('\n', stdout);
    }

    if (note) {
        spaces(x->indent);
        print_note(x->t, d->n - shown, width);
        fputc('\n', stdout);
    }
}

/* ------------------------------------------------------- horizontal bars */

typedef struct {
    ChartCtx *x;
    int       shown;
    int       pitch;      /* text rows per entry: 1 tight, 2 with a gap */
    int       rows_used;
    int       legend_w;
    bool      note;
    bool      show_files;
} BarCtx;

static void bar_row(void *ctx, int row)
{
    BarCtx *b = ctx;
    const Chart *d = b->x->data;

    if (b->note && row == b->rows_used) {
        spaces(2);
        print_note(b->x->t, d->n - b->shown, b->legend_w);
        return;
    }
    if (row % b->pitch != 0)
        return;

    int i = row / b->pitch;
    if (i >= b->shown)
        return;

    fputs("  ", stdout);
    legend_line(b->x->t, d, i, b->legend_w, b->x->mode, b->show_files);
}

static void draw_bars(ChartCtx *x)
{
    const Chart *d = x->data;

    int avail = x->rows;
    int shown = d->n;
    bool note = false;

    if (shown > avail) {
        note = true;
        shown = avail - 1;
        if (shown < 1) {
            shown = 1;
            note = false;
        }
    }

    int width = x->cols - x->indent;
    int legend_w = x->legend_w > LEGEND_MAX ? LEGEND_MAX : x->legend_w;

    if (legend_w < LEGEND_MIN)
        legend_w = LEGEND_MIN;

    /* The legend already carries the share of the whole, so the drawing
     * gets every column the legend does not need. */
    int bar_cols = width - legend_w - 2;

    while (bar_cols < 10 && legend_w > LEGEND_MIN) {
        legend_w--;
        bar_cols++;
    }
    if (bar_cols > 64)
        bar_cols = 64;
    if (legend_w < LEGEND_MIN || bar_cols < 8) {
        plain_bars(x);
        return;
    }

    BarCtx b;
    b.x          = x;
    b.shown      = shown;
    b.pitch      = 1;
    b.legend_w   = legend_w;
    b.note       = note;
    b.show_files = legend_w >= FILES_AT;

    if (shown * 2 - 1 + (note ? 1 : 0) <= avail)
        b.pitch = 2;
    b.rows_used = shown * b.pitch - (b.pitch - 1);

    Canvas *c = canvas_new(x->t, x->mode, bar_cols, b.rows_used);
    if (!c) {
        plain_bars(x);
        return;
    }

    RGB grey = axis_rgb(x->t);
    double faint = faint_alpha(x);

    /* The axis is a hairline column at the left edge; the tracks start just
     * clear of it, by two pixels where a pixel is smaller than a cell. */
    double axis_w = 1.0;
    double x0 = axis_w + (c->px_w >= 4.0 ? 2.0 : 0.0);
    double track_w = c->w - x0;

    if (track_w < 2.0) {
        canvas_free(c);
        plain_bars(x);
        return;
    }

    double thick = c->px_h * (b.pitch == 2 ? 0.58 : 0.74);
    if (thick < 2.0)
        thick = c->px_h;        /* half blocks: fill the whole text row */

    long long max = largest(d, shown);

    canvas_rect(c, 0.0, 0.0, axis_w, (double)c->h, grey, 0.55);

    for (int i = 0; i < shown; i++) {
        double cy = (i * b.pitch + 0.5) * c->px_h;
        double top = cy - thick / 2.0;
        double radius = thick / 3.0;

        if (faint > 0.0)
            canvas_round_rect(c, x0, top, track_w, thick, radius, grey,
                              faint);

        if (max <= 0 || d->slice[i]->size <= 0)
            continue;

        double len = track_w * (double)d->slice[i]->size / (double)max;
        if (len < thick)
            len = thick;        /* a sliver still deserves its round cap */
        if (len > track_w)
            len = track_w;

        canvas_round_rect(c, x0, top, len, thick, radius,
                          d->slice[i]->color, 1.0);
    }

    canvas_present(c, x->t, x->mode, x->indent,
                   b.rows_used + (note ? 1 : 0), bar_row, &b);
    canvas_free(c);
}

/* ----------------------------------------------------------- the columns */

typedef struct {
    ChartCtx *x;
    int       shown;
    int       legend_w;
    bool      note;
    bool      show_files;
} ColCtx;

static void col_row(void *ctx, int row)
{
    ColCtx *k = ctx;
    const Chart *d = k->x->data;

    if (row < k->shown) {
        fputs("  ", stdout);
        legend_line(k->x->t, d, row, k->legend_w, k->x->mode, k->show_files);
    } else if (k->note && row == k->shown) {
        fputs("  ", stdout);
        print_note(k->x->t, d->n - k->shown, k->legend_w);
    }
}

static void draw_columns(ChartCtx *x)
{
    const Chart *d = x->data;
    int avail = x->rows;

    if (avail < 2) {
        plain_bars(x);
        return;
    }

    int width = x->cols - x->indent;
    int legend_w = x->legend_w > LEGEND_MAX ? LEGEND_MAX : x->legend_w;

    if (legend_w < LEGEND_MIN)
        legend_w = LEGEND_MIN;

    int plot_cols = width - 2 - legend_w;

    while (plot_cols < 12 && legend_w > LEGEND_MIN) {
        legend_w--;
        plot_cols++;
    }
    if (legend_w < LEGEND_MIN || plot_cols < 3) {
        plain_bars(x);
        return;
    }

    /* The legend runs one row per column, and the count of what did not fit
     * needs a row of its own. */
    int shown = plot_cols / COL_MIN;

    if (shown > d->n)
        shown = d->n;
    if (shown > avail)
        shown = avail;
    if (shown < d->n && shown == avail)
        shown = avail - 1;
    if (shown < 1)
        shown = 1;

    bool note = shown < d->n;

    /* Eight cells per column is as wide as one needs to be; anything more
     * just pushes the legend away from what it describes. */
    if (plot_cols > shown * 8)
        plot_cols = shown * 8;

    int legend_rows = shown + (note ? 1 : 0);

    /* One row goes to the labels. The plot stops growing at PLOT_ROWS, but
     * a legend taller than that pulls it back up rather than leaving a hole
     * beside the lower half of the list. */
    int cap = legend_rows - 1 > PLOT_ROWS ? legend_rows - 1 : PLOT_ROWS;

    /* A cell is about twice as tall as it is wide, so a plot deeper than
     * half its width in rows is a tower rather than a chart. This only bites
     * when there are two or three columns to draw. */
    if (cap > plot_cols / 2)
        cap = plot_cols / 2;
    if (cap < 2)
        cap = 2;

    int plot_rows = avail - 1;
    if (plot_rows > cap)
        plot_rows = cap;

    int total_rows = plot_rows + 1 > legend_rows ? plot_rows + 1 : legend_rows;

    Canvas *c = canvas_new(x->t, x->mode, plot_cols, plot_rows);
    if (!c) {
        plain_bars(x);
        return;
    }

    ColCtx k;
    k.x          = x;
    k.shown      = shown;
    k.legend_w   = legend_w;
    k.note       = note;
    k.show_files = legend_w >= FILES_AT;

    RGB grey = axis_rgb(x->t);
    double faint = faint_alpha(x);
    double axis_w = 1.0;
    double base = c->h - axis_w;            /* top edge of the axis line */
    double top_pad = c->px_h >= 4.0 ? 2.0 : 0.0;
    double plot_h = base - top_pad;

    if (plot_h < 1.0) {
        plot_h = base;
        top_pad = 0.0;
    }

    long long max = largest(d, shown);

    canvas_rect(c, 0.0, base, (double)c->w, axis_w, grey, 0.55);

    for (int i = 0; i < shown; i++) {
        /* A column is a whole number of cells starting on a cell edge, one
         * cell narrower than its pitch, so it covers exactly the cells its
         * label is printed into below. The spare cell leads, which keeps the
         * first column clear of the plot edge instead of pinned to it. */
        int cs = i * plot_cols / shown;
        int ce = (i + 1) * plot_cols / shown;
        int lead = ce - cs > 1 ? 1 : 0;
        double bx = (cs + lead) * c->px_w;
        double bw = (ce - cs - lead) * c->px_w;

        double r = bw / 3.0;
        if (r > c->px_h * 0.5)
            r = c->px_h * 0.5;

        if (faint > 0.0)
            canvas_round_rect(c, bx, top_pad, bw, plot_h, r, grey, faint);

        if (max <= 0 || d->slice[i]->size <= 0)
            continue;

        double h = plot_h * (double)d->slice[i]->size / (double)max;
        if (h < 1.0)
            h = 1.0;

        canvas_round_rect(c, bx, base - h, bw, h, r, d->slice[i]->color, 1.0);
    }

    canvas_present(c, x->t, x->mode, x->indent, plot_rows, col_row, &k);
    canvas_free(c);

    /* The label strip, printed under the columns cell for cell. */
    spaces(x->indent);
    int cur = 0;
    for (int i = 0; i < shown; i++) {
        int cs = i * plot_cols / shown;
        int ce = (i + 1) * plot_cols / shown;
        int lead = ce - cs > 1 ? 1 : 0;
        int room = ce - cs - lead;
        char label[256];

        cs += lead;
        if (cs < cur)
            continue;
        spaces(cs - cur);
        cur = cs;

        int used = clip(d->slice[i]->name, label, sizeof(label), room);
        t_fg(x->t, d->slice[i]->color);
        fputs(label, stdout);
        t_reset(x->t);
        cur += used;
    }
    spaces(plot_cols - cur);
    col_row(&k, plot_rows);
    fputc('\n', stdout);

    for (int row = plot_rows + 1; row < total_rows; row++) {
        spaces(x->indent + plot_cols);
        col_row(&k, row);
        fputc('\n', stdout);
    }
}

/* ------------------------------------------------------------------ entry */

void chart_bars(ChartCtx *x, bool vertical)
{
    if (!x || !x->data || x->data->n <= 0 || x->rows < 1 || x->cols < 1)
        return;

    if (vertical)
        draw_columns(x);
    else
        draw_bars(x);
}
