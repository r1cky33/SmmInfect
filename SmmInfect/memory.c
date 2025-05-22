#include "memory.h"
#include "logging.h"

#include <Uefi/UefiBaseType.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseLib.h>

static UINT64 map_begin = 0x1000;
static UINT64 map_end = 0;
static BOOLEAN setup_done = FALSE;
static EFI_SMM_SYSTEM_TABLE2 *GSmst2;

static const UINT64 PMASK =  (~0xfull << 8) & 0xfffffffffull;
typedef UINT32 size_t;

#define PAGE_PRESENT 1
#define PAGE_PS 0x80
#define PAGE_RW      0x2

EFI_PHYSICAL_ADDRESS cachingPointer = 0;
UINT32 cachingSize = 0;

void *ZMemSet(void *ptr, int value, UINT64 num) {
    unsigned char *p = (unsigned char *)ptr;
    while (num--) {
        *p++ = (unsigned char)value;
    }
    return ptr;
}

BOOLEAN pMemCpy(EFI_PHYSICAL_ADDRESS dest, EFI_PHYSICAL_ADDRESS src, size_t size)
{
	// Check if the address ranges are in allowed range
	if ((IsAddressValid(src) == FALSE || IsAddressValid((src + size - 1)) == FALSE))
	{
		LOG_ERROR("[MEM] Aborted duo to disallowed memory range \r\n");

		// Return to invalid memory range
		return FALSE;
	}

    if (EnsurePhysicalPageIsMapped(src) == FALSE) {
        LOG_ERROR("[MEM] Aborted due to address not beeing mapped or not beeing able to map \r\n");
        return FALSE;
    }

	// Typecast src and dest addresses to bytes
	UINT8* csrc = (UINT8*)src;
	UINT8* cdest = (UINT8*)dest;

	// Copy contents of src[] to dest[] 
	for (int i = 0; i < size; i++)
		cdest[i] = csrc[i];

	// Return true
	return TRUE;
}

// Clamp the virtual read to bounderies of page size.
UINT8 *ReadVirtual(UINT64 address, UINT64 cr3, UINT8 *buffer, UINT64 length) {
    if (buffer == NULL)
        return NULL;

    UINT64 remaining_size = length;
    UINT64 offset = 0;

    while (remaining_size > 0) {
        UINT64 physical_address = CachedVTOP(cr3, address + offset);
        UINT64 page_offset = physical_address & 0xFFF;
        UINT64 current_size = 0x1000 - page_offset;

        if (current_size > remaining_size) {
            current_size = remaining_size;
        }

        if (!physical_address) {
            ZMemSet(buffer + offset, 0, current_size);
        } else {
            if (!pMemCpy((EFI_PHYSICAL_ADDRESS)(buffer + offset), physical_address, current_size)) {
                break;
            }
        }

        offset += current_size;
        remaining_size -= current_size;
    }

    return buffer;
}

UINT8 ReadVirtual8(UINT64 address, UINT64 cr3) {
    UINT8 value = 0;
    ReadVirtual(address, cr3, &value, sizeof(value));
    return value;
}

UINT16 ReadVirtual16(UINT64 address, UINT64 cr3) {
    UINT16 value = 0;
    ReadVirtual(address, cr3, (UINT8 *)&value, sizeof(value));
    return value;
}

UINT32 ReadVirtual32(UINT64 address, UINT64 cr3) {
    UINT32 value = 0;
    ReadVirtual(address, cr3, (UINT8 *)&value, sizeof(value));
    return value;
}
UINT64 ReadVirtual64(UINT64 address, UINT64 cr3) {
    UINT64 value = 0;
    ReadVirtual(address, cr3, (UINT8 *)&value, sizeof(value));
    return value;
}

