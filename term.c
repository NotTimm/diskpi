/*
 * Terminal size and colour: read straight from the kernel and the
 * environment, no round trip to the terminal itself. Every chart is plain
 * text, so the only things that matter are how many cells there are and
 * whether colour is worth spending.
 */

#define _DEFAULT_SOURCE

#include "diskpi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
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
    }

    const char *term = getenv("TERM");
    bool dumb = term && strcmp(term, "dumb") == 0;

    t->color = want_color && !dumb && isatty(STDOUT_FILENO);

    const char *ct = getenv("COLORTERM");
    t->truecolor = t->color && ct &&
                   (strstr(ct, "truecolor") || strstr(ct, "24bit"));
}
