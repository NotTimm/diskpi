/*
 * Drawing.
 *
 * The pie is defined once as a continuous shape — a point-in-wedge test over
 * the unit circle — and then sampled at whatever resolution the terminal can
 * actually show. Every cell is supersampled, so edges come out antialiased
 * instead of stair-stepped:
 *
 *   MODE_PIXELS  one sample grid per screen pixel, sent as an RGBA image
 *                through the Kitty graphics protocol. Alpha carries the
 *                coverage, so the terminal blends the edge for us.
 *   MODE_BLOCKS  two colour rows per text row via U+2580 (upper half block),
 *                foreground painting the top half and background the bottom.
 *                Partial coverage is blended against the queried background.
 *   MODE_ASCII   one character per cell, no colour required.
 */

#define _DEFAULT_SOURCE

#include "diskpi.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

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
    static const char *unit[] = { "B", "K", "M", "G", "T", "P" };
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

static RGB blend(RGB fg, RGB bg, double a)
{
    RGB out = {
        (unsigned char)(fg.r * a + bg.r * (1.0 - a) + 0.5),
        (unsigned char)(fg.g * a + bg.g * (1.0 - a) + 0.5),
        (unsigned char)(fg.b * a + bg.b * (1.0 - a) + 0.5)
    };
    return out;
}

/* ------------------------------------------------------- the pie, as math */

/* Which wedge covers (x, y)? Radius units, y pointing down; -1 for none. */
static int pie_index_at(const Chart *c, double x, double y)
{
    double r2 = x * x + y * y;
    if (r2 > 1.0)
        return -1;

    double r = sqrt(r2);
    double theta = atan2(-y, x);                       /* CCW from east */
    double turn = fmod(M_PI / 2.0 - theta + 2.0 * M_PI, 2.0 * M_PI);
    double f = turn / (2.0 * M_PI);                    /* clockwise from 12 */

    int idx = c->n - 1;
    for (int i = 0; i < c->n; i++)
        if (f < c->cum[i]) {
            idx = i;
            break;
        }

    /* Hairline separator, but never one wide enough to swallow its wedge. */
    if (c->gap > 0.0 && c->n > 1) {
        double start = idx > 0 ? c->cum[idx - 1] : 0.0;
        double end   = c->cum[idx];
        double near  = (f - start < end - f ? f - start : end - f);
        double arc   = near * 2.0 * M_PI * r;
        double wedge = (end - start) * 2.0 * M_PI * r;
        if (wedge > 6.0 * c->gap && arc < c->gap / 2.0)
            return -1;
    }

    return idx;
}

static bool pie_at(const Chart *c, double x, double y, RGB *out)
{
    int idx = pie_index_at(c, x, y);
    if (idx < 0)
        return false;
    *out = c->slice[idx]->color;
    return true;
}

/* Average the wedge colour over a ss*ss grid; returns coverage in 0..1. */
static double sample(const Chart *c, double cx, double cy,
                     double hx, double hy, int ss, RGB *avg)
{
    long r = 0, g = 0, b = 0;
    int hits = 0;

    for (int j = 0; j < ss; j++) {
        double y = cy + hy * (2.0 * (j + 0.5) / ss - 1.0);
        for (int i = 0; i < ss; i++) {
            double x = cx + hx * (2.0 * (i + 0.5) / ss - 1.0);
            RGB col;
            if (pie_at(c, x, y, &col)) {
                r += col.r;
                g += col.g;
                b += col.b;
                hits++;
            }
        }
    }

    if (hits > 0) {
        avg->r = (unsigned char)(r / hits);
        avg->g = (unsigned char)(g / hits);
        avg->b = (unsigned char)(b / hits);
    }
    return (double)hits / (double)(ss * ss);
}

/* ---------------------------------------------------------------- legend */

static void legend_line(const Term *t, const Chart *c, int i, int width,
                        Mode mode, bool show_files)
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

    int files_col = show_files ? 11 : 0;
    int name_width = width - 2 - 2 - files_col - 8 - 1 - 6;
    if (name_width < 6)
        name_width = 6;

    snprintf(raw, sizeof(raw), "%s%s", e->name, e->is_dir ? "/" : "");
    fit(raw, name_buf, sizeof(name_buf), name_width);

    if (mode == MODE_ASCII || !t->color) {
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

    if (show_files) {
        t_dim(t);
        printf("%*s ", files_col - 1, counted ? files_buf : "");
        t_reset(t);
    }

    printf("%8s ", size_buf);
    t_dim(t);
    fputs(pct_buf, stdout);
    t_reset(t);
}

