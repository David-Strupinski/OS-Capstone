/**
 * \file
 * \brief A library for managing physical memory (i.e., caps)
 */

/*
 * Copyright (c) 2008, 2011, ETH Zurich.
 * Copyright (c) 2022, The University of British Columbia.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstr. 6, CH-8092 Zurich. Attn: Systems Group.
 */

#include <string.h>
#include <aos/debug.h>
#include <aos/solution.h>
#include <mm/mm.h>



/**
 * @brief initializes the memory manager instance
 *
 * @param[in] mm        memory manager instance to initialize
 * @param[in] objtype   type of the capabilities stored in the memory manager
 * @param[in] ca        capability slot allocator to be used
 * @param[in] refill    slot allocator refill function to be used
 * @param[in] slab_buf  initial buffer space for slab allocators
 * @param[in] slab_sz   size of the initial slab buffer
 *
 * @return error value indicating success or failure
 *  - @retval SYS_ERR_OK if the memory manager was successfully initialized
 */
errval_t mm_init(struct mm *mm, enum objtype objtype, struct slot_allocator *ca,
                 slot_alloc_refill_fn_t refill, void *slab_buf, size_t slab_sz)
{
    // for (int i = 0; i < 100; i++) {
    // debug_printf("made it to start of mm_init\n");
    // }
    // initialize mm instance
    mm->objtype = objtype;
    mm->ca = ca;
    mm->refill = refill;
    mm->free_mem = 0;
    mm->total_mem = 0;

    // TODO: use these parameters
    (void)slab_buf;
    (void)slab_sz;
    
    // initialize the slab allocator that holds the metadata
    // TODO: change this to be dynamically allocated
    slab_init(&mm->ma, sizeof(struct metadata), NULL);
    slab_grow(&mm->ma, mm->slab_buf, SLAB_STATIC_SIZE(NumStructAlloc, sizeof(struct metadata)));
    
    return SYS_ERR_OK;
}


/**
 * @brief destroys an mm instance
 *
 * @param[in] mm  memory manager instance to be freed
 *
 * @return error value indicating success or failure
 *  - @retval SYS_ERR_OK if the memory manager was successfully destroyed
 *
 * @note: does not free the mm object itself
 *
 * @note: This function is here for completeness. Think about how you would implement it.
 *        It's implementation is not required.
 */
errval_t mm_destroy(struct mm *mm)
{
    // make the compiler happy
    (void)mm;

    UNIMPLEMENTED();
    return LIB_ERR_NOT_IMPLEMENTED;
}


/**
 * @brief adds new memory resources to the memory manager represented by the capability
 *
 * @param[in] mm   memory manager instance to add resources to
 * @param[in] cap  memory resources to be added to the memory manager instance
 *
 * @return error value indicating the success of the operation
 *  - @retval SYS_ERR_OK              on success
 *  - @retval MM_ERR_CAP_INVALID      if the supplied capability is invalid (size, alignment)
 *  - @retval MM_ERR_CAP_TYPE         if the supplied capability is not of the expected type
 *  - @retval MM_ERR_ALREADY_PRESENT  if the supplied memory is already managed by this allocator
 *  - @retval MM_ERR_SLAB_ALLOC_FAIL  if the memory for the new node's meta data could not be allocated
 *
 * @note: the memory manager instance must be initialized before calling this function.
 *
 * @note: the function transfers ownership of the capability to the memory manager
 *
 * @note: to return allocated memory to the allocator, see mm_free()
 */
errval_t mm_add(struct mm *mm, struct capref cap)
{
    errval_t err;
    
    // get the capability
    struct capability capability;
    err = cap_direct_identify(cap, &capability);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_CAP_IDENTIFY);
    }
    
    // check for invalid input
    if (capability.type != ObjType_RAM) {
        // not a RAM capability
        return MM_ERR_CAP_TYPE;
    }
    if (capability.u.ram.bytes <= 0 || capability.u.ram.base % BASE_PAGE_SIZE != 0) {
        // RAM doesn't exist or is not aligned to page boundary
        return MM_ERR_CAP_INVALID;
    }
    
    // walk the metadata and see if the memory is already managed, 
    // returning MM_ERR_ALREADY_PRESENT if so
    struct metadata *curr = mm->freelist;
    while (curr != NULL) {
        struct capability curr_cap;
        err = cap_direct_identify(curr->capability, &curr_cap);
        if (err_is_fail(err)){
            return err_push(err, LIB_ERR_CAP_IDENTIFY);
        }
        if (curr_cap.u.ram.base == capability.u.ram.base) {
            return MM_ERR_ALREADY_PRESENT;
        }
        curr = curr->next;
    }

    // allocate a slab for metadata
    struct metadata *cap_metadata = slab_alloc(&(mm->ma));
    if (cap_metadata == NULL) {
        return MM_ERR_SLAB_ALLOC_FAIL;
    }

    // add the new capability to the free list
    cap_metadata->next = mm->freelist;
    cap_metadata->prev = NULL;
    if (mm->freelist != NULL) mm->freelist->prev = cap_metadata;
    mm->freelist = cap_metadata;
    cap_metadata->capability = cap;
    cap_metadata->capability_base = capability.u.ram.base;
    cap_metadata->base = capability.u.ram.base;
    cap_metadata->size = capability.u.ram.bytes;
    cap_metadata->used = false;

    // update the free and total memory
    mm->free_mem += capability.u.ram.bytes;
    mm->total_mem += capability.u.ram.bytes;

    return SYS_ERR_OK;
}


