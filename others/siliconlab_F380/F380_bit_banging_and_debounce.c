//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include <SI_C8051F380_Register_Enums.h>                // SFR declarations

// GPIO Pins (Check PORT_Init, Timer_Init if modified)
SI_SBIT(DATA, SFR_P0, 1);  // P0.1 as DATA
SI_SBIT(CLK, SFR_P0, 0);   // P0.0 as CLK
SI_SBIT(SW, SFR_P2, 0);    // P2.0 as SW (Silicon Labs macro)

// Timer Settings (Check Timer_Init)
#define TIMER_CLOCK  12000000UL  // Timer base clock (12 MHz)
#define FIXED_POINT_MULTIPLIER 1000UL  // Fixed-point scaling factor
#define NS_PER_TICK_FIXED ((1000000000UL * FIXED_POINT_MULTIPLIER) / TIMER_CLOCK)  // ~20833 ns per tick

// Debounce Timing
#define DEBOUNCE_TIME 500000  // Debounce time (ns)
#define DEBOUNCE_VALUE (65536 - (uint16_t)(((unsigned long)DEBOUNCE_TIME * FIXED_POINT_MULTIPLIER + (NS_PER_TICK_FIXED >> 1)) / NS_PER_TICK_FIXED))  // Rounded integer


// Clock Timing Parameters
// Use Get_custom_CLK_ticks.ipynb to pre-calculate register value
#define CLK_PERIOD  41  // Clock cycle period
//#define NORMAL_PERIOD 750   // Total CLK duration (ns), extra 150ns low for START/STOP
//#define NORMAL_HIGH  284   // High-level duration (ns)
//#define START_STOP_PERIOD 1400  // Start-Stop total duration (ns)
//#define START_STOP_HIGH 1000    // Start-Stop high-level duration (ns)

// Start-Stop Positions
//const uint8_t start_stop_positions[] = {6, 41};
//#define START_STOP_COUNT (sizeof(start_stop_positions) / sizeof(start_stop_positions[0]))

//-----------------------------------------------------------------------------
// Clock Reload Values (Stored in Flash to prevent RAM overflow)
//-----------------------------------------------------------------------------
code uint16_t CLK_reload[82] = {
    65522, 65514, 65522, 65514, 65522, 65506, 65488, 65514, 65522, 65514,
    65522, 65514, 65522, 65514, 65522, 65514, 65522, 65514, 65522, 65514,
    65522, 65514, 65522, 65514, 65522, 65514, 65522, 65514, 65522, 65514,
    65522, 65514, 65522, 65514, 65522, 65514, 65522, 65514, 65522, 65514,
    65522, 65517, 65522, 65514, 65522, 65514, 65522, 65514, 65522, 65514,
    65522, 65514, 65522, 65514, 65522, 65514, 65522, 65514, 65522, 65514,
    65522, 65514, 65522, 65514, 65522, 65514, 65522, 65514, 65522, 65514,
    65522, 65514, 65522, 65514, 65522, 65514, 65522, 65514, 65522, 65514,
    65522, 65514
};

//-----------------------------------------------------------------------------
// Global Variables
//-----------------------------------------------------------------------------
volatile uint8_t clk_index = 0;  // Index for clock reload values
bit debounced = 0;  // Debounce flag

//-----------------------------------------------------------------------------
// SiLabs_Startup() Routine
// ----------------------------------------------------------------------------
void SiLabs_Startup (void)
{
  PCA0MD = 0x00;                      // Disable watchdog timer
}

//-----------------------------------------------------------------------------
// Port Initialization
//-----------------------------------------------------------------------------
void PORT_Init(void) {
    // GPIO Input: No need to configure GND & VCC
    P0MDOUT |= 0x03;    // Set P0.0~P0.1 as push-pull output

    // Crossbar configuration, critical
    XBR0 = 0x00;        // Disable all digital peripherals
    //XBR0 = 0x08;      // Enable SYSCLK output (if needed)
    XBR1 = 0x40;        // Enable crossbar (allows output)

    // Initial pin states
    CLK = 0;
    DATA = 0;
}

