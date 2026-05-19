# UngulaHal

> **High-performance embedded C++ libraries for ESP32, STM32 and other MCUs** — hardware abstraction layer (GPIO, PWM, ADC, UART, I2C, SPI, CAN, multiplexer, PWM input capture, quadrature decoder, hardware timer, critical section).

> **LLM usage note:** if this library is consumed from a coding AI workflow, explicitly point the agent to `API.md` first. `API.md` is the LLM-facing contract (public API + examples + constraints) and avoids wasting time/tokens scanning source files and this human-oriented README.

> **Warning - Active Development:** This library is under active architecture work to support multiple projects in parallel. Its structure is not finalized yet and may change without notice while this work is in progress. Updates are currently frequent (often daily). Target for structural freeze and stable `v1.0.0`: **June 2026**.

Hardware abstraction layer for embedded projects. Provides platform-portable GPIO, PWM, ADC, UART, I2C, SPI, CAN 2.0, an I2C bus multiplexer interface, single-pin PWM input capture, quadrature (A/B) decoders, deterministic hardware timers, and ISR-safe critical sections.

Each peripheral ships as a stable C++ interface in the `ungula::hal::` namespace, a concrete platform-dispatched driver, and (where appropriate) a header-only fake for host tests. Higher-level libraries (`lib_loadcell`, `lib_motor`, `lib_sd`, `lib_encoder`) compile against the abstractions, not the vendor SDKs.

On ESP32, GPIO uses `gpio_ll` direct register writes (single-cycle, ISR-safe); UART, I2C, SPI and CAN wrap the matching ESP-IDF drivers; PWM input capture uses GPIO-edge ISR + `esp_timer`; the quadrature decoder uses the PCNT (pulse counter) peripheral with hardware 4× decoding. On other platforms (host builds), no-op stubs are provided so consumer code compiles and links for unit tests.

## Table of Contents

