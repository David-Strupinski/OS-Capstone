/**
 * \file
 * \brief init process for child spawning
 */

/*
 * Copyright (c) 2007, 2008, 2009, 2010, 2016, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstrasse 6, CH-8092 Zurich. Attn: Systems Group.
 */

#include <stdio.h>
#include <stdlib.h>

#include <aos/aos.h>
#include <aos/morecore.h>
#include <aos/paging.h>
#include <aos/waitset.h>
#include <aos/aos_rpc.h>
#include <mm/mm.h>
#include <grading/grading.h>
#include <grading/io.h>

#include "mem_alloc.h"
#include "coreboot.h"
#include <proc_mgmt/proc_mgmt.h>


void send_ack(struct lmp_chan *lc)
{
    errval_t err;

    err = lmp_chan_alloc_recv_slot(lc);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "allocating receive slot\n");
        abort();
    }

    err = lmp_chan_send1(lc, 0, NULL_CAP, ACK_MSG);
    while (lmp_err_is_transient(err)) {
        err = lmp_chan_send1(lc, 0, NULL_CAP, ACK_MSG);
    }

    if (err_is_fail(err)) {
        printf("\n\n\n\n went into our error while loop\n\n\n\n");
        abort();
    }
}

void gen_recv_handler(void *arg)
{
    printf("received message\n");

    struct lmp_recv_msg msg = LMP_RECV_MSG_INIT;
    struct aos_rpc *rpc = arg;
    errval_t err;

    struct capref remote_cap;
    slot_alloc(&remote_cap);
    err = lmp_chan_recv(rpc->lmp_chan, &msg, &remote_cap);

    switch(msg.words[0]) {
        case ACK_MSG:
            // is ack
            printf("why is init receiving acks!?!?\n");
            abort();
            break;

        case SETUP_MSG:
            // is cap setup message
            rpc->lmp_chan->remote_cap = remote_cap;
            while (err_is_fail(err)) {
                printf("\n\n\nlooks like the code ran\n\n\n");
            }

            // printf("sending ack from setup\n");

            send_ack(rpc->lmp_chan);

            printf("successfully sent ack back from setup\n");

            break;

        case NUM_MSG:
            // is num
            while (err_is_fail(err)) {
                printf("\n\n\nlooks like the code ran\n\n\n");
            }
            grading_rpc_handle_number(msg.words[1]);

            // printf("sending ack from num\n");

            send_ack(rpc->lmp_chan);

            printf("successfully sent ack back from num\n");

            break;
            
        case STRING_MSG:
            // is string
            // printf("is string\n");
            while (err_is_fail(err)) {
                printf("\n\n\nlooks like the code ran\n\n\n");
            }

            void *buf;
            err = paging_map_frame_attr(get_current_paging_state(), &buf, msg.words[1], remote_cap, VREGION_FLAGS_READ_WRITE);
            if (err_is_fail(err)) {
                DEBUG_ERR(err, "mapping frame\n");
                return;
            }

            grading_rpc_handler_string(buf);

            send_ack(rpc->lmp_chan);

            printf("successfully sent ack back from string\n");

            break;

        case PUTCHAR:
            // putchar
            printf("recieved putchar message\n");
            while (err_is_fail(err)) {
                printf("\n\n\nlooks like the code ran\n\n\n");
            }

            putchar(msg.words[1]);
            grading_rpc_handler_serial_putchar(msg.words[1]);

            send_ack(rpc->lmp_chan);

            printf("successfully sent ack back from putchar\n");

            break;

        case GETCHAR:
            // getchar
            printf("recieved getchar message\n");
            while (err_is_fail(err)) {
                USER_PANIC_ERR(err, "registering receive handler\n");
            }
            char c = getchar();
            grading_rpc_handler_serial_getchar();

            // build getchar response message payload
            struct aos_rpc_num_payload *num_payload = malloc(sizeof(struct aos_rpc_num_payload));
            num_payload->rpc = rpc;
            num_payload->val = c;

            err = lmp_chan_register_send(rpc->lmp_chan, get_default_waitset(), MKCLOSURE(send_char_handler, (void*) num_payload));

            break;

        case GET_RAM_CAP:
            break;

        case SPAWN_CMDLINE:
            printf("recieved spawn cmdline message\n");
            while (err_is_fail(err)) {
                printf("not useless\n");
            }
            struct aos_rpc_cmdline_payload *payload = malloc(sizeof(struct aos_rpc_cmdline_payload));
            
            printf("here is the length we recieved: %d\n", msg.words[1]);
            void *buf2;
            err = paging_map_frame_attr(get_current_paging_state(), &buf2, msg.words[1], remote_cap, VREGION_FLAGS_READ_WRITE);

            printf("here is the string we recieved: %s\n", buf2);
            domainid_t our_pid;
            err = proc_mgmt_spawn_with_cmdline(buf2, msg.words[2], &our_pid);
            if (err_is_fail(err)) {
                printf("spawn failed\n");
                return;
            }
            payload->pid = our_pid;
            payload->rpc = rpc;
            err = lmp_chan_register_send(rpc->lmp_chan, get_default_waitset(), MKCLOSURE(send_pid_handler, (void*) payload));
            break;
        default:
            // i don't know
            printf("uh oh I have no idea what this is\n");
    }

    // allocate a new slot
    // err = lmp_chan_alloc_recv_slot(rpc->lmp_chan);
    // if (err_is_fail(err)) {
    //     DEBUG_ERR(err, "allocating receive slot\n");
    //     return;
    // }

    // reregister receive handler
    err = lmp_chan_register_recv(rpc->lmp_chan, get_default_waitset(), MKCLOSURE(gen_recv_handler, arg));
    printf("reregistered receive handler\n");
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "reregistering receive handler\n");
        return;
    }

    event_dispatch(get_default_waitset());
}

