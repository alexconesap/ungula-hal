# UngulaHal

> **High-performance embedded C++ libraries for ESP32, STM32 and other MCUs** — hardware abstraction layer (GPIO, PWM, ADC, UART, I2C, SPI).

Hardware abstraction layer for embedded projects. Provides platform-portable GPIO, PWM, ADC, UART, I2C and SPI access.

On ESP32, GPIO uses `gpio_ll` direct register writes (single-cycle, ISR-safe). UART wraps `esp_idf` uart driver. On other platforms, no-op stubs are provided for compilation.

## Quick Start

In your Arduino `.ino` file:

```cpp
#include <ungula/hal.h>
```

Then include the component you need:

```cpp
#include <ungula/hal/gpio/gpio_access.h>
#include <ungula/hal/uart/uart.h>
```

## GPIO (`ungula/hal/gpio/gpio_access.h`)

Two API layers for digital I/O:

| Layer | Functions | Validation | Use case |
| --- | --- | --- | --- |
| **Unchecked** | `read()`, `setHigh()`, `setLow()`, `write()` | None | ISRs, timers, PID loops — pin validated at boot |
| **Checked** | `checkedRead()`, `checkedSetHigh()`, `checkedSetLow()`, `checkedWrite()` | Per-call | Pin from config or user input |

Config functions (`configOutput`, `configInput`, etc.) always validate and return `bool`.

### Checked vs Uncheked

Check functions are slower than unchecked — they do a GPIO validity check on every call, so
they are not suitable for hot paths. Use them in setup(), command handlers, or
anywhere you want to be defensive against invalid pins but don't want to pay the cost of
validation on every read/write.

### Read helpers

Six inline wrappers over `read()` and `!read()` exist for readability. They carry no validation and are in the unchecked hot-path group.

| Helper | Equivalent | Typical use |
| --- | --- | --- |
| `isHigh(pin)` | `read(pin)` | Logic-level HIGH — signal lines, step outputs |
| `isLow(pin)` | `!read(pin)` | Logic-level LOW |
| `isEnabled(pin)` | `read(pin)` | Active-high enable signals |
| `isDisabled(pin)` | `!read(pin)` | Active-high enable signals, inverted sense |
| `isOpen(pin)` | `!read(pin)` | NC switch / relay — open = not triggered |
| `isClosed(pin)` | `read(pin)` | NC switch / relay — closed = triggered |

Use these only when the compiler can inline the call (it will on all supported toolchains with optimisation enabled). At `-O0` they add a function-call round-trip with no other benefit over calling `read()` directly.

```cpp
#include <ungula/hal/gpio/gpio_access.h>

using namespace ungula::hal::gpio;

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
#include <ungula/hal.h>

using ungula::hal::gpio::InterruptEdge;
using ungula::hal::gpio::PullMode;

// Button with internal pull-up, trigger on falling edge
configInputInterrupt(BUTTON_PIN, InterruptEdge::EDGE_FALLING, PullMode::UP);

// Open-drain signal (e.g. TMC2209 DIAG) with pull-up
configInputInterrupt(DIAG_PIN, InterruptEdge::EDGE_RISING, PullMode::UP);

// External pull — no internal resistor (default)
configInputInterrupt(SENSOR_PIN, InterruptEdge::EDGE_ANY);
```

`toggle()` is NOT atomic (read-then-write). If a pin is shared between ISR and task, the caller must synchronise.

## ADC (`ungula/hal/adc/adc_manager.h`)

Owning class — one instance holds all unit and calibration handles for the ADC peripheral. Calibration is tracked per `(unit, attenuation)` pair, so mixing attenuations on the same unit works correctly (each channel gets its own cali handle). Configure at boot, then read from anywhere.

```cpp
#include <ungula/hal/adc/adc_manager.h>

using ungula::hal::adc::AdcManager;
using ungula::hal::adc::Attenuation;

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

### ADC API

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

## UART (`ungula/hal/uart/uart.h`)

One-owner-per-port UART wrapper. Non-copyable.

```cpp
#include <ungula/hal/uart/uart.h>

ungula::hal::uart::Uart uart(2);  // ESP32 UART port 2

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

