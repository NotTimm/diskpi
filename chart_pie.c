/*
 * Pie and doughnut, drawn as an actual circle of braille dots (dots.c),
 * with the legend beside it. Below a certain size there is no room for a
 * circle worth drawing, so it falls back to a plain list.
 *
 * Wedges run clockwise from twelve o'clock and are inset at both ends by a
 * hairline so neighbouring fills never touch. A wedge too thin to survive
 * its own gap keeps its full span instead of disappearing.
 */

#define _DEFAULT_SOURCE

#include "chart.h"
#include "dots.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define GAP_SUB     1.5     /* hairline between wedges, in subpixels */
#define GAP_MIN_R  20.0     /* under this radius the hairline costs more
                             * than the separation is worth */
#define HOLE_R      0.55    /* doughnut inner radius, in outer radii */
#define GUTTER      3       /* columns between the circle and the legend */
#define LEGEND_MIN 26       /* narrower than this, legend_line overflows */
#define MAX_ROWS   18        /* a circle taller than this is more detail
                              * than a terminal legend has room to explain */

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

/* ------------------------------------------------------------- row output */

typedef struct {
    const ChartCtx *x;
    int    canvas_cols;
    int    legend_start;
    int    legend_n;        /* entries actually listed */
    int    more;            /* entries folded into a trailing summary */
    bool   show_files;
    int    hole_rows;       /* 0, or 2 lines centred in the doughnut */
    int    hole_row[2];
    int    hole_col[2];
    char   hole_text[2][48];
} PieRows;

/* Write one hole line by rewinding over the dot columns already printed. */
static void hole_line(const PieRows *p, int k)
{
    const char *text = p->hole_text[k];
    int len = display_width(text);
    int back = p->canvas_cols - p->hole_col[k];

    if (len <= 0 || back <= 0 || back > p->canvas_cols)
        return;

    printf("\033[%dD", back);
    if (k == 0)
        t_bold(p->x->t);
    else
        t_dim(p->x->t);
    fputs(text, stdout);
    t_reset(p->x->t);

    int forward = back - len;
    if (forward > 0)
        printf("\033[%dC", forward);
}

static void pie_row(void *ctx, int row)
{
    const PieRows *p = ctx;

    for (int k = 0; k < p->hole_rows; k++)
        if (p->hole_row[k] == row)
            hole_line(p, k);

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

/* Two lines centred in the hole, if the chord at their outer edge is wide
 * enough to hold them. Subpixels are 2 wide by 4 tall per cell. */
static void place_hole_text(PieRows *p, int cols, int rows, const ChartCtx *x,
                            double cxs, double cys, double r_in)
{
    p->hole_rows = 0;
    if (r_in <= 4.0)
        return;

    int row_a = (int)lround(cys / 4.0) - 1;
    if (row_a < 0 || row_a + 1 >= rows)
        return;

    double half = sqrt(r_in * r_in - 4.0 * 4.0);
    int room = (int)floor(2.0 * half / 2.0);
    if (room > cols)
        room = cols;
    if (room < 3)
        return;

    char size_buf[32], count_buf[48];
    human_size(x->total, size_buf, sizeof(size_buf));
    snprintf(count_buf, sizeof(count_buf), "%d entr%s", x->entries,
             x->entries == 1 ? "y" : "ies");

    const char *src[2] = { size_buf, count_buf };
    for (int k = 0; k < 2; k++) {
        int len = display_width(src[k]);
        if (len > room)
            break;

        int col = (int)lround(cxs / 2.0 - len / 2.0);
        if (col + len > cols)
            col = cols - len;
        if (col < 0)
            break;

        snprintf(p->hole_text[k], sizeof(p->hole_text[k]), "%s", src[k]);
        p->hole_row[k] = row_a + k;
        p->hole_col[k] = col;
        p->hole_rows = k + 1;
    }
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
     * twice as many columns of cells as rows. */
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

    if (doughnut)
        place_hole_text(&p, cols, rows, x, cxs, cys, r_in);

    int total_rows = p.legend_start + legend_rows;
    if (total_rows < rows)
        total_rows = rows;
    if (total_rows > x->rows)
        total_rows = x->rows;

    dots_present(dt, x->t, x->indent, total_rows, pie_row, &p);
    dots_free(dt);
}
