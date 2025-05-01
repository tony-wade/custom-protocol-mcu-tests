#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include "FTD2XX.h"

// 定義 SPI 設置的參數
#define CLK_PERIOD 41  // 週期 = 41 bit
#define NORMAL_PERIOD 750  // 正常周期 (ns)
#define NORMAL_HIGH 284  // 正常高電平時間 (ns)
#define START_STOP_PERIOD 1400  // 起始/停止周期 (ns)
#define START_STOP_HIGH 1000  // 起始/停止高電平時間 (ns)

#define START_STOP_POSITION_1 6
#define START_STOP_POSITION_2 41

// 用來儲存 SPI 訊號的資料
FT_HANDLE ftHandle;   // FT232H 處理句柄
BYTE OutputBuffer[CLK_PERIOD * 10];  // MPSSE 輸出緩衝區 , 

DWORD dwNumBytesToSend = 0;
DWORD dwNumBytesSent = 0;    // 追蹤輸出用

// 設置 SPI 時鐘頻率 (divide from 60 MHz)
int dwClockDivisor = 29; //Value of clock divisor, SCL Frequency = 60/((1+29)*2) (MHz) = 1Mhz

void InitFTDI()
{
    FT_STATUS ftStatus = FT_Open(0, &ftHandle);  // 開啟裝置, 多通道裝置要調整port num
    if (ftStatus != FT_OK) {
        printf("Failed to open FT232H device\n");
        exit(1);
    }

    // 確保裝置重置，避免 MPSSE 問題
    FT_ResetDevice(ftHandle);

    // 設定 MPSSE 模式（0x00 表示所有 GPIO 交給 MPSSE 控制）
    ftStatus = FT_SetBitMode(ftHandle, 0x00, 0x02);
    if (ftStatus != FT_OK) {
        printf("Failed to set MPSSE mode\n");
        exit(1);
    }

    DWORD dwNumBytesToSend = 0;
    BYTE OutputBuffer[16];

    OutputBuffer[dwNumBytesToSend++] = 0x8A; // 禁用內部時鐘分頻
    OutputBuffer[dwNumBytesToSend++] = 0x97; // 禁用適配器輸出
    OutputBuffer[dwNumBytesToSend++] = 0x8D; // 禁用 3 相時鐘 

    OutputBuffer[dwNumBytesToSend++] = 0x86; // 設定 Clock Divisor
    OutputBuffer[dwNumBytesToSend++] = (BYTE)(dwClockDivisor & 0xFF);
    OutputBuffer[dwNumBytesToSend++] = (BYTE)((dwClockDivisor >> 8) & 0xFF);

    ftStatus = FT_Write(ftHandle, OutputBuffer, dwNumBytesToSend, &dwNumBytesSent);
    if (ftStatus != FT_OK || dwNumBytesSent != dwNumBytesToSend) {
        printf("Failed to init\n");
        exit(1);

    Sleep(50);
    }
}

// MPSSE 
void SendDAT(byte Hi, byte Lo)
{
    FT_STATUS ftStatus;

    // 初始化 MPSSE 緩衝區
    dwNumBytesToSend = 0;

    // initial state
    OutputBuffer[dwNumBytesToSend++] = 0x80; // Set IOs
    OutputBuffer[dwNumBytesToSend++] = 0x01; // Clock start Hi
    OutputBuffer[dwNumBytesToSend++] = 0x0F; // First 4 is output

    // 填充 MPSSE cmd to CLKBuffer 
    OutputBuffer[dwNumBytesToSend++] = 0x10;    // Byte, on Pos edge
    OutputBuffer[dwNumBytesToSend++] = 2 - 1;   //Length of Data 0 = 1 Byte LoByte
    OutputBuffer[dwNumBytesToSend++] = 0;
    OutputBuffer[dwNumBytesToSend++] = Hi;  
    OutputBuffer[dwNumBytesToSend++] = Lo;

    // return init
    OutputBuffer[dwNumBytesToSend++] = 0x80; 
    OutputBuffer[dwNumBytesToSend++] = 0x01;  
    OutputBuffer[dwNumBytesToSend++] = 0x0F; 


    ftStatus = FT_Write(ftHandle, OutputBuffer, sizeof(OutputBuffer), &dwNumBytesSent);
    if (ftStatus != FT_OK) {
            printf("Failed to write to FT232H\n");
            exit(1);
    } 
}

void CleanupFTDI()
{
    FT_STATUS ftStatus = FT_Close(ftHandle);  // 關閉 FT232H 裝置
    if (ftStatus != FT_OK) {
        printf("Failed to close FT232H device\n");
    }
}

int main()
{
    InitFTDI();   
    SendDAT(0xA5, 0x5A);
    CleanupFTDI();    

    printf("Program finished. Press Enter to exit...\n");
    getchar();  // 等待用戶按 Enter 鍵 
    return 0;
}
