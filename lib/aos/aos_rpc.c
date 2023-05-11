/**
 * \file
 * \brief RPC Bindings for AOS
 */

/*
 * Copyright (c) 2013-2016, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached license file.
 * if you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstr. 6, CH-8092 Zurich. attn: systems group.
 */

#include <aos/aos.h>
#include <aos/aos_rpc.h>
#include <grading/grading.h>


void send_ack_handler(void *arg)
{
    printf("sending ack\n");
    struct aos_rpc *rpc = arg;
    struct lmp_chan *chan = rpc->lmp_chan;
    errval_t err;
    printf("about to fail\n");
    err = lmp_chan_send1(chan, 0, NULL_CAP, 0);
    printf("made it after the send\n");
    while (err_is_fail(err)) {
        printf("\n\n\n\n went into our error while loop\n\n\n\n");
        if (!lmp_err_is_transient(err)) {
            DEBUG_ERR(err, "failed sending ack\n");
            return;
        }
        err = lmp_chan_register_send(chan, get_default_waitset(), MKCLOSURE(send_ack_handler, arg));
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "registering send handler\n");
            return;
        }
        err = lmp_chan_send1(chan, LMP_SEND_FLAGS_DEFAULT, NULL_CAP, 0);
    }

    printf("ack sent\n");
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
    printf("reset the remote cap\n");
    
    printf("msg words[0]: %d\n", msg.words[0]);
    if (msg.words[0] == 0) {
        // is ack

        printf("received ack\n");
        
        rpc->waiting_on_ack = false;
        printf("heres the address of rpc->waiting_on_ack from the ack pov: %p\n", &(rpc->waiting_on_ack));
        while (err_is_fail(err)) {
            if (!lmp_err_is_transient(err)) {
                DEBUG_ERR(err, "registering receive handler\n");
                return;
            }
            err = lmp_chan_register_recv(rpc->lmp_chan, get_default_waitset(), MKCLOSURE(gen_recv_handler, arg));
            if (err_is_fail(err)) {
                DEBUG_ERR(err, "registering receive handler\n");
                return;
            }
            err = lmp_chan_recv(rpc->lmp_chan, &msg, &NULL_CAP);
        }
    } else if (msg.words[0] == 1) {
        // is cap setup message
        rpc->lmp_chan->remote_cap = remote_cap;
        while (err_is_fail(err)) {
            if (!lmp_err_is_transient(err)) {
                DEBUG_ERR(err, "registering receive handler\n");
                return;
            }
            err = lmp_chan_register_recv(rpc->lmp_chan, get_default_waitset(), MKCLOSURE(gen_recv_handler, arg));
            if (err_is_fail(err)) {
                DEBUG_ERR(err, "registering receive handler\n");
                return;
            }
            err = lmp_chan_recv(rpc->lmp_chan, &msg, &rpc->lmp_chan->remote_cap);
        }

        err = lmp_chan_register_send(rpc->lmp_chan, get_default_waitset(), MKCLOSURE(send_ack_handler, (void *) rpc));
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "registering send handler\n");
            return;
        }

    } else if (msg.words[0] == 2) {
        // is num
        while (err_is_fail(err)) {
            if (!lmp_err_is_transient(err)) {
                DEBUG_ERR(err, "registering receive handler\n");
                return;
            }
            err = lmp_chan_register_recv(rpc->lmp_chan, get_default_waitset(), MKCLOSURE(gen_recv_handler, arg));
            if (err_is_fail(err)) {
                DEBUG_ERR(err, "registering receive handler\n");
                return;
            }
            err = lmp_chan_recv(rpc->lmp_chan, &msg, &rpc->lmp_chan->remote_cap);
        }

        printf("here is the number we recieved: %d\n", msg.words[1]);

        err = lmp_chan_register_send(rpc->lmp_chan, get_default_waitset(), MKCLOSURE(send_ack_handler, (void*) rpc));
    } else if (msg.words[0] == 3) {
        // is string
        printf("is string\n");
        while (err_is_fail(err)) {
            if (!lmp_err_is_transient(err)) {
                DEBUG_ERR(err, "registering receive handler\n");
                return;
            }
            err = lmp_chan_register_recv(rpc->lmp_chan, get_default_waitset(), MKCLOSURE(gen_recv_handler, arg));
            if (err_is_fail(err)) {
                DEBUG_ERR(err, "registering receive handler\n");
                return;
            }
            err = lmp_chan_recv(rpc->lmp_chan, &msg, &rpc->lmp_chan->remote_cap);
        }
        printf("here is the length we recieved: %d\n", msg.words[1]);
        debug_print_cap_at_capref(remote_cap);
        void *buf;
        err = paging_map_frame_attr(get_current_paging_state(), &buf, msg.words[1], remote_cap, VREGION_FLAGS_READ_WRITE);

        printf("here is the string we recieved: %s\n", buf);

        err = lmp_chan_register_send(rpc->lmp_chan, get_default_waitset(), MKCLOSURE(send_ack_handler, (void*) rpc));
    } else {
        // i don't know
        printf("uh oh I have no idea what this is\n");
    }
    

    // reregister receive handler
    err = lmp_chan_register_recv(rpc->lmp_chan, get_default_waitset(), MKCLOSURE(gen_recv_handler, arg));
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "reregistering receive handler\n");
        return;
    }

    // allocate a new slot
    // TODO: allocate only when needed
    err = lmp_chan_alloc_recv_slot(rpc->lmp_chan);
}


