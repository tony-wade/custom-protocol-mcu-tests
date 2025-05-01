# -----------------------------------------------------------------------------
# Usage Instructions
# -----------------------------------------------------------------------------
# 1. Install required libraries:
#    $ sudo apt install libgpiod2 python3-libgpiod
#
# 2. Write and modify the Python script as needed.
#
# 3. Run the script:
#    $ sudo python3 <file_name>.py

# p.s. pigpio lib is not support Pi5 since DMA method on RP1 is different

# -----------------------------------------------------------------------------
# result:  ~8.36 kHz
# -----------------------------------------------------------------------------
# - `time.sleep()` lacks microsecond precision; it is at best millisecond-level.
# - Raspberry Pi is not a real-time system, so OS task scheduling affects timing.
# - The delays are much longer than expected due to OS overhead.
 

import time
import gpiod

# -----------------------------------------------------------------------------
# Setup GPIO (Using libgpiod)
# -----------------------------------------------------------------------------
# 1. Open GPIO chip0 (Raspberry Pi GPIO controller)
chip = gpiod.Chip('gpiochip0')

# 2. Select GPIO pin 17
line = chip.get_line(17)

# 3. Request control of the pin as an output
line.request(consumer='clk_gen', type=gpiod.LINE_REQ_DIR_OUT)

# -----------------------------------------------------------------------------
# Clock Generation Parameters
# -----------------------------------------------------------------------------
# Target frequency: 1.33 MHz → Period = 0.75 µs
period = 0.75e-6  # 750 ns (in seconds)

# -----------------------------------------------------------------------------
# Generate Clock Signal
# -----------------------------------------------------------------------------
try:
    print('Clock On!')
    while True:
        line.set_value(1)  # Set GPIO HIGH
        time.sleep(period / 2)  # Delay for half the period (375 ns)
        line.set_value(0)  # Set GPIO LOW
        time.sleep(period / 2)  # Delay for the other half

except KeyboardInterrupt:
    print("Clock generation stopped.")

finally:
    # Ensure the pin is set LOW before exiting
    line.set_value(0)
    chip.close() 