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
#include <aos/deferred.h>
#include <grading/grading.h>
#include <barrelfish_kpi/startup_arm.h>
#include <barrelfish_kpi/asm_inlines_arch.h>

#include <proc_mgmt/proc_mgmt.h>

domainid_t global_pid;
char global_retchar;
struct capref global_retcap;
size_t global_retbytes;

struct aos_rpc *global_rpc;

genvaddr_t global_urpc_frames[4];

void setup_send_handler(void *arg)
{
    // debug_printf("sending setup message\n");
    struct aos_rpc *rpc = arg;
    errval_t err;

    err = lmp_chan_register_recv(rpc->lmp_chan, get_default_waitset(), MKCLOSURE(ack_recv_handler, arg));
    // debug_printf("sending msg (not ack)\n");
    err = lmp_chan_send1(rpc->lmp_chan, 0, rpc->lmp_chan->local_cap, SETUP_MSG);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "sending setup message");
        abort();
    }

    // debug_printf("sent setup message\n");
}


void ack_recv_handler(void *arg) {
    // debug_printf("received ack message\n");
    struct lmp_recv_msg msg = LMP_RECV_MSG_INIT;
    struct aos_rpc *rpc = arg;
    errval_t err;
    
    struct capref retcap;
    err = lmp_chan_recv(rpc->lmp_chan, &msg, &retcap);
    
    // debug_printf("ack recv handler: msg words[0]: %d\n", msg.words[0]);
    err = lmp_chan_register_recv(rpc->lmp_chan, get_default_waitset(), MKCLOSURE(ack_recv_handler, arg));

    if (msg.words[0] == PID_ACK) {
        err = lmp_chan_alloc_recv_slot(rpc->lmp_chan);
        // debug_printf("heres the pid: %d\n", msg.words[1]);
        global_pid = msg.words[1];
        return;
    } else if (msg.words[0] == RAM_CAP_ACK) {
        err = lmp_chan_alloc_recv_slot(rpc->lmp_chan);
        // debug_printf("received ram cap size: %d\n", msg.words[1]);
        global_retcap = retcap;
        global_retbytes = msg.words[1];
        return;
    } else if (msg.words[0] == GETCHAR_ACK) {
        err = lmp_chan_alloc_recv_slot(rpc->lmp_chan);
        // debug_printf("received char: %c\n", msg.words[1]);
        global_retchar = msg.words[1];
        return;
    }
    // debug_printf("received ack\n");
        
   // debug_printf("heres the address of rpc->waiting_on_ack from the ack pov: %p\n", &(rpc->waiting_on_ack));
    while (err_is_fail(err)) {
        // debug_printf("\n\n\n\nthis actually ran (ack recv handler)\n\n\n\n\n");
    }

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
    if (rpc->lmp_chan == NULL) {
        return LIB_ERR_MALLOC_FAIL;
    }
    lmp_chan_init(rpc->lmp_chan);

    return SYS_ERR_OK;
}

// get the correct struct ump_chan on the monitor
// direction == 0: core -> monitor
// direction == 1: monitor -> core
struct ump_chan *get_ump_chan_mon(coreid_t core, int direction) {
    // BASE_PAGE_SIZE / 2 should be enough for bootinfo...
    return (struct ump_chan *)(global_urpc_frames[core] + 
            BASE_PAGE_SIZE / 2 + direction * sizeof(struct ump_chan));
}

// get the correct struct ump_chan for a core
// direction == 0: core -> monitor
// direction == 1: monitor -> core
struct ump_chan *get_ump_chan_core(int direction) {
    // BASE_PAGE_SIZE / 2 should be enough for bootinfo...
    return (struct ump_chan *)(MON_URPC_VBASE + 
            BASE_PAGE_SIZE / 2 + direction * sizeof(struct ump_chan));
}

// reset pointers and zero out a struct ump_chan
errval_t ump_chan_init(struct ump_chan *chan, size_t base) {
    chan->base = base;
    chan->head = 0;
    chan->tail = 0;
    chan->size = BASE_PAGE_SIZE;
    memset((void *)((genvaddr_t)chan + (genvaddr_t)chan->base), 0, BASE_PAGE_SIZE);
    return SYS_ERR_OK;
}

void ump_print(struct ump_chan *chan) {
    debug_printf("circular buffer with base %d\n", chan->base);
    for (int i = 0; i < 10; i++) {
        // get the current cache line
        struct cache_line *cl = (struct cache_line *)((genvaddr_t)chan + chan->base + 64 * i);

        if (cl->valid) {
            dmb();
            debug_printf("line of type %d\n", ((struct ump_payload *)(cl->payload))->type);
        } else {
            debug_printf("invalid line\n");
        }
    }
}