/**
 * @brief allocates memory with the requested size and alignment
 *
 * @param[in]  mm         memory manager instance to allocate from
 * @param[in]  size       minimum requested size of the memory region to allocate
 * @param[in]  alignment  minimum alignment requirement for the allocation
 * @param[out] retcap     returns the capability to the allocated memory
 *
 * @return error value indicating the success of the operation
 *  - @retval SYS_ERR_OK                on success
 *  - @retval MM_ERR_BAD_ALIGNMENT      if the requested alignment is not a power of two
 *  - @retval MM_ERR_OUT_OF_MEMORY      if there is not enough memory to satisfy the request
 *  - @retval MM_ERR_ALLOC_CONSTRAINTS  if there is memory, but the constraints are too tight
 *  - @retval MM_ERR_SLOT_ALLOC_FAIL    failed to allocate slot for new capability
 *  - @retval MM_ERR_SLAB_ALLOC_FAIL    failed to allocate memory for meta data
 *
 * @note The function allocates memory and returns a capability to it back to the caller.
 * The size of the returned capability is a multiple of BASE_PAGE_SIZE. Alignment requests
 * must be a power of two starting from BASE_PAGE_SIZE.
 *
 * @note The returned ownership of the capability is transferred to the caller.
 */
errval_t mm_alloc_aligned(struct mm *mm, size_t size, size_t alignment, struct capref *retcap)
{
    size_t aligned_size = ROUND_UP(size, alignment);

    // check alignment input value (power of two starting at base page size)
    if (alignment < BASE_PAGE_SIZE) {
        return MM_ERR_BAD_ALIGNMENT;
    }
    if ((alignment & (alignment - 1)) != 0) {
        return MM_ERR_BAD_ALIGNMENT;
    }

    // check that we have enough memory
    if (mm->free_mem < aligned_size || mm->free_mem < BASE_PAGE_SIZE) {
        return MM_ERR_OUT_OF_MEMORY;
    }

    // traverse the metadata list looking for a free space
    for (struct metadata * curr = mm->freelist; curr != NULL; curr = curr->next) {
        // determine the required alignment and skip this node if it is too great
        size_t alignment_offset = alignment - (curr->base % alignment);
        if (alignment_offset == alignment) {
            alignment_offset = 0;
        }
        if (alignment_offset >= curr->size) {
            curr = curr->next;
            continue;
        }

        // compute the required base and size, allocating this node if everything fits
        genpaddr_t potential_base = curr->base + alignment_offset; 
        size_t potential_size = curr->size - alignment_offset;
        if (curr->used == false && potential_size >= aligned_size && potential_size >= BASE_PAGE_SIZE) {
            // refill the slab allocator if necessary
            if (slab_freecount(&(mm->ma)) < 16) { //!mm->currentlyRefillingSA) {
                // TODO: refill the slab allocator
                //mm->currentlyRefillingSA = true;
                //mm->ma.mem_manager = mm;
                //slab_default_refill(&(mm->ma));
                //mm->ma.mem_manager = NULL;
                //mm->currentlyRefillingSA = false;
            }

            // split a node if there is enough space but the alignment is not correct
            if (alignment_offset > 0) {
                // allocate space for the new node and set the fields
                struct metadata *splitoff = slab_alloc(&(mm->ma));
                if (splitoff == NULL) {
                    return MM_ERR_SLAB_ALLOC_FAIL;
                }

                // set the new node's fields
                splitoff->base = curr->base;
                splitoff->size = alignment_offset;
                splitoff->used = false;
                splitoff->prev = curr->prev;
                splitoff->next = curr;
                splitoff->capability = curr->capability;
                splitoff->capability_base = curr->capability_base;
                if (curr->prev == NULL) {
                    mm->freelist = splitoff;
                } else {
                    curr->prev->next = splitoff;
                }
                curr->base = potential_base;
                curr->size = curr->size - alignment_offset;
                curr->prev = splitoff;
            }

            // split off the remainder of the node if possible
            if (curr->size > aligned_size) {
                // allocate space for the new node and set the fields
                struct metadata *splitoff = slab_alloc(&(mm->ma));
                if (splitoff == NULL) {
                    return MM_ERR_SLAB_ALLOC_FAIL;
                }

                // set the new node's fields
                splitoff->base = curr->base + aligned_size;
                splitoff->size = curr->size - aligned_size;
                splitoff->used = false;
                splitoff->prev = curr;
                splitoff->next = curr->next;
                splitoff->capability = curr->capability;
                splitoff->capability_base = curr->capability_base;
                curr->size = aligned_size;
                curr->next = splitoff;
            }
            
            // allocate a new slot for the return capability
            struct slot_prealloc *ca = (struct slot_prealloc *)mm->ca;
            //errval_t err = slot_prealloc_refill(ca);
            //if (err_is_fail(err)) {
            //    return MM_ERR_ALLOC_CONSTRAINTS;
            //}
            slot_prealloc_alloc(ca, retcap);
            
            // copy the original capability (with updated fields) into the new slot
            gensize_t aligned_offset = curr->base - curr->capability_base;
            if (aligned_offset % alignment != 0) {
                aligned_offset = aligned_offset + alignment - (aligned_offset % alignment);
            }
            cap_retype(*retcap, curr->capability, aligned_offset, ObjType_RAM, aligned_size);

            // mark the current metadata node as used and return
            curr->used = true;
            return SYS_ERR_OK;
        }
    }

    // no suitable block was found
    return MM_ERR_ALLOC_CONSTRAINTS;
}


