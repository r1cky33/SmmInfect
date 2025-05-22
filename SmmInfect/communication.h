#pragma once
#include <PiSmm.h>
#include <Protocol/SmmCpu.h>
#include <Uefi.h>
#define SMM_PROTOCOL_MAGIC 'i'

#define CMD_READ_VIRTUAL_MEMORY 0x10
#define CMD_GET_MODULE_BASE 0x20

#pragma pack(1)
typedef struct _SmmCommunicationProtocol {
    UINT8 magic;   // offset 0
    UINT8 command; // offset 1
    union {
        // For CMD_READ_VIRTUAL_MEMORY
        struct {
            UINT8 process_name[30]; // ASCII target process name
            UINT64 address;         // Absolute address to read
            UINT64 size;            // Read size
        } read;

        // For CMD_GET_MODULE_BASE
        struct {
            UINT8 target_process[30]; // ASCII process name
            UINT16 target_module[30]; // UTF-16 module name (like original)
        } module;
    };
    UINT8 buffer[512]; // Enlarged read buffer
    UINT64 smi_count;  // SMI counter
} SmmCommunicationProtocol;
#pragma pack()

UINT64 GetCommunicationProcess();
EFI_STATUS PerformCommunication();