// add a message to the ump channel
errval_t ump_send(struct ump_chan *chan, char *buf, size_t size) {
    if (size > 256 * 58) {
        return LIB_ERR_UMP_BUFSIZE_INVALID;
    }
    
    int total_frags = size % 58 == 0 ? size / 58 : (size / 58) + 1;

    dmb();

    for (int frag_num = 0; frag_num < total_frags; frag_num++) {
        struct cache_line *cl = (struct cache_line *)((genvaddr_t)chan + chan->base + chan->head);
        if (!cl->valid) {
            // zero out valid bytes in cache line
            memset((void *)cl, 0, sizeof(struct cache_line));

            // copy data into cache line
            memcpy((void *)cl, buf + frag_num * 58, MIN(58, size - frag_num * 58));

            // set other cache line fields
            cl->frag_num = frag_num;
            cl->total_frags = total_frags;

            // make a memory barrier and set valid flag
            dmb();
            cl->valid = 1;

            // advance head to next cache line
            chan->head = (chan->head + sizeof(struct cache_line)) % chan->size;
        } else {
            // full, panic for now
            USER_PANIC("ump channel full");
            return LIB_ERR_UMP_CHAN_FULL;
        }
    }

    return SYS_ERR_OK;
}

errval_t ump_receive(struct ump_chan *chan, enum msg_type type, void *buf) {
    // if tail == head, there are no messages
    if (chan->tail == chan->head) {
        return LIB_ERR_NO_UMP_MSG;
    }

    // if the tail msg type is not the type we're looking for, return
    struct cache_line *cl = (struct cache_line *)((genvaddr_t)chan + chan->base + chan->tail);
    if (!cl->valid) {
        return LIB_ERR_NO_UMP_MSG;
    }

    dmb();

    if (((struct ump_payload *)cl->payload)->type != type) {
        // need to wait for another message to be dequeued first
        return LIB_ERR_UMP_CHAN_RECV;
    }

    // copy out the received message and dequeue
    for (int frag_num = cl->frag_num; frag_num < cl->total_frags; frag_num++) {
        memcpy(buf + (58 * cl->frag_num), cl->payload, cl->frag_num == cl->total_frags - 1 ? sizeof(struct ump_payload) % 58 : 58);

        // invalidate this cache line
        cl->valid = 0;
        dmb();

        // advance to the next cache line
        cl++;

        // move up tail
        chan->tail = (chan->tail + sizeof(struct cache_line)) % chan->size;
    }

    return SYS_ERR_OK;
}

static void send_num_handler(void *arg)
{
    // debug_printf("got into send num handler\n");
    
    errval_t err;
    struct aos_rpc_num_payload *payload = (struct aos_rpc_num_payload *) arg;
    struct aos_rpc *rpc = payload->rpc;
    struct lmp_chan *lc = rpc->lmp_chan;
    uintptr_t num = payload->val;


    err = lmp_chan_send2(lc, 0, NULL_CAP, NUM_MSG, num);
    while (lmp_err_is_transient(err)) {
        err = lmp_chan_send2(lc, 0, NULL_CAP, NUM_MSG, num);
    }
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "sending num in handler\n");
        abort();
    }

    // debug_printf("number sent!\n");
}

static void send_string_handler(void *arg)
{
    // debug_printf("got into send string handler\n");
    
    errval_t err;

    // unpack the provided string and length
    struct aos_rpc_string_payload *payload = (struct aos_rpc_string_payload *) arg;
    struct aos_rpc *rpc = payload->rpc;
    struct capref frame = payload->frame;
    size_t len = payload->len;
    struct lmp_chan *lc = rpc->lmp_chan;
    // debug_printf("printing frame:\n");
    // debug_print_cap_at_capref(frame);

    err = lmp_chan_send2(lc, 0, frame, STRING_MSG, len);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "sending string in handler\n");
        abort();
    }

    // debug_printf("string sent!\n");
}

static void send_get_name_handler(void *arg)
{
    // debug_printf("got into send string handler\n");
    
    errval_t err;

    // unpack the provided string and length
    struct aos_rpc_string_payload *payload = (struct aos_rpc_string_payload *) arg;
    struct aos_rpc *rpc = payload->rpc;
    struct capref frame = payload->frame;
    size_t len = payload->len;
    struct lmp_chan *lc = rpc->lmp_chan;
    // debug_printf("printing frame:\n");
    // debug_print_cap_at_capref(frame);

    err = lmp_chan_send2(lc, 0, frame, NAME_MSG, len);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "sending name request in handler\n");
        abort();
    }

    // debug_printf("string sent!\n");
}

