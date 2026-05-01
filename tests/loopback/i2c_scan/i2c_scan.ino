// I2C bus scan test for bare ESP32.
// No external wiring needed — just verifies the I2C bus initialises.
// If you have devices connected, it reports their addresses.
// With nothing connected, you should see 0 devices (confirms the bus is alive
// and not stuck).

#include <ungula_hal.h>

static constexpr uint8_t SDA_PIN = 21;
static constexpr uint8_t SCL_PIN = 22;

ungula::i2c::I2cMaster bus(0);

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("=== I2C Bus Scan ===");
    Serial.printf("SDA: GPIO %d, SCL: GPIO %d\n", SDA_PIN, SCL_PIN);
    Serial.println();

    if (!bus.begin(SDA_PIN, SCL_PIN, 100000)) {
        Serial.println("FAIL: bus.begin() returned false");
        return;
    }
    Serial.println("PASS: bus.begin()");

    // Scan all 7-bit addresses (0x08–0x77).
    int found = 0;
    for (uint8_t addr = 0x08; addr <= 0x77; ++addr) {
        // A zero-length write is the standard I2C scan probe.
        if (bus.write(addr, nullptr, 0, 10)) {
            Serial.printf("  Device found at 0x%02X\n", addr);
            ++found;
        }
    }

    Serial.printf("\nDevices found: %d\n", found);
    if (found == 0) {
        Serial.println("INFO: no devices — that's normal on a bare board");
    }
    Serial.println("=== Done ===");
}

void loop() {}
