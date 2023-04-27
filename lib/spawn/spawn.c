/*
 * Copyright (c) 2016, ETH Zurich.
 * Copyright (c) 2022, The University of British Columbia.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetsstrasse 6, CH-8092 Zurich. Attn: Systems Group.
 */

#include <ctype.h>
#include <string.h>

#include <aos/aos.h>
#include <aos/dispatcher_arch.h>
#include <barrelfish_kpi/paging_arm_v8.h>
#include <barrelfish_kpi/domain_params.h>

#include <elf/elf.h>
#include <spawn/spawn.h>
#include <spawn/multiboot.h>
#include <spawn/argv.h>
#include <spawn/elfimg.h>




/**
 * @brief Sets the initial values of some registers in the dispatcher
 *
 * @param[in] handle    dispatcher handle to the child's dispatcher
 * @param[in] entry     entry point of the new process
 * @param[in] got_base  the base address of the global offset table
 *
 */
__attribute__((__used__)) static void armv8_set_registers(dispatcher_handle_t handle,
                                                          lvaddr_t entry, lvaddr_t got_base)
{
    assert(got_base != 0);
    assert(entry != 0);

    // set the got_base in the shared struct
    struct dispatcher_shared_aarch64 *disp_arm = get_dispatcher_shared_aarch64(handle);
    disp_arm->got_base                         = got_base;

    // set the got_base in the registers for the enabled case
    arch_registers_state_t *enabled_area         = dispatcher_get_enabled_save_area(handle);
    enabled_area->regs[REG_OFFSET(PIC_REGISTER)] = got_base;

    // set the got_base in the registers for the disabled case
    arch_registers_state_t *disabled_area         = dispatcher_get_disabled_save_area(handle);
    disabled_area->regs[REG_OFFSET(PIC_REGISTER)] = got_base;
    disabled_area->named.pc                       = entry;
}




/**
 * @brief constructs a new process by loading the image from the bootinfo struct
 *
 * @param[in] si    spawninfo structure to fill in
 * @param[in] bi    pointer to the bootinfo struct
 * @param[in] name  name of the binary in the bootinfo struct
 * @param[in] pid   the process id (PID) for the new process
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 *
 * Note, this function prepares a new process for running, but it does not make it
 * runnable. See spawn_start().
 */
errval_t spawn_load_with_bootinfo(struct spawninfo *si, struct bootinfo *bi, const char *name,
                                  domainid_t pid)
{
    //errval_t err;

    // Get the module from the multiboot image, create a capability to it
    //struct mem_region* module = multiboot_find_module(bi, name);
    (void) si;
    (void) bi;
    (void) name;
    (void) pid;

    // char* strings = multiboot_module_opts(module);
    
    // printf("This is what multiboot_module_opts returns: %s\n", strings);
    // // - Fill in argc/argv from the multiboot command line
    // // - Call spawn_load_with_args
    // struct elfimg *img;
    // spawn_load_with_args(si, img, int argc, const char *argv[], pid);
    return -1;
}

/**
 * @brief constructs a new process from the provided image pointer
 *
 * @param[in] si    spawninfo structure to fill in
 * @param[in] img   pointer to the elf image in memory
 * @param[in] argc  number of arguments in argv
 * @param[in] argv  command line arguments
 * @param[in] capc  number of capabilities in the caps array
 * @param[in] caps  array of capabilities to pass to the child
 * @param[in] pid   the process id (PID) for the new process
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 *
 * Note, this function prepares a new process for running, but it does not make it
 * runnable. See spawn_start().
 */
