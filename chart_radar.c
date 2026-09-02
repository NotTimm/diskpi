/*
 * Two shapes that answer different questions about the same numbers, as
 * text. The radar names the entries that would have been spokes, largest
 * first; the stacked bar prints one row of proportional symbols followed by
 * its own legend.
 */

#define _DEFAULT_SOURCE

#include "chart.h"

#include <stdio.h>
#include <string.h>

/* More spokes than this and the shape stopped being readable; the text form
 * keeps the same cap so the two stay in step. */
#define RADAR_SPOKES  10

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

    text_list(x, idx, k, x->rows);
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
