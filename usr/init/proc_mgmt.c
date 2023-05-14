/*
 * Copyright (c) 2022 The University of British Columbia
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstrasse 6, CH-8092 Zurich. Attn: Systems Group.
 */

/**
 * @file
 * @brief Interface for managing processes
 *
 * This file contains the process manager. It has basically the same interface as the
 * process manager client (see include/proc_mgmt/proc_mgmt.h). And a few additional functions
 */

#include <ctype.h>
#include <string.h>
#include <stdlib.h>

#include <aos/aos.h>
#include <spawn/spawn.h>
#include <spawn/multiboot.h>
#include <spawn/elfimg.h>
#include <spawn/argv.h>

#include "proc_mgmt.h"

extern struct bootinfo *bi;
extern coreid_t         my_core_id;

// TODO: make this work with multiple cores.
struct spawninfo* root = NULL;


/*
 * ------------------------------------------------------------------------------------------------
 * Utility Functions
 * ------------------------------------------------------------------------------------------------
 */

__attribute__((__used__)) static void spawn_info_to_proc_status(struct spawninfo   *si,
                                                                struct proc_status *status)
{
    status->core      = my_core_id;
    status->pid       = si->pid;
    status->exit_code = 0;
    strncpy(status->cmdline, si->cmdline, sizeof(status->cmdline));
    switch (si->state) {
    case SPAWN_STATE_SPAWNING:
        status->state = PROC_STATE_SPAWNING;
        break;
    case SPAWN_STATE_READY:
        status->state = PROC_STATE_SPAWNING;
        break;
    case SPAWN_STATE_RUNNING:
        status->state = PROC_STATE_RUNNING;
        break;
    case SPAWN_STATE_SUSPENDED:
        status->state = PROC_STATE_PAUSED;
        break;
    case SPAWN_STATE_KILLED:
        status->state     = PROC_STATE_KILLED;
        status->exit_code = -1;
        break;
    case SPAWN_STATE_TERMINATED:
        status->state     = PROC_STATE_EXITED;
        status->exit_code = si->exitcode;
        break;
    default:
        status->state = PROC_STATE_UNKNOWN;
    }
}



/*
 * ------------------------------------------------------------------------------------------------
 * Spawning a new process
 * ------------------------------------------------------------------------------------------------
 */

static errval_t parse_args(const char *cmdline, int *argc, char *argv[])
{
    // check if we have at least one argument
    if (argv == NULL || argv[0] == NULL || argc == NULL || cmdline == NULL) {
        return CAPS_ERR_INVALID_ARGS;
    }

    // parse cmdline, split on spaces
    char cmdline_ptr[MAX_CMDLINE_ARGS + 1];
    strncpy(cmdline_ptr, cmdline, strlen(cmdline) + 1);
    char *token = strtok(cmdline_ptr, " ");
    int i = 0;
    *argc = 0;

    while (token != NULL && i < MAX_CMDLINE_ARGS) {
        argv[i++] = token;
        (*argc)++;
        token = strtok(NULL, " ");
    }
    argv[i] = NULL;

    return SYS_ERR_OK;
}

/**
 * @brief spawns a new process with the given arguments and capabilities on the given core.
 *
 * @param[in]  argc  the number of arguments expected in the argv array
 * @param[in]  argv  array of null-terminated strings containing the arguments
 * @param[in]  capc  the number of capabilities to pass to the new process
 * @param[in]  capv  array of capabilitiies to pass to the child
 * @param[in]  core  id of the core to spawn the program on
 * @param[out] pid   returned program id (PID) of the spawned process
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 *
 * Note: concatenating all values of argv into a single string should yield the
 * command line of the process to be spawned.
 */
errval_t proc_mgmt_spawn_with_caps(int argc, const char *argv[], int capc, struct capref capv[],
                                   coreid_t core, domainid_t *pid)
{
    // make compiler happy about unused parameters
    (void)argc;
    (void)argv;
    (void)capc;
    (void)capv;
    (void)core;
    (void)pid;
    struct spawninfo * si = (struct spawninfo *) malloc(sizeof(struct spawninfo));
    struct elfimg ei;

    si->next = root;
    if (si->next == NULL) {
        si->pid = 1;
    } else {
        si->pid = si->next->pid + 1;
    }
    root = si;
    debug_printf("got to the multiboot\n");
    struct mem_region* module = multiboot_find_module(bi, argv[0]);
    if (module == NULL) {
        debug_printf("multiboot_find_module failed to find %s\n", argv[0]);
        return SPAWN_ERR_FIND_MODULE;
    }
        debug_printf("got past the multiboot\n");

    // added line bellow
    si->module = module;

    elfimg_init_from_module(&ei, module);
    spawn_load_with_caps(si, &ei, argc, argv, capc, capv, si->pid);
    spawn_start(si);
    *pid = si->pid;
    // TODO:
    //  - optional - if we want to stop and restart processes (or kill at all) keep track of 
    //    allocated cnodes so we can deallocate later
    //
    // Note: With multicore support, you many need to send a message to the other core
    return SYS_ERR_OK;
}


