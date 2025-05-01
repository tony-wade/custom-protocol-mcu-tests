#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include "FTD2XX.h"

const BYTE MSB_FALLING_EDGE_CLOCK_BYTE_IN = '\x24'; 
const BYTE MSB_BYTE_OUT_POSITIVE_TRIGG = '\x11';
const BYTE MSB_BIT_OUT_POSITIVE_TRIGG = '\x13';
FT_STATUS ftStatus; //Status defined in D2XX to indicate operation result
FT_HANDLE ftHandle; //Handle of FT2232H device port
BYTE OutputBuffer[1024]; //Buffer to hold MPSSE commands and data to be sent to FT2232H
BYTE InputBuffer[1024]; //Buffer to hold Data bytes to be read from FT2232H
DWORD dwClockDivisor = 0x0009; // Value of clock divisor, ideally SCL Frequency = 60/((1+0x0095)*2) (MHz) = 200khz
DWORD dwNumBytesToSend = 0; // Index of output buffer
DWORD dwNumBytesSent = 0, dwNumBytesRead = 0, dwNumInputBuffer = 0;



// Start/Stop operation will cap speed limit 
void HighSpeedStart(void)
{
    DWORD dwCount;
    OutputBuffer[dwNumBytesToSend++] = '\x80'; //Command to set directions of lower 8 pins = ADBUS and force value on bits set as output
    OutputBuffer[dwNumBytesToSend++] = '\x03'; //Set SDA, SCL high
    OutputBuffer[dwNumBytesToSend++] = '\x03'; //Set pin in/out
    
    OutputBuffer[dwNumBytesToSend++] = '\x80';  
    OutputBuffer[dwNumBytesToSend++] = '\x01'; //Set SDA low, SCL high
    OutputBuffer[dwNumBytesToSend++] = '\x03'; 

    OutputBuffer[dwNumBytesToSend++] = '\x80';
    OutputBuffer[dwNumBytesToSend++] = '\x00'; //Set SDA, SCL low
    OutputBuffer[dwNumBytesToSend++] = '\x03';
}

void HighSpeedStop(void)
{
    OutputBuffer[dwNumBytesToSend++] = '\x80';
    OutputBuffer[dwNumBytesToSend++] = '\x00'; //Set SDA, SCL low
    OutputBuffer[dwNumBytesToSend++] = '\x03';

    OutputBuffer[dwNumBytesToSend++] = '\x80'; //Command to set directions of lower 8 pins = ADBUS and force value on bits set as output
    OutputBuffer[dwNumBytesToSend++] = '\x01'; //Set SDA low, SCL high
    OutputBuffer[dwNumBytesToSend++] = '\x03'; //Set pin in/out
     
    OutputBuffer[dwNumBytesToSend++] = '\x80'; 
    OutputBuffer[dwNumBytesToSend++] = '\x03'; //Set SDA, SCL high
    OutputBuffer[dwNumBytesToSend++] = '\x03'; 

    OutputBuffer[dwNumBytesToSend++] = '\x80';
    OutputBuffer[dwNumBytesToSend++] = '\x03'; //Set SDA, SCL high
    OutputBuffer[dwNumBytesToSend++] = '\x03'; 
}

// 4 bits cmd in protocol form 
BOOL SendCmd(BYTE cmdByte)
{ 
    // Protocol structure, only appears in begin 
    OutputBuffer[dwNumBytesToSend++] = '\x80'; //Command to set directions of lower 8 pins = ADBUS and force value on bits set as output
    OutputBuffer[dwNumBytesToSend++] = '\x02'; //Set SDA high, SCL low
    OutputBuffer[dwNumBytesToSend++] = '\x03'; //Set pin in/out 
      
    HighSpeedStart();

    cmdByte = (cmdByte << 1); // discard bit 7 (start_bit) from data 
    OutputBuffer[dwNumBytesToSend++] = MSB_BIT_OUT_POSITIVE_TRIGG;
    OutputBuffer[dwNumBytesToSend++] = '\x03'; //Data length of 0x0000 means 1 bit
    OutputBuffer[dwNumBytesToSend++] = cmdByte; // data to be send

    HighSpeedStop();
    return TRUE;
}