static void send_getchar_handler(void *arg)
{
    // debug_printf("got into send char handler\n");
    
    errval_t err;
    struct aos_rpc *rpc = arg;
    struct lmp_chan *lc = rpc->lmp_chan;

    err = lmp_chan_send1(lc, 0, NULL_CAP, GETCHAR);
    while (err_is_fail(err)) {
        DEBUG_ERR(err, "sending getchar in handler\n");
        abort();
    }

    // debug_printf("char sent!\n");
}

static void send_putchar_handler(void *arg) {
    // debug_printf("got into send putchar handler\n");
    
    errval_t err;
    struct aos_rpc_num_payload *payload = (struct aos_rpc_num_payload *) arg;
    struct aos_rpc *rpc = payload->rpc;
    struct lmp_chan *lc = rpc->lmp_chan;
    uintptr_t c = payload->val;

    err = lmp_chan_send2(lc, 0, NULL_CAP, PUTCHAR, c);
    while (err_is_fail(err)) {
        DEBUG_ERR(err, "sending putchar in handler\n");
        abort();
    }
}

static void send_spawn_with_caps_handler(void * arg) {
    // debug_printf("got into spawn with caps request send handler\n");
    
    errval_t err;

    // unpack the provided string and length
    struct aos_rpc_string_payload *payload = (struct aos_rpc_string_payload *) arg;
    struct aos_rpc *rpc = payload->rpc;
    struct capref frame = payload->frame;
    size_t len = payload->len;
    struct lmp_chan *lc = rpc->lmp_chan;

    err = lmp_chan_send2(lc, 0, frame, SPAWN_WITH_CAPS_MSG, len);
    while (err_is_fail(err)) {
        DEBUG_ERR(err, "sending spawn with caps request in handler\n");
        abort();
    }

    // debug_printf("spawn with caps request sent!\n");
}

static void send_cmdline_handler(void* arg) {
    // debug_printf("got into send cmdline handler\n");
    
    errval_t err;

    // unpack the provided string and length
    struct aos_rpc_cmdline_payload *payload = (struct aos_rpc_cmdline_payload *) arg;
    struct aos_rpc *rpc = payload->rpc;
    struct capref frame = payload->frame;
    size_t len = payload->len;
    struct lmp_chan *lc = rpc->lmp_chan;

    err = lmp_chan_send3(lc, 0, frame, SPAWN_CMDLINE, len, payload->core);
    while (err_is_fail(err)) {
        DEBUG_ERR(err, "sending cmdline in handler\n");
        abort();
    }

    // debug_printf("cmdline sent!\n");
}

static void send_ram_cap_req_handler(void* arg) {
    // debug_printf("got into send ram cap req handler\n");
    
    errval_t err;

    struct aos_rpc_ram_cap_req_payload *payload = (struct aos_rpc_ram_cap_req_payload *) arg;
    struct aos_rpc *rpc = payload->rpc;
    struct lmp_chan *lc = rpc->lmp_chan;

    err = lmp_chan_send3(lc, 0, NULL_CAP, GET_RAM_CAP, payload->bytes, payload->alignment);
    while (err_is_fail(err)) {
        DEBUG_ERR(err, "sending ram cap req in handler\n");
        abort();
    }

    // debug_printf("ram cap request sent!\n");
}

static void send_get_all_pids_handler(void* arg) {
    // debug_printf("got into send get all pids handler\n");
    
    errval_t err;

    // unpack the provided string and length
    struct aos_rpc_string_payload *payload = (struct aos_rpc_string_payload *) arg;
    struct aos_rpc *rpc = payload->rpc;
    struct capref frame = payload->frame;
    size_t len = payload->len;
    struct lmp_chan *lc = rpc->lmp_chan;
    // debug_printf("printing frame:\n");
    // debug_print_cap_at_capref(frame);

    err = lmp_chan_send2(lc, 0, frame, GET_ALL_PIDS, len);
    while (err_is_fail(err)) {
        DEBUG_ERR(err, "sending get all pids request in handler\n");
        abort();
    }

    // debug_printf("get_all_pids request sent!\n");
}

