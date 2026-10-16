/*
 * Directory sizing: plain files by their own size, directories by summing
 * their contents recursively the way du(1) does. Symlinks count as the link
 * and are never followed, so a symlink loop cannot trap us and a link to a
 * huge tree is not billed to this directory.
 *
 * The walk is parallel at directory granularity, not at top-level-entry
 * granularity: every directory found becomes a job on a shared stack that
 * any idle worker can take. One enormous subtree therefore spreads across
 * all the cores instead of pinning the one thread that happened to draw it.
 *
 * Inside a directory, children are stat'ed relative to the open directory
 * (fstatat) rather than by full path, so the kernel does not re-walk the
 * whole path for every file, and readdir's d_type saves the stat entirely
 * for subdirectories. A path string is built once per directory rather than
 * once per file.
 */

#define _DEFAULT_SOURCE

#include "diskpi.h"

#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_WORKERS 32

typedef struct {
    char *path;      /* owned; freed once the job is done with */
    int   owner;     /* index of the top-level entry it counts towards */
} Job;

typedef struct {
    Job   *items;
    int    n, cap;

    int    active;           /* workers currently inside a directory */
    bool   stop;

    _Atomic long long *size;    /* per top-level entry */
    _Atomic long long *files;

    pthread_mutex_t lock;
    pthread_cond_t  wake;
} Pool;

/* ------------------------------------------------------------------ queue */

/* Caller holds the lock. */
static bool push_job(Pool *p, char *path, int owner)
{
    if (p->n == p->cap) {
        int cap = p->cap ? p->cap * 2 : 256;
        Job *grown = realloc(p->items, (size_t)cap * sizeof(Job));
        if (!grown)
            return false;
        p->items = grown;
        p->cap = cap;
    }
    p->items[p->n].path = path;
    p->items[p->n].owner = owner;
    p->n++;
    return true;
}

/* Depth first: taking the newest job keeps the stack short and the pages
 * we just read still warm. */
static bool pop_job(Pool *p, Job *out)
{
    pthread_mutex_lock(&p->lock);

    for (;;) {
        if (p->n > 0) {
            *out = p->items[--p->n];
            p->active++;
            pthread_mutex_unlock(&p->lock);
            return true;
        }
        if (p->active == 0 || p->stop) {
            /* Nothing queued and nobody still working: the walk is over. */
            p->stop = true;
            pthread_cond_broadcast(&p->wake);
            pthread_mutex_unlock(&p->lock);
            return false;
        }
        pthread_cond_wait(&p->wake, &p->lock);
    }
}

static void finish_job(Pool *p)
{
    pthread_mutex_lock(&p->lock);
    p->active--;
    if (p->n == 0 && p->active == 0) {
        p->stop = true;
        pthread_cond_broadcast(&p->wake);
    }
    pthread_mutex_unlock(&p->lock);
}

/* ----------------------------------------------------------------- worker */

static char *join_path(const char *dir, const char *name)
{
    size_t dl = strlen(dir), nl = strlen(name);
    char *out = malloc(dl + nl + 2);
    if (!out)
        return NULL;
    memcpy(out, dir, dl);
    out[dl] = '/';
    memcpy(out + dl + 1, name, nl);
    out[dl + nl + 1] = '\0';
    return out;
}

/* One directory: sum the files in it, queue the directories under it. */
static void walk_dir(Pool *p, const Job *job)
{
    DIR *dir = opendir(job->path);
    if (!dir)
        return;                 /* unreadable: treat as empty, keep going */

    int dfd = dirfd(dir);
    long long size = 0, files = 0;
    struct dirent *de;

    while ((de = readdir(dir)) != NULL) {
        const char *name = de->d_name;
        if (name[0] == '.' && (name[1] == '\0' ||
                              (name[1] == '.' && name[2] == '\0')))
            continue;

        /* d_type answers "is it a directory?" without a stat on every
         * filesystem that fills it in; only the size of a real file needs
         * one, and DT_UNKNOWN falls back to asking. */
        bool is_dir;
        struct stat st;

        if (de->d_type == DT_DIR) {
            is_dir = true;
        } else if (de->d_type == DT_REG || de->d_type == DT_LNK) {
            if (fstatat(dfd, name, &st, AT_SYMLINK_NOFOLLOW) != 0)
                continue;
            is_dir = false;
        } else {
            if (fstatat(dfd, name, &st, AT_SYMLINK_NOFOLLOW) != 0)
                continue;
            is_dir = S_ISDIR(st.st_mode);
        }

        if (is_dir) {
            char *child = join_path(job->path, name);
            if (!child)
                continue;
            pthread_mutex_lock(&p->lock);
            bool ok = push_job(p, child, job->owner);
            if (ok)
                pthread_cond_signal(&p->wake);
            pthread_mutex_unlock(&p->lock);
            if (!ok)
                free(child);
            continue;
        }

        files++;
        if (!S_ISLNK(st.st_mode))
            size += (long long)st.st_size;
    }

    closedir(dir);

    /* One atomic pair per directory, not per file. */
    if (size)
        atomic_fetch_add_explicit(&p->size[job->owner], size,
                                  memory_order_relaxed);
    if (files)
        atomic_fetch_add_explicit(&p->files[job->owner], files,
                                  memory_order_relaxed);
}