//-----------------------------------------------------------------------------
// Oscillator Initialization
//-----------------------------------------------------------------------------
void OSCILLATOR_Init(void) {
    uint8_t i;

    OSCICN = 0x83;   // Internal oscillator = 12 MHz
    RSTSRC = 0x04;   // Enable missing clock detector (for external oscillator)

    // Configure Clock Multiplier (4x)
    CLKMUL = 0x00;   // Reset multiplier
    CLKMUL |= 0x80;  // Enable multiplier
    for (i = 35; i > 0; i--);  // Wait for stabilization
    CLKMUL |= 0xC0;  // Initialize multiplier
    while ((CLKMUL & 0x20) == 0);  // Wait for readiness

    CLKSEL = 0x03;   // Set SYSCLK = 48 MHz (internal oscillator)
}

//-----------------------------------------------------------------------------
// Timer Initialization
//-----------------------------------------------------------------------------
void Timer_Init(void) {
    TMOD |= 0x11;  // Set Timer0 & Timer1 as 16-bit timers
    CKCON |= 0x0F; // Use SYSCLK (48 MHz) for Timer0 & Timer1

    TCON_TR0 = 0;  // Ensure Timer0 is off
    TCON_TR1 = 0;  // Ensure Timer1 is off

    IE_ET0 = 1;    // Enable Timer0 interrupt
    IE_ET1 = 1;    // Enable Timer1 interrupt
    IE_EA = 1;     // Enable global interrupts
}

//-----------------------------------------------------------------------------
// Timer0 Functions
//-----------------------------------------------------------------------------
// Load Timer0 with a reload value (reentrant)
void Hold_Timer0(uint16_t reload) reentrant {
    TH0 = reload >> 8;    // High byte
    TL0 = reload & 0xFF;  // Low byte
}

// Timer0 ISR - Generates clock signal
SI_INTERRUPT(Timer0_ISR, 1) {
    CLK ^= 1;  // Toggle clock

    Hold_Timer0(CLK_reload[clk_index]);
    clk_index = (clk_index + 1) & ((CLK_PERIOD * 2) - 1);  // Wrap index
}

//-----------------------------------------------------------------------------
// Timer1 Functions (Debounce Handling)
//-----------------------------------------------------------------------------
// Start Timer1 for debounce delay
void Start_Debounce_Timer1(void) {
    TH1 = DEBOUNCE_VALUE >> 8;   // High byte
    TL1 = DEBOUNCE_VALUE & 0xFF; // Low byte
    TCON_TR1 = 1;  // Start Timer1
}

// Timer1 ISR - Handles debounce timing
SI_INTERRUPT(Timer1_ISR, 3) {
    TCON_TR1 = 0;  // Stop Timer1
    debounced = 1; // Debounce complete
}

//-----------------------------------------------------------------------------
// Button Press Detection with Debounce
//-----------------------------------------------------------------------------
bit ButtonPressed(void) {
    static bit valid_press = 0;

    if (!SW) {  // Button pressed (active low)
        if (debounced) {
            valid_press = 1;
        } else if (!TCON_TR1) {  // Start debounce if not running
            Start_Debounce_Timer1();
        }
    } else {  // Button released
        if (valid_press) {  // Confirm press after release
            valid_press = 0;
            debounced = 0;
            return 1;
        }
        debounced = 0;     // Reset debounce flag
    }
    return 0;  // No valid press detected
}

//-----------------------------------------------------------------------------
// Main Routine
//-----------------------------------------------------------------------------
void main(void) {
    OSCILLATOR_Init();
    PORT_Init();
    Timer_Init();

    while (1) {
        if (ButtonPressed()) {
            if (TCON_TR0) {
                TCON_TR0 = 0;  // Stop Timer0
                CLK = 0;       // Reset clock state
                clk_index = 0;
            } else {
                Hold_Timer0(1000);  // Set initial reload value
                TCON_TR0 = 1;  // Start Timer0
            }
        }
    }
}


