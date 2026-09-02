/*
 * The two charts that were always mostly type rather than shape.
 *
 * chart_table  a dense comparison table. The inline bars are eighth-block
 *              characters, which give sub-cell precision and stay aligned
 *              with the text columns for free.
 * chart_gauge  filesystem fullness as an inline meter, with this tree's
 *              share of the whole filesystem alongside it.
 */

#define _DEFAULT_SOURCE

#include "chart.h"
#include "dots.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/statvfs.h>

/* ---------------------------------------------------------- small helpers */

static void pad(int n)
{
    for (int i = 0; i < n; i++)
        fputc(' ', stdout);
}

static double clamp01(double v)
{
    if (!(v > 0.0))
        return 0.0;
    return v > 1.0 ? 1.0 : v;
}

/* U+2588 down to U+258F: index is eighths of a cell, 0 draws nothing. */
static const char *eighth(int n)
{
    static const char *part[9] = {
        "", "\xe2\x96\x8f", "\xe2\x96\x8e", "\xe2\x96\x8d", "\xe2\x96\x8c",
        "\xe2\x96\x8b", "\xe2\x96\x8a", "\xe2\x96\x89", "\xe2\x96\x88"
    };
    if (n < 0) n = 0;
    if (n > 8) n = 8;
    return part[n];
}

/* Proportional bar of exactly `width` cells' worth of space, drawn from the
 * left. Zero draws nothing at all; anything above zero draws at least an
 * eighth, so a 0.01% entry is still visible as a sliver. */
static int inline_bar(const Term *t, double frac, int width, RGB col,
                      bool plain, bool empty)
{
    if (width <= 0)
        return 0;

    double exact = clamp01(frac) * width;
    int full = (int)exact;
    int rest = (int)((exact - full) * 8.0 + 0.5);
    if (rest == 8) {
        full++;
        rest = 0;
    }
    if (full > width) {
        full = width;
        rest = 0;
    }
    if (!empty && full == 0 && rest == 0)
        rest = 1;
    if (empty) {
        full = 0;
        rest = 0;
    }

    if (plain) {
        int cells = full + (rest >= 4 ? 1 : 0);
        if (!empty && cells == 0)
            cells = 1;
        for (int k = 0; k < cells; k++)
            fputc('#', stdout);
        return cells;
    }

    t_fg(t, col);
    for (int k = 0; k < full; k++)
        fputs("\xe2\x96\x88", stdout);
    if (rest > 0 && full < width) {
        fputs(eighth(rest), stdout);
        full++;
    }
    t_reset(t);
    return full;
}

/* ------------------------------------------------------------- the table */

/* A count only means something for things that contain other things. */
static bool counted(const Entry *e)
{
    return (e->is_dir || e->aggregate) && e->files > 0;
}

