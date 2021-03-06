/*
 * Copyright (c) 2014 ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetsstrasse 6, CH-8092 Zurich. Attn: Systems Group.
 */

#ifndef __SHL_ARRAY
#define __SHL_ARRAY

#include <cstdlib>
#include <cstdarg>
#include <cstring> // memset
#include <iostream>
#include <limits>
#include <stdio.h>

#include "shl.h"
#include "shl_internal.h"
#include "shl_timer.hpp"
#include "shl_configuration.hpp"


///< Enables array access range checks
#define ENABLE_RANGE_CHECK 1


#if ENABLE_RANGE_CHECK
#define RANGE_CHECK(_i) assert(array && _i < size);
#else
#define RANGE_CHECK(_i)
#endif

typedef enum array_type {
    SHL_A_INVALID,
    SHL_A_SINGLE_NODE,
    SHL_A_DISTRIBUTED,
    SHL_A_PARTITIONED,
    SHL_A_REPLICATED,
    SHL_A_EXPANDABLE,
    SHL_A_WR_REPLICATED
} array_t;

///< Enables array access profiling
//#define PROFILE

// --------------------------------------------------
// Implementations
// --------------------------------------------------


/**
 * \brief Base class for shoal array
 */
class shl_base_array {
 public:
    const char *name;   ///< name of the array
    array_t type;

    /**
     * \brief base array constructor
     *
     * \param name  Name of the array.
     */
    shl_base_array(const char *_name, const array_t _type)
    {
        shl_base_array::type = _type;
        shl_base_array::name = _name;
    }
};

/*
 * ==============================================================================
 * Generic Array
 * ==============================================================================
 */

/**
 * \brief Generic Shoal Array
 *
 * This class implements single-node arrays.
 */
template<class T>
class shl_array : public shl_base_array {

 protected:
    /**
     * Size of array in elements (and not bytes)
     */
    size_t size;        ///< size of the array in elements

    int pagesize;

    /* ------------------------- DMA ------------------------- */
    size_t dma_total_tx; ///< total DMA transactions issued
    size_t dma_compl_tx; ///< completed dma transactions

    /* ------------------------- Flags ------------------------- */
    bool use_hugepage;  ///< flag indicating the use of huge pages
    bool use_largepage;  ///< flag indicating the use of large pages
    bool read_only;     ///< flag indicating that this is a read only array
    bool alloc_done;    ///< flag indicating the allocation is done

    /// We have several different array pointers
    /// (depending on the array implementation),
    /// so we keep an extra bool to simplify
    /// tracking of whether arrays have been
    /// allocated

    bool is_used;       ///< flag indicating that this array is used.

    /// Some arrays might be allocated, but are
    /// never used (such as part of the graph data
    /// structure for some of the Green Marl
    /// programs). If we know that arrays are not
    /// used, we don't have to copy them
    /// around. Alternatively, we could (and
    /// should) remove this from the library and
    /// deal with this in the Green Marl compiler

    bool is_dynamic;    ///< flag indicating the array is dynamic

    /// This is an array attribute I found useful
    /// to deal with GM programs. There are arrays
    /// that are allocated prior to executing the
    /// main routine, and others that are
    /// dynamically allocated within
    /// programs. Latter ones do not have to be
    /// copied back. We probably want to get rid
    /// of this flag and deal with this in GM.

    void *meminfo;      ///< backend specific memory information
    T* array;           ///< pointer to the backing memory region

    uint8_t dma_fraction;

#ifdef PROFILE
    int64_t num_wr;     ///< number of writes to that array
    int64_t num_rd;///< number of reads from this array
#endif

 public:

    /*
     * ---------------------------------------------------------------------------
     * Constructors / Destructors
     * ---------------------------------------------------------------------------
     */

    shl_array(size_t _size, const char *_name) :
        shl_base_array(_name, SHL_A_SINGLE_NODE) // << SK wrong type! Should be unitialized
    {
        init(_size, _name);
    }

    void init(size_t _size, const char *_name)
    {
        size = _size;
        use_hugepage = get_conf()->use_hugepage &&
            shl__get_array_conf(shl_base_array::name, SHL_ARR_FEAT_HUGEPAGE, true);

        use_largepage = get_conf()->use_largepage &&
            shl__get_array_conf(shl_base_array::name, SHL_ARR_FEAT_LARGEPAGE, true);

        read_only = false;
        alloc_done = false;
        is_dynamic = false;
        is_used = false;
        meminfo = NULL;
        array = NULL;
        pagesize = 0;
        dma_total_tx = 0;
        dma_compl_tx = 0;
        poll_count = 0;
        dma_fraction = 0;
#ifdef PROFILE
        num_wr = 0;
        num_rd = 0;
#endif
    }

    /**
     * \brief Generic array constructor.
     *
     * \param _size the element count of this array
     * \param _name name of the array for identification purposes
     *
     * This constructor does not allocate the backing memory for the array.
     */
    shl_array(size_t _size, const char *_name, array_t _type) :
                    shl_base_array(_name, _type)
    {
        init(_size, _name);
    }

