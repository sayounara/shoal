#ifndef __SHL_H
#define __SHL_H

#include <assert.h>

#include <cstdio>
#include <omp.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>

#include <sys/time.h>

#define BASE_UNIT 1024
#define KILO BASE_UNIT
#define MEGA (KILO*BASE_UNIT)
#define GIGA (MEGA*BASE_UNIT)
#define MAXCORES 100

// --------------------------------------------------
// Hardcoded page sizes
// --------------------------------------------------
#define PAGESIZE_HUGE (2*1024*1024)
#define PAGESIZE (4*1024)

// --------------------------------------------------
// Configuration
// --------------------------------------------------

// Whether or not to use replication (needs: indirection OR copy)
#define REPLICATION

// Use NUMA aware memory allocation for replication
#define NUMA

// This does not seem to be currently used
//#define LOOKUP

// Add some additional debug checks! This will harm performance a LOT.
//#define DEBUG

// Enable PAPI support
//#define PAPI

// Enable support for hugepages
//#define ENABLE_HUGEPAGE

// --------------------------------------------------
// Typedefs
// --------------------------------------------------
typedef uint32_t coreid_t;

// --------------------------------------------------
// in misc.c
// --------------------------------------------------
double shl__end_timer(void);
double shl__get_timer(void);
void shl__start_timer(void);
int numa_cpu_to_node(int);
coreid_t *parse_affinity(bool);
void shl__init_thread(void);
char* get_env_str(const char*, const char*);
int get_env_int(const char*, int);
void print_number(long long);

// --------------------------------------------------
// OS specific stuff
// --------------------------------------------------
void shl__bind_processor(int proc);
void shl__bind_processor_aff(int proc);
int shl__get_proc_for_node(int node);

// --------------------------------------------------
// SHOAL
// --------------------------------------------------
void shl__end(void);
void papi_stop(void);
void papi_init(void);
void papi_start(void);
int shl__get_rep_id(void);
void** shl__copy_array(void*, size_t, bool, bool, const char*);
void shl__copy_array(void**, void*, size_t, bool, bool, bool, const char*);
void shl__copy_back_array_single(void*, void*, size_t, bool, bool, bool, const char*);
void* shl__malloc(size_t, int, int*);
void** shl_malloc_replicated(size_t, int*, int);
void shl__repl_sync(void*, void**, size_t, size_t);
void shl__init_thread(int);
void handle_error(int);
int shl__get_num_replicas(void);
void shl__init(size_t,bool);
// array helpers
// --------------------------------------------------
void** shl__copy_array(void *src, size_t size, bool is_used,
                       bool is_ro, const char* array_name);
void shl__copy_back_array(void **src, void *dest, size_t size, bool is_copied,
                          bool is_ro, bool is_dynamic, const char* array_name);
void shl__copy_back_array_single(void *src, void *dest, size_t size, bool is_copied,
                                 bool is_ro, bool is_dynamic, const char* array_name);


// --------------------------------------------------
// Auto-tuning interface
//
// Non of these functions are implemented. These are just meant as
// ideas for a future potential auto-tuning interface.
// --------------------------------------------------

/**
 * \brief Find sensible thread placement.
 *
 * Right now, a simple heuristic to decide how many threads to use is:
 * - 1) barriers -> one thread per physical core
 * - 2) no barriers -> one thread per HW context
 *
 * The thread placement would then be 1) to bind the threads such that
 * one thread is running on every physical core (i.e. not to have
 * threads on two HW contexts mapped to the same physical core) and 2)
 * one thread per HW context.
 *
 * But obviously, this is more complicated. Ultimately, we probably
 * want to specify an upper limit for the number of threads (maybe
 * also cores) to use (default: unlimited). Maybe also a bitmask of
 * CPUs to run on.
 *
 * The system then needs to figure out how many threads to spawn, and
 * where to place them. E.g. we know from newRTS that it is _not_
 * always good to spread out to several nodes. Somethings it is better
 * to use up all H/W threads on a node before going to the next one,
 * even if some of the threads will be hyperthreads.
 */
void shl__auto_tune_bind(int *num_cores,
                         coreid_t *bind,
                         bool uses_barriers);

extern int replica_lookup[];

// --------------------------------------------------
// Timer
// --------------------------------------------------
class Timer {
 public:
    void start();
    double stop();
    double timer_secs = 0.0;
 private:
    struct timeval TV1, TV2;
};

// --------------------------------------------------
// Program configuration
// --------------------------------------------------
class Configuration {

 public:
    Configuration(void);


    ~Configuration(void) {

        delete node_mem_avail;
    }

    // Should large pages be used
    bool use_hugepage;

    // Should replication be used
    bool use_replication;

    // Should distribution be used
    bool use_distribution;

    // Should partitioning be used
    bool use_partition;

    // Number of NUMA nodes
    int num_nodes;

    // How much memory is available on the machine
    long mem_avail;

    // How much memory is available on each node
    long* node_mem_avail;
};
Configuration* get_conf(void);

// --------------------------------------------------
// Colors!
// --------------------------------------------------
#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"


// --------------------------------------------------
// Defines for memory allocation
// --------------------------------------------------
#define SHL_MALLOC_NONE        (0)
#define SHL_MALLOC_HUGEPAGE    (0x1<<0)
#define SHL_MALLOC_DISTRIBUTED (0x1<<1)
#define SHL_MALLOC_PARTITION   (0x1<<2)

// --------------------------------------------------
// Includes depending on configuration
// --------------------------------------------------
#include <sys/mman.h>
#include <stdint.h>

#ifdef DEBUG
static uint64_t num_lookup = 0;
#endif

#endif
