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
#include <aos/kernel_cap_invocations.h>
#include <mm/mm.h>
#include <grading/grading.h>
#include <grading/io.h>
#include <spawn/spawn.h>

#include "coreboot.h"
#include "mem_alloc.h"
#include <proc_mgmt/proc_mgmt.h>
#include "proc_mgmt.h"

#include <barrelfish_kpi/startup_arm.h>

#include <drivers/lpuart.h>
#include <drivers/pl011.h>
#include <drivers/gic_dist.h>
#include <maps/qemu_map.h>
#include <maps/imx8x_map.h>
#include <aos/inthandler.h>

void gen_recv_handler(void *arg)
{
    // debug_printf("received message\n");
    struct lmp_recv_msg msg = LMP_RECV_MSG_INIT;
    struct aos_rpc *rpc = arg;
    errval_t err;
    
    struct capref remote_cap;
    slot_alloc(&remote_cap);
    err = lmp_chan_recv(rpc->lmp_chan, &msg, &remote_cap);
    
    // reregister receive handler
    err = lmp_chan_register_recv(rpc->lmp_chan, get_default_waitset(), MKCLOSURE(gen_recv_handler, arg));
    if (err_is_fail(err)) {
        DEBUG_ERR(err, err_getstring(err));
        return;
    }
        
    // debug_printf("msg words[0]: %d\n", msg.words[0]);
    switch(msg.words[0]) {
        case ACK_MSG:
            // is ack
            debug_printf("why is init receiving acks!?!?\n");
            break;

        case SETUP_MSG:
            // is cap setup message
            rpc->lmp_chan->remote_cap = remote_cap;
            while (err_is_fail(err)) {
                debug_printf("\n\n\nlooks like the code ran\n\n\n");

                // if (!lmp_err_is_transient(err)) {
                //     DEBUG_ERR(err, "registering receive handler\n");
                //     return;
                // }
                // err = lmp_chan_register_recv(rpc->lmp_chan, get_default_waitset(), MKCLOSURE(gen_recv_handler, arg));
                // if (err_is_fail(err)) {
                //     DEBUG_ERR(err, "registering receive handler\n");
                //     return;
                // }
                // err = lmp_chan_recv(rpc->lmp_chan, &msg, &rpc->lmp_chan->remote_cap);
            }

            err = lmp_chan_register_send(rpc->lmp_chan, get_default_waitset(), MKCLOSURE(send_ack_handler, (void *) rpc));
            if (err_is_fail(err)) {
                DEBUG_ERR(err, "registering send handler\n");
                return;
            }
            event_dispatch(get_default_waitset());
            break;

        case NUM_MSG:
            // is num
            while (err_is_fail(err)) {
                debug_printf("\n\n\nlooks like the code ran\n\n\n");

                // if (!lmp_err_is_transient(err)) {
                //     DEBUG_ERR(err, "registering receive handler\n");
                //     return;
                // }
                // err = lmp_chan_register_recv(rpc->lmp_chan, get_default_waitset(), MKCLOSURE(gen_recv_handler, arg));
                // if (err_is_fail(err)) {
                //     DEBUG_ERR(err, "registering receive handler\n");
                //     return;
                // }
                // err = lmp_chan_recv(rpc->lmp_chan, &msg, &rpc->lmp_chan->remote_cap);
            }
            grading_rpc_handle_number(msg.words[1]);
            //debug_printf("here is the number we recieved: %d\n", msg.words[1]);

            err = lmp_chan_register_send(rpc->lmp_chan, get_default_waitset(), MKCLOSURE(send_ack_handler, (void*) rpc));
            if (err_is_fail(err)) {
                DEBUG_ERR(err, "registering send handler\n");
                return;
            }
            event_dispatch(get_default_waitset());
            event_dispatch(get_default_waitset());
            break;
        case STRING_MSG:
            // is string
            // debug_printf("is string\n");
            while (err_is_fail(err)) {
                debug_printf("\n\n\nlooks like the code ran\n\n\n");
                // if (!lmp_err_is_transient(err)) {
                //     DEBUG_ERR(err, "registering receive handler\n");
                //     return;
                // }
                // err = lmp_chan_register_recv(rpc->lmp_chan, get_default_waitset(), MKCLOSURE(gen_recv_handler, arg));
                // if (err_is_fail(err)) {
                //     DEBUG_ERR(err, "registering receive handler\n");
                //     return;
                // }
                // err = lmp_chan_recv(rpc->lmp_chan, &msg, &rpc->lmp_chan->remote_cap);
            }

            // debug_printf("here is the length we recieved: %d\n", msg.words[1]);
            // debug_print_cap_at_capref(remote_cap);
            void *buf;
            err = paging_map_frame_attr(get_current_paging_state(), &buf, msg.words[1], remote_cap, VREGION_FLAGS_READ_WRITE);

            // debug_printf("here is the string we recieved: %s\n", buf);
            grading_rpc_handler_string(buf);

            err = lmp_chan_register_send(rpc->lmp_chan, get_default_waitset(), MKCLOSURE(send_ack_handler, (void*) rpc));
            if (err_is_fail(err)) {
                DEBUG_ERR(err, "registering send handler\n");
                return;
            }
            break;

        case PUTCHAR:
            // putchar
            // debug_printf("recieved putchar message\n");
            while (err_is_fail(err)) {
                debug_printf("\n\n\nlooks like the code ran\n\n\n");
                // if (!lmp_err_is_transient(err)) {
                //     DEBUG_ERR(err, "registering receive handler\n");
                //     return;
                // }
                // err = lmp_chan_register_recv(rpc->lmp_chan, get_default_waitset(), MKCLOSURE(gen_recv_handler, arg));
                // if (err_is_fail(err)) {
                //     DEBUG_ERR(err, "registering receive handler\n");
                //     return;
                // }
                // err = lmp_chan_recv(rpc->lmp_chan, &msg, &rpc->lmp_chan->remote_cap);
            }
            sys_print((char *) &msg.words[1], 1);
            //grading_rpc_handler_serial_putchar(msg.words[1]);
            err = lmp_chan_register_send(rpc->lmp_chan, get_default_waitset(), MKCLOSURE(send_ack_handler, (void*) rpc));
            if (err_is_fail(err)) {
                DEBUG_ERR(err, err_getstring(err));
                return;
            }
            break;

        case GETCHAR:
            // getchar
            // debug_printf("recieved getchar message\n");
            while (err_is_fail(err)) {
                USER_PANIC_ERR(err, "registering receive handler\n");
            }

            char c;
            sys_getchar(&c);
            grading_rpc_handler_serial_getchar();

            // build getchar response message payload
            struct aos_rpc_num_payload *num_payload = malloc(sizeof(struct aos_rpc_num_payload));
            num_payload->rpc = rpc;
            num_payload->val = c;

            err = lmp_chan_register_send(rpc->lmp_chan, get_default_waitset(), MKCLOSURE(send_char_handler, (void*) num_payload));
            if (err_is_fail(err)) {
                DEBUG_ERR(err, err_getstring(err));
                return;
            }

            event_dispatch(get_default_waitset());
            event_dispatch(get_default_waitset());

            break;

        case GET_RAM_CAP:
            // is ram cap
            // debug_printf("is ram cap\n");
            while (err_is_fail(err)) {
                debug_printf("\n\n\nlooks like the code ran\n\n\n");
                // if (!lmp_err_is_transient(err)) {
                //     DEBUG_ERR(err, "registering receive handler\n");
                //     return;
                // }
                // err = lmp_chan_register_recv(rpc->lmp_chan, get_default_waitset(), MKCLOSURE(gen_recv_handler, arg));
                // if (err_is_fail(err)) {
                //     DEBUG_ERR(err, "registering receive handler\n");
                //     return;
                // }
                // err = lmp_chan_recv(rpc->lmp_chan, &msg, &rpc->lmp_chan->remote_cap);
            }

            // debug_printf("here is the request we recieved: bytes: %d alignment: %d\n", msg.words[1],
            //                                                                      msg.words[2]);

            struct aos_rpc_ram_cap_resp_payload* resp = 
                malloc(sizeof(struct aos_rpc_ram_cap_resp_payload)); 
            resp->rpc = rpc;
            resp->ret_cap = NULL_CAP;
            resp->ret_bytes = 0;
            
            // check that process hasn't exceeded mem limit
            struct spawninfo* curr = root;
            while (curr->next != NULL) {
                if (curr->pid == rpc->pid)
                    break;
                curr = curr->next;
            }
            if (curr->pages_allocated + ROUND_UP(msg.words[1], BASE_PAGE_SIZE) / BASE_PAGE_SIZE <= 
                MAX_PROC_PAGES) 
            {
                err = ram_alloc_aligned(&resp->ret_cap, msg.words[1], msg.words[2]);
                if (err_is_fail(err)) {
                    DEBUG_ERR(err, "failed to allocate ram for child process\n");
                    return;
                }
                resp->ret_bytes = ROUND_UP(msg.words[1], BASE_PAGE_SIZE);

                grading_rpc_handler_ram_cap(resp->ret_bytes, msg.words[2]);
            }

            err = lmp_chan_register_send(rpc->lmp_chan, get_default_waitset(), 
                                         MKCLOSURE(send_ram_cap_resp_handler, (void*) resp));
            if (err_is_fail(err)) {
                DEBUG_ERR(err, "registering send handler\n");
                return;
            }
            
            break;

        case SPAWN_CMDLINE:
            // debug_printf("recieved spawn cmdline message\n");
            while (err_is_fail(err)) {
                debug_printf("not useless\n");
            }
            struct aos_rpc_cmdline_payload *payload = malloc(sizeof(struct aos_rpc_cmdline_payload));
            
            // debug_printf("here is the length we recieved: %d\n", msg.words[1]);
            void *buf2;
            err = paging_map_frame_attr(get_current_paging_state(), &buf2, msg.words[1], remote_cap, VREGION_FLAGS_READ_WRITE);

            // debug_printf("here is the string we recieved: %s\n", buf2);
            domainid_t our_pid;
            err = proc_mgmt_spawn_with_cmdline(buf2, msg.words[2], &our_pid);
            if (err_is_fail(err)) {
                debug_printf("spawn failed\n");
                return;
            }
            payload->pid = our_pid;
            payload->rpc = rpc;
            err = lmp_chan_register_send(rpc->lmp_chan, get_default_waitset(), MKCLOSURE(send_pid_handler, (void*) payload));
            grading_rpc_handler_process_spawn(buf2, msg.words[2]);
            break;
        case GET_ALL_PIDS:
            // debug_printf("is get_all_pids message\n");
            while (err_is_fail(err)) {
                debug_printf("\n\n\nlooks like the code ran\n\n\n");
                // if (!lmp_err_is_transient(err)) {
                //     DEBUG_ERR(err, "registering receive handler\n");
                //     return;
                // }
                // err = lmp_chan_register_recv(rpc->lmp_chan, get_default_waitset(), MKCLOSURE(gen_recv_handler, arg));
                // if (err_is_fail(err)) {
                //     DEBUG_ERR(err, "registering receive handler\n");
                //     return;
                // }
                // err = lmp_chan_recv(rpc->lmp_chan, &msg, &rpc->lmp_chan->remote_cap);
            }

            void *buf10;
            err = paging_map_frame_attr(get_current_paging_state(), &buf10, msg.words[1], remote_cap, VREGION_FLAGS_READ_WRITE);
            struct get_all_pids_frame_output * output = (struct get_all_pids_frame_output*) buf10;
            domainid_t * intermediate_pids;
            proc_mgmt_get_proc_list(&intermediate_pids, &output->num_pids);
            for (size_t i = 0; i < output->num_pids; i++) {
                output->pids[i] = intermediate_pids[i];
            }
            err = lmp_chan_register_send(rpc->lmp_chan, get_default_waitset(), MKCLOSURE(send_ack_handler, (void*) rpc));
            if (err_is_fail(err)) {
                DEBUG_ERR(err, "registering send handler\n");
                return;
            }
            break;
        case GET_PID:
            // debug_printf("is get_pid message\n");
            while (err_is_fail(err)) {
                debug_printf("\n\n\nlooks like the code ran\n\n\n");
                // if (!lmp_err_is_transient(err)) {
                //     DEBUG_ERR(err, "registering receive handler\n");
                //     return;
                // }
                // err = lmp_chan_register_recv(rpc->lmp_chan, get_default_waitset(), MKCLOSURE(gen_recv_handler, arg));
                // if (err_is_fail(err)) {
                //     DEBUG_ERR(err, "registering receive handler\n");
                //     return;
                // }
                // err = lmp_chan_recv(rpc->lmp_chan, &msg, &rpc->lmp_chan->remote_cap);
            }

            void *buf11;
            err = paging_map_frame_attr(get_current_paging_state(), &buf11, msg.words[1], remote_cap, VREGION_FLAGS_READ_WRITE);
            struct get_pid_frame_output * output2 = (struct get_pid_frame_output*) buf11;
            // debug_printf("heres the string we recieved: %s\n", buf11);
            proc_mgmt_get_pid_by_name(buf11, &output2->pid);
            // debug_printf("made it to the end of receiving\n");
            err = lmp_chan_register_send(rpc->lmp_chan, get_default_waitset(), MKCLOSURE(send_ack_handler, (void*) rpc));
            if (err_is_fail(err)) {
                DEBUG_ERR(err, "registering send handler\n");
                return;
            }
            break;
        case EXIT_MSG: 
            // debug_printf("is exit message\n");
            while (err_is_fail(err)) {
                debug_printf("\n\n\nlooks like the code ran\n\n\n");
                // if (!lmp_err_is_transient(err)) {
                //     DEBUG_ERR(err, "registering receive handler\n");
                //     return;
                // }
                // err = lmp_chan_register_recv(rpc->lmp_chan, get_default_waitset(), MKCLOSURE(gen_recv_handler, arg));
                // if (err_is_fail(err)) {
                //     DEBUG_ERR(err, "registering receive handler\n");
                //     return;
                // }
                // err = lmp_chan_recv(rpc->lmp_chan, &msg, &rpc->lmp_chan->remote_cap);
            }

            void *buf12;
            err = paging_map_frame_attr(get_current_paging_state(), &buf12, msg.words[1], remote_cap, VREGION_FLAGS_READ_WRITE);
            int status = *((int *) buf12);
            domainid_t pid8 = ((int*)buf12)[1];
            // debug_printf("heres the status we recieved: %d\n", status);
            proc_mgmt_terminated(pid8, status);
            // debug_printf("made it to the end of receiving\n");
            err = lmp_chan_register_send(rpc->lmp_chan, get_default_waitset(), MKCLOSURE(send_ack_handler, (void*) rpc));
            if (err_is_fail(err)) {
                DEBUG_ERR(err, "registering send handler\n");
                return;
            }
            break;
        case WAIT_MSG: 
            // debug_printf("is wait message\n");
            while (err_is_fail(err)) {
                debug_printf("\n\n\nlooks like the code ran\n\n\n");
                // if (!lmp_err_is_transient(err)) {
                //     DEBUG_ERR(err, "registering receive handler\n");
                //     return;
                // }
                // err = lmp_chan_register_recv(rpc->lmp_chan, get_default_waitset(s), MKCLOSURE(gen_recv_handler, arg));
                // if (err_is_fail(err)) {
                //     DEBUG_ERR(err, "registering receive handler\n");
                //     return;
                // }
                // err = lmp_chan_recv(rpc->lmp_chan, &msg, &rpc->lmp_chan->remote_cap);
            }

            void *buf13;
            err = paging_map_frame_attr(get_current_paging_state(), &buf13, msg.words[1], remote_cap, VREGION_FLAGS_READ_WRITE);
            domainid_t pid3 = ((int*)buf13)[0];
            // debug_printf("heres the pid we recieved: %d\n", pid3);
            proc_mgmt_wait(pid3, (int*)buf13);
            // debug_printf("heres the exit code we're sending: %d\n", *((int*)buf13));
            // debug_printf("made it to the end of receiving\n");
            err = lmp_chan_register_send(rpc->lmp_chan, get_default_waitset(), MKCLOSURE(send_ack_handler, (void*) rpc));
            if (err_is_fail(err)) {
                DEBUG_ERR(err, "registering send handler\n");
                return;
            }
            break;
        case SPAWN_WITH_CAPS_MSG:
            // debug_printf("is spawn with caps message\n");
            while (err_is_fail(err)) {
                debug_printf("\n\n\nlooks like the code ran\n\n\n");
                // if (!lmp_err_is_transient(err)) {
                //     DEBUG_ERR(err, "registering receive handler\n");
                //     return;
                // }
                // err = lmp_chan_register_recv(rpc->lmp_chan, get_default_waitset(), MKCLOSURE(gen_recv_handler, arg));
                // if (err_is_fail(err)) {
                //     DEBUG_ERR(err, "registering receive handler\n");
                //     return;
                // }
                // err = lmp_chan_recv(rpc->lmp_chan, &msg, &rpc->lmp_chan->remote_cap);
            }

            void *buf14;
            err = paging_map_frame_attr(get_current_paging_state(), &buf14, msg.words[1], remote_cap, VREGION_FLAGS_READ_WRITE);
            struct spawn_with_caps_frame_input * input = (struct spawn_with_caps_frame_input*) buf14;
            char ** argv = malloc(4096);
            for (int i = 0; i < input->argc; i++) {
                argv[i] = malloc(strlen(input->argv[i] + 1));
                strcpy(argv[i], input->argv[i]);
            }
            domainid_t pid4;
            err = proc_mgmt_spawn_with_caps(input->argc, (const char **) argv, input->capc, &input->cap, input->core, &pid4);
            (input->pid) = pid4;
            if (err_is_fail(err)) {
                debug_printf("spawn with caps failed\n");
            }
            err = lmp_chan_register_send(rpc->lmp_chan, get_default_waitset(), MKCLOSURE(send_ack_handler, (void*) rpc));
            if (err_is_fail(err)) {
                DEBUG_ERR(err, "registering send handler\n");
                return;
            }
            break;
        default:
            // i don't know
            debug_printf("received unknown message type\n");
            abort();
    }

    // allocate a new slot
    // TODO: allocate only when needed
    err = lmp_chan_alloc_recv_slot(rpc->lmp_chan);
}