    /**
     * \brief Specialized array constructor
     *
     * @param _size
     * @param _name
     * @param mem
     * @param data
     *
     * This constructor uses the supplied memory region for the array data.
     * The caller must ensure that the size of the memory region is large
     * enough to hold all the array elements.
     */
    shl_array(size_t _size, const char *_name, void *mem, void *data, array_t _type) :
                    shl_base_array(_name, _type)
    {
        init(_size, _name);
        array = (T*) data;
        meminfo = mem;
        alloc_done = true;
    }

    /**
     * \brief Array destructor
     */
    virtual ~shl_array(void)
    {
        // TODO: implementation
        // if (array!=NULL) {
        //     free(array);
        // }

        // array = NULL;
    }

    /*
     * ---------------------------------------------------------------------------
     * Array Options
     * ---------------------------------------------------------------------------
     */

    /**
     * \brief checks if the array is used and memory is being allocated
     *
     * \returns TRUE  iff the array is used by the program
     *          FALSE iff the array is not used by the program
     */
    virtual bool do_alloc(void)
    {
        return is_used;
    }

    /**
     * \brief checks whether data has to be copied into the array
     *
     *
     * \return TRUE  iff the array is used by the program
     *         FALSE iff the array is not used or only used internally
     */
    virtual bool do_copy_in(void)
    {
        return is_used && !is_dynamic;
    }

    /**
     *
     * \brief checks if data has to be copied back from the array
     *
     * \returns TRUE  iff a copy has to be done
     *          FALSE otherwise
     *
     * Read only data does NOT have to be copied back. Also,
     * dynamically allocated stuff and arrays that are not used
     */
    virtual bool do_copy_back(void)
    {
        return is_used && !read_only && !is_dynamic;
    }

    /**
     * \brief returns a bit field of the enabled options for this array
     *
     * \returns bitfield repesentation of the flags
     */
    virtual int get_options(void)
    {
        int options = SHL_MALLOC_NONE;

        if (use_hugepage)
            options |= SHL_MALLOC_HUGEPAGE;

        else if (use_largepage)
            options |= SHL_MALLOC_LARGEPAGE;

        return options;
    }

    /**
     * \brief modifies the used state of the array
     *
     * \param used  new value for the used flag
     */
    void set_used(bool used)
    {
        is_used = used;
    }

    /**
     * \brief modifies the dynamic state of the array
     *
     * \param dynamic   new value for the used flag
     */
    void set_dynamic(bool dynamic)
    {
        is_dynamic = dynamic;
    }

    /**
     * \brief modifies the dynamic state of the array
     *
     * \param dynamic   new value for the used flag
     */
    void set_read_only(bool ro)
    {
        read_only = ro;
    }

    /*
     * ---------------------------------------------------------------------------
     * Array Access Methods
     * ---------------------------------------------------------------------------
     */

    /**
     * \brief gets the i-th element of the array
     *
     * \param i index of the element
     *
     * \return value of element
     */
    virtual T get(size_t i)
    {
#ifdef PROFILE
        __sync_fetch_and_add(&num_rd, 1);
#endif
        RANGE_CHECK(i);

        return array[i];
    }

    void set_dma_fraction(uint8_t fraction) {
        if (fraction > 100) {
            fraction = 100;
        }
        dma_fraction = fraction;
    }

    /**
     * \brief writes to the i-th element
     *
     * \param i element index
     * \param v new element value
     */
    virtual void set(size_t i, T v)
    {
#ifdef PROFILE
        __sync_fetch_and_add(&num_wr, 1);
#endif
        RANGE_CHECK(i);

        array[i] = v;
    }

    /**
     * \brief returns a raw pointer to the begining of the array
     *
     * \return pointer to the array
     *         NULL if not initialized
     */
    virtual T* get_array(void)
    {
        return array;
    }

    /**
     * \brief returns the number of elements of the array
     *
     * \returns element count
     */
    size_t get_size(void)
    {
        return size;
    }

    void *get_meminfo(void)
    {
        return meminfo;
    }

    /*
     * ---------------------------------------------------------------------------
     * Array Debug Helpers
     * ---------------------------------------------------------------------------
     */

    /**
     * \brief prints the array access statistics
     *
     * Note: The programm has to be compiled with enabled PROFILE
     */
    virtual void print_statistics(void)
    {
#ifdef PROFILE
        printf("Number of writes %10d\n", num_wr);
        printf("Number of reads  %10d\n", num_rd);
#endif
    }

    /**
     * \brief
     */
    void print(void)
    {
        print_options();
        printf("\n");
    }

    /*
     * ---------------------------------------------------------------------------
     * Array Initialization Methods
     * ---------------------------------------------------------------------------
     */

    /**
     * \brief allocates a memory region to hold the array data
     *
     * This function will allocate memory in any region of the machine
     */
    virtual int alloc(void)
    {
        return alloc(SHL_NUMA_IGNORE);
    }

