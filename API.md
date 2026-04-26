# UngulaHal

Embedded C++17 hardware abstraction layer (HAL) for ESP32-class MCUs. Wraps GPIO, PWM, ADC, UART, I2C, and SPI behind stable C++ interfaces in the `ungula::` namespace, so higher-level libraries (motor, loadcell, sd, etc.) compile against the abstractions instead of vendor SDKs. ESP32 backend uses `gpio_ll`, LEDC, `esp_adc`, ESP-IDF `uart_driver`, ESP-IDF legacy `driver/i2c.h`, and `driver/spi_master.h`. Non-ESP32 builds get no-op stubs that compile and link for host unit tests.

This library is the only place direct Arduino / vendor APIs are allowed. Everything else in the project must call the symbols documented here.

---

## Usage

### Use case: blink an LED with checked configuration, unchecked hot-path writes

```cpp
#include <ungula_hal.h>
#include <hal/gpio/gpio_access.h>

constexpr uint8_t LED_PIN = 2;

void setup() {
    ungula::gpio::configOutput(LED_PIN);
    ungula::gpio::setLow(LED_PIN);
}

void loop() {
    ungula::gpio::toggle(LED_PIN);
    // pacing handled by the project's TimeControl, not delay()
}
```

When to use this: any digital output where the pin is known at compile time and was validated by `configOutput()` at boot. `toggle()` is read-then-write, not atomic — synchronise externally if shared with an ISR.

### Use case: read a button with pull-up and a falling-edge interrupt

```cpp
#include <hal/gpio/gpio_access.h>

constexpr uint8_t BUTTON_PIN = 4;

void UNGULA_ISR_ATTR onButton(void* /*ctx*/) {
    // Keep ISR body short. Signal a task/queue from here.
}

void setup() {
    using ungula::gpio::InterruptEdge;
    using ungula::gpio::PullMode;

    ungula::gpio::configInputInterrupt(BUTTON_PIN, InterruptEdge::EDGE_FALLING, PullMode::UP);
    ungula::gpio::installIsrService();
    ungula::gpio::addIsrHandler(BUTTON_PIN, &onButton, nullptr);
}

void loop() {}
```

When to use this: any input that must wake on edge events without polling. `installIsrService()` is idempotent — calling it more than once is fine.

### Use case: PWM fan / LED dimming via LEDC

```cpp
#include <hal/gpio/gpio_access.h>

constexpr uint8_t FAN_PIN = 25;

void setup() {
    ungula::gpio::configPwm(FAN_PIN, /*freqHz=*/25000, /*resolutionBits=*/8);
    ungula::gpio::writePwm(FAN_PIN, 128);  // 50% duty at 8-bit resolution
}

void loop() {}
```

When to use this: fans, LED dimming, or any frequency-controlled output. `configPwm()` is single-assignment — a second call for the same pin returns false. Duty range is `0..(2^resolutionBits - 1)`.

### Use case: ADC read with calibration