/**
 * @brief spawns a new process with the given commandline arguments on the given core
 *
 * @param[in]  cmdline  commandline of the programm to be spawned
 * @param[in]  core     id of the core to spawn the program on
 * @param[out] pid      returned program id (PID) of the spawned process
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 *
 * Note: this function should replace the default commandline arguments the program.
 */
errval_t proc_mgmt_spawn_with_cmdline(const char *cmdline, coreid_t core, domainid_t *pid)
{
    // make compiler happy about unused parameters
    (void)cmdline;
    (void)core;
    (void)pid;

    // Note: With multicore support, you many need to send a message to the other core
    
    // parse command line properly
    const char *argv[MAX_CMDLINE_ARGS];
    argv[0] = cmdline;
    int argc = 0;
    parse_args(cmdline, &argc, (char **)argv);
    proc_mgmt_spawn_with_caps(argc, argv, 0, NULL, core, pid);
    return SYS_ERR_OK;
}


/**
 * @brief spawns a new process with the default arguments on the given core
 *
 * @param[in]  path  string containing the path to the binary to be spawned
 * @param[in]  core  id of the core to spawn the program on
 * @param[out] pid   returned program id (PID) of the spawned process
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 *
 * Note: this function should spawn the program with the default arguments as
 *       listed in the menu.lst file.
 */
errval_t proc_mgmt_spawn_program(const char *path, coreid_t core, domainid_t *pid)
{
    // make compiler happy about unused parameters
    (void)path;
    (void)core;
    (void)pid;
    
    // Note: With multicore support, you many need to send a message to the other core

    struct spawninfo * si = (struct spawninfo *) malloc(sizeof(struct spawninfo));

    si->next = root;
    if (si->next == NULL) {
        si->pid = 1;
    } else {
        si->pid = si->next->pid + 1;
    }
    root = si;
    
    spawn_load_with_bootinfo(si, bi, path, si->pid);
    spawn_start(si);
    return SYS_ERR_OK;
}


/*
 * ------------------------------------------------------------------------------------------------
 * Listing of Processes
 * ------------------------------------------------------------------------------------------------
 */


/**
 * @brief obtains the statuses of running processes from the process manager
 *
 * @param[out] ps    array of process status in the system (must be freed by the caller)
 * @param[out] num   the number of processes in teh list
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 *
 * Note: the caller is responsible for freeing the array of process statuses.
 *       note: you may use the combination of the functions below to implement this one.
 */
errval_t proc_mgmt_ps(struct proc_status **ps, size_t *num)
{
    // make compiler happy about unused parameters
    (void)ps;
    (void)num;


    // TODO: this is not done
    struct spawninfo *curr = root;
    while (curr != NULL) {
        if (curr->state == SPAWN_STATE_RUNNING) {
            (*num)++;
        }
        curr = curr->next;
    }
    *ps = (struct proc_status *) malloc(sizeof(struct proc_status) * (*num));

    curr = root;
    int i = 0;
    while (curr != NULL) {
        if (curr->state == SPAWN_STATE_RUNNING) {
            ps[i]->pid = curr->pid;
            ps[i]->core = curr->core_id;
            ps[i]->state = PROC_STATE_RUNNING;
            ps[i]->exit_code = curr->exitcode;
            strncpy(ps[i]->cmdline, curr->cmdline, MAX_CMDLINE_ARGS);
            i++;
        }
        curr = curr->next;
    }

    return SYS_ERR_OK;
}


/**
 * @brief obtains the list of running processes from the process manager
 *
 * @param[out] pids  array of process ids in the system (must be freed by the caller)
 * @param[out] num   the number of processes in the list
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 *
 * Note: the caller is responsible for freeing the array of process ids.
 */
errval_t proc_mgmt_get_proc_list(domainid_t **pids, size_t *num)
{
    // make compiler happy about unused parameters
    (void)pids;
    (void)num;

    // TODO:
    //  - consult the process table to obtain a list of PIDs of the processes in the system
    struct spawninfo *curr = root;
    while (curr != NULL) {
        if (curr->state == SPAWN_STATE_RUNNING) {
            (*num)++;
        }
        curr = curr->next;
    }
    *pids = (domainid_t *) malloc(sizeof(domainid_t *) * (*num));  // TODO: memory leak? should malloc non-pointer??

    curr = root;
    int i = 0;
    while (curr != NULL) {
        if (curr->state == SPAWN_STATE_RUNNING) {
            (*pids)[i] = curr->pid;
            i++;
        }
        curr = curr->next;
    }

    return SYS_ERR_OK;
}


