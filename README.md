# OS3M-firmware-spoofork
This spoofing fork of the OS3M-firmware implements the HID descriptors and reporting that jfedor2 used in his [spaceball-2003 repo](https://github.com/jfedor2/spaceball-2003)
First install the 3Dconnexion drivers, and then this firmware should show up as a spacemouse

The firmware by Hexacube has here been extended with:
- Setting the multipliers to work with default default spacemouse driver gains
- Autozero-function if the sensor readings are nearly constant for some time. This handles plastic deformation and drift and can be configured or disabled via #defines AUTOZERO_MAX_TOTAL_CHANGE and LDC_HONE_PERIOD
- Averaging over a number of samples, as configured by the #define WIN_SIZE. (Default=5, set =1 to use raw values)
- Taking inputs from switches connected between GND and TP1-TP4 and reporting them as spacemouse buttons. 
  This can be configured/disabled via #defines TP1_BIT, TP2_BIT, TP3_BIT and TP4_BIT
  Default: TP1 = Fit View ; TP2 = Front View ; TP3 = Rotation (block) toggle; TP4 = CTRL
- Firmware binary is included in the bin folder

Also added is a Python tool for viewing the HID reports on the PC side

# Build
This fork builds and flashes with platformio, but the first time flash and configure as per the instructions in [HOW_TO_FLASH](./HOW_TO_FLASH.md) . 

Make sure to use a USB2.0 hub between the OS3M and your PC, otherwise the up/download may fail, leaving the memory corrupt and the board non-bootable. If it happens you must lift pin 1 of the STM32, program it and then re-solder pin 1.

With a working install of platformio, `pio run -t upload` should build and upload the firmware assuming the OS3M mouse is in DFU