/* ------------------------------------------------- kitty graphics protocol */

static const char b64_alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static char *base64(const unsigned char *data, size_t len, size_t *out_len)
{
    size_t out_cap = ((len + 2) / 3) * 4;
    char *out = malloc(out_cap + 1);
    if (!out)
        return NULL;

    size_t o = 0;
    for (size_t i = 0; i < len; i += 3) {
        unsigned v = (unsigned)data[i] << 16;
        if (i + 1 < len) v |= (unsigned)data[i + 1] << 8;
        if (i + 2 < len) v |= (unsigned)data[i + 2];

        out[o++] = b64_alphabet[(v >> 18) & 0x3F];
        out[o++] = b64_alphabet[(v >> 12) & 0x3F];
        out[o++] = i + 1 < len ? b64_alphabet[(v >> 6) & 0x3F] : '=';
        out[o++] = i + 2 < len ? b64_alphabet[v & 0x3F] : '=';
    }
    out[o] = '\0';
    *out_len = o;
    return out;
}

/* Render the circle as RGBA and hand it to the terminal as one image. */
static bool blit_pixels(const Term *t, const Chart *c,
                        int chart_cols, int chart_rows)
{
    int w = chart_cols * t->cell_w;
    int h = chart_rows * t->cell_h;
    if (w <= 0 || h <= 0)
        return false;

    unsigned char *px = calloc((size_t)w * (size_t)h, 4);
    if (!px)
        return false;

    double cx = w / 2.0, cy = h / 2.0;
    double radius = (w < h ? w : h) / 2.0 - 1.0;
    if (radius < 1.0) {
        free(px);
        return false;
    }

    Chart local = *c;
    local.gap = radius > 48.0 ? 1.6 / radius : 0.0;

    double half = 0.5 / radius;     /* half a pixel, in radius units */

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            double nx = (x + 0.5 - cx) / radius;
            double ny = (y + 0.5 - cy) / radius;

            RGB col = { 0, 0, 0 };
            double cov = sample(&local, nx, ny, half, half, 3, &col);

            unsigned char *p = px + ((size_t)y * w + x) * 4;
            p[0] = col.r;
            p[1] = col.g;
            p[2] = col.b;
            p[3] = (unsigned char)(cov * 255.0 + 0.5);
        }
    }

    size_t b64_len = 0;
    char *payload = base64(px, (size_t)w * (size_t)h * 4, &b64_len);
    free(px);
    if (!payload)
        return false;

    const size_t chunk = 4096;
    size_t sent = 0;
    bool first = true;

    while (sent < b64_len) {
        size_t take = b64_len - sent < chunk ? b64_len - sent : chunk;
        int more = (sent + take < b64_len) ? 1 : 0;

        if (first) {
            /* C=1 keeps the cursor put: we place the image ourselves. */
            printf("\033_Ga=T,f=32,s=%d,v=%d,c=%d,r=%d,C=1,q=2,m=%d;",
                   w, h, chart_cols, chart_rows, more);
            first = false;
        } else {
            printf("\033_Gm=%d;", more);
        }
        fwrite(payload + sent, 1, take, stdout);
        fputs("\033\\", stdout);
        sent += take;
    }

    free(payload);
    return true;
}

/* ------------------------------------------------------------ public draw */

