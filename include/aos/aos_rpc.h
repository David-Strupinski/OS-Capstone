/**
 * \file
 * \brief RPC Bindings for AOS
 */

/*
 * Copyright (c) 2013-2016, ETH Zurich.
 * Copyright (c) 2022 The University of British Columbia.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstr. 6, CH-8092 Zurich. Attn: Systems Group.
 */

#ifndef _LIB_BARRELFISH_AOS_MESSAGES_H
#define _LIB_BARRELFISH_AOS_MESSAGES_H

#include <aos/aos.h>

#define MAX_PROC_PAGES 1 << 16   // 256 mib (65536 pages)

#define MOD_NAME_MAX_NUM 32
#define MOD_NAME_LEN 64

#define SPAWN_ERR_PID 999990
#define NOT_TERMINATED_PID 999991


/// defines the transport backend of the RPC channel
enum aos_rpc_transport {
    AOS_RPC_LMP,
    AOS_RPC_UMP,
};


enum msg_type {
    ACK_MSG,
    SETUP_MSG,
    NUM_MSG,
    STRING_MSG,
    NAME_MSG,
    PUTCHAR,
    GETCHAR,
    GETCHAR_ACK,
    GET_RAM_CAP,
    SPAWN_CMDLINE,
    PID_ACK,
    RAM_CAP_ACK,
    GET_ALL_PIDS,
    GET_MOD_NAMES,
    GET_PID,
    EXIT_MSG,
    WAIT_MSG,
    SPAWN_WITH_CAPS_MSG,
};


/// type of the receive handler function.
/// depending on your RPC implementation, maybe you want to slightly adapt this
typedef void (*aos_recv_handler_fn)(void *rpc);


/**
 * @brief represents an RPC binding
 *
 * Note: the RPC binding should work over LMP (M4) or UMP (M6)
 */

// store pointers to the URPC frames for the other three cores (BSP only)
// genvaddr_t global_urpc_frames[4];

// get the correct struct ump_chan on the monitor
// direction == 0: core -> monitor
// direction == 1: monitor -> core
struct ump_chan *get_ump_chan_mon(coreid_t core, int direction);

// get the correct struct ump_chan for a core
// direction == 0: core -> monitor
// direction == 1: monitor -> core
struct ump_chan *get_ump_chan_core(int direction);

// circular buffer for UMP messaging in a URPC frame
struct ump_chan {
    size_t base;  // offset of base from struct ump_chan
    size_t head;  // offset of head from base
    size_t tail;  // offset of tail from base
    size_t size;  // size of the buffer
};

// circular ump chan buffer functions
errval_t ump_send(struct ump_chan *chan, char *buf, size_t size);

errval_t ump_receive(struct ump_chan *chan, enum msg_type type, void *buf);

void ump_print(struct ump_chan *chan);

struct cache_line {
    char payload[58];
    uint8_t frag_num;
    uint8_t total_frags;
    uint32_t valid;
};

struct ump_payload {
    enum msg_type type;
    coreid_t send_core;  // core that sent the message
    coreid_t recv_core;  // core that should receive the message
    char payload[128];
};

struct aos_rpc {
    struct lmp_chan *lmp_chan;
    domainid_t pid;
};

struct aos_rpc_num_payload {
    struct aos_rpc *rpc;
    uintptr_t val;
};

struct aos_rpc_string_payload {
    struct aos_rpc *rpc;
    struct capref frame;
    size_t len;
};

struct aos_rpc_cmdline_payload {
    struct aos_rpc   *rpc;
    struct capref     frame;
           size_t     len;
           coreid_t   core;
           domainid_t pid;
};

struct aos_rpc_ram_cap_req_payload {
    struct aos_rpc *rpc;
    size_t bytes;
    size_t alignment;
};

struct aos_rpc_ram_cap_resp_payload {
    struct aos_rpc *rpc;
    struct capref ret_cap;
    size_t ret_bytes;
};

struct get_all_pids_frame_output {
    size_t      num_pids;
    domainid_t  pids[128];
};

struct get_elf_mod_names_output {
    int      num_names;
    char     names[MOD_NAME_MAX_NUM][MOD_NAME_LEN];
};

struct get_pid_frame_output {
    domainid_t pid;
};

struct wait_frame_output {
    int status;
};

struct spawn_with_caps_frame_input {
    int argc;
    char argv[8][8];
    int capc;
    struct capref cap;
    coreid_t core;
    domainid_t pid;
};

// global send acknowledgement handler
void send_ack_handler(void *arg);

// global send ack with a pid handler
void send_pid_handler(void *arg);

// global send char handler (getchar)
void send_char_handler(void *arg);

// global send ram cap response handler
void send_ram_cap_resp_handler(void *arg); 

// global receive handler
void gen_recv_handler(void *arg);

// global ack handler (for processes)
void ack_recv_handler(void *arg);

// for serial
void char_recv_handler(void *arg);

// child process setup message send handler
void setup_send_handler(void *arg);

// general handler for recieving an ack with a pid in it.
void pid_recv_handler(void* arg);

/**
 * @brief Initialize an aos_rpc struct.
 *
 * @param[in] rpc  The aos_rpc struct to initialize.
 *
 * @returns SYS_ERR_OK on success, or error value on failure
 */
errval_t aos_rpc_init(struct aos_rpc *rpc);

errval_t ump_chan_init(struct ump_chan *chan, size_t base);






