/*
 * The drawing surface.
 *
 * Every primitive reduces to one question - is this point inside the shape -
 * answered on a 4x4 subpixel grid over the shape's bounding box. The fraction
 * of subsamples inside is the coverage, and coverage times the caller's alpha
 * is what gets composited source-over into the buffer. That single path is
 * why shapes may overlap and why edges come out smooth without any primitive
 * knowing anything about antialiasing.
 *
 * The buffer is non-premultiplied RGBA and starts fully transparent, so the
 * destination of a composite is frequently transparent rather than opaque;
 * the blend below is written for that case and divides out the result alpha
 * instead of assuming it is one.
 */

#define _DEFAULT_SOURCE

#include "canvas.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* A canvas larger than this is a bug in the caller's layout arithmetic, not
 * a request worth honouring: the guard also keeps w * h * 4 far from
 * overflowing size_t. */
#define CANVAS_MAX_DIM   16384
#define CANVAS_MAX_BYTES (64u * 1024u * 1024u)

#define SS 4                    /* subpixel grid is SS x SS per pixel */

/* --------------------------------------------------------------- surface */

Canvas *canvas_new(const Term *t, Mode mode, int cols, int rows)
{
    if (!t || mode == MODE_ASCII || cols <= 0 || rows <= 0)
        return NULL;

    long long w, h;
    double px_w, px_h;

    if (mode == MODE_PIXELS) {
        if (t->cell_w <= 0 || t->cell_h <= 0)
            return NULL;
        w = (long long)cols * t->cell_w;
        h = (long long)rows * t->cell_h;
        px_w = t->cell_w;
        px_h = t->cell_h;
    } else {
        /* One canvas pixel per half cell. */
        w = cols;
        h = (long long)rows * 2;
        px_w = 1.0;
        px_h = 2.0;
    }

    if (w <= 0 || h <= 0 || w > CANVAS_MAX_DIM || h > CANVAS_MAX_DIM)
        return NULL;
    if ((unsigned long long)w * (unsigned long long)h * 4ull >
        CANVAS_MAX_BYTES)
        return NULL;

    Canvas *c = calloc(1, sizeof(*c));
    if (!c)
        return NULL;

    c->px = calloc((size_t)w * (size_t)h, 4);
    if (!c->px) {
        free(c);
        return NULL;
    }

    c->w    = (int)w;
    c->h    = (int)h;
    c->cols = cols;
    c->rows = rows;
    c->px_w = px_w;
    c->px_h = px_h;
    return c;
}

void canvas_free(Canvas *c)
{
    if (!c)
        return;
    free(c->px);
    free(c);
}

void canvas_clear(Canvas *c)
{
    if (c && c->px)
        memset(c->px, 0, (size_t)c->w * (size_t)c->h * 4);
}

/* ------------------------------------------------------------ compositing */

static void composite(unsigned char *p, RGB col, double sa)
{
    if (sa <= 0.0)
        return;
    if (sa > 1.0)
        sa = 1.0;

    double da = p[3] / 255.0;
    double oa = sa + da * (1.0 - sa);
    if (oa <= 0.0) {
        p[0] = p[1] = p[2] = p[3] = 0;
        return;
    }

    double wd = da * (1.0 - sa);
    double r = (col.r * sa + p[0] * wd) / oa;
    double g = (col.g * sa + p[1] * wd) / oa;
    double b = (col.b * sa + p[2] * wd) / oa;

    if (r < 0.0) r = 0.0; else if (r > 255.0) r = 255.0;
    if (g < 0.0) g = 0.0; else if (g > 255.0) g = 255.0;
    if (b < 0.0) b = 0.0; else if (b > 255.0) b = 255.0;

    p[0] = (unsigned char)(r + 0.5);
    p[1] = (unsigned char)(g + 0.5);
    p[2] = (unsigned char)(b + 0.5);
    p[3] = (unsigned char)(oa * 255.0 + 0.5);
}

/* --------------------------------------------------------------- coverage */

typedef bool (*InsideFn)(const void *shape, double x, double y);

/* The one supersampling loop. The bounding box may be anything; it is
 * clipped, and points outside it are assumed outside the shape, so every
 * primitive must pass a box that truly contains it. */
