/**
 * \file
 * \brief AOS paging helpers.
 */

/*
 * Copyright (c) 2012, 2013, 2016, ETH Zurich.
 * Copyright (c) 2022, The University of British Columbia.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstr. 6, CH-8092 Zurich. Attn: Systems Group.
 */

#include <aos/aos.h>
#include <aos/paging.h>
#include <aos/except.h>
#include <aos/slab.h>
#include "threads_priv.h"
#include <mm/slot_alloc.h>

#include <stdio.h>
#include <string.h>

static struct paging_state current;



/**
 * @brief allocates a new page table for the given paging state with the given type
 *
 * @param[in]  st    paging state to allocate the page table for (required for slot allcator)
 * @param[in]  type  the type of the page table to create
 * @param[out] ret   returns the capref to the newly allocated page table
 *
 * @returns error value indicating success or failure
 *   - @retval SYS_ERR_OK if the allocation was successfull
 *   - @retval LIB_ERR_SLOT_ALLOC if there couldn't be a slot allocated for the new page table
 *   - @retval LIB_ERR_VNODE_CREATE if the page table couldn't be created
 */
static errval_t pt_alloc(struct paging_state *st, enum objtype type, struct capref *ret)
{
    errval_t err;

    assert(type == ObjType_VNode_AARCH64_l0 || type == ObjType_VNode_AARCH64_l1
           || type == ObjType_VNode_AARCH64_l2 || type == ObjType_VNode_AARCH64_l3);
    // try to get a slot from the slot allocator to hold the new page table
    err = st->slot_alloc->alloc(st->slot_alloc, ret);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_SLOT_ALLOC);
    }

    // create the vnode in the supplied slot
    err = vnode_create(*ret, type);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_VNODE_CREATE);
    }

    return SYS_ERR_OK;
}

__attribute__((unused)) static errval_t pt_alloc_l1(struct paging_state *st, struct capref *ret)
{
    return pt_alloc(st, ObjType_VNode_AARCH64_l1, ret);
}

__attribute__((unused)) static errval_t pt_alloc_l2(struct paging_state *st, struct capref *ret)
{
    return pt_alloc(st, ObjType_VNode_AARCH64_l2, ret);
}

__attribute__((unused)) static errval_t pt_alloc_l3(struct paging_state *st, struct capref *ret)
{
    return pt_alloc(st, ObjType_VNode_AARCH64_l3, ret);
}


/**
 * @brief initializes the paging state struct for the current process
 *
 * @param[in] st           the paging state to be initialized
 * @param[in] start_vaddr  start virtual address to be managed
 * @param[in] root         capability to the root leve page table
 * @param[in] ca           the slot allocator instance to be used
 *
 * @return SYS_ERR_OK on success, or LIB_ERR_* on failure
 */
errval_t paging_init_state(struct paging_state *st, lvaddr_t start_vaddr, struct capref root,
                           struct slot_allocator *ca)
{
    //errval_t err;
    // TODO (M1):
    //  - Implement basic state struct initialization
    // TODO (M2):
    //  -  Implement page fault handler that installs frames when a page fault
    //     occurs and keeps track of the virtual address space.
    
    // set some metadata
    st->current_vaddr = start_vaddr;
    st->start_vaddr = start_vaddr;
    st->slot_alloc = ca;
   
    // initialize a slab allocator to give us our memory
    slab_init(&st->ma, sizeof(struct pageTable), NULL);
    slab_grow(&st->ma, st->slab_buf, SLAB_STATIC_SIZE(NUM_PTS_ALLOC, sizeof(struct pageTable)));
    
    // Initialize first L0 table metadata and other metadata
    struct pageTable *pt = slab_alloc(&(st->ma));
    pt->offset = 0;
    pt->self = root;
    pt->numFree = NUM_PT_SLOTS;
    pt->parent = NULL;
    for (int i = 0; i < NUM_PT_SLOTS; i++) {
        pt->children[i] = NULL;
    }
    st->root = pt;

    return SYS_ERR_OK;
}


/**
 * @brief initializes the paging state struct for a foreign process when spawning a new one
 *
 * @param[in] st           the paging state to be initialized
 * @param[in] start_vaddr  start virtual address to be managed
 * @param[in] root         capability to the root leve page table
 * @param[in] ca           the slot allocator instance to be used
 *
 * @return SYS_ERR_OK on success, or LIB_ERR_* on failure
 */