void send_ack_handler(void *arg)
{
    printf("sending ack\n");
    struct aos_rpc *rpc = arg;
    struct lmp_chan *chan = rpc->lmp_chan;
    errval_t err;
    err = lmp_chan_send1(chan, 0, NULL_CAP, 0);
    while (err_is_fail(err)) {
        printf("\n\n\n\n went into our error while loop\n\n\n\n");
    }

    printf("ack sent\n");
}

void send_char_handler(void *arg)
{
    printf("sending char\n");
    struct aos_rpc_num_payload *payload = arg;
    struct aos_rpc *rpc = payload->rpc;
    struct lmp_chan *chan = rpc->lmp_chan;
    char c = payload->val;
    errval_t err;
    err = lmp_chan_send2(chan, 0, NULL_CAP, GETCHAR, c);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "failed sending char\n");
    }

    printf("char sent\n");
}

void send_pid_handler(void *arg) {
    printf("sending our pid\n");
    struct aos_rpc_cmdline_payload *payload = arg;
    struct aos_rpc *rpc = payload->rpc;
    struct lmp_chan *chan = rpc->lmp_chan;
    errval_t err;
    err = lmp_chan_send2(chan, 0, NULL_CAP, PID_ACK, payload->pid);
    while (err_is_fail(err)) {
        printf("\n\n\n\n went into our error while loop\n\n\n\n");
        // if (!lmp_err_is_transient(err)) {
        //     DEBUG_ERR(err, "failed sending ack\n");
        //     return;
        // }
        // err = lmp_chan_register_send(chan, get_default_waitset(), MKCLOSURE(send_ack_handler, arg));
        // if (err_is_fail(err)) {
        //     DEBUG_ERR(err, "registering send handler\n");
        //     return;
        // }
        // err = lmp_chan_send1(chan, LMP_SEND_FLAGS_DEFAULT, NULL_CAP, 0);
    }

    printf("ack sent\n");
}

struct bootinfo *bi;

coreid_t my_core_id;
struct platform_info platform_info;