static void fill_shape(Canvas *c, double bx0, double by0, double bx1,
                       double by1, InsideFn inside, const void *shape,
                       RGB col, double alpha)
{
    if (!c || !c->px || alpha <= 0.0)
        return;
    if (alpha > 1.0)
        alpha = 1.0;
    if (!(bx1 > bx0) || !(by1 > by0))
        return;

    int ix0 = (int)floor(bx0), ix1 = (int)ceil(bx1);
    int iy0 = (int)floor(by0), iy1 = (int)ceil(by1);

    if (ix0 < 0) ix0 = 0;
    if (iy0 < 0) iy0 = 0;
    if (ix1 > c->w) ix1 = c->w;
    if (iy1 > c->h) iy1 = c->h;

    const double step = 1.0 / SS;
    const double first = 0.5 / SS;
    const double total = (double)(SS * SS);

    for (int y = iy0; y < iy1; y++) {
        for (int x = ix0; x < ix1; x++) {
            int hits = 0;
            for (int j = 0; j < SS; j++) {
                double sy = y + first + j * step;
                for (int i = 0; i < SS; i++) {
                    double sx = x + first + i * step;
                    if (inside(shape, sx, sy))
                        hits++;
                }
            }
            if (hits == 0)
                continue;
            composite(c->px + ((size_t)y * (size_t)c->w + (size_t)x) * 4,
                      col, (hits / total) * alpha);
        }
    }
}

/* ------------------------------------------------------------- rectangles */

typedef struct {
    double x0, y0, x1, y1;
    double r;                   /* corner radius, 0 for a plain rectangle */
} RectShape;

static bool rect_inside(const void *shape, double x, double y)
{
    const RectShape *s = shape;

    if (x < s->x0 || x >= s->x1 || y < s->y0 || y >= s->y1)
        return false;
    if (s->r <= 0.0)
        return true;

    /* Distance from the rounded corner's centre, once the point is inside
     * one of the four corner squares. */
    double dx = 0.0, dy = 0.0;
    if (x < s->x0 + s->r)      dx = s->x0 + s->r - x;
    else if (x > s->x1 - s->r) dx = x - (s->x1 - s->r);
    if (y < s->y0 + s->r)      dy = s->y0 + s->r - y;
    else if (y > s->y1 - s->r) dy = y - (s->y1 - s->r);

    return dx * dx + dy * dy <= s->r * s->r;
}

void canvas_rect(Canvas *c, double x, double y, double w, double h,
                 RGB col, double alpha)
{
    canvas_round_rect(c, x, y, w, h, 0.0, col, alpha);
}

void canvas_round_rect(Canvas *c, double x, double y, double w, double h,
                       double radius, RGB col, double alpha)
{
    if (w < 0.0) { x += w; w = -w; }
    if (h < 0.0) { y += h; h = -h; }
    if (w <= 0.0 || h <= 0.0)
        return;

    double limit = (w < h ? w : h) / 2.0;
    if (radius < 0.0)
        radius = 0.0;
    if (radius > limit)
        radius = limit;

    RectShape s = { x, y, x + w, y + h, radius };
    fill_shape(c, s.x0, s.y0, s.x1, s.y1, rect_inside, &s, col, alpha);
}

/* ---------------------------------------------------- circles and annuli */

typedef struct {
    double cx, cy;
    double r2_in, r2_out;
    double a0;                  /* degrees clockwise from twelve o'clock */
    double sweep;               /* >= 360 means the whole annulus */
} ArcShape;

static bool arc_inside(const void *shape, double x, double y)
{
    const ArcShape *s = shape;

    double dx = x - s->cx;
    double dy = y - s->cy;
    double d2 = dx * dx + dy * dy;

    if (d2 > s->r2_out || d2 < s->r2_in)
        return false;
    if (s->sweep >= 360.0)
        return true;

    /* Twelve o'clock is -y, and angles grow clockwise, which is exactly
     * atan2(dx, -dy) once y points down. */
    double ang = atan2(dx, -dy) * 180.0 / M_PI;
    double rel = fmod(ang - s->a0, 360.0);
    if (rel < 0.0)
        rel += 360.0;
    return rel < s->sweep;
}

