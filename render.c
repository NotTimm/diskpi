/*
 * Shared text helpers: colour generation, human-readable sizes, UTF-8 aware
 * truncation, and the legend line every chart type prints.
 */

#define _DEFAULT_SOURCE

#include "chart.h"
#include "diskpi.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* ----------------------------------------------------------- small utils */

RGB hsl(double h, double s, double l)
{
    h = fmod(fmod(h, 360.0) + 360.0, 360.0);

    double c = (1.0 - fabs(2.0 * l - 1.0)) * s;
    double x = c * (1.0 - fabs(fmod(h / 60.0, 2.0) - 1.0));
    double m = l - c / 2.0;
    double r = 0, g = 0, b = 0;

    if (h < 60)       { r = c; g = x; }
    else if (h < 120) { r = x; g = c; }
    else if (h < 180) { g = c; b = x; }
    else if (h < 240) { g = x; b = c; }
    else if (h < 300) { r = x; b = c; }
    else              { r = c; b = x; }

    RGB out = {
        (unsigned char)((r + m) * 255.0 + 0.5),
        (unsigned char)((g + m) * 255.0 + 0.5),
        (unsigned char)((b + m) * 255.0 + 0.5)
    };
    return out;
}

void human_size(long long bytes, char *out, size_t out_len)
{
    static const char *unit[] = { "B", "KB", "MB", "GB", "TB", "PB" };
    double value = (double)bytes;
    int i = 0;

    while (value >= 1024.0 && i < 5) {
        value /= 1024.0;
        i++;
    }

    if (i == 0)
        snprintf(out, out_len, "%lld B", bytes);
    else if (value < 10.0)
        snprintf(out, out_len, "%.1f %s", value, unit[i]);
    else
        snprintf(out, out_len, "%.0f %s", value, unit[i]);
}

/* ---------------------------------------------------------------- colour */

/* Up to twelve entries get hand-picked colours: every neighbouring pair is
 * a long way round the wheel from the last, which no formula spacing hues
 * evenly manages as well at small counts. Past twelve there are more slices
 * than there are colours anyone can tell apart by hue alone, so the walk
 * takes over - the golden angle for the hue, and a three-step cycle of
 * lightness and saturation underneath it, so two slices that do land on a
 * similar hue still differ in weight. */
RGB palette(int i, int n)
{
    static const RGB vivid[12] = {
        { 232,  72,  78 },   /* red      */
        {  56, 176, 240 },   /* azure    */
        {  92, 208,  96 },   /* green    */
        { 246, 178,  48 },   /* amber    */
        { 172, 114, 248 },   /* violet   */
        {  40, 208, 192 },   /* teal     */
        { 242,  96, 192 },   /* magenta  */
        { 176, 216,  62 },   /* lime     */
        {  92, 126, 246 },   /* blue     */
        { 250, 134,  58 },   /* orange   */
        { 246, 146, 170 },   /* pink     */
        { 218, 202,  72 }    /* gold     */
    };

    if (i < 0)
        i = 0;
    if (n <= 12 && i < 12)
        return vivid[i];

    double hue = 200.0 + 137.508 * i;
    static const double light[3] = { 0.64, 0.50, 0.74 };
    static const double sat[3]   = { 0.88, 0.74, 0.96 };

    return hsl(hue, sat[i % 3], light[i % 3]);
}

/* Columns a UTF-8 string occupies, counting one per code point. */
int display_width(const char *s)
{
    int w = 0;
    for (; *s; s++)
        if ((*s & 0xC0) != 0x80)
            w++;
    return w;
}

void fit(const char *src, char *dst, size_t dst_len, int width)
{
    if (width <= 0) {
        dst[0] = '\0';
        return;
    }
    if (display_width(src) <= width) {
        snprintf(dst, dst_len, "%.*s", (int)dst_len - 1, src);
        return;
    }

    /* Cut on a code point boundary, leaving room for the ellipsis. */
    int kept = 0;
    size_t i = 0;
    while (src[i] && kept < width - 1) {
        i++;
        while ((src[i] & 0xC0) == 0x80)
            i++;
        kept++;
    }
    snprintf(dst, dst_len, "%.*s…", (int)i, src);
}

/* ---------------------------------------------------------------- legend */

void legend_line(const Term *t, const Chart *c, int i, int width,
                 bool show_files)
{
    const Entry *e = c->slice[i];
    char size_buf[32], pct_buf[16], files_buf[32], name_buf[512], raw[600];

    human_size(e->size, size_buf, sizeof(size_buf));
    snprintf(pct_buf, sizeof(pct_buf), "%5.1f%%", c->pct[i] * 100.0);

    /* A count is only interesting for things that contain other things. */
    bool counted = (e->is_dir || e->aggregate) && e->files > 0;
    if (e->files == 1)
        snprintf(files_buf, sizeof(files_buf), "1 file");
    else
        snprintf(files_buf, sizeof(files_buf), "%lld files", e->files);

    /* The line fills exactly `width`: a marker, the name, then the fixed
     * columns. Twelve is wide enough for "12345 files" and a separator;
     * anything longer is truncated rather than allowed to push the line out.
     * On a narrow terminal the optional columns are shed, least useful
     * first, until the name has somewhere to go. */
    bool want_files = show_files, want_pct = true, want_size = true;
    int files_col, name_width;

    for (;;) {
        files_col = want_files ? 12 : 0;
        name_width = width - 2 - 2 - files_col
                   - (want_size ? 9 : 0) - (want_pct ? 6 : 0);
        if (name_width >= 6)
            break;
        if (want_files)     want_files = false;
        else if (want_pct)  want_pct = false;
        else if (want_size) want_size = false;
        else                break;
    }
    if (name_width < 1)
        name_width = 1;

    snprintf(raw, sizeof(raw), "%s%s", e->name, e->is_dir ? "/" : "");
    fit(raw, name_buf, sizeof(name_buf), name_width);

    if (!t->color) {
        printf("%c%c ", e->sym, e->sym);
    } else {
        t_fg(t, e->color);
        fputs("\xe2\x97\x8f ", stdout);      /* U+25CF black circle */
        t_reset(t);
    }

    if (e->is_dir)
        t_bold(t);
    t_fg(t, e->color);
    fputs(name_buf, stdout);
    t_reset(t);

    for (int k = display_width(name_buf); k < name_width + 2; k++)
        fputc(' ', stdout);

    if (want_files) {
        t_dim(t);
        printf("%*.*s ", files_col - 1, files_col - 1,
               counted ? files_buf : "");
        t_reset(t);
    }

    if (want_size)
        printf("%8s ", size_buf);
    if (want_pct) {
        t_dim(t);
        fputs(pct_buf, stdout);
        t_reset(t);
    }
}
