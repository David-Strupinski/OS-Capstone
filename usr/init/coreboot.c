#include <string.h>
#include <aos/aos.h>
#include <aos/deferred.h>
#include <spawn/multiboot.h>
#include <elf/elf.h>
#include <barrelfish_kpi/arm_core_data.h>
#include <aos/kernel_cap_invocations.h>
#include <aos/cache.h>

#include "coreboot.h"


#define ARMv8_KERNEL_OFFSET 0xffff000000000000

#define NEW_CORE_MEM_SZ 1024 * 1024 * 256     // 256 mib

extern struct platform_info platform_info;
extern struct bootinfo     *bi;

struct mem_info {
    size_t   size;       // Size in bytes of the memory region
    void    *buf;        // Address where the region is currently mapped
    lpaddr_t phys_base;  // Physical base address
};

/**
 * Load a ELF image into memory.
 *
 * binary:            Valid pointer to ELF image in current address space
 * mem:               Where the ELF will be loaded
 * entry_point:       Virtual address of the entry point
 * reloc_entry_point: Return the loaded, physical address of the entry_point
 */
__attribute__((__used__)) static errval_t load_elf_binary(genvaddr_t             binary,
                                                          const struct mem_info *mem,
                                                          genvaddr_t             entry_point,
                                                          genvaddr_t            *reloc_entry_point)

{
    struct Elf64_Ehdr *ehdr = (struct Elf64_Ehdr *)binary;

    /* Load the CPU driver from its ELF image. */
    bool found_entry_point = 0;
    bool loaded            = 0;

    struct Elf64_Phdr *phdr = (struct Elf64_Phdr *)(binary + ehdr->e_phoff);
    for (size_t i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type != PT_LOAD) {
            DEBUG_PRINTF("Segment %d load address 0x% " PRIx64 ", file size %" PRIu64
                         ", memory size 0x%" PRIx64 " SKIP\n",
                         i, phdr[i].p_vaddr, phdr[i].p_filesz, phdr[i].p_memsz);
            continue;
        }

        DEBUG_PRINTF("Segment %d load address 0x% " PRIx64 ", file size %" PRIu64 ", memory size "
                     "0x%" PRIx64 " LO"
                     "AD"
                     "\n",
                     i, phdr[i].p_vaddr, phdr[i].p_filesz, phdr[i].p_memsz);


        if (loaded) {
            USER_PANIC("Expected one load able segment!\n");
        }
        loaded = 1;

        void    *dest      = mem->buf;
        lpaddr_t dest_phys = mem->phys_base;

        assert(phdr[i].p_offset + phdr[i].p_memsz <= mem->size);

        /* copy loadable part */
        memcpy(dest, (void *)(binary + phdr[i].p_offset), phdr[i].p_filesz);

        /* zero out BSS section */
        memset(dest + phdr[i].p_filesz, 0, phdr[i].p_memsz - phdr[i].p_filesz);

        if (!found_entry_point) {
            if (entry_point >= phdr[i].p_vaddr && entry_point - phdr[i].p_vaddr < phdr[i].p_memsz) {
                *reloc_entry_point = (dest_phys + (entry_point - phdr[i].p_vaddr));
                found_entry_point  = 1;
            }
        }
    }

    if (!found_entry_point) {
        USER_PANIC("No entry point loaded\n");
    }

    return SYS_ERR_OK;
}

/**
 * Relocate an already loaded ELF image.
 *
 * binary:            Valid pointer to ELF image in current address space
 * mem:               Where the ELF is loaded
 * kernel_:       Virtual address of the entry point
 * reloc_entry_point: Return the loaded, physical address of the entry_point
 */
