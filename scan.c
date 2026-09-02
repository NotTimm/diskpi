/*
 * Directory sizing: plain files by their own size, directories by summing
 * their contents recursively the way du(1) does. Symlinks count as the link
 * and are never followed, so a symlink loop cannot trap us and a link to a
 * huge tree is not billed to this directory.
 *
 * Sizing each top-level entry is independent of every other one, so once the
 * immediate listing is in hand a pool of threads - one per core - works
 * through the entries in parallel. A directory with a few large subtrees
 * finishes in roughly (total work) / (core count) instead of one thread
 * walking the whole thing alone.
 */

#define _DEFAULT_SOURCE

#include "diskpi.h"

#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_WORKERS 16

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

/* ------------------------------------------------------ parallel sizing */

/* What a worker needs for one top-level entry: the full path, whether it is
 * a directory (already known from pass one, so no second stat), and - for a
 * plain file - its size and symlink-ness straight from that same stat. */
typedef struct {
    char      *path;
    bool       is_dir;
    bool       is_link;
    long long  own_size;
    long long  size;      /* filled in by the worker */
    long long  files;
} Work;

typedef struct {
    Work *items;
    int   n;
    int   next;             /* shared work cursor */
    pthread_mutex_t lock;
} WorkQueue;

static void *worker(void *arg)
{
    WorkQueue *q = arg;

    for (;;) {
        pthread_mutex_lock(&q->lock);
        int i = q->next < q->n ? q->next++ : -1;
        pthread_mutex_unlock(&q->lock);
        if (i < 0)
            break;

        Work *w = &q->items[i];
        if (w->is_dir) {
            w->files = 0;
            w->size = scan_dir(w->path, &w->files);
        } else {
            w->files = 1;
            w->size = w->is_link ? 0 : w->own_size;
        }
    }
    return NULL;
}

static int cpu_count(void)
{
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    if (n < 1)
        n = 1;
    if (n > MAX_WORKERS)
        n = MAX_WORKERS;
    return (int)n;
}

Entry *scan_directory(const char *path, int *count,
                      long long *total, long long *files)
{
    DIR *dir = opendir(path);
    if (!dir)
        return NULL;

    Entry *entries = NULL;
    Work  *work = NULL;
    int n = 0, cap = 0;
    struct dirent *de;

    *total = 0;
    *files = 0;

    /* Pass one, single-threaded: list the directory and stat each child
     * directly. Cheap - one call per entry - and it is what tells a worker
     * whether it has a subtree to walk or a single file to record. */
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
            Entry *ge = realloc(entries, (size_t)cap * sizeof(Entry));
            Work  *gw = realloc(work, (size_t)cap * sizeof(Work));
            if (!ge || !gw) {
                free(child);
                break;
            }
            entries = ge;
            work = gw;
        }

        entries[n].name   = strdup(de->d_name);
        entries[n].is_dir = S_ISDIR(st.st_mode);
        entries[n].sym    = '#';
        entries[n].color  = (RGB){ 128, 128, 128 };

        work[n].path     = child;
        work[n].is_dir   = S_ISDIR(st.st_mode);
        work[n].is_link  = S_ISLNK(st.st_mode);
        work[n].own_size = (long long)st.st_size;
        n++;
    }
    closedir(dir);

    if (n == 0) {
        *count = 0;
        free(work);
        return entries;
    }

    /* Pass two, in parallel: size every entry. Each worker owns disjoint
     * indices, so no entry is ever touched by two threads. */
    WorkQueue q = { work, n, 0, PTHREAD_MUTEX_INITIALIZER };
    int nthreads = cpu_count();
    if (nthreads > n)
        nthreads = n;

    pthread_t pool[MAX_WORKERS];
    for (int i = 0; i < nthreads; i++)
        pthread_create(&pool[i], NULL, worker, &q);
    for (int i = 0; i < nthreads; i++)
        pthread_join(pool[i], NULL);
    pthread_mutex_destroy(&q.lock);

    for (int i = 0; i < n; i++) {
        entries[i].size  = work[i].size;
        entries[i].files = work[i].files;
        *total += work[i].size;
        *files += work[i].files;
        free(work[i].path);
    }

    free(work);
    *count = n;
    return entries;
}
