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

domainid_t global_pid;

void recv_ack(void *arg)
{
    errval_t err;
    struct lmp_chan *lc = (struct lmp_chan *) arg;
    struct lmp_recv_msg msg = LMP_RECV_MSG_INIT;

    err = lmp_chan_recv(lc, &msg, NULL);
    while (lmp_err_is_transient(err)) {
        debug_printf("in recv ack, got transient error\n");
        err = lmp_chan_recv(lc, &msg, NULL);
    }
    
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "recv ack");
    }

    if (msg.words[0] != 0) {
        debug_printf("\n\n\nreceived something other than an ack\n\n\n");
        abort();
    }
    // debug_printf("received ack!\n\n\n\n");
}

void recv_ramcap_ack(struct lmp_chan *lc, struct capref* ret_cap, size_t* ret_bytes) {
    
    errval_t err;
    struct lmp_recv_msg msg = LMP_RECV_MSG_INIT;
    struct capref retcap;

    err = lmp_chan_recv(lc, &msg, &retcap);
    while (lmp_err_is_transient(err)) {
        err = lmp_chan_recv(lc, &msg, &retcap);
    }
    if (msg.words[0] != RAM_CAP_ACK) {
        printf("\n\n\nreceived something other than a ramcap ack\n\n\n");
        abort();
    }

    debug_print_capref(retcap);
    printf("received ram cap size: %d\n", msg.words[1]);

    if (capref_is_null(retcap)) {
        USER_PANIC("downloading ram failed\n");
    }

    *ret_cap = retcap;
    *ret_bytes = msg.words[1];
}

void recv_getchar_ack(void *arg)
{
    errval_t err;
    struct lmp_recv_msg msg = LMP_RECV_MSG_INIT;

    struct aos_rpc_char_payload *payload = arg;
    struct lmp_chan *lc = payload->rpc->lmp_chan;
    char *retchar = payload->c;

    err = lmp_chan_recv(lc, &msg, NULL);
    while (lmp_err_is_transient(err)) {
        err = lmp_chan_recv(lc, &msg, NULL);
    }
    if (msg.words[0] != GETCHAR_ACK) {
        debug_printf("\n\n\nreceived something other than an ack\n\n\n");
        abort();
    }
    *retchar = msg.words[1];
}


