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

#include <stdio.h>

#include <aos/aos.h>
#include <aos/solution.h>
#include <aos/capabilities.h>
#include <aos/ram_alloc.h>
#include <aos/aos_rpc.h>
#include <grading/grading.h>
#include <proc_mgmt/proc_mgmt.h>

#include <grading/io.h>
#include <grading/state.h>
#include <grading/options.h>
#include <grading/tests.h>
#include "../include/grading/options_internal.h"


/// framesize to be 5 MB
#define FRAME_SIZE      (5 << 20)
#define NUM_MAPS        16
#define FIXED_ADDRESS   (32ULL << 40)
#define FIXED_ADDRESS2  (32ULL << 40)
#define FIXED_ADDRESS3  (32ULL << 40) 
#define HEAP_ALLOC_SIZE (256 << 20)

static void alloc_and_map_one(void)
{
    errval_t err;

    grading_printf("alloc_and_map_one(%zu)\n", FRAME_SIZE);

    struct capref cap;
    err = frame_alloc(&cap, FRAME_SIZE, NULL);
    if (err_is_fail(err)) {
        grading_test_fail("V1-1", "failed to allocate a single frame\n");
        return;
    }

    grading_printf("allocated frame, trying to map it\n");

    void *buf;
    err = paging_map_frame(get_current_paging_state(), &buf, FRAME_SIZE, cap);
    if (err_is_fail(err)) {
        grading_test_fail("V1-1", "failed to map the frame\n");
        return;
    }

    // the frame should be all zeroed, otherwise something is odd
    grading_printf("mapped frame, checking for zeroes\n");
    for (size_t i = 0; i < FRAME_SIZE; i++) {
        if (((uint8_t *)buf)[i] != 0x00) {
            grading_test_fail("V1-1", "memory not set correctly\n");
            return;
        }
    }

    grading_printf("memset(%p, i, %zu)\n", buf, FRAME_SIZE);
    uint64_t *ptr = (uint64_t *)buf;
    for (size_t i = 0; i < FRAME_SIZE / sizeof(uint64_t); i++) {
        ptr[i] = i;
    }

    grading_printf("verifying..\n", buf, FRAME_SIZE);
    for (size_t i = 0; i < FRAME_SIZE / sizeof(uint64_t); i++) {
        if (ptr[i] != i) {
            grading_printf("verification failed: ptr[%zu] was %lu (expected %zu)\n", i, ptr[i], i);
            grading_test_fail("V1-1", "memory not set correctly\n");
            return;
        }
    }
    grading_test_pass("V1-1", "alloc_and_map\n");
}

static void alloc_and_map_many(void)
{
    errval_t err;

    grading_printf("alloc_and_map_many(%zu)\n", NUM_MAPS);

    struct capref cap;
    err = frame_alloc(&cap, FRAME_SIZE, NULL);
    if (err_is_fail(err)) {
        grading_test_fail("V1-2", "failed to allocate a single frame\n");
        return;
    }

    grading_printf("allocated frame, trying to map it %zu times\n", NUM_MAPS);

    for (size_t n = 0; n <= NUM_MAPS; n++) {
        void *buf;
        err = paging_map_frame(get_current_paging_state(), &buf, FRAME_SIZE, cap);
        if (err_is_fail(err)) {
            grading_test_fail("V1-2", "failed to map the frame (%s)\n", err_getstring(err));
            return;
        }

        // the frame should be all zeroed, otherwise something is odd
        grading_printf("mapped frame %zu, checking for zeroes\n", n);
        for (size_t i = 0; i < FRAME_SIZE; i++) {
            if (((uint8_t *)buf)[i] != (uint8_t)n) {
                grading_test_fail("V1-2", "memory not set correctly\n");
                return;
            }
        }

        grading_printf("%zu memset(%p, %u, %zu) and verify\n", n, buf, (uint8_t)n, FRAME_SIZE);
        memset(buf, (uint8_t)(n + 1), FRAME_SIZE);
        for (size_t i = 0; i < FRAME_SIZE; i++) {
            if (((uint8_t *)buf)[i] != (uint8_t)(n + 1)) {
                grading_test_fail("V1-2", "memory not set correctly\n");
                return;
            }
        }
    }
    grading_test_pass("V1-2", "alloc_and_map\n");
}

