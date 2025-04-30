#include "communication.h"
#include "Library/BaseMemoryLib.h"
#include "Uefi/UefiBaseType.h"
#include "windows.h"
#include "memory.h"
#include <Library/UefiLib.h>
#include <Protocol/SmmCpu.h>
#include <Uefi.h>
#include <Protocol/SmmCpu.h>
#include <PiSmm.h>
#include <stddef.h>
#include <string.h>
#include "serial.h"

UINT64 SmiCountIndex = 0;

UINT64 GetCommunicationProcess()
{
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

    SERIAL_PRINT("[INFO] kernel: 0x%x\r\n", kernel);
    SERIAL_PRINT("[INFO] cr3: 0x%x\r\n", cr3);

    UINT64 cprocess = GetCommunicationProcess();
    if (!cprocess) {
        return EFI_NOT_FOUND;
    }

    SERIAL_PRINT("[INFO] cprocess: 0x%x\r\n", cprocess);

    UINT64 base = GetWindowsBaseAddressModuleX64(cprocess, L"ricky.exe");
    if (!base) {
        return EFI_NOT_FOUND;
    }

    SERIAL_PRINT("[INFO] cprocess base: 0x%x\r\n", base);
    UINT64 section = GetWindowsSectionBaseAddressX64(cprocess, base, (UINT8*)".RICKY");
    if (!section) {
        return EFI_NOT_FOUND;
    }

    SERIAL_PRINT("[INFO] cprocess section: 0x%x\r\n", section);

    SmmCommunicationProtocol protocol = {0};
    ReadVirtual(section + 0b0, GetWindowsProcessCr3(cprocess), (UINT8*)&protocol, sizeof(protocol));

    if (protocol.magic != SMM_PROTOCOL_MAGIC) {
        return EFI_INVALID_PARAMETER;
    }

    *(UINT64*)TranslateVirtualToPhysical(GetWindowsProcessCr3(cprocess), section + offsetof(SmmCommunicationProtocol, smi_count)) = SmiCountIndex;

    switch (protocol.command) {
        case CMD_READ_VIRTUAL_MEMORY: {

            SERIAL_PRINT("[INFO] CMD_READ_VIRTUAL_MEMORY");
            UINT64 tprocess = GetWindowsEProcess((CHAR8*)protocol.read.process_name);
            if (!tprocess) {
                return EFI_NOT_FOUND;
            }

            SERIAL_PRINT("[INFO] tprocess base: 0x%x\r\n", tprocess);

            if (protocol.read.size > sizeof(protocol.buffer)) {
                return EFI_BAD_BUFFER_SIZE;
            }

            ReadVirtual(protocol.read.address, GetWindowsProcessCr3(tprocess), protocol.buffer, protocol.read.size);


            SERIAL_PRINT("[INFO] read: 0x%x  copying...\r\n", protocol.read.address);
            // write back results
            UINT64 buffer_phys = TranslateVirtualToPhysical(GetWindowsProcessCr3(cprocess), section + offsetof(SmmCommunicationProtocol, buffer));
            CopyMem((VOID*)buffer_phys, protocol.buffer, protocol.read.size);
            break;
        }

        case CMD_GET_MODULE_BASE: {

            SERIAL_PRINT("[INFO] CMD_READ_VIRTUAL_MEMORY");
            UINT64 tprocess = GetWindowsEProcess((CHAR8*)protocol.module.target_process);
            if (!tprocess) {
                return EFI_NOT_FOUND;
            }

    
            SERIAL_PRINT("[INFO] tprocess base: 0x%x\r\n", tprocess);
            
            UINT64 module_base = GetWindowsBaseAddressModuleX64(tprocess, protocol.module.target_module);

            if (!module_base) {
                return EFI_NOT_FOUND;
            }

            SERIAL_PRINT("[INFO] module base: 0x%x\r\n", module_base);

            // write back base address
            UINT64 buffer_phys = TranslateVirtualToPhysical(GetWindowsProcessCr3(cprocess),
                                                         section + offsetof(SmmCommunicationProtocol, buffer));
            *(UINT64*)buffer_phys = module_base;
            break;
        }
    }
}