### UART API

| Method | Description |
| --- | --- |
| `begin(baud, txPin, rxPin)` | Install driver and configure pins. Optional rxBufSize, txBufSize. |
| `write(data, length)` | Blocking transmit. Returns bytes written or -1. |
| `read(buf, maxLen, timeoutMs)` | Read with timeout. Returns bytes read or -1. |
| `flush()` | Block until all TX data is physically sent. |
| `flushInput()` | Discard all pending RX data. |

## I2C Master (`ungula/hal/i2c/i2c_master.h`)

One-owner-per-port I2C master wrapper. Non-copyable.

On ESP32, uses the legacy `driver/i2c.h` command-link API (compatible with ESP-IDF 5.1+). On desktop, a no-op stub returns success for all operations.

```cpp
#include <ungula/hal/i2c/i2c_master.h>

ungula::hal::i2c::I2cMaster bus(0);  // I2C port 0

void setup() {
    bus.begin(21, 22, 400000);  // SDA=21, SCL=22, 400kHz
}

void readRegister(uint8_t addr, uint8_t reg, uint8_t* buf, size_t len) {
    bus.writeRead(addr, &reg, 1, buf, len);  // repeated START
}
```

### I2C API

| Method | Description |
| --- | --- |
| `begin(sdaPin, sclPin, freqHz)` | Install driver, configure pins and clock speed. |
| `write(addr, data, length, timeoutMs)` | Write bytes to a 7-bit address. Returns true on ACK. |
| `read(addr, buffer, length, timeoutMs)` | Read bytes from a 7-bit address. |
| `writeRead(addr, writeData, writeLen, readBuf, readLen, timeoutMs)` | Combined write-then-read with repeated START. |
| `port()` | Hardware peripheral index passed at construction. |

## SPI Master (`ungula/hal/spi/spi_master.h`)

Single-device SPI master wrapper. One `SpiMaster` = one CS line. For multiple devices on the same bus, create one instance per device sharing the same host but with different CS pins.

On ESP32, uses `driver/spi_master.h` with polling transmit. On desktop, a stub that zeros receive buffers.

```cpp
#include <ungula/hal/spi/spi_master.h>

ungula::hal::spi::SpiMaster spi;

void setup() {
    spi.begin(18, 19, 23, 5, 1000000, 1);  // SCLK, MISO, MOSI, CS, 1MHz, mode 1
}

void readAdc(uint8_t* result, size_t len) {
    uint8_t cmd = 0x10;  // RDATA command
    spi.writeRead(&cmd, 1, result, len);  // write cmd, then read result
}
```

### SPI API

| Method | Description |
| --- | --- |
| `begin(sclk, miso, mosi, cs, freqHz, mode, host)` | Initialize bus and add device. Mode 0-3, host 1=SPI2, 2=SPI3. |
| `transfer(txData, rxData, length)` | Full-duplex transfer. Either buffer may be nullptr. |
| `write(data, length)` | Write-only transfer. |
| `read(buffer, length)` | Read-only transfer (sends zeros). |
| `writeRead(txData, writeLen, rxBuf, readLen)` | Write followed by read in a single CS assertion. |

## CAN bus (`ungula/hal/can/can.h`)

CAN 2.0 controller wrapper. On ESP32 it sits on top of ESP-IDF's TWAI driver (`driver/twai.h`); on the host build it's a no-op stub so consumers compile and unit-test off-device.

The wrapper is for the **bus** itself. Higher-level protocols — servo motor command sets, OBD-II, J1939 — live in their own libraries that consume `ungula::hal::can::Can`.

### Real-world use case — talk to a CAN servo motor

```cpp
#include <ungula/hal/can/can.h>

namespace can = ungula::hal::can;

can::Can bus(/*controller=*/0);

void setup() {
    bus.begin(/*tx=*/21, /*rx=*/22, can::BITRATE_1M);
    bus.setAcceptanceFilter(/*id=*/0x141, /*mask=*/0x7FF, /*extendedId=*/false);
}

void sendVelocity(int32_t rpm) {
    can::CanFrame f{};
    f.id = 0x140;          // motor address
    f.dlc = 8;
    f.data[0] = 0xA2;      // "speed control" command
    // ...pack rpm into bytes 4..7...
    bus.send(f, /*timeoutMs=*/10);
}

void poll() {
    can::CanFrame in{};
    if (bus.receive(in, /*timeoutMs=*/0) == 1) {
        // parse motor reply
    }
}
```