/*
 * ===============================================================================================
 * Generic RPCs
 * ===============================================================================================
 */


/**
 * @brief Initialize an aos_rpc struct.
 *
 * @param[in] rpc  The aos_rpc struct to initialize.
 *
 * @returns SYS_ERR_OK on success, or error value on failure
 */
errval_t aos_rpc_init(struct aos_rpc *rpc) {
    rpc->lmp_chan = malloc(sizeof(struct lmp_chan));
    lmp_chan_init(rpc->lmp_chan);

    return SYS_ERR_OK;
}


static void send_num_handler(void *arg)
{
    printf("got into send num handler\n");
    
    errval_t err;
    struct aos_rpc_num_payload *payload = (struct aos_rpc_num_payload *) arg;
    struct aos_rpc *rpc = payload->rpc;
    struct lmp_chan *lc = rpc->lmp_chan;
    uintptr_t num = payload->val;

    rpc->waiting_on_ack = true;

    err = lmp_chan_register_recv(lc, get_default_waitset(), MKCLOSURE(gen_recv_handler, (void *) rpc));

    err = lmp_chan_send2(lc, LMP_SEND_FLAGS_DEFAULT, NULL_CAP, 2, num);
    while (err_is_fail(err)) {
        if (!lmp_err_is_transient(err)) {
            DEBUG_ERR(err, "lmp_chan_send2");
            return;
        }
        err = lmp_chan_register_send(lc, get_default_waitset(), MKCLOSURE(send_num_handler, arg));
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "lmp_chan_register_send");
            return;
        }
        err = lmp_chan_send2(lc, LMP_SEND_FLAGS_DEFAULT, NULL_CAP, 2, num);
    }

    printf("number sent!\n");


    // TODO: fix this waiting on ack loop
    while (rpc->waiting_on_ack) {
        printf("heres the address of rpc->waiting_on_ack from the num sender pov: %p\n", &(rpc->waiting_on_ack));

        printf("in the send num while loop\n");
        event_dispatch(get_default_waitset());
        break;
    }
    printf("after the loop\n");
    rpc->waiting_on_ack = false;
    // err = lmp_chan_register_recv(lc, get_default_waitset(), MKCLOSURE(receive_ack_handler, (void *) rpc));
    // if (err_is_fail(err)) {
    //     DEBUG_ERR(err, "lmp_chan_register_recv");
    //     return;
    // }
}