/**
 * @brief obtains the PID for a process name
 *
 * @param[in]  name  name of the process to obtain the PID for
 * @param[out] pid   returned program id (PID) of the process
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 *
 * Note: Names that are an absoute path should match precisely on the full path.
 *       Names that just include the binary name may match all processes with the
 *       same name. If there are multiple matches
 */
errval_t proc_mgmt_get_pid_by_name(const char *name, domainid_t *pid)
{
    // make compiler happy about unused parameters
    (void)name;
    (void)pid;
    debug_printf("heres the string were looking for: %s\n", name);
    struct spawninfo *curr = root;
    while (curr != NULL) {
        debug_printf("here's the current binary: %s\n", curr->binary_name);
        if (strcmp(name, curr->binary_name) == 0) {
            *pid = curr->pid;
            debug_printf("found pid: %d\n", curr->pid);
            return SYS_ERR_OK;
        }
        curr = curr->next;
    }
    *pid = -1;
    return -1;
}

static proc_state_t get_proc_state_from_spawn_state(spawn_state_t state)
{
    switch (state) {
        case SPAWN_STATE_RUNNING:
            return PROC_STATE_RUNNING;
        case SPAWN_STATE_UNKNOWN:
            return PROC_STATE_UNKNOWN;
        case SPAWN_STATE_SPAWNING:
            return PROC_STATE_SPAWNING;
        case SPAWN_STATE_SUSPENDED:
            return PROC_STATE_PAUSED;
        case SPAWN_STATE_KILLED:
            return PROC_STATE_KILLED;
        default:
            USER_PANIC("invalid spawn state\n");
            return -1;
    }
    USER_PANIC("invalid spawn state\n");
    return -1;
}

/**
 * @brief obtains the status of a process with the given PID
 *
 * @param[in] pid
 * @param[out] status
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 */
errval_t proc_mgmt_get_status(domainid_t pid, struct proc_status *status)
{
    // make compiler happy about unused parameters
    (void)pid;
    (void)status;

    // TODO:
    //   - get the status of the process with the given PID
    struct spawninfo *curr = root;
    while (curr != NULL) {
        if (curr->pid == pid) {
            status->core = curr->core_id;
            status->state = get_proc_state_from_spawn_state(curr->state);
            status->pid = pid;
            status->exit_code = curr->exitcode;

            strncpy(status->cmdline, curr->cmdline, MAX_CMDLINE_ARGS);

            return SYS_ERR_OK;
        }
        curr = curr->next;
    }
    debug_printf("done\n");

    return SPAWN_ERR_WRONG_STATE;
}


/**
 * @brief obtains the name of a process with the given PID
 *
 * @param[in] did   the PID of the process
 * @param[in] name  buffer to store the name in
 * @param[in] len   length of the name buffer
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 */
errval_t proc_mgmt_get_name(domainid_t pid, char *name, size_t len)
{
    // make compiler happy about unused parameters
    (void)pid;
    (void)name;
    (void)len;

    // TODO:
    //   - get the name of the process with the given PID
    struct spawninfo * curr = root;
    while (curr != NULL) {
        if (curr->pid == pid) {
            // found our process
            strcpy(name, curr->binary_name);
            len = strlen(curr->binary_name);
        }
        curr = curr->next;
    }
    return SPAWN_ERR_DOMAIN_NOTFOUND;
}


/*
 * ------------------------------------------------------------------------------------------------
 * Pausing and Resuming of Processes
 * ------------------------------------------------------------------------------------------------
 */


/**
 * @brief pauses the execution of a process
 *
 * @param[in] pid  the PID of the process to pause
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 */
errval_t proc_mgmt_suspend(domainid_t pid)
{
    // make compiler happy about unused parameters
    (void)pid;

    USER_PANIC("functionality not implemented\n");
    // TODO:
    //   - find the process with the given PID and suspend it
    return LIB_ERR_NOT_IMPLEMENTED;
}


/**
 * @brief resumes the execution of a process
 *
 * @param[in] pid  the PID of the process to resume
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 */
errval_t proc_mgmt_resume(domainid_t pid)
{
    // make compiler happy about unused parameters
    (void)pid;

    USER_PANIC("functionality not implemented\n");
    // TODO:
    //   - find the process with the given PID and resume its execution
    return LIB_ERR_NOT_IMPLEMENTED;
}


/*
 * ------------------------------------------------------------------------------------------------
 * Termination of a Process
 * ------------------------------------------------------------------------------------------------
 */