EFI_STATUS SetupMemoryMap() {
    UINT32 descriptor_version;
    UINTN memory_map_size = 0;
    EFI_MEMORY_DESCRIPTOR *memory_map = NULL;
    UINTN map_key;
    UINTN descriptor_size;
    EFI_STATUS status;

    status = gBS->GetMemoryMap(&memory_map_size, memory_map, &map_key,
                               &descriptor_size, &descriptor_version);
    if (status == EFI_INVALID_PARAMETER || status == EFI_SUCCESS) {
        return EFI_ACCESS_DENIED;
    }

    status = gBS->AllocatePool(EfiBootServicesData, memory_map_size,
                               (VOID **)&memory_map);

    if (EFI_ERROR(status)) {
        return status;
    }

    status = gBS->GetMemoryMap(&memory_map_size, memory_map, &map_key,
                               &descriptor_size, &descriptor_version);
    if (EFI_ERROR(status)) {
        gBS->FreePool(memory_map);
    }

    UINT64 max_end = 0;
    EFI_MEMORY_DESCRIPTOR *desc = memory_map;

    for (UINT64 i = 0; i < memory_map_size / descriptor_size; ++i) {
        UINT64 region_end =
            desc->PhysicalStart + (desc->NumberOfPages * 0x1000);
        if (region_end > max_end) {
            max_end = region_end;
        }

        desc = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)desc + descriptor_size);
    }

    gBS->FreePool(memory_map);
    map_end = max_end;
    return EFI_SUCCESS;
}

BOOLEAN IsAddressValid(UINT64 address) {
    // return !(address < map_begin || address > map_end);
    return address != 0;
}

EFI_PHYSICAL_ADDRESS VTOP(EFI_VIRTUAL_ADDRESS address, EFI_PHYSICAL_ADDRESS directoryBase) {
    // Check if address was properly set
    if (address == 0) {
        LOG_DBG("[MEM] address is 0 \r\n");

        // No valid virtual address, abort
        return 0;
    }

    // check if directory base was properly set
    if (directoryBase == 0) {
        LOG_DBG("[MEM] directoryBase is 0 \r\n");

        // No valid directory base, abort
        return 0;
    }

    // Null the last bits
    directoryBase &= ~0xf;

    // Calculate the relevant offsets based on the address
    UINT64 pageOffset = address & ~(~0ul << PAGE_OFFSET_SIZE);
    EFI_PHYSICAL_ADDRESS pte = ((address >> 12) & (0x1ffll));
    EFI_PHYSICAL_ADDRESS pt = ((address >> 21) & (0x1ffll));
    EFI_PHYSICAL_ADDRESS pd = ((address >> 30) & (0x1ffll));
    EFI_PHYSICAL_ADDRESS pdp = ((address >> 39) & (0x1ffll));

    LOG_DBG("[MEM] Dirbase:  %p  VA %p  PO: %p  PTE: %p  PT: %p  PD: %p  PDP: "
            "%p\r\n",
            directoryBase, address, pageOffset, pte, pt, pd, pdp);

    // Read the PDPE from the directory base
    EFI_PHYSICAL_ADDRESS pdpe = 0;
    if (pMemCpy((EFI_PHYSICAL_ADDRESS)&pdpe, directoryBase + 8 * pdp,
                sizeof(EFI_PHYSICAL_ADDRESS)) == FALSE) {
        // Failed reading, abort
        return 0;
    }

    LOG_DBG("[MEM] Dump PDPE at %p results %p \r\n", directoryBase + 8 * pdp, 16,
            pdpe & PMASK);

    // Check if we got a valid entry
    if (~pdpe & 1)
        return 0;

    // Read the PDE entry from the directory base
    EFI_PHYSICAL_ADDRESS pde = 0;
    if (pMemCpy((EFI_PHYSICAL_ADDRESS)&pde, (pdpe & PMASK) + 8 * pd,
                sizeof(EFI_PHYSICAL_ADDRESS)) == FALSE) {
        // Failed reading, abort
        return 0;
    }

    LOG_DBG("[MEM] Dump PDE at %p results %p \r\n", (pdpe & PMASK) + 8 * pd,
            pde & PMASK);

    // Check if we got a valid entry
    if (~pde & 1)
        return 0;

    /* 1GB large page, use pde's 12-34 bits */
    if (pde & 0x80)
        return (pde & (~0ull << 42 >> 12)) + (address & ~(~0ull << 30));

    // Read the PTE address from the directory base
    EFI_PHYSICAL_ADDRESS pteAddr = 0;
    if (pMemCpy((EFI_PHYSICAL_ADDRESS)&pteAddr,
                (EFI_PHYSICAL_ADDRESS)(pde & PMASK) + 8 * pt,
                sizeof(EFI_PHYSICAL_ADDRESS)) == FALSE) {
        // Failed reading, abort
        return 0;
    }

    LOG_DBG("[MEM] Dump pteAddr at %p results %p \r\n", (pde & PMASK) + 8 * pt,
            pteAddr & PMASK);

    // Check if we got a valid entry
    if (~pteAddr & 1)
        return 0;

    /* 2MB large page */
    if (pteAddr & 0x80)
        return (pteAddr & PMASK) + (address & ~(~0ull << 21));

    // Read the address from the directory base
    if (pMemCpy((EFI_PHYSICAL_ADDRESS)&address, (pteAddr & PMASK) + 8 * pte,
                sizeof(EFI_VIRTUAL_ADDRESS)) == FALSE) {
        // Failed reading, abort
        return 0;
    }

    // 16.04 fix large address in corrupted page tables, 48bit physical address
    // is enough
    address = address & PMASK;

    LOG_DBG("[MEM] Dump address at %p \r\n", (pteAddr & PMASK) + 8 * pte);

    // Check if we got a valid address
    if (!address)
        return 0;

    // Return the address and add the page relative offset again
    return address + pageOffset;
}

