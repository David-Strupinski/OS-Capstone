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


    // Step 1 -----------------------------------------------------------------------------
    // Initialize the spawn_info struct
    si->binary_name = (char *) malloc(strlen(argv[0]) + 1);
    strncpy(si->binary_name, argv[0], strlen(argv[0]) + 1);
    si->cmdline = NULL;
    si->pid = pid;
    si->state = SPAWN_STATE_SPAWNING;
    si->exitcode = 0;
    printf("finished step 1\n");

    // Step 2 -----------------------------------------------------------------------------
    // Map the elfimg into the address space
    errval_t err = paging_map_frame_attr_offset(get_current_paging_state(), &img->buf, img->size, 
                                                img->mem, 0, VREGION_FLAGS_READ_WRITE);
    DEBUG_ERR_ON_FAIL(err, "looks like paging failed to map the elf image in our own vspace\n");
    printf("finished step 2\n");

    // Step 3 -----------------------------------------------------------------------------
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

    si->taskcn_slot_argspage.cnode = si->rootcn_slot_taskcn_cnoderef;       // Args are filled later
    si->taskcn_slot_argspage.slot = TASKCN_SLOT_ARGSPAGE;
    err = ram_alloc(&si->taskcn_slot_argspage, BASE_PAGE_SIZE);
    DEBUG_ERR_ON_FAIL(err, "spawn_load_with_caps: Failed to create args page capability");

    si->taskcn_slot_earlymem.cnode = si->rootcn_slot_taskcn_cnoderef;       // Cap is created later
    si->taskcn_slot_earlymem.slot = TASKCN_SLOT_EARLYMEM;
    err = ram_alloc(&si->taskcn_slot_earlymem, BASE_PAGE_SIZE * 256);       // see ram_alloc.c:175
    DEBUG_ERR_ON_FAIL(err, "spawn_load_with_caps: Failed to create early mem capability");

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
    printf("finished step 3\n");

    // Step 4 -----------------------------------------------------------------------------
    // Setup the child's vspace

    // TODO: mapping to vaddr PAGE_SIZE right now (skip first page), change if needed
    // (I think it is fine to map at BASE_PAGE_SIZE)
    struct capref parent_l0_table;
    err = slot_alloc(&parent_l0_table);
    DEBUG_ERR_ON_FAIL(err, "slot alloc failed to give a slot for our L0 page table\n");

    err = vnode_create(parent_l0_table, ObjType_VNode_AARCH64_l0);
    DEBUG_ERR_ON_FAIL(err, "failed to create a new vnode for our child's L0 page table\n");

    err = cap_copy(si->rootcn_slot_pagecn_slot0, parent_l0_table);
    DEBUG_ERR_ON_FAIL(err, "failed to copy capref from parent L0 table to child L0 table\n");

    err = paging_init_state_foreign(&si->st, BASE_PAGE_SIZE, parent_l0_table, 
                                    get_default_slot_allocator());
    DEBUG_ERR_ON_FAIL(err, "spawn_load_with_caps: paging_init_state_foreign failed");
    printf("finished step 4\n");

    // Step 5 -----------------------------------------------------------------------------
    // parse the ELF file
    
    // call elf_load
    genvaddr_t endpoint;
    
    err = elf_load(EM_AARCH64, spawn_elf_section_allocator, si, (genvaddr_t)img->buf, img->size, &endpoint);
    DEBUG_ERR_ON_FAIL(err, "elf load failed :(\n");

    // get the got
    struct Elf64_Shdr *got = elf64_find_section_header_name((genvaddr_t) img->buf, img->size, ".got");
    if (got == NULL) {
        printf ("darn\n");
        return -1;
    }
    // got the got
    printf("finished step 5\n");;
    

    // Step 6 -----------------------------------------------------------------------------
    err = frame_alloc(&si->taskcn_slot_dispframe, DISPATCHER_FRAME_SIZE, NULL);
    genvaddr_t buf_c;
    genvaddr_t buf_p;

    err = paging_map_frame_attr_offset(&si->st, (void**)(&buf_c), 
                                       DISPATCHER_FRAME_SIZE, si->taskcn_slot_dispframe, 0, 
                                       VREGION_FLAGS_READ_WRITE);
    DEBUG_ERR_ON_FAIL(err, "mapping into the child failed\n");

    err = paging_map_frame_attr_offset(get_current_paging_state(), (void**)(&buf_p), 
                                       DISPATCHER_FRAME_SIZE, si->taskcn_slot_dispframe, 0, 
                                       VREGION_FLAGS_READ_WRITE);
    DEBUG_ERR_ON_FAIL(err, "mapping into the parent failed\n");

    struct dispatcher_shared_generic *disp = get_dispatcher_shared_generic(buf_p);
    struct dispatcher_generic *disp_gen = get_dispatcher_generic(buf_p);

    // arch_registers_state_t *enabled_area  = dispatcher_get_enabled_save_area(buf_p);
    arch_registers_state_t *disabled_area = dispatcher_get_disabled_save_area(buf_p);

    disp_gen->core_id = disp_get_core_id();
    disp_gen->domain_id = disp_get_domain_id();
    disp->udisp = (lvaddr_t)buf_c;
    disp->disabled = 1;
    strncpy(disp->name, si->binary_name, DISP_NAME_LEN);
    disabled_area->named.pc = (lvaddr_t) endpoint;
    // TODO: verify arguments of above and bellow
    armv8_set_registers(buf_p, (lvaddr_t) endpoint, (lvaddr_t) got->sh_addr);
    disp_gen->eh_frame = 0;
    disp_gen->eh_frame_size = 0;
    disp_gen->eh_frame_hdr = 0;
    disp_gen->eh_frame_hdr_size = 0;

    printf("finished step 6\n");


    // TODO: move step 7 to spawn start

    // Step 7 -----------------------------------------------------------------------------
    struct capref disp_cap_copy;
    err = slot_alloc(&disp_cap_copy);
    DEBUG_ERR_ON_FAIL(err, "spawn_load_with_caps: failed to slot alloc");
    err = cap_copy(disp_cap_copy, si->taskcn_slot_dispatcher);
    DEBUG_ERR_ON_FAIL(err, "spawn_load_with_caps: failed to gain access to child dispatcher cap");

    struct capref root_cap_copy;
    err = slot_alloc(&root_cap_copy);
    DEBUG_ERR_ON_FAIL(err, "spawn_load_with_caps: failed to slot alloc");
    err = cap_copy(root_cap_copy, si->root);
    DEBUG_ERR_ON_FAIL(err, "spawn_load_with_caps: failed to gain access to child root cap");

    err = invoke_dispatcher(disp_cap_copy, cap_dispatcher, root_cap_copy, 
                            si->rootcn_slot_pagecn_slot0, si->taskcn_slot_dispframe, true);
    DEBUG_ERR_ON_FAIL(err, "invoke dispatcher failed\n");

    return SYS_ERR_OK;
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
    struct spawninfo * st= (struct spawninfo*) state;

    // Create some physical memory to back this ELF section
    size_t actual_size;
    struct capref temp;
    err = frame_alloc(&temp, size, &actual_size);
    DEBUG_ERR_ON_FAIL(err, "spawn_elf_section_allocator: Failed to create frame capability");
    if (err_is_fail(err)) {
        return err;
    }
    size = actual_size;

    // Parse elf flags
    uint32_t temp_flags = 0;
    if (flags & PF_X)  {
        temp_flags |= VREGION_FLAGS_EXECUTE;
    }
    if (flags & PF_W) {
        temp_flags |= VREGION_FLAGS_WRITE;
    }
    if (flags & PF_R) {
        temp_flags |= VREGION_FLAGS_READ;
    }
    
    // Map newly allocated frame cap into child vspace. We do not support mapping above 1/4 of the
    // child's vaddr space.
    if (base + size > ((uint64_t)1)<<46) {
        printf("Oh noes! This ELF section is addressed to high! >:-(\n");
        return SPAWN_ERR_ELF_MAP;
    }
    err = paging_map_fixed_attr_offset(&st->st, base, temp, size, 0, temp_flags);
    DEBUG_ERR_ON_FAIL(err, "spawn_elf_section_allocator: Failed to map mem into child process");
    if (err_is_fail(err)) {
        return err;
    }

    // map into parent (our) vaddress space
    err = paging_map_frame_attr_offset(get_current_paging_state(), ret, size, temp, 0, 
                                       VREGION_FLAGS_READ_WRITE);
    DEBUG_ERR_ON_FAIL(err, "spawn_elf_section_allocator: Failed to map mem into parent vspace"); 
    if (err_is_fail(err)) {
        return err;
    }
    
    return SYS_ERR_OK;
    // Other stuff?
    // TODO: potentially support unmapping with more book keeping
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