- [C++ Compatibility](#c-compatibility)
- [Platform support](#platform-support)
- [Quick Start](#quick-start)
- [GPIO (`ungula/hal/gpio/gpio.h`)](#gpio-ungulahalgpiogpioh)
  - [Checked vs Uncheked](#checked-vs-uncheked)
  - [Read helpers](#read-helpers)
  - [Interrupts](#interrupts)
- [ADC (`ungula/hal/adc/adc_manager.h`)](#adc-ungulahaladcadcmanagerh)
  - [ADC API](#adc-api)
  - [Notes](#notes)
- [UART (`ungula/hal/uart/uart.h`)](#uart-ungulahaluartuarth)
  - [UART API](#uart-api)
- [I2C Master (`ungula/hal/i2c/i2c_master.h`)](#i2c-master-ungulahali2ci2cmasterh)
  - [I2C API](#i2c-api)
- [SPI Master (`ungula/hal/spi/spi_master.h`)](#spi-master-ungulahalspispimasterh)
  - [SPI API](#spi-api)
- [CAN bus (`ungula/hal/can/can.h`)](#can-bus-ungulahalcancanh)
  - [Real-world use case — talk to a CAN servo motor](#real-world-use-case-talk-to-a-can-servo-motor)
  - [Real-world use case — recover from bus-off after a wiring fault](#real-world-use-case-recover-from-bus-off-after-a-wiring-fault)
  - [CAN API](#can-api)
- [Hardware timer (`ungula/hal/timer/drivers/hwtimer.h`)](#hardware-timer-ungulahaltimerdrivershwtimerh)
  - [Real-world use case — variable-period ISR pulse engine](#real-world-use-case-variable-period-isr-pulse-engine)
  - [Timer API](#timer-api)
- [Critical section (`ungula/hal/sync/critical_section.h`)](#critical-section-ungulahalsynccriticalsectionh)
  - [Real-world use case — ISR/task shared counters](#real-world-use-case-isrtask-shared-counters)
  - [Critical section API](#critical-section-api)
- [I2C multiplexer (`ungula/hal/multiplexer/i_multiplexer.h`)](#i2c-multiplexer-ungulahalmultiplexerimultiplexerh)
  - [Real-world use case — single TCA9548 with two encoders](#real-world-use-case-single-tca9548-with-two-encoders)
  - [Real-world use case — two multiplexers on the same bus](#real-world-use-case-two-multiplexers-on-the-same-bus)
  - [Multiplexer API](#multiplexer-api)
  - [Multiplexer logging](#multiplexer-logging)
- [PWM input capture (`ungula/hal/pwm_input/i_pwm_input.h`)](#pwm-input-capture-ungulahalpwminputipwminputh)
  - [Real-world use case — read an AS5600 in PWM mode](#real-world-use-case-read-an-as5600-in-pwm-mode)
  - [Real-world use case — per-frame ISR callback (no polling)](#real-world-use-case-per-frame-isr-callback-no-polling)
  - [PWM input API](#pwm-input-api)
- [Quadrature decoder (`ungula/hal/quadrature/i_decoder.h`)](#quadrature-decoder-ungulahalquadratureidecoderh)
  - [Real-world use case — angle from an MT6701 in ABI mode](#real-world-use-case-angle-from-an-mt6701-in-abi-mode)
  - [Real-world use case — homing to a known reference](#real-world-use-case-homing-to-a-known-reference)
  - [Quadrature decoder API](#quadrature-decoder-api)
- [Structure](#structure)
- [Testing](#testing)
- [Acknowledgements](#acknowledgements)
- [License](#license)
- [Arduino CLI symlink note (rarely relevant)](#arduino-cli-symlink-note-rarely-relevant)

## C++ Compatibility

- **Own source minimum**: `C++17`.
- **Effective minimum for consumers**: `C++17`.
- **Dependency impact**: None (no declared internal dependencies).

## Platform support

Currently supported production backend:

- ESP32

Test-only backend:

- default/fake backend for host tests

The default backend is not suitable for production MCU targets. New MCU targets must provide platform-specific implementations for timer and synchronization primitives before being used in production.

## Quick Start

In your Arduino `.ino` file:

```cpp
#include <ungula/hal.h>
```

Then include the component you need:

```cpp
#include <ungula/hal/gpio/gpio.h>
#include <ungula/hal/uart/uart.h>
```

## GPIO (`ungula/hal/gpio/gpio.h`)

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
#include <ungula/hal/gpio/gpio.h>

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

## Hardware timer (`ungula/hal/timer/drivers/hwtimer.h`)

`IHwTimer` / `HwTimer` is the low-level pulse timer for deterministic ISR work. The contract is **true one-shot**: every successful `startOneShotTicks()` fires the callback exactly once. To produce a continuous (and possibly variable-period) pulse train, the callback calls `rearmFromIsr()` from inside itself. At the last pulse, the callback calls `disarmFromIsr()` so no further alarm can fire.

### Real-world use case — variable-period ISR pulse engine

```cpp
#include <ungula/hal/gpio/gpio.h>
#include <ungula/hal/timer/drivers/hwtimer.h>

namespace gpio = ungula::hal::gpio;
namespace timer = ungula::hal::timer;

constexpr uint8_t STEP_PIN = 18;
constexpr uint32_t TIMER_HZ = 1000000;    // 1 tick = 1 us
constexpr uint32_t STEP_TICKS = 500;        // 500 us half-period

timer::drivers::HwTimer pulseTimer;
volatile uint32_t pulseEdges = 0;           // word-atomic on Xtensa

void UNGULA_ISR_ATTR onPulse(void* /*ctx*/) {
    // No RAII, no lock. The pulse ISR holds no state of its own — it
    // just toggles the pin and re-arms.
    gpio::toggle(STEP_PIN);
    pulseEdges++;
    pulseTimer.rearmFromIsr(STEP_TICKS);
}

void setup() {
    gpio::configOutput(STEP_PIN);
    gpio::setLow(STEP_PIN);

    timer::HwTimerConfig cfg;
    cfg.resolutionHz = TIMER_HZ;
    cfg.minTicks     = 5;

    pulseTimer.begin(cfg);
    pulseTimer.setCallback(&onPulse, nullptr);
    pulseTimer.startOneShotTicks(STEP_TICKS);
}

void loop() {
    // `pulseEdges` is a 32-bit volatile, safe to sample without a lock.
    const uint32_t snapshot = pulseEdges;
    (void)snapshot;
}
```

### Timer API

| Method | Description |
| --- | --- |
| `begin(cfg)` | Allocate/configure timer. `InvalidConfig` for `resolutionHz=0`, `AlreadyInitialized` if called twice. |
| `setCallback(cb, ctx)` | Register ISR callback. `NotInitialized` if called before `begin`. |
| `startOneShotTicks(ticks)` | Task-context arm. Counter resets to 0; alarm fires once at `ticks`. `InvalidTicks` if `ticks < cfg.minTicks`. |
| `rearmFromIsr(ticks)` | ISR-safe re-arm. Schedules next alarm `ticks` ticks after the alarm that just fired (no counter read — `lastAlarmCount_` is cached from the alarm event data). |
| `disarmFromIsr()` | ISR-safe disarm. Software gate at the trampoline drops any further user-callback invocation; does NOT call `gptimer_stop()` from ISR. |
| `stop()` | Task-context full halt: stops the hardware counter AND clears the armed flag. |
| `resolutionHz()` | Returns configured tick rate (0 before `begin`). |
| `isArmed()` | True between any arm/rearm and the next fire/disarm. Backed by `std::atomic<bool>` for dual-core safety. |

Important edge case: internal state now tracks **`started_` separately from `armed_`**. On ESP32 GPTimer, one-shot alarm consumption clears `armed_` but the hardware timer can still be running. `startOneShotTicks()` uses `started_` to decide whether it must call `gptimer_stop()` before re-starting, preventing `ESP_ERR_INVALID_STATE` on restart-after-fire flows.

All non-const methods return `HwTimerStatus`; `Ok` is success. `drivers/hwtimer_fake.h` ships a header-only fake for host tests that honours the same one-shot semantics — `fire()` on a disarmed fake is silently dropped.

ISR-rearm path on ESP32 was rewritten in 1.5.7 to call the lockless `timer_ll_set_alarm_value` / `timer_ll_enable_alarm` register primitives directly. The previous path went through `gptimer_set_alarm_action`, which takes the gptimer driver's portMUX critical section and pinned CPU0 inside the alarm ISR at step rates ~46 kHz and above, eventually tripping the IDLE0 watchdog. The new path is two register writes per pulse with no spinlock, no critical section, and no driver-state bookkeeping. Public API is unchanged; the consumer still calls `rearmFromIsr(ticks)` the same way. 1.5.8 fixes a follow-on bug from that rewrite: `lastAlarmCount_` (the relative-scheduling cursor) used to be refreshed from `gptimer_alarm_event_data_t::alarm_value`, which is filled from a cached C-struct field that the LL-bypass doesn't update — leaving the cursor frozen at the seed value and causing every `rearmFromIsr` to schedule into the past. `HwTimer` now maintains the cursor itself (seeded in `startOneShotTicks`, advanced in `rearmFromIsr`); `fireFromIsr` no longer touches it.

## Critical section (`ungula/hal/sync/critical_section.h`)

`CriticalSection` is a dual-core spinlock on ESP32 (`portMUX_TYPE`) and a no-op on host builds. Use it to guard tiny shared state touched by both task context and ISR callbacks.

### Real-world use case — ISR/task shared counters

```cpp
#include <ungula/hal/sync/critical_section.h>

namespace sync = ungula::hal::sync;

sync::CriticalSection statsCs;
volatile uint32_t edgeCount = 0;
uint32_t processedCount = 0;

void UNGULA_ISR_ATTR onEdge(void* /*ctx*/) {
    sync::ScopedLock lock(statsCs);
    edgeCount++;
}

void loop() {
    uint32_t pending = 0;
    {
        sync::ScopedLock lock(statsCs);
        pending = edgeCount;
        edgeCount = 0;
    }
    processedCount += pending;
}
```

### Critical section API

| Symbol | Description |
| --- | --- |
| `CriticalSection::enter()` / `exit()` | Manual lock/unlock for tiny critical regions. |
| `ScopedLock` | RAII guard; lock on construction, unlock on destruction. |

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

## PWM input capture (`ungula/hal/pwm_input/i_pwm_input.h`)

Single-pin PWM-input capture. Wraps "this pin carries a PWM signal — give me the most recent high-pulse width and period". The chip behind the signal can be anything — AS5600 in PWM mode, MT6701 PWM mode, RC receivers, servo position feedback, etc. The HAL only cares about the timing of the edges.

The interface is intentionally narrow: callers ask for the latest measurement; they do not subscribe to per-edge events. The backend (GPIO interrupt + `esp_timer` on ESP32) records the most recent rising-edge to falling-edge gap (high time) and rising-edge to rising-edge gap (period). After `begin()`, `hasSample()` stays `false` until a full period has been observed; once true, `sampleAgeUs()` reports microseconds since the last edge so callers can flag a stalled signal.

### Real-world use case — read an AS5600 in PWM mode

```cpp
#include <ungula/hal/pwm_input/drivers/pwm_input.h>

namespace pwm = ungula::hal::pwm_input;

pwm::drivers::PwmInput cap;

void setup() {
    cap.begin(/*pin=*/34);
}

void loop() {
    if (!cap.hasSample() || cap.sampleAgeUs() > 50000U) {
        // signal stalled — flag it, but don't crash the read loop
        return;
    }
    const uint32_t high   = cap.lastHighTimeUs();
    const uint32_t period = cap.lastPeriodUs();
    // AS5600 frame: 4351 chip clocks total, 128 high preamble.
    // raw_angle = round(high / period * 4351) - 128
}
```

In production this is exactly what `ungula::encoder::drivers::As5600Pwm` does internally — the encoder driver borrows an `IPwmInput&` and translates the high/period numbers into degrees.

### Real-world use case — per-frame ISR callback (no polling)

For low-latency consumers, `setSampleCallback()` registers a function the backend invokes from interrupt context every time a complete period is captured. After it lands, the host can stop polling — the callback runs on every frame.

```cpp
#include <ungula/hal/core/compiler_attrs.h>   // UNGULA_ISR_ATTR
#include <ungula/hal/pwm_input/drivers/pwm_input.h>

namespace pwm = ungula::hal::pwm_input;

pwm::drivers::PwmInput cap;
volatile uint32_t g_lastHighUs = 0;
volatile uint32_t g_lastPeriodUs = 0;

UNGULA_ISR_ATTR void onPwmFrame(void* /*ctx*/) {
    g_lastHighUs   = cap.lastHighTimeUs();
    g_lastPeriodUs = cap.lastPeriodUs();
    // No floats, no logging, no I2C/SPI here — keep it short.
}

void setup() {
    cap.begin(/*pin=*/34);
    cap.setSampleCallback(&onPwmFrame, /*ctx=*/nullptr);
}
```

The callback fires on the rising edge once a full period has been observed, so it sees a consistent `(highUs, periodUs)` pair. Pass `nullptr` to disarm. There is one slot per `PwmInput` instance — registering a new callback replaces any previous one.

### PWM input API

| Method | Description |
| --- | --- |
| `begin(pin)` | Install the capture for `pin`. Idempotent — second call returns `false`. |
| `stop()` | Tear down. Safe to call when not installed. |
| `lastHighTimeUs()` | Most recent high-pulse width in µs. `0` before the first complete period. |
| `lastPeriodUs()` | Most recent rising-to-rising period in µs. `0` before the first complete period. |
| `hasSample()` | `true` once at least one full period has been captured. |
| `sampleAgeUs()` | Microseconds since the last edge. Pair with a threshold to detect a stalled signal. |
| `pin()` | Pin the capture is bound to (valid only after `begin()`). |
| `setSampleCallback(cb, ctx)` | Install a per-period ISR-context callback. `cb=nullptr` disarms. One slot per instance. |

Drivers shipped:

- `drivers/pwm_input.h` — concrete `IPwmInput`, platform-dispatched (ESP32 + host stub).
- `drivers/pwm_input_fake.h` — header-only fake (sample injection + counters). For host tests of consumer drivers.

## Quadrature decoder (`ungula/hal/quadrature/i_decoder.h`)

A/B (and optional Z index) quadrature decoder. Wraps "I have an A/B pulse pair on these two pins — give me the signed count of edges". On ESP32 the backend is the PCNT peripheral configured for 4× quadrature decoding (every edge counts), with a virtual 32-bit accumulator on top of PCNT's 16-bit hardware counter. On host the backend is a counter you can move with `reset()` — useful for compile-checks, but not for simulating motion. Tests of consumer code drive `DecoderFake` instead.

The interface is small on purpose: hardware decoders track motion in the background, so callers only need to read the count. Index handling (`hasIndex()`, `latchedAtIndex()`) is reported by backends that wire up Z; backends without a Z line keep the safe defaults (`false`).

### Real-world use case — angle from an MT6701 in ABI mode

```cpp
#include <ungula/hal/quadrature/drivers/decoder.h>

namespace quad = ungula::hal::quadrature;

quad::drivers::Decoder dec;

void setup() {
    // 1024 PPR encoder × 4× quadrature = 4096 counts per revolution.
    dec.begin(/*pinA=*/34, /*pinB=*/35, /*initial=*/0);
}

void loop() {
    const int32_t count = dec.count();
    const float degrees = static_cast<float>(count) * (360.0f / 4096.0f);
    // ...
}
```

This is what `ungula::encoder::drivers::Mt6701Abi` does — it borrows an `IDecoder&` and exposes degrees through the standard `IEncoder` API (`setCalibration`, `readAngle`).

### Real-world use case — homing to a known reference

```cpp
#include <ungula/hal/quadrature/drivers/decoder.h>

namespace quad = ungula::hal::quadrature;

quad::drivers::Decoder dec;

void homeAxis() {
    // Drive the axis until the home switch trips; then snap the
    // count to zero so subsequent reads are absolute.
    if (homeSwitchTripped()) {
        dec.reset(/*value=*/0);
    }
}
```

`reset()` clears both the hardware counter and the virtual accumulator, so the next `count()` starts from `value`.

### Quadrature decoder API

| Method | Description |
| --- | --- |
| `begin(pinA, pinB, initial=0)` | Install the decoder. Idempotent. Seeds the count with `initial`. |
| `stop()` | Tear down. Idempotent. |
| `count()` | Current signed count (32-bit on the ESP32 backend). |
| `reset(value=0)` | Force the count to `value`. Use it for homing. |
| `hasIndex()` | `true` when the backend tracks a Z (index) line. Default `false`. |
| `latchedAtIndex()` | `true` when the latest count was captured at the Z pulse. Meaningful only when `hasIndex()`. |
| `pinA()` / `pinB()` | Pins as supplied to `begin()`; `0xFF` until installed. |

Drivers shipped:

- `drivers/decoder.h` — concrete `IDecoder`, platform-dispatched (ESP32 PCNT + host stub).
- `drivers/decoder_fake.h` — header-only fake (`tick(delta)`, `setCount`, index latching, counters). For host tests of consumer drivers.

## Structure

```text
lib_hal/
  src/
    ungula/
      hal.h                       # chain header for Arduino discovery
      hal/
        gpio/
          gpio.h           # bridge header (dispatches to platform)
          gpio_types.h
          platforms/
            gpio_esp32.h   # ESP32: gpio_ll direct register access
            gpio_default.h # no-op stubs for desktop builds
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
        sync/
          critical_section.h      # cross-platform critical section + RAII lock
          platforms/
            critical_section_esp32.h   # portMUX-based spinlock
            critical_section_default.h # host no-op
        timer/
          i_hwtimer.h             # hardware timer interface (one-shot + re-arm)
          drivers/
            hwtimer.h             # concrete timer, platform-dispatched
            hwtimer_fake.h        # header-only fake for host tests
          platforms/
            hwtimer_esp32.cpp     # ESP32 GPTimer backend
            hwtimer_default.cpp   # host stub backend
        multiplexer/
          i_multiplexer.h         # interface + cached selectChannel
          i_multiplexer.cpp       # base behaviour (cache, retry, logging)
          drivers/
            multiplexer_tca9548.h # TCA9548A driver
            multiplexer_tca9548.cpp
            multiplexer_fake.h    # header-only fake for tests
        pwm_input/
          i_pwm_input.h           # abstract single-pin PWM-input capture
          i_pwm_input.cpp         # translation-unit anchor
          drivers/
            pwm_input.h           # concrete IPwmInput, platform-dispatched
            pwm_input_fake.h      # header-only fake for tests
          platforms/
            pwm_input_esp32.cpp   # GPIO ISR + esp_timer high/period capture
            pwm_input_default.cpp # no-op stub for desktop builds
        quadrature/
          i_decoder.h             # abstract A/B (+ optional Z) decoder
          i_decoder.cpp           # translation-unit anchor
          drivers/
            decoder.h             # concrete IDecoder, platform-dispatched
            decoder_fake.h        # header-only fake for tests
          platforms/
            decoder_esp32.cpp     # PCNT (pulse_cnt) backend, 4× quadrature
            decoder_default.cpp   # host stub (count moves only via reset)
  tests/
    test_gpio_access.cpp          # GPIO stub tests (GoogleTest)
    test_i2c_master.cpp           # I2C stub tests
    test_spi_master.cpp           # SPI stub tests
    test_uart.cpp                 # UART stub tests
    test_adc_manager.cpp          # AdcManager stub tests
    test_can.cpp                  # CAN stub tests
    test_sync.cpp                 # CriticalSection + ScopedLock compile/behavior tests
    test_hwtimer.cpp              # HwTimer default backend + HwTimerFake tests
    test_multiplexer.cpp          # IMultiplexer base + fake driver tests
    test_pwm_input.cpp            # PwmInput stub + PwmInputFake tests
    test_quadrature.cpp           # Decoder stub + DecoderFake tests
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