### Real-world use case — recover from bus-off after a wiring fault

```cpp
#include <ungula/hal.h>

if (bus.isBusOff()) {
    bus.recoverFromBusOff();   // controller resumes after a brief pause
}
```

### CAN API

| Method | Description |
| --- | --- |
| `begin(txPin, rxPin, bitrateBps)` | Install + start the controller at one of the `BITRATE_*` presets. |
| `stop()` | Stop and uninstall the driver. Idempotent. |
| `send(frame, timeoutMs)` | Transmit one frame. Returns false on timeout / error. |
| `receive(out, timeoutMs)` | Receive one frame. Returns 1 on success, 0 on timeout, -1 on error. |
| `setAcceptanceFilter(id, mask, extendedId)` | Single hardware filter; transparent stop+reinstall under the hood. |
| `clearAcceptanceFilter()` | Accept-all (default). |
| `isBusOff()` / `recoverFromBusOff()` | Bus-off detection + recovery. |
| `controller()` | Hardware controller index. |

Bitrate constants exposed in the same namespace: `BITRATE_25K` … `BITRATE_1M`. CAN-FD is not supported — separate class when the need arises.

## I2C multiplexer (`ungula/hal/multiplexer/i_multiplexer.h`)

Driver-agnostic interface for I2C bus multiplexers. The host project owns the underlying `I2cMaster`, the multiplexer driver borrows it, and downstream code calls `selectChannel(n)` before talking to a device wired to channel `n`.

The base class caches the active channel — repeated `selectChannel(n)` for the same `n` is a no-op (no I2C traffic) when the device is healthy. Real-world: a horizontal + vertical AS5600 encoder pair share a TCA9548A; the position read loop calls `selectChannel(0)` then reads several registers — only the first call hits the wire.

### Real-world use case — single TCA9548 with two encoders

```cpp
#include <ungula/hal/i2c/i2c_master.h>
#include <ungula/hal/multiplexer/drivers/multiplexer_tca9548.h>

namespace mux = ungula::hal::multiplexer;

ungula::hal::i2c::I2cMaster bus(0);
mux::drivers::MultiplexerTCA9548 mux70(0x70, bus);

void setup() {
    bus.begin(21, 22, 400000);
    if (!mux70.begin()) {
        // I2C up but the chip is missing — handle accordingly
        return;
    }
    mux70.enableLogging();  // route base-class diagnostics through EmblogX
}

void readEncoders() {
    if (mux70.selectChannel(0)) { /* read the horizontal encoder */ }
    if (mux70.selectChannel(1)) { /* read the vertical encoder   */ }
}
```

### Real-world use case — two multiplexers on the same bus

Two TCA9548s wired to the same I2C bus with strapping resistors `0x70` and `0x71`. Both share one `I2cMaster`; each driver only knows its own address.

```cpp
#include <ungula/hal.h>

ungula::hal::i2c::I2cMaster bus(0);
mux::drivers::MultiplexerTCA9548 muxA(0x70, bus);
mux::drivers::MultiplexerTCA9548 muxB(0x71, bus);

void setup() {
    bus.begin(21, 22, 400000);
    muxA.begin();
    muxB.begin();
}

void readSensors() {
    muxA.selectChannel(3);  // talk to A:3 device
    // ... I2C transactions to A:3 ...
    muxB.selectChannel(0);  // switch the other multiplexer; A:3 stays selected
    // ... I2C transactions to B:0 ...
}
```

### Multiplexer API

| Method | Description |
| --- | --- |
| `begin()` | Probe the chip and mark it ready. Returns false when not responding. |
| `selectChannel(channel)` | Cached + retried channel select. Returns false after `SELECT_RETRY_COUNT` failures. |
| `getCurrentChannel()` | Last channel successfully selected; `0xFF` until first `selectChannel`. |
| `restartBus()` | Forget cached state so the next `selectChannel` re-hits the wire. |
| `isResponding()` | Probe the chip without changing channel state. |
| `enableLogging()` / `disableLogging()` / `isLoggingEnabled()` | Toggle EmblogX diagnostics on this driver (`module="mux"`). |