void send_ack_handler(void *arg)
{
    // debug_printf("sending ack\n");
    struct aos_rpc *rpc = arg;
    struct lmp_chan *chan = rpc->lmp_chan;
    errval_t err;
    err = lmp_chan_send1(chan, 0, NULL_CAP, ACK_MSG);
    while (err_is_fail(err)) {
        debug_printf("\n\n\n\n went into our error while loop\n\n\n\n");
    }

    // debug_printf("ack sent\n");
}

void send_char_handler(void *arg)
{
    //debug_printf("sending char\n");
    struct aos_rpc_num_payload *payload = arg;
    struct aos_rpc *rpc = payload->rpc;
    struct lmp_chan *chan = rpc->lmp_chan;
    char c = payload->val;
    errval_t err;
    err = lmp_chan_send2(chan, 0, NULL_CAP, GETCHAR_ACK, c);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "failed sending char\n");
    }

    free(payload);

    //debug_printf("char sent: %c\n", c);
}

void send_pid_handler(void *arg) {
    // debug_printf("sending our pid\n");
    struct aos_rpc_cmdline_payload *payload = arg;
    struct aos_rpc *rpc = payload->rpc;
    struct lmp_chan *chan = rpc->lmp_chan;
    errval_t err;
    err = lmp_chan_send2(chan, 0, NULL_CAP, PID_ACK, payload->pid);
    while (err_is_fail(err)) {
        debug_printf("\n\n\n\n went into our error while loop\n\n\n\n");
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

    free(payload);

    // debug_printf("ack sent\n");
}

void send_ram_cap_resp_handler(void *arg) 
{
    // debug_printf("sending ram cap response\n");
    struct aos_rpc_ram_cap_resp_payload* resp = arg;
    struct lmp_chan *chan = resp->rpc->lmp_chan;

    // debug_printf("sent ram cap size: %d\n", resp->ret_bytes);
    errval_t err = lmp_chan_send2(chan, 0, resp->ret_cap, RAM_CAP_ACK, resp->ret_bytes);
    while (err_is_fail(err)) {
        debug_printf("\n\n\n\n went into our error while loop\n\n\n\n");
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

    free(resp);

    // debug_printf("ram cap resp sent\n");
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

    // initialize mem allocator, vspace management here

    // calling early grading tests, required functionality up to here:
    //   - allocate memory
    //   - create mappings in the address space
    //   - spawn new processes
    // DO NOT REMOVE THE FOLLOWING LINE!
    grading_test_early();


    switch (platform_info.platform) {
        case PI_PLATFORM_IMX8X: {
            // SPAWN THE SECOND CORE on the IMX8X baord
            err = coreboot_boot_core(1, "boot_armv8_generic", "cpu_imx8x", "init", NULL);
            err = coreboot_boot_core(2, "boot_armv8_generic", "cpu_imx8x", "init", NULL);
            err = coreboot_boot_core(3, "boot_armv8_generic", "cpu_imx8x", "init", NULL);

            break;
        }
        case PI_PLATFORM_QEMU: {
            // SPAWN THE SECOND CORE on QEMU
            err = coreboot_boot_core(1, "boot_armv8_generic", "cpu_a57_qemu", "init", NULL);
            err = coreboot_boot_core(2, "boot_armv8_generic", "cpu_a57_qemu", "init", NULL);
            err = coreboot_boot_core(3, "boot_armv8_generic", "cpu_a57_qemu", "init", NULL);

            break;
        }
        default:
            debug_printf("Unsupported platform\n");
            return LIB_ERR_NOT_IMPLEMENTED;
    }
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "Booting second core failed. Continuing.\n");
    }

    // Spawn system processes, boot second core etc. here

    // initialize UMP channels
    for (int i = 1; i < 4; i++) {
        genvaddr_t ump_addr = (genvaddr_t)get_ump_chan_mon(i, 0);
        ump_chan_init((struct ump_chan *)ump_addr, ROUND_UP(ump_addr, BASE_PAGE_SIZE) - ump_addr);
        ump_addr = (genvaddr_t)get_ump_chan_mon(i, 1);
        ump_chan_init((struct ump_chan *)ump_addr, ROUND_UP(ump_addr, BASE_PAGE_SIZE) - ump_addr + BASE_PAGE_SIZE);
    }

    // calling late grading tests, required functionality up to here:
    //   - full functionality of the system
    // DO NOT REMOVE THE FOLLOWING LINE!
    grading_test_late();

    // get the devframe passed to init
    struct capref devframe;
    devframe.cnode = cnode_task;
    devframe.slot = TASKCN_SLOT_DEV;
    debug_print_cap_at_capref(devframe);

    struct capability devframe_cap;
    err = cap_direct_identify(devframe, &devframe_cap);
    DEBUG_ERR_ON_FAIL(err, "couldn't identify devframe\n");

    // retype the devframe using the base and size in the devices header
    // to return a capref to the UART and GIC registers
    bool qemu = platform_info.platform == PI_PLATFORM_QEMU;
    struct capref uart_frame, gic_frame;
    genvaddr_t uart_base, gic_base;
    err = slot_alloc(&uart_frame);
    DEBUG_ERR_ON_FAIL(err, "couldn't allocate slot for UART frame\n");
    err = slot_alloc(&gic_frame);
    DEBUG_ERR_ON_FAIL(err, "couldn't allocate slot for GIC frame\n");
    uart_base = qemu ? QEMU_UART_BASE : IMX8X_UART3_BASE;
    gic_base = qemu ? QEMU_GIC_DIST_BASE : IMX8X_GIC_DIST_BASE;
    err = cap_retype(uart_frame, devframe, uart_base - devframe_cap.u.devframe.base, ObjType_DevFrame, QEMU_UART_SIZE);
    DEBUG_ERR_ON_FAIL(err, "couldn't retype UART from devframe\n");
    err = cap_retype(gic_frame, devframe, gic_base - devframe_cap.u.devframe.base, ObjType_DevFrame, QEMU_GIC_DIST_SIZE);
    DEBUG_ERR_ON_FAIL(err, "couldn't retype GIC from devframe\n");

    // map the UART and the GIC
    void *uart_buf;
    void *gic_buf;
    err = paging_map_frame_attr(get_current_paging_state(), &uart_buf, QEMU_UART_SIZE, uart_frame, 
                                VREGION_FLAGS_READ_WRITE_NOCACHE);
    DEBUG_ERR_ON_FAIL(err, "couldn't map UART frame\n");
    err = paging_map_frame_attr(get_current_paging_state(), &gic_buf, QEMU_GIC_DIST_SIZE, gic_frame, 
                                VREGION_FLAGS_READ_WRITE_NOCACHE);
    DEBUG_ERR_ON_FAIL(err, "couldn't map GIC frame\n");

    // initialize GIC
    struct gic_dist_s *gic;
    err = gic_dist_init(&gic, gic_buf);
    if (err_is_fail(err)) {
        debug_printf("error: %s\n", err_getstring(err));
        abort();
    }

    // set up interrupt handler
    // TODO: this code doesn't seem to actually do anything...
    struct capref dest_irq;
    slot_alloc(&dest_irq);
    err = inthandler_alloc_dest_irq_cap(PL011_UART0_INT, &dest_irq);
    DEBUG_ERR_ON_FAIL(err, "couldn't get interrupt destination cap\n");
    struct event_closure handler = {
        .handler = NULL,
        .arg = NULL,
    };
    err = inthandler_setup(dest_irq, get_default_waitset(), handler);
    DEBUG_ERR_ON_FAIL(err, "couldn't attach interrupts to handler\n");

    // initialize UART
    if (qemu) {
        struct pl011_s *uart;
        err = pl011_init(&uart, uart_buf);
        DEBUG_ERR_ON_FAIL(err, "couldn't initialize pl011_s\n");
        err = pl011_enable_interrupt(uart);
        DEBUG_ERR_ON_FAIL(err, "unable to enable pl011 interrupts\n");
    } else {
        struct lpuart_s *uart;
        err = lpuart_init(&uart, uart_buf);
        DEBUG_ERR_ON_FAIL(err, "couldn't initialize lpuart\n");
        err = lpuart_enable_interrupt(uart);
        DEBUG_ERR_ON_FAIL(err, "unable to enable lpuart interrupts\n");
    }

    // spawn the shell
    domainid_t shell_pid;
    proc_mgmt_spawn_with_cmdline("shell", 0, &shell_pid);

    // Hang around
    struct waitset *default_ws = get_default_waitset();
    while (true) {
        err = event_dispatch_non_block(default_ws);
        if (err_is_fail(err) && err != LIB_ERR_NO_EVENT) {
            DEBUG_ERR(err, "in event_dispatch");
            abort();
        }

        // poll for UMP messages
        for (int core = 1; core <= 3; core++) {
            struct ump_payload payload;
            err = ump_receive(get_ump_chan_mon(core, 0), SPAWN_CMDLINE, &payload);
            if (err == LIB_ERR_UMP_CHAN_RECV) {
                err = ump_receive(get_ump_chan_mon(core, 0), PID_ACK, &payload);

                // if this ack is for us, put it back, else forward it
                if (payload.recv_core == my_core_id) {
                    err = ump_send(get_ump_chan_mon(payload.send_core, 0), (char *)&payload, sizeof(payload));
                    if (err_is_fail(err)) {
                        debug_printf("couldn't put an ack back on the queue\n");
                        abort();
                    }
                }
            }
            if (err_is_fail(err)) {
                continue;
            }

            domainid_t pid;

            // if this core is the recv core, spawn the process and send an ack
            if (payload.recv_core == my_core_id) {
                err = proc_mgmt_spawn_with_cmdline(payload.payload, payload.recv_core, &pid);
                if (err_is_fail(err)) {
                    debug_printf("couldn't spawn a process\n");
                    abort();
                }

                // setup ack
                struct ump_payload recv_msg;
                recv_msg.type = PID_ACK;
                recv_msg.recv_core = payload.send_core;
                recv_msg.send_core = my_core_id;
                memcpy(&recv_msg.payload, &pid, sizeof(pid));

                // forward the ack to the correct core
                err = ump_send(get_ump_chan_mon(recv_msg.recv_core, 1), (char *)&recv_msg, sizeof(recv_msg));
                if (err_is_fail(err)) {
                    debug_printf("couldn't send an ack\n");
                    abort();
                }

                continue;
            }

            // else, forward the message to the correct core
            err = ump_send(get_ump_chan_mon(payload.recv_core, 1), (char *)&payload, sizeof(payload));
            if (err_is_fail(err)) {
                debug_printf("couldn't forward a message\n");
                abort();
            }
        }

        thread_yield();
    }

    return EXIT_SUCCESS;
}

