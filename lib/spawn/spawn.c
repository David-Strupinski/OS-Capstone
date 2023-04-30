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

// Helper funciton declarations
errval_t spawn_elf_section_allocator(void *state, genvaddr_t base, size_t size, 
                                     uint32_t flags, void **ret);


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

errval_t fresh_start(struct spawninfo *si, struct elfimg *img, int argc,
                     const char *argv[], int capc, struct capref caps[], domainid_t pid);
errval_t fresh_start(struct spawninfo *si, struct elfimg *img, int argc,
                     const char *argv[], int capc, struct capref caps[], domainid_t pid) {
    (void) si;
    (void) img;
    (void) argc;
    (void) argv;
    (void) capc;
    (void) caps;
    (void) pid;

    errval_t err;

    // TODO: error checking everywhere, I did absolutely no error checking

    struct capref elf_frame = {
        .cnode = cnode_module,
        .slot = si->module->mrmod_slot,
    };
    err = paging_map_frame_attr(get_current_paging_state(), &si->module_data, si->module->mrmod_size, elf_frame, VREGION_FLAGS_READ_WRITE);

    si->binary_name = (char*)argv[0];

    // SETUP CSPACE ---------------------------------------------------------
    // TODO: for some reason this works even when not all the cnodes 
    //       listed in the book are created

    struct capref child_table;
    struct capref cap_l1_cnode;
    struct cnoderef child_task_cnode;

    err = cnode_create_l1(&cap_l1_cnode, NULL);
    
    struct cnoderef *l2_slot_task_cnode = &child_task_cnode;
    err = cnode_create_foreign_l2(cap_l1_cnode, ROOTCN_SLOT_TASKCN, l2_slot_task_cnode);

    struct capref cap_l1_slot_cnode;
    cap_l1_slot_cnode.cnode = child_task_cnode,
    cap_l1_slot_cnode.slot = TASKCN_SLOT_ROOTCN,
    
    err = cap_copy(cap_l1_slot_cnode, cap_l1_cnode);

    struct cnoderef l2_slot_page_cnode;
    err = cnode_create_foreign_l2(cap_l1_cnode, ROOTCN_SLOT_PAGECN, &l2_slot_page_cnode);

    struct cnoderef l2_slot_basepage_cnode;
    err = cnode_create_foreign_l2(cap_l1_cnode, ROOTCN_SLOT_BASE_PAGE_CN, &l2_slot_basepage_cnode);
    
    // ram for earlymem
    struct capref some_ram;
    ram_alloc(&some_ram, BASE_PAGE_SIZE * 256); // TODO CHECK THE SIZE

    struct capref child_earlymem = {
        .cnode = child_task_cnode,
        .slot  = TASKCN_SLOT_EARLYMEM
    };
    cap_copy(child_earlymem, some_ram);

    child_table.cnode = l2_slot_page_cnode;
    child_table.slot = 0;
    
    // END SETUP CSPACE -----------------------------------------------------
    
    // SETUP VSPACE ---------------------------------------------------------
    
    struct capref parent_version_of_child_table;
    err = slot_alloc(&parent_version_of_child_table);

    err = vnode_create(child_table, ObjType_VNode_AARCH64_l0);

    err = cap_copy(parent_version_of_child_table, child_table);
    
    si->st = malloc(sizeof(struct paging_state));
    if (si->st == NULL) {
        return -1;
    }

    err = paging_init_state_foreign(si->st, BASE_PAGE_SIZE, parent_version_of_child_table, get_default_slot_allocator());
    
    // END SETUP VSPACE -----------------------------------------------------

    // ELF PARSING ----------------------------------------------------------
    
    genvaddr_t entry_pt;
    
    err = elf_load(EM_AARCH64, spawn_elf_section_allocator, si, (lvaddr_t) si->module_data, si->module->mrmod_size, &entry_pt);
    
    struct Elf64_Shdr *got = elf64_find_section_header_name((genvaddr_t) si->module_data, si->module->mrmod_size, ".got");

    // END ELF PARSING ------------------------------------------------------

    // SETUP ENVIRONMENT ----------------------------------------------------
    
    struct capref args_cap;
    void *parent_args;
    void *child_args;

    err = frame_alloc(&args_cap, ARGS_SIZE, NULL);

    err = paging_map_frame_attr(si->st, &child_args, ARGS_SIZE, args_cap, VREGION_FLAGS_READ_WRITE);
    err = paging_map_frame_attr(get_current_paging_state(), &parent_args, ARGS_SIZE, args_cap, VREGION_FLAGS_READ_WRITE);

    struct spawn_domain_params *args = (struct spawn_domain_params *) parent_args;

    memset(parent_args, 0, ARGS_SIZE);

    strcpy(parent_args + sizeof(struct spawn_domain_params), argv[0]);
    args->argv[0] = child_args + sizeof(struct spawn_domain_params);
    args->argc = argc;
    args->argv[argc] = NULL;

    // TODO: set up a lot more environment, and more than just argv[0]
    
    // END SETUP ENVIRONMENT ------------------------------------------------

    // SETUP DISPATCHER -----------------------------------------------------
    struct capref dispatcher;

    err = slot_alloc(&dispatcher);

    err = dispatcher_create(dispatcher);

    struct capref parent_dispframe;

    err = frame_alloc(&parent_dispframe, DISPATCHER_FRAME_SIZE, NULL);

    void *buf_p;
    err = paging_map_frame_attr(get_current_paging_state(), &buf_p, DISPATCHER_FRAME_SIZE, parent_dispframe, VREGION_FLAGS_READ_WRITE);

    void *buf_c;
    err = paging_map_frame_attr(si->st, &buf_c, DISPATCHER_FRAME_SIZE, parent_dispframe, VREGION_FLAGS_READ_WRITE);

    // from the book
    struct dispatcher_shared_generic *disp = get_dispatcher_shared_generic((dispatcher_handle_t)buf_p);
    struct dispatcher_generic *disp_gen = get_dispatcher_generic((dispatcher_handle_t)buf_p);

    arch_registers_state_t *enabled_area = dispatcher_get_enabled_save_area((dispatcher_handle_t)buf_p);
    arch_registers_state_t *disabled_area = dispatcher_get_disabled_save_area((dispatcher_handle_t)buf_p);

    disp_gen->core_id = disp_get_core_id();
    disp_gen->domain_id = disp_get_domain_id();
    disp->udisp = (lvaddr_t)buf_c;
    disp->disabled = 1;
    strncpy(disp->name, si->binary_name, DISP_NAME_LEN);
    disabled_area->named.pc = (lvaddr_t)got->sh_addr;

    armv8_set_registers((dispatcher_handle_t)buf_p, (lvaddr_t) entry_pt, (lvaddr_t) got->sh_addr);
    disp_gen->eh_frame = 0;
    disp_gen->eh_frame_size = 0;
    disp_gen->eh_frame_hdr = 0;
    disp_gen->eh_frame_hdr_size = 0;
    
    // easy to forget about this little function call in the book
    registers_set_param(enabled_area, (uint64_t) child_args);

    // END SETUP DISPATCHER -------------------------------------------------

    // FINALY THE BEAUTIFUL MOMENT HAS COME!!!!!
    // TODO: move this to spawn start (will require adding more state to st)
    
    // gotta copy the parent dispatcher frame over to the child as a last step.
    struct capref child_dispframe;
    child_dispframe.cnode = child_task_cnode;
    child_dispframe.slot = TASKCN_SLOT_DISPFRAME;
    err = cap_copy(child_dispframe, parent_dispframe);

    // very unnerving that this works even when these are commented out.

    // struct capref child_disp;
    // child_disp.cnode = child_task_cnode,
    // child_disp.slot = TASKCN_SLOT_DISPATCHER,
    // err = cap_copy(child_disp, dispatcher);
    
    // struct capref selfep;
    // selfep.cnode = child_task_cnode,
    // selfep.slot = TASKCN_SLOT_SELFEP,
    // err = cap_retype(selfep, dispatcher, 0, ObjType_EndPointLMP, 0);
    
    err = invoke_dispatcher(dispatcher, cap_dispatcher, cap_l1_cnode, child_table, child_dispframe, true);

    return SYS_ERR_OK;
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

    return fresh_start(si, img, argc, argv, capc, caps, pid);
}