static void fill_arc(Canvas *c, double cx, double cy, double r_in,
                     double r_out, double a0, double sweep,
                     RGB col, double alpha)
{
    if (r_out <= 0.0 || sweep <= 0.0)
        return;
    if (r_in < 0.0)
        r_in = 0.0;
    if (r_in >= r_out)
        return;

    ArcShape s;
    s.cx     = cx;
    s.cy     = cy;
    s.r2_in  = r_in * r_in;
    s.r2_out = r_out * r_out;
    s.a0     = fmod(a0, 360.0);
    s.sweep  = sweep;

    fill_shape(c, cx - r_out, cy - r_out, cx + r_out, cy + r_out,
               arc_inside, &s, col, alpha);
}

void canvas_circle(Canvas *c, double cx, double cy, double r,
                   RGB col, double alpha)
{
    fill_arc(c, cx, cy, 0.0, r, 0.0, 360.0, col, alpha);
}

void canvas_wedge(Canvas *c, double cx, double cy, double r_inner,
                  double r_outer, double a0, double a1, RGB col, double alpha)
{
    fill_arc(c, cx, cy, r_inner, r_outer, a0, a1 - a0, col, alpha);
}

void canvas_ring(Canvas *c, double cx, double cy, double r_inner,
                 double r_outer, RGB col, double alpha)
{
    fill_arc(c, cx, cy, r_inner, r_outer, 0.0, 360.0, col, alpha);
}

/* ------------------------------------------------------ lines and joins */

typedef struct {
    const Pt *pts;
    int       n;
    double    r2;               /* half thickness, squared */
} PathShape;

/* Squared distance from (x, y) to the segment a-b. */
static double seg_dist2(double x, double y, Pt a, Pt b)
{
    double vx = b.x - a.x, vy = b.y - a.y;
    double wx = x - a.x,   wy = y - a.y;
    double len2 = vx * vx + vy * vy;
    double t = 0.0;

    if (len2 > 0.0) {
        t = (wx * vx + wy * vy) / len2;
        if (t < 0.0) t = 0.0;
        else if (t > 1.0) t = 1.0;
    }

    double dx = wx - t * vx;
    double dy = wy - t * vy;
    return dx * dx + dy * dy;
}

static bool path_inside(const void *shape, double x, double y)
{
    const PathShape *s = shape;

    if (s->n == 1)
        return seg_dist2(x, y, s->pts[0], s->pts[0]) <= s->r2;

    for (int i = 0; i + 1 < s->n; i++)
        if (seg_dist2(x, y, s->pts[i], s->pts[i + 1]) <= s->r2)
            return true;
    return false;
}

static void path_bounds(const Pt *pts, int n, double pad,
                        double *x0, double *y0, double *x1, double *y1)
{
    *x0 = *x1 = pts[0].x;
    *y0 = *y1 = pts[0].y;

    for (int i = 1; i < n; i++) {
        if (pts[i].x < *x0) *x0 = pts[i].x;
        if (pts[i].x > *x1) *x1 = pts[i].x;
        if (pts[i].y < *y0) *y0 = pts[i].y;
        if (pts[i].y > *y1) *y1 = pts[i].y;
    }

    *x0 -= pad; *y0 -= pad;
    *x1 += pad; *y1 += pad;
}

void canvas_polyline(Canvas *c, const Pt *pts, int n, double thickness,
                     RGB col, double alpha)
{
    if (!pts || n <= 0 || thickness <= 0.0)
        return;

    double half = thickness / 2.0;
    PathShape s = { pts, n, half * half };

    double x0, y0, x1, y1;
    path_bounds(pts, n, half + 1.0, &x0, &y0, &x1, &y1);
    fill_shape(c, x0, y0, x1, y1, path_inside, &s, col, alpha);
}

void canvas_line(Canvas *c, double x0, double y0, double x1, double y1,
                 double thickness, RGB col, double alpha)
{
    Pt pts[2] = { { x0, y0 }, { x1, y1 } };
    canvas_polyline(c, pts, 2, thickness, col, alpha);
}

/* ---------------------------------------------------------------- polygons */

typedef struct {
    const Pt *pts;
    int       n;
} PolyShape;

/* Nonzero winding, so self-intersecting outlines stay filled; the ray cast
 * eastwards counts each crossing with its direction. Concave outlines - a
 * radar polygon, usually - need no special handling here. */
static bool poly_inside(const void *shape, double x, double y)
{
    const PolyShape *s = shape;
    int wind = 0;

    for (int i = 0, j = s->n - 1; i < s->n; j = i++) {
        double ay = s->pts[j].y, by = s->pts[i].y;
        if ((ay <= y) == (by <= y))
            continue;

        double ax = s->pts[j].x, bx = s->pts[i].x;
        double cross = ax + (y - ay) / (by - ay) * (bx - ax);
        if (cross > x)
            wind += (by > ay) ? 1 : -1;
    }
    return wind != 0;
}