static void alloc_and_map_fixed(void)
{
    errval_t err;

    grading_printf("alloc_and_map_fixed(%lx, %zu)\n", FIXED_ADDRESS, BASE_PAGE_SIZE);

    struct capref cap;
    err = frame_alloc(&cap, FRAME_SIZE, NULL);
    if (err_is_fail(err)) {
        grading_test_fail("V1-3", "failed to allocate a single frame\n");
        return;
    }

    grading_printf("allocated frame, trying to map it at %lx\n", FIXED_ADDRESS);

    void *buf = (void *)FIXED_ADDRESS;
    err       = paging_map_fixed(get_current_paging_state(), FIXED_ADDRESS, cap, FRAME_SIZE);
    if (err_is_fail(err)) {
        grading_test_fail("V1-3", "failed to map the frame\n");
        return;
    }

    // the frame should be all zeroed, otherwise something is odd
    grading_printf("mapped frame, checking for zeroes\n");
    for (size_t i = 0; i < BASE_PAGE_SIZE; i++) {
        if (((uint8_t *)buf)[i] != 0x00) {
            grading_test_fail("V1-3", "memory not set correctly\n");
            return;
        }
    }

    grading_printf("memset(%p, i, %zu)\n", buf, FRAME_SIZE);
    uint64_t *ptr = (uint64_t *)buf;
    for (size_t i = 0; i < FRAME_SIZE / sizeof(uint64_t); i++) {
        ptr[i] = i;
    }

    grading_printf("verifying..\n", buf, FRAME_SIZE);
    for (size_t i = 0; i < FRAME_SIZE / sizeof(uint64_t); i++) {
        if (ptr[i] != i) {
            grading_printf("verification failed: ptr[%zu] was %lu (expected %zu)\n", i, ptr[i], i);
            grading_test_fail("V1-3", "memory not set correctly\n");
            return;
        }
    }

    grading_test_pass("V1-3", "alloc_and_map_fixed\n");
}

static void alloc_heap(void)
{
    grading_printf("alloc_heap(%zu)\n", HEAP_ALLOC_SIZE);

    uint8_t *buf = malloc(HEAP_ALLOC_SIZE);
    if (buf == NULL) {
        grading_test_fail("V1-4", "failed to allocate heap\n");
        return;
    }

    size_t npages = HEAP_ALLOC_SIZE / BASE_PAGE_SIZE;
    for (size_t i = 0; i < npages / 32; i++) {
        // grading_printf("accessing buf[%zu] @ %p\n", i * BASE_PAGE_SIZE * 8,
        //                &buf[i * BASE_PAGE_SIZE * 8]);

        buf[i * BASE_PAGE_SIZE * 8] = 0x42;
    }

    grading_test_pass("V1-4", "alloc_heap\n");
}

// Student tests


static void alloc_map_and_unmap_one(void)
{
    errval_t err;

    grading_printf("alloc_map_and_unmap_one(%zu)\n", FRAME_SIZE);

    struct capref cap;
    err = frame_alloc(&cap, FRAME_SIZE, NULL);
    if (err_is_fail(err)) {
        grading_test_fail("V1-5", "failed to allocate a single frame\n");
        return;
    }

    grading_printf("allocated frame, trying to map it\n");

    void *buf;
    err = paging_map_frame(get_current_paging_state(), &buf, FRAME_SIZE, cap);
    if (err_is_fail(err)) {
        grading_test_fail("V1-5", "failed to map the frame\n");
        return;
    }

    // the frame should be all zeroed, otherwise something is odd
    grading_printf("mapped frame, checking for zeroes\n");
    for (size_t i = 0; i < FRAME_SIZE; i++) {
        if (((uint8_t *)buf)[i] != 0x00) {
            grading_test_fail("V1-5", "memory not set correctly\n");
            return;
        }
    }

    grading_printf("memset(%p, i, %zu)\n", buf, FRAME_SIZE);
    uint64_t *ptr = (uint64_t *)buf;
    for (size_t i = 0; i < FRAME_SIZE / sizeof(uint64_t); i++) {
        ptr[i] = i;
    }

    grading_printf("verifying..\n", buf, FRAME_SIZE);
    for (size_t i = 0; i < FRAME_SIZE / sizeof(uint64_t); i++) {
        if (ptr[i] != i) {
            grading_printf("verification failed: ptr[%zu] was %lu (expected %zu)\n", i, ptr[i], i);
            grading_test_fail("V1-5", "memory not set correctly\n");
            return;
        }
    }

    // try to unmap
    err = paging_unmap(get_current_paging_state(), buf);
    if (err_is_fail(err)) {
        grading_test_fail("V1-5", "failed to unmap the frame\n");
        return;
    }

    // check if actually unmapped
    grading_printf("this should cause a segfault\n");
    ptr[FRAME_SIZE / sizeof(uint64_t) - 1] = 0xDEADBEEF;

    grading_test_pass("V1-5", "alloc_and_map\n");
}