/**
 * @brief Allocates memory for a section of ELF image in a new process, 
 *        injects it into the new process's vspace
 *
 * @param[in]  state a struct that the allocator can use to keep track of which pages were mapped.
 *                   set to null if not used.
 * @param[in]  base  address in child's vspace to start mapping section
 * @param[in]  size  size of section in bytes
 * @param[in]  flags permissions of new process to the newly allocated section
 * @param[out] ret   pointer to mapped section of parent vspace, which is doubly mapped to
 *                   child vspace and which the ELF library will use to write the 
 *                   ELF section contents
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 */
errval_t spawn_elf_section_allocator(void *state, genvaddr_t base, size_t size, 
                                     uint32_t flags, void **ret) {
    errval_t err;

    struct spawninfo *si = (struct spawninfo *) state;
    
    // have to round since this might not be aligned, 
    // and we want to move the base down, not up, if it isn't aligned
    genvaddr_t init_base = base;
    base = ROUND_DOWN(base, BASE_PAGE_SIZE);
    size = ROUND_UP(base + size, BASE_PAGE_SIZE) - base;

    struct capref frame;
    err = frame_alloc(&frame, size, NULL);

    void *retval;
    err = paging_map_frame_attr(
        get_current_paging_state(),
        &retval,
        size,
        frame,
        VREGION_FLAGS_READ_WRITE
    );
    
    err = paging_map_fixed_attr(
        si->st,
        base,
        frame,
        size,
        flags
    );

    *ret = retval + (init_base - base);

    return SYS_ERR_OK;
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
    //USER_PANIC("Not implemented");
    return SYS_ERR_OK;
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
