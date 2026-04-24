# UngulaHal

> **High-performance embedded C++ libraries for ESP32, STM32 and other MCUs** — hardware abstraction layer (GPIO, PWM, ADC, UART, I2C, SPI).

Hardware abstraction layer for embedded projects. Provides platform-portable GPIO, PWM, ADC, UART, I2C and SPI access.

On ESP32, GPIO uses `gpio_ll` direct register writes (single-cycle, ISR-safe). UART wraps `esp_idf` uart driver. On other platforms, no-op stubs are provided for compilation.

## Quick Start

In your Arduino `.ino` file:

```cpp
#include <ungula_hal.h>
```

Then include the component you need:

```cpp
#include <hal/gpio/gpio_access.h>
#include <hal/uart/uart.h>
```

## GPIO (`hal/gpio/gpio_access.h`)

Two API layers for digital I/O:

| Layer | Functions | Validation | Use case |
| --- | --- | --- | --- |
| **Unchecked** | `read()`, `setHigh()`, `setLow()`, `write()` | None | ISRs, timers, PID loops — pin validated at boot |
| **Checked** | `checkedRead()`, `checkedSetHigh()`, `checkedSetLow()`, `checkedWrite()` | Per-call | Pin from config or user input |

Config functions (`configOutput`, `configInput`, etc.) always validate and return `bool`.

```cpp
#include <hal/gpio/gpio_access.h>

using namespace ungula::gpio;

void setup() {
    configOutput(LED_PIN);
    configInputPullup(BUTTON_PIN);
    configPwm(FAN_PIN, 25000, 8);  // 25kHz, 8-bit resolution

    setHigh(LED_PIN);              // unchecked — pin validated above
}

void loop() {
    if (isLow(BUTTON_PIN)) {
        setHigh(LED_PIN);
    }
    writePwm(FAN_PIN, 128);        // 50% duty

    // Checked — pin from runtime config, might be invalid
    uint8_t userPin = getUserConfiguredPin();
    if (!checkedSetHigh(userPin)) {
        handleError();
    }
}
```

### Interrupts

```cpp
using ungula::gpio::InterruptEdge;
using ungula::gpio::PullMode;

// Button with internal pull-up, trigger on falling edge
configInputInterrupt(BUTTON_PIN, InterruptEdge::EDGE_FALLING, PullMode::UP);

// Open-drain signal (e.g. TMC2209 DIAG) with pull-up
configInputInterrupt(DIAG_PIN, InterruptEdge::EDGE_RISING, PullMode::UP);

// External pull — no internal resistor (default)
configInputInterrupt(SENSOR_PIN, InterruptEdge::EDGE_ANY);
```

`toggle()` is NOT atomic (read-then-write). If a pin is shared between ISR and task, the caller must synchronise.

## ADC (`hal/adc/adc_manager.h`)

Owning class — one instance holds all unit and calibration handles for the ADC peripheral. Calibration is tracked per `(unit, attenuation)` pair, so mixing attenuations on the same unit works correctly (each channel gets its own cali handle). Configure at boot, then read from anywhere.

```cpp
#include <hal/adc/adc_manager.h>

using ungula::adc::AdcManager;
using ungula::adc::Attenuation;

AdcManager adc;

void setup() {
    adc.configure(34);                       // default 12 dB → 0–3.3 V
    adc.configure(35, Attenuation::DB_6);    // 0–1.75 V, better precision near 0
}

void loop() {
    uint32_t mv = 0;
    if (adc.readMv(34, mv)) {
        // mv = calibrated millivolts (or attenuation-aware fallback if no cali)
    }

    int raw = 0;
    adc.readRaw(35, raw);                    // 0..4095, diagnostic path
}
```

### API

| Method | Description |
| --- | --- |
| `configure(pin, atten)` | Bind a GPIO to a channel. Single-assignment per pin. Succeeds even when calibration can't be created (fallback path is used). |
| `readMv(pin, mv)` | Calibrated millivolts when eFuse calibration is available, attenuation-aware linear fallback otherwise (5–10 % error). |
| `readRaw(pin, raw)` | Raw 12-bit ADC count, no conversion. |
| `deinit()` | Release unit + calibration handles. Idempotent. The destructor calls this. |

### Notes

- **Capacity**: `AdcManager::MAX_CHANNELS` = 16 by default. Override via `-DUNGULA_HAL_MAX_ADC_CHANNELS=N` in your build flags.
- **Thread safety**: not thread-safe for `configure()`. Configure at boot from a single context. Concurrent `readMv()`/`readRaw()` on already-configured channels is fine.
- **ADC2 + Wi-Fi**: on classic ESP32, ADC2 channels conflict with the Wi-Fi PHY and may fail while the radio is active. Prefer ADC1 (GPIO 32–39) when Wi-Fi is in use.
- **No lazy singleton**: create one `AdcManager` (global, member of a higher-level class, whatever fits) and share it. ESP-IDF treats the ADC unit as a singleton, so instantiating a second `AdcManager` and calling `configure()` on overlapping pins is a programming error — no runtime guard.

## UART (`hal/uart/uart.h`)

One-owner-per-port UART wrapper. Non-copyable.

