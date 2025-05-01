#include <stdio.h>
#include <windows.h>
#include "ftd2xx.h"

#define MSB_RISING_EDGE_CLOCK_BYTE_IN  0x20
#define SEND_IMMEDIATE                  0x87

int main() {
    FT_HANDLE ftHandle;
    FT_STATUS ftStatus;
    DWORD dwNumBytesToSend = 0;
    DWORD dwNumBytesSent = 0;
    DWORD dwNumBytesRead = 0;
    DWORD rxSize = 0;

    UCHAR OutputBuffer[64];
    UCHAR InputBuffer[64];

    // 開啟裝置
    ftStatus = FT_Open(0, &ftHandle);
    if (ftStatus != FT_OK) {
        printf("FT_Open failed\n");
        return 1;
    }

    // Reset & 進入 MPSSE 模式
    FT_ResetDevice(ftHandle);
    FT_SetTimeouts(ftHandle, 5000, 5000);
    FT_Purge(ftHandle, FT_PURGE_RX | FT_PURGE_TX);
    FT_SetUSBParameters(ftHandle, 65536, 65536);
    FT_SetLatencyTimer(ftHandle, 1);
    FT_SetBitMode(ftHandle, 0x00, 0x00);  // reset
    Sleep(50);
    FT_SetBitMode(ftHandle, 0x00, 0x02);  // MPSSE
    Sleep(50);

    // 設定 clock divisor => 100kHz
    OutputBuffer[0] = 0x86;
    OutputBuffer[1] = 0x2C;  // low byte
    OutputBuffer[2] = 0x01;  // high byte → (1 + 2*300) = 601 → 60MHz / 601 ≈ 99.83kHz
    FT_Write(ftHandle, OutputBuffer, 3, &dwNumBytesSent);

    // 設定 SCL 為輸出 (ADBUS0)，SDA 為輸入 (ADBUS1)
    OutputBuffer[0] = 0x80;
    OutputBuffer[1] = 0x00;  // initial value (SCL low)
    OutputBuffer[2] = 0x01;   
    FT_Write(ftHandle, OutputBuffer, 3, &dwNumBytesSent);

    Sleep(10); // 給 SDA 穩定時間

    // 要 clock-in 4 bytes
    dwNumBytesToSend = 0;
    OutputBuffer[dwNumBytesToSend++] = MSB_RISING_EDGE_CLOCK_BYTE_IN;  // 0x20
    OutputBuffer[dwNumBytesToSend++] = 0x03;  // 4 bytes = 0x0003 + 1
    OutputBuffer[dwNumBytesToSend++] = 0x00;
    OutputBuffer[dwNumBytesToSend++] = SEND_IMMEDIATE;  // 0x87

    FT_Write(ftHandle, OutputBuffer, dwNumBytesToSend, &dwNumBytesSent);
    dwNumBytesToSend = 0;

    // 等待資料回來
    do {
        FT_GetQueueStatus(ftHandle, &rxSize);
        Sleep(1);
    } while (rxSize < 4);

    FT_Read(ftHandle, InputBuffer, 4, &dwNumBytesRead);

    printf("Read bytes:\n");
    for (DWORD i = 0; i < dwNumBytesRead; i++) {
        printf("%02X ", InputBuffer[i]);
    }
    printf("\n");

    // 再讀一次腳位電壓狀態（ADBUS pins）  # 上機後僅首次為low
    dwNumBytesToSend = 0;
    OutputBuffer[dwNumBytesToSend++] = 0x81;  // Read ADBUS
    OutputBuffer[dwNumBytesToSend++] = 0x87;  // Send Immediate
    FT_Write(ftHandle, OutputBuffer, dwNumBytesToSend, &dwNumBytesSent);
    dwNumBytesToSend = 0;

    FT_Read(ftHandle, InputBuffer, 1, &dwNumBytesRead);

    if ((InputBuffer[0] & 0x02) == 0) {
        printf("SDA (ADBUS1) is LOW\n");
    }
    else {
        printf("SDA (ADBUS1) is HIGH\n");
    }

    // 清理
    FT_Close(ftHandle);

    getchar();
    return 0;
}