void chart_table(ChartCtx *x)
{
    if (!x || !x->t || !x->data || x->data->n <= 0 || x->rows <= 0)
        return;

    const Term  *t = x->t;
    const Chart *c = x->data;
    bool plain = !t->color;

    /* One column stays clear on the right: a row that fills the last cell of
     * the terminal wraps on some of them. */
    int avail = x->cols - x->indent - 1;
    if (avail < 16)
        avail = 16;

    /* Column widths come from the widest value actually printed. */
    char buf[512], raw[600];
    int size_w = 4, files_w = 5, name_w = 4, pct_w = 6;
    bool show_files = false;

    for (int i = 0; i < c->n; i++) {
        const Entry *e = c->slice[i];

        human_size(e->size, buf, sizeof(buf));
        int w = display_width(buf);
        if (w > size_w) size_w = w;

        w = display_width(e->name) + (e->is_dir ? 1 : 0);
        if (w > name_w) name_w = w;

        if (counted(e)) {
            show_files = true;
            snprintf(buf, sizeof(buf), "%lld", e->files);
            w = display_width(buf);
            if (w > files_w) files_w = w;
        }
    }
    if (name_w > 40)
        name_w = 40;

    /* Fit the columns. The bar is the point of the table, so a squeeze comes
     * out of the name first, then out of the file counts, and only when
     * neither has anything left does the bar itself go. */
    /* The bar grows with the terminal rather than freezing at one length, so
     * a wide window buys resolution in the tail instead of empty margin. */
    int bar_cap = avail / 3;
    if (bar_cap < 28)
        bar_cap = 28;
    if (bar_cap > 44)
        bar_cap = 44;

    int bar_w = 0;
    for (;;) {
        int fixed = 2 + name_w + 1 + size_w + (pct_w ? 1 + pct_w : 0)
                  + (show_files ? 1 + files_w : 0);
        int rem = avail - fixed;

        if (rem >= 11) {
            bar_w = rem - 1;
            if (bar_w > bar_cap)
                bar_w = bar_cap;
            break;
        }
        if (name_w > 14) {
            name_w -= 11 - rem;
            if (name_w < 14)
                name_w = 14;
            continue;
        }
        if (show_files) {
            show_files = false;
            continue;
        }
        if (rem >= 7) {
            bar_w = rem - 1;
            break;
        }
        if (rem >= 0)
            break;
        if (name_w > 8) {
            name_w += rem;
            if (name_w < 8)
                name_w = 8;
            continue;
        }
        if (name_w > 1) {
            name_w += rem;
            if (name_w < 1)
                name_w = 1;
            continue;
        }
        if (pct_w > 0) {
            pct_w = 0;          /* a terminal this narrow keeps the sizes */
            continue;
        }
        break;                  /* nothing left to give */
    }

    int table_w = 2 + name_w + 1 + size_w + (pct_w ? 1 + pct_w : 0)
                + (show_files ? 1 + files_w : 0)
                + (bar_w ? 1 + bar_w : 0);
    if (table_w > avail)
        table_w = avail;

    /* Rows: header and rule first, then as many entries as are left. A
     * truncated table spends its last row saying so. */
    bool header = x->rows >= 4;
    int body = header ? x->rows - 2 : x->rows;
    int shown = c->n;
    bool more = false;
    if (body < shown) {
        shown = body;
        if (body >= 2) {
            shown = body - 1;
            more = true;
        }
    }
    if (shown <= 0)
        return;

    /* Bars are scaled to the largest entry, not to the whole, so the shape
     * of the tail stays readable; the percent column carries the absolute.
     * Only the rows that get printed count: a truncated table would otherwise
     * be scaled by something off the bottom of it, and every visible bar
     * collapses to a sliver with nothing on screen to explain why. */
    double top = 0.0;
    for (int i = 0; i < shown; i++)
        if (c->pct[i] > top)
            top = c->pct[i];
    if (!(top > 0.0))
        top = 1.0;

    if (header) {
        pad(x->indent);
        t_dim(t);
        printf("  %-*.*s %*s", name_w, name_w, "name", size_w, "size");
        if (show_files)
            printf(" %*s", files_w, "files");
        if (pct_w)
            printf(" %*s", pct_w, "share");
        if (bar_w >= 19)
            fputs(" relative to largest", stdout);
        else if (bar_w >= 10)
            fputs(" vs largest", stdout);
        t_reset(t);
        fputc('\n', stdout);

        pad(x->indent);
        if (plain) {
            for (int k = 0; k < table_w; k++)
                fputc('-', stdout);
        } else {
            for (int k = 0; k < table_w; k++)
                fputs("\xe2\x94\x80", stdout);       /* U+2500 */
        }
        fputc('\n', stdout);
    }

    for (int i = 0; i < shown; i++) {
        const Entry *e = c->slice[i];

        pad(x->indent);
        if (plain) {
            printf("%c ", e->sym);
        } else {
            t_fg(t, e->color);
            fputs("\xe2\x97\x8f ", stdout);          /* U+25CF */
            t_reset(t);
        }

        snprintf(raw, sizeof(raw), "%s%s", e->name, e->is_dir ? "/" : "");
        fit(raw, buf, sizeof(buf), name_w);
        if (e->is_dir)
            t_bold(t);
        fputs(buf, stdout);
        t_reset(t);
        pad(name_w - display_width(buf));

        human_size(e->size, buf, sizeof(buf));
        printf(" %*s", size_w, buf);

        if (show_files) {
            if (counted(e))
                snprintf(buf, sizeof(buf), "%lld", e->files);
            else
                buf[0] = '\0';
            t_dim(t);
            printf(" %*s", files_w, buf);
            t_reset(t);
        }

        if (pct_w) {
            snprintf(buf, sizeof(buf), "%.1f%%", c->pct[i] * 100.0);
            t_dim(t);
            printf(" %*s", pct_w, buf);
            t_reset(t);
        }

        if (bar_w > 0) {
            fputc(' ', stdout);
            inline_bar(t, c->pct[i] / top, bar_w, e->color, plain,
                       e->size <= 0);
        }
        fputc('\n', stdout);
    }

    if (more) {
        int rest = c->n - shown;
        snprintf(raw, sizeof(raw), "  %d more entr%s", rest,
                 rest == 1 ? "y" : "ies");
        fit(raw, buf, sizeof(buf), avail);
        pad(x->indent);
        t_dim(t);
        fputs(buf, stdout);
        t_reset(t);
        fputc('\n', stdout);
    }
}

/* ------------------------------------------------------------- the gauge */

static RGB fullness_color(double frac)
{
    if (frac < 0.70) { RGB g = {  76, 185, 118 }; return g; }
    if (frac < 0.85) { RGB a = { 226, 168,  62 }; return a; }
    RGB r = { 221, 84, 78 };
    return r;
}