EFI_PHYSICAL_ADDRESS CachedVTOP(EFI_VIRTUAL_ADDRESS directoryBase,
                                EFI_VIRTUAL_ADDRESS address) {
    // Check if caching is already setup
    if (cachingPointer == 0) {
        // Set it up now
        EFI_PHYSICAL_ADDRESS physAddress;
        GSmst2->SmmAllocatePages(AllocateAnyPages, EfiRuntimeServicesData, 1,
                                 &physAddress);

        // Check if we got a valid address
        if (physAddress != 0) {
            // Assign it to the global pointer
            cachingPointer = physAddress;
            LOG_INFO("[MEM] Allocated caching memory at 0x%x \r\n",
                     cachingPointer);
        } else {
            LOG_ERROR("[MEM] Failed allocating caching memory \r\n");
        }
    }

    // Handle everything with pagebases so theres only 1 translation per page
    EFI_VIRTUAL_ADDRESS virtualPageBase = address & 0xfffffffffffff000;
    EFI_VIRTUAL_ADDRESS virtualRelative = address & 0xfff;

    // Only support a maximum of 255 pages, check if we already reached the
    // limit
    if (cachingSize >= 255) {
        // Reset the caching by setting the size to 0
        cachingSize = 0;
    }

    // Loop through the page to check if an entry exists
    for (UINT32 i = 0; i < cachingSize; i++) {
        // Get the current caching entry at this position
        Cache *tempCache = (Cache *)(cachingPointer + (sizeof(Cache) * i));

        // Check if the virtual page address matches the one requested and the
        // entry has a valid physical address
        if (tempCache->vAddress == virtualPageBase &&
            tempCache->pAddress != 0) {
            
            // Found the entry, return physical address
            return tempCache->pAddress + virtualRelative;
        }
    }

    // If we get here, it means the entry was not found in the caching, thus set
    // it up as next entry
    EFI_PHYSICAL_ADDRESS tempPhysical = VTOP(address, directoryBase);

    // Check if we successfully translated the virtual address
    if (tempPhysical != 0) {
        // Successfully translated, increase the caching size by 1
        cachingSize = cachingSize + 1;

        // Create a new caching structure at the last position
        Cache *tempCache =
            (Cache *)(cachingPointer + (cachingSize * sizeof(Cache)));

        // Set the physical and virtual address in the new entry
        tempCache->pAddress = tempPhysical & 0xfffffffffffff000;
        tempCache->vAddress = virtualPageBase & 0xfffffffffffff000;

        // Return this physical address
        return tempPhysical;
    } else {
        // Failed to get physical address, return 0 as this is most likely an
        // invalid virtual address
        return 0;
    }
}

