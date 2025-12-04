#include <FastLED.h>

#define LED_PIN_PORTAL1 9
#define NUM_LEDS 216
#define BRIGHTNESS 200
#define GAME_BRIGHTNESS 155

#define LED_TYPE WS2812
#define COLOR_ORDER GRB

#define RX_PIN 4
#define TX_PIN -1

#define ENCODER_STEP 10

const int TAIL_LENGTH = 10;
const int POSITION_TOLERANCE = 8;
const int COLOR_TOLERANCE = 100;

CRGB leds1[NUM_LEDS];

HardwareSerial NanoSerial(1);
HardwareSerial HC12(2);

// Slave state received from SLAVE via HC12 / direct cable
int slavePos = 0;
int slaveR = 0;
int slaveG = 0;
int slaveB = 0;
int slaveSpeed = 0;
bool slaveMode = false;

// local encoders
long encoders[8] = {0};
long lastEncoders[8] = {0};

bool btn1State = false;

enum PortalState {
  PASSIVE,
  GAME,
  WIN_EFFECT
};

PortalState statePortal1 = PASSIVE;

unsigned long winStartTime = 0;
bool winTimerActive = false;

int gamePos1 = 0;
unsigned long lastUpdateTime1 = 0;

// ---------------- PARSE NANO UART ----------------
void parseUARTData(String &line) {
  int startIdx = 0;
  while (startIdx < line.length()) {

    int spaceIdx = line.indexOf(' ', startIdx);
    if (spaceIdx == -1) spaceIdx = line.length();

    String token = line.substring(startIdx, spaceIdx);
    startIdx = spaceIdx + 1;

    int colonIdx = token.indexOf(':');
    if (colonIdx == -1) continue;

    String key = token.substring(0, colonIdx);
    long val = token.substring(colonIdx + 1).toInt();

    if (key == "BTN") {
      btn1State = (val != 0);
    }

    for (int i = 0; i < 4; i++) {
      String expectedKey = "E" + String(i + 1);
      if (key == expectedKey) {
        if (val > lastEncoders[i]) encoders[i] += ENCODER_STEP;
        else if (val < lastEncoders[i]) encoders[i] -= ENCODER_STEP;

        encoders[i] = constrain(encoders[i], 0, 255);
        lastEncoders[i] = val;
      }
    }

    // Mirror for convenience
    encoders[4] = encoders[0];
    encoders[5] = encoders[1];
    encoders[6] = encoders[2];
    encoders[7] = encoders[3];
  }
}

// ---------------- PARSE HC12 FROM SLAVE ----------------
void parseHC12(String &msg) {
  if (!msg.startsWith("{M")) return;

  int idx;
  idx = msg.indexOf("WIN:");
  if (idx > 0) {
    int val = msg.substring(idx + 4).toInt();
    if (val == 1) {
      statePortal1 = WIN_EFFECT;
      Serial.println("MASTER: WIN effect triggered by SLAVE");
    }
  }

  Serial.print("HC12 RX: ");
  Serial.println(msg);
}

// ---------------- SEND DATA TO SLAVE ----------------
void sendToSlave() {
  String out = "{S ";
  out += "MODE:" + String(statePortal1 == GAME ? 1 : 0);
  out += " POS:" + String(gamePos1);
  out += " R:" + String(encoders[0]);
  out += " G:" + String(encoders[1]);
  out += " B:" + String(encoders[2]);
  out += " SPD:" + String(encoders[3]);
  out += "}";

  HC12.println(out);
  Serial.print("HC12 TX: ");
  Serial.println(out);
}

// ---------------- SHOW PASSIVE ----------------
void showPassive() {
  CRGB c1 = CRGB(encoders[0], encoders[1], encoders[2]);
  c1.nscale8_video(encoders[3]);
  fill_solid(leds1, NUM_LEDS, c1);
  FastLED.show();
}

