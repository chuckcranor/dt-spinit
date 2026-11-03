/*
 * dt-spinit.h  dthread spinlock microbenchmark common header file
 * 28-Oct-2026  chuck@ece.cmu.edu
 */

#include <inttypes.h>
#include <time.h>

#include <dthread/dthread.h>

/*
 * general defines
 */
#define PREFIXLEN 4                /* length of shmsrc prefix ('xxx:' => 4) */
#define DEF_MAXTH 10               /* default max threads == 10 */
#define DEF_SHMMB 4                /* default MB of memory in shm */
#define DEF_SHMSPEC "/tmp/dt.shm"  /* default shm spec, use psh: */
#define GMAGIC    0xcafef00d       /* magic number for gstate */

/*
 * structs
 */

/* worker state */
struct wstate {
    uint64_t lockcnt;              /* number of locks completed */
    struct timespec start;         /* time worker started locking */
    struct timespec end;           /* time worker finished lock */
};

/* global state */
/* XXX: may need to cvt some from int to u64 for larger runs? */
struct gstate {
    int gmagic;                    /* magic number to check mapping */
    dthread_spinlock_t *sa;        /* spin lock arrary */
    int nsa;                       /* #entries used to malloc sa[] array */
    int nlocks;                    /* total #slots being used as a lock */
    int dataslots;                 /* #data slots per-lock */
    int usebarrier;                /* use barrier to sync startup */
    dthread_barrier_t bar;         /* barrier for usebarrier */
    struct wstate *wst;            /* array of worker state info */
    int nworkers;                  /* #of worker dthreads */
    int duration;                  /* run duration */
    int timeduration;              /* duration is seconds (not iterations) */
    int i_per_timecheck;           /* iterations per time check */
    int pollyidx;                  /* polly function index */

};

/* app functions prototypes */

int app_main(int argc, char **argv);
dthread_argret_t spinit(dthread_argret_t *dt_arg);


/* util functions prototypes */

int atou64tsize(char *nptr, int use_xib, char *sizes, char *types,
                uint64_t *rv, int *typep);
int loadsrc(int mpirank, char *spec, dthread_shmsrc_t *src, int *sopid);
