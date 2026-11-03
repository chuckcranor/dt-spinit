/*
 * app.c  dthread spinlock microbenchmark main app code
 * 28-Oct-2026  chuck@ece.cmu.edu
 */
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dt-spinit.h"

/*
 * worker structure (for array local to main app thread)
 */
struct worker {
    int active;          /* is worker active? */
    dthread_t dth;       /* handle to dthread (if active) */
};

/*
 * worker thread arg (passing arg from main app thread to new worker)
 */
struct workerarg {
    struct gstate *g;    /* global state */
    int workerno;        /* my worker number */
};

/*
 * selection policy struct
 */
struct polly {
    char *pname;
    int (*nextlock)(int workerno, int iteration, int prevlock,
                    struct gstate *g);
};

int seq_polly(int workerno, int iteration, int prevlock, struct gstate *g);
int random_polly(int workerno, int iteration, int prevlock, struct gstate *g);

struct polly parray[] = {
    { "seq", seq_polly },
    { "random", random_polly },
    { NULL, NULL }
};

static int get_polly(char *name) {
    int lcv;
    for (lcv = 0 ; parray[lcv].pname != NULL ; lcv++) {
        if (strcmp(parray[lcv].pname, name) == 0)
            return(lcv);
    }
    return(-1);
}

static void pollyhelp() {
    printf("pollyhelp:\n");
    printf("\t'-p random' -- workers randomly select a lock\n");
    printf("\t'-p seq' -- workers start with lock 'workerno %% nlocks' and\n");
    printf("\t            sequentially move forward by one lock each\n");
    printf("\t            iteration\n");
}

static void usage() {
    printf("usage: mpirun [mpi-flags] dt-spinit [dt-spinit-flags]\n");
    printf("where dt-spinit-flags are:\n");
    printf("\t-b           user barrier to sync startup\n");
    printf("\t-d datasize  datasize in 'b' bytes or 's' slots (def=b)\n");
    printf("\t-i niter     iterations per time check (time-based duration)\n");
    printf("\t-n nlocks    #locks in 'l' locks, 'b' bytes (def=l)\n");
    printf("\t             use '1M' to max to remaining malloc size\n");
    printf("\t-p policy    policy for selecting next spinlock\n");
    printf("\t             use '-p help' to list options\n");
    printf("\t-r rundurat  run duration in 'i' iterations, or\n");
    printf("\t             'H' 'M' 'S' for time-based duration.\n");
    printf("\t-s shmsrc    shared memory source to use\n");
    printf("\t-t max       max number of dthread to allocate for\n");
    printf("\t-w wcnt      number of worker dthreads to use\n");
    printf("\n");
    printf("shmsrc format: [ type: ] info [ :size-in-mb ]\n");
    printf("  type 'psh:' pthread local shared memory, info is filename\n");
}

/*
 * main app thread
 */