void recv_pid_ack(void *arg)
{
    debug_printf("in recv pid ack\n");
    errval_t err;
    struct lmp_recv_msg msg = LMP_RECV_MSG_INIT;

    struct aos_rpc_cmdline_payload *payload = arg;
    struct lmp_chan *lc = payload->rpc->lmp_chan;
    domainid_t *pid = payload->pid;

    err = lmp_chan_recv(lc, &msg, NULL);
    while (lmp_err_is_transient(err)) {
        err = lmp_chan_recv(lc, &msg, NULL);
    }
    if (msg.words[0] != PID_ACK) {
        debug_printf("\n\n\nreceived something other than an ack: %d\n\n\n", msg.words[0]);
        abort();
    }
    *pid = msg.words[1];
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
 * Note: DOES NOT SET PID. This must be done manually.
 * 
 * @returns SYS_ERR_OK on success, or error value on failure
 */
errval_t aos_rpc_init(struct aos_rpc *rpc) {
    rpc->lmp_chan = malloc(sizeof(struct lmp_chan));
    lmp_chan_init(rpc->lmp_chan);

    return SYS_ERR_OK;
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
    // printf("made it in to send num api\n");
    // printf("here is the number we are trying to send: %d\n", num);
    // TODO: implement functionality to send a number over the channel
    // given channel and wait until the ack gets returned.
    struct lmp_chan *lc = rpc->lmp_chan;
    errval_t err;

    debug_printf("send num: local cap:\n");
    debug_print_cap_at_capref(lc->local_cap);
    err = lmp_chan_send2(lc, LMP_SEND_FLAGS_DEFAULT, rpc->lmp_chan->local_cap, NUM_MSG, num);
    while (lmp_err_is_transient(err)) {
        err = lmp_chan_send2(lc, LMP_SEND_FLAGS_DEFAULT, rpc->lmp_chan->local_cap, NUM_MSG, num);
    }
    DEBUG_ERR_ON_FAIL(err, "send num\n");

    // printf("sent num! waiting for ack\n");

    // err = lmp_chan_register_recv(lc, get_default_waitset(), MKCLOSURE(recv_ack, lc));
    recv_ack(lc);

    debug_printf("made it to the end of number sending\n");

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

    struct lmp_chan *lc = rpc->lmp_chan;

    // allocate and map a frame, copying to it the string contents
    struct capref frame;
    void *buf;
    int len = strlen(string);
    err = frame_alloc(&frame, len, NULL);
    DEBUG_ERR_ON_FAIL(err, "couldn't allocate frame for string\n");
    err = paging_map_frame_attr(get_current_paging_state(), &buf, len, frame, VREGION_FLAGS_READ_WRITE);
    DEBUG_ERR_ON_FAIL(err, "couldn't map frame for string\n");
    strcpy(buf, string);

    // send the frame and the length on the channel
    err = lmp_chan_alloc_recv_slot(lc);
    DEBUG_ERR_ON_FAIL(err, "lmp_chan_alloc_recv_slot");

    debug_printf("in aos rpc, sending string\n");
    err = lmp_chan_send2(lc, LMP_SEND_FLAGS_DEFAULT, frame, STRING_MSG, len);
    while (lmp_err_is_transient(err)) {
        err = lmp_chan_send2(lc, LMP_SEND_FLAGS_DEFAULT, frame, STRING_MSG, len);
    }
    DEBUG_ERR_ON_FAIL(err, "sending string\n");

    debug_printf("sent string! waiting for ack\n");

    // err = lmp_chan_register_recv(lc, get_default_waitset(), MKCLOSURE(recv_ack, lc));
    recv_ack(lc);

    debug_printf("send string complete!\n");

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
    printf("made it in to download ram api\n");
    printf("here is the size we are trying to request: %d\n", bytes);

    errval_t err;

    struct lmp_chan *lc = rpc->lmp_chan;
    
    // send the bytes and alignment on the channel
    err = lmp_chan_alloc_recv_slot(lc);
    DEBUG_ERR_ON_FAIL(err, "lmp_chan_alloc_recv_slot");

    err = lmp_chan_send3(lc, LMP_SEND_FLAGS_DEFAULT, NULL_CAP, GET_RAM_CAP, bytes, alignment);
    while (lmp_err_is_transient(err)) {
        err = lmp_chan_send3(lc, LMP_SEND_FLAGS_DEFAULT, NULL_CAP, GET_RAM_CAP, bytes, alignment);
    }
    DEBUG_ERR_ON_FAIL(err, "sending ram cap request\n");
    
    printf("ram cap request sent!\n");

    recv_ramcap_ack(lc, ret_cap, ret_bytes);

    printf("requesting ramcap complete!\n");

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
    debug_printf("sending getchar\n");

    // TODO implement functionality to request a character from
    // the serial driver.
    struct lmp_chan *lc = rpc->lmp_chan;
    errval_t err;

    err = lmp_chan_send1(lc, LMP_SEND_FLAGS_DEFAULT, rpc->lmp_chan->local_cap, GETCHAR);
    while (lmp_err_is_transient(err)) {
        err = lmp_chan_send1(lc, LMP_SEND_FLAGS_DEFAULT, rpc->lmp_chan->local_cap, GETCHAR);
    }
    DEBUG_ERR_ON_FAIL(err, "send getchar\n");

    // printf("sent getchar! waiting for ack\n");

    char retchar;
    struct aos_rpc_char_payload payload = {
        .rpc = rpc,
        .c = &retchar
    };

    err = lmp_chan_register_recv(lc, get_default_waitset(), MKCLOSURE(recv_getchar_ack, &payload));
    recv_getchar_ack(&payload);

    debug_printf("send getchar complete! Got char %c\n", retchar);

    *retc = retchar;

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
    // printf("made it in to send char api\n");
    // TODO: implement functionality to send a number over the channel
    // given channel and wait until the ack gets returned.
    struct lmp_chan *lc = rpc->lmp_chan;
    errval_t err;

    err = lmp_chan_send2(lc, LMP_SEND_FLAGS_DEFAULT, rpc->lmp_chan->local_cap, PUTCHAR, c);
    while (lmp_err_is_transient(err)) {
        err = lmp_chan_send2(lc, LMP_SEND_FLAGS_DEFAULT, rpc->lmp_chan->local_cap, PUTCHAR, c);
    }
    DEBUG_ERR_ON_FAIL(err, "send char\n");

    // printf("sent char! waiting for ack\n");

    // printf("sent char! waiting for ack\n");
    // err = lmp_chan_register_recv(lc, get_default_waitset(), MKCLOSURE(recv_ack, lc));
    recv_ack(lc);

    // debug_printf("send char complete!\n");

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
errval_t aos_rpc_proc_spawn_with_cmdline(struct aos_rpc *rpc, const char *cmdline, coreid_t core,
                                         domainid_t *newpid)
{
    // make compiler happy about unused parameters
    (void)rpc;
    (void)cmdline;
    (void)core;
    (void)newpid;

    errval_t err;
    
    // printf("entered api for spawn cmdline\n");
    debug_printf("here's the cmdline: %s\n", cmdline);
    struct lmp_chan *lc = rpc->lmp_chan;

    // allocate and map a frame, copying to it the string contents
    struct capref frame;
    void *buf;
    int len = strlen(cmdline);
    // printf("this is the string len: %d\n", len);
    err = frame_alloc(&frame, len, NULL);
    DEBUG_ERR_ON_FAIL(err, "couldn't allocate frame for string\n");
    err = paging_map_frame_attr(get_current_paging_state(), &buf, len, frame, VREGION_FLAGS_READ_WRITE);
    strcpy(buf, cmdline);

    // pass the string frame and length in the payload
    debug_printf("here is the initial value of pid: %d\n", global_pid);

    // send the frame and the length on the channel
    err = lmp_chan_alloc_recv_slot(lc);
    DEBUG_ERR_ON_FAIL(err, "lmp_chan_alloc_recv_slot");
    
    debug_printf("sending spawn with cmdline\n");
    err = lmp_chan_send3(lc, LMP_SEND_FLAGS_DEFAULT, frame, SPAWN_CMDLINE, len, core);
    while (lmp_err_is_transient(err)) {
        err = lmp_chan_send3(lc, LMP_SEND_FLAGS_DEFAULT, frame, SPAWN_CMDLINE, len, core);
    }
    DEBUG_ERR_ON_FAIL(err, "send spawn with cmdline\n");

    debug_printf("sent spawn with cmdline! waiting for ack\n");

    // wait for pid ack
    struct aos_rpc_cmdline_payload payload = {
        .rpc = rpc,
        .pid = newpid
    };

    // err = lmp_chan_register_recv(lc, get_default_waitset(), MKCLOSURE(recv_pid_ack, &payload));
    recv_pid_ack(&payload);
    
    debug_printf("here is the pid we received: %d\n", global_pid);

    return SYS_ERR_OK;
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
        debug_printf("aos_rpc_get_init_channel: malloc failed\n");
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