static void send_string_handler(void *arg)
{
    printf("got into send string handler\n");
    
    errval_t err;

    // unpack the provided string and length
    struct aos_rpc_string_payload *payload = (struct aos_rpc_string_payload *) arg;
    struct aos_rpc *rpc = payload->rpc;
    struct capref frame = payload->frame;
    size_t len = payload->len;
    struct lmp_chan *lc = rpc->lmp_chan;
    printf("printing frame:\n");
    debug_print_cap_at_capref(frame);

    rpc->waiting_on_ack = true;

    err = lmp_chan_register_recv(lc, get_default_waitset(), MKCLOSURE(gen_recv_handler, (void *)rpc));
    err = lmp_chan_send2(lc, LMP_SEND_FLAGS_DEFAULT, frame, 3, len);
    while (err_is_fail(err)) {
        printf("%s\n", err_getstring(err));
        if (!lmp_err_is_transient(err)) {
            DEBUG_ERR(err, "lmp_chan_send2");
            return;
        }
        err = lmp_chan_register_send(lc, get_default_waitset(), MKCLOSURE(send_string_handler, arg));
        printf("registered send\n");
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "lmp_chan_register_send");
            return;
        }
        err = lmp_chan_send2(lc, LMP_SEND_FLAGS_DEFAULT, frame, 3, len);
    }

    printf("string sent!\n");


    // TODO: fix this waiting on ack loop
    while (rpc->waiting_on_ack) {
        printf("heres the address of rpc->waiting_on_ack from the num sender pov: %p\n", &(rpc->waiting_on_ack));

        printf("in the send num while loop\n");
        event_dispatch(get_default_waitset());
        break;
    }
    printf("after the loop\n");
    rpc->waiting_on_ack = false;
    // err = lmp_chan_register_recv(lc, get_default_waitset(), MKCLOSURE(receive_ack_handler, (void *) rpc));
    // if (err_is_fail(err)) {
    //     DEBUG_ERR(err, "lmp_chan_register_recv");
    //     return;
    // }
}


/**
 * @brief Send a single number over an RPC channel.
 *
 * @param[in] chan  the RPC channel to use
 * @param[in] val   the number to send
 *
 * @returns SYS_ERR_OK on success, or error value on failure
 *
 * Channel: init
 */
errval_t aos_rpc_send_number(struct aos_rpc *rpc, uintptr_t num)
{
    // make compiler happy about unused parameters
    (void)rpc;
    (void)num;
    printf("\n\n\nmade it in to send num api\n\n\n");
    printf("here is the number we are trying to send: %d\n", num);
    // TODO: implement functionality to send a number over the channel
    // given channel and wait until the ack gets returned.
    struct lmp_chan *lc = rpc->lmp_chan;
    errval_t err;
    // marshall args into num payload
    struct aos_rpc_num_payload *payload = malloc(sizeof(struct aos_rpc_num_payload));
    payload->rpc = rpc;
    payload->val = num;

    // err = lmp_chan_alloc_recv_slot(lc);
    // DEBUG_ERR_ON_FAIL(err, "lmp_chan_alloc_recv_slot");

    err = lmp_chan_register_send(lc, get_default_waitset(), MKCLOSURE(send_num_handler, (void *) payload));
    DEBUG_ERR_ON_FAIL(err, "lmp_chan_send1");
    event_dispatch(get_default_waitset());
    // err = lmp_chan_register_recv(lc, get_default_waitset(), NOP_CLOSURE);
    // DEBUG_ERR_ON_FAIL(err, "lmp_chan_register_recv");

    // debug_print_cap_at_capref(lc->remote_cap);

    // struct lmp_recv_msg msg = LMP_RECV_MSG_INIT;
    // err = lmp_chan_recv(lc, &msg, NULL);
    // DEBUG_ERR_ON_FAIL(err, "lmp_chan_recv");
    printf("made it to the end of number sending\n");

    // check for an ack
    while (rpc->waiting_on_ack) {
        ;
    }

    return SYS_ERR_OK;
}


/**
 * @brief Send a single number over an RPC channel.
 *
 * @param[in] chan  the RPC channel to use
 * @param[in] val   the string to send
 *
 * @returns SYS_ERR_OK on success, or error value on failure
 *
 * Channel: init
 */
