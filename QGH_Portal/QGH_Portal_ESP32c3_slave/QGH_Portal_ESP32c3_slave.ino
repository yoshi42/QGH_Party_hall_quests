#include <FastLED.h>
#include <HardwareSerial.h>
HardwareSerial NanoSerial(1);

// ---------- LED CONFIG ----------
#define LED_PIN 9
#define NUM_LEDS 216
#define LED_TYPE WS2812
#define COLOR_ORDER GRB
#define DEFAULT_BRIGHTNESS 200

CRGB leds[NUM_LEDS];

// ---------- STATE ----------
bool isGameMode = false;
bool winActive = false;

// from Nano encoder controller
int e1 = 0, e2 = 0, e3 = 0, e4 = 0;
int rawEnc[4] = {0, 0, 0, 0};    // last raw values from Nano
int ctrlEnc[4] = {0, 0, 0, 0};   // 0..255 control values for R,G,B,brightness
const int ENCODER_STEP = 10;     // step per encoder tick
int btnMode = 0;

// game parameters
int pos = 0;
int speed = 5;
unsigned long lastMove = 0;

int R = 0, G = 0, B = 0;
int brightness = 150;

// WIN EFFECT
unsigned long winStart = 0;
const unsigned long WIN_DURATION = 5000;

// parsing buffer
String line = "";

// ------------------------------------------------------------
// SETUP
// ------------------------------------------------------------
void setup() {
  Serial.begin(115200);

  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(DEFAULT_BRIGHTNESS);

  NanoSerial.begin(115200, SERIAL_8N1, 4, -1);
  Serial.println("Slave: listening on UART1 RX=4");
  // --- DEBUG STARTUP BLINK (RED, 5 seconds) ---
  for (int i = 0; i < 10; i++) {          // 25 cycles × 200 ms = 5 seconds
    fill_solid(leds, NUM_LEDS, CRGB(255, 0, 0));
    FastLED.show();
    delay(100);
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    FastLED.show();
    delay(100);
  }
}


// ------------------------------------------------------------
// PARSE UART LINE
// ------------------------------------------------------------
void parseLine(String &msg) {
  Serial.print("UART RX: ");
  Serial.println(msg);

  // WIN FROM MASTER
  if (msg.startsWith("{M")) {
    int idx = msg.indexOf("WIN:");
    if (idx > 0) {
      int winFlag = msg.substring(idx + 4).toInt();
      if (winFlag == 1) {
        winActive = true;
        winStart = millis();
        Serial.println("WIN from MASTER!");
      }
    }
    return;
  }

  // NANO ENCODER DATA (incremental mapping)
  if (msg.startsWith("E1:")) {
    int idx;
    // E1
    idx = msg.indexOf("E1:");
    if (idx >= 0) {
      int val = msg.substring(idx + 3).toInt();
      if (val > rawEnc[0])      ctrlEnc[0] += ENCODER_STEP;
      else if (val < rawEnc[0]) ctrlEnc[0] -= ENCODER_STEP;
      rawEnc[0] = val;
    }
    // E2
    idx = msg.indexOf("E2:");
    if (idx >= 0) {
      int val = msg.substring(idx + 3).toInt();
      if (val > rawEnc[1])      ctrlEnc[1] += ENCODER_STEP;
      else if (val < rawEnc[1]) ctrlEnc[1] -= ENCODER_STEP;
      rawEnc[1] = val;
    }
    // E3
    idx = msg.indexOf("E3:");
    if (idx >= 0) {
      int val = msg.substring(idx + 3).toInt();
      if (val > rawEnc[2])      ctrlEnc[2] += ENCODER_STEP;
      else if (val < rawEnc[2]) ctrlEnc[2] -= ENCODER_STEP;
      rawEnc[2] = val;
    }
    // E4
    idx = msg.indexOf("E4:");
    if (idx >= 0) {
      int val = msg.substring(idx + 3).toInt();
      if (val > rawEnc[3])      ctrlEnc[3] += ENCODER_STEP;
      else if (val < rawEnc[3]) ctrlEnc[3] -= ENCODER_STEP;
      rawEnc[3] = val;
    }

    // clamp control values to 0..255
    for (int i = 0; i < 4; i++) {
      if (ctrlEnc[i] < 0) ctrlEnc[i] = 0;
      if (ctrlEnc[i] > 255) ctrlEnc[i] = 255;
    }

    // mirror to e1..e4 for debugging
    e1 = ctrlEnc[0];
    e2 = ctrlEnc[1];
    e3 = ctrlEnc[2];
    e4 = ctrlEnc[3];

    // BTN
    idx = msg.indexOf("BTN:");
    if (idx >= 0) btnMode = msg.substring(idx + 4).toInt();

    Serial.print("Parsed encoders -> ");
    Serial.print("E1="); Serial.print(e1);
    Serial.print(" E2="); Serial.print(e2);
    Serial.print(" E3="); Serial.print(e3);
    Serial.print(" E4="); Serial.print(e4);
    Serial.print(" BTN="); Serial.println(btnMode);
  }
}