void canvas_polygon(Canvas *c, const Pt *pts, int n, RGB col, double alpha)
{
    if (!pts || n < 3)
        return;

    PolyShape s = { pts, n };
    double x0, y0, x1, y1;
    path_bounds(pts, n, 1.0, &x0, &y0, &x1, &y1);
    fill_shape(c, x0, y0, x1, y1, poly_inside, &s, col, alpha);
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

static bool blit_kitty(const Canvas *c)
{
    size_t b64_len = 0;
    char *payload = base64(c->px, (size_t)c->w * (size_t)c->h * 4, &b64_len);
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
                   c->w, c->h, c->cols, c->rows, more);
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

/* ----------------------------------------------------------- presentation */

static void spaces(int n)
{
    for (int i = 0; i < n; i++)
        fputc(' ', stdout);
}

/* One canvas pixel resolved against the terminal background. Returns false
 * when the pixel should be left for the terminal to paint. */
static bool resolve(const Term *t, const unsigned char *p, RGB *out)
{
    double a = p[3] / 255.0;
    RGB col = { p[0], p[1], p[2] };

    if (t->have_bg) {
        if (a <= 0.02)
            return false;
        out->r = (unsigned char)(col.r * a + t->bg.r * (1.0 - a) + 0.5);
        out->g = (unsigned char)(col.g * a + t->bg.g * (1.0 - a) + 0.5);
        out->b = (unsigned char)(col.b * a + t->bg.b * (1.0 - a) + 0.5);
        return true;
    }

    /* Nothing to blend into, so a pixel is either there or it is not. */
    if (a < 0.5)
        return false;
    *out = col;
    return true;
}

static void present_blocks(Canvas *c, const Term *t, int indent,
                           int total_rows, CanvasRowFn row_fn, void *ctx)
{
    for (int row = 0; row < total_rows; row++) {
        spaces(indent);

        if (row >= c->rows) {
            spaces(c->cols);
        } else {
            for (int col = 0; col < c->cols; col++) {
                int yt = row * 2, yb = row * 2 + 1;
                RGB top, bot;
                bool has_top = false, has_bot = false;

                if (col < c->w && yt < c->h)
                    has_top = resolve(t, c->px +
                        ((size_t)yt * (size_t)c->w + (size_t)col) * 4, &top);
                if (col < c->w && yb < c->h)
                    has_bot = resolve(t, c->px +
                        ((size_t)yb * (size_t)c->w + (size_t)col) * 4, &bot);

                if (!has_top && !has_bot) {
                    fputc(' ', stdout);
                } else if (has_top && has_bot) {
                    t_fg(t, top);
                    t_bg(t, bot);
                    fputs("\xe2\x96\x80", stdout);       /* U+2580 upper */
                    t_reset(t);
                } else if (has_top) {
                    t_bg_default(t);
                    t_fg(t, top);
                    fputs("\xe2\x96\x80", stdout);
                    t_reset(t);
                } else {
                    t_bg_default(t);
                    t_fg(t, bot);
                    fputs("\xe2\x96\x84", stdout);       /* U+2584 lower */
                    t_reset(t);
                }
            }
            t_reset(t);
        }

        if (row_fn)
            row_fn(ctx, row);
        fputc('\n', stdout);
    }
}

void canvas_present(Canvas *c, const Term *t, Mode mode, int indent,
                    int total_rows, CanvasRowFn row_fn, void *ctx)
{
    if (!c || !t || total_rows <= 0)
        return;
    if (indent < 0)
        indent = 0;

    if (mode != MODE_PIXELS) {
        present_blocks(c, t, indent, total_rows, row_fn, ctx);
        return;
    }

    /* Lay the text out first, then drop the image into the gap it leaves. */
    for (int row = 0; row < total_rows; row++) {
        spaces(indent + c->cols);
        if (row_fn)
            row_fn(ctx, row);
        fputc('\n', stdout);
    }

    printf("\033[%dA\r", total_rows);
    if (indent > 0)
        printf("\033[%dC", indent);

    blit_kitty(c);

    printf("\033[%dB\r", total_rows);
}