errval_t aos_rpc_send_string(struct aos_rpc *rpc, const char *string)
{
    errval_t err;
    
    // make compiler happy about unused parameters
    (void)rpc;
    (void)string;

    printf("sending string\n");
    struct lmp_chan *lc = rpc->lmp_chan;

    // allocate and map a frame, copying to it the string contents
    struct capref frame;
    void *buf;
    int len = strlen(string);
    err = frame_alloc(&frame, len, NULL);
    DEBUG_ERR_ON_FAIL(err, "couldn't allocate frame for string\n");
    err = paging_map_frame_attr(get_current_paging_state(), &buf, len, frame, VREGION_FLAGS_READ_WRITE);
    strcpy(buf, string);

    // pass the string frame and length in the payload
    struct aos_rpc_string_payload *payload = malloc(sizeof(struct aos_rpc_string_payload));
    payload->rpc = rpc;
    debug_print_cap_at_capref(frame);
    //err = cap_copy(payload->frame, frame);
    payload->frame = frame;
    debug_print_cap_at_capref(payload->frame);
    payload->len = len;

    // send the frame and the length on the channel
    err = lmp_chan_alloc_recv_slot(lc);
    DEBUG_ERR_ON_FAIL(err, "lmp_chan_alloc_recv_slot");
    err = lmp_chan_register_send(lc, get_default_waitset(), MKCLOSURE(send_string_handler, (void *)payload));
    DEBUG_ERR_ON_FAIL(err, "lmp_chan_send1");
    event_dispatch(get_default_waitset());

    // check for an ack
    while (rpc->waiting_on_ack) {
        ;
    }

    return SYS_ERR_OK;
}


/*
 * ===============================================================================================
 * RAM Alloc RPCs
 * ===============================================================================================
 */


/**
 * @brief Request a RAM capability with >= bytes of size
 *
 * @param[in]  chan       the RPC channel to use (memory channel)
 * @param[in]  bytes      minimum number of bytes to request
 * @param[in]  alignment  minimum alignment of the requested RAM capability
 * @param[out] retcap     received capability
 * @param[out] ret_bytes  size of the received capability in bytes
 *
 * @returns SYS_ERR_OK on success, or error value on failure
 *
 * Channel: memory
 */
errval_t aos_rpc_get_ram_cap(struct aos_rpc *rpc, size_t bytes, size_t alignment,
                             struct capref *ret_cap, size_t *ret_bytes)
{
    // make compiler happy about unused parameters
    (void)rpc;
    (void)bytes;
    (void)alignment;
    (void)ret_cap;
    (void)ret_bytes;

    // TODO: implement functionality to request a RAM capability over the
    // given channel and wait until it is delivered.
    // Hint: think about where the received cap will be stored
    return SYS_ERR_OK;
}



/*
 * ===============================================================================================
 * Serial RPCs
 * ===============================================================================================
 */


/**
 * @brief obtains a single character from the serial
 *
 * @param chan  the RPC channel to use (serial channel)
 * @param retc  returns the read character
 *
 * @return SYS_ERR_OK on success, or error value on failure
 */
errval_t aos_rpc_serial_getchar(struct aos_rpc *rpc, char *retc)
{
    // make compiler happy about unused parameters
    (void)rpc;
    (void)retc;

    // TODO implement functionality to request a character from
    // the serial driver.
    return SYS_ERR_OK;
}



/**
 * @brief sends a single character to the serial
 *
 * @param chan  the RPC channel to use (serial channel)
 * @param c     the character to send
 *
 * @return SYS_ERR_OK on success, or error value on failure
 */
errval_t aos_rpc_serial_putchar(struct aos_rpc *rpc, char c)
{
    // make compiler happy about unused parameters
    (void)rpc;
    (void)c;

    // TODO implement functionality to send a character to the
    // serial port.
    return SYS_ERR_OK;
}


/*
 * ===============================================================================================
 * Processes RPCs
 * ===============================================================================================
 */