/*
 * ------------------------------------------------------------------------------------------------
 * AOS RPC: Init Channel
 * ------------------------------------------------------------------------------------------------
 */


/**
 * @brief Send a single number over an RPC channel.
 *
 * @param[in] chan  the RPC channel to use  (init channel)
 * @param[in] val   the number to send
 *
 * @returns SYS_ERR_OK on success, or error value on failure
 *
 * Channel: init
 */
errval_t aos_rpc_send_number(struct aos_rpc *chan, uintptr_t val);


/**
 * @brief Send a single number over an RPC channel.
 *
 * @param[in] chan  the RPC channel to use (init channel)
 * @param[in] val   the string to send
 *
 * @returns SYS_ERR_OK on success, or error value on failure
 *
 * Channel: init
 */
errval_t aos_rpc_send_string(struct aos_rpc *chan, const char *string);


/*
 * ------------------------------------------------------------------------------------------------
 * AOS RPC: Memory Channel
 * ------------------------------------------------------------------------------------------------
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
errval_t aos_rpc_get_ram_cap(struct aos_rpc *chan, size_t bytes, size_t alignment,
                             struct capref *retcap, size_t *ret_bytes);


/*
 * ------------------------------------------------------------------------------------------------
 * AOS RPC: Serial Channel
 * ------------------------------------------------------------------------------------------------
 */


/**
 * @brief obtains a single character from the serial
 *
 * @param chan  the RPC channel to use (serial channel)
 * @param retc  returns the read character
 *
 * @return SYS_ERR_OK on success, or error value on failure
 */
errval_t aos_rpc_serial_getchar(struct aos_rpc *chan, char *retc);


/**
 * @brief sends a single character to the serial
 *
 * @param chan  the RPC channel to use (serial channel)
 * @param c     the character to send
 *
 * @return SYS_ERR_OK on success, or error value on failure
 */
errval_t aos_rpc_serial_putchar(struct aos_rpc *chan, char c);


/*
 * ------------------------------------------------------------------------------------------------
 * AOS RPC: Process Management
 * ------------------------------------------------------------------------------------------------
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
                                      struct capref cap, coreid_t core, domainid_t *newpid);


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
                                         domainid_t *newpid);


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
                                              domainid_t *newpid);


/**
 * @brief obtains a list of PIDs of all processes in the system
 *
 * @param[in]  chan       the RPC channel to use (process channel)
 * @param[out] pids       array of PIDs of all processes in the system (freed by caller)
 * @param[out] pid_count  the number of PIDs in the list
 *
 * @return SYS_ERR_OK on success, or error value on failure
 */
errval_t aos_rpc_proc_get_all_pids(struct aos_rpc *chan, domainid_t **pids, size_t *pid_count);

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
                                 char *cmdline, int cmdline_max, uint8_t *state, int *exit_code);


/**
 * @brief obtains the name of a process with a given PID
 *
 * @param[in] chan  the RPC channel to use (process channel)
 * @param[in] name  the name of the process to search for
 * @param[in] pid   returns PID of the process to pause/suspend
 *
 * @return SYS_ERR_OK on success, or error value on failure
 */
errval_t aos_rpc_proc_get_name(struct aos_rpc *chan, domainid_t pid, char **name);


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
errval_t aos_rpc_proc_get_pid(struct aos_rpc *chan, const char *name, domainid_t *pid);

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
                                    int *name_count);

/**
 * @brief pauses or suspends the execution of a running process
 *
 * @param[in] chan  the RPC channel to use (process channel)
 * @param[in] pid   PID of the process to pause/suspend
 *
 * @return SYS_ERR_OK on success, or error value on failure
 */
errval_t aos_rpc_proc_pause(struct aos_rpc *chan, domainid_t pid);


/**
 * @brief resumes a previously paused process
 *
 * @param[in] chan  the RPC channel to use (process channel)
 * @param[in] pid   PID of the process to resume
 *
 * @return SYS_ERR_OK on success, or error value on failure
 */
errval_t aos_rpc_proc_resume(struct aos_rpc *chan, domainid_t pid);


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
errval_t aos_rpc_proc_exit(struct aos_rpc *chan, int status);


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
errval_t aos_rpc_proc_wait(struct aos_rpc *chan, domainid_t pid, int *status);


/**
 * @brief requests that the process with the given PID is terminated
 *
 * @param[in] chan  the RPC channel to use (process channel)
 * @param[in] pid   PID of the process to be terminated
 *
 * @return SYS_ERR_OK on success, or error value on failure
 */
errval_t aos_rpc_proc_kill(struct aos_rpc *chan, domainid_t pid);


/**
 * @brief requests that all processes that match the supplied name are terminated
 *
 * @param[in] chan  the RPC channel to use (process channel)
 * @param[in] name  name of the processes to be terminated
 *
 * @return SYS_ERR_OK on success, or error value on failure
 */
errval_t aos_rpc_proc_kill_all(struct aos_rpc *chan, const char *name);




/**
 * \brief Returns the RPC channel to init.
 */
struct aos_rpc *aos_rpc_get_init_channel(void);

/**
 * \brief Returns the channel to the memory server
 */
struct aos_rpc *aos_rpc_get_memory_channel(void);

/**
 * \brief Returns the channel to the process manager
 */
struct aos_rpc *aos_rpc_get_process_channel(void);

/**
 * \brief Returns the channel to the serial console
 */
struct aos_rpc *aos_rpc_get_serial_channel(void);

#endif  // _LIB_BARRELFISH_AOS_MESSAGES_H
