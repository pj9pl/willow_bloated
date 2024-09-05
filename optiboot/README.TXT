This directory contains a modified version of the Optiboot small bootloader
for AVR microcontrollers, derived from:-
  arduino-1.8.10/hardware/arduino/avr/bootloaders/optiboot

This version has had most of the conditional directives removed as it is
targetted at an ATmega328P with a bootloader switch and without an LED.

Optiboot is more fully described here: http://code.google.com/p/optiboot/
and is the work of Peter Knight (aka Cathedrow), building on work of Jason P
Kyle, Spiff, and Ladyada.  Arduino-specific modification are by Bill
Westfield (aka WestfW)

Arduino-specific issues are tracked as part of the Arduino project
at http://code.google.com/p/arduino