errval_t spawn_load_with_caps(struct spawninfo *si, struct elfimg *img, int argc,
                              const char *argv[], int capc, struct capref caps[], domainid_t pid)
{
    // make compiler happy about unused parameters
    (void)si;
    (void)img;
    (void)argc;
    (void)argv;
    (void)capc;
    (void)caps;
    (void)pid;

    // TODO: Implement the domain spawning
    // - Load the ELF binary
    // - Setup the dispatcher
    // - Setup the environment
    // - set the registers correctly
    // - Set the spawn state
    printf("Made it into spawn_load_with_caps (lower level)----------------------------------------------\n");

    // Initialize the spawn_info struct
    si->binary_name = (char *) malloc(strlen(argv[0]) + 1);
    strncpy(si->binary_name, argv[0], strlen(argv[0]) + 1);
    si->cmdline = NULL;
    si->pid = pid;
    si->state = SPAWN_STATE_SPAWNING;
    si->exitcode = 0;

    // Map the elfimg into the address space
    errval_t err = paging_map_frame_attr_offset(get_current_paging_state(), &img->buf, img->size, 
                                                img->mem, 0, VREGION_FLAGS_READ_WRITE);
    DEBUG_ERR_ON_FAIL(err, "looks like paging failed to map the elf image in our own vspace\n");


    // Setup the child's cspace

    // Create child l1 
    err = cnode_create_l1(&si->root, &si->root_cnoderef);
    DEBUG_ERR_ON_FAIL(err, "spawn_load_with_caps: Failed to create child l1 pg tbl");

    // Create child ROOTCN_SLOT_TASKCN L2 page table, fill required capabilities
    err = cnode_create_foreign_l2(si->root, ROOTCN_SLOT_TASKCN, &si->rootcn_slot_taskcn_cnoderef);
    DEBUG_ERR_ON_FAIL(err, "spawn_load_with_caps: Failed to create child ROOTCN_SLOT_TASKCN L2 pg "
                           "tbl");

    si->taskcn_slot_dispatcher.cnode = si->rootcn_slot_taskcn_cnoderef;
    si->taskcn_slot_dispatcher.slot = TASKCN_SLOT_DISPATCHER;
    err = dispatcher_create(si->taskcn_slot_dispatcher);
    DEBUG_ERR_ON_FAIL(err, "spawn_load_with_caps: Failed to create child dispatcher capability");

    si->taskcn_slot_selfep.cnode = si->rootcn_slot_taskcn_cnoderef;
    si->taskcn_slot_selfep.slot = TASKCN_SLOT_SELFEP;

    struct capability dispcap;
    err = cap_direct_identify(si->taskcn_slot_dispatcher, &dispcap);
    DEBUG_ERR_ON_FAIL(err, "spawn_load_with_caps: Failed to access child dispatcher capability");
    err = cap_retype(si->taskcn_slot_selfep, si->taskcn_slot_dispatcher, 0, ObjType_EndPointLMP, 
                     dispcap.u.frame.bytes);
    DEBUG_ERR_ON_FAIL(err, "spawn_load_with_caps: Failed to create child self reference "
                           "capability");

    si->taskcn_slot_rootcn.cnode = si->rootcn_slot_taskcn_cnoderef;
    si->taskcn_slot_rootcn.slot = TASKCN_SLOT_ROOTCN;
    err = cap_copy(si->taskcn_slot_rootcn, si->root);
    DEBUG_ERR_ON_FAIL(err, "spawn_load_with_caps: Failed to copy l1 cnode capability to child"
                           "process");

    si->taskcn_slot_dispframe.cnode = si->rootcn_slot_taskcn_cnoderef;      // Cap is created later
    si->taskcn_slot_dispframe.slot = TASKCN_SLOT_DISPFRAME;

    si->taskcn_slot_argspage.cnode = si->rootcn_slot_taskcn_cnoderef; // TODO: Create cap, or later?
    si->taskcn_slot_argspage.slot = TASKCN_SLOT_ARGSPAGE;

    si->taskcn_slot_earlymem.cnode = si->rootcn_slot_taskcn_cnoderef; // TODO: Create cap, or later?
    si->taskcn_slot_earlymem.slot = TASKCN_SLOT_EARLYMEM;

    // Create child ROOTCN_SLOT_ALLOC_0 L2 page table
    err = cnode_create_foreign_l2(si->root, ROOTCN_SLOT_SLOT_ALLOC0, 
                                  &si->rootcn_slot_alloc_0_cnoderef);
    DEBUG_ERR_ON_FAIL(err, "spawn_load_with_caps: Failed to create child ROOTCN_SLOT_ALLOC_0 L2 pg "
                           "tbl");

    // Create child ROOTCN_SLOT_ALLOC_1 L2 page table
    err = cnode_create_foreign_l2(si->root, ROOTCN_SLOT_SLOT_ALLOC1, 
                                  &si->rootcn_slot_alloc_1_cnoderef);
    DEBUG_ERR_ON_FAIL(err, "spawn_load_with_caps: Failed to create child ROOTCN_SLOT_ALLOC_1 L2 pg "
                           "tbl");

    // Create child ROOTCN_SLOT_ALLOC_2 L2 page table
    err = cnode_create_foreign_l2(si->root, ROOTCN_SLOT_SLOT_ALLOC2, 
                                  &si->rootcn_slot_alloc_2_cnoderef);
    DEBUG_ERR_ON_FAIL(err, "spawn_load_with_caps: Failed to create child ROOTCN_SLOT_ALLOC_2 L2 pg "
                           "tbl");

    // Create child ROOTCN_SLOT_PAGECN L2 page table, capref to child's l0 page table
    err = cnode_create_foreign_l2(si->root, ROOTCN_SLOT_PAGECN, &si->rootcn_slot_pagecn_cnoderef);
    DEBUG_ERR_ON_FAIL(err, "spawn_load_with_caps: Failed to create child ROOTCN_SLOT_PAGECN L2 pg "
                           "tbl");
    si->rootcn_slot_pagecn_slot0.cnode = si->rootcn_slot_taskcn_cnoderef;
    si->rootcn_slot_pagecn_slot0.slot = 0;


    // Setup the child's vspace

    // TODO: mapping to vaddr PAGE_SIZE right now (skip first page), change if needed
    err = paging_init_state_foreign(&si->st, BASE_PAGE_SIZE, si->rootcn_slot_pagecn_slot0, 
                                    get_default_slot_allocator());
    DEBUG_ERR_ON_FAIL(err, "spawn_load_with_caps: paging_init_state_foreign failed");

    // // Map a page in our OWN vaddress space to store child's paging state struct,
    // // note the use of the child ptable's capability, save a reference to it
    // err = paging_map_frame_attr_offset(&current, si.st, BASE_PAGE_SIZE,
    //                                    root, 0, VREGION_FLAGS_READ_WRITE);
    // DEBUG_ERR(err, "spawn_load_with_bootinfo: Failed to map pgstruct page in parent page table");


    // // Make child inherit its own paging_state struct by mapping its second page to the 
    // // same page in the parent, leaving first empty for NULL
    // err = paging_map_fixed_attr_offset(child_state, BASE_PAGE_SIZE, root, BASE_PAGE_SIZE, 
    //                                    0, VREGION_FLAGS_READ_WRITE);
    // DEBUG_ERR(err, "spawn_load_with_bootinfo: Failed to map pgstruct page in child page table");

    // struct capref child_frame = {
    //     .cnode = cnode_module, // not certain this is the right thing
    //     .slot = module->mrmod_slot,
    // };
    return -1;
}

