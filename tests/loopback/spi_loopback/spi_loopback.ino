// SPI loopback test for bare ESP32.
// Wire: connect MOSI (GPIO 23) to MISO (GPIO 19) with a jumper wire.
// CS (GPIO 5) left unconnected (driven by the SPI peripheral).
// Sends a pattern via MOSI, reads it back on MISO, and verifies.

#include <ungula_hal.h>

static constexpr uint8_t SCLK_PIN = 18;
static constexpr uint8_t MISO_PIN = 19;
static constexpr uint8_t MOSI_PIN = 23;
static constexpr uint8_t CS_PIN = 5;

ungula::spi::SpiMaster spi;

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("=== SPI Loopback Test ===");
  Serial.println("Wiring: GPIO 23 (MOSI) -> GPIO 19 (MISO)");
  Serial.println();

  if (!spi.begin(SCLK_PIN, MISO_PIN, MOSI_PIN, CS_PIN, 1000000, 0)) {
    Serial.println("FAIL: spi.begin() returned false");
    return;
  }
  Serial.println("PASS: spi.begin()");

  // Full-duplex loopback: what we send on MOSI appears on MISO.
  const uint8_t tx[] = {0xCA, 0xFE, 0xBA, 0xBE};
  uint8_t rx[4] = {};

  if (!spi.transfer(tx, rx, 4)) {
    Serial.println("FAIL: spi.transfer() returned false");
    return;
  }

  bool match = true;
  for (size_t i = 0; i < 4; ++i) {
    Serial.printf("  TX: 0x%02X  RX: 0x%02X  %s\n", tx[i], rx[i],
                  (tx[i] == rx[i]) ? "OK" : "MISMATCH");
    if (tx[i] != rx[i]) {
      match = false;
    }
  }

  if (match) {
    Serial.println("PASS: full-duplex loopback");
  } else {
    Serial.println("FAIL: data mismatch");
  }

  // writeRead test: write 2 bytes, then read 2 bytes.
  // With loopback, the read phase sees zeros (MOSI is low during read clocks)
  // but the write phase data should not corrupt anything.
  uint8_t cmd[] = {0x55, 0xAA};
  uint8_t readBuf[2] = {0xFF, 0xFF};
  if (!spi.writeRead(cmd, 2, readBuf, 2)) {
    Serial.println("FAIL: spi.writeRead() returned false");
    return;
  }
  Serial.printf("  writeRead read phase: 0x%02X 0x%02X (expected 0x00 0x00)\n",
                readBuf[0], readBuf[1]);
  if (readBuf[0] == 0x00 && readBuf[1] == 0x00) {
    Serial.println("PASS: writeRead read phase zeros");
  } else {
    Serial.println("INFO: non-zero read — check wiring");
  }

  Serial.println("=== Done ===");
}

void loop() {}