```cpp
#include <hal/adc/adc_manager.h>

ungula::adc::AdcManager adc;

constexpr uint8_t SENSE_PIN = 34;
constexpr uint8_t REF_PIN = 35;

void setup() {
    adc.configure(SENSE_PIN);                                // default DB_12 → 0..3.3 V
    adc.configure(REF_PIN, ungula::adc::Attenuation::DB_6);  // 0..1.75 V, finer resolution
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
#include <hal/i2c/i2c_master.h>

ungula::i2c::I2cMaster bus(0);  // I2C port 0

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
#include <hal/spi/spi_master.h>

ungula::spi::SpiMaster spi;

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
#include <hal/uart/uart.h>

ungula::uart::Uart uart(2);  // ESP32 UART port 2

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

---

## API

Public namespaces:

- `ungula::gpio` — digital I/O, edge interrupts, PWM (free functions)
- `ungula::adc` — `AdcManager` class, `Attenuation`, `CaliScheme`
- `ungula::i2c` — `I2cMaster` class
- `ungula::spi` — `SpiMaster` class
- `ungula::uart` — `Uart` class, `DEFAULT_RX_BUF`, `DEFAULT_TX_BUF`

Top-level chain header: `<ungula_hal.h>`. Including it pulls in every subsystem header. For finer-grained includes, use the per-subsystem headers shown in the use cases.

---

## Public types

### `ungula::gpio::InterruptEdge`

```cpp
enum class InterruptEdge : uint8_t { EDGE_RISING = 0, EDGE_FALLING = 1, EDGE_ANY = 2 };
```

Edge selection for `configInputInterrupt()`.

### `ungula::gpio::PullMode`

```cpp
enum class PullMode : uint8_t { NONE = 0, UP = 1, DOWN = 2 };
```

Internal pull-resistor selection for interrupt-enabled inputs. Default for `configInputInterrupt()` is `NONE` (assume external pull).

### `ungula::gpio::GpioIsrHandler`

```cpp
using GpioIsrHandler = void (*)(void*);
```

ISR callback signature. Place the function with `UNGULA_ISR_ATTR` so it lands in IRAM and survives flash operations.

### `UNGULA_ISR_ATTR`

Preprocessor macro. On ESP32 expands to `IRAM_ATTR`; on the default backend expands to nothing. Apply to any function called from ISR context.

### `ungula::adc::Attenuation`

```cpp
enum class Attenuation : uint8_t {
    DB_0   = 0,  // ~0..950 mV
    DB_2_5 = 1,  // ~0..1.25 V
    DB_6   = 2,  // ~0..1.75 V
    DB_12  = 3,  // ~0..3.3 V (default)
};
```

Per-channel attenuation. The class tracks one calibration handle per `(unit, attenuation)` pair, so mixing attenuations on the same ADC unit is correct.

### `ungula::adc::CaliScheme`

```cpp
enum class CaliScheme : uint8_t { None, CurveFitting, LineFitting };
```

Internal record of which calibration scheme the driver picked at runtime. Not used by callers; exposed for completeness.

### `ungula::adc::AdcManager`

Owning class for the ADC peripheral. Non-copyable. Fields are private.

Public constants:

- `static constexpr size_t MAX_CHANNELS` — capacity (default 16, override with `-DUNGULA_HAL_MAX_ADC_CHANNELS=N`).
- `static constexpr size_t UNIT_COUNT = 2` (ESP32 backend only).
- `static constexpr size_t ATTEN_COUNT = 4` (ESP32 backend only).

### `ungula::uart::Uart`

Owning class for one hardware UART port. Non-copyable. `port()` returns the index passed at construction.

Constants:

- `constexpr uint16_t ungula::uart::DEFAULT_RX_BUF = 256;`
- `constexpr uint16_t ungula::uart::DEFAULT_TX_BUF = 0;` — `0` means blocking writes (no TX ring buffer).

### `ungula::i2c::I2cMaster`

Owning class for one I2C master port. Non-copyable. `port()` returns the index passed at construction.

### `ungula::spi::SpiMaster`

Owning class for one SPI device on a bus. Non-copyable. One instance per chip-select line.

---

## Public functions / methods

### GPIO — pin configuration (always validated)

```cpp
bool ungula::gpio::configOutput(uint8_t pin);
bool ungula::gpio::configInput(uint8_t pin);
bool ungula::gpio::configInputPullup(uint8_t pin);
bool ungula::gpio::configInputPulldown(uint8_t pin);
bool ungula::gpio::configOutputOpenDrain(uint8_t pin);
```

- **Purpose** — set direction and pull state in one atomic ESP-IDF call.
- **Returns** — `true` on success, `false` for an invalid pin or driver error.
- **Side effects** — writes ESP32 IO_MUX / GPIO matrix registers.
- **Failure behavior** — return `false`; does not throw.
- **Usage notes** — call once per pin at boot before any unchecked read/write.

### GPIO — unchecked digital I/O (hot-path)

```cpp
bool ungula::gpio::read(uint8_t pin);
void ungula::gpio::setHigh(uint8_t pin);
void ungula::gpio::setLow(uint8_t pin);
void ungula::gpio::write(uint8_t pin, bool high);
void ungula::gpio::writeHigh(uint8_t pin);
void ungula::gpio::writeLow(uint8_t pin);
void ungula::gpio::toggle(uint8_t pin);
bool ungula::gpio::isHigh(uint8_t pin);
bool ungula::gpio::isLow(uint8_t pin);
```

- **Purpose** — single-cycle GPIO access for ISRs, timers, PID loops.
- **Side effects** — direct W1TS/W1TC register writes via `gpio_ll`.
- **Failure behavior** — none. No validation. Caller must have configured the pin first.
- **Usage notes** — `toggle()` is *not* atomic (read followed by write). Synchronise externally if the pin is shared between ISR and task context.

### GPIO — checked digital I/O (untrusted pin source)

```cpp
bool ungula::gpio::checkedRead(uint8_t pin, bool& out);
bool ungula::gpio::checkedSetHigh(uint8_t pin);
bool ungula::gpio::checkedSetLow(uint8_t pin);
bool ungula::gpio::checkedWrite(uint8_t pin, bool high);
```

- **Purpose** — same as the unchecked variants but validate the pin against the SoC bitmask first.
- **Returns** — `true` if the pin is a valid (output-capable, where relevant) GPIO and the operation succeeded; `false` otherwise. `checkedRead()` only writes to `out` on success.
- **Usage notes** — use when the pin number comes from configuration files, runtime input, or anywhere not verified at compile time.

### GPIO — interrupts

```cpp
bool ungula::gpio::configInputInterrupt(uint8_t pin,
                                        ungula::gpio::InterruptEdge edge,
                                        ungula::gpio::PullMode pull = ungula::gpio::PullMode::NONE);
