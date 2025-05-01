#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include "ftd2xx.h"  // 引入 D2XX 驅動頭檔

FT_HANDLE ftHandle; // FT232H handle
FT_STATUS status;
DWORD numDevs;
BYTE portValue = 0x00; // initial value for port 0
BYTE ucMask = 0x01;    //每個 bit 代表一個引腳，1 表示輸出）

int main() {
    // 查詢可用的 FT232H 設備數量
    status = FT_CreateDeviceInfoList(&numDevs); // 查詢設備列表
    if (status != FT_OK || numDevs == 0) {
        printf("No FTDI devices found\n");
        return -1;
    }

    printf("Number of FTDI devices: %d\n", numDevs);

    // 打開第一個設備
    status = FT_Open(0, &ftHandle); // 打開設備
    if (status != FT_OK) {
        printf("Error opening device\n");
        return -1;
    }

    // 設置端口模式
    status = FT_SetBitMode(ftHandle, ucMask, 0x01); // 0x1: 非同步位元方式
    status = FT_SetBitMode(ftHandle, 0, FT_BITMODE_RESET); // 重置所有端口
    if (status != FT_OK) {
        printf("Error resetting port\n");
        return -1;
    }

    // 設定 Port 0 = ADBUS0, 為數位輸出
    status = FT_SetBitMode(ftHandle, ucMask, 0x01); 
    if (status != FT_OK) {
        printf("Error setting bit mode\n");
        return -1;
    }
     
    printf("Starting port0 toggle test...\n");
    while (1) {
        for (int i = 0; i < 10; i++) {   
            portValue = ~portValue;
            status = FT_Write(ftHandle, &portValue, 1, &numDevs);
            if (status != FT_OK) {
                printf("Error writing to port\n");
                break;
            }
        }
        Sleep(1);  // 1ms 
    }

    // 關閉設備
    FT_Close(ftHandle);
    return 0;
} 