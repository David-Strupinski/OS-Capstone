/**
 * \file
 * \brief Hello world application
 */

/*
 * Copyright (c) 2016 ETH Zurich.
 * Copyright (c) 2022 The University of British Columbia.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, CAB F.78, Universitaetstr. 6, CH-8092 Zurich,
 * Attn: Systems Group.
 */


#include <stdio.h>

#include <aos/aos.h>
#include <grading/grading.h>
#include <grading/io.h>

int main(int argc, char *argv[])
{
    /// !!! Keep those prints here to make the tests go through
    grading_printf("Hello, world! from userspace (%u)\n", argc);

    for (int i = 0; i < argc; i++) {
        grading_printf("argv[%d] = %s\n", i, argv[i]);
    }

    // Get a cnoderef to the L2 table containing inherited capabilities.
    struct cnoderef frame_ref;
    frame_ref.croot = get_croot_addr(cap_root);
    frame_ref.cnode = ROOTCN_SLOT_ADDR(ROOTCN_SLOT_SLOT_ALLOC0);
    frame_ref.level = CNODE_TYPE_OTHER;
    
    // Find the first inherited capref.
    struct capref frame;
    frame.cnode = frame_ref;
    frame.slot = 0;

    // Check if we've inherited a capability.
    struct capability cap;
    errval_t err = cap_direct_identify(frame, &cap);
    if (!err_is_fail(err)) {
        // Map and print the value at the inherited capref.
        void *buf;
        err = paging_map_frame_attr(get_current_paging_state(), 
                                    &buf, 
                                    BASE_PAGE_SIZE, 
                                    frame, 
                                    VREGION_FLAGS_READ_WRITE);
        if (err_is_fail(err)) {
            printf("couldn't map passed frame capability in userspace\n");
        }
        printf("%s\n", (char *)buf);
    }

    while(1) {
        thread_yield();
    }

    return EXIT_SUCCESS;
}