/**
 * @brief requests a new process to be spawned with the supplied arguments and caps
 *
 * @param[in]  chan    the RPC channel to use (process channel)
 * @param[in]  argc    number of arguments in argv
 * @param[in]  argv    array of strings of the arguments to be passed to the new process
 * @param[in]  capc    the number of capabilities that are being sent
 * @param[in]  cap     capabilities to give to the new process, or NULL_CAP if none
 * @param[in]  core    core on which to spawn the new process on
 * @param[out] newpid  returns the PID of the spawned process
 *
 * @return SYS_ERR_OK on success, or error value on failure
 *
 * Hint: we should be able to send multiple capabilities, but we can only send one.
 *       Think how you could send multiple cappabilities by just sending one.
 */
errval_t aos_rpc_proc_spawn_with_caps(struct aos_rpc *chan, int argc, const char *argv[], int capc,
                                      struct capref cap, coreid_t core, domainid_t *newpid)
{
    // make compiler happy about unused parameters
    (void)chan;
    (void)argc;
    (void)argv;
    (void)capc;
    (void)cap;
    (void)core;
    (void)newpid;

    // TODO: implement the process spawn with caps RPC
    DEBUG_ERR(LIB_ERR_NOT_IMPLEMENTED, "%s not implemented", __FUNCTION__);
    return LIB_ERR_NOT_IMPLEMENTED;
}



/**
 * @brief requests a new process to be spawned with the supplied commandline
 *
 * @param[in]  chan    the RPC channel to use (process channel)
 * @param[in]  cmdline  command line of the new process, including its args
 * @param[in]  core     core on which to spawn the new process on
 * @param[out] newpid   returns the PID of the spawned process
 *
 * @return SYS_ERR_OK on success, or error value on failure
 */
errval_t aos_rpc_proc_spawn_with_cmdline(struct aos_rpc *chan, const char *cmdline, coreid_t core,
                                         domainid_t *newpid)
{
    // make compiler happy about unused parameters
    (void)chan;
    (void)cmdline;
    (void)core;
    (void)newpid;

    // TODO: implement the process spawn with cmdline RPC
    DEBUG_ERR(LIB_ERR_NOT_IMPLEMENTED, "%s not implemented", __FUNCTION__);
    return LIB_ERR_NOT_IMPLEMENTED;
}


/**
 * @brief requests a new process to be spawned with the default arguments
 *
 * @param[in]  chan     the RPC channel to use (process channel)
 * @param[in]  path     name of the binary to be spawned
 * @param[in]  core     core on which to spawn the new process on
 * @param[out] newpid   returns the PID of the spawned process
 *
 * @return SYS_ERR_OK on success, or error value on failure
 */
errval_t aos_rpc_proc_spawn_with_default_args(struct aos_rpc *chan, const char *path, coreid_t core,
                                              domainid_t *newpid)
{
    // make compiler happy about unused parameters
    (void)chan;
    (void)path;
    (void)core;
    (void)newpid;

    // TODO: implement the process spawn with default args RPC
    DEBUG_ERR(LIB_ERR_NOT_IMPLEMENTED, "%s not implemented", __FUNCTION__);
    return LIB_ERR_NOT_IMPLEMENTED;
}

/**
 * @brief obtains a list of PIDs of all processes in the system
 *
 * @param[in]  chan       the RPC channel to use (process channel)
 * @param[out] pids       array of PIDs of all processes in the system (freed by caller)
 * @param[out] pid_count  the number of PIDs in the list
 *
 * @return SYS_ERR_OK on success, or error value on failure
 */
errval_t aos_rpc_proc_get_all_pids(struct aos_rpc *chan, domainid_t **pids, size_t *pid_count)
{
    // make compiler happy about unused parameters
    (void)chan;
    (void)pids;
    (void)pid_count;

    // TODO: implement the process get all PIDs RPC
    DEBUG_ERR(LIB_ERR_NOT_IMPLEMENTED, "%s not implemented", __FUNCTION__);
    return LIB_ERR_NOT_IMPLEMENTED;
}