errval_t paging_init_state_foreign(struct paging_state *st, lvaddr_t start_vaddr,
                                   struct capref root, struct slot_allocator *ca)
{
    // make compiler happy about unused parameters
    (void)st;
    (void)start_vaddr;
    (void)root;
    (void)ca;

    // TODO (M3): Implement state struct initialization

    st->current_vaddr = start_vaddr;
    st->start_vaddr = start_vaddr;
    st->slot_alloc = ca;
   
    // initialize a slab allocator to give us our memory
    slab_init(&st->ma, sizeof(struct pageTable), NULL);
    slab_grow(&st->ma, st->slab_buf, SLAB_STATIC_SIZE(NUM_PTS_ALLOC, sizeof(struct pageTable)));
    
    // Initialize first L0 table metadata and other metadata
    struct pageTable *pt = slab_alloc(&(st->ma));
    pt->offset = 0;
    pt->self = root;
    pt->parent = NULL;
    for (int i = 0; i < NUM_PT_SLOTS; i++) {
        pt->children[i] = NULL;
    }
    st->root = pt;

    // Initialize the actual L0 page table
    // errval_t err = vnode_create(root, ObjType_VNode_AARCH64_l0);
    // DEBUG_ERR_ON_FAIL(err, "paging_init_state_foreign: Failed to create hardware l0 page table");

    return SYS_ERR_OK;
}

/**
 * @brief This function initializes the paging for this domain
 *
 * Note: The function is called once before main.
 */
errval_t paging_init(void)
{
    // TODO (M1): Call paging_init_state for &current
    // TODO (M2): initialize self-paging handler
    // TIP: use thread_set_exception_handler() to setup a page fault handler
    // TIP: Think about the fact that later on, you'll have to make sure that
    // you can handle page faults in any thread of a domain.
    // TIP: it might be a good idea to call paging_init_state() from here to
    // avoid code duplication.
    paging_init_state(&current, ((uint64_t)1)<<46, cap_vroot, get_default_slot_allocator());
    set_current_paging_state(&current);
    return SYS_ERR_OK;
}


/**
 * @brief frees up the resources allocate in the foreign paging state
 *
 * @param[in] st   the foreign paging state to be freed
 *
 * @return SYS_ERR_OK on success, or LIB_ERR_* on failure
 *
 * Note: this function will free up the resources of *the current* paging state
 * that were used to construct the foreign paging state. There should be no effect
 * on the paging state of the foreign process.
 */
errval_t paging_free_state_foreign(struct paging_state *st)
{
    (void)st;
    // TODO: implement me
    return SYS_ERR_OK;
}


/**
 * @brief Initializes the paging functionality for the calling thread
 *
 * @param[in] t   the tread to initialize the paging state for.
 *
 * This function prepares the thread to handing its own page faults
 */
errval_t paging_init_onthread(struct thread *t)
{
    // make compiler happy about unused parameters
    (void)t;

    // TODO (M2):
    //   - setup exception handler for thread `t'.

    return LIB_ERR_NOT_IMPLEMENTED;
}


/**
 * @brief Find a free region of virtual address space that is large enough to accomodate a
 *        buffer of size 'bytes'.
 *
 * @param[in]  st          A pointer to the paging state to allocate from
 * @param[out] buf         Returns the free virtual address that was found.
 * @param[in]  bytes       The requested (minimum) size of the region to allocate
 * @param[in]  alignment   The address needs to be a multiple of 'alignment'.
 *
 * @return Either SYS_ERR_OK if no error occured or an error indicating what went wrong otherwise.
 */