__attribute__((__used__)) static errval_t relocate_elf(genvaddr_t binary, struct mem_info *mem,
                                                       lvaddr_t load_offset)
{
    DEBUG_PRINTF("Relocating image.\n");

    struct Elf64_Ehdr *ehdr = (struct Elf64_Ehdr *)binary;

    size_t             shnum = ehdr->e_shnum;
    struct Elf64_Phdr *phdr  = (struct Elf64_Phdr *)(binary + ehdr->e_phoff);
    struct Elf64_Shdr *shead = (struct Elf64_Shdr *)(binary + (uintptr_t)ehdr->e_shoff);

    /* Search for relocaton sections. */
    for (size_t i = 0; i < shnum; i++) {
        struct Elf64_Shdr *shdr = &shead[i];
        if (shdr->sh_type == SHT_REL || shdr->sh_type == SHT_RELA) {
            if (shdr->sh_info != 0) {
                DEBUG_PRINTF("I expected global relocations, but got"
                             " section-specific ones.\n");
                return ELF_ERR_HEADER;
            }


            uint64_t segment_elf_base  = phdr[0].p_vaddr;
            uint64_t segment_load_base = mem->phys_base;
            uint64_t segment_delta     = segment_load_base - segment_elf_base;
            uint64_t segment_vdelta    = (uintptr_t)mem->buf - segment_elf_base;

            size_t rsize;
            if (shdr->sh_type == SHT_REL) {
                rsize = sizeof(struct Elf64_Rel);
            } else {
                rsize = sizeof(struct Elf64_Rela);
            }

            assert(rsize == shdr->sh_entsize);
            size_t nrel = shdr->sh_size / rsize;

            void *reldata = (void *)(binary + shdr->sh_offset);

            /* Iterate through the relocations. */
            for (size_t ii = 0; ii < nrel; ii++) {
                void *reladdr = reldata + ii * rsize;

                switch (shdr->sh_type) {
                case SHT_REL:
                    DEBUG_PRINTF("SHT_REL unimplemented.\n");
                    return ELF_ERR_PROGHDR;
                case SHT_RELA: {
                    struct Elf64_Rela *rel = reladdr;

                    uint64_t offset = rel->r_offset;
                    uint64_t sym    = ELF64_R_SYM(rel->r_info);
                    uint64_t type   = ELF64_R_TYPE(rel->r_info);
                    uint64_t addend = rel->r_addend;

                    uint64_t *rel_target = (void *)offset + segment_vdelta;

                    switch (type) {
                    case R_AARCH64_RELATIVE:
                        if (sym != 0) {
                            DEBUG_PRINTF("Relocation references a"
                                         " dynamic symbol, which is"
                                         " unsupported.\n");
                            return ELF_ERR_PROGHDR;
                        }

                        /* Delta(S) + A */
                        *rel_target = addend + segment_delta + load_offset;
                        break;

                    default:
                        DEBUG_PRINTF("Unsupported relocation type %d\n", type);
                        return ELF_ERR_PROGHDR;
                    }
                } break;
                default:
                    DEBUG_PRINTF("Unexpected type\n");
                    break;
                }
            }
        }
    }

    return SYS_ERR_OK;
}




/**
 * @brief boots a new core with the provided mpid
 *
 * @param[in]  mpid         The ARM MPID of the core to be booted
 * @param[in]  boot_driver  Path of the boot driver binary
 * @param[in]  cpu_driver   Path of the CPU driver binary
 * @param[in]  init         Path to the init binary
 * @param[out] core         Returns the coreid of the booted core
 *
 * @return SYS_ERR_OK on success, errval on failure
 */