Drivers shipped:

- `drivers/multiplexer_tca9548.h` — TCA9548A 8-channel I2C multiplexer.
- `drivers/multiplexer_fake.h` — header-only fake for host tests (counters, scriptable failures).

### Multiplexer logging

Off by default. When enabled, every line goes through EmblogX with the module tag `mux`, so host filters can route them like any other category. Failures (init, retry exhausted) hit `log_error_m` regardless of level; channel-select traffic is `log_debug_m`. Logging is per-instance, not global — enable it on the chip you want to debug and leave the rest quiet.

## Structure

```text
lib_hal/
  src/
    ungula/
      hal.h                       # chain header for Arduino discovery
      hal/
        gpio/
          gpio_access.h           # bridge header (dispatches to platform)
          gpio_types.h
          platforms/
            gpio_access_esp32.h   # ESP32: gpio_ll direct register access
            gpio_access_default.h # no-op stubs for desktop builds
            gpio_pwm_esp32.cpp    # LEDC PWM implementation
        adc/
          adc_manager.h           # bridge header (dispatches to platform)
          platforms/
            adc_manager_esp32.h   # ESP32 class definition
            adc_manager_esp32.cpp # oneshot + per-(unit,atten) calibration
            adc_manager_default.h # host-side stub with same surface
        i2c/
          i2c_master.h            # platform-portable I2C master interface
          platforms/
            i2c_master_esp32.cpp  # ESP-IDF legacy i2c driver wrapper
            i2c_master_default.cpp# no-op stubs for desktop builds
        spi/
          spi_master.h            # platform-portable SPI master interface
          platforms/
            spi_master_esp32.cpp  # ESP-IDF spi_master wrapper
            spi_master_default.cpp# no-op stubs for desktop builds
        uart/
          uart.h                  # platform-portable UART interface
          platforms/
            uart_esp32.cpp        # ESP-IDF uart driver wrapper
            uart_default.cpp      # no-op stubs for desktop builds
        can/
          can.h                   # platform-portable CAN 2.0 interface
          platforms/
            can_esp32.cpp         # ESP-IDF TWAI driver wrapper
            can_default.cpp       # no-op stubs for desktop builds
        multiplexer/
          i_multiplexer.h         # interface + cached selectChannel
          i_multiplexer.cpp       # base behaviour (cache, retry, logging)
          drivers/
            multiplexer_tca9548.h # TCA9548A driver
            multiplexer_tca9548.cpp
            multiplexer_fake.h    # header-only fake for tests
  tests/
    test_gpio_access.cpp          # GPIO stub tests (GoogleTest)
    test_i2c_master.cpp           # I2C stub tests
    test_spi_master.cpp           # SPI stub tests
    test_uart.cpp                 # UART stub tests
    test_adc_manager.cpp          # AdcManager stub tests
    test_can.cpp                  # CAN stub tests
    test_multiplexer.cpp          # IMultiplexer base + fake driver tests
```

## Testing

Host unit tests verify the desktop stubs compile and behave correctly (begin guards, pre-begin failures, buffer zeroing).

```bash
cd tests
./1_build.sh
./2_run.sh
```

## Acknowledgements

- Thanks to Claude and ChatGPT for helping on generating this documentation and the tests.

## License

MIT License — see [LICENSE](LICENSE) file.

---

## Arduino CLI symlink note (rarely relevant)

This library ships a flat forwarder header at `src/ungula_hal.h` that
just `#include`s `ungula/hal.h`. `library.properties` `includes=` points
at the forwarder.

It only exists to work around an Arduino CLI quirk: when the library is
consumed through a symlink, the CLI sometimes fails to discover headers
nested under `src/ungula/`. The flat forwarder fixes that scan.

**Host code keeps including the real header**:

```cpp
#include <ungula/hal.h>
```

PlatformIO, ESP-IDF component builds, and plain CMake setups can ignore
the forwarder.