int app_main(int argc, char **argv) {
    dthread_shmref_t mal, gsref;
    int rv, ch, slotsize, type, maxlocks, maxth, gotpolly, lcv;
    struct gstate *gs;
    char *sflag;
    uint64_t got;
    struct worker *workers = NULL;
    struct workerarg warg;
    dthread_argret_t argret;
    struct timespec ts, te;
    double t1, t0, tdelta;

    /* init random number generator */
    srandom(time(NULL) ^ getpid());

    printf("dt-spinit: app_main dthread now running!\n\n");

    /* setup malloc for all dthreads and alloc gstate */
    rv = dthread_shm_new_arena(0, "break", 0, &mal);
    if (rv) {
        printf("dthead_shm_new_arena: unexpected fail: %s\n", strerror(rv));
        return(0);
    }
    rv = dthread_shm_set_defaultarena(&mal);
    if (rv) {
        printf("dthread_shm_set_defaultarena: fail: %s\n", strerror(rv));
        return(0);
    }
    gs = dthread_shm_malloc(NULL, sizeof(*gs), &gsref);
    if (!gs) {
        printf("error: global state malloc failed!\n");
        return(0);
    }
    memset(gs, 0, sizeof(*gs));
    gs->gmagic = GMAGIC;
    printf("dt-spinit: break malloc setup and global state allocated.\n");
    
    maxlocks = 0;
    maxth = DEF_MAXTH;
    gotpolly = 0;
    slotsize = (uintptr_t) &gs->sa[1] - (uintptr_t) &gs->sa[0];
    sflag = NULL;
    while ((ch = getopt(argc, argv, "bd:i:n:p:r:s:t:w:")) != -1) {
        switch (ch) {
        case 'b':
            gs->usebarrier = 1;
            break;
        case 'd':
            rv = atou64tsize(optarg, 1, NULL, "bs", &got, &type);
            if (rv < 0) {
                printf("error: bad -d val: %s\n", optarg);
                return(0);
            }
            if (type == 'b') {    /* round up, convert to slots */ 
                got = ((got + (slotsize - 1)) / slotsize) * slotsize;
                got = got / slotsize;
            }
            if (got * slotsize > mal.dt_length) {
                printf("error: bad -d val size: %s\n", optarg);
                return(0);
            }
            gs->dataslots = got;
            break;
        case 'i':
            gs->i_per_timecheck = atoi(optarg);
            if (gs->i_per_timecheck < 1) {
                printf("error: bad -i val size: %s\n", optarg);
                return(0);
            }
            break;
        case 'n':
            rv = atou64tsize(optarg, 1, NULL, "lbM", &got, &type);
            if (rv < 0 || (type == 'M' && got != 1)) {
                printf("error: bad -n val: %s\n", optarg);
                return(0);
            }
            if (type == 'b') {
                got = ((got + (slotsize - 1)) / slotsize) * slotsize;
                got = got / slotsize;
            }
            if (type == 'M') {
                maxlocks = 1;
                gs->nlocks = 0;
            } else {
                maxlocks = 0;
                if (got * slotsize > mal.dt_length) {
                    printf("error: bad -n val size: %s\n", optarg);
                    return(0);
                }
                gs->nlocks = got;
            }
            break;
        case 'p':
            if (strcmp(optarg, "help") == 0) {
                pollyhelp();
                return(0);
            }
            gs->pollyidx = get_polly(optarg);
            if (gs->pollyidx == -1) {
                printf("error: bad -p val: %s\n", optarg);
                return(0);
            }
            gotpolly++;
            break;
        case 'r':
            rv = atou64tsize(optarg, 0, NULL, "iHMS", &got, &type);
            if (rv < 0) {
                printf("error: bad -r val: %s\n", optarg);
                return(0);
            }
            gs->duration = got;
            if (type == 'i') {
                gs->timeduration = 0;
            } else {
                gs->timeduration = 1;
                if (type == 'M')
                    gs->duration *= 60;
                else if (type == 'H') 
                    gs->duration *= (60*60);
            }
            break;
        case 's':
            /* already handled by main(), but get a copy for checking */
            sflag = optarg;
            break;
        case 't':
            /* already handled by main(), but get a copy for checking */
            maxth = atoi(optarg);
            break;
        case 'w':
            gs->nworkers = atoi(optarg);
            break;
        case '?':
        default:
            usage();
            return(0);
        }
    }
    argc -= optind;
    argv += optind;

    /* sanity check args */
    if (sizeof(warg) > DTHREAD_INLINE_SIZE) {
        printf("error: worker arg too big to inline?!\n");
        return(0);
    }
    if (maxth < 2) {
        printf("error: maxth too small (%d)\n", maxth);
        return(0);
    }
    if (gs->nworkers == 0) {
        gs->nworkers = maxth - 1;
    } else if (gs->nworkers + 1 > maxth) {
        printf("error: not enough threads for %d workers\n", gs->nworkers);
        return(0);
    }
    if (gs->duration < 1) {
        printf("error: invalid duration %d\n", gs->duration);
        return(0);
    }
    if (gs->timeduration && gs->i_per_timecheck == 0) {
        gs->i_per_timecheck = 1;
    }
    if (gs->nlocks < 1) {
        printf("error: must have at least 1 lock (got=%d)\n", gs->nlocks);
        return(0);
    }
    if (!gotpolly) {
        printf("error: -p arg required\n");
        return(0);
    }

    /* finish dynamic allocations */
    gs->wst = dthread_shm_malloc(NULL, sizeof(gs->wst[0]) * gs->nworkers, NULL);
    if (!gs->wst) {
        printf("error: global wst state malloc failed!\n");
        return(0);
    }
    memset(gs->wst, 0, sizeof(gs->wst[0]) * gs->nworkers);

    if (maxlocks) {
        got = mal.dt_length;   /* use up most of arena */
        got = got - 2 * (sizeof(*gs) + sizeof(gs->wst[0]) * gs->nworkers);

        gs->nlocks = got / (slotsize + (gs->dataslots * slotsize));
    }

    /* size and allocate lock array */
    gs->nsa = gs->nlocks + (gs->nlocks * gs->dataslots);
    gs->sa = dthread_shm_malloc(NULL, gs->nsa * slotsize, NULL);
    memset(gs->sa, 0, gs->nsa * slotsize);
    for (lcv = 0 ; lcv < gs->nlocks ; lcv++) {
        if (dthread_spin_init(&gs->sa[lcv*(1+gs->dataslots)],
            PTHREAD_PROCESS_SHARED)) {
            printf("error: dthread_spin_init fails?!\n");
            return(0);
        }
    }

    /* local worker array */
    workers = malloc(sizeof(workers[0]) * gs->nworkers);
    if (!workers) {
        printf("error: local workers array malloc failed!\n");
        return(0);
    }
    memset(workers, 0, sizeof(workers[0]) * gs->nworkers);

    if (gs->usebarrier) {
        pthread_barrierattr_t bat;
        if (pthread_barrierattr_init(&bat) ||
            pthread_barrierattr_setpshared(&bat, PTHREAD_PROCESS_SHARED)) {
            printf("error: barrierattr init failed?!\n");
            return(0);
        }
        if (dthread_barrier_init(&gs->bar, &bat, gs->nworkers+1)) {
            printf("error: dthread_barrier_init init failed?!\n");
            return(0);
        }
        pthread_barrierattr_destroy(&bat);  /* ignore errors */
    }

    printf("dt-spinit config:\n");
    if (sflag)
        printf("\tshmsrc = %s\n", sflag);
    else
        printf("\tshmsrc = %s:%d (default)\n", DEF_SHMSPEC, DEF_SHMMB);
    printf("\tslotsize = %d bytes for 1 lock\n", slotsize);
    printf("\tdatasize = %d bytes per lock\n", gs->dataslots * slotsize);
    printf("\tnlocks = %d lock%s\n", gs->nlocks, (gs->nlocks == 1) ? "" : "s");
    printf("\ttotal-slots = %d\n", gs->nsa);
    printf("\tmaxthreads = %d\n", maxth);
    printf("\tnworkers = %d\n", gs->nworkers);
    printf("\tbarrier = %d (for startup sync)\n", gs->usebarrier);
    if (gs->timeduration) {
        printf("\tduration = %d seconds\n", gs->duration);
        printf("\ttime-check = %d iteration%s\n", gs->i_per_timecheck,
               (gs->i_per_timecheck == 1) ? "" : "s");
    } else {
        printf("\tduration = %d iterations (per worker)\n", gs->duration);
    }
    printf("\tpolly = %s\n", parray[gs->pollyidx].pname);
    printf("\n");

    printf("app main launching worker threads\n");
    warg.g = gs;   /* umap pointer, so no need to translate */
    argret.dt_argret_type = DTHREAD_INLINE;
    argret.dt_inlinelen = sizeof(warg);
    for (lcv = 0 ; lcv < gs->nworkers ; lcv++) {
        warg.workerno = lcv;
        memcpy(&argret.u.dt_inline[0], &warg, sizeof(warg));
        rv = dthread_ncreate(&workers[lcv].dth, NULL, spinit, &argret);
        if (rv) {
            printf("app main failed to create thread %d (%s)\n", lcv,
                   strerror(rv));
            return(0);
        }
        workers[lcv].active = 1;
    }

    if (gs->usebarrier) {
        printf("app main thread blocking at barrier\n");
        got = dthread_barrier_wait(&gs->bar);
        if (got != 0 && got != PTHREAD_BARRIER_SERIAL_THREAD) {
            printf("error: dthread_barrier_wait failed\n");
            return(0);
        }
        printf("app main thread past the barrier\n");
    }

    printf("app main waiting for workers to complete\n");
    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0)
        err(1, "clock_gettime");   /* fatal */
    for (lcv = 0 ; lcv < gs->nworkers ; lcv++) {
        if (workers[lcv].active) {
            /* thread results are in gs->wst[lcv] */
            rv = dthread_njoin(workers[lcv].dth, &argret);
            if (rv) {
                /* just print it, not much else to do */
                printf("app main: %d njoin error %s\n", lcv, strerror(rv));
            }
            workers[lcv].active = 0;
        }
    }
    if (clock_gettime(CLOCK_MONOTONIC, &te) < 0)
        err(1, "clock_gettime");   /* fatal */
    printf("app main all workers done!\n\n");

    for (lcv = 0, got = 0 ; lcv < gs->nworkers ; lcv++) {
        got += gs->wst[lcv].lockcnt;
    }
    t0 = ts.tv_sec + (ts.tv_nsec / 1000000000.0);
    t1 = te.tv_sec + (te.tv_nsec / 1000000000.0);
    tdelta = t1 - t0;
    printf("runtime: %lf secs\n", tdelta);
    printf("total-locks: %" PRIu64 " locks\n", got);
    printf("average-lock-rate: %lf locks/sec\n", got / tdelta);
    printf("\n");

    printf("app main done.\n");
    return(0);
}