static void *worker(void *arg)
{
    Pool *p = arg;
    Job job;

    while (pop_job(p, &job)) {
        walk_dir(p, &job);
        free(job.path);
        finish_job(p);
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

/* ------------------------------------------------------------------ entry */

Entry *scan_directory(const char *path, int *count,
                      long long *total, long long *files)
{
    DIR *dir = opendir(path);
    if (!dir)
        return NULL;

    Entry *entries = NULL;
    int n = 0, cap = 0;
    struct dirent *de;
    int dfd = dirfd(dir);

    *total = 0;
    *files = 0;

    Pool p;
    memset(&p, 0, sizeof(p));
    pthread_mutex_init(&p.lock, NULL);
    pthread_cond_init(&p.wake, NULL);

    /* Pass one: the immediate listing. Plain files are finished here and
     * now; each directory becomes the seed job for its own subtree. */
    while ((de = readdir(dir)) != NULL) {
        const char *name = de->d_name;
        if (name[0] == '.' && (name[1] == '\0' ||
                              (name[1] == '.' && name[2] == '\0')))
            continue;

        struct stat st;
        if (fstatat(dfd, name, &st, AT_SYMLINK_NOFOLLOW) != 0)
            continue;

        if (n == cap) {
            cap = cap ? cap * 2 : 64;
            Entry *grown = realloc(entries, (size_t)cap * sizeof(Entry));
            if (!grown)
                break;
            entries = grown;
        }

        char *owned = strdup(name);
        if (!owned)
            continue;

        memset(&entries[n], 0, sizeof(entries[n]));
        entries[n].name   = owned;
        entries[n].is_dir = S_ISDIR(st.st_mode);
        entries[n].sym    = '#';
        entries[n].color  = (RGB){ 128, 128, 128 };

        if (!entries[n].is_dir) {
            entries[n].files = 1;
            entries[n].size = S_ISLNK(st.st_mode) ? 0 : (long long)st.st_size;
        }
        n++;
    }
    closedir(dir);

    if (n == 0) {
        *count = 0;
        pthread_mutex_destroy(&p.lock);
        pthread_cond_destroy(&p.wake);
        return entries;
    }

    p.size  = calloc((size_t)n, sizeof(*p.size));
    p.files = calloc((size_t)n, sizeof(*p.files));
    if (!p.size || !p.files) {
        free(p.size);
        free(p.files);
        pthread_mutex_destroy(&p.lock);
        pthread_cond_destroy(&p.wake);
        *count = n;
        return entries;
    }

    int seeded = 0;
    for (int i = 0; i < n; i++) {
        if (!entries[i].is_dir)
            continue;
        char *child = join_path(path, entries[i].name);
        if (child && push_job(&p, child, i))
            seeded++;
        else
            free(child);
    }

    if (seeded > 0) {
        /* Every core gets a worker regardless of how many seeds there are:
         * a single directory can expand into a million jobs, and a worker
         * with nothing to do yet costs one blocked thread. Sizing the pool
         * to the seed count would leave most of the machine idle on the
         * very case this exists for - one enormous subtree. */
        int nthreads = cpu_count();
        if (nthreads < 1)
            nthreads = 1;

        pthread_t pool[MAX_WORKERS];
        int made = 0;
        for (int i = 0; i < nthreads; i++)
            if (pthread_create(&pool[made], NULL, worker, &p) == 0)
                made++;

        if (made == 0)
            worker(&p);          /* no threads available: do it here */
        for (int i = 0; i < made; i++)
            pthread_join(pool[i], NULL);
    }

    for (int i = 0; i < n; i++) {
        if (entries[i].is_dir) {
            entries[i].size  = atomic_load(&p.size[i]);
            entries[i].files = atomic_load(&p.files[i]);
        }
        *total += entries[i].size;
        *files += entries[i].files;
    }

    free(p.items);
    free(p.size);
    free(p.files);
    pthread_mutex_destroy(&p.lock);
    pthread_cond_destroy(&p.wake);

    *count = n;
    return entries;
}
