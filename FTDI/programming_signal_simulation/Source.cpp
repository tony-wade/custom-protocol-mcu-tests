#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include "FTD2XX.h"
#include <string.h>  

// ==================== FTDI Definitions ====================
const BYTE MSB_BYTE_IN_POSITIVE_TRIGG = '\x20';     // ADBUS2
const BYTE MSB_BYTE_OUT_POSITIVE_TRIGG = '\x11';    // ADBUS0
const BYTE MSB_BIT_OUT_POSITIVE_TRIGG = '\x13';     // ADBUS0

FT_STATUS ftStatus;        // Status returned by D2XX API to indicate result
FT_HANDLE ftHandle;        // Handle for FTDI device port

// Ideally, SCL frequency = 60 / ((1 + value) * 2) MHz
// In high-speed scenario:
//     0x09: 2.0 MHz
//     0x10: 1.18 MHz 
DWORD dwClockDivisor = 0x0010;   // Clock divisor for FTDI (sets SCL frequency)

#define FTDI_MAX_BYTES 4096
BYTE OutputBuffer[FTDI_MAX_BYTES];   // Buffer for MPSSE commands and data to be sent (FIFO TX)
BYTE InputBuffer[12];              // Buffer for data read from FTDI

DWORD dwNumBytesToSend = 0;          // Number of bytes to send
DWORD dwNumBytesSent = 0;
DWORD dwNumBytesRead = 0;
DWORD dwNumInputBuffer = 0;


// ==================== Other Definitions ====================
LARGE_INTEGER frequency;  // High-resolution counter frequency (used for timing)

#define MAX_FILES 100
#define MAX_PATH_LEN 256

int file_count = 0;
char file_list[MAX_FILES][MAX_PATH_LEN];

const char* input_folder_path = "..\\..\\input_datas";  // Relative path to .exe (\\..\\ = go up one folder)



// ==================== Core Functions ====================
void MPSSEStart(void)
{//  note: Start/Stop operation will cap speed limit 
    OutputBuffer[dwNumBytesToSend++] = '\x80'; //Command to set directions of lower 8 pins = ADBUS and force value on bits set as output
    OutputBuffer[dwNumBytesToSend++] = '\x03'; //Set SDA, SCL high, bit 7~0 as ADBUS 7~0 
    OutputBuffer[dwNumBytesToSend++] = '\x03'; //Set pin in/out

    OutputBuffer[dwNumBytesToSend++] = '\x80';
    OutputBuffer[dwNumBytesToSend++] = '\x01'; //Set SDA low, SCL high
    OutputBuffer[dwNumBytesToSend++] = '\x03';

    OutputBuffer[dwNumBytesToSend++] = '\x80';
    OutputBuffer[dwNumBytesToSend++] = '\x00'; //Set SDA, SCL low
    OutputBuffer[dwNumBytesToSend++] = '\x03';
}

void MPSSEStop(void)
{
    OutputBuffer[dwNumBytesToSend++] = '\x80'; //Command to set directions of lower 8 pins = ADBUS and force value on bits set as output
    OutputBuffer[dwNumBytesToSend++] = '\x00'; //Set SDA, SCL low
    OutputBuffer[dwNumBytesToSend++] = '\x03';

    OutputBuffer[dwNumBytesToSend++] = '\x80';
    OutputBuffer[dwNumBytesToSend++] = '\x01'; //Set SDA low, SCL high
    OutputBuffer[dwNumBytesToSend++] = '\x03';  

    for (int i = 0; i < 3; i++) {
        OutputBuffer[dwNumBytesToSend++] = '\x80';
        OutputBuffer[dwNumBytesToSend++] = '\x03'; //Set SDA, SCL high
        OutputBuffer[dwNumBytesToSend++] = '\x03';
    } 
}

