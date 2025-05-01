/*
    GPIO access on RP1 through PCI BAR1
        - 以 /dev/mem 操作RP1 測試極限速度
      
    Compile with:
      gcc -o name file_path -O2 
    Run with:
      sudo ./name

    Originated from Praktronics
*/

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>

void delay_ns(int nanoseconds)
{
    struct timespec ts;
    ts.tv_sec = 0;  //  
    ts.tv_nsec = nanoseconds % 1000000000L;  // 取餘數作為納秒部分
    nanosleep(&ts, NULL);
}

// PCI BAR1 資訊
#define RP1_BAR1      0x1f00000000
#define RP1_BAR1_LEN  0x400000

// 偏移量 (參照 RP1 文件)
#define RP1_IO_BANK0_BASE      0x0d0000
#define RP1_RIO0_BASE          0x0e0000
#define RP1_PADS_BANK0_BASE    0x0f0000

// Atomic 操作偏移
#define RP1_ATOM_XOR_OFFSET    0x1000
#define RP1_ATOM_SET_OFFSET    0x2000
#define RP1_ATOM_CLR_OFFSET    0x3000

// PADS 設定偏移
#define PADS_BANK0_GPIO_OFFSET 0x4

// RIO 寄存器偏移
#define RIO_OUT_OFFSET         0x00
#define RIO_OE_OFFSET          0x04
#define RIO_NOSYNC_IN_OFFSET   0x08
#define RIO_SYNC_IN_OFFSET     0x0C

// 控制值
#define CTRL_MASK_FUNCSEL 0b00000000000000000000000000011111
#define PADS_MASK_OUTPUT  0b00000000000000000000000011000000
#define CTRL_FUNCSEL_RIO  0x05

// 將 /dev/mem 映射到使用者空間
void *mapgpio(off_t dev_base, off_t dev_size)
{
    int fd;
    void *mapped;
    
    printf("sizeof(off_t): %zu\n", sizeof(off_t));

    if ((fd = open("/dev/mem", O_RDWR | O_SYNC)) == -1)
    {
        printf("Can't open /dev/mem\n");
        return NULL;
    }

    mapped = mmap(0, dev_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, dev_base);
    // 不關閉 fd 可保留存取權限
    printf("base address: %llx, size: %x, mapped: %p\n", dev_base, dev_size, mapped);

    if (mapped == MAP_FAILED)
    {
        printf("Can't map the memory to user space.\n");
        return NULL;
    }

    return mapped;
}

typedef struct
{
    uint8_t number;
    volatile uint32_t *status;
    volatile uint32_t *ctrl;
    volatile uint32_t *pad;
} gpio_pin_t;

typedef struct
{
    volatile void *rp1_peripherial_base;
    volatile void *gpio_base;
    volatile void *pads_base;
    volatile uint32_t *rio_out;
    volatile uint32_t *rio_output_enable;
    volatile uint32_t *rio_nosync_in;

    gpio_pin_t *pin;   // 只使用一個 pin（GPIO 17）
} rp1_t;

// 建立 RP1 裝置
bool create_rp1(rp1_t **rp1)
{
    rp1_t *r = calloc(1, sizeof(rp1_t));
    if (r == NULL)
        return false;

    void *base = mapgpio(RP1_BAR1, RP1_BAR1_LEN);
    if (base == NULL)
        return false;

    r->rp1_peripherial_base = base;
    r->gpio_base = base + RP1_IO_BANK0_BASE;
    r->pads_base = base + RP1_PADS_BANK0_BASE;
    r->rio_out = (volatile uint32_t *)(base + RP1_RIO0_BASE + RIO_OUT_OFFSET);
    r->rio_output_enable = (volatile uint32_t *)(base + RP1_RIO0_BASE + RIO_OE_OFFSET);
    r->rio_nosync_in = (volatile uint32_t *)(base + RP1_RIO0_BASE + RIO_NOSYNC_IN_OFFSET);

    *rp1 = r;
    return true;
}

// 建立並初始化單一 GPIO pin（這裡只建立 GPIO 17）
bool create_pin(uint8_t pinnumber, rp1_t *rp1)
{
    gpio_pin_t *newpin = calloc(1, sizeof(gpio_pin_t));
    if(newpin == NULL) return false;

    newpin->number = pinnumber;
    newpin->status = (uint32_t *)(rp1->gpio_base + 8 * pinnumber);
    newpin->ctrl = (uint32_t *)(rp1->gpio_base + 8 * pinnumber + 4);
    newpin->pad = (uint32_t *)(rp1->pads_base + PADS_BANK0_GPIO_OFFSET + pinnumber * 4);

    // 清除原本的功能設定，再設定為 RIO 功能
    *(newpin->ctrl + RP1_ATOM_CLR_OFFSET / 4) = CTRL_MASK_FUNCSEL;
    *(newpin->ctrl + RP1_ATOM_SET_OFFSET / 4) = CTRL_FUNCSEL_RIO;

    rp1->pin = newpin;
    printf("GPIO %d created.\n", pinnumber);
    return true;
}

// 啟用輸出：設定 pads 並啟用 RIO 輸出
int pin_enable_output(uint8_t pinnumber, rp1_t *rp1)
{
    printf("Enabling output on GPIO %d...\n", pinnumber);
    volatile uint32_t *writeadd = rp1->pin->pad + RP1_ATOM_CLR_OFFSET / 4;
    printf("Writing to pad %p at address %p\n", rp1->pin->pad, writeadd);
    *writeadd = PADS_MASK_OUTPUT;
    *(rp1->rio_output_enable + RP1_ATOM_SET_OFFSET / 4) = 1 << rp1->pin->number;
    return 0;
}

// 打開與關閉 pin 的函數
void pin_on(rp1_t *rp1, uint8_t pin)
{
    *(rp1->rio_out + RP1_ATOM_SET_OFFSET / 4) = 1 << pin;
}

void pin_off(rp1_t *rp1, uint8_t pin)
{
    *(rp1->rio_out + RP1_ATOM_CLR_OFFSET / 4) = 1 << pin;
}

int main(void)
{
    rp1_t *rp1;
    printf("Creating RP1 device...\n");
    if (!create_rp1(&rp1))
    {
        printf("Unable to create RP1 device\n");
        return 2;
    }

    // 僅建立 GPIO 17
    if (!create_pin(17, rp1))
    {
        printf("Unable to create GPIO 17\n");
        return 3;
    }
    pin_enable_output(17, rp1);

    // 重覆開關 GPIO 17
    while (1) {
        pin_on(rp1, 17);    // 直接切換約20 MHz
        //printf("GPIO 17 ON\n");  
        pin_off(rp1, 17);
        //printf("GPIO 17 OFF\n"); 

        pin_on(rp1, 17);
        //printf("GPIO 17 ON\n");  
        pin_off(rp1, 17);
        //printf("GPIO 17 OFF\n");


        delay_ns(375);     // 實際花了 53 microseconds (19 KHz)... 
    }

    //printf("Done toggling GPIO 17.\n");
    return 0;
}