static void heap_alloc_and_free(void)
{
    grading_printf("heap_alloc_and_free(%zu)\n", HEAP_ALLOC_SIZE);
    
    uint8_t *buf = malloc(HEAP_ALLOC_SIZE);
    if (buf == NULL) {
        grading_test_fail("V1-6", "failed to allocate heap\n");
        return;
    }

    size_t npages = HEAP_ALLOC_SIZE / BASE_PAGE_SIZE;
    for (size_t i = 0; i < npages / 32; i++) {
        // grading_printf("accessing buf[%zu] @ %p\n", i * BASE_PAGE_SIZE * 8,
        //                &buf[i * BASE_PAGE_SIZE * 8]);

        buf[i * BASE_PAGE_SIZE * 8] = 0x42;
    }
    grading_printf("freeing buf\n");
    free(buf);
    grading_printf("trying to access buf (this should segfault)\n");
    grading_test_pass("V1-6", "heap_alloc_and_free\n");
}

static void alloc_and_map_unmap_and_remap_fixed(void)
{
    errval_t err;

    grading_printf("alloc_and_map_unmap_and_remap_fixed(%lx, %zu)\n", FIXED_ADDRESS2, BASE_PAGE_SIZE);
    
    struct capref cap;
    err = frame_alloc(&cap, FRAME_SIZE, NULL);
    if (err_is_fail(err)) {
        grading_test_fail("V1-7", "failed to allocate a single frame\n");
        return;
    }

    grading_printf("allocated frame, trying to map it at %lx\n", FIXED_ADDRESS2);

    void *buf = (void *)FIXED_ADDRESS2;
    err       = paging_map_fixed(get_current_paging_state(), FIXED_ADDRESS2, cap, FRAME_SIZE);
    if (err_is_fail(err)) {
        grading_test_fail("V1-7", "failed to map the frame\n");
        return;
    }

    // the frame should be all zeroed, otherwise something is odd
    grading_printf("mapped frame, checking for zeroes\n");
    for (size_t i = 0; i < BASE_PAGE_SIZE; i++) {
        if (((uint8_t *)buf)[i] != 0x00) {
            grading_test_fail("V1-7", "memory not set correctly\n");
            return;
        }
    }

    grading_printf("memset(%p, i, %zu)\n", buf, FRAME_SIZE);
    uint64_t *ptr = (uint64_t *)buf;
    for (size_t i = 0; i < FRAME_SIZE / sizeof(uint64_t); i++) {
        ptr[i] = i;
    }

    grading_printf("verifying..\n", buf, FRAME_SIZE);
    for (size_t i = 0; i < FRAME_SIZE / sizeof(uint64_t); i++) {
        if (ptr[i] != i) {
            grading_printf("verification failed: ptr[%zu] was %lu (expected %zu)\n", i, ptr[i], i);
            grading_test_fail("V1-7", "memory not set correctly\n");
            return;
        }
    }

    grading_printf("trying to unmap the page..\n");
    paging_unmap(get_current_paging_state(), buf);
    err = paging_unmap(get_current_paging_state(), buf);
    if (err_is_fail(err)) {
        grading_test_fail("V1-7", "failed to unmap the frame\n");
        return;
    }
    

    buf = (void*) FIXED_ADDRESS2;
    err       = paging_map_fixed(get_current_paging_state(), FIXED_ADDRESS2, cap, FRAME_SIZE);
    if (err_is_fail(err)) {
        grading_test_fail("V1-7", "failed to map the frame\n");
        return;
    }

    // I am pretty sure the expected behavior is that the memory retains 
    // it's bytes from the previous mapping but I could be wrong.

    // the frame should be all zeroed, otherwise something is odd
    // grading_printf("mapped frame, checking for zeroes\n");
    // for (size_t i = 0; i < BASE_PAGE_SIZE; i++) {
    //     if (((uint8_t *)buf)[i] != 0x00) {
    //         grading_test_fail("V1-7", "memory not set correctly\n");
    //         return;
    //     }
    // }

    //grading_printf("memset(%p, i, %zu)\n", buf, FRAME_SIZE);
    ptr = (uint64_t *)buf;
    // for (size_t i = 0; i < FRAME_SIZE / sizeof(uint64_t); i++) {
    //     ptr[i] = i;
    // }

    grading_printf("verifying that bytes remained the same..\n", buf, FRAME_SIZE);
    for (size_t i = 0; i < FRAME_SIZE / sizeof(uint64_t); i++) {
        if (ptr[i] != i) {
            grading_printf("verification failed: ptr[%zu] was %lu (expected %zu)\n", i, ptr[i], i);
            grading_test_fail("V1-7", "memory not set correctly\n");
            return;
        }
    }
    

    grading_test_pass("V1-7", "alloc_and_map_unmap_and_remap_fixed\n");
}