/**
 * @brief starts the execution of the new process by making it runnable
 *
 * @param[in] si   spawninfo structure of the constructed process
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 */
errval_t spawn_start(struct spawninfo *si)
{
    // make compiler happy about unused parameters
    (void)si;

    // TODO:
    //  - check whether the process is in the right state (ready to be started)
    //  - invoke the dispatcher to make the process runnable
    //  - set the state to running
    USER_PANIC("Not implemented");
    return LIB_ERR_NOT_IMPLEMENTED;
}

/**
 * @brief resumes the execution of a previously stopped process
 *
 * @param[in] si   spawninfo structure of the process
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 */
errval_t spawn_resume(struct spawninfo *si)
{
    // make compiler happy about unused parameters
    (void)si;

    // TODO:
    //  - check whether the process is in the right state
    //  - resume the execution of the process
    //  - set the state to running
    USER_PANIC("Not implemented");
    return LIB_ERR_NOT_IMPLEMENTED;
}

/**
 * @brief stops/suspends the execution of a running process
 *
 * @param[in] si   spawninfo structure of the process
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 */
errval_t spawn_suspend(struct spawninfo *si)
{
    // make compiler happy about unused parameters
    (void)si;

    // TODO:
    //  - check whether the process is in the right state
    //  - stop the execution of the process
    //  - set the state to suspended
    USER_PANIC("Not implemented");
    return LIB_ERR_NOT_IMPLEMENTED;
}