errval_t paging_alloc(struct paging_state *st, void **buf, size_t bytes, size_t alignment)
{
    /**
     * TODO(M1):
     *    - use a linear allocation scheme. (think about what allocation sizes are valid)
     *
     * TODO(M2): Implement this function
     *   - Find a region of free virtual address space that is large enough to
     *     accomodate a buffer of size `bytes`.
     */
    size_t aligned_bytes = ROUND_UP(bytes, alignment);

    genvaddr_t vaddr = st->current_vaddr;
    size_t space = 0;
    genvaddr_t currentL0 = VMSAv8_64_L0_INDEX(vaddr);
    genvaddr_t currentL1 = VMSAv8_64_L1_INDEX(vaddr);
    genvaddr_t currentL2 = VMSAv8_64_L2_INDEX(vaddr);
    genvaddr_t currentL3 = VMSAv8_64_L3_INDEX(vaddr);
    // find a completely free address with enough space contiguous
    bool resetVaddr = false;
    while (space < aligned_bytes) {

        if (st->root->children[currentL0] == NULL ||
            st->root->children[currentL0]->children[currentL1] == NULL || 
            st->root->children[currentL0]->children[currentL1]->children[currentL2] == NULL ||
            st->root->children[currentL0]->children[currentL1]->
                      children[currentL2]->children[currentL3] == NULL) {
            
            space = space + BASE_PAGE_SIZE;         
        } else {
            resetVaddr = true;
        }

        currentL3++;
        if (currentL3 >= NUM_PT_SLOTS) {
            currentL3 = 0;
            currentL2++;
        }
        if (currentL2 >= NUM_PT_SLOTS) {
            currentL2 = 0;
            currentL1++;
        }
        if (currentL1 >= NUM_PT_SLOTS) {
            currentL1 = 0;
            currentL0++;
        }
        if (currentL0 >= NUM_PT_SLOTS) {
            vaddr = st->start_vaddr;
            currentL0 = VMSAv8_64_L0_INDEX(vaddr);
            currentL1 = VMSAv8_64_L1_INDEX(vaddr);
            currentL2 = VMSAv8_64_L2_INDEX(vaddr);
            currentL3 = VMSAv8_64_L3_INDEX(vaddr);
            resetVaddr = false;
            space = 0;
        }

        if (resetVaddr) {
            resetVaddr = false;
            vaddr = VADDR_CALCULATE(currentL0, currentL1, currentL2, currentL3, 0);
            space = 0;
        }
    }
    
    
    *buf = (void*) vaddr;
    // move the address up so that reentrant threads don't take the memory we want
    // (it is still possible to reuse freed addresses because the vaddr will eventually loop back around)
    // TODO: is the adding of BASE_PAGE_SIZE necessary?
    st->current_vaddr = ROUND_UP(vaddr + bytes + BASE_PAGE_SIZE,BASE_PAGE_SIZE);
    return SYS_ERR_OK;
}


errval_t mapNewPT(struct paging_state * st, capaddr_t slot, 
                  uint64_t offset, uint64_t pte_ct, enum objtype type, struct pageTable * parent) {
    errval_t err;

    //slab_check_and_refill(&(st->ma));
    parent->children[slot] = (struct pageTable*)slab_alloc(&(st->ma));
    
    struct capref mapping;
    err = st->slot_alloc->alloc(st->slot_alloc, &mapping);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_SLOT_ALLOC);
    }
    pt_alloc(st, type, &(parent->children[slot]->self));

    
    err = vnode_map(parent->self, parent->children[slot]->self, 
                    slot, VREGION_FLAGS_READ_WRITE, offset, pte_ct, mapping);
    if (err_is_fail(err)) {
        printf("     vnode_map failed mapping: %s\n", err_getstring(err));
        return -1;
    }

    for (int i = 0; i < NUM_PT_SLOTS; i++) {
        parent->children[slot]->children[i] = NULL;
    }

    return SYS_ERR_OK;
}



/**
 * @brief maps a frame at a free virtual address region and returns its address
 *
 * @param[in]  st      paging state of the address space to create the mapping in
 * @param[out] buf     returns the virtual address of the mapped frame
 * @param[in]  bytes   the amount of bytes to be mapped
 * @param[in]  frame   frame capability of backing memory to be mapped
 * @param[in]  offset  offset into the frame capability to be mapped
 * @param[in]  flags   mapping flags
 *
 * @return SYS_ERR_OK on sucecss, LIB_ERR_* on failure.
 */
errval_t paging_map_frame_attr_offset(struct paging_state *st, void **buf, size_t bytes,
                                      struct capref frame, size_t offset, int flags)
{
    errval_t err;
    // TODO(M1):
    //  - decide on which virtual address to map the frame at
    //  - map the frame assuming all mappings will fit into one leaf page table (L3)  (fail otherwise)
    //  - return the virtual address of the created mapping
    //
    // Hint:
    //  - keep it simple: use a linear allocator like st->vaddr_start += ...