// ---------------- SHOW GAME ----------------
void showGame() {
  unsigned long now = millis();

  int delayMs = map(encoders[3], 0, 255, 200, 10);

  if (now - lastUpdateTime1 >= (unsigned long)delayMs) {
    lastUpdateTime1 = now;
    gamePos1 = (gamePos1 + 1) % NUM_LEDS;
  }

  fill_solid(leds1, NUM_LEDS, CRGB::Black);

  CRGB c1(encoders[0], encoders[1], encoders[2]);

  for (int i = 0; i < TAIL_LENGTH; i++) {
    int pos = (gamePos1 - i + NUM_LEDS) % NUM_LEDS;
    uint8_t brightness = (255 - (255 / TAIL_LENGTH) * i) * GAME_BRIGHTNESS / 255;
    CRGB temp = c1;
    temp.nscale8_video(brightness);
    leds1[pos] = temp;
  }

  FastLED.show();
}

// ---------------- WIN EFFECT ----------------
void showWinEffect() {
  static uint8_t index = 0;
  index++;
  fill_rainbow(leds1, NUM_LEDS, index, 7);
  FastLED.show();
}

// ---------------- CHECK MATCH ----------------
bool colorsMatch() {
  int dr = encoders[0] - slaveR;
  int dg = encoders[1] - slaveG;
  int db = encoders[2] - slaveB;
  long dist2 = (long)dr*dr + (long)dg*dg + (long)db*db;
  return dist2 <= (long)COLOR_TOLERANCE * COLOR_TOLERANCE;
}

bool positionsMatch() {
  int diff = abs(gamePos1 - slavePos);
  if (diff > NUM_LEDS/2) diff = NUM_LEDS - diff;
  return diff <= POSITION_TOLERANCE;
}

// ---------------- SETUP ----------------
void setup() {
  Serial.begin(115200);
  NanoSerial.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);
  HC12.begin(9600, SERIAL_8N1, 6, 5);

  FastLED.addLeds<LED_TYPE, LED_PIN_PORTAL1, COLOR_ORDER>(leds1, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);

  Serial.println("MASTER READY");

  // --- DEBUG STARTUP BLINK (GREEN, 10 cycles) ---
  for (int i = 0; i < 10; i++) {
    fill_solid(leds1, NUM_LEDS, CRGB(0, 255, 0));
    FastLED.show();
    delay(100);
    fill_solid(leds1, NUM_LEDS, CRGB::Black);
    FastLED.show();
    delay(100);
  }
}

// ---------------- LOOP ----------------
void loop() {

  // --------- read from Nano ---------
  static String inputLine = "";
  while (NanoSerial.available()) {
    char c = NanoSerial.read();
    if (c == '\n') {
      parseUARTData(inputLine);
      inputLine = "";
    } else if (c!='\r') {
      inputLine += c;
    }
  }

  // --------- read from SLAVE via HC12 ---------
  static String hcLine = "";
  while (HC12.available()) {
    char h = HC12.read();
    if (h == '\n') {
      parseHC12(hcLine);
      hcLine = "";
    } else if (h != '\r') {
      hcLine += h;
    }
  }

  // --------- handle button toggle ---------
  static bool lastBtn = false;
  if (!lastBtn && btn1State) {
    if (statePortal1 == PASSIVE) {
      statePortal1 = GAME;
      gamePos1 = random(NUM_LEDS);
      encoders[0] = random(255);
      encoders[1] = random(255);
      encoders[2] = random(255);
      encoders[3] = random(50, 200);
      lastUpdateTime1 = millis();
      Serial.println("MASTER: GAME");
    }
    else {
      statePortal1 = PASSIVE;
      Serial.println("MASTER: PASSIVE");
    }
    winTimerActive = false;
  }
  lastBtn = btn1State;

  // --------- win-check (MASTER side) ---------
  if (statePortal1 == GAME) {
    if (colorsMatch() && positionsMatch()) {
      if (!winTimerActive) {
        winStartTime = millis();
        winTimerActive = true;
      }
      else if (millis() - winStartTime >= 3000) {
        statePortal1 = WIN_EFFECT;
        Serial.println("MASTER: WIN!");
        winTimerActive = false;
      }
    } else {
      winTimerActive = false;
    }
  }

  // --------- send state to slave ---------
  sendToSlave();

  // --------- show LED ---------
  if (statePortal1 == PASSIVE) showPassive();
  else if (statePortal1 == GAME) showGame();
  else if (statePortal1 == WIN_EFFECT) showWinEffect();
}