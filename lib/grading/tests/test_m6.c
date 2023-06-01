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
#include <aos/aos_rpc.h>
#include <aos/solution.h>
#include <aos/paging.h>
#include <proc_mgmt/proc_mgmt.h>

#include <grading/io.h>
#include <grading/state.h>
#include <grading/options.h>
#include <grading/tests.h>
#include "../include/grading/options_internal.h"

#define BINARY_NAME "alloc"


static void spawn_one_without_args(coreid_t core)
{
    errval_t err;

    grading_printf("spawn_one_without_args(%s, %d)\n", BINARY_NAME, core);

    domainid_t pid;
    debug_printf("sending spawn request to core %d\n", core);
    err = proc_mgmt_spawn_with_cmdline(BINARY_NAME, core, &pid);
    if (err_is_fail(err)) {
        grading_test_fail("U1-1", "failed to load: %s\n", err_getstring(err));
        return;
    }
    debug_printf("got pid: %d\n", pid);

    // grading_printf("core %d received pid: %d\n", pid, core);
    // Heads up! When you have messaging support, then you may need to handle a
    // few messages here for the process to start up

    grading_printf("waiting 2 seconds to give the other domain chance to run...\n");
    // barrelfish_usleep(2000000);
}

static void send_ack(coreid_t send_core, coreid_t recv_core, domainid_t pid)
{
    errval_t err;

    struct ump_payload payload;
    payload.type = PID_ACK;
    payload.send_core = send_core;
    payload.recv_core = recv_core;
    memset(payload.payload, 0, sizeof(payload.payload));
    memcpy(payload.payload, &pid, sizeof(domainid_t));

    if (disp_get_core_id() == 0) {
        err = ump_send(get_ump_chan_mon(recv_core, 1), (char *) &payload, sizeof(struct ump_payload));
    } else {
        err = ump_send(get_ump_chan_core(0), (char *) &payload, sizeof(struct ump_payload));
    }
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "ump_send\n");
        abort();
    }
}

errval_t grading_run_tests_urpc(void)
{
    if (grading_options.m6_subtest_run == 0) {
        //return SYS_ERR_OK;
    }

    if ( disp_get_core_id() == 3) {
        return SYS_ERR_OK;
    }

    grading_printf("#################################################\n");
    grading_printf("# TESTS: Milestone 6 (URPC)                      \n");
    grading_printf("#################################################\n");

    struct ump_payload payload;
    errval_t err;

    if (disp_get_core_id() == 0 && false) {
        spawn_one_without_args(1);

        grading_test_pass("U1-1", "received ack from core 1\n");

        spawn_one_without_args(1);
        
        // ump_print(get_ump_chan_mon(2, 0));
        while ((err = ump_receive(get_ump_chan_mon(2, 0), SPAWN_CMDLINE, &payload)) != SYS_ERR_OK);
        ump_send(get_ump_chan_mon(1, 1), (char *) &payload, sizeof(struct ump_payload));

        while ((err = ump_receive(get_ump_chan_mon(1, 0), PID_ACK, &payload)) != SYS_ERR_OK);
        send_ack(1, 2, 44);
    }

    if (disp_get_core_id() == 1 && false) {
        spawn_one_without_args(0);

        spawn_one_without_args(1);

        spawn_one_without_args(2);

        while ((err = ump_receive(get_ump_chan_core(1), SPAWN_CMDLINE, &payload)) != SYS_ERR_OK);
        send_ack(1, 2, 42);
        debug_printf("long payload: %s, len: %d\n", payload.payload, strlen(payload.payload));
        if (strlen(payload.payload) == 86) {
            domainid_t hello_pid;
            err = proc_mgmt_spawn_with_cmdline(payload.payload, disp_get_core_id(), &hello_pid);
            DEBUG_ERR_ON_FAIL(err, "proc_mgmt_spawn_with_cmdline");
            grading_test_pass("U1-2", "received long spawn request from core 1, pid: %d\n", hello_pid);
        }
    }

    if (disp_get_core_id() == 2 && false) {
        domainid_t pid;
        debug_printf("sending long message\n");
        proc_mgmt_spawn_with_cmdline("hello this_is_a_loooooooooooooooooooooooooooooooooooooooooooooooooooooooooooong_string", 1, &pid);
        debug_printf("got here........\n");
        debug_printf("pid: %d\n", pid);
        if (pid == 44) {
            grading_test_pass("U1-3", "received pid 44 from core 1\n");
        }
    }

    if (disp_get_core_id() == 2) {
        domainid_t pid;
        proc_mgmt_spawn_with_cmdline("hello", 0, &pid);
    }



    grading_printf("#################################################\n");
    grading_printf("# DONE:  Milestone 6 (URPC)                      \n");
    grading_printf("#################################################\n");

    return SYS_ERR_OK;
}


bool grading_opts_handle_m6_tests(struct grading_options *opts, const char *arg)
{
    (void)arg;

    // enable the m6 tests
    opts->m6_subtest_run = 0x1;

    // TODO(optional): parsing options to selectively enable tests or configure them at runtime.

    return true;
}