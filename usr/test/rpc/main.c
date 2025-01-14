/**
 * \file
 * \brief init process for child spawning
 */

/*
 * Copyright (c) 2016, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetsstrasse 6, CH-8092 Zurich. Attn: Systems Group.
 */

#include <stdio.h>
#include <stdlib.h>

#include <aos/aos.h>
#include <aos/aos_rpc.h>
#include <aos/waitset.h>
#include <aos/deferred.h>

#include <grading/grading.h>
#include <grading/io.h>
#include <proc_mgmt/proc_mgmt.h>

#include <aos/systime.h>


#define NUM_MEMORY_REQUESTS 10
#define CMDLINE             "hello arg1 arg2 arg3"

static void test_basic_rpc(void)
{
    errval_t err;

    grading_printf("test_basic_rpc()\n");

    struct aos_rpc *init_rpc = aos_rpc_get_init_channel();
    if (!init_rpc) {
        grading_test_fail("R1-3", "no init_rpc channel set!\n");
        return;
    }

    grading_printf("sending number 42.\n");
    uint32_t before_time = systime_now();
    err = aos_rpc_send_number(init_rpc, 42);
    uint32_t after_time = systime_now();
    uint32_t microseconds = systime_to_us(after_time - before_time);
    debug_printf("microseconds for call: %lu\n", systime_to_us(after_time - before_time));
    debug_printf("clock cycle estimation: %lf\n", (double)microseconds / 0.00083333333333333);
    if (err_is_fail(err)) {
        grading_test_fail("R1-1", "failed to send number: %s\n", err_getstring(err));
        return;
    }

    grading_printf("sending string 'hello init'\n");
    err = aos_rpc_send_string(init_rpc, "hello init");
    if (err_is_fail(err)) {
        grading_test_fail("R1-1", "failed to send string: %s\n", err_getstring(err));
        return;
    }
    grading_test_pass("R1-1", "test_basic_rpc\n");
}

static void test_serial_rpc(void)
{
    errval_t err;

    grading_printf("test_serial_rpc()\n");

    grading_printf("normal printf(hello world);\n");
    printf("hello world\n");

    struct aos_rpc *serial_rpc = aos_rpc_get_serial_channel();
    if (!serial_rpc) {
        grading_test_fail("R1-2", "no serial_rpc channel set!\n");
        return;
    }

    grading_printf("normal print character by character\n");
    char *str = "hello world\n";
    for (size_t i = 0; i < strlen(str); i++) {
        err = aos_rpc_serial_putchar(serial_rpc, str[i]);
        if (err_is_fail(err)) {
            grading_test_fail("R1-2", "failed to send char: %s\n", err_getstring(err));
            return;
        }
    }
    grading_test_pass("R1-2", "test_basic_rpc\n");
}

static void test_serial_getchar(void)
{
    errval_t err;

    grading_printf("test_serial_getchar()\n");

    struct aos_rpc *serial_rpc = aos_rpc_get_serial_channel();
    if (!serial_rpc) {
        grading_test_fail("R1-5", "no serial_rpc channel set!\n");
        return;
    }

    grading_printf("Enter some text: ");
    char str[4];
    for (size_t i = 0; i < 4; i++) {
        err = aos_rpc_serial_getchar(serial_rpc, &str[i]);
        if (err_is_fail(err)) {
            grading_test_fail("R1-5", "failed to get char: %s\n", err_getstring(err));
            return;
        }
    }

    grading_printf("string: %s\n", str);

    grading_test_pass("R1-5", "test_serial_getchar\n");
}

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

