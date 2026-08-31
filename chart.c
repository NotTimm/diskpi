/*
 * Chart registry: names in, drawing out.
 *
 * The table is indexed by ChartKind, so the order here must match the enum
 * in chart.h. chart_desc() feeds --chart=list and nothing else.
 */

#include "chart.h"

#include <stddef.h>
#include <string.h>

static const struct {
    const char *name;
    const char *desc;
} kinds[CHART_COUNT] = {
    { "pie",      "circular wedges, one per entry" },
    { "doughnut", "pie with the total in the hole" },
    { "bar",      "horizontal bars, largest first" },
    { "column",   "vertical bars along a baseline" },
    { "stacked",  "one bar, every entry stacked to 100%" },
    { "line",     "cumulative share by rank" },
    { "area",     "the same curve, filled" },
    { "scatter",  "file count against size" },
    { "bubble",   "file count against mean file size, area = size" },
    { "radar",    "the largest entries as spokes" },
    { "table",    "comparison table with inline bars" },
    { "gauge",    "filesystem fullness, and this tree's share" }
};

const char *chart_name(ChartKind k)
{
    if (k < 0 || k >= CHART_COUNT)
        return "?";
    return kinds[k].name;
}

const char *chart_desc(ChartKind k)
{
    if (k < 0 || k >= CHART_COUNT)
        return "";
    return kinds[k].desc;
}

bool chart_lookup(const char *name, ChartKind *out)
{
    if (!name)
        return false;

    for (int i = 0; i < CHART_COUNT; i++)
        if (strcmp(name, kinds[i].name) == 0) {
            if (out)
                *out = (ChartKind)i;
            return true;
        }
    return false;
}

void chart_draw(ChartCtx *x, ChartKind kind)
{
    if (!x)
        return;

    switch (kind) {
    case CHART_PIE:      chart_pie(x, false);    break;
    case CHART_DOUGHNUT: chart_pie(x, true);     break;
    case CHART_BAR:      chart_bars(x, false);   break;
    case CHART_COLUMN:   chart_bars(x, true);    break;
    case CHART_STACKED:  chart_stacked(x);       break;
    case CHART_LINE:     chart_curve(x, false);  break;
    case CHART_AREA:     chart_curve(x, true);   break;
    case CHART_SCATTER:  chart_points(x, false); break;
    case CHART_BUBBLE:   chart_points(x, true);  break;
    case CHART_RADAR:    chart_radar(x);         break;
    case CHART_TABLE:    chart_table(x);         break;
    case CHART_GAUGE:    chart_gauge(x);         break;
    case CHART_COUNT:                            break;
    }
}