/**
 * @brief tells the process manager that the calling process terminated with the given status
 *
 * @param[in] status  integer value with the given status
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 *
 * Note: this function should not be called by the process directly, but from the exit code
 *       when main returns. Moreover, the function should make sure that the process is
 *       no longer scheduled. The status is the return value of main(), or the error value
 *       e.g., page fault or alike.
 */
errval_t proc_mgmt_exit(int status)
{
    // make compiler happy about unused parameters
    (void)status;
    USER_PANIC("FUNCIOTNALITY NOT IMPLENMENTED\n");
    return SYS_ERR_NOT_IMPLEMENTED;
}

/**
 * @brief tells the process manager than the process with pid has terminated.
 *
 * @param[in] pid     process identifier of the process to wait for
 * @param[in] status  integer value with the given status
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 *
 * Note: this means the process has exited gracefully
 */
errval_t proc_mgmt_terminated(domainid_t pid, int status)
{
    // make compiler happy about unused parameters
    (void)pid;
    (void)status;

    debug_printf("entering proc_mgmt_exit\n");
    struct spawninfo * curr = root;
    while (curr != NULL) {
        if (curr->pid == pid) {
            // found our process
            debug_printf("found our process, here is the exit code we are using: %d\n", status);
            curr->exitcode = status;
            curr->state = SPAWN_STATE_TERMINATED;
            // TODO: actually kill the process (bonus)
            return SYS_ERR_OK;
        }
        curr = curr->next;
    }
    debug_printf("\n!\n!\n!\n!tried to kill a process that doesn't exist!\n!\n!\n!\n");
    return SPAWN_ERR_DOMAIN_NOTFOUND;
}


/**
 * @brief waits for a process to have terminated
 *
 * @param[in]  pid     process identifier of the process to wait for
 * @param[out] status  returns the status of the process as set by `proc_mgmt_exit()`
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 */
errval_t proc_mgmt_wait(domainid_t pid, int *status)
{
    // make compiler happy about unused parameters
    (void)pid;
    (void)status;

    struct spawninfo * curr = root;
    debug_printf("spawn_state_terminated: %d, killed: %d\n", SPAWN_STATE_TERMINATED, SPAWN_STATE_KILLED);

    while (curr != NULL) {
        if (curr->pid == pid) {
            debug_printf("pid: %d, state: %d\n", curr->pid, curr->state);
            while(curr->state != SPAWN_STATE_TERMINATED && curr->state != SPAWN_STATE_KILLED) {
                // wait;
                debug_printf("waiting...\n");
                //thread_yield();
                // event_dispatch(get_default_waitset());
            }
            
            debug_printf("heres the exit code from inside proc_mgmt_wait: %d\n", curr->exitcode);
            *status = curr->exitcode;
            return SYS_ERR_OK;
        }
        curr = curr->next;
    }
    return SPAWN_ERR_DOMAIN_NOTFOUND;
}


/**
 * @brief tells the process manager than the process with pid has terminated.
 *
 * @param[in] pid     process identifier of the process to wait for
 * @param[in] status  integer value with the given status
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 *
 * Note: this means the process has exited gracefully
 */
errval_t proc_mgmt_register_wait(domainid_t pid, enum aos_rpc_transport t, void *chan,
                                 struct waitset *ws)
{

    (void)pid;
    (void)t;
    (void)chan;
    (void)ws;

    USER_PANIC("functionality not implemented\n");
    // TODO:
    //   - find the process with the given PID register the channel for notification
    return LIB_ERR_NOT_IMPLEMENTED;
}

/**
 * @brief terminates the process with the given process id
 *
 * @param[in] pid  process identifier of the process to be killed
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 */
errval_t proc_mgmt_kill(domainid_t pid)
{
    // make compiler happy about unused parameters
    (void)pid;

    USER_PANIC("functionality not implemented\n");
    // TODO:
    //  - find the process in the process table and kill it
    //   - remove the process from the process table
    //   - clean up the state of the process
    //   - M4: notify its waiting processes
    return LIB_ERR_NOT_IMPLEMENTED;
}


/**
 * @brief terminates all processes that match the given name
 *
 * @param[in] name   null-terminated string of the processes to be terminated
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 *
 * The all processes that have the given name should be terminated. If the name is
 * an absolute path, then there must be an exact match. If the name only contains the
 * binary name, then any processes with the same binary name should be terminated.
 *
 * Good students may implement regular expression matching for the name.
 */
errval_t proc_mgmt_killall(const char *name)
{
    // make compiler happy about unused parameters
    (void)name;

    USER_PANIC("functionality not implemented\n");
    // TODO:
    //  - find all the processs that match the given name
    //  - remove the process from the process table
    //  - clean up the state of the process
    //  - M4: notify its waiting processes
    return LIB_ERR_NOT_IMPLEMENTED;
}