/**
 * @brief obtains the status of a process
 *
 * @param[in]  chan         the RPC channel to use (process channel)
 * @param[in]  pid          PID of the process to get the status of
 * @param[out] core         core on which the process is running
 * @param[out] cmdline      buffer to store the cmdline in
 * @param[out] cmdline_max  size of the cmdline buffer in bytes
 * @param[out] state        returns the state of the process
 * @param[out] exit_code    returns the exit code of the process (if terminated)
 *
 * @return SYS_ERR_OK on success, or error value on failure
 */
errval_t aos_rpc_proc_get_status(struct aos_rpc *chan, domainid_t pid, coreid_t *core,
                                 char *cmdline, int cmdline_max, uint8_t *state, int *exit_code)
{
    // make compiler happy about unused parameters
    (void)chan;
    (void)pid;
    (void)core;
    (void)cmdline;
    (void)cmdline_max;
    (void)state;
    (void)exit_code;

    // TODO: implement the process get status RPC
    DEBUG_ERR(LIB_ERR_NOT_IMPLEMENTED, "%s not implemented", __FUNCTION__);
    return LIB_ERR_NOT_IMPLEMENTED;
}


/**
 * @brief obtains the name of a process with a given PID
 *
 * @param[in] chan  the RPC channel to use (process channel)
 * @param[in] name  the name of the process to search for
 * @param[in] pid   returns PID of the process to pause/suspend
 *
 * @return SYS_ERR_OK on success, or error value on failure
 */
errval_t aos_rpc_proc_get_name(struct aos_rpc *chan, domainid_t pid, char *name, size_t len)
{
    // make compiler happy about unused parameters
    (void)chan;
    (void)pid;
    (void)name;
    (void)len;

    // TODO: implement the process get name RPC
    DEBUG_ERR(LIB_ERR_NOT_IMPLEMENTED, "%s not implemented", __FUNCTION__);
    return LIB_ERR_NOT_IMPLEMENTED;
}


/**
 * @brief obtains the PID of a process with a given name
 *
 * @param[in]  chan  the RPC channel to use (process channel)
 * @param[in]  name  the name of the process to search for
 * @param[out] pid   returns PID of the process with the given name
 *
 * @return SYS_ERR_OK on success, or error value on failure
 *
 * Note: if there are multiple processes with the same name, the smallest PID should be
 * returned.
 */
errval_t aos_rpc_proc_get_pid(struct aos_rpc *chan, const char *name, domainid_t *pid)
{
    // make compiler happy about unused parameters
    (void)chan;
    (void)name;
    (void)pid;

    // TODO: implement the process get PID RPC
    DEBUG_ERR(LIB_ERR_NOT_IMPLEMENTED, "%s not implemented", __FUNCTION__);
    return LIB_ERR_NOT_IMPLEMENTED;
}


/**
 * @brief pauses or suspends the execution of a running process
 *
 * @param[in] chan  the RPC channel to use (process channel)
 * @param[in] pid   PID of the process to pause/suspend
 *
 * @return SYS_ERR_OK on success, or error value on failure
 */
errval_t aos_rpc_proc_pause(struct aos_rpc *chan, domainid_t pid)
{
    // make compiler happy about unused parameters
    (void)chan;
    (void)pid;

    // TODO: implement the process pause RPC
    DEBUG_ERR(LIB_ERR_NOT_IMPLEMENTED, "%s not implemented", __FUNCTION__);
    return LIB_ERR_NOT_IMPLEMENTED;
}


/**
 * @brief resumes a previously paused process
 *
 * @param[in] chan  the RPC channel to use (process channel)
 * @param[in] pid   PID of the process to resume
 *
 * @return SYS_ERR_OK on success, or error value on failure
 */
errval_t aos_rpc_proc_resume(struct aos_rpc *chan, domainid_t pid)
{
    // make compiler happy about unused parameters
    (void)chan;
    (void)pid;

    // TODO: implement the process resume RPC
    DEBUG_ERR(LIB_ERR_NOT_IMPLEMENTED, "%s not implemented", __FUNCTION__);
    return LIB_ERR_NOT_IMPLEMENTED;
}


/**
 * @brief exists the current process with the supplied exit code
 *
 * @param[in] chan    the RPC channel to use (process channel)
 * @param[in] status  exit status code to send to the process manager.
 *
 * @return SYS_ERR_OK on success, or error value on failure
 *
 * Note: this function does not return, the process manager will halt the process execution.
 */
