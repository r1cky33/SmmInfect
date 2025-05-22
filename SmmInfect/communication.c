#include "communication.h"
#include "logging.h"
#include "memory.h"
#include "windows.h"

#include <Library/BaseMemoryLib.h>
#include <Library/UefiLib.h>
#include <PiSmm.h>
#include <Protocol/SmmCpu.h>
#include <Uefi.h>
#include <Uefi/UefiBaseType.h>
#include <stddef.h>

UINT64 SmiCountIndex = 0;
UINT64 cprocess = 0;
UINT64 base = 0;
UINT64 section = 0;
UINT64 processCr3 = 0;

UINT64 GetCommunicationProcess() {
    UINT64 proc = GetWindowsEProcess("ricky.exe");
    return proc;
}

EFI_STATUS PerformCommunication() {
    SmiCountIndex++;

    UINT64 kernel = GetWindowsKernelBase();
    UINT64 cr3 = GetWindowsKernelCr3();

    if (!kernel || !cr3) {
        return EFI_NOT_READY;
    }

    UINT64 tempProc = GetCommunicationProcess();
    if (cprocess != tempProc) {
        cprocess = tempProc;
        if (!cprocess) {
            return EFI_NOT_FOUND;
        }

        LOG_INFO("[INFO] cprocess: 0x%x\r\n", cprocess);

        base = GetWindowsBaseAddressModuleX64(cprocess, L"ricky.exe");
        if (!base) {
            return EFI_NOT_FOUND;
        }

        LOG_INFO("[INFO] cprocess base: 0x%x\r\n", base);
        section =
            GetWindowsSectionBaseAddressX64(cprocess, base, (UINT8 *)".RICKY");
        if (!section) {
            return EFI_NOT_FOUND;
        }

        LOG_INFO("[INFO] cprocess section: 0x%x\r\n", section);

        processCr3 = GetWindowsProcessCr3(cprocess);
        if (!processCr3) {
            return EFI_NOT_FOUND;
        }

        LOG_INFO("[INFO] cprocess cr3: 0x%x\r\n", processCr3);
    }

    SmmCommunicationProtocol protocol = {0};
    ReadVirtual(section + 0b0, processCr3, (UINT8 *)&protocol,
                sizeof(protocol));

    if (protocol.magic != SMM_PROTOCOL_MAGIC) {
        return EFI_INVALID_PARAMETER;
    }

    *(UINT64 *)CachedVTOP(
        processCr3, section + offsetof(SmmCommunicationProtocol, smi_count)) =
        SmiCountIndex;

    switch (protocol.command) {
    case CMD_READ_VIRTUAL_MEMORY: {

        LOG_INFO("[INFO] CMD_READ_VIRTUAL_MEMORY \r\n");
        UINT64 tprocess =
            GetWindowsEProcess((CHAR8 *)protocol.read.process_name);
        if (!tprocess) {
            return EFI_NOT_FOUND;
        }

        LOG_VERB("[INFO] tprocess base: 0x%x\r\n", tprocess);

        if (protocol.read.size > sizeof(protocol.buffer)) {
            LOG_ERROR("[ERR] buffer too large: 0x%x \r\n", protocol.read.size);
            return EFI_BAD_BUFFER_SIZE;
        }

        ReadVirtual(protocol.read.address, GetWindowsProcessCr3(tprocess),
                    protocol.buffer, protocol.read.size);

        LOG_VERB("[INFO] read: 0x%x  copying...\r\n", protocol.read.address);

        UINT64 buffer_phys = CachedVTOP(
            processCr3, section + offsetof(SmmCommunicationProtocol, buffer));
        CopyMem((VOID *)buffer_phys, protocol.buffer, protocol.read.size);
        break;
    }

    case CMD_GET_MODULE_BASE: {

        LOG_INFO("[INFO] CMD_READ_VIRTUAL_MEMORY \r\n");
        UINT64 tprocess =
            GetWindowsEProcess((CHAR8 *)protocol.module.target_process);
        if (!tprocess) {
            return EFI_NOT_FOUND;
        }

        LOG_VERB("[INFO] tprocess base: 0x%x\r\n", tprocess);

        UINT64 module_base = GetWindowsBaseAddressModuleX64(
            tprocess, protocol.module.target_module);

        if (!module_base) {
            return EFI_NOT_FOUND;
        }

        LOG_VERB("[INFO] module base: 0x%x\r\n", module_base);

        UINT64 buffer_phys = CachedVTOP(
            processCr3, section + offsetof(SmmCommunicationProtocol, buffer));
        *(UINT64 *)buffer_phys = module_base;
        break;
    }}
}