EFI_STATUS SetupMemory(EFI_SMM_SYSTEM_TABLE2 *smst) {
    if (setup_done == TRUE) {
        return EFI_SUCCESS;
    }

    if (smst == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    GSmst2 = smst;

    if (SetupMemoryMap() == EFI_SUCCESS) {
        setup_done = TRUE;
        return EFI_SUCCESS;
    }

    return EFI_NOT_FOUND;
}

//
// Page walk all mapped pages for debugging purposes
//
VOID DumpAccessiblePages(EFI_PHYSICAL_ADDRESS cr3) {
    EFI_PHYSICAL_ADDRESS pml4_phys = cr3 &= ~0xF;
    EFI_PHYSICAL_ADDRESS pml4e, pdpe, pde, pte;
    EFI_PHYSICAL_ADDRESS base;

    LOG_VERB("[MEM] Walking Page-Tables: %x \r\n", (UINT32)pml4_phys);

    for (UINT64 pml4e_idx = 0; pml4e_idx < 512; pml4e_idx++) {
        LOG_DBG("[MEM] PML4E idx: %x \r\n", pml4e_idx);

        if (!pMemCpy((EFI_PHYSICAL_ADDRESS)&pml4e, pml4_phys + 8 * pml4e_idx, sizeof(EFI_PHYSICAL_ADDRESS))) {
            continue;
        }

        if (!(pml4e & PAGE_PRESENT)) {
            continue;
        }

        for (UINT64 pdpe_idx = 0; pdpe_idx < 512; pdpe_idx++) {
            LOG_DBG("[MEM] PDPE idx: %x \r\n", pdpe_idx);

            if (!pMemCpy((EFI_PHYSICAL_ADDRESS)&pdpe, (pml4e & PMASK) + 8 * pdpe_idx, sizeof(EFI_PHYSICAL_ADDRESS)))
                continue;
            if (!(pdpe & PAGE_PRESENT)) {
                continue;
            }

            if (pdpe & 0x80) {
                base = (pdpe & (~0ull << 42 >> 12));
                LOG_DBG("[MEM] 1G Page: 0x%x \r\n", (UINT32)base);
                continue;
            }

            for (UINT64 pde_idx = 0; pde_idx < 512; pde_idx++) {
                if (!pMemCpy((EFI_PHYSICAL_ADDRESS)&pde, (pdpe & PMASK) + 8 * pde_idx, sizeof(EFI_PHYSICAL_ADDRESS)))
                    continue;
                if (!(pde & PAGE_PRESENT)) {
                    continue;
                }

                if (pde & 0x80) {
                    base = (pde & PMASK);
                    LOG_DBG("[MEM] 2M Page: 0x%x \r\n", (UINT32)base);
                    continue;
                }

                for (UINT64 pte_idx = 0; pte_idx < 512; pte_idx++) {
                        if (!pMemCpy((EFI_PHYSICAL_ADDRESS)&pte, (pde & PMASK) + 8 * pte_idx, sizeof(EFI_PHYSICAL_ADDRESS)))
                        continue;
                    if (!(pte & PAGE_PRESENT)) {
                        continue;
                    }

                    base = pte & PMASK;
                    LOG_DBG("[MEM] 4K Page: 0x%x \r\n", (UINT32)base);
                }
            }
        }
    }
}

//
// We need to ensure that a the accessed virtual address is (identity) is mapped
// to our address space. If we don't do this, the CPU will #PF ofc (Even tho i was able to read valid values somehow...).
//
BOOLEAN EnsurePhysicalPageIsMapped(EFI_PHYSICAL_ADDRESS physAddr) {

    // Align the physical address to 4KB boundary
    physAddr &= PMASK;
    EFI_PHYSICAL_ADDRESS cr3  = AsmReadCr3();

    // Calculate the indices
    UINT64 pml4_index = (physAddr >> 39) & 0x1FF;
    UINT64 pdpt_index = (physAddr >> 30) & 0x1FF;
    UINT64 pd_index   = (physAddr >> 21) & 0x1FF;
    UINT64 pt_index   = (physAddr >> 12) & 0x1FF;
    
    LOG_DBG("[MEM] Ensuring accessed address is mapped:  A %x  PML4i: %d  PDPTi: %d  PDi: %d  PTi: %d \r\n",
            physAddr, pml4_index, pdpt_index, pd_index, pt_index);
    
    // Cast physical address to pointer directly (requires identity mapping)
    UINT64* pml4_ptr = (UINT64*)cr3;

    if (!(pml4_ptr[pml4_index] & PAGE_PRESENT)) {
        EFI_PHYSICAL_ADDRESS pdpt;
        if (EFI_ERROR(GSmst2->SmmAllocatePages(AllocateAnyPages, EfiRuntimeServicesData, 1, &pdpt)))
            return FALSE;

        ZMemSet((void*)pdpt, 0, SIZE_4KB);
        pml4_ptr[pml4_index] = pdpt | PAGE_PRESENT | PAGE_RW;
    }

    UINT64* pdpt_ptr = (UINT64*)(pml4_ptr[pml4_index] & PMASK);

    // Allocate PD if not present
    if (!(pdpt_ptr[pdpt_index] & PAGE_PRESENT)) {
        EFI_PHYSICAL_ADDRESS pd;
        if (EFI_ERROR(GSmst2->SmmAllocatePages(AllocateAnyPages, EfiRuntimeServicesData, 1, &pd)))
            return FALSE;

        ZMemSet((void*)pd, 0, SIZE_4KB);
        pdpt_ptr[pdpt_index] = pd | PAGE_PRESENT | PAGE_RW;
    } else {

        // If the page is present and PS (Page Size) bit is set, we have a 1GB page mapped 
        if (pdpt_ptr[pdpt_index] & PAGE_PS) {

            // Here we could also ensure, that the requested physical address is within the 1GB page
            LOG_DBG("[MEM] Mapped on 1GB page \r\n");
            return TRUE; 
        }
    }

    UINT64* pd_ptr = (UINT64*)(pdpt_ptr[pdpt_index] & PMASK);

    // Allocate PT if not present
    if (!(pd_ptr[pd_index] & PAGE_PRESENT)) {
        EFI_PHYSICAL_ADDRESS pt;
        if (EFI_ERROR(GSmst2->SmmAllocatePages(AllocateAnyPages, EfiRuntimeServicesData, 1, &pt)))
            return FALSE;

        ZMemSet((void*)pt, 0, SIZE_4KB);
        pd_ptr[pd_index] = pt | PAGE_PRESENT | PAGE_RW;
    } else {

        // If the page is present and PS (Page Size) bit is set, we have a 2MB page mapped
        if (pd_ptr[pd_index] & PAGE_PS) {

            // Again could ensure if the requested physical address is within the 2MB page
            LOG_DBG("[MEM] Mapped on 2MB page \r\n");
            return TRUE;
        }
    }

    UINT64* pt_ptr = (UINT64*)(pd_ptr[pd_index] & PMASK);

    // Map the physical page itself
    if (!(pt_ptr[pt_index] & PAGE_PRESENT)) {
        LOG_DBG("[MEM] Adding mapping for address \r\n");
        pt_ptr[pt_index] = physAddr | PAGE_PRESENT | PAGE_RW;
    } else {
        LOG_DBG("[MEM] Accessed page is already mapped as 4KB \r\n");
    }

    // Optional: flush TLB
    // __asm__ __volatile__("invlpg (%0)" ::"r"(physAddr) : "memory");

    return TRUE;
}
