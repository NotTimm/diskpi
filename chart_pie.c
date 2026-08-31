/*
 * Pie and doughnut.
 *
 * Wedges run clockwise from twelve o'clock and are inset at both ends by half
 * a hairline so neighbouring fills never touch. The inset is angular, so it is
 * taken at the radius where the band is actually looked at: the rim of a pie,
 * the middle of a doughnut's ring. A wedge too thin to survive its own gap
 * keeps its full span instead of disappearing.
 *
 * The doughnut's hole is left transparent and the total is written into it as
 * ordinary terminal text: the row function backs the cursor up over the chart
 * columns it has just passed, which is safe only because those cells are
 * empty, and hands the cursor back before the legend.
 */

#define _DEFAULT_SOURCE

#include "chart.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define GAP_PX      1.5     /* hairline between wedges, in canvas pixels */
#define GAP_MIN_R  14.0     /* under this radius the hairline costs more
                             * than the separation is worth */
#define HOLE_R      0.58    /* doughnut inner radius, in outer radii */
#define GUTTER      3       /* columns between the circle and the legend */
#define LEGEND_MIN  26      /* narrower than this, legend_line overflows */

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

    if (x->mode == MODE_ASCII || !x->t->color) {
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

/* No canvas, or no room for a circle worth drawing: just the numbers. */
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
            legend_line(x->t, x->data, i, width, x->mode, width >= 52);
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

/* Write one hole line by rewinding over chart columns already printed. */
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
        legend_line(p->x->t, p->x->data, li, p->x->legend_w, p->x->mode,
                    p->show_files);
    } else if (p->more > 0 && li == p->legend_n) {
        for (int k = 0; k < GUTTER; k++)
            fputc(' ', stdout);
        more_line(p->x, p->more);
    }
}

/* ---------------------------------------------------------------- drawing */

static void draw_wedges(Canvas *cv, const Chart *c, double cx, double cy,
                        double r_in, double r_out)
{
    /* One gap measured across the middle of the visible band, expressed as
     * the angle that spans it there. */
    double r_ref = r_in > 0.0 ? (r_in + r_out) / 2.0 : r_out;
    double gap_deg = 0.0;
    if (c->n > 1 && r_out >= GAP_MIN_R && r_ref > 0.0)
        gap_deg = GAP_PX / r_ref * 180.0 / M_PI;

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

        canvas_wedge(cv, cx, cy, r_in, r_out, a0, a1,
                     c->slice[i]->color, 1.0);
    }
}

/* Two lines centred in the hole, if the chord at their outer edge is wide
 * enough to hold them. */
static void place_hole_text(PieRows *p, const Canvas *cv, const ChartCtx *x,
                            double cx, double cy, double r_in)
{
    p->hole_rows = 0;
    if (x->mode == MODE_ASCII || r_in <= cv->px_h)
        return;

    int row_a = (int)lround(cy / cv->px_h) - 1;
    if (row_a < 0 || row_a + 1 >= cv->rows)
        return;

    /* Worst case is the far edge of the outer row, one cell from centre. */
    double half = sqrt(r_in * r_in - cv->px_h * cv->px_h);
    int room = (int)floor(2.0 * half / cv->px_w);
    if (room > cv->cols)
        room = cv->cols;
    if (room < 3)
        return;

    char size_buf[32], count_buf[48];
    human_size(x->total, size_buf, sizeof(size_buf));
    snprintf(count_buf, sizeof(count_buf), "%d entr%s", x->entries,
             x->entries == 1 ? "y" : "ies");

    /* The size leads; a count on its own in the hole reads as a stray
     * number, so it is only added once the size is in place. */
    const char *src[2] = { size_buf, count_buf };
    for (int k = 0; k < 2; k++) {
        int len = display_width(src[k]);
        if (len > room)
            break;

        int col = (int)lround(cx / cv->px_w - len / 2.0);
        if (col + len > cv->cols)
            col = cv->cols - len;
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

    /* Cells are taller than they are wide, so a round circle needs more
     * columns than rows in the same proportion. */
    double px_w = 1.0, px_h = 2.0;
    if (x->mode == MODE_PIXELS && x->t->cell_w > 0 && x->t->cell_h > 0) {
        px_w = x->t->cell_w;
        px_h = x->t->cell_h;
    }
    double aspect = px_h / px_w;

    int rows = x->rows > 36 ? 36 : x->rows;
    int cols = (int)(rows * aspect + 0.5);
    if (cols > avail) {
        cols = avail;
        rows = (int)(cols / aspect + 0.5);
    }
    if (rows > x->rows)
        rows = x->rows;

    Canvas *cv = (rows >= 5 && cols >= 10)
               ? canvas_new(x->t, x->mode, cols, rows) : NULL;
    if (!cv) {
        pie_fallback(x);
        return;
    }

    double cx = cv->w / 2.0, cy = cv->h / 2.0;
    double r_out = (cv->w < cv->h ? cv->w : cv->h) / 2.0 - 1.0;
    if (r_out < 2.0) {
        canvas_free(cv);
        pie_fallback(x);
        return;
    }
    double r_in = doughnut ? r_out * HOLE_R : 0.0;

    draw_wedges(cv, x->data, cx, cy, r_in, r_out);

    PieRows p;
    memset(&p, 0, sizeof(p));
    p.x           = x;
    p.canvas_cols = cv->cols;
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
    p.legend_start = (cv->rows - legend_rows) / 2;
    if (p.legend_start < 0)
        p.legend_start = 0;
    if (p.legend_start + legend_rows > x->rows)
        p.legend_start = x->rows - legend_rows;
    if (p.legend_start < 0)
        p.legend_start = 0;

    if (doughnut)
        place_hole_text(&p, cv, x, cx, cy, r_in);

    int total_rows = p.legend_start + legend_rows;
    if (total_rows < cv->rows)
        total_rows = cv->rows;
    if (total_rows > x->rows)
        total_rows = x->rows;

    canvas_present(cv, x->t, x->mode, x->indent, total_rows, pie_row, &p);
    canvas_free(cv);
}
