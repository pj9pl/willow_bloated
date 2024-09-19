## Willow - cooperative multitasking for the ATmega328P

- Willow is an application framework based on message passing finite state
machines, known as tasks.

Here are two examples that demonstrate the technique:-

1. **Goat** is a high voltage parallel programmer for the ATmega328P.
   It features a zero insertion force socket that accepts an unmounted
   device, and has proved itself useful in reviving unresponsive devices.

2. **Zara** is a string of eight ATmega328P devices connected via the
   two wire interface:-

      - **bali** provides the primary HC-05 Bluetooth interface, a command line
        interpreter, in-system programming and in-circuit serial programming to
        the seven other devices.

      - **fido** provides a scripted keypad, utilities for managing an
        RV-3028-C7 real time clock, and a BMP180 barograph process director.

      - **iowa** provides a secondary HC-05 Bluetooth interface, in-circuit
        serial programming for *bali*, and file system utilities.

      - **lima** manages an I2C OLED screen and provides display services.

      - **oslo** manages an SDCard and provides a stateless file server.

      - **peru** manages an SPI OLED screen and provides display services.

      - **pisa** manages an MCP4728 Quad DAC and an AD7124 24-bit ADC and
        provides precision voltage generation and measurement.

      - **sumo** manages a 2 line 16 character LCD screen and provides
        display services.

- The **lib** directory contains tasks and headers that can be shared between
applications.

This is a work in progress, with this repository acting as a remote backup.
