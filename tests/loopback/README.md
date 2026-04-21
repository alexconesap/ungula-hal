# HAL Loopback Tests

Simple Arduino sketches to verify HAL implementations on a bare ESP32 with no external peripherals. Each test prints PASS/FAIL to the serial monitor at 115200 baud.

## UART Loopback (`uart_loopback/`)

**Wiring:** GPIO 17 (TX2) → GPIO 16 (RX2) with a jumper wire.

Sends a 5-byte pattern through UART2 and reads it back. Verifies byte-for-byte match.

## SPI Loopback (`spi_loopback/`)

**Wiring:** GPIO 23 (MOSI) → GPIO 19 (MISO) with a jumper wire.

Sends a 4-byte pattern via full-duplex transfer. Since MOSI feeds directly into MISO, the received data should match the transmitted data exactly.

## I2C Bus Scan (`i2c_scan/`)

**Wiring:** none required (internal pull-ups enabled).

Initialises the I2C bus and probes all 112 valid 7-bit addresses (0x08–0x77). With nothing connected, reports 0 devices — that confirms the bus started correctly without hanging. If you connect a device, it shows up in the scan.
