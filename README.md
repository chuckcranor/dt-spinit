# dt-spinit

dt-spinit is a spinlock microbenchmark that uses dthread-based threading.

## building dt-spinit

To compile you need:

  - an installed dthread library (  https://github.com/pnfs-storage/dthread )

  - the MPI platform that the dthread library was built with

### building with cmake

Choose a prefix to build and optionally install in by setting
CMAKE_INSTALL_PREFIX.  If the dthread library is not in that prefix or
in a standard system path then also set CMAKE_PREFIX_PATH to point to
the dthread install prefix.  Run cmake to configure, "make" to build,
and optionally "make install" to install.

Example: the dthread library is installed in prefix /tmp/dt and we
want to build and install dt-spinit there as well.  Set your directory
to the dt-spinit source directory, make sure MPI is in your path and
do:

```sh
mkdir -p build
cd build
cmake -DCMAKE_INSTALL_PREFIX=/tmp/dt ..
make
make install
```

The build should look like this:
```sh
% ls
CMakeLists.txt  README.md  app.c  dt-spinit.c  dt-spinit.h  util.c
% mkdir -p build
% cd build
% cmake -DCMAKE_INSTALL_PREFIX=/tmp/dt ..
-- The C compiler identification is GNU 11.5.0
-- Detecting C compiler ABI info
-- Detecting C compiler ABI info - done
-- Check for working C compiler: /usr/bin/cc - skipped
-- Detecting C compile features
-- Detecting C compile features - done
-- Performing Test CMAKE_HAVE_LIBC_PTHREAD
-- Performing Test CMAKE_HAVE_LIBC_PTHREAD - Success
-- Found Threads: TRUE
-- Found MPI_C: /share/r9-ucx/ompi/lib/libmpi.so (found version "3.1")
-- Found MPI: TRUE (found version "3.1")
-- Configuring done (0.8s)
-- Generating done (0.0s)
-- Build files have been written to: /users/chuck/src/dt-spinit/build
% make
[ 25%] Building C object CMakeFiles/dt-spinit.dir/app.c.o
[ 50%] Building C object CMakeFiles/dt-spinit.dir/dt-spinit.c.o
[ 75%] Building C object CMakeFiles/dt-spinit.dir/util.c.o
[100%] Linking C executable dt-spinit
[100%] Built target dt-spinit
% make install
[100%] Built target dt-spinit
Install the project...
-- Install configuration: ""
-- Installing: /tmp/dt/bin/dt-spinit
-- Set runtime path of "/tmp/dt/bin/dt-spinit" to "/tmp/dt/lib:/share/r9-ucx/ompi/lib"
%
```

### building without cmake using mpicc

Alternately, assuming that the dthread library is installed in
/tmp/dt, the dt-spinit can be built without cmake using mpicc like
this:

```sh
mpicc -O -o dt-spinit app.c dt-spinit.c util.c \
    -I/tmp/dt/include -L/tmp/dt/lib -Wl,-rpath=/tmp/dt/lib -ldthread
```

## dt-spinit operation

The dt-spinit microbenchmark:

 - sets up dthread shared memory config

 - configures a set of one or more spin locks as specified on the
   command line

 - launches a set of worker threads that loop locking and unlocking
   spinlocks in a tight loop for the duration specified on the command
   line

 - when all the worker threads complete the application reports the
   overall number of spin lock/unlock ops per second

Like all dthread-based applications, the microbenchmark is intended to
be run under MPI.

### command line usage

```
usage: mpirun [mpi-flags] dt-spinit [dt-spinit-flags]
where dt-spinit-flags are:
	-b           user barrier to sync startup
	-d datasize  datasize in 'b' bytes or 's' slots (def=b)
	-i niter     iterations per time check (time-based duration)
	-n nlocks    #locks in 'l' locks, 'b' bytes (def=l)
	             use '1M' to max to remaining malloc size
	-p policy    policy for selecting next spinlock
	             use '-p help' to list options
	-r rundurat  run duration in 'i' iterations, or
	             'H' 'M' 'S' for time-based duration.
	-s shmsrc    shared memory source to use
	-t max       max number of dthread to allocate for
	-w wcnt      number of worker dthreads to use

shmsrc format: [ type: ] info [ :size-in-mb ]
  type 'psh:' pthread local shared memory, info is filename
```

The "-s" flag is used to configure dthread shared memory.  "psh:" or a
plain filename will use POSIX shared memory on the local system.  For
example, "-s /tmp/shm:16" or "-s psh:/tmp/shm:16" creates a 16MB posix
shared memory file in /tmp/shm.

The spinlocks are stored in a single array in shared memory.  The
number of spinlocks in the array that are used for lock/unlock
operations are set with the "-n" flag.  This can be specified by
number of spinlocks or by number of bytes of shared memory to use for
spinlocks.  Examples: "-n 1k" or "-n 1kl" means 1024 spinlocks, "-n
1mb" means as many spinlocks that can fit in 1MiB of memory.  You can
use "-n 1M" if you want to fill the remaining free memory managed by
malloc with as many spinlocks that can fit.

If the "-d" datasize is non-zero, then we insert unused space between
each allocated spinlock (to emulate space used to hold the "data"
being locked).  The data space is sized in sizeof(spinlock) sized
chunk slots in order to preserve alignment in the array.  Examples:
"-d 1s" is 1 slot, "-d 1kb" is 1024 bytes rounded up to be a multiple
of the spinlock size.

The "-b" flag causes the app to use a dthread barrier to synchronize
the start of the worker threads (without it they immediately start
their lock after being launched).

The duration of the benchmark run is specified with the "-r" flag.  It
can be specified in number of lock/unlock iterations per worker or
worker run time.  Examples by iteration count: "-r 10" "-r 1k" "-r
1ki" "-r 10mi" for 10, 1000, 1000, or 1000000 iterations).  Examples
by run time: "-r 30S" "-r 5M" "-r 2H" for 30 seconds, 5 minutes, or 2
hours.

For time-based duration the workers must periodically poll
clock_gettime() to see if they have hit the time limit.  The "-i" flag
sets the number of lock/unlock iterations the worker makes between
calls to clock_gettime() -- the default is 1.

The number of threads used to init the dthread library is set with
"-t" (used with the dthread_run() API).  The number of worker threads
the app launches is set with the "-w" flag.  The "-w" number of
workers must be less than the "-t" minus one value (the main thread is
reserved to control the overall flow of the benchmark).

The "-p" flag controls how workers choose which lock in the array to
lock/unlock.  Currently the possible values are:

  - '-p random' -- workers randomly select a lock
  
  - '- seq' -- workers start with lock 'workerno % nlocks' and
	            sequentially move forward by one lock each iteration

### example run

Below we run a dthread MPI job with 4 ranks (to host running
dthreads).  We configure the benchmark to allocate 2 spinlocks for 8
worker threads running the random lock selection policy.  We set the
run duration to 20 seconds.  This run uses the default shared memory
configuration (4MiB of posix shared memory from /tmp/dt.shm).  Note
that dt-spinit dumps it configuration to stdout as part of its
startup.

```sh
% mpirun -n 4 ./dt-spinit -n 2 -w 8 -p random -r 20S 
dt-spinit: app_main dthread now running!

dt-spinit: break malloc setup and global state allocated.
dt-spinit config:
	shmsrc = /tmp/dt.shm:4 (default)
	slotsize = 4 bytes for 1 lock
	datasize = 0 bytes per lock
	nlocks = 2 locks
	total-slots = 2
	maxthreads = 10
	nworkers = 8
	barrier = 0 (for startup sync)
	duration = 20 seconds
	time-check = 1 iteration
	polly = random

app main launching worker threads
app main waiting for workers to complete
app main all workers done!

runtime: 20.019206 secs
total-locks: 184109577 locks
average-lock-rate: 9196647.212670 locks/sec

app main done.
%
```
