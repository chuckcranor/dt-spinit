/*
 * util.c  dt-spinit util functions
 * 28-Oct-2026  chuck@ece.cmu.edu
 */

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dt-spinit.h"

/*
 * atou64tsize(): ascii string with type/size to uint64_t and type.
 * can omit type or size as long if result is not ambiguous.
 */
int atou64tsize(char *nptr, int use_xib, char *sizes, char *types,
                uint64_t *rv, int *typep) {
    uint64_t ret, szmult;
    char *ep, *szhit, *typehit;
    int type;

    errno = 0;
    ret = strtoull(nptr, &ep, 0);
    if (errno == ERANGE)
        return(-1);
    if (nptr == ep) {               /* no number? */
        errno = EINVAL;
        return(-1);
    }

    if (sizes == NULL)     /* let us choose the default sizes */
        sizes = "kmg";
    if (types == NULL)
        types = "";        /* no types defined, types[0] is 0 due to null */

    /* parse and validate the suffix */
    if (!isalpha(ep[0])) {
        /* no suffix: default type, no multiplier */
        type = types[0];         
        szmult = 1;
    } else if (!isalpha(ep[1])) {
        /* only one: validate, reject if ambiguous */
        szhit = strchr(sizes, ep[0]);
        typehit = strchr(types, ep[0]);
        if ((szhit && typehit) || (!szhit && !typehit)) {
            errno = EINVAL;
            return(-1);        /* it was ambiguous or unknown */
        }
        if (szhit) {
            szmult = ep[0];    /* got size, take default type */
            type = types[0];
        } else {
            szmult = 1;        /* got type, no size */
            type = ep[0];
        }
    } else {
        szhit = strchr(sizes, ep[0]);
        typehit = strchr(types, ep[1]);
        if (!szhit || !typehit) {
            errno = EINVAL;
            return(-1);        /* bad type or size */
        }
        /* fully specified size and type */
        szmult = ep[0];
        type = ep[1];
    }

   /* compute size multipler */
    switch (szmult) {
    case 'k':
        szmult = (use_xib) ? 1024 : 1000;
        break;
    case 'm':
        szmult = (use_xib) ? 1024*1024 : 1000*1000;
        break;
    case 'g':
        szmult = (use_xib) ? 1024*1024*1024 : 1000*1000*1000;
        break;
    case 1:
        /* no multipler given, so keep at 1 as a no-op */
        break;
    default:               /* cannot happen, but just to be safe ... */
        errno = EINVAL;
        return(-1);
    }

    /* done! */
    *rv = ret * szmult;
    *typep = type;
    return(0);
}

/*
 * loadsrc: process shmsrc arg.  return 0 to ignore, 1 on success,
 * -1 on error.
 *
 * XXX: adjust to add new types of shared memory.
 */
int loadsrc(int mpirank, char *spec, dthread_shmsrc_t *src, int *sopid) {
    char *sdup = strdup(spec);   /* avoid overwriting spec */
    char *sp, *cp;
    int mb;

    if (!sdup)
        goto bad;

    /* DTHREAD_SYNCOP_ID_PSHARED */
    if (*sdup == '/' || strncmp(sdup, "psh:", PREFIXLEN) == 0) {
        sp = (*sdup == '/') ? sdup : sdup + PREFIXLEN;
        cp = strchr(sp, ':');
        if (cp) {
            *cp++ = 0;        /* remove mb suffix */
             mb = atoi(cp);
        } else {
             mb = DEF_SHMMB;  /* use the default */
        }
        if (mb < 0) {
            if (mpirank == 0) warnx("bad psh mb: %s", cp);
            goto bad;
        }
        src->dt_src = sp;
        src->dt_srcflags = DTHREAD_SRC_FILE|DTHREAD_SRC_UMAP;
        src->dt_mmoffset = 0;
        src->dt_mmsize = (uint64_t) mb * 1024*1024;
        src->dt_addr = NULL;
        src->dt_reserve_front = 0;
        src->dt_reserve_back = 0;
        *sopid = DTHREAD_SYNCOP_ID_PSHARED;
        return(1);
    }

bad:
    /* no match */
    if (sdup)
        free(sdup);
    return(-1);
}