bool ungula::gpio::installIsrService();
bool ungula::gpio::addIsrHandler(uint8_t pin, ungula::gpio::GpioIsrHandler handler, void* context);
bool ungula::gpio::removeIsrHandler(uint8_t pin);
```

- **Purpose** — wire a GPIO to an interrupt vector and attach a handler.
- **Returns** — `true` on success; `installIsrService()` also returns `true` if the service is already installed (`ESP_ERR_INVALID_STATE` is treated as success).
- **Side effects** — installs the global GPIO ISR service on first call; registers the per-pin handler.
- **Usage notes** — handlers must be marked `UNGULA_ISR_ATTR`. Keep them short — defer real work to a task via a queue or notification.

### GPIO — PWM

```cpp
bool ungula::gpio::configPwm(uint8_t pin, uint32_t freqHz = 1000, uint8_t resolutionBits = 8);
bool ungula::gpio::writePwm(uint8_t pin, uint32_t duty);
```

- **Purpose** — assign an LEDC channel to the pin and drive duty cycle.
- **Returns** — `true` on success. `configPwm()` is single-assignment per pin: a second call for the same pin returns `false`.
- **Side effects** — claims one LEDC channel per pin from a finite pool. State lives in `gpio_pwm_esp32.cpp` (single TU, shared across the firmware).
- **Failure behavior** — channel pool exhaustion or invalid pin → `false`. `writePwm()` clamps duty to `(2^resolutionBits - 1)` to avoid hardware overflow.

### `ungula::adc::AdcManager`

```cpp
bool configure(uint8_t pin, ungula::adc::Attenuation atten = ungula::adc::Attenuation::DB_12);
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

### `ungula::i2c::I2cMaster`

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

### `ungula::spi::SpiMaster`

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

### `ungula::uart::Uart`

```cpp
explicit Uart(uint8_t portNumber);
~Uart();
bool begin(uint32_t baudRate, uint8_t txPin, uint8_t rxPin,
           uint16_t rxBufSize = ungula::uart::DEFAULT_RX_BUF,
           uint16_t txBufSize = ungula::uart::DEFAULT_TX_BUF);
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

1. **Boot (`setup()`):** call every `config*()` (GPIO), `configPwm()`, `AdcManager::configure()`, `I2cMaster::begin()`, `SpiMaster::begin()`, `Uart::begin()` you need. All of these may allocate driver state — heap allocation after `setup()` is forbidden by project rules, so do it here.
2. **Operate (`loop()` / tasks):** call the unchecked read/write helpers, `readMv()`, `read()`/`write()`/`writeRead()`. These do not allocate.
3. **Shutdown:** destructors of `AdcManager`, `Uart`, `I2cMaster`, `SpiMaster` release their drivers. `AdcManager::deinit()` is idempotent — call it explicitly only when reordering matters.

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
- **ADC2 + Wi-Fi:** classic ESP32 ADC2 channels conflict with the Wi-Fi PHY. Prefer ADC1 (GPIO 32–39) when the radio is active.
- **PWM (LEDC):** a finite number of channels is shared across the firmware. `configPwm()` is single-assignment per pin and consumes one channel; do not call it in a loop.
- **No heap after `setup()`:** all `begin()` / `configure()` / `configPwm()` calls must happen during initialization.

---

## Internals not part of the public API

- `ungula::gpio::detail::isValidGpio`, `ungula::gpio::detail::isValidOutputGpio` — pin-bitmask helpers used by the checked variants. Not part of the surface.
- `AdcManager` private members (`ChannelInfo`, `CaliEntry`, `channels_`, `units_`, `cali_`, `ensureUnit`, `ensureCalibration`, `findChannel`, `unitToIndex`, `attenToIndex`, `toIdfAttenuation`, `fallbackFullScaleMv`, `rawToMvFallback`) — implementation detail. Use only the public methods.
- `I2cMaster::installed_`, `Uart::installed_`, `Uart::port_`, `I2cMaster::port_`, `SpiMaster::installed_`, `SpiMaster::devHandle_` — private state.
- Files under `src/hal/*/platforms/` — picked at compile time by the bridge headers (`gpio_access.h`, `adc_manager.h`) or by build glob (`i2c_master_*.cpp`, `spi_master_*.cpp`, `uart_*.cpp`). Do not include the platform headers directly; always go through the bridge header or `<ungula_hal.h>`.
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
- Always go through the bridge headers (`<hal/gpio/gpio_access.h>`, `<hal/adc/adc_manager.h>`) or the chain header `<ungula_hal.h>`. Never include `platforms/*` directly.
- Do not call `Arduino.h` APIs (`millis`, `digitalWrite`, `pinMode`, `Serial.print*`, `String`) anywhere. Use this library plus the project's `ungula::TimeControl` and `string_t`.
- Configure pins / ports / channels in `setup()`. The hot path (`loop()` / tasks / ISRs) must not allocate or reconfigure.
- Prefer the unchecked GPIO functions inside ISRs and timing-critical loops; prefer the checked variants when the pin number is from configuration.
- ISR handlers must be tagged `UNGULA_ISR_ATTR`.
- One owner per `Uart` / `I2cMaster` / `SpiMaster`. Do not pass copies; pass references.
- Do not log from this library or wrap it in code that does — the HAL is logging-free by project policy.