static int
bsp_main(int argc, char *argv[]) {
    errval_t err;

    // initialize the grading/testing subsystem
    // DO NOT REMOVE THE FOLLOWING LINE!
    grading_setup_bsp_init(argc, argv);

    // First argument contains the bootinfo location, if it's not set
    bi = (struct bootinfo*)strtol(argv[1], NULL, 10);
    assert(bi);

    // initialize our RAM allocator
    err = initialize_ram_alloc(bi);
    if(err_is_fail(err)){
        USER_PANIC_ERR(err, "initialize_ram_alloc");
    }

    // TODO: initialize mem allocator, vspace management here

    // calling early grading tests, required functionality up to here:
    //   - allocate memory
    //   - create mappings in the address space
    //   - spawn new processes
    // DO NOT REMOVE THE FOLLOWING LINE!
    grading_test_early();


    switch (platform_info.platform) {
        case PI_PLATFORM_IMX8X: {
            // SPAWN THE SECOND CORE on the IMX8X baord
            hwid_t mpid = 1;
            err = coreboot_boot_core(mpid, "boot_armv8_generic", "cpu_imx8x", "init", NULL);
            break;
        }
        case PI_PLATFORM_QEMU: {
            // SPAWN THE SECOND CORE on QEMU
            hwid_t mpid = 1;
            err = coreboot_boot_core(mpid, "boot_armv8_generic", "cpu_a57_qemu", "init", NULL);
            break;
        }
        default:
            debug_printf("Unsupported platform\n");
            return LIB_ERR_NOT_IMPLEMENTED;
    }
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "Booting second core failed. Continuing.\n");
    }

    // TODO: Spawn system processes, boot second core etc. here

    // calling late grading tests, required functionality up to here:
    //   - full functionality of the system
    // DO NOT REMOVE THE FOLLOWING LINE!
    grading_test_late();

    debug_printf("Message handler loop\n");
    // Hang around
    struct waitset *default_ws = get_default_waitset();
    while (true) {
        err = event_dispatch(default_ws);
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "in event_dispatch");
            abort();
        }
    }

    return EXIT_SUCCESS;
}

static int
app_main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    errval_t err;
    // TODO (M5):
    //   - initialize memory allocator etc.
    //   - obtain a pointer to the bootinfo structure on the appcore!

    // initialize the grading/testing subsystem
    // DO NOT REMOVE THE FOLLOWING LINE!
    grading_setup_app_init(bi);

    // calling early grading tests, required functionality up to here:
    //   - allocate memory
    //   - create mappings in the address space
    //   - spawn new processes
    // DO NOT REMOVE THE FOLLOWING LINE!
    grading_test_early();

    // TODO (M7)
    //  - initialize subsystems for nameservice, distops, ...

    // TODO(M5): signal the other core that we're up and running

    // TODO (M6): initialize URPC

    // calling late grading tests, required functionality up to here:
    //   - full functionality of the system
    // DO NOT REMOVE THE FOLLOWING LINE!
    grading_test_late();

    // Hang around
    struct waitset *default_ws = get_default_waitset();
    while (true) {
        err = event_dispatch(default_ws);
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "in event_dispatch");
            abort();
        }
    }

    return EXIT_SUCCESS;
}

int main(int argc, char *argv[])
{
    errval_t err;

    /* obtain the core information from the kernel*/
    err = invoke_kernel_get_core_id(cap_kernel, &my_core_id);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "failed to obtain the core id from the kernel\n");
    }

    /* Set the core id in the disp_priv struct */
    disp_set_core_id(my_core_id);

    /* obtain the platform information */
    err = invoke_kernel_get_platform_info(cap_kernel, &platform_info);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "failed to obtain the platform info from the kernel\n");
    }

    char *platform;
    switch (platform_info.platform) {
        case PI_PLATFORM_QEMU:
            platform = "QEMU";
            break;
        case PI_PLATFORM_IMX8X:
            platform = "IMX8X";
            break;
        default:
            platform = "UNKNOWN";
    }

    err = cap_retype(cap_selfep, cap_dispatcher, 0, ObjType_EndPointLMP, 0);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_CAP_RETYPE);
    }

    // this print statement should remain here
    grading_printf("init domain starting on core %" PRIuCOREID " (%s)\n", my_core_id, platform);
    fflush(stdout);

    if(my_core_id == 0) return bsp_main(argc, argv);
    else                return app_main(argc, argv);
}