    // find and reserve an empty area of the virtual address space
    err = paging_alloc(st, buf, bytes, BASE_PAGE_SIZE);
    DEBUG_ERR_ON_FAIL(err, "couldn't allocate a page for mapping\n");
    
    // map the found slot
    genvaddr_t vaddr = (genvaddr_t)*buf;
    err = paging_map_fixed_attr_offset(st, vaddr, frame, bytes, offset, flags);
    if (err_is_fail(err)) {
        printf("vnode_map failed: %s\n", err_getstring(err));
        return err;
    }
    

    // TODO(M2):
    // - General case: you will need to handle mappings spanning multiple leaf page tables.
    // - Find and allocate free region of virtual address space of at least bytes in size.
    // - Map the user provided frame at the free virtual address
    // - return the virtual address in the buf parameter
    //
    // Hint:
    //  - think about what mapping configurations are actually possible

    return SYS_ERR_OK;
}

/**
 * @brief maps a frame at a user-provided virtual address region
 *
 * @param[in] st      paging state of the address space to create the mapping in
 * @param[in] vaddr   provided virtual address to map the frame at
 * @param[in] frame   frame capability of backing memory to be mapped
 * @param[in] bytes   the amount of bytes to be mapped
 * @param[in] offset  offset into the frame capability to be mapped
 * @param[in] flags   mapping flags
 *
 * @return SYS_ERR_OK on success, LIB_ERR_* on failure
 *
 * The region at which the frame is requested to be mapped must be free (i.e., hasn't been
 * allocated), otherwise the mapping request shoud fail.
 */
