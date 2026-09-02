/*
 * Bars and columns, drawn as rounded braille-dot bars sitting on a faint
 * track, legend beside them. Both variants are the same drawing seen from
 * two sides; below a useful size they both fall back to a plain list.
 */

#define _DEFAULT_SOURCE

#include "chart.h"
#include "dots.h"

#include <stdio.h>
#include <string.h>

#define LEGEND_MIN 26
#define LEGEND_MAX 54
#define FILES_AT   52       /* legend wide enough to earn a file count */
#define COL_MIN     4        /* narrowest column worth drawing, in cells */
#define MAX_ROWS   16

static void spaces(int n)
{
    for (int i = 0; i < n; i++)
        fputc(' ', stdout);
}

static RGB track_rgb(void)
{
    return (RGB){ 90, 90, 100 };
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
    legend_line(b->x->t, d, i, b->legend_w, b->show_files);
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
    if (shown > MAX_ROWS) {
        note = true;
        shown = MAX_ROWS;
    }

    int width = x->cols - x->indent;
    int legend_w = x->legend_w > LEGEND_MAX ? LEGEND_MAX : x->legend_w;
    if (legend_w < LEGEND_MIN)
        legend_w = LEGEND_MIN;

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

    if (shown * 2 - 1 + (note ? 1 : 0) <= avail && shown * 2 <= MAX_ROWS)
        b.pitch = 2;
    b.rows_used = shown * b.pitch - (b.pitch - 1);

    Dots *dt = dots_new(bar_cols, b.rows_used);
    if (!dt) {
        plain_bars(x);
        return;
    }

    RGB grey = track_rgb();
    long long max = largest(d, shown);
    double subw = bar_cols * 2.0;

    for (int i = 0; i < shown; i++) {
        double cy = (i * b.pitch + 0.5) * 4.0;
        double thick = 4.0 * (b.pitch == 2 ? 0.62 : 0.82);
        double top = cy - thick / 2.0;
        double radius = thick / 2.5;

        double len = 0.0;
        if (max > 0 && d->slice[i]->size > 0) {
            len = subw * (double)d->slice[i]->size / (double)max;
            if (len < thick)
                len = thick;
            if (len > subw)
                len = subw;
        }

        /* The track and the value never overlap the same dot: a shared
         * subpixel would otherwise average the two colours together. */
        if (len > 0.0)
            dots_round_rect(dt, 0.0, top, len, thick, radius,
                            d->slice[i]->color);
        if (len < subw)
            dots_round_rect(dt, len, top, subw - len, thick, radius, grey);
    }

    dots_present(dt, x->t, x->indent, b.rows_used + (note ? 1 : 0),
                bar_row, &b);
    dots_free(dt);
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
        legend_line(k->x->t, d, row, k->legend_w, k->show_files);
    } else if (k->note && row == k->shown) {
        fputs("  ", stdout);
        print_note(k->x->t, d->n - k->shown, k->legend_w);
    }
}

static void clip(const char *src, char *dst, size_t cap, int w, int *used)
{
    size_t i = 0;
    int wide = 0;

    while (src[i]) {
        size_t j = i + 1;
        while ((src[j] & 0xC0) == 0x80)
            j++;
        if (j >= cap)
            break;

        memcpy(dst, src, j);
        dst[j] = '\0';
        int nw = display_width(dst);
        if (nw > w)
            break;

        i = j;
        wide = nw;
    }
    memcpy(dst, src, i);
    dst[i] = '\0';
    *used = wide;
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

    if (plot_cols > shown * 8)
        plot_cols = shown * 8;

    int legend_rows = shown + (note ? 1 : 0);
    int cap = legend_rows - 1 > 8 ? legend_rows - 1 : 8;
    if (cap > plot_cols / 2)
        cap = plot_cols / 2;
    if (cap < 2)
        cap = 2;
    if (cap > MAX_ROWS)
        cap = MAX_ROWS;

    int plot_rows = avail - 1;
    if (plot_rows > cap)
        plot_rows = cap;

    int total_rows = plot_rows + 1 > legend_rows ? plot_rows + 1 : legend_rows;

    Dots *dt = dots_new(plot_cols, plot_rows);
    if (!dt) {
        plain_bars(x);
        return;
    }

    ColCtx k;
    k.x          = x;
    k.shown      = shown;
    k.legend_w   = legend_w;
    k.note       = note;
    k.show_files = legend_w >= FILES_AT;

    RGB grey = track_rgb();
    double base = plot_rows * 4.0;
    long long max = largest(d, shown);

    for (int i = 0; i < shown; i++) {
        int cs = i * plot_cols / shown;
        int ce = (i + 1) * plot_cols / shown;
        int lead = ce - cs > 1 ? 1 : 0;
        double bx = (cs + lead) * 2.0;
        double bw = (ce - cs - lead) * 2.0;

        double r = bw / 3.0;
        if (r > bw / 2.0) r = bw / 2.0;

        double h = 0.0;
        if (max > 0 && d->slice[i]->size > 0) {
            h = base * (double)d->slice[i]->size / (double)max;
            if (h < 1.0)
                h = 1.0;
        }

        /* The track and the value never share a dot, the same way the
         * horizontal bars keep them apart. */
        if (h > 0.0)
            dots_round_rect(dt, bx, base - h, bw, h, r, d->slice[i]->color);
        if (h < base)
            dots_round_rect(dt, bx, 0.0, bw, base - h, r, grey);
    }

    dots_present(dt, x->t, x->indent, plot_rows, NULL, NULL);
    dots_free(dt);

    /* The label strip, printed under the columns cell for cell. */
    spaces(x->indent);
    int cur = 0;
    for (int i = 0; i < shown; i++) {
        int cs = i * plot_cols / shown;
        int ce = (i + 1) * plot_cols / shown;
        int lead = ce - cs > 1 ? 1 : 0;
        int room = ce - cs - lead;
        char label[256];
        int used;

        cs += lead;
        if (cs < cur)
            continue;
        spaces(cs - cur);
        cur = cs;

        clip(d->slice[i]->name, label, sizeof(label), room, &used);
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