static void alloc_and_map_unmap_and_remap_many_times_fixed(int num_times)
{
    errval_t err;

    grading_printf("alloc_and_map_unmap_and_remap_many_times_fixed(%lx, %zu)\n", FIXED_ADDRESS3, BASE_PAGE_SIZE);
    
    struct capref cap;
    err = frame_alloc(&cap, FRAME_SIZE, NULL);
    if (err_is_fail(err)) {
        grading_test_fail("V1-8", "failed to allocate a single frame\n");
        return;
    }

    grading_printf("allocated frame, trying to map it at %lx\n", FIXED_ADDRESS3);
    
    for (int j = 0; j < num_times; j++) {
        void *buf = (void *)FIXED_ADDRESS3;
        err       = paging_map_fixed(get_current_paging_state(), FIXED_ADDRESS3, cap, FRAME_SIZE);
        if (err_is_fail(err)) {
            grading_test_fail("V1-8", "failed to map the frame\n");
            return;
        }

        grading_printf("memset(%p, i, %zu)\n", buf, FRAME_SIZE);
        uint64_t *ptr = (uint64_t *)buf;
        for (size_t i = 0; i < FRAME_SIZE / sizeof(uint64_t); i++) {
            ptr[i] = i;
        }

        grading_printf("verifying..\n", buf, FRAME_SIZE);
        for (size_t i = 0; i < FRAME_SIZE / sizeof(uint64_t); i++) {
            if (ptr[i] != i) {
                grading_printf("verification failed: ptr[%zu] was %lu (expected %zu)\n", i, ptr[i], i);
                grading_test_fail("V1-8", "memory not set correctly\n");
                return;
            }
        }

        grading_printf("trying to unmap the page..\n");
        paging_unmap(get_current_paging_state(), buf);
        err = paging_unmap(get_current_paging_state(), buf);
        if (err_is_fail(err)) {
            grading_test_fail("V1-8", "failed to unmap the frame\n");
            return;
        }
        grading_printf("successfully mapped and unmapped %d times\n", j + 1);
    }

    grading_test_pass("V1-8", "alloc_and_map_unmap_and_remap_fixed\n");
}


errval_t grading_run_tests_virtual_memory(bool early)
{
    // make compiler happy about unused parameters
    (void)early;

    if (grading_options.m2_subtest_run == 0) {
        return SYS_ERR_OK;
    }

    // run them on core 0 only, core 1 tests come in M5
    if (disp_get_core_id() != 0) {
        return SYS_ERR_OK;
    }

    grading_printf("#################################################\n");
    grading_printf("# TESTS: Milestone 2 (Virtual Memory Management) \n");
    grading_printf("#################################################\n");
    
    //student tests
    if (true) alloc_and_map_unmap_and_remap_many_times_fixed(2000);
    if (false) alloc_and_map_unmap_and_remap_fixed();
    
    if (true)alloc_and_map_fixed();
    alloc_and_map_one();
    alloc_and_map_many();
    alloc_heap();

    //student tests
    if (false) alloc_map_and_unmap_one(); // this test is expected to cause a segfault
    if (false) heap_alloc_and_free(); //don't use this test it is broken and bad and tests nothing

    grading_printf("#################################################\n");
    grading_printf("# DONE:  Milestone 2 (Virtual Memory Management) \n");
    grading_printf("#################################################\n");

    grading_stop();

    return SYS_ERR_OK;
}


bool grading_opts_handle_m2_tests(struct grading_options *opts, const char *arg)
{
    (void)arg;

    // enable the m2 tests
    opts->m2_subtest_run = 0x1;

    // TODO(optional): parsing options to selectively enable tests or configure them at runtime.

    return true;
}