errval_t aos_rpc_proc_exit(struct aos_rpc *chan, int status)
{
    // make compiler happy about unused parameters
    (void)chan;
    (void)status;

    // TODO: implement the process exit RPC
    DEBUG_ERR(LIB_ERR_NOT_IMPLEMENTED, "%s not implemented", __FUNCTION__);
    return LIB_ERR_NOT_IMPLEMENTED;
}


/**
 * @brief waits for the process with the given PID to exit
 *
 * @param[in]  chan     the RPC channel to use (process channel)
 * @param[in]  pid      PID of the process to wait for
 * @param[out] status   returns the exit status of the process
 *
 * @return SYS_ERR_OK on success, or error value on failure
 *
 * Note: the RPC will only return after the process has exited
 */
errval_t aos_rpc_proc_wait(struct aos_rpc *chan, domainid_t pid, int *status)
{
    // make compiler happy about unused parameters
    (void)chan;
    (void)pid;
    (void)status;

    // TODO: implement the process wait RPC
    DEBUG_ERR(LIB_ERR_NOT_IMPLEMENTED, "%s not implemented", __FUNCTION__);
    return LIB_ERR_NOT_IMPLEMENTED;
}

/**
 * @brief requests that the process with the given PID is terminated
 *
 * @param[in] chan  the RPC channel to use (process channel)
 * @param[in] pid   PID of the process to be terminated
 *
 * @return SYS_ERR_OK on success, or error value on failure
 */
errval_t aos_rpc_proc_kill(struct aos_rpc *chan, domainid_t pid)
{
    // make compiler happy about unused parameters
    (void)chan;
    (void)pid;

    // TODO: implement the process kill RPC
    DEBUG_ERR(LIB_ERR_NOT_IMPLEMENTED, "%s not implemented", __FUNCTION__);
    return LIB_ERR_NOT_IMPLEMENTED;
}


/**
 * @brief requests that all processes that match the supplied name are terminated
 *
 * @param[in] chan  the RPC channel to use (process channel)
 * @param[in] name  name of the processes to be terminated
 *
 * @return SYS_ERR_OK on success, or error value on failure
 */
errval_t aos_rpc_proc_kill_all(struct aos_rpc *chan, const char *name)
{
    // make compiler happy about unused parameters
    (void)chan;
    (void)name;

    // TODO: implement the process killall RPC
    DEBUG_ERR(LIB_ERR_NOT_IMPLEMENTED, "%s not implemented", __FUNCTION__);
    return LIB_ERR_NOT_IMPLEMENTED;
}



/**
 * \brief Returns the RPC channel to init.
 */
struct aos_rpc *aos_rpc_get_init_channel(void)
{
    // TODO: Return channel to talk to init process
    errval_t err;

    struct aos_rpc *rpc = malloc(sizeof(struct aos_rpc));
    if (rpc == NULL) {
        printf("aos_rpc_get_init_channel: malloc failed\n");
        return NULL;
    }
    aos_rpc_init(rpc);

    err = lmp_chan_accept(rpc->lmp_chan, DEFAULT_LMP_BUF_WORDS, cap_initep);

    return rpc;
}

/**
 * \brief Returns the channel to the memory server
 */
struct aos_rpc *aos_rpc_get_memory_channel(void)
{
    // TODO: Return channel to talk to memory server process (or whoever
    // implements memory server functionality)
    return aos_rpc_get_init_channel();
}

/**
 * \brief Returns the channel to the process manager
 */
struct aos_rpc *aos_rpc_get_process_channel(void)
{
    // TODO: Return channel to talk to process server process (or whoever
    // implements process server functionality)
    return aos_rpc_get_init_channel();
}

/**
 * \brief Returns the channel to the serial console
 */
struct aos_rpc *aos_rpc_get_serial_channel(void)
{
    // TODO: Return channel to talk to serial driver/terminal process (whoever
    // implements print/read functionality)
    return aos_rpc_get_init_channel();
}
