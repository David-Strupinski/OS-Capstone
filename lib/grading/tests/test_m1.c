/*
 * \file
 * \brief Code to test milestone 1
 */

/*
 * Copyright (c) 2019, ETH Zurich.
 * Copyright (c) 2022, The University of British Columbia.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstrasse 6, CH-8092 Zurich. Attn: Systems Group.
 */

///////////////////////////////////////////////////////////////////////////////////////////////////
//                                                                                               //
//                          !! WARNING !!   !! WARNING !!   !! WARNING !!                        //
//                                                                                               //
//      This file is part of the grading library and will be overwritten before grading.         //
//                         You may edit this file for your own tests.                            //
//                                                                                               //
///////////////////////////////////////////////////////////////////////////////////////////////////

#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#include <aos/aos.h>
#include <aos/solution.h>
#include <aos/paging.h>
#include <mm/mm.h>

#include <grading/io.h>
#include <grading/state.h>
#include <grading/options.h>
#include <grading/tests.h>
#include "../include/grading/options_internal.h"

#define NUM_ALLOC 4
#define PRINT_MAPS false
#define VERBOSE false

static struct capref allocated[NUM_ALLOC];

static bool check_cap_size(struct capref cap, size_t size)
{
    errval_t err;

    struct capability capability;
    err = cap_direct_identify(cap, &capability);
    if (err_is_fail(err)) {
        return false;
    }

    if (capability.type != ObjType_RAM) {
        return false;
    }

    if (capability.u.ram.bytes < size) {
        return false;
    }

    return true;
}

static void alloc_one(struct mm *mem)
{
    errval_t err;

    grading_printf("alloc_one(%zu)\n", BASE_PAGE_SIZE);

    struct capref cap;
    err = mm_alloc(mem, BASE_PAGE_SIZE, &cap);
    if (err_is_fail(err)) {
        grading_test_fail("A1-1", "failed to allocate a single frame\n");
        return;
    }

    if (!check_cap_size(cap, BASE_PAGE_SIZE)) {
        grading_test_fail("A1-1", "cap check failed\n");
        return;
    }

    grading_test_pass("A1-1", "allocate_one\n");
}

static void free_one(struct mm *mem)
{
    errval_t err;

    grading_printf("free_one(%zu)\n", BASE_PAGE_SIZE);

    struct capref cap;
    err = mm_alloc(mem, BASE_PAGE_SIZE, &cap);
    if (err_is_fail(err)) {
        grading_test_fail("A2-1", "failed to allocate a single frame\n");
        return;
    }

    if (!check_cap_size(cap, BASE_PAGE_SIZE)) {
        grading_test_fail("A2-1", "cap check failed\n");
        return;
    }

    err = mm_free(mem, cap);
    if (err_is_fail(err)) {
        grading_test_fail("A2-1", "failed to free a single frame\n");
        return;
    }

    grading_test_pass("A2-1", "free_one\n");
}

static void alloc_many(struct mm *mem)
{
    grading_printf("alloc_many(%zu)\n", NUM_ALLOC);

    for (size_t i = 0; i < NUM_ALLOC; i++) {
        struct capref cap;
        errval_t err = mm_alloc(mem, BASE_PAGE_SIZE, &cap);
        if (err_is_fail(err)) {
            grading_test_fail("A3-1", "failed to allocate a single frame\n");
            return;
        }

        if (!check_cap_size(cap, BASE_PAGE_SIZE)) {
            grading_test_fail("A3-1", "cap check failed\n");
            return;
        }

        allocated[i] = cap;

        if (VERBOSE) grading_printf("allocated %zu\n", i + 1);
    }

    grading_test_pass("A3-1", "allocate_many\n");
}

static void free_many(struct mm *mem)
{
    grading_printf("free_many(%zu)\n", NUM_ALLOC);

    for (size_t i = 0; i < NUM_ALLOC; i++) {
        errval_t err = mm_free(mem, allocated[i]);
        if (err_is_fail(err)) {
            grading_test_fail("A6-1", "failed to free a single frame\n");
            return;
        }

        if (VERBOSE) grading_printf("freed %zu\n", i + 1);
    }

    grading_test_pass("A6-1", "free_many\n");
}

