/*
 * Directory sizing: plain files by their own size, directories by summing
 * their contents recursively the way du(1) does. Symlinks count as the link
 * and are never followed, so a symlink loop cannot trap us and a link to a
 * huge tree is not billed to this directory.
 */

#define _DEFAULT_SOURCE

#include "diskpi.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static long long scan_dir(const char *path, long long *files);

static long long size_of(const char *path, const struct stat *st,
                         long long *files)
{
    if (S_ISDIR(st->st_mode))
        return scan_dir(path, files);

    (*files)++;
    if (S_ISLNK(st->st_mode))
        return 0;
    return (long long)st->st_size;
}

static long long scan_dir(const char *path, long long *files)
{
    DIR *dir = opendir(path);
    if (!dir)
        return 0;               /* unreadable: treat as empty, keep going */

    long long total = 0;
    struct dirent *de;

    while ((de = readdir(dir)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;

        size_t len = strlen(path) + strlen(de->d_name) + 2;
        char *child = malloc(len);
        if (!child)
            continue;
        snprintf(child, len, "%s/%s", path, de->d_name);

        struct stat st;
        if (lstat(child, &st) == 0)
            total += size_of(child, &st, files);

        free(child);
    }

    closedir(dir);
    return total;
}

Entry *scan_directory(const char *path, int *count,
                      long long *total, long long *files)
{
    DIR *dir = opendir(path);
    if (!dir)
        return NULL;

    Entry *entries = NULL;
    int n = 0, cap = 0;
    struct dirent *de;

    *total = 0;
    *files = 0;

    while ((de = readdir(dir)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;

        size_t len = strlen(path) + strlen(de->d_name) + 2;
        char *child = malloc(len);
        if (!child)
            continue;
        snprintf(child, len, "%s/%s", path, de->d_name);

        struct stat st;
        if (lstat(child, &st) != 0) {
            free(child);
            continue;
        }

        if (n == cap) {
            cap = cap ? cap * 2 : 64;
            Entry *grown = realloc(entries, (size_t)cap * sizeof(Entry));
            if (!grown) {
                free(child);
                break;
            }
            entries = grown;
        }

        long long sub = 0;
        long long size = size_of(child, &st, &sub);
        free(child);

        entries[n].name   = strdup(de->d_name);
        entries[n].size   = size;
        entries[n].files  = sub;
        entries[n].is_dir = S_ISDIR(st.st_mode);
        entries[n].sym    = '#';
        entries[n].color  = (RGB){ 128, 128, 128 };

        *total += size;
        *files += sub;
        n++;
    }

    closedir(dir);
    *count = n;
    return entries;
}
