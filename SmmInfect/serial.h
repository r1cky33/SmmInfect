#include <Uefi.h>
#include <stdarg.h>
#include <stddef.h>

// Credits: Cr4sh

#define SERIAL_INIT() SerialPortInitialize(0x3F8, 115200)
#define SERIAL_PRINT(x, ...) DbgMsg(__FILE__, __LINE__, x, ##__VA_ARGS__)


VOID SerialPortInitialize(UINT16 Port, UINTN Baudrate);

VOID SerialPortWrite(UINT16 Port, UINT8 Data);
UINT8 SerialPortRead(UINT16 Port);
void DbgMsg(char *lpszFile, int Line, char *lpszMsg, ...);
size_t std_strlen(const char *str);