// 4 bits cmd out in, protocol form 
BOOL SendCmd(BYTE cmdByte)
{
    // Protocol structure 
    for (int i = 0; i < 2; i++) {
        OutputBuffer[dwNumBytesToSend++] = '\x80'; //Command to set directions of lower 8 pins = ADBUS and force value on bits set as output
        OutputBuffer[dwNumBytesToSend++] = '\x02'; //Set SDA high, SCL low
        OutputBuffer[dwNumBytesToSend++] = '\x03'; //Set pin in/out 
    } 

    MPSSEStart();

    cmdByte = (cmdByte << 1); // discard bit 7 (start_bit) from data 
    OutputBuffer[dwNumBytesToSend++] = MSB_BIT_OUT_POSITIVE_TRIGG;
    OutputBuffer[dwNumBytesToSend++] = '\x03'; //Data length of 0x0000 means 1 bit
    OutputBuffer[dwNumBytesToSend++] = cmdByte; // data to be send

    MPSSEStop();
    return TRUE;
}

// 4 bytes data out in, protocol form
BOOL SendData(BYTE dataList[4])
{
    MPSSEStart();

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

    MPSSEStop();
    return TRUE;
}

// 4 bytes data in, protocol form (remember to set latency and timeout)
// Due to weak pull-up design, FDTI will read 0xFF if slave is not responding
// Sending 0x00 is enough to check if no further usage on the data
BOOL ReadData(void)
{
    MPSSEStart();

    // 2 fixed bits = 0b10 at front
    OutputBuffer[dwNumBytesToSend++] = MSB_BIT_OUT_POSITIVE_TRIGG;
    OutputBuffer[dwNumBytesToSend++] = '\x01'; // 2 fixed bits
    OutputBuffer[dwNumBytesToSend++] = 0x80; 

    // Set SDA(ADBUS0),ADBUS2 as input, SCL remain out
    OutputBuffer[dwNumBytesToSend++] = '\x80';
    OutputBuffer[dwNumBytesToSend++] = '\x00'; //Set SDA, SCL low
    OutputBuffer[dwNumBytesToSend++] = '\x01'; 

    // 4 bytes read from ADBUS2
    OutputBuffer[dwNumBytesToSend++] = MSB_BYTE_IN_POSITIVE_TRIGG;
    OutputBuffer[dwNumBytesToSend++] = '\x03';// 0x0003 means 4 bytes 
    OutputBuffer[dwNumBytesToSend++] = '\x00'; 
     
    FT_Write(ftHandle, OutputBuffer, dwNumBytesToSend, &dwNumBytesSent);
    dwNumBytesToSend = 0; 
     
    MPSSEStop();
    return TRUE;
}

//  for signal checking
void Demo(void)
{
    BYTE CmdToBeSend = 0xb0;
    BYTE BytesToBeSend[4] = { 0xDE, 0xAD, 0xAE, 0xEB };

    SendCmd(CmdToBeSend);
    SendData(BytesToBeSend);

    SendCmd(0xb8);
    SendData(BytesToBeSend);

    //Send off the commands
    ftStatus = FT_Write(ftHandle, OutputBuffer, dwNumBytesToSend, &dwNumBytesSent);
    dwNumBytesToSend = 0; //Clear output buffer

    Sleep(50); //Delay for a while to ensure EEPROM program is completed
}


// ==================== Timing & Control ====================
void precise_delay(int milliseconds) 
{   
    LARGE_INTEGER start, current;

    // 獲取當前計數器的值
    QueryPerformanceCounter(&start);
     
    do {
        QueryPerformanceCounter(&current);
    } while ((double)(current.QuadPart - start.QuadPart) * 1000 / frequency.QuadPart < milliseconds);
}

// Send data if reach max buffer size
void FlushIfNeeded()
{
    //if (unitSize == 0 || unitSize > FTDI_MAX_BYTES) return;       // use if  need to track protocol size
    
    if (dwNumBytesToSend + 67 >= FTDI_MAX_BYTES) {// current protocol size=67
        FT_Write(ftHandle, OutputBuffer, dwNumBytesToSend, &dwNumBytesSent);
        dwNumBytesToSend = 0;
    }
}