    /**
     * \brief allocates a memory region to hold the array data
     *
     * \param numa_node node ID of the region to allocate from
     *
     * This function tries to allocate memory from the specified NUMA region
     */
    virtual int alloc(int numa_node)
    {
        /* don't alloc if the array pointer is present */
        if (array) {
            return -1;
        }

        if (!do_alloc()) {
            /* the array is not used. Skip allocation. */
            return 0;
        }

        assert(!alloc_done);

        print();

        /*
         * shl__malloc determines the pagesize to be used for an array.
         *
         * This might depend on the size of the array, access
         * patterns, and hardware we are running on.
         *
         * Alternatively, the policy on when to use hugepages could
         * also be part of the array class.
         */

        array = (T*) shl__malloc(size * sizeof(T), get_options(), &pagesize,
                                 numa_node, &meminfo);

        printf("pagesize used is %u\n", pagesize);

        alloc_done = true;

        return 0;
    }

    Timer tPrepare;
    Timer tCopy;
    Timer tBarrier;

    /**
     * \brief Optimized method for copying data between two arrays
     *
     * \param src   The source array to copy from
     *
     * \returns 0        if the copy was successful
     *          non-zero if there was an error
     *
     *
     * This is useful for example for "double-buffering" for parallel
     * OpenMP loops.
     */
    virtual int copy_from_array_async(shl_array<T> *src_array, size_t elements);
    virtual int copy_from_array(shl_array<T> *src_array);

    /**
     * \brief initializes the array to a specific value
     *
     * \param value data two fill the array with
     *
     * \returns 0 if the array has been initialized
     *          non-zero if the array is not yet allocated
     */
    virtual int init_from_value_async(T value, size_t elements);
    virtual int init_from_value(T value)
    {
        size_t start = (size * ARRAY_COPY_DMA_RATION);

        if ((start > 0) && init_from_value_async(value, start) != 0) {
            start = 0;
        }

        #pragma omp parallel for
        for (size_t i = start; i < size; ++i) {
            array[i] = value;
        }

        copy_barrier();
        return 0;
    }

    /**
     * \brief Copy data from existing array
     *
     * \param src pointer to a memory location of data to copy in
     *
     * XXX WARNING: The sice of array src is not checked.
     * Assumption: sizeof(src) == this->size
     */
    virtual int copy_from_async(T* src, size_t elements);
    virtual int copy_from(T* src)
    {
        if (!do_copy_in()) {
            return 0;
        }

        size_t start = (size / 100 * dma_fraction);

        if ((start > 0) && copy_from_async(src, start) != 0) {
            start = 0;
        }

        #pragma omp parallel for
        for (size_t i = start; i < size; ++i) {
            array[i] = src[i];
        }

        copy_barrier();

        return 0;
    }

    /**
     * \brief Copy data to existing array
     *
     * \param dest  destination array
     *
     * XXX WARNING: The sice of array src is not checked. Assumption:
     * sizeof(src) == this->size
     */
    virtual int copy_back_async(T* dest, size_t elements);
    virtual int copy_back(T* dest)
    {
        if (!do_copy_back()) {
            return 0;
        }

        size_t start = (size * ARRAY_COPY_DMA_RATION);

        if ((start > 0) && copy_back_async(dest, size) != 0) {
            start = 0;
        }

        #pragma omp parallel for
        for (size_t i = start; i < size; ++i) {
            dest[i] = array[i];
        }
        copy_barrier();

        return 0;
    }

    size_t poll_count;

    /**
     * \brief blocks until the async copy to this array has been completed
     */
    virtual void copy_barrier(void);

 protected:

    /**
     * \brief prints the options for this array
     */
    virtual void print_options(void)
    {
        printf("Array[%20s]: elements=%10zu-", shl_base_array::name, size);
        print_number(size);
        printf(" size=%10zu-", size * sizeof(T));
        print_number(size * sizeof(T));
        printf(" -- ");
        printf("hugepage=[%c] ", use_hugepage ? 'X' : ' ');
        printf(" -- ");
        printf("used=[%c] ", is_used ? 'X' : ' ');
    }

    virtual void dump(void)
    {
        for (size_t i = 0; i < size; i++) {

            noprintf("idx[%3zu] is %d\n", i, array[i]);
        }
    }

 public:
    virtual unsigned long get_crc( void )
    {
        if (!alloc_done)
            return 0;

        return shl__calculate_crc(array, size, sizeof(T));
    }

    void print_crc( void )
    {
        if (get_conf()->do_crc) {
            printf("CRC %s 0x%lx\n", shl_base_array::name, get_crc());
        } else {
            printf("CRC %s n.a.\n", shl_base_array::name);
        }
    }

 public:
    Timer t_collapse;
    Timer t_expand[MAXCORES];

    virtual void collapse(void)
    {
        // nop
    }

    virtual void expand(void)
    {
        // nop
    }

    virtual void set_cached(size_t i, T v, struct array_cache c)
    {
        set(i, v);
    }
};

#if defined(BARRELFISH)
#include <backend/barrelfish/shl_array_backend.hpp>
#elif defined(__linux)
#include <backend/linux/shl_array_backend.hpp>
#else
#error Unknown Operating System
#endif

#endif /* __SHL_ARRAY */
