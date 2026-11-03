/*
 * dt-spinit.c  dthread spinlock microbenchmark
 * 27-Oct-2026  chuck@ece.cmu.edu
 */

/*
 * the idea is to allocate an array of spinlocks in shared memory
 * and then fork off a number of dthreads to access the locals using
 * a pattern.
 *
 * we assume that the shared memory is mapped with DTHREAD_SRC_UMAP
 * so that virtual addresses match across ranks (and we can test
 * the umap option).
 */
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <mpi.h>   /* XXX to get rank */

#include "dt-spinit.h"

/*
 * dispatch table and shared memory source
 */
dthread_dispatch_t disptable[] = {
    { "app_main", { .start0 = app_main }, NULL, NULL },
    { "spinit", { .start = spinit }, NULL, NULL },
};

dthread_shmsrc_t shmsrc = { 0 };   /* filled out by loadsrc() */

/*
 * process-level main
 */
int main(int argc, char **argv) {
    int rv, mpirank, maxth, lcv, syncop_id;

    setlinebuf(stdout);

    /* we call dthread_init() as early as we can to init MPI */
    rv = dthread_init(&argc, &argv);
    if (rv != 0)
        errx(1, "dthread_init failed: %s (%d)", strerror(rv), rv);

    /* XXX: should dthread have an api for this? */
    if (MPI_Comm_rank(MPI_COMM_WORLD, &mpirank)) {
        warnx("MPI_Comm_rank failed");
        return(-1);
    }

    /*
     * we want to allow either main or app_main to process argc/argv.
     *
     * having main() process argc/argv allows it to use the args
     * to control how we call dthread_run(), but the processing
     * happens before dthreads is running (so you cannot do dthread ops).
     * instead we prescan for args main() handles and then let's
     * the app main do the rest.
     *
     * main flags:
     *   -s  shmsrc      set shmsrc
     *   -t  maxthreads  set max number of threads
     */
    maxth = DEF_MAXTH;    /* default max threads */
    for (lcv = 0 ; lcv < argc - 1 && rv == 0 ; lcv++) {
        if (strcmp(argv[lcv], "-s") == 0)
            rv = loadsrc(mpirank, argv[lcv+1], &shmsrc, &syncop_id);
        else if (strcmp(argv[lcv], "-t") == 0)
            maxth = atoi(argv[lcv+1]);
    }
    if (rv == 0)    /* try default config... */
        rv = loadsrc(mpirank, DEF_SHMSPEC, &shmsrc, &syncop_id);

    if (rv < 0) {
        MPI_Finalize();  /* XXX */
        exit(1);
    }

    /* we can now run in dthread environment */
    dthread_run(disptable, sizeof(disptable)/sizeof(disptable[0]),
                &shmsrc, 1, NULL, 0, syncop_id, maxth, argc, argv);

    printf("main: dthread_run unexpectedly returned\n");
    exit(1);
}
