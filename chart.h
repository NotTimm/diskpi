#ifndef CHART_H
#define CHART_H

/*
 * Chart types. Each one gets the same context and owns its whole output
 * block: it prints its own rows, legend included, and leaves the cursor on
 * the line after itself.
 *
 * Everything here is plain terminal text - characters and ANSI colour, no
 * graphics protocol and no half-block drawing - so a chart draws in exactly
 * the time it takes to print it.
 */

#include "diskpi.h"

typedef enum {
    CHART_PIE,
    CHART_DOUGHNUT,
    CHART_BAR,          /* horizontal bars, one per entry */
    CHART_COLUMN,       /* vertical bars, one per entry */
    CHART_STACKED,      /* one bar, every entry stacked to 100% */
    CHART_LINE,         /* cumulative share by rank */
    CHART_AREA,         /* same curve, filled */
    CHART_SCATTER,      /* file count against size */
    CHART_BUBBLE,       /* file count against mean file size, area = size */
    CHART_RADAR,        /* top entries as spokes */
    CHART_TABLE,        /* comparison table with inline bars */
    CHART_GAUGE,        /* filesystem fullness, and this tree's share */
    CHART_COUNT
} ChartKind;

typedef struct {
    const Term  *t;
    const Chart *data;        /* slices, cumulative fractions, percentages */
    const char  *path;        /* directory being shown */
    long long    total;       /* bytes across every entry */
    long long    total_files;
    int          entries;     /* entries found, before pooling */
    int          cols;        /* columns available for the whole block */
    int          rows;        /* rows available for the whole block */
    int          legend_w;
    int          indent;
} ChartCtx;

/* Name <-> kind, for the --chart option and its error message. */
const char *chart_name(ChartKind k);
const char *chart_desc(ChartKind k);
bool        chart_lookup(const char *name, ChartKind *out);

/* Draws the chart. */
void chart_draw(ChartCtx *x, ChartKind kind);

/* Implementations, one per file. */
void chart_pie(ChartCtx *x, bool doughnut);
void chart_bars(ChartCtx *x, bool vertical);
void chart_stacked(ChartCtx *x);
void chart_curve(ChartCtx *x, bool filled);
void chart_points(ChartCtx *x, bool bubble);
void chart_radar(ChartCtx *x);
void chart_table(ChartCtx *x);
void chart_gauge(ChartCtx *x);

/* Shared legend line, printed by most chart types (render.c). It fills
 * exactly `width` columns and needs at least 26 of them; ChartCtx.legend_w
 * is never smaller. */
void legend_line(const Term *t, const Chart *c, int i, int width,
                 bool show_files);

#endif /* CHART_H */
