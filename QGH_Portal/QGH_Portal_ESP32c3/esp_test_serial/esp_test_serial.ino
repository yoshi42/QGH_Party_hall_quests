#define RX_PIN 3  // підключений до Arduino TX
HardwareSerial mySerial(1); // UART1

void setup() {
  Serial.begin(115200);                  // монітор порту
  mySerial.begin(115200, SERIAL_8N1, RX_PIN, -1); // RX = 3, TX не підключений
  delay(100);
}

void loop() {
  while (mySerial.available()) {
    char c = mySerial.read();
    Serial.write(c); // відображаємо в моніторі ESP
  }
}