static void alloc_and_map(void)
{
    errval_t err;

    grading_printf("alloc_and_map()\n");

    struct capref cap;
    err = frame_alloc(&cap, BASE_PAGE_SIZE, NULL);
    if (err_is_fail(err)) {
        grading_test_fail("A4-1", "failed to allocate a single frame\n");
        return;
    }

    grading_printf("allocated frame, trying to map it\n");

    void *buf;
    err = paging_map_frame(get_current_paging_state(), &buf, BASE_PAGE_SIZE, cap);
    if (err_is_fail(err)) {
        grading_test_fail("A4-1", "failed to map the frame\n");
        return;
    }
    grading_printf("mapped frame, accessing it memset(%p, 0x42, %zu)\n", buf, BASE_PAGE_SIZE);
    memset(buf, 0x42, BASE_PAGE_SIZE);
    for (size_t i = 0; i < BASE_PAGE_SIZE; i++) {
        if (((uint8_t *)buf)[i] != 0x42) {
            grading_test_fail("A4-1", "memory not set correctly\n");
            return;
        }
    }
    grading_test_pass("A4-1", "alloc_and_map\n");
}

static void partial_free(struct mm *mem)
{
    errval_t err;

    grading_printf("partial free\n");

    mm_print_map(mem);

    struct capref cap;
    err = mm_alloc(mem, BASE_PAGE_SIZE * 8, &cap);
    if (err_is_fail(err)) {
        grading_test_fail("A5-1", "failed to allocate a frame\n");
        return;
    }

    if (!check_cap_size(cap, BASE_PAGE_SIZE * 8)) {
        grading_test_fail("A5-1", "cap check failed\n");
        return;
    }

    mm_print_map(mem);

    struct capref new;
    err = slot_alloc(&new);
    if (err_is_fail(err)) {
        grading_test_fail("A5-1", "failed to allocate slot\n");
        return;
    }
    err = cap_retype(new, cap, BASE_PAGE_SIZE, ObjType_RAM, BASE_PAGE_SIZE * 7);
    if (err_is_fail(err)) {
        grading_test_fail("A5-1", "failed to resize capability\n");
        return;
    }

    err = mm_free(mem, new);
    if (err_is_fail(err)) {
        grading_test_fail("A5-1", "failed to free a single frame\n");
        debug_printf("%s\n", err_getstring(err));
        return;
    }

    mm_print_map(mem);

    grading_test_pass("A5-1", "partial_free\n");
}

errval_t grading_run_tests_physical_memory(struct mm *mm)
{
    if (grading_options.m1_subtest_run == 0) {
        //return SYS_ERR_OK;  // TODO: we changed this
    }

    grading_printf("#################################################\n");
    grading_printf("# TESTS: Milestone 1 (Physical Memory Management)\n");
    grading_printf("#################################################\n");

    if (PRINT_MAPS) mm_print_map(mm);

    alloc_one(mm);
    if (PRINT_MAPS) mm_print_map(mm);

    free_one(mm);
    if (PRINT_MAPS) mm_print_map(mm);

    alloc_many(mm);
    if (PRINT_MAPS) mm_print_map(mm);

    free_many(mm);
    if (PRINT_MAPS) mm_print_map(mm);

    // TODO: free many reverse

    // TODO: run this test
    if (false) alloc_and_map();

    partial_free(mm);
    if (PRINT_MAPS) mm_print_map(mm);

    // TODO: write more tests for mapping

    grading_printf("#################################################\n");
    grading_printf("# DONE:  Milestone 1 (Physical Memory Management)\n");
    grading_printf("#################################################\n");

    grading_stop();

    return SYS_ERR_OK;
}



bool grading_opts_handle_m1_tests(struct grading_options *opts, const char *arg)
{
    (void)arg;

    // enable the m1 tests
    opts->m1_subtest_run = 0x1;

    // TODO(optional): parsing options to selectively enable tests or configure them at runtime.

    return true;
}
