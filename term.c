/*
 * Terminal capability detection.
 *
 * Everything here is done by asking the terminal directly rather than by
 * guessing from $TERM: one round trip carries a Kitty graphics query, a
 * background-colour query and a cell-size query, terminated by a primary
 * device attributes request that every terminal answers. That final answer
 * tells us when to stop reading, so an unsupported query costs nothing but
 * a missing reply.
 */

#define _DEFAULT_SOURCE

#include "diskpi.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

/* ------------------------------------------------------------ colour out */

/* Nearest xterm-256 index, for terminals without 24-bit colour. */
static int rgb_to_256(RGB c)
{
    /* Greys are a separate ramp and are worth checking first. */
    if (abs(c.r - c.g) < 8 && abs(c.g - c.b) < 8 && abs(c.r - c.b) < 8) {
        int level = (c.r * 23 + 127) / 255;
        return 232 + level;
    }
    int r = (c.r * 5 + 127) / 255;
    int g = (c.g * 5 + 127) / 255;
    int b = (c.b * 5 + 127) / 255;
    return 16 + 36 * r + 6 * g + b;
}

void t_fg(const Term *t, RGB c)
{
    if (!t->color)
        return;
    if (t->truecolor)
        printf("\033[38;2;%u;%u;%um", c.r, c.g, c.b);
    else
        printf("\033[38;5;%dm", rgb_to_256(c));
}

void t_bg(const Term *t, RGB c)
{
    if (!t->color)
        return;
    if (t->truecolor)
        printf("\033[48;2;%u;%u;%um", c.r, c.g, c.b);
    else
        printf("\033[48;5;%dm", rgb_to_256(c));
}

void t_bg_default(const Term *t)
{
    if (t->color)
        fputs("\033[49m", stdout);
}

void t_reset(const Term *t)
{
    if (t->color)
        fputs("\033[0m", stdout);
}

void t_bold(const Term *t)
{
    if (t->color)
        fputs("\033[1m", stdout);
}

void t_dim(const Term *t)
{
    if (t->color)
        fputs("\033[2m", stdout);
}

/* --------------------------------------------------------------- queries */

/* Read until the primary-DA reply ("ESC [ ... c") lands or time runs out. */
static size_t read_replies(int fd, char *buf, size_t cap, int timeout_ms)
{
    size_t len = 0;

    while (len + 1 < cap) {
        fd_set set;
        FD_ZERO(&set);
        FD_SET(fd, &set);

        struct timeval tv;
        tv.tv_sec  = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;

        int ready = select(fd + 1, &set, NULL, NULL, &tv);
        if (ready <= 0)
            break;

        ssize_t got = read(fd, buf + len, cap - len - 1);
        if (got <= 0)
            break;
        len += (size_t)got;
        buf[len] = '\0';

        /* The DA reply is last, so its 'c' terminator ends the exchange. */
        for (size_t i = 0; i + 1 < len; i++)
            if (buf[i] == '\033' && buf[i + 1] == '[') {
                for (size_t j = i + 2; j < len; j++) {
                    if (buf[j] == 'c')
                        return len;
                    if (buf[j] == '\033')
                        break;
                }
            }
    }
    return len;
}

/* "rgb:1e1e/2020/2e2e" and the 2- and 3-digit variants. */
static bool parse_osc_color(const char *s, RGB *out)
{
    unsigned v[3] = { 0, 0, 0 };

    for (int i = 0; i < 3; i++) {
        int digits = 0;
        unsigned value = 0;

        while (*s && *s != '/' && *s != '\033' && *s != '\007') {
            int d;
            if (*s >= '0' && *s <= '9')       d = *s - '0';
            else if (*s >= 'a' && *s <= 'f')  d = *s - 'a' + 10;
            else if (*s >= 'A' && *s <= 'F')  d = *s - 'A' + 10;
            else                              return false;
            value = value * 16 + (unsigned)d;
            digits++;
            s++;
        }
        if (digits == 0)
            return false;

        /* Scale whatever width we were given down to 8 bits. */
        while (digits > 2) {
            value >>= 4;
            digits--;
        }
        if (digits == 1)
            value = value * 17;
        v[i] = value;

        if (i < 2) {
            if (*s != '/')
                return false;
            s++;
        }
    }

    out->r = (unsigned char)v[0];
    out->g = (unsigned char)v[1];
    out->b = (unsigned char)v[2];
    return true;
}

static void query_terminal(Term *t)
{
    int fd = isatty(STDIN_FILENO) ? STDIN_FILENO : open("/dev/tty", O_RDWR);
    if (fd < 0)
        return;

    struct termios saved, raw;
    if (tcgetattr(fd, &saved) != 0) {
        if (fd != STDIN_FILENO)
            close(fd);
        return;
    }
    raw = saved;
    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(fd, TCSANOW, &raw) != 0) {
        if (fd != STDIN_FILENO)
            close(fd);
        return;
    }

    /* One 1x1 RGB pixel, transmitted directly, asking only for a verdict. */
    fputs("\033_Gi=31,s=1,v=1,a=q,t=d,f=24;AAAA\033\\", stdout);
    fputs("\033]11;?\033\\", stdout);   /* background colour */
    fputs("\033[16t", stdout);          /* cell size in pixels */
    fputs("\033[c", stdout);            /* sentinel: everyone answers this */
    fflush(stdout);

    char buf[1024];
    size_t len = read_replies(fd, buf, sizeof(buf), 250);

    tcsetattr(fd, TCSANOW, &saved);
    if (fd != STDIN_FILENO)
        close(fd);

    if (len == 0)
        return;

    if (strstr(buf, "_Gi=31;OK"))
        t->kitty = true;

    const char *bg = strstr(buf, "]11;rgb:");
    if (bg && parse_osc_color(bg + 8, &t->bg))
        t->have_bg = true;

    /* Cell size arrives as ESC [ 6 ; height ; width t */
    const char *cell = strstr(buf, "\033[6;");
    if (cell) {
        int h, w;
        if (sscanf(cell + 4, "%d;%dt", &h, &w) == 2 && w > 0 && h > 0) {
            t->cell_w = w;
            t->cell_h = h;
        }
    }
}

/* ---------------------------------------------------------------- detect */

void term_detect(Term *t, bool want_color)
{
    memset(t, 0, sizeof(*t));
    t->cols = 80;
    t->rows = 24;

    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        if (ws.ws_col > 0)
            t->cols = ws.ws_col;
        if (ws.ws_row > 0)
            t->rows = ws.ws_row;
        /* Some terminals report the window in pixels too, which saves a
         * query and is more trustworthy than one. */
        if (ws.ws_xpixel > 0 && ws.ws_ypixel > 0 && ws.ws_col > 0 &&
            ws.ws_row > 0) {
            t->cell_w = ws.ws_xpixel / ws.ws_col;
            t->cell_h = ws.ws_ypixel / ws.ws_row;
        }
    }

    const char *term = getenv("TERM");
    bool dumb = term && strcmp(term, "dumb") == 0;

    t->color = want_color && !dumb && isatty(STDOUT_FILENO);

    const char *ct = getenv("COLORTERM");
    t->truecolor = t->color && ct &&
                   (strstr(ct, "truecolor") || strstr(ct, "24bit"));

    if (t->color && !dumb)
        query_terminal(t);

    /* A terminal that speaks the graphics protocol but never told us its
     * cell size cannot be drawn into accurately. */
    if (t->cell_w <= 0 || t->cell_h <= 0)
        t->kitty = false;
}