/**
 * @brief allocates memory of a given size within a given base-limit range (EXTRA CHALLENGE)
 *
 * @param[in]  mm         memory manager instance to allocate from
 * @param[in]  base       minimum requested address of the memory region to allocate
 * @param[in]  limit      maximum requested address of the memory region to allocate
 * @param[in]  size       minimum requested size of the memory region to allocate
 * @param[in]  alignment  minimum alignment requirement for the allocation
 * @param[out] retcap     returns the capability to the allocated memory
 *
 * @return error value indicating the success of the operation
 *  - @retval SYS_ERR_OK                on success
 *  - @retval MM_ERR_BAD_ALIGNMENT      if the requested alignment is not a power of two
 *  - @retval MM_ERR_OUT_OF_MEMORY      if there is not enough memory to satisfy the request
 *  - @retval MM_ERR_ALLOC_CONSTRAINTS  if there is memory, but the constraints are too tight
 *  - @retval MM_ERR_OUT_OF_BOUNDS      if the supplied range is not within the allocator's range
 *  - @retval MM_ERR_SLOT_ALLOC_FAIL    failed to allocate slot for new capability
 *  - @retval MM_ERR_SLAB_ALLOC_FAIL    failed to allocate memory for meta data
 *
 * The returned capability should be within [base, limit] i.e., base <= cap.base,
 * and cap.base + cap.size <= limit.
 *
 * The requested alignment should be a power two of at least BASE_PAGE_SIZE.
 */
errval_t mm_alloc_from_range_aligned(struct mm *mm, size_t base, size_t limit, size_t size,
                                     size_t alignment, struct capref *retcap)
{
    // for (int i = 0; i < 100; i++) {
    // debug_printf("made it to start of mm_alloc_from_range_aligned\n");
    // }
    // make compiler happy about unused parameters
    (void)mm;
    (void)base;
    (void)limit;
    (void)size;
    (void)alignment;
    (void)retcap;

    // Perform allocations with the give alignment and size that are within the supplied
    /// base and limit range.

    UNIMPLEMENTED();
    return LIB_ERR_NOT_IMPLEMENTED;
}