/**
 * @brief stops the execution of a running process
 *
 * @param[in] si   spawninfo structure of the process
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 */
errval_t spawn_kill(struct spawninfo *si)
{
    // make compiler happy about unused parameters
    (void)si;

    // TODO:
    //  - check whether the process is in the right state
    //  - stop the execution of the process
    //  - set the state to killed
    USER_PANIC("Not implemented");
    return LIB_ERR_NOT_IMPLEMENTED;
}

/**
 * @brief marks the process as having exited
 *
 * @param[in] si        spawninfo structure of the process
 * @param[in] exitcode  exit code of the process
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 *
 * The process manager should call this function when it receives the exit
 * notification from the child process. The function makes sure that the
 * process is no longer running and can be cleaned up later on.
 */
errval_t spawn_exit(struct spawninfo *si, int exitcode)
{
    // make compiler happy about unused parameters
    (void)si;
    (void)exitcode;

    // TODO:
    //  - check whether the process is in the right state
    //  - stop the execution of the process, update the exit code
    //  - set the state to terminated
    USER_PANIC("Not implemented");
    return LIB_ERR_NOT_IMPLEMENTED;
}

/**
 * @brief cleans up the resources of a process
 *
 * @param[in] si   spawninfo structure of the process
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 *
 * Note: The process has to be stopped before calling this function.
 */
errval_t spawn_cleanup(struct spawninfo *si)
{
    // make compiler happy about unused parameters
    (void)si;

    // Resources need to be cleaned up at some point. How would you go about this?
    // This is certainly not an easy task. You need to track down all the resources
    // that the process was using and collect them. Recall, in Barrelfish all the
    // resources are represented by capabilities -- so you could, in theory, simply
    // walk the CSpace of the process. Then, some of the resources you may have kept
    // in the process manager's CSpace and created mappings in the VSpace.
    //
    // TODO(not required):
    //  - cleanup the resources of the process
    //  - clean up the resources in the process manager
    USER_PANIC("Not implemented");
    return LIB_ERR_NOT_IMPLEMENTED;
}

/**
 * @brief initializes the IPC channel for the process
 *
 * @param[in] si       spawninfo structure of the process
 * @param[in] ws       waitset to be used
 * @param[in] handler  message handler for the IPC channel
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 *
 * Note: this functionality is required for the IPC milestone.
 *
 * Hint: the IPC subsystem should be initialized before the process is being run for
 * the first time.
 */
errval_t spawn_setup_ipc(struct spawninfo *si, struct waitset *ws, aos_recv_handler_fn handler)
{
    // make compiler happy about unused parameters
    (void)si;
    (void)ws;
    (void)handler;

    // TODO:
    //  - initialize the messaging channels for the process
    //  - check its execution state (it shouldn't have run yet)
    //  - create the required capabilities if needed
    //  - set the receive handler
    USER_PANIC("Not implemented");
    return LIB_ERR_NOT_IMPLEMENTED;
}


/**
 * @brief sets the receive handler function for the message channel
 *
 * @param[in] si       spawninfo structure of the process
 * @param[in] handler  handler function to be set
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 */
errval_t spawn_set_recv_handler(struct spawninfo *si, aos_recv_handler_fn handler)
{
    // make compiler happy about unused parameters
    (void)si;
    (void)handler;

    // TODO:
    //  - set the custom receive handler for the message channel
    USER_PANIC("Not implemented");
    return LIB_ERR_NOT_IMPLEMENTED;
}
