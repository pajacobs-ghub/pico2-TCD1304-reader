Introduction
------------

The care and feeding of the TCD1304DG linear image sensor is done in three parts.
A PIC18F16Q41 microcontroller drives the SH, ICG ande master clock pins of the 
sensor such that it outputs the pixel data as a time-varying analog voltage.
The RP2350 microcontroller on the Pico2 board samples that analog voltage and converts 
the pixel data into digital values, which are then sent (via the serial port) 
to a supervisory program that is running on a PC.

This repository holds the source code for the Pico2 firmware and a monitor program
written in Python3.
The firmware for the PIC18F16Q41 is in the repository:
https://github.com/pajacobs-ghub/pic18f16q41-TCD1304-driver


Interaction with the Pico2
--------------------------

The firmware on the Pico2 waits for an incoming command (as new-line terminated text),
interprets the command, takes action (if appropriate), and responds with text.
To interact with the Pico2 via a serial-port terminal program 
(such as GTKTerm on Linux),
set the port to /dev/ttyUSB0 (or whatever is suitable),
baud rate to 460800, and flow control to none.
Text commands are terminated with a new-line (Ctrl-J) character.

The commands are typically a single character, followed by the new-line character.
Responses are either a single line or many lines, as described below.


Commands
--------

The `v` command prompts the Pico2's firmware to report it's version string.
This can be handy for seeing if the microcontroller is running and responsive.

The `b` command tells the Pico2 to convert a batch of pixel voltages to numbers and store them locally in an array on the Pico2.
Once the Pico2 receives this command, it waits for a suitable point in time when the voltages from the pixels are about to be clocked out and then samples the output pin at 500k samples/s for a little under 8 milliseconds.
The overall time depends on just when the Pico2 starts looking at the clocking signals.
Since the PIC18F16Q41 driver MCU is running independently and continuously clocking the TCD1304 with SH, ICG and a 2MHz master clock, the Pico2 has to watch and wait for a rising edge of the ICG signal.
On completion of converting the voltages to numbers in the array, the Pico2 computes 
the mean, standard deviation and the time (in microseconds) to collect the data
and reports those stats as its (single-line) response. 

The `r` command tells the Pico2 to report the numbers (pixel data) 
that it has stored in that array.
The serial data transfer is the rate-limiting step and the report of 3800 numbers
as simple text takes about 500 milliseconds.
The general plan is that you first issue the 'b' command 
(to get a fresh batch of pixel data) and then the 'r' command.

The `q` command gets the Pico2 to report the pixel data in a more compact base-64
encoding.
This is significantly faster than the report for the `r` command but will require
decoding at the PC end.
The Python3 monitoring program shows how to do this.

The `p` command gets the Pico2 to talk to the PIC18F16Q41 MCU to adjust 
the SH and ICG clocking signals.
For example `p 300 8400` will set the SH and ICG periods to 300 microseconds 
and 8400 microseconds, respectively.
Note that the ICG period needs to be a multiple of the SH period 
for the pixel voltages to be output correctly.
The minimum value for SH is 10 microseconds and 
the minimum value of ICG should be around 8000 microseconds.
Maximum values are around 32000 for both because the PIC18 accepts the values
as 16-bit signed integers.


Licence
-------
GPL3