errval_t paging_map_fixed_attr_offset(struct paging_state *st, lvaddr_t vaddr, struct capref frame,
                                      size_t bytes, size_t offset, int flags)
{
    errval_t err;
    int numMapped;

    // TODO(M2):
    //  - General case: you will need to handle mappings spanning multiple leaf page tables.
    //  - Make sure to update your paging state to reflect the newly mapped region
    //  - Map the user provided frame at the provided virtual address
    //
    // Hint:
    //  - think about what mapping configurations are actually possible
    //
        
    // number of pages to map
    int originalNumPages = ROUND_UP(bytes, BASE_PAGE_SIZE) / BASE_PAGE_SIZE;
    int numPages = originalNumPages;

    // // detect duplicate mappings
    // for (int checkPage = 0; checkPage < originalNumPages; checkPage++) {
    //     if (st->root->children[VMSAv8_64_L0_INDEX(vaddr)] != NULL &&
    //             st->root->children[VMSAv8_64_L0_INDEX(vaddr)]->
    //                       children[VMSAv8_64_L1_INDEX(vaddr)] != NULL &&
    //             st->root->children[VMSAv8_64_L0_INDEX(vaddr)]->
    //                       children[VMSAv8_64_L1_INDEX(vaddr)]->
    //                       children[VMSAv8_64_L2_INDEX(vaddr)] != NULL &&
    //             st->root->children[VMSAv8_64_L0_INDEX(vaddr)]->
    //                       children[VMSAv8_64_L1_INDEX(vaddr)]->
    //                       children[VMSAv8_64_L2_INDEX(vaddr)]->
    //                       children[VMSAv8_64_L3_INDEX(vaddr)] != NULL) {
    //         return LIB_ERR_VSPACE_REGION_OVERLAP;
    //     }
    // }

    // map pages in L3 page table-sized chunks
    for (int i = 0; numPages > 0; i++) {
        // If necessary allocate and initialize a new L1 pagetable
        if (st->root->children[VMSAv8_64_L0_INDEX(vaddr)] == NULL) {
            // mapNewPt() is a helper function that adds a new page 
            // table of the type provided to the page table provided
            err = mapNewPT(st, VMSAv8_64_L0_INDEX(vaddr), offset, 1, ObjType_VNode_AARCH64_l1, st->root);
            if (err_is_fail(err)) {
                printf("pt_alloc_l1 failed: %s\n", err_getstring(err));
                return err;
            }
        }
        // If necessary allocate and initialize a new L2 pagetable
        if (st->root->children[VMSAv8_64_L0_INDEX(vaddr)]->
                      children[VMSAv8_64_L1_INDEX(vaddr)] == NULL) {
            err = mapNewPT(st, VMSAv8_64_L1_INDEX(vaddr), offset, 1, ObjType_VNode_AARCH64_l2, 
                     st->root->children[VMSAv8_64_L0_INDEX(vaddr)]);
            if (err_is_fail(err)) {
                printf("pt_alloc_l2 failed: %s\n", err_getstring(err));
                return err;
            }
        }
        // If necessary allocate and initialize a new L3 pagetable
        if (st->root->children[VMSAv8_64_L0_INDEX(vaddr)]->children[VMSAv8_64_L1_INDEX(vaddr)]->
                      children[VMSAv8_64_L2_INDEX(vaddr)] == NULL) {
            err = mapNewPT(st, VMSAv8_64_L2_INDEX(vaddr), offset, 1, ObjType_VNode_AARCH64_l3, 
                     st->root->children[VMSAv8_64_L0_INDEX(vaddr)]->
                               children[VMSAv8_64_L1_INDEX(vaddr)]);
            if (err_is_fail(err)) {
                printf("pt_alloc_l3 failed: %s\n", err_getstring(err));
                return err;
            }
        }
        
        
        // allocate a slot for the mapping of the pages in the L3 page table
        struct capref mapping;
        err = st->slot_alloc->alloc(st->slot_alloc, &(mapping));
        if (err_is_fail(err)) {
            return err_push(err, LIB_ERR_SLOT_ALLOC);
        }

        // map the maximum number of pages that we can fit in this L3 page table
        //debug_printf("awful capref at L0 index %d L1 index %d L2 index %d and L3 index %d:\n", VMSAv8_64_L0_INDEX(vaddr), VMSAv8_64_L1_INDEX(vaddr),VMSAv8_64_L2_INDEX(vaddr), VMSAv8_64_L3_INDEX(vaddr));
        // debug_print_cap_at_capref(st->root->children[VMSAv8_64_L0_INDEX(vaddr)]->
        //                           children[VMSAv8_64_L1_INDEX(vaddr)]->
        //                           children[VMSAv8_64_L2_INDEX(vaddr)]->self);
        numMapped = MIN((int)(NUM_PT_SLOTS - VMSAv8_64_L3_INDEX(vaddr)), numPages);
        err = vnode_map(st->root->children[VMSAv8_64_L0_INDEX(vaddr)]->
                                  children[VMSAv8_64_L1_INDEX(vaddr)]->
                                  children[VMSAv8_64_L2_INDEX(vaddr)]->self, frame,
                                  VMSAv8_64_L3_INDEX(vaddr), flags, 
                                  offset + (BASE_PAGE_SIZE * (originalNumPages - numPages)), numMapped, mapping);
        if (err_is_fail(err)) {
            printf("vnode_map failed mapping leaf node: %s\n", err_getstring(err));
            return err;
        }

        // book keeping for unmapping later
        st->root->children[VMSAv8_64_L0_INDEX(vaddr)]->children[VMSAv8_64_L1_INDEX(vaddr)]->
                  children[VMSAv8_64_L2_INDEX(vaddr)]->children[VMSAv8_64_L3_INDEX(vaddr)] = slab_alloc(&st->ma);
        st->root->children[VMSAv8_64_L0_INDEX(vaddr)]->children[VMSAv8_64_L1_INDEX(vaddr)]->
                  children[VMSAv8_64_L2_INDEX(vaddr)]->children[VMSAv8_64_L3_INDEX(vaddr)]->mapping = mapping;
        st->root->children[VMSAv8_64_L0_INDEX(vaddr)]->children[VMSAv8_64_L1_INDEX(vaddr)]->
                  children[VMSAv8_64_L2_INDEX(vaddr)]->children[VMSAv8_64_L3_INDEX(vaddr)]->numBytes = bytes;
        
        // set all the rest of the children in this L3 page table to not null (so we see them as unused) 
        // except for the first one where we store our book keeping
        // TODO: potentially save the extra slots in our L3 page table if the optomization is needed.
        vaddr+=BASE_PAGE_SIZE;
        for (int j = VMSAv8_64_L3_INDEX(vaddr); j < NUM_PT_SLOTS; j++) {
            st->root->children[VMSAv8_64_L0_INDEX(vaddr)]->children[VMSAv8_64_L1_INDEX(vaddr)]->
                  children[VMSAv8_64_L2_INDEX(vaddr)]->children[VMSAv8_64_L3_INDEX(vaddr)] 
                  = (void*) 1;
            vaddr += BASE_PAGE_SIZE;
        }

        // update loop variable
        numPages -= numMapped;
        // printf("slab size:  %p\n", slab_freecount(&st->ma));
        // printf("slot size:  %p\n", st->slot_alloc->space);
        // refill the slab if necessary
        err = slab_check_and_refill(&(st->ma));
        if (err_is_fail(err)) {
            printf("slab alloc error: %s\n", err_getstring(err));
            return LIB_ERR_SLAB_REFILL;
        }
    }
    
    return SYS_ERR_OK;
}