static void send_get_elf_mod_names_handler(void* arg) {
    // debug_printf("got into send get elf mod names handler\n");
    
    errval_t err;

    // unpack the provided string and length
    struct aos_rpc_string_payload *payload = (struct aos_rpc_string_payload *) arg;
    struct aos_rpc *rpc = payload->rpc;
    struct capref frame = payload->frame;
    size_t len = payload->len;
    struct lmp_chan *lc = rpc->lmp_chan;
    // debug_printf("printing frame:\n");
    // debug_print_cap_at_capref(frame);

    err = lmp_chan_send2(lc, 0, frame, GET_MOD_NAMES, len);
    while (err_is_fail(err)) {
        DEBUG_ERR(err, "sending get elf mod names request in handler\n");
        abort();
    }

    // debug_printf("get_elf_mod_names request sent!\n");
}

static void send_get_pid_handler(void *arg) {
    // debug_printf("got into send get pid handler\n");
    
    errval_t err;

    // unpack the provided string and length
    struct aos_rpc_string_payload *payload = (struct aos_rpc_string_payload *) arg;
    struct aos_rpc *rpc = payload->rpc;
    struct capref frame = payload->frame;
    size_t len = payload->len;
    struct lmp_chan *lc = rpc->lmp_chan;
    // debug_printf("printing frame:\n");
    // debug_print_cap_at_capref(frame);

    err = lmp_chan_send2(lc, 0, frame, GET_PID, len);
    while (err_is_fail(err)) {
        DEBUG_ERR(err, "sending get pid request in handler\n");
        abort();
    }

    // debug_printf("get pid request sent!\n");
}

static void send_exit_handler(void * arg) {
    // debug_printf("got into send exit handler\n");
    
    errval_t err;

    // unpack the provided string and length
    struct aos_rpc_string_payload *payload = (struct aos_rpc_string_payload *) arg;
    struct aos_rpc *rpc = payload->rpc;
    struct capref frame = payload->frame;
    size_t len = payload->len;
    struct lmp_chan *lc = rpc->lmp_chan;
    // debug_printf("printing frame:\n");
    // debug_print_cap_at_capref(frame);

    err = lmp_chan_send2(lc, 0, frame, EXIT_MSG, len);
    while (err_is_fail(err)) {
        DEBUG_ERR(err, "sending exit request in handler\n");
        abort();
    }

    // debug_printf("exit request sent!\n");
}