```cpp
#include <hal/uart/uart.h>

ungula::uart::Uart uart(2);  // ESP32 UART port 2

void setup() {
    uart.begin(115200, PIN_TX, PIN_RX);
}

void sendCommand(const uint8_t* cmd, size_t len) {
    uart.write(cmd, len);
    uart.flush();  // wait for TX to complete
}

int32_t receiveResponse(uint8_t* buf, size_t maxLen) {
    return uart.read(buf, maxLen, 100);  // 100ms timeout
}
```

### API

| Method | Description |
| --- | --- |
| `begin(baud, txPin, rxPin)` | Install driver and configure pins. Optional rxBufSize, txBufSize. |
| `write(data, length)` | Blocking transmit. Returns bytes written or -1. |
| `read(buf, maxLen, timeoutMs)` | Read with timeout. Returns bytes read or -1. |
| `flush()` | Block until all TX data is physically sent. |
| `flushInput()` | Discard all pending RX data. |

## I2C Master (`hal/i2c/i2c_master.h`)

One-owner-per-port I2C master wrapper. Non-copyable.

On ESP32, uses the legacy `driver/i2c.h` command-link API (compatible with ESP-IDF 5.1+). On desktop, a no-op stub returns success for all operations.

```cpp
#include <hal/i2c/i2c_master.h>

ungula::i2c::I2cMaster bus(0);  // I2C port 0

void setup() {
    bus.begin(21, 22, 400000);  // SDA=21, SCL=22, 400kHz
}

void readRegister(uint8_t addr, uint8_t reg, uint8_t* buf, size_t len) {
    bus.writeRead(addr, &reg, 1, buf, len);  // repeated START
}
```

### API

| Method | Description |
| --- | --- |
| `begin(sdaPin, sclPin, freqHz)` | Install driver, configure pins and clock speed. |
| `write(addr, data, length, timeoutMs)` | Write bytes to a 7-bit address. Returns true on ACK. |
| `read(addr, buffer, length, timeoutMs)` | Read bytes from a 7-bit address. |
| `writeRead(addr, writeData, writeLen, readBuf, readLen, timeoutMs)` | Combined write-then-read with repeated START. |
| `port()` | Hardware peripheral index passed at construction. |

## SPI Master (`hal/spi/spi_master.h`)

Single-device SPI master wrapper. One `SpiMaster` = one CS line. For multiple devices on the same bus, create one instance per device sharing the same host but with different CS pins.

On ESP32, uses `driver/spi_master.h` with polling transmit. On desktop, a stub that zeros receive buffers.

```cpp
#include <hal/spi/spi_master.h>

ungula::spi::SpiMaster spi;

void setup() {
    spi.begin(18, 19, 23, 5, 1000000, 1);  // SCLK, MISO, MOSI, CS, 1MHz, mode 1
}

void readAdc(uint8_t* result, size_t len) {
    uint8_t cmd = 0x10;  // RDATA command
    spi.writeRead(&cmd, 1, result, len);  // write cmd, then read result
}
```

### API

| Method | Description |
| --- | --- |
| `begin(sclk, miso, mosi, cs, freqHz, mode, host)` | Initialize bus and add device. Mode 0-3, host 1=SPI2, 2=SPI3. |
| `transfer(txData, rxData, length)` | Full-duplex transfer. Either buffer may be nullptr. |
| `write(data, length)` | Write-only transfer. |
| `read(buffer, length)` | Read-only transfer (sends zeros). |
| `writeRead(txData, writeLen, rxBuf, readLen)` | Write followed by read in a single CS assertion. |

## Structure

```text
lib_hal/
  src/
    ungula_hal.h              # chain header for Arduino discovery
    hal/
      gpio/
        gpio_access.h             # bridge header (dispatches to platform)
        platforms/
          gpio_access_esp32.h     # ESP32: gpio_ll direct register access
          gpio_access_default.h   # no-op stubs for desktop builds
          gpio_pwm_esp32.cpp      # LEDC PWM implementation
      adc/
        adc_manager.h             # bridge header (dispatches to platform)
        platforms/
          adc_manager_esp32.h     # ESP32 class definition
          adc_manager_esp32.cpp   # oneshot + per-(unit,atten) calibration
          adc_manager_default.h   # host-side stub with same surface
      i2c/
        i2c_master.h              # platform-portable I2C master interface
        platforms/
          i2c_master_esp32.cpp    # ESP-IDF legacy i2c driver wrapper
          i2c_master_default.cpp  # no-op stubs for desktop builds
      spi/
        spi_master.h              # platform-portable SPI master interface
        platforms/
          spi_master_esp32.cpp    # ESP-IDF spi_master wrapper
          spi_master_default.cpp  # no-op stubs for desktop builds
      uart/
        uart.h                    # platform-portable UART interface
        platforms/
          uart_esp32.cpp          # ESP-IDF uart driver wrapper
          uart_default.cpp        # no-op stubs for desktop builds
  tests/
    test_i2c_master.cpp           # I2C stub tests (GoogleTest)
    test_spi_master.cpp           # SPI stub tests
    test_uart.cpp                 # UART stub tests
    test_adc_manager.cpp          # AdcManager stub tests
```

## Testing

Host unit tests verify the desktop stubs compile and behave correctly (begin guards, pre-begin failures, buffer zeroing).

```bash
cd tests
./1_build.sh
./2_run.sh
```

## Acknowledgements

Thanks to Claude and ChatGPT for helping on generating this documentation.

## License

MIT License — see [LICENSE](LICENSE) file.