/**
 * @brief frees a previously allocated memory by returning it to the memory manager
 *
 * @param[in] mm   the memory manager instance to return the freed memory to
 * @param[in] cap  capability of the memory to be freed
 *
 * @return error value indicating the success of the operation
 *   - @retval SYS_ERR_OK            The memory was successfully freed and added to the allocator
 *   - @retval MM_ERR_NOT_FOUND      The memory was not allocated by this allocator
 *   - @retval MM_ERR_DOUBLE_FREE    The (parts of) memory region has already been freed
 *   - @retval MM_ERR_CAP_TYPE       The capability is not of the correct type
 *   - @retval MM_ERR_CAP_INVALID    The supplied cabability was invalid or does not exist.
 *
 * @pre  The function assumes that the capability passed in is no where else used.
 *       It is the only copy and there are no descendants of it. Calling functions need
 *       to ensure this. Later allocations can safely hand out the freed capability again.
 *
 * @note The memory to be freed must have been added to the `mm` instance and it must have been
 *       allocated before, otherwise an error is to be returned.
 *
 * @note The ownership of the capability slot is transferred to the memory manager and may
 *       be recycled for future allocations.
 */
errval_t mm_free(struct mm *mm, struct capref cap)
{
    // TODO:
    //   - add the memory back to the allocator by marking the region as free
    //
    // You can assume that the capability was the one returned by a previous call
    // to mm_alloc() or mm_alloc_aligned(). For the extra challenge, you may also
    // need to handle partial frees, where a capability was split up by the client

    // get the capability
    struct capability capability;
    errval_t err = cap_direct_identify(cap, &capability);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_CAP_IDENTIFY);
    }

    // check that the capability is a RAM capability
    if (capability.type != ObjType_RAM) {
        return MM_ERR_CAP_TYPE;
    }

    // check that the supplied capability is valid
    if (capability.u.ram.bytes <= 0 || capability.u.ram.base % BASE_PAGE_SIZE != 0) {
        return MM_ERR_CAP_INVALID;
    } 

    // search the freelist for the capability, returning an error if it is not found
    struct metadata *curr = mm->freelist;
    bool found = false;
    while (curr != NULL && !found) {
        if (curr->base == capability.u.ram.base) {
            found = true;
        } else {
            curr = curr->next;
        }
    }
    if (!found) return MM_ERR_NOT_FOUND;

    // check that the region hasn't already been freed
    if (curr->used == false) {
        return MM_ERR_DOUBLE_FREE;
    }
    
    // free the capability
    err = cap_destroy(cap);
    if (err_is_fail(err)) {
        return MM_ERR_NOT_FOUND;
    }

    // deallocate the memory by marking the metadata node as unused
    curr->used = false;

    // combine with the surrounding metadata nodes if they are free and from 
    // the same original capability
    if (curr->prev != NULL && curr->prev->used == false && capcmp(curr->prev->capability, curr->capability)) {
        // coalesce with the previous node
        curr->prev->size += curr->size;
        curr->prev->next = curr->next;
        slab_free(&mm->ma, curr);
    }
    if (curr->next != NULL && curr->next->used == false && capcmp(curr->next->capability, curr->capability)) {
        // coalesce with the following node
        struct metadata *old_next = curr->next;
        curr->size += curr->next->size;
        curr->next = curr->next->next;
        slab_free(&mm->ma, old_next);
    }

    return SYS_ERR_OK;
}


/**
 * @brief returns the amount of available (free) memory of the memory manager
 *
 * @param[in] mm   memory manager instance to query
 *
 * @return the amount of memory available in bytes in the memory manager
 */
size_t mm_mem_available(struct mm *mm)
{
    return mm->free_mem;
}


/**
 * @brief returns the total amount of memory this mm instances manages.
 *
 * @param[in] mm   memory manager instance to query
 *
 * @return the total amount of memory in bytes of the memory manager
 */
size_t mm_mem_total(struct mm *mm)
{
    return mm->total_mem;
}


/**
 * @brief obtains the range of free memory of the memory allocator instance
 *
 * @param[in]  mm     memory manager instance to query
 * @param[out] base   returns the minimum address of free memroy
 * @param[out] limit  returns the maximum address of free memory
 *
 * Note: This is part of the extra challenge. You can ignore potential (allocation)
 *       holes in the free memory regions, and just return the smallest address of
 *       a region than is free, and likewise the highest address
 */
void mm_mem_get_free_range(struct mm *mm, lpaddr_t *base, lpaddr_t *limit)
{
    // make compiler happy about unused parameters
    (void)mm;
    (void)base;
    (void)limit;

    UNIMPLEMENTED();
}

/**
 * @brief print all blocks managed by allocator
 *
 * @param[in] mm   memory manager instance to query
 */
void mm_print_map(struct mm *mm) {
    printf("Managed memory: =============================================================\n");
    for (struct metadata *curr = mm->freelist; curr != NULL; curr = curr->next) {
        char *used_string = curr->used ? "Used" : "Free";
        printf("%s node of size %d at address %p\n", used_string, curr->size, curr->base);
    }
    printf("=============================================================================\n");
}