static void test_memory_rpc(void)
{
    errval_t err;
    size_t   bytes = BASE_PAGE_SIZE;

    grading_printf("test_memory_rpc(%zu)\n", bytes);

    grading_printf("calling ram_alloc with %zu bytes...\n", bytes);
    struct capref ram_cap;
    err = ram_alloc(&ram_cap, bytes);
    if (err_is_fail(err)) {
        grading_test_fail("R1-3", "failed to do ram_alloc %s\n", err_getstring(err));
        return;
    }

    if (!check_cap_size(ram_cap, bytes)) {
        grading_test_fail("R1-3", "cap check failed\n");
        return;
    }

    grading_printf("successful ram_alloc.\n");

    grading_printf("calling memory RPC directly.\n");

    struct aos_rpc *mem_rpc = aos_rpc_get_memory_channel();
    if (!mem_rpc) {
        grading_test_fail("R1-3", "no mem_rpc channel set!\n");
        return;
    }

    for (size_t i = 0; i < NUM_MEMORY_REQUESTS; i++) {
        size_t retbytes;
        err = aos_rpc_get_ram_cap(mem_rpc, bytes, bytes, &ram_cap, &retbytes);
        if (err_is_fail(err)) {
            grading_test_fail("R1-3", "failed to do memory alloc rpc %s\n", err_getstring(err));
            return;
        }

        if (!check_cap_size(ram_cap, bytes)) {
            grading_test_fail("R1-3", "cap check failed\n");
            return;
        }

        grading_printf("get_ram_cap %zu / %zu successful\n", i, NUM_MEMORY_REQUESTS);
    }

    grading_test_pass("R1-3", "test_memory_rpc\n");
}

static void test_spawn_rpc(void)
{
    errval_t err;

    // the core we want to spawn on, our own.
    coreid_t core = disp_get_core_id();

    grading_printf("test_spawn_rpc(%s)\n", CMDLINE);


    grading_printf("spawn using proc_mgmt client.\n");
    domainid_t pid;
    err = proc_mgmt_spawn_with_cmdline(CMDLINE, core, &pid);
    if (err_is_fail(err)) {
        grading_test_fail("R1-4", "failed to load: %s\n", err_getstring(err));
        return;
    }

    grading_printf("waiting 5 seconds to give the other domain chance to run...\n");
    grading_printf("actually we are going to call our fancy new wait instead\n");
    // int status;
    // aos_rpc_proc_wait(aos_rpc_get_process_channel(),pid,&status);
    // grading_printf("hello exited with status: %d\n", status);
    barrelfish_usleep(5000000);


    grading_printf("calling spawn RPC directly.\n");

    struct aos_rpc *proc_rpc = aos_rpc_get_process_channel();
    if (!proc_rpc) {
        grading_test_fail("R1-4", "no proc_rpc channel set!\n");
        return;
    }
    
    err = aos_rpc_proc_spawn_with_cmdline(proc_rpc, CMDLINE, core, &pid);
    // err = aos_rpc_proc_spawn_with_default_args(proc_rpc, "hello", core, &pid);
    // char* args[5] = {"hello", "these", "are", "the", "args"};
    // err = aos_rpc_proc_spawn_with_caps(proc_rpc, 5, (const char**)args, 0, NULL_CAP, core, &pid);
    if (err_is_fail(err)) {
        grading_test_fail("R1-4", "failed to load: %s\n", err_getstring(err));
        return;
    }

    grading_printf("waiting 5 seconds to give the other domain chance to run...\n");
    grading_printf("actually we are going to call our fancy new wait instead\n");
    //aos_rpc_proc_wait(aos_rpc_get_process_channel(),pid,&status);
    // grading_printf("hello exited with status: %d\n", status);
    barrelfish_usleep(5000000);

    grading_printf("testing get all pids\n");
    domainid_t * pid_array;
    size_t pid_count;
    aos_rpc_proc_get_all_pids(proc_rpc, &pid_array, &pid_count);
    grading_printf("number of pids: %d\n", pid_count);
    grading_printf("printing all pids: \n");
    for (size_t i = 0; i < pid_count - 1; i++)  {
        grading_printf("%d,\n", pid_array[i]);
    }
    grading_printf("%d\n", pid_array[pid_count - 1]);


    grading_printf("testing get pid by name with input: \"hello\"\n");
    domainid_t pid2;
    aos_rpc_proc_get_pid(proc_rpc, "hello", &pid2);
    grading_printf("recieved pid: %d\n", pid2);

    grading_test_pass("R1-4", "test_spawn_rpc\n");
}


int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    grading_printf("rpcclient started...\n");

    test_basic_rpc();
    test_serial_rpc();
    test_serial_getchar();
    test_memory_rpc();
    test_spawn_rpc();

    grading_printf("rpcclient done with tests...\n");

    return EXIT_SUCCESS;
}