static void send_wait_handler(void *arg) {
    // debug_printf("got into send wait handler\n");
    
    errval_t err;

    // unpack the provided string and length
    struct aos_rpc_string_payload *payload = (struct aos_rpc_string_payload *) arg;
    struct aos_rpc *rpc = payload->rpc;
    struct capref frame = payload->frame;
    size_t len = payload->len;
    struct lmp_chan *lc = rpc->lmp_chan;
    // debug_printf("printing frame:\n");
    // debug_print_cap_at_capref(frame);

    err = lmp_chan_send2(lc, 0, frame, WAIT_MSG, len);
    while (err_is_fail(err)) {
        DEBUG_ERR(err, "sending wait request in handler\n");
        abort();
    }

    // debug_printf("wait request sent!\n");
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
    struct lmp_chan *lc = rpc->lmp_chan;
    errval_t err;

    // marshall args into num payload
    struct aos_rpc_num_payload *payload = malloc(sizeof(struct aos_rpc_num_payload));
    payload->rpc = rpc;
    payload->val = num;
    err = lmp_chan_register_send(lc, get_default_waitset(), MKCLOSURE(send_num_handler, (void *) payload));
    DEBUG_ERR_ON_FAIL(err, "lmp_chan_send1");
    
    // debug_printf("made it to the end of number sending\n");
    event_dispatch(get_default_waitset());
    event_dispatch(get_default_waitset());
    
    free(payload);

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

    // debug_printf("sending string\n");
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
    // debug_print_cap_at_capref(frame);
    //err = cap_copy(payload->frame, frame);
    payload->frame = frame;
    // debug_print_cap_at_capref(payload->frame);
    payload->len = len;

    // send the frame and the length on the channel
    err = lmp_chan_alloc_recv_slot(lc);
    DEBUG_ERR_ON_FAIL(err, "lmp_chan_alloc_recv_slot");
    err = lmp_chan_register_send(lc, get_default_waitset(), MKCLOSURE(send_string_handler, (void *)payload));
    DEBUG_ERR_ON_FAIL(err, "lmp_chan_send1");
    event_dispatch(get_default_waitset());
    event_dispatch(get_default_waitset());
    // check for an ack
   
    free(payload);

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
    // debug_printf("made it in to download ram api\n");
    // debug_printf("here is the size we are trying to request: %d\n", bytes);

    struct lmp_chan *lc = rpc->lmp_chan;
    errval_t err;
    
    // marshall args into num payload
    struct aos_rpc_ram_cap_req_payload payload;
    payload.rpc = rpc;
    payload.bytes = bytes;
    payload.alignment = alignment;

    err = lmp_chan_register_send(lc, get_default_waitset(), MKCLOSURE(send_ram_cap_req_handler, 
                                 (void *) &payload));
    DEBUG_ERR_ON_FAIL(err, "lmp_chan_send1");
    
    // debug_printf("made it to the end of ram cap request sending\n");
    event_dispatch(get_default_waitset());
    event_dispatch(get_default_waitset());

    if (capref_is_null(global_retcap)) {
        debug_printf("downloading ram failed\n");
        return LIB_ERR_RAM_ALLOC;
    }

    *ret_cap = global_retcap;
    *ret_bytes = global_retbytes;
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
    // TODO implement functionality to request a character from
    // the serial driver.
    struct lmp_chan *lc = rpc->lmp_chan;
    errval_t err;

    err = lmp_chan_register_send(lc, get_default_waitset(), MKCLOSURE(send_getchar_handler, (void *) rpc));
    DEBUG_ERR_ON_FAIL(err, "lmp_chan_send1");
    event_dispatch(get_default_waitset());
    event_dispatch(get_default_waitset());
    *retc = global_retchar;

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
    // TODO: implement functionality to send a number over the channel
    // given channel and wait until the ack gets returned.
    struct lmp_chan *lc = rpc->lmp_chan;
    errval_t err;
    // marshall args into num payload
    struct aos_rpc_num_payload payload;
    payload.rpc = rpc;
    payload.val = c;
    err = lmp_chan_register_send(lc, get_default_waitset(), MKCLOSURE(send_putchar_handler, (void *)&payload));
    DEBUG_ERR_ON_FAIL(err, "lmp_chan_send1");
    
    // debug_printf("made it to the end of putchar sending\n");
    event_dispatch(get_default_waitset());
    event_dispatch(get_default_waitset());

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
errval_t aos_rpc_proc_spawn_with_caps(struct aos_rpc *rpc, int argc, const char *argv[], int capc,
                                      struct capref cap, coreid_t core, domainid_t *newpid)
{
    // make compiler happy about unused parameters
    (void)rpc;
    (void)argc;
    (void)argv;
    (void)capc;
    (void)cap;
    (void)core;
    (void)newpid;

    for (int i = 0; i < argc; i++) {
        debug_printf("arg %d: %s\n", i, argv[i]);
    }
    struct lmp_chan *lc = rpc->lmp_chan;
    errval_t err;

    // allocate and map a frame, copying to it the string contents
    struct capref frame;
    void *buf;
    err = frame_alloc(&frame, BASE_PAGE_SIZE, NULL);
    DEBUG_ERR_ON_FAIL(err, "couldn't allocate frame for string\n");
    err = paging_map_frame_attr(get_current_paging_state(), &buf, BASE_PAGE_SIZE, frame, VREGION_FLAGS_READ_WRITE);
    struct spawn_with_caps_frame_input * input = (struct spawn_with_caps_frame_input *) buf;
    input->argc = argc;
    for (int i = 0; i < argc; i++) {
        strcpy(input->argv[i], argv[i]);
    }
    input->capc = capc;
    input->cap = cap;
    input->core = core;
    
    // pass the string frame and length in the payload
    struct aos_rpc_string_payload *payload = malloc(sizeof(struct aos_rpc_string_payload));
    payload->rpc = rpc;
    payload->frame = frame;
    payload->len = BASE_PAGE_SIZE;

    // send the frame and the length on the channel
    err = lmp_chan_alloc_recv_slot(lc);
    DEBUG_ERR_ON_FAIL(err, "lmp_chan_alloc_recv_slot");
    err = lmp_chan_register_send(lc, get_default_waitset(), MKCLOSURE(send_spawn_with_caps_handler, (void *)payload));
    DEBUG_ERR_ON_FAIL(err, "lmp_chan_send1");
    event_dispatch(get_default_waitset());
    event_dispatch(get_default_waitset());

    *newpid = input->pid;
    // printf("pid of the new process: %d\n", input->pid);
    free(payload);
    return SYS_ERR_OK;
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

    // debug_printf("entered api for spawn cmdline\n");
    // debug_printf("here's the cmdline: %s\n", cmdline);
    struct lmp_chan *lc = rpc->lmp_chan;

    // allocate and map a frame, copying to it the string contents
    struct capref frame;
    void *buf;
    int len = strlen(cmdline);
    // debug_printf("this is the string len: %d\n", len);
    err = frame_alloc(&frame, BASE_PAGE_SIZE, NULL);
    DEBUG_ERR_ON_FAIL(err, "couldn't allocate frame for string\n");
    err = paging_map_frame_attr(get_current_paging_state(), &buf, BASE_PAGE_SIZE, frame, VREGION_FLAGS_READ_WRITE);
    DEBUG_ERR_ON_FAIL(err, "couldn't map frame for string\n");
    strcpy(buf, cmdline);

    // pass the string frame and length in the payload
    struct aos_rpc_cmdline_payload *payload = malloc(sizeof(struct aos_rpc_cmdline_payload));
    payload->rpc = rpc;
    payload->frame = frame;
    payload->len = len;
    payload->core = core;
    // debug_printf("here is the initial value of pid: %d\n", global_pid);

    // send the frame and the length on the channel
    err = lmp_chan_alloc_recv_slot(lc);
    DEBUG_ERR_ON_FAIL(err, "lmp_chan_alloc_recv_slot");
    //err = lmp_chan_register_recv(lc, get_default_waitset(), MKCLOSURE(pid_recv_handler,     (void *)payload));
    err = lmp_chan_register_send(lc, get_default_waitset(), MKCLOSURE(send_cmdline_handler, (void *)payload));
    DEBUG_ERR_ON_FAIL(err, "lmp_chan_send1");
    event_dispatch(get_default_waitset());
    event_dispatch(get_default_waitset());
    // check for an ack
    
    // debug_printf("here is the pid we recieved: %d\n", global_pid);
    *newpid = global_pid;

    free(payload);
    
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

    return aos_rpc_proc_spawn_with_cmdline(chan, path, core, newpid);
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
errval_t aos_rpc_proc_get_all_pids(struct aos_rpc *rpc, domainid_t **pids, size_t *pid_count)
{
    // make compiler happy about unused parameters
    (void)rpc;
    (void)pids;
    (void)pid_count;
    struct lmp_chan *lc = rpc->lmp_chan;
    errval_t err;

    // allocate and map a frame, copying to it the string contents
    struct capref frame;
    void *buf;
    err = frame_alloc(&frame, BASE_PAGE_SIZE, NULL);
    DEBUG_ERR_ON_FAIL(err, "couldn't allocate frame for string\n");
    err = paging_map_frame_attr(get_current_paging_state(), &buf, BASE_PAGE_SIZE, frame, VREGION_FLAGS_READ_WRITE);

    // pass the string frame and length in the payload
    struct aos_rpc_string_payload *payload = malloc(sizeof(struct aos_rpc_string_payload));
    payload->rpc = rpc;
    payload->frame = frame;
    payload->len = BASE_PAGE_SIZE;

    // send the frame and the length on the channel
    err = lmp_chan_alloc_recv_slot(lc);
    DEBUG_ERR_ON_FAIL(err, "lmp_chan_alloc_recv_slot");
    err = lmp_chan_register_send(lc, get_default_waitset(), MKCLOSURE(send_get_all_pids_handler, (void *)payload));
    DEBUG_ERR_ON_FAIL(err, "lmp_chan_send1");
    event_dispatch(get_default_waitset());
    event_dispatch(get_default_waitset());

    // verify contents of frame
    struct get_all_pids_frame_output* output = (struct get_all_pids_frame_output*) buf;
    // debug_printf("here is the number of pids: %d\n", output->num_pids);
    // debug_printf("here are the pids:\n");
    *pids = output->pids;
    *pid_count = output->num_pids;

    free(payload);
    return SYS_ERR_OK;
}

/**
 * @brief obtains a list of all elf memory modules in the system
 *
 * @param[in]  chan       the RPC channel to use (process channel)
 * @param[out] names       array of strings of all elf mem module names in the system (freed by caller)
 * @param[out] name_count  the number of names in the list
 *
 * @return SYS_ERR_OK on success, or error value on failure
 */
errval_t aos_rpc_list_elf_mod_names(struct aos_rpc *rpc, 
                                    char (**names)[][MOD_NAME_LEN], 
                                    int *name_count)
{
    // make compiler happy about unused parameters
    (void)rpc;
    (void)names;
    (void)name_count;
    struct lmp_chan *lc = rpc->lmp_chan;
    errval_t err;

    // allocate and map a frame, hijacking the aos string payload
    struct capref frame;
    void *buf;
    err = frame_alloc(&frame, BASE_PAGE_SIZE, NULL);
    DEBUG_ERR_ON_FAIL(err, "couldn't allocate frame for elf module names array\n");
    err = paging_map_frame_attr(get_current_paging_state(), &buf, BASE_PAGE_SIZE, frame, VREGION_FLAGS_READ_WRITE);

    struct aos_rpc_string_payload *payload = malloc(sizeof(struct aos_rpc_string_payload));
    payload->rpc = rpc;
    payload->frame = frame;
    payload->len = BASE_PAGE_SIZE;

    // send the frame and the length on the channel
    err = lmp_chan_alloc_recv_slot(lc);
    DEBUG_ERR_ON_FAIL(err, "lmp_chan_alloc_recv_slot");
    err = lmp_chan_register_send(lc, get_default_waitset(), 
                                 MKCLOSURE(send_get_elf_mod_names_handler, (void *)payload));
    DEBUG_ERR_ON_FAIL(err, "lmp_chan_send1");
    event_dispatch(get_default_waitset());
    event_dispatch(get_default_waitset());

    // verify contents of frame
    struct get_elf_mod_names_output* output_mod_names = (struct get_elf_mod_names_output*) buf;
    // debug_printf("here is the number of elf mod names: %d\n", output_mod_names->num_names);
    // debug_printf("here are the names:\n");
    *names = &output_mod_names->names;
    *name_count = output_mod_names->num_names;

    free(payload);
    return SYS_ERR_OK;
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
errval_t aos_rpc_proc_get_name(struct aos_rpc *chan, domainid_t pid, char **name)
{
    // make compiler happy about unused parameters
    (void)chan;
    (void)pid;
    (void)name;

    struct lmp_chan *lc = chan->lmp_chan;
    errval_t err;

    // allocate and map a frame, copying to it the string contents
    struct capref frame;
    void *buf;
    err = frame_alloc(&frame, BASE_PAGE_SIZE, NULL);
    DEBUG_ERR_ON_FAIL(err, "couldn't allocate frame for name\n");
    err = paging_map_frame_attr(get_current_paging_state(), &buf, BASE_PAGE_SIZE, frame, VREGION_FLAGS_READ_WRITE);

    // pass the string frame and length in the payload
    struct aos_rpc_string_payload *payload = malloc(sizeof(struct aos_rpc_string_payload));
    payload->rpc = chan;
    payload->frame = frame;
    payload->len = pid;

    // send the frame and the length on the channel
    err = lmp_chan_alloc_recv_slot(lc);
    DEBUG_ERR_ON_FAIL(err, "lmp_chan_alloc_recv_slot");
    err = lmp_chan_register_send(lc, get_default_waitset(), MKCLOSURE(send_get_name_handler, (void *)payload));
    DEBUG_ERR_ON_FAIL(err, "lmp_chan_send1");
    event_dispatch(get_default_waitset());
    event_dispatch(get_default_waitset());

    // set name and return
    *name = buf;
    return SYS_ERR_OK;
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
errval_t aos_rpc_proc_get_pid(struct aos_rpc *rpc, const char *name, domainid_t *pid)
{
    // make compiler happy about unused parameters
    (void)rpc;
    (void)name;
    (void)pid;

    struct lmp_chan *lc = rpc->lmp_chan;
    errval_t err;

    // allocate and map a frame, copying to it the string contents
    struct capref frame;
    void *buf;
    err = frame_alloc(&frame, BASE_PAGE_SIZE, NULL);
    DEBUG_ERR_ON_FAIL(err, "couldn't allocate frame for string\n");
    err = paging_map_frame_attr(get_current_paging_state(), &buf, BASE_PAGE_SIZE, frame, VREGION_FLAGS_READ_WRITE);
    strcpy(buf, name);
    // pass the string frame and length in the payload
    struct aos_rpc_string_payload *payload = malloc(sizeof(struct aos_rpc_string_payload));
    payload->rpc = rpc;
    payload->frame = frame;
    payload->len = BASE_PAGE_SIZE;

    // send the frame and the length on the channel
    err = lmp_chan_alloc_recv_slot(lc);
    DEBUG_ERR_ON_FAIL(err, "lmp_chan_alloc_recv_slot");
    err = lmp_chan_register_send(lc, get_default_waitset(), MKCLOSURE(send_get_pid_handler, (void *)payload));
    DEBUG_ERR_ON_FAIL(err, "lmp_chan_send1");
    event_dispatch(get_default_waitset());
    event_dispatch(get_default_waitset());

    // verify contents of frame
    struct get_pid_frame_output* output = (struct get_pid_frame_output*) buf;
    *pid = output->pid;

    free(payload);
    return SYS_ERR_OK;
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
errval_t aos_rpc_proc_exit(struct aos_rpc *rpc, int status)
{
    // make compiler happy about unused parameters
    (void)rpc;
    (void)status;
    // TODO: implement the process exit RPC

    // I think this must be bonus becasue it involves killing a process
    // and we aren't able to do that
    // debug_printf("made it into aos_rpc_proc_exit\n");
    
    struct lmp_chan *lc = rpc->lmp_chan;
    errval_t err;
    // debug_printf("here's the status we're sending: %d\n", status);
    // debug_printf("here is our pid: %d\n", disp_get_domain_id());
    // allocate and map a frame, copying to it the string contents
    struct capref frame;
    void *buf;
    err = frame_alloc(&frame, BASE_PAGE_SIZE, NULL);
    DEBUG_ERR_ON_FAIL(err, "couldn't allocate frame for string\n");
    err = paging_map_frame_attr(get_current_paging_state(), &buf, BASE_PAGE_SIZE, frame, VREGION_FLAGS_READ_WRITE);
    *((int *) buf) = status;
    ((int *) buf)[1] = disp_get_domain_id();
    // pass the string frame and length in the payload
    struct aos_rpc_string_payload *payload = malloc(sizeof(struct aos_rpc_string_payload));
    payload->rpc = rpc;
    payload->frame = frame;
    payload->len = BASE_PAGE_SIZE;

    // send the frame and the length on the channel
    err = lmp_chan_alloc_recv_slot(lc);
    DEBUG_ERR_ON_FAIL(err, "lmp_chan_alloc_recv_slot");
    err = lmp_chan_register_send(lc, get_default_waitset(), MKCLOSURE(send_exit_handler, (void *)payload));
    DEBUG_ERR_ON_FAIL(err, "lmp_chan_send1");
    event_dispatch(get_default_waitset());
    event_dispatch(get_default_waitset());

    // debug_printf("looks like we've been killed alright\n");

    free(payload);

    return SYS_ERR_OK;
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
errval_t aos_rpc_proc_wait(struct aos_rpc *rpc, domainid_t pid, int *status)
{
    bool terminated = false;
    do {
        struct lmp_chan *lc = rpc->lmp_chan;
        errval_t err;

        // allocate and map a frame, copying to it the string contents
        struct capref frame;
        void *buf;
        err = frame_alloc(&frame, BASE_PAGE_SIZE, NULL);
        DEBUG_ERR_ON_FAIL(err, "couldn't allocate frame for string\n");
        err = paging_map_frame_attr(get_current_paging_state(), &buf, BASE_PAGE_SIZE, frame, VREGION_FLAGS_READ_WRITE);
        *((domainid_t*) buf) = pid;
        // pass the string frame and length in the payload
        struct aos_rpc_string_payload *payload = malloc(sizeof(struct aos_rpc_string_payload));
        payload->rpc = rpc;
        payload->frame = frame;
        payload->len = BASE_PAGE_SIZE;

        // send the frame and the length on the channel
        err = lmp_chan_alloc_recv_slot(lc);
        DEBUG_ERR_ON_FAIL(err, "lmp_chan_alloc_recv_slot");
        err = lmp_chan_register_send(lc, get_default_waitset(), MKCLOSURE(send_wait_handler, (void *)payload));
        DEBUG_ERR_ON_FAIL(err, "lmp_chan_send1");
        event_dispatch(get_default_waitset());
        event_dispatch(get_default_waitset());

        // verify contents of frame
        struct wait_frame_output* output = (struct wait_frame_output*) buf;
        // debug_printf("returned exit code: %d\n", output->status);
        *status = output->status;

        if (*status != NOT_TERMINATED_PID) {
            terminated = true;
        } else {
            thread_yield();
            barrelfish_usleep(100000);
        }
        free(payload);
    } while (terminated == false);
    return SYS_ERR_OK;
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
    errval_t        err;
    struct aos_rpc *rpc = global_rpc;
    // debug_printf("entered init channel\n");

    if (global_rpc == NULL) {
        // debug_printf("creating new aos_rpc channel\n");
        rpc = malloc(sizeof(struct aos_rpc));
        if (rpc == NULL) {
            // debug_printf("aos_rpc_get_init_channel: malloc failed\n");
            return NULL;
        }
        aos_rpc_init(rpc);

        err = lmp_chan_accept(rpc->lmp_chan, DEFAULT_LMP_BUF_WORDS, cap_initep);
    }

    global_rpc = rpc;
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