// 4 bytes data in protocol form
BOOL SendData(BYTE dataList[4])
{
    HighSpeedStart();

    // 2 fixed bits = 0b10 at front
    OutputBuffer[dwNumBytesToSend++] = MSB_BIT_OUT_POSITIVE_TRIGG;
    OutputBuffer[dwNumBytesToSend++] = '\x01'; // 2 fixed bits
    OutputBuffer[dwNumBytesToSend++] = 0x80;

    // 4 bytes data
    OutputBuffer[dwNumBytesToSend++] = MSB_BYTE_OUT_POSITIVE_TRIGG;
    OutputBuffer[dwNumBytesToSend++] = '\x03'; // 0x0003 means 4 bytes
    OutputBuffer[dwNumBytesToSend++] = '\x00';
     
    for (int i = 0; i < 4; i++) {
        OutputBuffer[dwNumBytesToSend++] = dataList[i];
    }

    HighSpeedStop();
    return TRUE;
}

//  demo
void send_byte(void)
{  
    BYTE CmdToBeSend = 0xb0;
    BYTE BytesToBeSend[4] = { 0xDE, 0xAD, 0xAE, 0xEB};

    SendCmd(CmdToBeSend);
    SendData(BytesToBeSend);

    SendCmd(0xb8);
    SendData(BytesToBeSend);
     
    //Send off the commands
    ftStatus = FT_Write(ftHandle, OutputBuffer, dwNumBytesToSend, &dwNumBytesSent);
    dwNumBytesToSend = 0; //Clear output buffer

    Sleep(50); //Delay for a while to ensure EEPROM program is completed
}


void InitFTDI(void)
{ 
    //Try to open the FT2232H device port and get the valid handle for subsequent access 
    char SerialNumBuf[64];
    ftStatus = FT_ListDevices((PVOID)0, &SerialNumBuf, FT_LIST_BY_INDEX | FT_OPEN_BY_SERIAL_NUMBER);   
    ftStatus = FT_OpenEx((PVOID)SerialNumBuf, FT_OPEN_BY_SERIAL_NUMBER, &ftHandle);

    if (ftStatus == FT_OK)
    { // Port opened successfully
        ftStatus |= FT_ResetDevice(ftHandle); //Reset USB device 
        ftStatus |= FT_SetBitMode(ftHandle, 0x0, 0x00); //Reset controller
        ftStatus |= FT_SetBitMode(ftHandle, 0x0, 0x02); //Enable MPSSE mode
        if (ftStatus != FT_OK) { 
            printf("Failed to init device\n");
            exit(1);
        }
        Sleep(50); // Wait for all the USB stuff to complete and work

        OutputBuffer[dwNumBytesToSend++] = '\x8A'; //Ensure disable clock divide by 5 for 60Mhz master clock
        OutputBuffer[dwNumBytesToSend++] = '\x97'; //Ensure turn off adaptive clocking
        OutputBuffer[dwNumBytesToSend++] = '\x8C'; //Enable 3 phase data clock = control data, clk separately
        ftStatus = FT_Write(ftHandle, OutputBuffer, dwNumBytesToSend, &dwNumBytesSent);  
        dwNumBytesToSend = 0; //Clear output buffer

        OutputBuffer[dwNumBytesToSend++] = '\x80'; //Command to set directions of lower 8 pins and force value on bits set as output
        OutputBuffer[dwNumBytesToSend++] = '\x00'; //Set SDA, SCL low
        OutputBuffer[dwNumBytesToSend++] = '\x03'; //Set SK,DO pins as output with bit ¡¦, other pins as inpbut with bit ¡¥¡¦

        // SK frequency = 60MHz /((1 + [(1 +0xValueH*256) OR 0xValueL])*2)
        // Don't rely on this formula if in high speed!!!
        OutputBuffer[dwNumBytesToSend++] = '\x86'; //Command to set clock divisor
        OutputBuffer[dwNumBytesToSend++] = dwClockDivisor & '\xFF'; //Set 0xValueL of clock divisor
        OutputBuffer[dwNumBytesToSend++] = (dwClockDivisor >> 8) & '\xFF'; //Set 0xValueH of clock divisor
        ftStatus = FT_Write(ftHandle, OutputBuffer, dwNumBytesToSend, &dwNumBytesSent);  
        dwNumBytesToSend = 0;   

        Sleep(20); //Delay 20ms 
    }
}

 
 
void CleanupFTDI()
{
    FT_STATUS ftStatus = FT_Close(ftHandle);  // Ãö³¬ FT2232H
    if (ftStatus != FT_OK) {
        printf("Failed to close FT232H device\n"); 
        exit(1);
    }
}

int main()
{
    InitFTDI();
    send_byte();
    CleanupFTDI();

    printf("Program finished. Press Enter to exit...\n");
    getchar();  // µ¥«Ý¥Î¤á«ö Enter Áä 
    return 0;
}
