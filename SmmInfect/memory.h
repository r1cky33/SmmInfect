#pragma once

#include "Uefi/UefiBaseType.h"
#include <PiSmm.h>
#include <Protocol/SmmBase2.h>
#include <Protocol/SmmCpu.h>
#include <Uefi.h>

// #define Linux UNSUPPORTED

void *ZMemSet(void *ptr, int value, UINT64 num);
EFI_STATUS SetupMemoryMap();
BOOLEAN IsAddressValid(UINT64 address);
EFI_STATUS SetupMemory(EFI_SMM_SYSTEM_TABLE2 *smst);
EFI_PHYSICAL_ADDRESS CachedVTOP(EFI_VIRTUAL_ADDRESS directoryBase, EFI_VIRTUAL_ADDRESS address);

// Print mapped pages for debugging
VOID DumpAccessiblePages(EFI_PHYSICAL_ADDRESS cr3);

// Ensure all accessed addresses are identity-mapped
BOOLEAN EnsurePhysicalPageIsMapped(EFI_PHYSICAL_ADDRESS physAddr);

//
// Virtual memory
//
UINT8 *ReadVirtual(UINT64 address, UINT64 cr3, UINT8 *buffer, UINT64 length);
UINT8 ReadVirtual8(UINT64 address, UINT64 cr3);
UINT16 ReadVirtual16(UINT64 address, UINT64 cr3);
UINT32 ReadVirtual32(UINT64 address, UINT64 cr3);
UINT64 ReadVirtual64(UINT64 address, UINT64 cr3);

#define PAGE_OFFSET_SIZE 12

// Structures

typedef struct _Cache
{
	UINT64 vAddress;
	UINT64 pAddress;
} Cache, *PCache;