// ------------------------------------------------------------
// PASSIVE MODE
// ------------------------------------------------------------
void runPassive() {
  int r = ctrlEnc[0];
  int g = ctrlEnc[1];
  int b = ctrlEnc[2];
  brightness = ctrlEnc[3];

  FastLED.setBrightness(brightness);
  fill_solid(leds, NUM_LEDS, CRGB(r, g, b));
  FastLED.show();
}


// ------------------------------------------------------------
// GAME MODE
// ------------------------------------------------------------
void sendGamePacket() {
  String out = "{S MODE:1 POS:";
  out += pos;
  out += " R:";  out += R;
  out += " G:";  out += G;
  out += " B:";  out += B;
  out += " SPD:"; out += speed;
  out += "}";

  NanoSerial.println(out);

  Serial.print("UART TX: ");
  Serial.println(out);
}

void runGame() {
  R = ctrlEnc[0];
  G = ctrlEnc[1];
  B = ctrlEnc[2];

  int rawSpeed = ctrlEnc[3];
  speed = map(rawSpeed, 0, 255, 0, 20);
  if (speed < 0) speed = 0;
  if (speed > 20) speed = 20;

  // stable interval calculation that never goes negative
  unsigned long now = millis();
  unsigned long interval = 40 - (speed * 2);  // speed=0 → interval=40
  if (interval < 5) interval = 5;             // clamp to avoid freeze

if (speed == 0) {
    // freeze — не рухаємо pos
} else {
    if (now - lastMove >= interval) {
        lastMove = now;
        pos = (pos + 1) % NUM_LEDS;
    }
}

  fill_solid(leds, NUM_LEDS, CRGB::Black);

  const int TAIL = 10;
  for (int i = 0; i < TAIL; i++) {
    int p = (pos - i + NUM_LEDS) % NUM_LEDS;
    uint8_t fade = 255 - (i * (255 / TAIL));
    leds[p] = CRGB(R, G, B).nscale8(fade);
  }

  FastLED.setBrightness(180);
  FastLED.show();

  sendGamePacket();
}


// ------------------------------------------------------------
// WIN EFFECT
// ------------------------------------------------------------
void runWinEffect() {
  unsigned long now = millis();
  if (now - winStart > WIN_DURATION) {
    winActive = false;
    return;
  }

  uint8_t hue = (now / 5) % 255;
  fill_solid(leds, NUM_LEDS, CHSV(hue, 255, 255));
  FastLED.show();
}


// ------------------------------------------------------------
// LOOP
// ------------------------------------------------------------
void loop() {

  // READ HC-12
  while (NanoSerial.available()) {
    char h = NanoSerial.read();
    if (h == '\n') {
      parseLine(line);
      line = "";
    } else if (h != '\r') {
      line += h;
      if (line.length() > 200) line = "";
    }
  }

  // MODE TOGGLE
  if (btnMode == 1) {
    isGameMode = !isGameMode;
    Serial.print("Mode changed: isGameMode=");
    Serial.println(isGameMode);
    if (isGameMode) {
      R = random(0,256);
      G = random(0,256);
      B = random(0,256);
      Serial.println("Random game color assigned");
    }
    delay(200);
  }

  // PRIORITY: WIN EFFECT
  if (winActive) {
    runWinEffect();
    return;
  }

  if (!isGameMode) runPassive();
  else runGame();
}