void render_pie(const Term *t, const Chart *c, Mode mode,
                int radius, int legend_w, int indent)
{
    int chart_rows = radius * 2;
    int chart_cols = radius * 4;
    bool show_files = legend_w >= 52;

    int legend_start = (chart_rows - c->n) / 2;
    if (legend_start < 0)
        legend_start = 0;
    int total_rows = chart_rows > legend_start + c->n
                   ? chart_rows : legend_start + c->n;

    if (mode == MODE_PIXELS) {
        /* Lay out the text first, then drop the image into the gap. */
        for (int row = 0; row < total_rows; row++) {
            for (int k = 0; k < indent + chart_cols; k++)
                fputc(' ', stdout);

            int li = row - legend_start;
            if (li >= 0 && li < c->n) {
                fputs("   ", stdout);
                legend_line(t, c, li, legend_w, mode, show_files);
            }
            fputc('\n', stdout);
        }

        printf("\033[%dA\r", total_rows);           /* back to the top */
        if (indent > 0)
            printf("\033[%dC", indent);

        bool ok = blit_pixels(t, c, chart_cols, chart_rows);

        printf("\033[%dB\r", total_rows);
        if (ok)
            return;
        /* The image failed after the space was already reserved; the block
         * renderer below would overwrite it, so just leave the legend. */
        return;
    }

    /* Two colour rows per text row (blocks), or one character (ascii). */
    double cell_hx = 1.0 / (2.0 * radius);      /* half a cell, horizontally */
    double cell_hy = 1.0 / (2.0 * radius);      /* half a half-row */

    for (int row = 0; row < total_rows; row++) {
        for (int k = 0; k < indent; k++)
            fputc(' ', stdout);

        if (row < chart_rows) {
            for (int col = 0; col < chart_cols; col++) {
                double x = (col + 0.5 - chart_cols / 2.0) / (chart_cols / 2.0);

                if (mode == MODE_ASCII) {
                    double y = (row + 0.5 - chart_rows / 2.0) /
                               (chart_rows / 2.0);
                    int idx = pie_index_at(c, x, y);
                    fputc(idx >= 0 ? c->slice[idx]->sym : ' ', stdout);
                    continue;
                }

                /* Upper and lower half of this cell, sampled separately. */
                double y_top = (2 * row + 0.5 - chart_rows) / (double)chart_rows;
                double y_bot = (2 * row + 1.5 - chart_rows) / (double)chart_rows;

                RGB top = { 0, 0, 0 }, bot = { 0, 0, 0 };
                double ct = sample(c, x, y_top, cell_hx, cell_hy, 4, &top);
                double cb = sample(c, x, y_bot, cell_hx, cell_hy, 4, &bot);

                if (t->have_bg) {
                    if (ct > 0.0) top = blend(top, t->bg, ct);
                    if (cb > 0.0) bot = blend(bot, t->bg, cb);
                } else {
                    /* No background to blend into: snap to the majority. */
                    ct = ct >= 0.5 ? 1.0 : 0.0;
                    cb = cb >= 0.5 ? 1.0 : 0.0;
                }

                bool has_top = ct > 0.02, has_bot = cb > 0.02;

                if (!has_top && !has_bot) {
                    t_reset(t);
                    fputc(' ', stdout);
                } else if (has_top && has_bot) {
                    t_fg(t, top);
                    t_bg(t, bot);
                    fputs("\xe2\x96\x80", stdout);       /* U+2580 ▀ */
                    t_reset(t);
                } else if (has_top) {
                    t_bg_default(t);
                    t_fg(t, top);
                    fputs("\xe2\x96\x80", stdout);
                    t_reset(t);
                } else {
                    t_bg_default(t);
                    t_fg(t, bot);
                    fputs("\xe2\x96\x84", stdout);       /* U+2584 ▄ */
                    t_reset(t);
                }
            }
        } else {
            for (int k = 0; k < chart_cols; k++)
                fputc(' ', stdout);
        }

        int li = row - legend_start;
        if (li >= 0 && li < c->n) {
            fputs("   ", stdout);
            legend_line(t, c, li, legend_w, mode, show_files);
        }
        fputc('\n', stdout);
    }
}

void render_bars(const Term *t, const Chart *c, int width)
{
    int bar_w = width - 4;
    if (bar_w < 8)
        bar_w = 8;

    for (int i = 0; i < c->n; i++) {
        fputs("  ", stdout);
        legend_line(t, c, i, width - 4, t->color ? MODE_BLOCKS : MODE_ASCII,
                    false);
        fputc('\n', stdout);

        /* Eighth-width blocks give the bar sub-cell precision too. */
        double exact = c->pct[i] * bar_w;
        int full = (int)exact;
        int eighths = (int)((exact - full) * 8.0 + 0.5);
        if (full == 0 && eighths == 0)
            eighths = 1;

        fputs("  ", stdout);
        t_fg(t, c->slice[i]->color);
        for (int k = 0; k < full; k++)
            fputs(t->color ? "\xe2\x96\x88" : "#", stdout);   /* U+2588 █ */
        if (eighths > 0 && t->color) {
            static const char *part[] = {
                "", "\xe2\x96\x8f", "\xe2\x96\x8e", "\xe2\x96\x8d",
                "\xe2\x96\x8c", "\xe2\x96\x8b", "\xe2\x96\x8a",
                "\xe2\x96\x89", "\xe2\x96\x88"
            };
            fputs(part[eighths], stdout);
        }
        t_reset(t);
        fputc('\n', stdout);
    }
}
