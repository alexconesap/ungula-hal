# UngulaHal

Embedded C++17 hardware abstraction layer (HAL) for ESP32-class MCUs. Wraps GPIO, PWM, ADC, UART, I2C, SPI, CAN 2.0, an I2C bus multiplexer interface, single-pin PWM input capture, and quadrature (A/B) decoding behind stable C++ interfaces in the `ungula::hal::` namespace, so higher-level libraries (motor, loadcell, sd, encoder, etc.) compile against the abstractions instead of vendor SDKs. ESP32 backend uses `gpio_ll`, LEDC, `esp_adc`, ESP-IDF `uart_driver`, ESP-IDF legacy `driver/i2c.h`, `driver/spi_master.h`, `driver/twai.h`, GPIO ISRs + `esp_timer` (PWM input), and `driver/pulse_cnt.h` (PCNT, quadrature). Non-ESP32 builds get no-op stubs that compile and link for host unit tests; consumer-driver tests use the header-only fakes shipped with each peripheral that has an interface (`MultiplexerFake`, `PwmInputFake`, `DecoderFake`).

This library is the only place direct Arduino / vendor APIs are allowed. Everything else in the project must call the symbols documented here.

## Platform support

Currently supported production backend:

- ESP32

Test-only backend:

- default/fake backend for host tests

The default backend is not suitable for production MCU targets. New MCU targets must provide platform-specific implementations for timer and synchronization primitives before being used in production.

---

## Build requirements (ESP-IDF components)

**ESP32 / ESP-IDF backend only** (`ESP_PLATFORM` defined). On host/fake builds these sources are no-ops and need no IDF component.

lib_hal is consumed as source. The consuming project's `idf_component_register(... REQUIRES ...)` must list every IDF component used by the lib_hal sources it compiles, or the build fails with `fatal error: <header>: No such file or directory`.

| lib_hal peripheral | IDF component to add to `REQUIRES` |
| --- | --- |
| ADC (`adc_manager.h`) | **`esp_adc`** — ⚠️ not pulled in by `driver`; must be added explicitly |
| GPIO, UART, I2C, SPI, CAN, PWM out, quadrature (PCNT) | `driver` |
| PWM input capture, hardware timer | `esp_timer` |
| GPIO low-level / SoC caps (`hal/gpio_ll.h`, `soc/soc_caps.h`) | `hal`, `soc` (usually transitive via `driver`) |

Projects that `GLOB_RECURSE` all of `lib_hal/src` compile `adc/platforms/adc_manager_esp32.cpp` even when the ADC is unused, so they need `esp_adc` regardless. Full-HAL baseline:

```cmake
REQUIRES driver esp_adc esp_timer esp_hw_support esp_system freertos
```

---

## LLM quick map