errval_t coreboot_boot_core(hwid_t mpid, const char *boot_driver, const char *cpu_driver,
                            const char *init, coreid_t *core)
{
    // make compiler happy about unused parameters
    (void)init;
    (void)boot_driver;
    (void)cpu_driver;
    // make compiler happy about unused parameters
    (void)core;
    (void)mpid;

    errval_t err;

    // ============================================================================================
    // Get a new KCB by retyping a RAM cap to ObjType_KernelControlBlock.
    // Note that it should at least OBJSIZE_KCB, and it should also be aligned
    // to a multiple of 16k.
    // ============================================================================================

    struct capref kcb_ram_capref;
    err = ram_alloc_aligned(&kcb_ram_capref, OBJSIZE_KCB, 4 * BASE_PAGE_SIZE);
    DEBUG_ERR_ON_FAIL(err, "couldn't allocate ram for KCB\n");
    struct capref kcb_capref;
    err = slot_alloc(&kcb_capref);
    DEBUG_ERR_ON_FAIL(err, "couldn't allocate slot for KCB\n");
    err = cap_retype(kcb_capref, kcb_ram_capref, 0, ObjType_KernelControlBlock, OBJSIZE_KCB);
    DEBUG_ERR_ON_FAIL(err, "couldn't retype RAM capability into KCB capability\n");
    struct capability kcb_ram_capability;
    err = cap_direct_identify(kcb_ram_capref, &kcb_ram_capability);
    debug_printf("kcb base: %p size %lu\n", kcb_ram_capability.u.ram.base, kcb_ram_capability.u.ram.bytes);

    // ============================================================================================
    // Get and load the CPU driver binary.
    // ============================================================================================

    // find the CPU driver and get its frame
    struct mem_region *cpu_mr;
    cpu_mr = multiboot_find_module(bi, cpu_driver);
    if (cpu_mr == NULL) {
        debug_printf("couldn't find CPU driver module\n");
        return -1;
    }
    struct capref cpu_frame = {
        .cnode = cnode_module,
        .slot = cpu_mr->mrmod_slot,
    };

    // get the size of the CPU driver frame
    struct capability cpu_cap;
    err = cap_direct_identify(cpu_frame, &cpu_cap);
    DEBUG_ERR_ON_FAIL(err, "couldn't identify frame for CPU driver\n");
    size_t cpu_bytes = cpu_cap.u.frame.bytes;

    // map the CPU driver frame
    void *cpu_buf;
    err = paging_map_frame_attr(get_current_paging_state(), &cpu_buf, cpu_bytes, cpu_frame, VREGION_FLAGS_READ_WRITE);

    DEBUG_ERR_ON_FAIL(err, "couldn't map CPU driver frame\n");

    // create and map a new frame to hold the loaded ELF binary
    struct capref cpu_elf_frame;
    err = frame_alloc(&cpu_elf_frame, cpu_bytes, NULL);
    DEBUG_ERR_ON_FAIL(err, "couldn't allocate CPU driver ELF frame\n");
    void *cpu_elf_buf;
    err = paging_map_frame_attr(get_current_paging_state(), &cpu_elf_buf, cpu_bytes, cpu_elf_frame, VREGION_FLAGS_READ_WRITE);
    DEBUG_ERR_ON_FAIL(err, "couldn't map CPU driver ELF frame\n");
    struct capability cpu_elf_cap;
    err = cap_direct_identify(cpu_elf_frame, &cpu_elf_cap);
    DEBUG_ERR_ON_FAIL(err, "couldn't identify CPU driver ELF frame\n");

    // set up a mem_info struct to point to the loaded ELF binary frame
    struct mem_info cpu_mi;
    cpu_mi.buf = cpu_elf_buf;
    cpu_mi.phys_base = cpu_elf_cap.u.ram.base;
    cpu_mi.size = cpu_bytes;

    // get the physical entry point of the ELF binary
    struct Elf64_Sym *cpu_entry;
    cpu_entry = elf64_find_symbol_by_name((genvaddr_t)cpu_buf, cpu_bytes, "arch_init", 0, STT_FUNC, NULL);
    if (cpu_entry == NULL) {
        debug_printf("couldn't find arch_init\n");
        return -1;
    }
    genvaddr_t cpu_phys_entry_point = cpu_entry->st_value;

    // load the ELF binary into the ELF binary frame we set up earlier
    genvaddr_t cpu_reloc_entry_point;
    err = load_elf_binary((genvaddr_t)cpu_buf, &cpu_mi, cpu_phys_entry_point, &cpu_reloc_entry_point);
    DEBUG_ERR_ON_FAIL(err, "couldn't load CPU driver binary\n");

    // ============================================================================================
    // Get and load the boot driver binary.
    // ============================================================================================

    // find the boot driver and get its frame
    struct mem_region *boot_mr;
    boot_mr = multiboot_find_module(bi, boot_driver);
    if (boot_mr == NULL) {
        debug_printf("couldn't find boot driver module\n");
        return -1;
    }
    struct capref boot_frame;
    boot_frame.cnode = cnode_module;
    boot_frame.slot = boot_mr->mrmod_slot;

    // get the size of the boot driver frame
    struct capability boot_cap;
    err = cap_direct_identify(boot_frame, &boot_cap);
    DEBUG_ERR_ON_FAIL(err, "couldn't identify frame for boot driver\n");
    size_t boot_bytes = boot_cap.u.frame.bytes;

    // map the boot driver frame
    void *boot_buf;
    err = paging_map_frame_attr(get_current_paging_state(), &boot_buf, boot_bytes, boot_frame, VREGION_FLAGS_READ_WRITE);
    DEBUG_ERR_ON_FAIL(err, "couldn't map boot driver frame\n");

    // create and map a new frame to hold the loaded ELF binary
    struct capref boot_elf_frame;
    err = frame_alloc(&boot_elf_frame, boot_bytes, NULL);
    DEBUG_ERR_ON_FAIL(err, "couldn't allocate boot driver ELF frame\n");
    void *boot_elf_buf;
    err = paging_map_frame_attr(get_current_paging_state(), &boot_elf_buf, boot_bytes, boot_elf_frame, VREGION_FLAGS_READ_WRITE);
    DEBUG_ERR_ON_FAIL(err, "couldn't map boot driver ELF frame\n");
    struct capability boot_elf_cap;
    err = cap_direct_identify(boot_elf_frame, &boot_elf_cap);
    DEBUG_ERR_ON_FAIL(err, "couldn't identify boot driver ELF frame\n");

    // set up a mem_info struct to point to the loaded ELF binary frame
    struct mem_info boot_mi;
    boot_mi.buf = boot_elf_buf;
    boot_mi.phys_base = boot_elf_cap.u.ram.base;
    boot_mi.size = boot_bytes;

    // get the physical entry point of the ELF binary
    struct Elf64_Sym *boot_entry;
    boot_entry = elf64_find_symbol_by_name((genvaddr_t)boot_buf, boot_bytes, "boot_entry_psci", 0, STT_FUNC, NULL);
    if (boot_entry == NULL) {
        debug_printf("couldn't find boot_entry_psci\n");
        return -1;
    }
    genvaddr_t boot_phys_entry_point = boot_entry->st_value;

    // load the ELF binary into the ELF binary frame we set up earlier
    genvaddr_t boot_reloc_entry_point;
    err = load_elf_binary((genvaddr_t)boot_buf, &boot_mi, boot_phys_entry_point, &boot_reloc_entry_point);
    DEBUG_ERR_ON_FAIL(err, "couldn't load boot driver binary\n");

    // ============================================================================================
    // Relocate the boot and CPU driver. The boot driver runs with a 1:1
    // VA->PA mapping. The CPU driver is expected to be loaded at the
    // high virtual address space, at offset ARMV8_KERNEL_OFFSET.
    // ============================================================================================

    err = relocate_elf((genvaddr_t)boot_buf, &boot_mi, 0);
    DEBUG_ERR_ON_FAIL(err, "couldn't relocate boot driver\n");
    err = relocate_elf((genvaddr_t)cpu_buf, &cpu_mi, ARMv8_KERNEL_OFFSET);
    DEBUG_ERR_ON_FAIL(err, "couldn't relocate CPU driver\n");

    // ============================================================================================
    // Allocate a page for the core data struct
    // ============================================================================================

    struct capref cd_frame;
    err = frame_alloc(&cd_frame, BASE_PAGE_SIZE, NULL);
    DEBUG_ERR_ON_FAIL(err, "couldn't allocate frame for core data struct\n");
    void *cd_buf;
    err = paging_map_frame_attr(get_current_paging_state(), &cd_buf, BASE_PAGE_SIZE, cd_frame, VREGION_FLAGS_READ_WRITE);
    struct capability cd_cap;
    err = cap_direct_identify(cd_frame, &cd_cap);
    DEBUG_ERR_ON_FAIL(err, "couldn't identify core data capability\n");
    struct armv8_core_data *cd = cd_buf;

    // ============================================================================================
    // Allocate stack memory for the new cpu driver (at least 16 pages)
    // ============================================================================================

    struct capref stack_ram;
    err = ram_alloc(&stack_ram, 16 * BASE_PAGE_SIZE);
    DEBUG_ERR_ON_FAIL(err, "couldn't allocate stack for CPU driver\n");
    struct capability ram_cap;
    err = cap_direct_identify(stack_ram, &ram_cap);
    
    // ============================================================================================
    // Fill in the core data struct, for a description, see the definition
    // in include/target/aarch64/barrelfish_kpi/arm_core_data.h
    // ============================================================================================

    // find the init binary
    struct mem_region *init_mr;
    init_mr = multiboot_find_module(bi, init);
    if (init_mr == NULL) {
        debug_printf("couldn't find init module\n");
        return -1;
    }
    struct armv8_coredata_memreg monitor_binary;
    monitor_binary.base = init_mr->mr_base;
    monitor_binary.length = init_mr->mrmod_size;
    debug_printf("init binary base: %p size %lu\n", monitor_binary.base, monitor_binary.length);

    // allocate some space to load the init process
    struct armv8_coredata_memreg init_mem;
    struct capref init_ram;
    err = ram_alloc(&init_ram, ARMV8_CORE_DATA_PAGES * BASE_PAGE_SIZE + ROUND_UP(monitor_binary.length, BASE_PAGE_SIZE));
    DEBUG_ERR_ON_FAIL(err, "couldn't allocate space to load init\n");
    struct capability init_cap;
    err = cap_direct_identify(init_ram, &init_cap);
    init_mem.base = init_cap.u.ram.base;
    init_mem.length = init_cap.u.ram.bytes;
    debug_printf("init mem base: %p size %lu\n", init_mem.base, init_mem.length);

    // allocate space for the URPC frame
    struct armv8_coredata_memreg urpc_mem;
    struct capref urpc_frame;
    err = frame_alloc(&urpc_frame, BASE_PAGE_SIZE, NULL);
    DEBUG_ERR_ON_FAIL(err, "couldn't allocate space to urpc frame\n");
    struct capability urpc_cap;
    void *urpc_buf;
    err = paging_map_frame_attr(get_current_paging_state(), &urpc_buf, BASE_PAGE_SIZE, urpc_frame, VREGION_FLAGS_READ_WRITE);
    DEBUG_ERR_ON_FAIL(err, "couldn't map urpc frame\n");
    err = cap_direct_identify(urpc_frame, &urpc_cap);
    urpc_mem.base = urpc_cap.u.frame.base;
    urpc_mem.length = urpc_cap.u.frame.bytes;
    debug_printf("urpc base: %p size %lu\n", urpc_mem.base, urpc_mem.length);

    // set the fields of the core data struct
    cd->boot_magic = ARMV8_BOOTMAGIC_PSCI;
    cd->cpu_driver_stack = ram_cap.u.ram.base + ram_cap.u.ram.bytes;
    cd->cpu_driver_stack_limit = ram_cap.u.ram.base;
    cd->cpu_driver_entry = cpu_reloc_entry_point + ARMv8_KERNEL_OFFSET;
    memset(cd->cpu_driver_cmdline, 0, 128);
    cd->memory = init_mem;
    cd->urpc_frame = urpc_mem;
    cd->monitor_binary = monitor_binary;
    cd->kcb = kcb_ram_capability.u.ram.base;
    cd->src_core_id = disp_get_core_id();
    cd->dst_core_id = mpid;
    cd->src_arch_id = disp_get_core_id();
    cd->dst_arch_id = mpid;

    // ============================================================================================
    // Find the CPU driver entry point. Look for the symbol "arch_init". Put
    // the address in the core data struct.
    // ============================================================================================

    // already done

    // ============================================================================================
    // Find the boot driver entry point. Look for the symbol "boot_entry_psci"
    // Flush the cache.
    // ============================================================================================

    // already found boot driver entry point
    
    // vscode doesn't like this cast, but the compiler requires it
    cpu_dcache_wbinv_range((vm_offset_t)cd_buf, BASE_PAGE_SIZE);


    // ===========================================================================================
    /* Pseudocode for assigning mem to new core:
     * 1. ram alloc 256 mb
     * 2. init new bootinfo struct for other core
     * 3. pass into urpc frame, cache flush
     * 4. read it in app_main
     * 5. forge capabilities
     * 6. add to core's memory mgr
     */
    // ============================================================================================
    
    // Ram alloc 256 mb
    struct capref new_core_mem;
    err = ram_alloc(&new_core_mem, NEW_CORE_MEM_SZ);
    DEBUG_ERR_ON_FAIL(err, "couldn't allocate ram for new core\n");

    struct capability new_core_mem_cap;
    err = cap_direct_identify(new_core_mem, &new_core_mem_cap);
    DEBUG_ERR_ON_FAIL(err, "couldn't get cap for ram that was meant for new core\n");

    // Get capability to module strings
    struct capability mod_strings_cap;
    err = cap_direct_identify(cap_mmstrings, &mod_strings_cap);
    DEBUG_ERR_ON_FAIL(err, "couldn't get cap for module strings\n");

    // Init new bootinfo struct for other core
    struct mem_region new_mem_region = {
        .mr_base = new_core_mem_cap.u.ram.base,
        .mr_type = RegionType_Empty,
        .mr_bytes = new_core_mem_cap.u.ram.bytes,
        .mr_consumed = 0,
        .mrmod_size = 0,
        .mrmod_data = 0,
        .mrmod_slot = 0,
    };
    
    // Count all elf mem module regions
    int module_counter = 0;
    for (int i = 0; i < (int) bi->regions_length; i++) {
        if (bi->regions[i].mr_type == RegionType_Module) {
            module_counter++;
        }
    }

    int bootinfo_size = sizeof(struct bootinfo) + ((module_counter + 1) * sizeof(struct mem_region));
    struct bootinfo* new_core_bootinfo = (struct bootinfo*) malloc(bootinfo_size);
    new_core_bootinfo->regions[0] = new_mem_region;
    new_core_bootinfo->regions_length = module_counter + 1;
    new_core_bootinfo->mem_spawn_core = bi->mem_spawn_core;

    for (int i = 0; i < (int) bi->regions_length; i++) {
        if (bi->regions[i].mr_type == RegionType_Module) {
            new_core_bootinfo->regions[i].mr_base = bi->regions[i].mr_base;
            new_core_bootinfo->regions[i].mr_type = RegionType_Module,
            new_core_bootinfo->regions[i].mr_bytes = bi->regions[i].mr_bytes;
            new_core_bootinfo->regions[i].mr_consumed = bi->regions[i].mr_consumed; // <-- Is this right?
            new_core_bootinfo->regions[i].mrmod_size = bi->regions[i].mrmod_size;
            new_core_bootinfo->regions[i].mrmod_data = bi->regions[i].mrmod_data;
            new_core_bootinfo->regions[i].mrmod_slot = bi->regions[i].mrmod_slot;
        }
    }

    // Pass into urpc frame, cache flush
    memcpy(urpc_buf, new_core_bootinfo, bootinfo_size);
    memcpy(urpc_buf + bootinfo_size, &(mod_strings_cap.u.frame.base), sizeof(genpaddr_t));
    memcpy(urpc_buf + bootinfo_size + sizeof(genpaddr_t), &(mod_strings_cap.u.frame.bytes), sizeof(gensize_t));
    cpu_dcache_wbinv_range((vm_offset_t)urpc_buf, BASE_PAGE_SIZE);
    free(new_core_bootinfo);

    // ============================================================================================
    // Call the invoke_monitor_spawn_core with the entry point
    // of the boot driver and pass the (physical, of course) address of the
    // boot struct as argument.
    // ============================================================================================

    err = invoke_monitor_spawn_core(cd->dst_arch_id, CPU_ARM8, boot_reloc_entry_point, cd_cap.u.frame.base, 0);
    DEBUG_ERR_ON_FAIL(err, "couldn't invoke monitor to spawn core\n");

    // set the return core parameter
    if (core != NULL) {
        *core = (coreid_t)mpid;
    }

    return SYS_ERR_OK;
}