// Count CSV files and store their paths
void ScanCsvFiles(const char* folder_path) {
    file_count = 0;
    char search_path[MAX_PATH_LEN];
    snprintf(search_path, MAX_PATH_LEN, "%s\\*.csv", folder_path);

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(search_path, &fd);

    if (hFind == INVALID_HANDLE_VALUE) {
        printf("No .csv files found.\n");
        return;
    }

    // recoed file path
    do {
        snprintf(file_list[file_count], MAX_PATH_LEN, "%s\\%s", folder_path, fd.cFileName);
        file_count++;
    } while (FindNextFileA(hFind, &fd));

    FindClose(hFind);
}

// Send data by record each row in HEX
void SendFromTheCsv(const char* file_path)
{
    FILE* fp;
    errno_t err = fopen_s(&fp, file_path, "r");
    if (err != 0 || !fp) {
        printf("Failed to open file: %s\n", file_path);
        return;
    }
    printf("Processing file: %s\n", file_path);

    char line[256];
    int row_idx = 0;

    // Read  a row as 'line'
    while (fgets(line, sizeof(line), fp)) {
        row_idx++;

        BYTE buffer[5]; // Cmd + 4 data bytes
        int col_idx = 0;

        char* token;
        char* context = NULL;   // pointer

        token = strtok_s(line, ",", &context); // read one value from 'context', seperate with ','

        while (token && col_idx < 5) {
            BYTE value = (BYTE)strtol(token, NULL, 16); // read token as hex
            buffer[col_idx++] = value;
            token = strtok_s(NULL, ",", &context); // next
        }

        if (col_idx == 5) {
            SendCmd(buffer[0]);
            SendData(&buffer[1]); // = buffer[1:]
            FlushIfNeeded();
        }
        else {
            printf("Skipped row %d in %s (less than 5 columns)\n", row_idx, file_path);
        }
    }

    fclose(fp);

    // Send all before next file
    if (dwNumBytesToSend > 0) {
        FT_Write(ftHandle, OutputBuffer, dwNumBytesToSend, &dwNumBytesSent);
        dwNumBytesToSend = 0;
    }

}
 
// Read if meet special command, else send data
void SendAndReadFromTheCsv(const char* file_path)
{
    FILE* fp;
    errno_t err = fopen_s(&fp, file_path, "r");
    if (err != 0 || !fp) {
        printf("Failed to open file: %s\n", file_path);
        return;
    }
    printf("Processing file: %s\n", file_path);

    char line[256];
    int row_idx = 0;

    // Read  a row as 'line'
    while (fgets(line, sizeof(line), fp)) {
        row_idx++;

        BYTE buffer[5]; // Cmd + 4 data bytes
        int col_idx = 0;

        char* token;
        char* context = NULL;   // pointer

        token = strtok_s(line, ",", &context); // read one value from 'context', seperate with ','

        while (token && col_idx < 5) {
            BYTE value = (BYTE)strtol(token, NULL, 16); // read token as hex
            buffer[col_idx++] = value;
            token = strtok_s(NULL, ",", &context); // next
        }

        if (col_idx == 5) {
            if (buffer[0] != 0x98) {
                SendCmd(buffer[0]);
                SendData(&buffer[1]); // = buffer[1:]
                FlushIfNeeded();
            }
            else {
                SendCmd(buffer[0]);
                ReadData();
            }
        }
        else {
            printf("Skipped row %d in %s (less than 5 columns)\n", row_idx, file_path);
        }
    }

    fclose(fp);

    // Send all before next file
    if (dwNumBytesToSend > 0) {
        FT_Write(ftHandle, OutputBuffer, dwNumBytesToSend, &dwNumBytesSent);
        dwNumBytesToSend = 0;
    }

    // Retrive data from FDTI chip, need some time to process
    FT_Read(ftHandle, &InputBuffer, 12, &dwNumBytesRead);
    if ((ftStatus == FT_OK) && dwNumBytesRead > 0) {
        for (DWORD i = 0; i < dwNumBytesRead; i++)
        {
            printf("%02X ", InputBuffer[i]);
        }
        printf("\n");
    }

    memset(InputBuffer, 0, sizeof(InputBuffer));    // clear cache 
}