- **Build requires (ESP-IDF)**: consumer `REQUIRES` must include `esp_adc` for the ADC manager — it is NOT covered by `driver`. See [Build requirements](#build-requirements-esp-idf-components).

- **Primary include**: `#include <ungula/hal.h>`.
- **Arduino discovery include**: `#include <ungula_hal.h>` (forwarder only; host code should keep using the real header).
- **Namespace root**: `ungula::hal`.
- **Own source minimum**: `C++17`.
- **Effective minimum for consumers**: `C++17`.
- **Dependency impact**: None (no declared internal dependencies).
- **Supported architectures**: `esp32`.
- **Read order for coding agents**: `Usage` (working patterns) -> `API` (symbols/signatures) -> `Lifecycle`/`Error handling`/`Threading` notes in this file.

### Use-case index

- [Use case: blink an LED with checked configuration, unchecked hot-path writes](#use-case-blink-an-led-with-checked-configuration-unchecked-hot-path-writes)
- [Use case: read a button with pull-up and a falling-edge interrupt](#use-case-read-a-button-with-pull-up-and-a-falling-edge-interrupt)
- [Use case: PWM fan / LED dimming via LEDC](#use-case-pwm-fan-led-dimming-via-ledc)
- [Use case: ADC read with calibration](#use-case-adc-read-with-calibration)
- [Use case: I2C register read](#use-case-i2c-register-read)
- [Use case: SPI device on SPI2](#use-case-spi-device-on-spi2)
- [Use case: UART transmit/receive](#use-case-uart-transmitreceive)
- [Use case: interactive serial console / menu](#use-case-interactive-serial-console--menu)
- [Use case: CAN 2.0 bus to a servo motor](#use-case-can-20-bus-to-a-servo-motor)
- [Use case: variable-period hardware timer ISR](#use-case-variable-period-hardware-timer-isr)
- [Use case: protect ISR/task shared counters with `CriticalSection`](#use-case-protect-isrtask-shared-counters-with-criticalsection)
- [Use case: I2C bus multiplexer (TCA9548A)](#use-case-i2c-bus-multiplexer-tca9548a)
- [Use case: PWM input capture (AS5600 in PWM-only mode)](#use-case-pwm-input-capture-as5600-in-pwm-only-mode)
- [Use case: quadrature (A/B) decoder for an MT6701 in ABI mode](#use-case-quadrature-ab-decoder-for-an-mt6701-in-abi-mode)

### LLM rules

- Use only symbols and include paths documented in this file; do not infer extra public API from implementation files.
- Prefer the use-case patterns here over ad-hoc rewrites; keep dependency wiring and lifecycle order identical unless the task explicitly changes API design.
- Treat headers under `detail/`, `platform/`, and `platforms/` as internal unless this document calls them out as public.
- If required behavior is missing from the documented API, report the gap explicitly instead of inventing new public symbols.


## Usage

### Use case: blink an LED with checked configuration, unchecked hot-path writes

```cpp
#include <ungula/hal.h>
#include <ungula/hal/gpio/gpio.h>

constexpr uint8_t LED_PIN = 2;

void setup() {
    ungula::hal::gpio::configOutput(LED_PIN);
    ungula::hal::gpio::setLow(LED_PIN);
}

void loop() {
    ungula::hal::gpio::toggle(LED_PIN);
    // pacing handled by the project's `ungula::core::time` API, not delay()
}
```

When to use this: any digital output where the pin is known at compile time and was validated by `configOutput()` at boot. `toggle()` is read-then-write, not atomic — synchronise externally if shared with an ISR.

### Use case: read a button with pull-up and a falling-edge interrupt

```cpp
#include <ungula/hal/gpio/gpio.h>

constexpr uint8_t BUTTON_PIN = 4;

void UNGULA_ISR_ATTR onButton(void* /*ctx*/) {
    // Keep ISR body short. Signal a task/queue from here.
}

void setup() {
    using ungula::hal::gpio::InterruptEdge;
    using ungula::hal::gpio::PullMode;

    ungula::hal::gpio::configInputInterrupt(BUTTON_PIN, InterruptEdge::EDGE_FALLING, PullMode::UP);
    const auto install = ungula::hal::gpio::installIsrService();
    if (install == ungula::hal::gpio::IsrServiceInstall::Failed) {
        return;
    }
    ungula::hal::gpio::addIsrHandler(BUTTON_PIN, &onButton, nullptr);
}

void loop() {}
```

When to use this: any input that must wake on edge events without polling. `installIsrService()` is idempotent — repeated calls return `AlreadyInstalled`.

### Use case: PWM fan / LED dimming via LEDC

```cpp
#include <ungula/hal/gpio/gpio.h>

constexpr uint8_t FAN_PIN = 25;

void setup() {
    ungula::hal::gpio::configPwm(FAN_PIN, /*freqHz=*/25000, /*resolutionBits=*/8);
    ungula::hal::gpio::writePwm(FAN_PIN, 128);  // 50% duty at 8-bit resolution
}

void loop() {}
```

When to use this: fans, LED dimming, or any frequency-controlled output. `configPwm()` is single-assignment — a second call for the same pin returns false. Duty range is `0..(2^resolutionBits - 1)`.

### Use case: ADC read with calibration

```cpp
#include <ungula/hal/adc/adc_manager.h>

ungula::hal::adc::AdcManager adc;

constexpr uint8_t SENSE_PIN = 34;
constexpr uint8_t REF_PIN = 35;

void setup() {
    adc.configure(SENSE_PIN);                                // default DB_12 → 0..3.3 V
    adc.configure(REF_PIN, ungula::hal::adc::Attenuation::DB_6);  // 0..1.75 V, finer resolution
}

void loop() {
    uint32_t mv = 0;
    if (adc.readMv(SENSE_PIN, mv)) {
        // mv is calibrated millivolts (or attenuation-aware fallback)
    }
    int raw = 0;
    adc.readRaw(REF_PIN, raw);  // 0..4095, diagnostic
}
```

When to use this: any analog-input read. Configure every channel from a single context at boot. After that, `readMv()` / `readRaw()` are safe to call from multiple tasks against already-configured channels.

### Use case: I2C register read

```cpp
#include <ungula/hal/i2c/i2c_master.h>

ungula::hal::i2c::I2cMaster bus(0);  // I2C port 0

constexpr uint8_t DEVICE_ADDR = 0x2A;

void setup() {
    bus.begin(/*sdaPin=*/21, /*sclPin=*/22, /*freqHz=*/400000);
}

bool readReg(uint8_t reg, uint8_t* dst, size_t len) {
    return bus.writeRead(DEVICE_ADDR, &reg, 1, dst, len);
}

void loop() {}
```

When to use this: standard I2C peripherals exposing a register file. `writeRead()` issues a repeated START between phases — no STOP in the middle.

### Use case: SPI device on SPI2

```cpp
#include <ungula/hal/spi/spi_master.h>

ungula::hal::spi::SpiMaster spi;

void setup() {
    spi.begin(/*sclkPin=*/18, /*misoPin=*/19, /*mosiPin=*/23,
              /*csPin=*/5, /*freqHz=*/1000000, /*mode=*/0, /*host=*/1);
}

bool readSensor(uint8_t cmd, uint8_t* rxBuf, size_t rxLen) {
    return spi.writeRead(&cmd, 1, rxBuf, rxLen);
}

void loop() {}
```

When to use this: a single device per `SpiMaster` instance, one CS line per instance. For multiple devices on the same bus, instantiate one `SpiMaster` per device with the same `host` and different `csPin`.

### Use case: UART transmit/receive

```cpp
#include <ungula/hal/uart/uart.h>

ungula::hal::uart::Uart uart(2);  // ESP32 UART port 2

void setup() {
    uart.begin(/*baud=*/115200, /*txPin=*/17, /*rxPin=*/16);
}

void send(const uint8_t* data, size_t len) {
    uart.write(data, len);
    uart.flush();
}

int32_t receive(uint8_t* buf, size_t maxLen) {
    return uart.read(buf, maxLen, /*timeoutMs=*/100);
}

void loop() {}
```

When to use this: any serial protocol (Modbus, NMEA, custom). One `Uart` per physical port — class is non-copyable.

### Use case: interactive serial console / menu

```cpp
#include <ungula/hal/console/console.h>

using ungula::hal::console::Console;

Console console;  // defaults: UART0, 115200, TX=1 / RX=3

void setup() {
    (void)console.begin();  // false => RX driver not installed; warn if so
    console.write("Press 'h' for help.\r\n");
}

void loop() {
    const int c = console.readChar();   // -1 when nothing typed; never blocks
    if (c == 'h') console.write("commands: h, e, d\r\n");

    // Or assemble whole lines ("speed 40"): nullptr until Enter is pressed.
    const char* line = console.readLine();
    if (line != nullptr) { /* parse line */ }
}
```

When to use this: hardware bring-up harnesses and interactive serial menus. Non-blocking `readChar()` for instant single keys, `readLine()` for commands with arguments (handles backspace + echo). Output still uses `printf`/stdout, so a serial monitor keeps showing logs. Default-constructed targets the standard ESP32 console UART; pass a `ConsoleConfig` for a secondary port. One owner per UART port.

### Use case: CAN 2.0 bus to a servo motor

```cpp
#include <ungula/hal/can/can.h>

namespace can = ungula::hal::can;

can::Can bus(0);

void setup() {
    bus.begin(/*tx=*/21, /*rx=*/22, can::BITRATE_1M);
    bus.setAcceptanceFilter(0x141, 0x7FF, /*extendedId=*/false);
}

void sendCommand(uint32_t motorId, const uint8_t* payload, uint8_t len) {
    can::CanFrame f{};
    f.id = motorId;
    f.dlc = len;
    for (uint8_t i = 0; i < len; ++i) {
        f.data[i] = payload[i];
    }
    bus.send(f, /*timeoutMs=*/10);
}
```

When to use this: any CAN 2.0 protocol (servo motors, OBD-II, J1939, custom). One `Can` per physical controller — class is non-copyable. Higher-level protocol parsing belongs in a consumer library.

### Use case: variable-period hardware timer ISR

```cpp
#include <ungula/hal/gpio/gpio.h>
#include <ungula/hal/sync/critical_section.h>
#include <ungula/hal/timer/drivers/hwtimer.h>

namespace gpio = ungula::hal::gpio;
namespace sync = ungula::hal::sync;
namespace timer = ungula::hal::timer;

constexpr uint8_t STEP_PIN = 18;
constexpr uint32_t TIMER_RESOLUTION_HZ = 1000000;
constexpr uint32_t STEP_HALF_PERIOD_TICKS = 400;

timer::drivers::HwTimer pulseTimer;
sync::CriticalSection pulseCs;
volatile uint32_t edgeCount = 0;

void UNGULA_ISR_ATTR onPulse(void* /*ctx*/) {
    // Toggle the step pin from the ISR. Avoid RAII / ScopedLock here —
    // pulse ISRs should hold no locks; use atomics or volatile.
    gpio::toggle(STEP_PIN);
    edgeCount++;
    // Schedule the next edge. Returning Ok keeps the train going.
    pulseTimer.rearmFromIsr(STEP_HALF_PERIOD_TICKS);
}

void setup() {
    gpio::configOutput(STEP_PIN);
    gpio::setLow(STEP_PIN);

    timer::HwTimerConfig cfg;
    cfg.resolutionHz = TIMER_RESOLUTION_HZ;
    cfg.minTicks     = 5;

    pulseTimer.begin(cfg);
    pulseTimer.setCallback(&onPulse, nullptr);
    pulseTimer.startOneShotTicks(STEP_HALF_PERIOD_TICKS);
}

void loop() {}
```

When to use this: deterministic pulse timing where the next alarm interval may change at every shot (motion engines, software DDS, bit-banged protocols). The timer is **true one-shot** — `startOneShotTicks()` fires the callback once, and the ISR must call `rearmFromIsr()` to keep going. At the last pulse of a move, call `disarmFromIsr()` from the ISR (or `stop()` from task context) to guarantee no further alarms.

### Use case: protect ISR/task shared counters with `CriticalSection`

```cpp
#include <ungula/hal/sync/critical_section.h>

namespace sync = ungula::hal::sync;

sync::CriticalSection statsCs;
volatile uint32_t isrCounter = 0;
uint32_t processedCounter = 0;

void UNGULA_ISR_ATTR onEvent(void* /*ctx*/) {
    sync::ScopedLock lock(statsCs);
    isrCounter++;
}

void loop() {
    uint32_t pending = 0;
    {
        sync::ScopedLock lock(statsCs);
        pending = isrCounter;
        isrCounter = 0;
    }
    processedCounter += pending;
}
```

When to use this: tiny shared state updates across ISR and task context. Keep locked regions short and side-effect free (no logging, no I/O, no allocation).

### Use case: I2C bus multiplexer (TCA9548A)

```cpp
#include <ungula/hal/i2c/i2c_master.h>
#include <ungula/hal/multiplexer/drivers/multiplexer_tca9548.h>

namespace mux = ungula::hal::multiplexer;

ungula::hal::i2c::I2cMaster bus(0);
mux::drivers::MultiplexerTCA9548 mux70(0x70, bus);

void setup() {
    bus.begin(21, 22, 400000);
    mux70.begin();
}

void readSensorOnChannel(uint8_t channel, uint8_t addr, uint8_t reg,
                         uint8_t* out, size_t n) {
    if (mux70.selectChannel(channel)) {
        bus.writeRead(addr, &reg, 1, out, n);
    }
}
```

When to use this: more I2C devices than the bus accommodates without address clashes (e.g. two AS5600 encoders that share the same fixed `0x36`). Repeat-calls to `selectChannel(n)` for the same `n` skip the wire — read-loop friendly.

To debug a specific instance, call `mux70.enableLogging()`; lines flow through EmblogX with module tag `mux`.

### Use case: PWM input capture (AS5600 in PWM-only mode)

```cpp
#include <ungula/hal/pwm_input/drivers/pwm_input.h>

namespace pwm = ungula::hal::pwm_input;

pwm::drivers::PwmInput cap;

void setup() {
    cap.begin(/*pin=*/34);
}

bool readEncoderRaw(uint16_t& rawAngle) {
    if (!cap.hasSample() || cap.sampleAgeUs() > 50000U) {
        return false;  // signal stalled
    }
    const uint64_t high   = cap.lastHighTimeUs();
    const uint64_t period = cap.lastPeriodUs();
    if (period == 0U || high > period) {
        return false;
    }
    // AS5600 PWM frame: 4351 chip clocks total, 128 high preamble.
    const uint64_t scaled = (high * 4351U + period / 2U) / period;
    rawAngle = (scaled <= 128U) ? 0U
             : static_cast<uint16_t>(scaled - 128U);
    return true;
}
```

When to use this: any peripheral whose state arrives as a duty-cycle on a single pin (AS5600 / MT6701 PWM, RC receiver channels, servo position feedback). Pair `hasSample()` with `sampleAgeUs()` to detect a stalled wire — the API does not throttle reads itself. For unit tests of the consumer code, swap `PwmInput` for `PwmInputFake` and call `injectSample()`.

### Use case: quadrature (A/B) decoder for an MT6701 in ABI mode

```cpp
#include <ungula/hal/quadrature/drivers/decoder.h>

namespace quad = ungula::hal::quadrature;

quad::drivers::Decoder dec;

void setup() {
    // 1024 PPR encoder × 4× quadrature decode = 4096 counts/rev.
    dec.begin(/*pinA=*/34, /*pinB=*/35, /*initial=*/0);
}

float angleDegrees() {
    const int32_t count = dec.count();
    return static_cast<float>(count) * (360.0f / 4096.0f);
}

void homeToZero() {
    dec.reset(0);
}
```

When to use this: any encoder reporting motion as A/B (and optional Z) pulses — incremental optical encoders, magnetic chips configured for ABI output, servo motor feedback. The ESP32 backend uses PCNT with a virtual 32-bit accumulator, so callers don't have to chase the hardware 16-bit limit. For unit tests, drive a `DecoderFake` with `tick(delta)` or `setCount(value)`.

---

## API

Public namespaces:

- `ungula::hal::gpio` — digital I/O, edge interrupts, PWM (free functions)
- `ungula::hal::adc` — `AdcManager` class, `Attenuation`, `CaliScheme`
- `ungula::hal::i2c` — `I2cMaster` class
- `ungula::hal::spi` — `SpiMaster` class
- `ungula::hal::uart` — `Uart` class, `DEFAULT_RX_BUF`, `DEFAULT_TX_BUF`
- `ungula::hal::can` — `Can` class, `CanFrame`, `BITRATE_*` constants
- `ungula::hal::sync` — `CriticalSection`, `ScopedLock`
- `ungula::hal::timer` — `IsrTimerCallback`, `HwTimerConfig`, `HwTimerStatus`, `IHwTimer`; concrete timer under `ungula::hal::timer::drivers` (`HwTimer`, `HwTimerFake`)
- `ungula::hal::multiplexer` — `IMultiplexer` interface; concrete drivers under `ungula::hal::multiplexer::drivers` (`MultiplexerTCA9548`, `MultiplexerFake`)
- `ungula::hal::pwm_input` — `IPwmInput` interface; concrete + fake under `ungula::hal::pwm_input::drivers` (`PwmInput`, `PwmInputFake`)
- `ungula::hal::quadrature` — `IDecoder` interface; concrete + fake under `ungula::hal::quadrature::drivers` (`Decoder`, `DecoderFake`)

Top-level chain header: `<ungula/hal.h>`. Including it pulls in every subsystem header. For finer-grained includes, use the per-subsystem headers shown in the use cases.

---

## Public types

### `ungula::hal::gpio::InterruptEdge`

```cpp
enum class InterruptEdge : uint8_t { EDGE_RISING = 0, EDGE_FALLING = 1, EDGE_ANY = 2 };
```

Edge selection for `configInputInterrupt()`.

### `ungula::hal::gpio::PullMode`

```cpp
enum class PullMode : uint8_t { NONE = 0, UP = 1, DOWN = 2 };
```

Internal pull-resistor selection for interrupt-enabled inputs. Default for `configInputInterrupt()` is `NONE` (assume external pull).

### `ungula::hal::gpio::GpioIsrHandler`

```cpp
using GpioIsrHandler = void (*)(void*);
```

ISR callback signature. Place the function with `UNGULA_ISR_ATTR` so it lands in IRAM and survives flash operations.

### `ungula::hal::gpio::IsrServiceInstall`

```cpp
enum class IsrServiceInstall : uint8_t {
    Installed = 0,
    AlreadyInstalled = 1,
    Failed = 2,
};
```

Result code for `installIsrService()`: first successful install returns `Installed`; subsequent idempotent calls return `AlreadyInstalled`; driver errors return `Failed`.

### `UNGULA_ISR_ATTR`  (`ungula/hal/core/compiler_attrs.h`)

Preprocessor macro. On ESP32 expands to `IRAM_ATTR` (forces the function into IRAM so it remains callable while flash is busy); on the default backend expands to nothing. Apply to any function called from ISR context — handlers, trampolines, helpers, inline callees.

Single source of truth in `ungula/hal/core/compiler_attrs.h`. Subsystems that need ISR placement (gpio, timer, sync, future ones) include this directly so there is no cross-subsystem dependency just for the macro.

### `ungula::hal::adc::Attenuation`

```cpp
enum class Attenuation : uint8_t {
    DB_0   = 0,  // ~0..950 mV
    DB_2_5 = 1,  // ~0..1.25 V
    DB_6   = 2,  // ~0..1.75 V
    DB_12  = 3,  // ~0..3.3 V (default)
};
```

Per-channel attenuation. The class tracks one calibration handle per `(unit, attenuation)` pair, so mixing attenuations on the same ADC unit is correct.

### `ungula::hal::adc::CaliScheme`

```cpp
enum class CaliScheme : uint8_t { None, CurveFitting, LineFitting };
```

Internal record of which calibration scheme the driver picked at runtime. Not used by callers; exposed for completeness.

### `ungula::hal::adc::AdcManager`

Owning class for the ADC peripheral. Non-copyable. Fields are private.

Public constants:

- `static constexpr size_t MAX_CHANNELS` — capacity (default 16, override with `-DUNGULA_HAL_MAX_ADC_CHANNELS=N`).
- `static constexpr size_t UNIT_COUNT = 2` (ESP32 backend only).
- `static constexpr size_t ATTEN_COUNT = 4` (ESP32 backend only).

### `ungula::hal::uart::Uart`

Owning class for one hardware UART port. Non-copyable. `port()` returns the index passed at construction.

Constants:

- `constexpr uint16_t ungula::hal::uart::DEFAULT_RX_BUF = 256;`
- `constexpr uint16_t ungula::hal::uart::DEFAULT_TX_BUF = 0;` — `0` means blocking writes (no TX ring buffer).

### `ungula::hal::console::Console`

Non-blocking serial console over a `Uart`. Non-copyable (owns the port). Construct with a `ConsoleConfig` (defaults to the ESP32 console UART) or no argument. Methods: `begin()` (false if the port is taken), `readChar()` (-1 if none), `readLine()` (nullptr until Enter), `echo(c)`, `write(str)`, `flushInput()`.

Constants / config:

- `constexpr uint8_t  ungula::hal::console::DEFAULT_PORT = 0;`
- `constexpr uint8_t  ungula::hal::console::DEFAULT_TX_PIN = 1;`
- `constexpr uint8_t  ungula::hal::console::DEFAULT_RX_PIN = 3;`
- `constexpr uint32_t ungula::hal::console::DEFAULT_BAUD = 115200;`
- `constexpr uint16_t ungula::hal::console::DEFAULT_LINE_MAX = 80;` — max usable chars per `readLine()`.
- `struct ConsoleConfig { uint8_t port; uint8_t txPin; uint8_t rxPin; uint32_t baud; }` — each field defaults to the matching constant above.

### `ungula::hal::i2c::I2cMaster`

Owning class for one I2C master port. Non-copyable. `port()` returns the index passed at construction.

### `ungula::hal::spi::SpiMaster`

Owning class for one SPI device on a bus. Non-copyable. One instance per chip-select line.

### `ungula::hal::can::Can`

Owning class for one CAN 2.0 controller. Non-copyable. `controller()` returns the index passed at construction.

### `ungula::hal::can::CanFrame`

Plain-old struct: 32-bit `id` (11- or 29-bit per `extendedId`), `remote` flag, `dlc` 0..8, `data[8]`.

Bitrate constants in the same namespace: `BITRATE_25K`, `BITRATE_50K`, `BITRATE_100K`, `BITRATE_125K`, `BITRATE_250K`, `BITRATE_500K`, `BITRATE_800K`, `BITRATE_1M`.

### `ungula::hal::sync::CriticalSection`

Cross-platform critical section used to guard tiny shared state between ISR and task context.

- ESP32 backend: `portMUX_TYPE` + `portENTER_CRITICAL_SAFE`/`portEXIT_CRITICAL_SAFE`.
- Host backend: no-op stub (compile/test seam).

### `ungula::hal::sync::ScopedLock`

RAII guard for `CriticalSection`. Locks in constructor, unlocks in destructor.

### `ungula::hal::timer::IsrTimerCallback`

```cpp
using IsrTimerCallback = void (*)(void* ctx);
```

ISR-context callback signature for timer alarm handlers. The callback runs in true interrupt context — must be IRAM-safe (`UNGULA_ISR_ATTR`), no logging / UART / I2C / SPI / blocking calls.

### `ungula::hal::timer::HwTimerStatus`

```cpp
enum class HwTimerStatus : uint8_t {
    Ok, NotInitialized, AlreadyInitialized, InvalidConfig,
    InvalidTicks, BackendError, Unsupported, Busy,
};
```

Returned by every non-const timer operation. Replaces boolean `bool` returns so the consumer can distinguish "wrong config" from "backend failure" from "called too early" without a separate error API.

### `ungula::hal::timer::HwTimerConfig`

Configuration struct for `IHwTimer::begin()`.

```cpp
struct HwTimerConfig {
    uint32_t resolutionHz = 1000000;   // counter tick rate
    uint32_t minTicks     = 5;           // lower bound on ticks args
};
```

`autoReload` was removed in lib_hal 1.5.2 — the timer is true one-shot, and periodic behaviour is the caller's responsibility via `rearmFromIsr()` from inside the alarm callback. In lib_hal 1.5.3 the implementation was hardened: `disarmFromIsr()` no longer calls `gptimer_stop()` from ISR, the armed state is `std::atomic<bool>` (dual-core safe with a software gate inside the trampoline), and `rearmFromIsr` schedules the next alarm from a cached `lastAlarmCount_` instead of reading the counter register on every pulse. In lib_hal 1.5.7 the ESP32 backend's `rearmFromIsr` was rewritten to call the lockless `timer_ll_set_alarm_value` / `timer_ll_enable_alarm` primitives directly, bypassing the gptimer driver's portMUX critical section that pinned CPU0 in the alarm ISR at ~46 kHz step rates. Public API unchanged. 1.5.7 had a latent bug from that rewrite: `gptimer_alarm_event_data_t::alarm_value` is filled from the gptimer driver's cached C-struct field, which our LL-bypass deliberately does not update, so `lastAlarmCount_` froze at the seed value and every `rearmFromIsr` re-scheduled into the past → instant ISR storm at any step rate. 1.5.8 fixes this by maintaining `lastAlarmCount_` internally: `startOneShotTicks` seeds it to the first alarm value and `rearmFromIsr` advances it after each register write; `fireFromIsr` no longer touches it.

### `ungula::hal::timer::IHwTimer`

Abstract hardware timer interface supporting true one-shot start and ISR-safe re-arm / disarm. See the "Public functions / methods" section below for the full contract.

### `ungula::hal::timer::drivers::HwTimer`

Concrete platform-dispatched timer. ESP32 backend wraps GPTimer with `auto_reload_on_alarm = false` so the one-shot contract holds at the hardware level; host backend is a structural stub for compile-only tests. The public header does not expose any ESP-IDF types — `gptimer_handle_t` is held internally as `void*`.

### `ungula::hal::timer::drivers::HwTimerFake`

Header-only fake. Models true one-shot semantics: `fire()` on a disarmed timer is dropped (silent no-op), so tests catch over-firing and stuck-armed bugs. Knobs: `fire()`, `fireMany(n)`, `isArmed()`, `lastArmedTicks()`. Counters: `beginCallCount`, `startCallCount`, `rearmCallCount`, `disarmCallCount`, `fireCallCount`, `firesDroppedWhileDisarmed`.

Backend matrix for the concrete `HwTimer`:
- ESP32: GPTimer with `auto_reload_on_alarm = false`. ISR trampoline lives in the platform `.cpp`; no ESP-IDF types reach the public header.
- Host (default): structural stub. Tracks armed/disarmed state and returns sensible `HwTimerStatus` codes, but never fires the callback. Tests of consumer code that need the callback to fire deterministically must use `HwTimerFake`.

### `ungula::hal::multiplexer::IMultiplexer`

Abstract base for I2C bus multiplexers. Concrete drivers live under `drivers/`. Caches the active channel; retries `SELECT_RETRY_COUNT` times on hardware failure. Per-instance EmblogX logging via `enableLogging()`/`disableLogging()` (off by default; module tag `mux`).

### `ungula::hal::multiplexer::drivers::MultiplexerTCA9548`

TCA9548A 8-channel multiplexer driver. Borrows a `I2cMaster&`; one bus can drive several multiplexers at different addresses (`0x70..0x77`).

### `ungula::hal::multiplexer::drivers::MultiplexerFake`

Header-only fake. Records `selectChannel_` calls, exposes scriptable failure injection (`failNextSelects(n)`, `setSelectAlwaysFails(true)`) and counters (`selectCallCount`, `restartCallCount`, etc.). Use it in any test that takes an `IMultiplexer*`.

### `ungula::hal::pwm_input::IPwmInput`

Abstract single-pin PWM-input capture. Exposes the latest high-pulse width and period for polling consumers, plus an optional per-period ISR callback (`setSampleCallback`) for consumers that need zero-latency updates without a polling task. The backend (GPIO ISR + `esp_timer` on ESP32) records every edge and fires the callback on the rising edge after a complete period has been captured. Non-copyable.

### `ungula::hal::pwm_input::SampleCallback`

```cpp
using SampleCallback = void (*)(void* ctx);
```

ISR-context callback signature for `IPwmInput::setSampleCallback`. The callback runs under the same constraints as a GPIO ISR: short, IRAM-safe, no logging, no I2C/SPI, no blocking calls. The accessor methods (`lastHighTimeUs()`, `lastPeriodUs()`, `pin()`) are safe to call from inside it.

### `ungula::hal::pwm_input::drivers::PwmInput`

Concrete `IPwmInput`, platform-dispatched. ESP32 backend installs a per-pin GPIO ISR that timestamps both edges with `esp_timer_get_time()`. Host backend is a `begin/stop`-only stub — tests should use `PwmInputFake` for sample injection.

### `ungula::hal::pwm_input::drivers::PwmInputFake`

Header-only fake. Test knobs: `injectSample(highUs, periodUs)` (silent — does *not* fire the callback), `triggerSample(highUs, periodUs)` (injects + fires the callback if armed, simulating an ISR fire), `setHighTimeUs`, `setPeriodUs`, `setHasSample`, `setSampleAgeUs`. Counters: `beginCallCount`, `stopCallCount`, `injectCallCount`, `setCallbackCallCount`, `callbackInvocationCount`. Use whenever a consumer driver takes `IPwmInput&`.

### `ungula::hal::quadrature::IDecoder`

Abstract A/B (and optional Z index) decoder. Exposes a signed count plus `reset(value)` for homing. `hasIndex()` defaults to `false`; backends that wire up the Z line override it and report `latchedAtIndex()`. Non-copyable.

### `ungula::hal::quadrature::drivers::Decoder`

Concrete `IDecoder`, platform-dispatched. ESP32 backend uses the PCNT peripheral configured for 4× quadrature decoding, with a virtual 32-bit accumulator added on top of PCNT's 16-bit hardware counter (so wraparound is invisible to callers). Host backend is a counter-only stub.

### `ungula::hal::quadrature::drivers::DecoderFake`

Header-only fake. Test knobs: `tick(delta)`, `setCount(value)`, `setHasIndex(bool)`, `markIndex(bool)`. Counters: `beginCallCount`, `stopCallCount`, `resetCallCount`. Use whenever a consumer driver takes `IDecoder&`.

---

## Public functions / methods

### GPIO — pin configuration (always validated)

```cpp
bool ungula::hal::gpio::configOutput(uint8_t pin);
bool ungula::hal::gpio::configInput(uint8_t pin);
bool ungula::hal::gpio::configInputPullup(uint8_t pin);
bool ungula::hal::gpio::configInputPulldown(uint8_t pin);
bool ungula::hal::gpio::configOutputOpenDrain(uint8_t pin);
```

- **Purpose** — set direction and pull state in one atomic ESP-IDF call.
- **Returns** — `true` on success, `false` for an invalid pin or driver error.
- **Side effects** — writes ESP32 IO_MUX / GPIO matrix registers.
- **Failure behavior** — return `false`; does not throw.
- **Usage notes** — call once per pin at boot before any unchecked read/write.

### GPIO — unchecked digital I/O (hot-path)

```cpp
bool ungula::hal::gpio::read(uint8_t pin);
void ungula::hal::gpio::setHigh(uint8_t pin);
void ungula::hal::gpio::setLow(uint8_t pin);
void ungula::hal::gpio::write(uint8_t pin, bool high);
void ungula::hal::gpio::writeHigh(uint8_t pin);
void ungula::hal::gpio::writeLow(uint8_t pin);
void ungula::hal::gpio::toggle(uint8_t pin);
bool ungula::hal::gpio::isHigh(uint8_t pin);
bool ungula::hal::gpio::isLow(uint8_t pin);
```

- **Purpose** — single-cycle GPIO access for ISRs, timers, PID loops.
- **Side effects** — direct W1TS/W1TC register writes via `gpio_ll`.
- **Failure behavior** — none. No validation. Caller must have configured the pin first.
- **Usage notes** — `toggle()` is *not* atomic (read followed by write). Synchronise externally if the pin is shared between ISR and task context.

### GPIO — checked digital I/O (untrusted pin source)

```cpp
bool ungula::hal::gpio::checkedRead(uint8_t pin, bool& out);
bool ungula::hal::gpio::checkedSetHigh(uint8_t pin);
bool ungula::hal::gpio::checkedSetLow(uint8_t pin);
bool ungula::hal::gpio::checkedWrite(uint8_t pin, bool high);
```

- **Purpose** — same as the unchecked variants but validate the pin against the SoC bitmask first.
- **Returns** — `true` if the pin is a valid (output-capable, where relevant) GPIO and the operation succeeded; `false` otherwise. `checkedRead()` only writes to `out` on success.
- **Usage notes** — use when the pin number comes from configuration files, runtime input, or anywhere not verified at compile time.

### GPIO — interrupts

```cpp
bool ungula::hal::gpio::configInputInterrupt(uint8_t pin,
                                        ungula::hal::gpio::InterruptEdge edge,
                                        ungula::hal::gpio::PullMode pull = ungula::hal::gpio::PullMode::NONE);
ungula::hal::gpio::IsrServiceInstall ungula::hal::gpio::installIsrService();
bool ungula::hal::gpio::addIsrHandler(uint8_t pin, ungula::hal::gpio::GpioIsrHandler handler, void* context);
bool ungula::hal::gpio::removeIsrHandler(uint8_t pin);
```

- **Purpose** — wire a GPIO to an interrupt vector and attach a handler.
- **Returns** — `installIsrService()` returns `Installed`, `AlreadyInstalled`, or `Failed`; all other functions return `true` on success.
- **Side effects** — installs the global GPIO ISR service on first call; registers the per-pin handler.
- **Usage notes** — handlers must be marked `UNGULA_ISR_ATTR`. Keep them short — defer real work to a task via a queue or notification.

### GPIO — PWM

```cpp
bool ungula::hal::gpio::configPwm(uint8_t pin, uint32_t freqHz = 1000, uint8_t resolutionBits = 8);
bool ungula::hal::gpio::writePwm(uint8_t pin, uint32_t duty);
```

- **Purpose** — assign an LEDC channel to the pin and drive duty cycle.
- **Returns** — `true` on success. `configPwm()` is single-assignment per pin: a second call for the same pin returns `false`.
- **Side effects** — claims one LEDC channel per pin from a finite pool. State lives in `gpio_pwm_esp32.cpp` (single TU, shared across the firmware).
- **Failure behavior** — channel pool exhaustion or invalid pin → `false`. `writePwm()` clamps duty to `(2^resolutionBits - 1)` to avoid hardware overflow.

### `ungula::hal::adc::AdcManager`

```cpp
bool configure(uint8_t pin, ungula::hal::adc::Attenuation atten = ungula::hal::adc::Attenuation::DB_12);
bool readMv(uint8_t pin, uint32_t& mv) const;
bool readRaw(uint8_t pin, int& raw) const;
void deinit() noexcept;
```

- **`configure`** — bind a GPIO to an ADC channel with the given attenuation. Single-assignment per pin: returns `false` on duplicate. Succeeds even when calibration cannot be created (read path falls back to attenuation-aware linear conversion, ~5–10 % error).
- **`readMv`** — calibrated millivolts when an eFuse calibration handle exists; fallback otherwise. Returns `false` for unknown pins or driver errors. Always sets `mv` on success (including to `0` for a grounded input — use the return value, not magnitude, to detect failure).
- **`readRaw`** — raw 12-bit ADC count (0..4095 on classic ESP32). Returns `false` for unknown pins.
- **`deinit`** — release unit and calibration handles. Idempotent. Destructor calls it.
- **Failure behavior** — returns `false`; never throws. Does not allocate after `configure()`.
- **Side effects** — `configure()` claims an ESP-IDF oneshot unit handle and possibly a cali handle. ADC2 channels conflict with Wi-Fi; prefer ADC1 (GPIO 32–39) when the radio is up.

### `ungula::hal::i2c::I2cMaster`

```cpp
explicit I2cMaster(uint8_t portNumber);
~I2cMaster();
bool begin(uint8_t sdaPin, uint8_t sclPin, uint32_t freqHz = 400000);
bool write(uint8_t addr, const uint8_t* data, size_t length, uint32_t timeoutMs = 50);
bool read(uint8_t addr, uint8_t* buffer, size_t length, uint32_t timeoutMs = 50);
bool writeRead(uint8_t addr, const uint8_t* writeData, size_t writeLen,
               uint8_t* readBuf, size_t readLen, uint32_t timeoutMs = 50);
uint8_t port() const;
```

- **`begin`** — install the legacy `driver/i2c.h` master, configure pins and clock. Returns `false` if already installed or driver error.
- **`write` / `read`** — single-phase transactions to a 7-bit address. Return `true` only on full ACK before timeout.
- **`writeRead`** — write phase, repeated START, then read phase. Standard register read.
- **`port`** — the index passed at construction (ESP32: 0 or 1).
- **Failure behavior** — return `false` on NACK, timeout, or driver error. Never throws.
- **Side effects** — owns the driver for the port. Destructor uninstalls it.

### `ungula::hal::spi::SpiMaster`

```cpp
SpiMaster();
~SpiMaster();
bool begin(uint8_t sclkPin, uint8_t misoPin, uint8_t mosiPin, uint8_t csPin,
           uint32_t freqHz = 1000000, uint8_t mode = 1, uint8_t host = 1);
bool transfer(const uint8_t* txData, uint8_t* rxData, size_t length);
bool write(const uint8_t* data, size_t length);
bool read(uint8_t* buffer, size_t length);
bool writeRead(const uint8_t* txData, size_t writeLen, uint8_t* rxBuf, size_t readLen);
```

- **`begin`** — initialise the SPI bus (if not already) and add this device. `host=1` → SPI2_HOST, `host=2` → SPI3_HOST. `mode` is 0–3 (`CPOL | CPHA`).
- **`transfer`** — full-duplex; either buffer may be `nullptr` for half-duplex.
- **`write` / `read`** — convenience wrappers around `transfer`. `read()` clocks zeros out.
- **`writeRead`** — write then read inside a single CS assertion (no de-assert between phases).
- **Failure behavior** — return `false` on driver error. Never throws.
- **Side effects** — adds one device handle to the bus; destructor removes the device but leaves the bus initialised so other `SpiMaster` instances can keep using it.
- **Usage notes** — for multiple devices on the same bus, share `host` and use distinct `csPin` per instance.

### `ungula::hal::can::Can`

```cpp
explicit Can(uint8_t controllerNumber);
~Can();
bool begin(uint8_t txPin, uint8_t rxPin, uint32_t bitrateBps);
bool stop();
bool send(const CanFrame& frame, uint32_t timeoutMs = 50);
int32_t receive(CanFrame& out, uint32_t timeoutMs = 0);
bool setAcceptanceFilter(uint32_t id, uint32_t mask, bool extendedId);
bool clearAcceptanceFilter();
bool isBusOff() const;
bool recoverFromBusOff();
uint8_t controller() const;
```

- **`begin`** — install + start the controller at one of the `BITRATE_*` presets. Returns `false` if already installed or the bitrate is unsupported.
- **`send`** — transmit one frame. Blocks up to `timeoutMs` waiting for room in the TX queue. Returns `false` on timeout / driver error.
- **`receive`** — read one frame. Returns `1` on success, `0` on timeout (`timeoutMs > 0`), `-1` on driver error or before `begin()`.
- **`setAcceptanceFilter`** / **`clearAcceptanceFilter`** — single hardware filter; the wrapper does a transparent stop+reinstall under the hood because TWAI only accepts the filter at install time.
- **`isBusOff`** / **`recoverFromBusOff`** — bus-off detection (line stuck low, error counter overflow) and recovery. The bus may take some milliseconds to come back online after `recoverFromBusOff()`.
- **Failure behavior** — `bool` returns are `false` on failure; `int32_t` returns are `-1`. Never throws.
- **Side effects** — owns the TWAI driver for the controller. Destructor stops + uninstalls it. Non-copyable.

### `ungula::hal::sync::CriticalSection`

```cpp
void enter();
void exit();
```

- **Purpose** — lock tiny ISR/task shared regions.
- **Failure behavior** — no return value; lock operations are backend primitives.
- **Usage notes** — keep critical regions minimal; prefer `ScopedLock` for exception-safe/early-return-safe unlock.

### `ungula::hal::sync::ScopedLock`

```cpp
explicit ScopedLock(CriticalSection& cs);
~ScopedLock();
```

- **Purpose** — RAII wrapper around `enter()`/`exit()`.
- **Usage notes** — use block scope to define lock lifetime explicitly.

### `ungula::hal::timer::IHwTimer`

```cpp
virtual HwTimerStatus begin(const HwTimerConfig& cfg) = 0;
virtual HwTimerStatus setCallback(IsrTimerCallback cb, void* ctx) = 0;
virtual HwTimerStatus startOneShotTicks(uint32_t ticks) = 0;
virtual HwTimerStatus rearmFromIsr(uint32_t ticks) = 0;
virtual HwTimerStatus disarmFromIsr() = 0;
virtual HwTimerStatus stop() = 0;
virtual uint32_t      resolutionHz() const = 0;
virtual bool          isArmed()      const = 0;
```

- **`begin`** — allocate/configure timer resources. Returns `InvalidConfig` for `resolutionHz == 0`, `AlreadyInitialized` on a second call, `BackendError` on driver failure.
- **`setCallback`** — register ISR callback. Returns `NotInitialized` if called before `begin`.
- **`startOneShotTicks`** — task-context arm. Counter resets to 0; alarm fires once at `ticks`. Returns `InvalidTicks` if `ticks < cfg.minTicks`. After the alarm fires the timer is **disarmed** — call `rearmFromIsr()` inside the callback to keep going.
- **`rearmFromIsr`** — ISR-safe re-arm. Sets the next alarm to fire `ticks` ticks after the alarm that just fired. The implementation tracks the last alarm count from the event data so no counter register read is needed per pulse.
- **`disarmFromIsr`** — ISR-safe disarm. Guarantees no further user callback will be invoked until the next `startOneShotTicks` from task context. Implemented as a software gate at the trampoline boundary — does NOT call `gptimer_stop` from ISR; the natural one-shot (auto_reload=false) hardware behaviour means no pending hardware fire after the callback returns.
- **`stop`** — task-context full halt. Stops the hardware counter AND clears the armed state.
- **`resolutionHz`** — configured timer resolution; 0 before begin.
- **`isArmed`** — true between any successful arm/rearm and the next alarm firing or any disarm/stop. Implemented as `std::atomic<bool>` so it's safe to read from any task or ISR on dual-core ESP32 — the same atomic backs the trampoline's "drop the fire if disarmed" gate.

ESP32 implementation note: `started_` and `armed_` are intentionally separate. With GPTimer one-shot (`auto_reload_on_alarm=false`), the alarm can be consumed (`armed_ = false`) while the hardware timer FSM is still running. `startOneShotTicks()` keys its stop/restart sequence on `started_`, not `armed_`, to avoid restart failures (`ESP_ERR_INVALID_STATE`).

### `ungula::hal::pwm_input::IPwmInput`

```cpp
virtual bool begin(uint8_t pin) = 0;
virtual bool stop() = 0;
virtual uint32_t lastHighTimeUs() const = 0;
virtual uint32_t lastPeriodUs() const = 0;
virtual bool hasSample() const = 0;
virtual uint32_t sampleAgeUs() const = 0;
virtual uint8_t pin() const = 0;
virtual void setSampleCallback(SampleCallback cb, void* ctx) = 0;
```

- **`begin`** — install the capture for `pin`. Idempotent — second call returns `false`.
- **`stop`** — tear down the capture. Safe to call when not installed.
- **`lastHighTimeUs`** / **`lastPeriodUs`** — most recent high-pulse width and full-period duration in µs. Both return `0` until a complete period has been observed.
- **`hasSample`** — `true` once at least one full period has been captured.
- **`sampleAgeUs`** — microseconds since the most recent edge. Useful for detecting a stalled signal even when `hasSample()` is `true`.
- **`setSampleCallback`** — install (or disarm with `cb = nullptr`) a function the backend invokes from ISR context after every complete period sample. The callback runs under the standard GPIO-ISR rules: short, IRAM-safe, no logging, no I2C/SPI, no blocking. One slot per instance — re-arming replaces any previous callback. Use this when a consumer needs per-frame latency without running a polling task; consumers see exactly the same accessor values inside the callback as they would right after polling.
- **Failure behavior** — `bool` returns are `false` on failure. The accessors do not signal errors; they return `0` when there is no sample yet. Pair them with `hasSample()` / `sampleAgeUs()` for liveness checks.
- **Side effects** — concrete ESP32 driver installs the global GPIO ISR service on first `begin()` and registers a per-pin handler that fires the registered callback (if any) on the rising edge after the period has been computed. The host-side stub stores the callback but never calls it (no real samples). The fake exposes `injectSample()` (silent) and `triggerSample()` (fires the callback) plus call counters for tests.

### `ungula::hal::quadrature::IDecoder`

```cpp
virtual bool begin(uint8_t pinA, uint8_t pinB, int32_t initialCount = 0) = 0;
virtual bool stop() = 0;
virtual int32_t count() const = 0;
virtual bool reset(int32_t value = 0) = 0;
virtual bool hasIndex() const;            // default: false
virtual bool latchedAtIndex() const;      // default: false
virtual uint8_t pinA() const = 0;
virtual uint8_t pinB() const = 0;
```

- **`begin`** — install the decoder on `pinA` / `pinB` and seed the count with `initialCount`. Idempotent — second call returns `false`.
- **`stop`** — tear down the decoder. Idempotent.
- **`count`** — current signed count. The ESP32 backend combines the seed value with the live PCNT delta so callers see a 32-bit virtual count even though the hardware counter is 16-bit.
- **`reset`** — force the count to `value`. Use it when homing to a known reference. Clears the underlying PCNT counter on hardware backends.
- **`hasIndex`** / **`latchedAtIndex`** — Z-line support reported by the backend; safe defaults are `false`.
- **`pinA`** / **`pinB`** — pins as supplied to `begin()`. `0xFF` until installed.
- **Failure behavior** — `bool` returns are `false` on failure. `count()` always returns the last known value (no error signalling needed; an unhealthy bus never gets installed in the first place).
- **Side effects** — concrete ESP32 driver allocates a PCNT unit + 2 channels per instance and frees them in `stop()`/destructor. The fake exposes `tick(delta)`, `setCount(value)`, `setHasIndex(bool)`, `markIndex(bool)`, plus call counters.

### `ungula::hal::uart::Uart`

```cpp
explicit Uart(uint8_t portNumber);
~Uart();
bool begin(uint32_t baudRate, uint8_t txPin, uint8_t rxPin,
           uint16_t rxBufSize = ungula::hal::uart::DEFAULT_RX_BUF,
           uint16_t txBufSize = ungula::hal::uart::DEFAULT_TX_BUF);
int32_t write(const uint8_t* data, size_t length);
int32_t read(uint8_t* buffer, size_t maxLength, uint32_t timeoutMs);
void flush();
void flushInput();
uint8_t port() const;
```

- **`begin`** — install the ESP-IDF `uart_driver`, configure baud rate and pin routing. Returns `false` if already installed or on driver error.
- **`write`** — blocking transmit until bytes land in the FIFO/ring. Returns bytes written or `-1` on error.
- **`read`** — read up to `maxLength` bytes, blocking up to `timeoutMs` (use `0` for non-blocking). Returns bytes read or `-1`.
- **`flush`** — block until TX is physically complete on the wire.
- **`flushInput`** — discard pending RX bytes.
- **Failure behavior** — `int32_t` return of `-1` indicates a driver error; `bool begin()` returns `false`. Never throws.
- **Side effects** — owns the port driver; destructor uninstalls it. With `txBufSize = 0`, writes are blocking with no ring buffer.

---

## Lifecycle

1. **Boot (`setup()`):** call every `config*()` (GPIO), `configPwm()`, `AdcManager::configure()`, `I2cMaster::begin()`, `SpiMaster::begin()`, `Uart::begin()`, `Can::begin()`, `IMultiplexer::begin()`, `IPwmInput::begin()`, `IDecoder::begin()` you need. All of these may allocate driver state — heap allocation after `setup()` is forbidden by project rules, so do it here.
2. **Operate (`loop()` / tasks):** call the unchecked read/write helpers, `readMv()`, `read()`/`write()`/`writeRead()`, `send()`/`receive()`, `lastHighTimeUs()`/`lastPeriodUs()`, `count()`. These do not allocate.
3. **Shutdown:** destructors of `AdcManager`, `Uart`, `I2cMaster`, `SpiMaster`, `Can`, the concrete `PwmInput`, the concrete `Decoder` release their drivers. `AdcManager::deinit()` is idempotent — call it explicitly only when reordering matters. `IPwmInput::stop()` and `IDecoder::stop()` are idempotent for re-init flows.

For timer/sync: initialize `IHwTimer` in boot, install callback once, start with `startOneShotTicks()`, and only use `rearmFromIsr()` from inside the callback to continue the pulse train. Call `disarmFromIsr()` at the last pulse so no further alarm can fire. Use `CriticalSection` for tiny shared state between callback and task — but prefer atomics/volatile inside the pulse ISR over RAII `ScopedLock`, since holding a lock across a step edge is a needless source of jitter.

Calling unchecked GPIO functions on a pin you never configured is undefined behavior at the SoC level. Use the checked variants if there is any doubt.

---

## Error handling

- Every `bool`-returning function returns `false` on failure. No exceptions are thrown; the library is `-fno-exceptions`-friendly.
- UART `read`/`write` return `int32_t` with `-1` for errors and a non-negative byte count on success.
- `AdcManager::readMv` and `readRaw` set the out parameter on every successful call (including legitimate zero readings). Always check the boolean return.
- ESP-IDF errors are not propagated — callers see only `bool` / `-1`. There is no logging from this library by design (project rule: HAL does not log).
- Single-assignment APIs (`configPwm`, `AdcManager::configure`) return `false` on duplicate registration; treat that as a programming error, not a runtime fault.

---

## Threading / timing / hardware notes

- **GPIO unchecked functions** are ISR-safe (single-cycle register writes through `gpio_ll`). The checked variants are *not* — they call `GPIO_IS_VALID_*` which is fine on ESP32 but should not be used from within an ISR by convention.
- **`toggle()` is not atomic.** Read-then-write. Synchronise externally if shared.
- **`AdcManager::configure()` is not thread-safe.** Run it on a single context at boot. After configuration, `readMv()` / `readRaw()` are safe to call from multiple tasks (ESP-IDF oneshot reads are internally synchronised).
- **`I2cMaster`, `SpiMaster`, `Uart`** are non-copyable. Treat each instance as exclusive owner of its port. Concurrent access from multiple tasks must be serialised by the caller (mutex around the bus).
- **`IHwTimer` callback runs in ISR context.** No logging, no I2C/SPI/UART calls, no heap.
- **Use `CriticalSection` only for short POD updates.** Keep lock windows tiny.
- **ADC2 + Wi-Fi:** classic ESP32 ADC2 channels conflict with the Wi-Fi PHY. Prefer ADC1 (GPIO 32–39) when the radio is active.
- **PWM (LEDC):** a finite number of channels is shared across the firmware. `configPwm()` is single-assignment per pin and consumes one channel; do not call it in a loop.
- **No heap after `setup()`:** all `begin()` / `configure()` / `configPwm()` calls must happen during initialization.
- **PWM-input ISR scope:** the ESP32 `PwmInput` backend installs a per-pin GPIO ISR that runs in IRAM and only touches a small file-scope state slot indexed by the GPIO number. Multiple `PwmInput` instances on different pins coexist; two instances on the same pin do not (the second `begin()` returns `false`).
- **PCNT pool:** the ESP32 `Decoder` backend allocates one PCNT unit per instance (4 units on ESP32 classic, 8 on S3). Treat that as a finite resource — don't construct decoders inside hot paths, and call `stop()` before destroying one if a unit needs to be reclaimed early.

---

## Internals not part of the public API

- `ungula::hal::gpio::detail::isValidGpio`, `ungula::hal::gpio::detail::isValidOutputGpio` — pin-bitmask helpers used by the checked variants. Not part of the surface.
- `AdcManager` private members (`ChannelInfo`, `CaliEntry`, `channels_`, `units_`, `cali_`, `ensureUnit`, `ensureCalibration`, `findChannel`, `unitToIndex`, `attenToIndex`, `toIdfAttenuation`, `fallbackFullScaleMv`, `rawToMvFallback`) — implementation detail. Use only the public methods.
- `I2cMaster::installed_`, `Uart::installed_`, `Uart::port_`, `I2cMaster::port_`, `SpiMaster::installed_`, `SpiMaster::devHandle_` — private state.
- `timer/platforms/hwtimer_esp32.cpp` and `timer/platforms/hwtimer_default.cpp` — backend internals. Keep callers on `IHwTimer` / `drivers::HwTimer`.
- Files under `src/ungula/hal/*/platforms/` — picked at compile time by the bridge headers (`gpio.h`, `adc_manager.h`) or by build glob (`i2c_master_*.cpp`, `spi_master_*.cpp`, `uart_*.cpp`). Do not include the platform headers directly; always go through the bridge header or `<ungula/hal.h>`.
- `gpio_pwm_esp32.cpp` — owner of the LEDC channel pool. Do not poke it from outside.
- `library.properties` lists `architectures=esp32`, but the default-platform stubs allow host unit-test builds. The stub backend is for tests only — its return values are not a contract.

---

## Recommended improvements (proposed — not yet implemented)

The HAL is generally deep at the subsystem level, but a few sharp edges leak:

- **Unify GPIO checked/unchecked under a typed `Pin` handle (proposed).** A `class Pin` holding a validated pin number, returned from `configOutput()` / `configInput()`, would remove the `uint8_t pin` discipline and let unchecked calls become member methods that are statically guaranteed to be on a configured pin.
- **`I2cMaster::scan()` (proposed).** Bus discovery is the single most common diagnostic; today every caller writes its own loop over 0x03..0x77.
- **Promote `installIsrService()` to an internal lazy call (proposed).** Today every caller has to remember to call it before `addIsrHandler()`. Folding it into `addIsrHandler()` would shrink the surface by one symbol.
- **Drop `writeHigh()` / `writeLow()` (proposed).** They are exact aliases of `setHigh()` / `setLow()` and double the surface for no value.
- **A single platform-portable `IAdcManager` interface (proposed).** Today the desktop stub and the ESP32 class are independent definitions sharing only a coding convention. A common abstract base would make stubbing in tests explicit and prevent silent drift.
- **Expose I2C / SPI clock-stretching and DMA buffer hints (proposed).** The ESP-IDF backends support both; the wrapper currently hides them.

These are notes for future work. Do not assume any of them exist.

---

## LLM usage rules

- Use only the symbols documented above. If the task seems to need something else (e.g. SPI half-duplex with explicit timing, ADC continuous mode, UART parity), say so explicitly — do not invent a function.
- Always go through the bridge headers (`<ungula/hal/gpio/gpio.h>`, `<ungula/hal/adc/adc_manager.h>`) or the chain header `<ungula/hal.h>`. Never include `platforms/*` directly.
- Do not call `Arduino.h` APIs (`millis`, `digitalWrite`, `pinMode`, `Serial.print*`, `String`) anywhere. Use this library plus the project's `ungula::core::time` and `string_t`.
- Configure pins / ports / channels in `setup()`. The hot path (`loop()` / tasks / ISRs) must not allocate or reconfigure.
- Prefer the unchecked GPIO functions inside ISRs and timing-critical loops; prefer the checked variants when the pin number is from configuration.
- ISR handlers must be tagged `UNGULA_ISR_ATTR`.
- One owner per `Uart` / `I2cMaster` / `SpiMaster` / `Can` / `PwmInput` / `Decoder`. Do not pass copies; pass references.
- For drivers that consume an interface (`IMultiplexer`, `IPwmInput`, `IDecoder`), the host owns the concrete instance and the driver borrows it by reference. Do not let the lifetime of the consumer driver exceed the lifetime of the underlying peripheral.
- Do not log from this library or wrap it in code that does — the HAL is logging-free by project policy.