/**
 * @brief shutdown the execution of the given core and free its resources
 *
 * @param[in] core  Coreid of the core to be shut down
 *
 * @return SYS_ERR_OK on success, errval on failure
 *
 * Note: calling this function with the coreid of the BSP core (0) will cause an error.
 */
errval_t coreboot_shutdown_core(coreid_t core)
{
    (void)core;
    // Hints:
    //  - think of what happens when you call this function with the coreid of another core,
    //    or with the coreid of the core you are running on.
    //  - use the BSP core as the manager.
    USER_PANIC("Not implemented");
    return LIB_ERR_NOT_IMPLEMENTED;
}

/**
 * @brief shuts down the core and reboots it using the provided arguments
 *
 * @param[in] core         Coreid of the core to be rebooted
 * @param[in] boot_driver  Path of the boot driver binary
 * @param[in] cpu_driver   Path of the CPU driver binary
 * @param[in] init         Path to the init binary
 *
 * @return SYS_ERR_OK on success, errval on failure
 *
 * Note: calling this function with the coreid of the BSP core (0) will cause an error.
 */
errval_t coreboot_reboot_core(coreid_t core, const char *boot_driver, const char *cpu_driver,
                              const char *init)
{
(void)core;
(void)boot_driver;
(void)cpu_driver;
(void)init;
    // Hints:
    //  - think of what happens when you call this function with the coreid of another core,
    //    or with the coreid of the core you are running on.
    //  - use the BSP core as the manager.
    //  - after you've shutdown the core, you can reuse `coreboot_boot_core` to boot it again.

    USER_PANIC("Not implemented");
    return LIB_ERR_NOT_IMPLEMENTED;
}