/* Percentages read better with a decimal only where one carries meaning. */
static void pct_str(double frac, char *out, size_t len)
{
    double v = frac * 100.0;
    if (v > 0.0 && v < 10.0)
        snprintf(out, len, "%.1f%%", v);
    else
        snprintf(out, len, "%.0f%%", v);
}

typedef struct {
    const Term *t;
    char        label[4][16];
    char        value[4][16];
    char        note[4][48];
    int         n;
    int         label_w, value_w;
} GaugeText;

static void gauge_add(GaugeText *g, const char *label, const char *value,
                      const char *note)
{
    if (g->n >= 4)
        return;
    snprintf(g->label[g->n], sizeof(g->label[0]), "%s", label);
    snprintf(g->value[g->n], sizeof(g->value[0]), "%s", value);
    snprintf(g->note[g->n], sizeof(g->note[0]), "%s", note);

    int w = display_width(g->label[g->n]);
    if (w > g->label_w) g->label_w = w;
    w = display_width(g->value[g->n]);
    if (w > g->value_w) g->value_w = w;
    g->n++;
}

static void gauge_line(const GaugeText *g, int i, int indent, int cols)
{
    char buf[96];
    snprintf(buf, sizeof(buf), "%-*s  %*s  %s", g->label_w, g->label[i],
             g->value_w, g->value[i], g->note[i]);
    for (size_t k = strlen(buf); k > 0 && buf[k - 1] == ' '; k--)
        buf[k - 1] = '\0';

    char cut[96];
    fit(buf, cut, sizeof(cut), cols - indent);
    pad(indent);
    t_dim(g->t);
    fputs(cut, stdout);
    t_reset(g->t);
    fputc('\n', stdout);
}

typedef struct {
    const Term *t;
    int         hole_row;
    int         hole_col;
    int         canvas_cols;
    char        hole_text[16];
} GaugeRows;

static void gauge_row(void *ctx, int row)
{
    const GaugeRows *g = ctx;
    if (row != g->hole_row)
        return;

    int len = display_width(g->hole_text);
    int back = g->canvas_cols - g->hole_col;
    if (len <= 0 || back <= 0 || back > g->canvas_cols)
        return;

    printf("\033[%dD", back);
    t_bold(g->t);
    fputs(g->hole_text, stdout);
    t_reset(g->t);
    int forward = back - len;
    if (forward > 0)
        printf("\033[%dC", forward);
}

