/*
 * Copyright (c) 2014 ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetsstrasse 6, CH-8092 Zurich. Attn: Systems Group.
 */

#ifndef __SHL_ARRAY_REPLICATED
#define __SHL_ARRAY_REPLICATED

#include <cstdlib>
#include <cstdarg>
#include <cstring> // memset
#include <iostream>
#include <limits>

#include "shl.h"
#include "shl_timer.hpp"
#include "shl_configuration.hpp"

/**
 * \brief Array implementing replication
 *
 *
 * The number of replicas is given by shl_malloc_replicated.
 */
template<class T>
class shl_array_replicated : public shl_array<T> {

private:
    T* master_copy;

 public:
    T** rep_array;      ///<
    int (*lookup)(void);

 protected:
    int num_replicas;


 public:
    /**
     * \brief Initialize replicated array
     *
     * \param _size     size of the array in elements
     * \param _name     name of the array
     * \param lookup_fn function handling the lookups
     */
    shl_array_replicated(size_t _size, const char *_name, int (*lookup_fn)(void)) :
                    shl_array<T>(_size, _name, SHL_A_REPLICATED)
    {
        shl_array<T>::read_only = true;
        lookup = lookup_fn;
        master_copy = NULL;
        num_replicas = -1;
        rep_array = NULL;
    }

    /**
     * \brief initalizes a new replicated array based on supplied memory
     *
     * \param _size     size of the array in elements
     * \param _name     name of the array
     * \param lookup_fn function handling the lookups
     * \param mem       array of pointers to the memory information structures
     * \param data      array of pointers to the arrays
     */
    shl_array_replicated(size_t _size,
                         const char *_name,
                         int (*lookup_fn)(void),
                         void **mem,
                         void **data) :
                    shl_array<T>(_size, _name, mem, data, SHL_A_REPLICATED)
    {
        shl_array<T>::read_only = true;
        lookup = lookup_fn;
        master_copy = NULL;
        num_replicas = -1;
        rep_array = NULL;
    }

    /**
     * \brief allocates the arrays
     */
    virtual int alloc(void);

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
    int copy_from_array_async(shl_array<T> *src_array, size_t elements);
    int copy_from_array(shl_array<T> *src_array)
    {
        size_t elements = (src_array->get_size() > this->size)
                                        ? this->size : src_array->get_size();

        size_t start = (elements * ARRAY_COPY_DMA_RATION);

        if (copy_from_array_async(src_array, start) != 0) {
            start = 0;
        }

        T* src = src_array->get_array();
        #pragma omp parallel for
        for (size_t i = start; i < elements; ++i) {
            for (int j = 0; j < num_replicas; j++) {
                rep_array[j][i] = src[i];
            }
        }

        this->copy_barrier();

        return 0;
    }

    /**
     * \brief initializes the array to a specific value
     *
     * \param value data two fill the array with
     *
     * \returns 0 if the array has been initialized
     *          non-zero if the array is not yet allocated
     */
    int init_from_value_async(T value, size_t elements);
    int init_from_value(T value)
    {
        size_t start = (this->size * ARRAY_COPY_DMA_RATION);

        if (init_from_value_async(value, start) != 0) {
            start = 0;
        }

        #pragma omp parallel for
        for (size_t i = start; i < this->size; ++i) {
            for (int j = 0; j < num_replicas; j++) {
                rep_array[j][i] = value;
            }
        }

        this->copy_barrier();
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
    int copy_from_async(T* src, size_t elements);
    int copy_from(T* src)
    {
        if (!this->do_copy_in()) {
            return 0;
        }

        size_t start = (this->size / 100 * this->dma_fraction);

        if (copy_from_async(src, start) != 0) {
            start = 0;
        }

        #pragma omp parallel for
        for (size_t i = start; i < this->size; ++i) {
            for (int j = 0; j < num_replicas; j++) {
                rep_array[j][i] = src[i];
            }
        }

        this->copy_barrier();

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
    int copy_back_async(T* dest, size_t elements);
    int copy_back(T* a)
    {
        // nop -- currently only replicating read-only data
        assert(shl_array<T>::read_only);

        return 0;
    }

    /**
     * \brief Return pointer to beginning of replica
     */
    virtual T* get_array(void)
    {
#ifdef SHL_DBG_ARRAY
        printf("Getting pointer for array [%s]\n", shl_base_array::name);
#endif
        if (this->alloc_done) {
            return rep_array[lookup()];
        } else {
            return NULL;
        }
    }

    virtual T get(size_t i)
    {
#ifdef PROFILE
        __sync_fetch_and_add(&shl_array<T>::num_rd, 1);
#endif
        return rep_array[lookup()][i];
    }

    virtual void set(size_t i, T v)
    {
#ifdef PROFILE
        __sync_fetch_and_add(&shl_array<T>::num_wr, 1);
#endif
        for (int j = 0; j < num_replicas; j++)
            rep_array[j][i] = v;
    }

    virtual ~shl_array_replicated(void)
    {
        // // Free replicas
        // for (int i=0; i<num_replicas; i++) {
        //     free(rep_array[i]);
        // }
        // // Clean up meta-data
        // delete rep_array;
    }

    void synchronize(void)
    {
        assert(shl_array<T>::alloc_done);
        shl__repl_sync(master_copy, (void**) rep_array, num_replicas,
                       shl_array<T>::size * sizeof(T));
    }

 protected:
    virtual void print_options(void)
    {
        shl_array<T>::print_options();
        printf("replication=[X]");
    }

    virtual void dump(void)
    {
        for (int j = 0; j < num_replicas; j++) {

            for (size_t i = 0; i < shl_array<T>::size; i++) {

                noprintf("rep[%2d] idx[%3zu] is %d\n", j, i, rep_array[j][i]);
            }
        }
    }

 public:
    virtual unsigned long get_crc( void )
        {
        printf("repl get_crc\n");
            if (!shl_array<T>::alloc_done)
                return 0;

            unsigned long crc_0 = shl__calculate_crc(rep_array[0], shl_array<T>::size, sizeof(T));
            unsigned long crc_i;

#ifdef DEBUG
            for (int i = 0; i < num_replicas; i++) {
                crc_i = shl__calculate_crc(rep_array[i], shl_array<T>::size, sizeof(T));
                if (crc_0 != crc_i) {
                    printf(ANSI_COLOR_CYAN "WARNING: " ANSI_COLOR_RESET
                           "replica %d's content diverges (%lx vs %lx)\n",
                           i, (unsigned long) crc_i, (unsigned long) crc_0);
                } else {
                    printf("replica %d's content is %lx\n", i, (unsigned long) crc_i);
                }
            }
#endif

            return crc_0;
        }

};

/// include backend specific functions
#if defined(BARRELFISH)
#include <backend/barrelfish/shl_array_replicated_backend.hpp>
#elif defined(__linux)
#include <backend/linux/shl_array_replicated_backend.hpp>
#else
#error Unknown Operating System
#endif

#endif /* __SHL_ARRAY */