static int
app_main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    errval_t err;
                   
    // Create the elf module root cnode
    struct capref module_cnode_cslot = {
        .cnode = cnode_root,
        .slot = ROOTCN_SLOT_MODULECN
    };
    struct cnoderef module_cnode_ref;
    err = cnode_create_raw(module_cnode_cslot, &module_cnode_ref,
                           ObjType_L2CNode, L2_CNODE_SLOTS);
    DEBUG_ERR_ON_FAIL(err, "failed to create elf module root on new core");

    // Get urpc frame
    void* urpc_buf;
    err = paging_map_frame_attr(get_current_paging_state(), &urpc_buf, BASE_PAGE_SIZE, cap_urpc, VREGION_FLAGS_READ_WRITE);
    DEBUG_ERR_ON_FAIL(err, "app_main: couldn't map urpc frame\n");

    bi = (struct bootinfo*) urpc_buf;           // janky bootinfo struct with only 1 region

    // Forge cap to ram
    struct capref ram_cap = {
        .cnode = cnode_memory,
        .slot = 0
    };
    err = ram_forge(ram_cap, bi->regions[0].mr_base, bi->regions[0].mr_bytes, my_core_id);
    DEBUG_ERR_ON_FAIL(err, "couldn't get ram from other core\n");

    // Forge caps to every module
    for (int i = 1; i < (int) bi->regions_length; i++) {
        struct capref module_cap = {
            .cnode = cnode_module,
            .slot = bi->regions[i].mrmod_slot,
        };
        //debug_printf("module %d: addr %p, %d bytes\n", i, bi->regions[i].mr_base, bi->regions[i].mrmod_size);
        err = frame_forge(module_cap, bi->regions[i].mr_base, 
                          ROUND_UP(bi->regions[i].mrmod_size, BASE_PAGE_SIZE), my_core_id);
        DEBUG_ERR_ON_FAIL(err, "couldn't forge cap to module\n");
    }

    // Forge cap to module strings
    genpaddr_t* base = urpc_buf + sizeof(struct bootinfo) + ((bi->regions_length) * sizeof(struct mem_region));
    gensize_t* bytes = urpc_buf + sizeof(struct bootinfo) + ((bi->regions_length) * sizeof(struct mem_region)) + sizeof(genpaddr_t);
    err = frame_forge(cap_mmstrings, *base, ROUND_UP(*bytes, BASE_PAGE_SIZE), my_core_id);
    DEBUG_ERR_ON_FAIL(err, "couldn't get module strings from other core\n");

    // Init the mem allocator
    err = initialize_ram_alloc(bi);
    if(err_is_fail(err)){
        USER_PANIC_ERR(err, "initialize_ram_alloc");
    }

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
        err = event_dispatch_non_block(default_ws);

        if (err_is_fail(err) && err != LIB_ERR_NO_EVENT) {
            DEBUG_ERR(err, "in event_dispatch");
            abort();
        }

        // check for a UMP message
        struct ump_payload payload;
        err = ump_receive(get_ump_chan_core(1), SPAWN_CMDLINE, &payload);
        if (err_is_fail(err)) {
            continue;
        }

        // if this core is the recv core, spawn the process and send an ack
        if (payload.recv_core == my_core_id) {
            domainid_t pid;
            err = proc_mgmt_spawn_with_cmdline(payload.payload, payload.recv_core, &pid);
            if (err_is_fail(err)) {
                debug_printf("couldn't spawn a process\n");
                abort();
            }

            // setup ack
            struct ump_payload recv_msg;
            recv_msg.type = PID_ACK;
            recv_msg.recv_core = payload.send_core;
            recv_msg.send_core = my_core_id;
            memcpy(&recv_msg.payload, &pid, sizeof(pid));

            // forward the ack to the correct core
            err = ump_send(get_ump_chan_core(recv_msg.recv_core), (char *)&recv_msg, sizeof(recv_msg));
            if (err_is_fail(err)) {
                debug_printf("couldn't send an ack\n");
                abort();
            }

            continue;
        }

        thread_yield();
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