void chart_gauge(ChartCtx *x)
{
    if (!x || !x->t || x->rows <= 0 || x->cols <= x->indent)
        return;

    const Term *t = x->t;
    struct statvfs st;
    bool have_fs = (x->path && statvfs(x->path, &st) == 0);

    double fs_total = 0.0, fs_used = 0.0, fs_free = 0.0;
    if (have_fs) {
        double unit = (double)(st.f_frsize ? st.f_frsize : st.f_bsize);
        fs_total = (double)st.f_blocks * unit;
        fs_free  = (double)st.f_bavail * unit;
        fs_used  = st.f_bfree <= st.f_blocks
                 ? (double)(st.f_blocks - st.f_bfree) * unit : fs_total;
        if (!(fs_total > 0.0))
            have_fs = false;
    }

    double used_frac = have_fs ? clamp01(fs_used / fs_total) : 0.0;
    double tree_frac = have_fs && x->total > 0
                     ? clamp01((double)x->total / fs_total) : 0.0;

    char used_buf[32], free_buf[32], total_buf[32], tree_buf[32];
    char pct_buf[16], note[64];
    human_size((long long)fs_used, used_buf, sizeof(used_buf));
    human_size((long long)fs_free, free_buf, sizeof(free_buf));
    human_size((long long)fs_total, total_buf, sizeof(total_buf));
    human_size(x->total, tree_buf, sizeof(tree_buf));
    snprintf(pct_buf, sizeof(pct_buf), "%.0f%%", used_frac * 100.0);
    char share_buf[16];
    pct_str(tree_frac, share_buf, sizeof(share_buf));

    GaugeText g;
    memset(&g, 0, sizeof(g));
    g.t = t;

    if (have_fs) {
        char tmp[16];
        snprintf(note, sizeof(note), "%s of %s", pct_buf, total_buf);
        gauge_add(&g, "used", used_buf, note);
        pct_str(fs_free / fs_total, tmp, sizeof(tmp));
        snprintf(note, sizeof(note), "%s free", tmp);
        gauge_add(&g, "available", free_buf, note);
        snprintf(note, sizeof(note), "%s of the filesystem", share_buf);
        gauge_add(&g, "this tree", tree_buf, note);
    } else {
        gauge_add(&g, "this tree", tree_buf, "");
        gauge_add(&g, "filesystem", "?", "could not be queried");
    }

    RGB used_col = fullness_color(used_frac);
    RGB tree_col = (x->data && x->data->n > 0)
                 ? x->data->slice[0]->color : used_col;

    /* Reserve the text first; the arc gets whatever rows are left over. */
    int lines = g.n > x->rows ? x->rows : g.n;
    int arc_rows = x->rows - lines;
    if (arc_rows > 12)
        arc_rows = 12;

    int avail = x->cols - x->indent;
    int arc_cols = arc_rows * 3 + 4;
    if (arc_cols > avail)
        arc_cols = avail;

    Dots *dt = (have_fs && arc_rows >= 6 && arc_cols >= 12)
             ? dots_new(arc_cols, arc_rows) : NULL;

    /* The arc spans 270 degrees round to 135, so it reaches full width but
     * only 0.707 r below the centre: 1.708 r of height in all, in subpixels. */
    double subw = arc_cols * 2.0, subh = arc_rows * 4.0;
    double radius = 0.0, cxs = 0.0, cys = 0.0;
    if (dt) {
        double by_w = (subw - 4.0) / 2.0;
        double by_h = (subh - 4.0) / 1.708;
        radius = by_w < by_h ? by_w : by_h;
        cxs = subw / 2.0;
        cys = 2.0 + radius;
        if (radius < 10.0) {
            dots_free(dt);
            dt = NULL;
        }
    }

    if (!dt) {
        /* No surface, or no room for one: a single inline meter instead. */
        int budget = x->rows;
        if (have_fs && budget > 0) {
            int bar_w = avail - display_width(pct_buf) - 14;
            if (bar_w > 40)
                bar_w = 40;
            if (bar_w >= 4) {
                pad(x->indent);
                t_dim(t);
                fputs("filesystem  ", stdout);
                t_reset(t);
                int drawn = inline_bar(t, used_frac, bar_w, used_col,
                                       !t->color, false);
                pad(bar_w - drawn);
                t_dim(t);
                printf(" %s", pct_buf);
                t_reset(t);
                fputc('\n', stdout);
                budget--;
            }
        }
        for (int i = 0; i < g.n && budget > 0; i++, budget--)
            gauge_line(&g, i, x->indent, x->cols);
        return;
    }

    double track_t = radius * 0.22;
    if (track_t < 3.0) track_t = 3.0;
    double r_out = radius, r_in = radius - track_t;

    double gap = radius * 0.08;
    if (gap < 1.5) gap = 1.5;
    double t_out = r_in - gap;
    double t_thick = track_t * 0.5;
    if (t_thick < 2.0) t_thick = 2.0;
    double t_in = t_out - t_thick;
    bool inner = t_in > radius * 0.22;

    RGB track = { 90, 90, 100 };

    /* Wedges never overlap: the filled span and the track split the same
     * 270 degrees rather than one being drawn over the other. */
    double used_end = 225.0 + 270.0 * used_frac;
    if (used_frac > 0.0)
        dots_wedge(dt, cxs, cys, r_in, r_out, 225.0, used_end, used_col);
    if (used_frac < 1.0)
        dots_wedge(dt, cxs, cys, r_in, r_out, used_end, 495.0, track);

    if (inner) {
        double sweep = 270.0 * tree_frac;
        double least = 90.0 / (t_out > 1.0 ? t_out : 1.0);
        if (tree_frac > 0.0 && sweep < least)
            sweep = least;
        double tree_end = 225.0 + sweep;

        if (sweep > 0.0)
            dots_wedge(dt, cxs, cys, t_in, t_out, 225.0, tree_end, tree_col);
        if (sweep < 270.0)
            dots_wedge(dt, cxs, cys, t_in, t_out, tree_end, 495.0, track);
    }

    GaugeRows gr;
    memset(&gr, 0, sizeof(gr));
    gr.t = t;
    gr.canvas_cols = arc_cols;
    gr.hole_row = -1;

    double hole = inner ? t_in : r_in;
    if (hole > 4.0) {
        int row = (int)lround(cys / 4.0) - 1;
        if (row >= 0 && row < arc_rows) {
            int len = display_width(pct_buf);
            double half = sqrt(hole * hole - 4.0 * 4.0);
            int room = (int)floor(2.0 * half / 2.0);
            if (len <= room) {
                int col = (int)lround(cxs / 2.0 - len / 2.0);
                if (col < 0) col = 0;
                if (col + len > arc_cols) col = arc_cols - len;
                gr.hole_row = row;
                gr.hole_col = col;
                snprintf(gr.hole_text, sizeof(gr.hole_text), "%s", pct_buf);
            }
        }
    }

    dots_present(dt, t, x->indent, arc_rows, gauge_row, &gr);
    dots_free(dt);

    for (int i = 0; i < lines; i++)
        gauge_line(&g, i, x->indent, x->cols);
}