/*
 * worker thread
 */
dthread_argret_t spinit(dthread_argret_t *dt_arg) {
    struct workerarg wa;
    struct wstate *wst;
    int rv, check, prev, pick, lockidx;
    struct timespec tstop, tnow;
    uint64_t iter;
    int (*pollyfn)(int workerno, int iteration, int prevlock,
                    struct gstate *g);
    dthread_argret_t ret;

    if (dt_arg->dt_argret_type != DTHREAD_INLINE ||
        dt_arg->dt_inlinelen != sizeof(wa)) {
        printf("worker error: bad arg\n");
        exit(1);   /* fatal error, should not happen, kill job */
    }

    memcpy(&wa, &dt_arg->u.dt_inline[0], sizeof(wa));
    if (wa.g->gmagic != GMAGIC) {
        printf("worked error: worker %d got bad magic\n", wa.workerno);
        exit(1);   /* fatal error, should not happen, kill job */
    }
    if (wa.workerno < 0 || wa.workerno >= wa.g->nworkers) {
        printf("worked error: workerid error!\n");
        exit(1);   /* fatal error, should not happen, kill job */
    }
    wst = &wa.g->wst[wa.workerno];

    if (wa.g->usebarrier) {
        rv = dthread_barrier_wait(&wa.g->bar);
        if (rv != 0 && rv != PTHREAD_BARRIER_SERIAL_THREAD) {
            printf("worked error: %d barrier error %s\n",
                   wa.workerno, strerror(rv));
            exit(1);   /* fatal error, should not happen, kill job */
        }
    }

    /* setup loop state */
    iter = 0;
    pollyfn = parray[wa.g->pollyidx].nextlock;
    prev = -1;

    if (clock_gettime(CLOCK_MONOTONIC, &wst->start) < 0)
        err(1, "clock_gettime: start");   /* fatal */
    if (wa.g->timeduration) {
        tstop.tv_sec = wst->start.tv_sec + wa.g->duration;
        tstop.tv_nsec = wst->start.tv_nsec;
        check = wa.g->i_per_timecheck;
    }

    /* main loop */
    while (1) {

        /* pick the next lock using the polly function */
        pick = (*pollyfn)(wa.workerno, iter, prev, wa.g);
        if (pick < 0 || pick >= wa.g->nlocks) {
            printf("ERROR: back pick %d from polly func, ABORT!\n", pick);
            break;
        }

        /* use pick to get lock index in sa[] */
        lockidx = pick * (1 + wa.g->dataslots);  /* account for dataslots */

        /* lock it, unlock it */
        dthread_spin_lock(&wa.g->sa[lockidx]);
        /* no-op */
        dthread_spin_unlock(&wa.g->sa[lockidx]);

        /* advance */
        iter++;
        prev = pick;
        if (wa.g->timeduration) {
            check--;
            if (check > 0)
                continue;
            check = wa.g->i_per_timecheck;   /* reset */
            if (clock_gettime(CLOCK_MONOTONIC, &tnow) < 0)
                err(1, "clock_gettime: now");   /* fatal */
            if (tnow.tv_sec > tstop.tv_sec)
                break;
            if (tnow.tv_sec == tstop.tv_sec && tnow.tv_nsec > tstop.tv_nsec)
                break;
            /* otherwise we keep going */
        } else {
            if (iter >= wa.g->duration)
                break;
        }
    }

    if (clock_gettime(CLOCK_MONOTONIC, &wst->end) < 0)
        err(1, "clock_gettime: end");   /* fatal */
    wst->lockcnt = iter;

    ret.dt_argret_type = DTHREAD_NODATA;
    return(ret);
}

/*
 * polly functions
 */

/* seq: workers walk through locks in sequence, starting at diff places */
int seq_polly(int workerno, int iteration, int prevlock, struct gstate *g) {
    if (iteration == 0) {
        return(workerno % g->nlocks);
    }
    return((prevlock + 1) % g->nlocks);
}

/* random: worker picks a random lock */
int random_polly(int workerno, int iteration, int prevlock, struct gstate *g) {
    return(random() % g->nlocks);
}