// Sigal-sending schedule
void CommFlow(const char* folder_path) 
{
    LARGE_INTEGER start, current;
    
    ScanCsvFiles(folder_path);

    // Protocol structure 
    for (int i = 0; i < 3; i++) {
        OutputBuffer[dwNumBytesToSend++] = '\x80'; //Command to set directions of lower 8 pins = ADBUS and force value on bits set as output
        OutputBuffer[dwNumBytesToSend++] = '\x02'; //Set SDA high, SCL low
        OutputBuffer[dwNumBytesToSend++] = '\x03'; //Set pin in/out  
    }
     
    // enable programming
    for (int i = 0; i < 2; i++) { 
        SendFromTheCsv(file_list[i]);
        precise_delay(7);  // halt for 7 ms  
    }

    SendFromTheCsv(file_list[2]);

    QueryPerformanceCounter(&start);
    do {  // repeat for 3.14 s
        precise_delay(6);
        SendFromTheCsv(file_list[3]); 

        QueryPerformanceCounter(&current); 
    } while ((double)(current.QuadPart - start.QuadPart) / frequency.QuadPart < 3.14);
      
    if (file_count > 0) {
        precise_delay(200); 
        SendAndReadFromTheCsv(file_list[file_count - 1]);
     }


 }


// ==================== Program Entry & Setup ====================
void InitWinCounter(void)
{ 
    // Get frequency
    if (!QueryPerformanceFrequency(&frequency)) { 
        printf("Windows counter not supported.\n");
        exit(EXIT_FAILURE);
    }
}

void InitFTDI(void)
{
    //Try to open the FT2232H device port and get the valid handle for subsequent access 
    char SerialNumBuf[64];
    ftStatus = FT_ListDevices((PVOID)0, &SerialNumBuf, FT_LIST_BY_INDEX | FT_OPEN_BY_SERIAL_NUMBER);  // Change device index = (PVOID)0 if not connected
    ftStatus = FT_OpenEx((PVOID)SerialNumBuf, FT_OPEN_BY_SERIAL_NUMBER, &ftHandle);

    if (ftStatus == FT_OK)
    {
        ftStatus |= FT_ResetDevice(ftHandle); //Reset USB device  
        ftStatus |= FT_SetBitMode(ftHandle, 0x0, 0x00); //Reset controller 
        ftStatus |= FT_SetBitMode(ftHandle, 0x0, 0x02); //Enable MPSSE mode
        if (ftStatus != FT_OK) {
            printf("Failed to init device\n");
            exit(1);
        }
        Sleep(50); // Wait for all the USB stuff to complete and work

        FT_Purge(ftHandle, FT_PURGE_RX | FT_PURGE_TX); //Clear buffers

        OutputBuffer[dwNumBytesToSend++] = '\x8A'; //Ensure disable clock divide by 5 for 60Mhz master clock
        OutputBuffer[dwNumBytesToSend++] = '\x97'; //Ensure turn off adaptive clocking
        OutputBuffer[dwNumBytesToSend++] = '\x8C'; //Enable 3 phase data clock = control data, clk separately
        ftStatus = FT_Write(ftHandle, OutputBuffer, dwNumBytesToSend, &dwNumBytesSent);
        dwNumBytesToSend = 0; //Clear output buffer

        OutputBuffer[dwNumBytesToSend++] = '\x80'; //Command to set directions of lower 8 pins and force value on bits set as output
        OutputBuffer[dwNumBytesToSend++] = '\x00'; //Set all low
        OutputBuffer[dwNumBytesToSend++] = '\x03'; //Set as output with bit '1', other pins as inpbut with bit ‘0’

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
    FT_STATUS ftStatus = FT_Close(ftHandle);  // 關閉 FT2232H
    if (ftStatus != FT_OK) {
        printf("Failed to close FT232H device\n");
        exit(1);
    }
}

int main()
{
    InitWinCounter();
    InitFTDI();

    CommFlow(input_folder_path);
    //Demo();

    CleanupFTDI();

    printf("Program finished. Press Enter to exit...\n");
    getchar();  // 等待用戶按 Enter 鍵 
    return 0;
}