/**
 * @brief Unmaps the region starting at the supplied pointer.
 *
 * @param[in] st      the paging state to create the mapping in
 * @param[in] region  starting address of the region to unmap
 *
 * @return SYS_ERR_OK on success, or error code indicating the kind of failure
 *
 * The supplied `region` must be the start of a previously mapped frame.
 */
errval_t paging_unmap(struct paging_state *st, const void *region)
{
    // make compiler happy about unused parameters
    (void)st;
    (void)region;

    // TODO(M2):
    //  - implemet unmapping of a previously mapped region

    // check if the region is allocated.
    if (st->root->children[VMSAv8_64_L0_INDEX(region)]==NULL
        ||st->root->children[VMSAv8_64_L0_INDEX(region)]->
                    children[VMSAv8_64_L1_INDEX(region)]==NULL
        ||st->root->children[VMSAv8_64_L0_INDEX(region)]->
                    children[VMSAv8_64_L1_INDEX(region)]->
                    children[VMSAv8_64_L2_INDEX(region)]==NULL 
        ||st->root->children[VMSAv8_64_L0_INDEX(region)]->
                    children[VMSAv8_64_L1_INDEX(region)]->
                    children[VMSAv8_64_L2_INDEX(region)]->
                    children[VMSAv8_64_L3_INDEX(region)]==NULL
        ) {
        printf("region is not allocated\n");
        return SYS_ERR_VM_ALREADY_MAPPED;
    }
    // find out the size of the region to unmap (stored in each L3 page table 
    // since mappings are done by groups of L3 page tables)
    uint64_t bytes_to_unmap = st->root->children[VMSAv8_64_L0_INDEX(region)]->
                                        children[VMSAv8_64_L1_INDEX(region)]->
                                        children[VMSAv8_64_L2_INDEX(region)]->
                                        children[VMSAv8_64_L3_INDEX(region)]->numBytes;

    
    // continually unmap the existing mappings until we've gone over the limit. 
    uint64_t bytes_unmapped = 0;
    while (bytes_unmapped < bytes_to_unmap) {
        vnode_unmap(st->root->children[VMSAv8_64_L0_INDEX(region)]->
                              children[VMSAv8_64_L1_INDEX(region)]->
                              children[VMSAv8_64_L2_INDEX(region)]->self, 
                    st->root->children[VMSAv8_64_L0_INDEX(region)]->
                              children[VMSAv8_64_L1_INDEX(region)]->
                              children[VMSAv8_64_L2_INDEX(region)]->
                              children[VMSAv8_64_L3_INDEX(region)]->mapping);
        
        // be sure to mark the L3 PT slots unused
        for (uint64_t i = 0; i < MIN(NUM_PT_SLOTS, bytes_to_unmap-bytes_unmapped); i++) {
            st->root->children[VMSAv8_64_L0_INDEX(region)]->
                      children[VMSAv8_64_L1_INDEX(region)]->
                      children[VMSAv8_64_L2_INDEX(region)]->
                      children[VMSAv8_64_L3_INDEX(region)]->
                      children[i] = NULL;
        }
        bytes_unmapped += BASE_PAGE_SIZE * 512;
        region += BASE_PAGE_SIZE * 512;
    }
    return SYS_ERR_OK;
}
