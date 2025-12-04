/*
=============================================================
  UNIVERSAL NANO FIRMWARE for QGH Portals
=============================================================

Функції:
- Читання 4 енкодерів (A/B)
- Читання кнопки
- Читання HC-12 (SoftwareSerial)
- Формування ОДНОГО пакета:
  E1:x E2:x E3:x E4:x BTN:x HC:{...}

Ця прошивка однакова і для MASTER, і для SLAVE!
=============================================================
*/

#include <Arduino.h>
#include <SoftwareSerial.h>

// -----------------------------------------------------------
// HC-12 → Nano
// -----------------------------------------------------------
#define HC_RX 2
#define HC_TX 3
SoftwareSerial HC12(HC_RX, HC_TX);

// -----------------------------------------------------------
// Encoder pins
// -----------------------------------------------------------
struct EncPins { uint8_t A; uint8_t B; };

#define ENC1_A 12
#define ENC1_B 11
#define ENC2_A 10
#define ENC2_B  9
#define ENC3_A  8
#define ENC3_B  7
#define ENC4_A  4
#define ENC4_B  5

EncPins enc[4] = {
  {ENC1_A, ENC1_B},
  {ENC2_A, ENC2_B},
  {ENC3_A, ENC3_B},
  {ENC4_A, ENC4_B}
};

#define BTN_PIN 6

// -----------------------------------------------------------
// Variables
// -----------------------------------------------------------
int val[4] = {0,0,0,0};
int lastA[4];

bool lastBtn = HIGH;
unsigned long lastBtnTime = 0;
#define BTN_DEBOUNCE 80

unsigned long lastSend = 0;
#define SEND_INTERVAL 35

String hcBuffer = "";

// -----------------------------------------------------------
// Update encoder
// -----------------------------------------------------------
void updateEncoder(uint8_t id) {
  int a = digitalRead(enc[id].A);
  int b = digitalRead(enc[id].B);

  if (a != lastA[id]) {
    if (a == HIGH) {
      if (b == LOW) val[id] += 1;
      else          val[id] -= 1;
    }
    lastA[id] = a;
  }
}

// -----------------------------------------------------------
// Read button
// -----------------------------------------------------------
int readButton() {
  bool raw = digitalRead(BTN_PIN);

  if (raw != lastBtn && millis() - lastBtnTime > BTN_DEBOUNCE) {
    lastBtnTime = millis();
    lastBtn = raw;

    if (raw == LOW) return 1;
  }
  return 0;
}

// -----------------------------------------------------------
// Read HC-12 data (MASTER/SLAVE packets)
// -----------------------------------------------------------
String readHC12() {
  while (HC12.available()) {
    char c = HC12.read();
    if (c == '\n') {
      String out = hcBuffer;
      hcBuffer = "";
      return out;             // return full packet
    } else if (c != '\r') {
      hcBuffer += c;
      if (hcBuffer.length() > 200) hcBuffer = "";
    }
  }
  return "";
}

// -----------------------------------------------------------
// Send unified packet to ESP32-C3
// -----------------------------------------------------------
void sendPacket(int btn, String hcData) {
  Serial.print("E1:"); Serial.print(val[0]);
  Serial.print(" E2:"); Serial.print(val[1]);
  Serial.print(" E3:"); Serial.print(val[2]);
  Serial.print(" E4:"); Serial.print(val[3]);
  Serial.print(" BTN:"); Serial.print(btn);

  Serial.print(" HC:");
  if (hcData.length() > 0) Serial.print(hcData);
  else Serial.print("{}");

  Serial.println();
}

// -----------------------------------------------------------
// setup
// -----------------------------------------------------------
void setup() {
  Serial.begin(115200);
  HC12.begin(9600);

  // --- STARTUP ENCODER EMULATION (simulate blue channel changes) ---
  val[0] = 0;
  val[1] = 0;
  val[3] = 0;
  for (int i = 0; i < 10; i++) {
    // Toggle encoder 3 value between 0 and 255
    val[2] = (i % 2 == 0) ? 0 : 255;
    sendPacket(0, "");
    delay(500);
  }

  for (int i=0; i<4; i++) {
    pinMode(enc[i].A, INPUT_PULLUP);
    pinMode(enc[i].B, INPUT_PULLUP);
    lastA[i] = digitalRead(enc[i].A);
  }

  pinMode(BTN_PIN, INPUT_PULLUP);
}

// -----------------------------------------------------------
// main loop
// -----------------------------------------------------------
void loop() {

  // Read encoders
  for (int i=0; i<4; i++) updateEncoder(i);

  // Button
  int btn = readButton();

  // HC-12 receive
  String hc = readHC12();

  // Send data
  if (millis() - lastSend > SEND_INTERVAL || btn == 1 || hc.length() > 0) {
    lastSend = millis();
    sendPacket(btn, hc);
  }
}