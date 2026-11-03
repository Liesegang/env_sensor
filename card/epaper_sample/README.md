# Card Environment Sensor Display Firmware

XIAO ESP32C3 prototype for the Waveshare 3.6-inch Spectra 6 display.

Displays an environmental dashboard on the Waveshare 3.6-inch e-Paper HAT+
(E). The XIAO ESP32C3 generates 1440 simulated samples for temperature, humidity,
pressure, CO2, VOC, formaldehyde (HCHO), NOx, and illuminance (LUX) at one-minute
intervals for a 24-hour, 1440-sample history. It draws all eight logs overlaid on
one normalized graph, renders black/blue/green/yellow/red status badges inside
the current-value cards, and packs the complete six-color framebuffer at runtime.
The added traces use horizontal one-pixel color alternation for optical mixes:
VOC is yellow+red, HCHO red+blue, NOx blue+green, and LUX green+yellow.
No pre-rendered image or PSRAM is required.

## Wiring

| HAT+ | XIAO ESP32C3 | GPIO |
|---|---:|---:|
| VCC | 3V3 | - |
| GND | GND | - |
| DIN | D10 | 10 |
| CLK | D8 | 8 |
| CS | D1 | 3 |
| DC | D2 | 4 |
| RST | D3 | 5 |
| BUSY | D4 | 6 |
| PWR | D5 | 7 |

Use the HAT+ driver board and power it from 3.3 V. Do not connect the bare panel
directly, and do not put 5 V on a XIAO GPIO.

## Build and upload

```powershell
pio run
pio run --target upload
pio device monitor
```

The refresh takes roughly 15 seconds. After the update the firmware puts the
panel to sleep and drives `PWR` low; the image remains visible without power.