/**
 * @brief suspends (halts) the execution of the given core
 *
 * @param[in] core  Coreid of the core to be suspended
 *
 * @return SYS_ERR_OK on success, errval on failure
 *
 * Note: calling this function with the coreid of the BSP core (0) will cause an error.
 */
errval_t coreboot_suspend_core(coreid_t core)
{
    (void)core;
    // Hints:
    //  - think of what happens when you call this function with the coreid of another core,
    //    or with the coreid of the core you are running on.
    //  - use the BSP core as the manager.

    USER_PANIC("Not implemented");
    return LIB_ERR_NOT_IMPLEMENTED;
}

/**
 * @brief resumes the execution of the given core
 *
 * @param[in] core  Coreid of the core to be resumed
 *
 * @return SYS_ERR_OK on success, errval on failure
 */
errval_t coreboot_resume_core(coreid_t core)
{
    (void)core;
    // Hints:
    //  - check if the coreid is valid and the core is in fact suspended
    //  - wake up the core to resume its execution

    USER_PANIC("Not implemented");
    return LIB_ERR_NOT_IMPLEMENTED;
}



/**
 * @brief obtains the number of cores present in the system.
 *
 * @param[out] num_cores  returns the number of cores in the system
 *
 * @return SYS_ERR_OK on success, errval on failure
 *
 * Note: This function should return the number of cores that the system supports
 */
errval_t coreboot_get_num_cores(coreid_t *num_cores)
{
    // TODO: change me with multicore support!
    *num_cores = 1;
    return SYS_ERR_OK;
}


/**
 * @brief obtains the status of a core in the system.
 *
 * @param[in]  core    the ID of the core to obtain the status from
 * @param[out] status  status struct filled in
 *
 * @return SYS_ERR_OK on success, errval on failure
 */
errval_t coreboot_get_core_status(coreid_t core, struct corestatus *status)
{
    (void)core;
    (void)status;
    // TODO: obtain the status of the core.
    USER_PANIC("Not implemented");
    return LIB_ERR_NOT_IMPLEMENTED;
}