#include <HardwareSerial.h>

#define RX_PIN 4
#define TX_PIN -1  // поки тільки приймаємо

HardwareSerial NanoSerial(1);

void setup() {
  Serial.begin(115200);
  NanoSerial.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);
  Serial.println("ESP32C3 ready, listening on RX=4");
}

void loop() {
  while (NanoSerial.available()) {
    char c = NanoSerial.read();
    Serial.print(c); // дублюємо на USB
  }
}