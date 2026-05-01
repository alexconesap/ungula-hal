// UART loopback test for bare ESP32.
// Wire: connect TX2 (GPIO 17) to RX2 (GPIO 16) with a jumper wire.
// Opens UART2, sends a known pattern, reads it back and verifies.

#include <ungula_hal.h>

static constexpr uint8_t TX_PIN = 17;
static constexpr uint8_t RX_PIN = 16;
static constexpr uint32_t BAUD = 115200;

ungula::uart::Uart uart2(2);

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("=== UART Loopback Test ===");
    Serial.println("Wiring: GPIO 17 (TX2) -> GPIO 16 (RX2)");
    Serial.println();

    if (!uart2.begin(BAUD, TX_PIN, RX_PIN)) {
        Serial.println("FAIL: uart2.begin() returned false");
        return;
    }
    Serial.println("PASS: uart2.begin()");

    // Send test pattern.
    const uint8_t pattern[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x42};
    int32_t written = uart2.write(pattern, sizeof(pattern));
    Serial.printf("Sent %d bytes\n", written);

    uart2.flush();
    delay(50);

    // Read back.
    uint8_t rx[16] = {};
    int32_t received = uart2.read(rx, sizeof(rx), 200);
    Serial.printf("Received %d bytes\n", received);

    if (received != static_cast<int32_t>(sizeof(pattern))) {
        Serial.printf("FAIL: expected %d bytes, got %d\n", sizeof(pattern), received);
        return;
    }

    bool match = true;
    for (size_t i = 0; i < sizeof(pattern); ++i) {
        if (rx[i] != pattern[i]) {
            Serial.printf("FAIL: byte[%d] expected 0x%02X, got 0x%02X\n", i, pattern[i], rx[i]);
            match = false;
        }
    }

    if (match) {
        Serial.println("PASS: all bytes match");
    }
    Serial.println("=== Done ===");
}

void loop() {}
