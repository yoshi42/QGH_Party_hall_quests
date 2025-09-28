#include <FastLED.h>

#define LED_PIN_PORTAL1 9
#define LED_PIN_PORTAL2 10
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
CRGB leds2[NUM_LEDS];

HardwareSerial NanoSerial(1);

// масив для значень енкодерів
long encoders[8] = {0};
long lastEncoders[8] = {0};

bool btn1State = false;
bool btn2State = false;

enum PortalState {
  PASSIVE,
  GAME,
  WIN_EFFECT
};

PortalState statePortal1 = PASSIVE;
PortalState statePortal2 = PASSIVE;

unsigned long winStartTime = 0;
bool winTimerActive = false;

int gamePos1 = 0;
int gamePos2 = 0;

unsigned long lastUpdateTime1 = 0;
unsigned long lastUpdateTime2 = 0;

// ----------------- PARSE UART -----------------
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

    if (key == "BTN1") {
      btn1State = val != 0;
    } else if (key == "BTN2") {
      btn2State = val != 0;
    }

    // Дискретна обробка енкодерів
    for (int i = 0; i < 8; i++) {
      String expectedKey = "E" + String(i + 1);
      if (key == expectedKey) {
        if (val > lastEncoders[i]) {
          encoders[i] += ENCODER_STEP;
        } else if (val < lastEncoders[i]) {
          encoders[i] -= ENCODER_STEP;
        }
        encoders[i] = constrain(encoders[i], 0, 255);
        lastEncoders[i] = val;
        Serial.print(expectedKey); Serial.print(": "); Serial.println(val);
      }
    }
  }
}

// ----------------- SHOW PASSIVE -----------------
void showPassive() {
  // портал 1
  CRGB c1 = CRGB(
    constrain(encoders[0], 0, 255),
    constrain(encoders[1], 0, 255),
    constrain(encoders[2], 0, 255)
  );
  c1.nscale8_video(constrain(encoders[3], 0, 255));
  fill_solid(leds1, NUM_LEDS, c1);

  // портал 2
  CRGB c2 = CRGB(
    constrain(encoders[4], 0, 255),
    constrain(encoders[5], 0, 255),
    constrain(encoders[6], 0, 255)
  );
  c2.nscale8_video(constrain(encoders[7], 0, 255));
  fill_solid(leds2, NUM_LEDS, c2);

  FastLED.show();
}

void showGame() {
  unsigned long now = millis();

  // speed from E4 and E8, map 0-255 to 10-200 ms delay (higher value = slower)
  int speed1 = map(encoders[3], 0, 255, 200, 10);
  int speed2 = map(encoders[7], 0, 255, 200, 10);

  // Update positions
  if (now - lastUpdateTime1 >= (unsigned long)speed1) {
    lastUpdateTime1 = now;
    gamePos1 = (gamePos1 + 1) % NUM_LEDS;
  }
  if (now - lastUpdateTime2 >= (unsigned long)speed2) {
    lastUpdateTime2 = now;
    gamePos2 = (gamePos2 + 1) % NUM_LEDS;
  }

  // Clear strips
  fill_solid(leds1, NUM_LEDS, CRGB::Black);
  fill_solid(leds2, NUM_LEDS, CRGB::Black);

  // Colors from encoders for each portal
  CRGB c1 = CRGB(
    constrain(encoders[0], 0, 255),
    constrain(encoders[1], 0, 255),
    constrain(encoders[2], 0, 255)
  );

  CRGB c2 = CRGB(
    constrain(encoders[4], 0, 255),
    constrain(encoders[5], 0, 255),
    constrain(encoders[6], 0, 255)
  );

  // Draw tail for portal 1
  for (int i = 0; i < TAIL_LENGTH; i++) {
    int pos = (gamePos1 - i + NUM_LEDS) % NUM_LEDS;
    uint8_t brightness = (uint8_t)(((255 - (255 / TAIL_LENGTH) * i) * GAME_BRIGHTNESS) / 255);
    CRGB temp = c1;
    temp.nscale8_video(brightness);
    leds1[pos] = temp;
  }

  // Draw tail for portal 2
  for (int i = 0; i < TAIL_LENGTH; i++) {
    int pos = (gamePos2 - i + NUM_LEDS) % NUM_LEDS;
    uint8_t brightness = (uint8_t)(((255 - (255 / TAIL_LENGTH) * i) * GAME_BRIGHTNESS) / 255);
    CRGB temp = c2;
    temp.nscale8_video(brightness);
    leds2[pos] = temp;
  }

  FastLED.show();
}

void showGameOld() {
  unsigned long now = millis();

  // speed from E4 and E8, map 0-255 to 10-200 ms delay (higher value = slower)
  int speed1 = map(encoders[3], 0, 255, 200, 10);
  int speed2 = map(encoders[7], 0, 255, 200, 10);

  if (now - lastUpdateTime1 >= (unsigned long)speed1) {
    lastUpdateTime1 = now;
    gamePos1 = (gamePos1 + 1) % NUM_LEDS;
  }
  if (now - lastUpdateTime2 >= (unsigned long)speed2) {
    lastUpdateTime2 = now;
    gamePos2 = (gamePos2 + 1) % NUM_LEDS;
  }

  // Clear strips
  fill_solid(leds1, NUM_LEDS, CRGB::Black);
  fill_solid(leds2, NUM_LEDS, CRGB::Black);

  // Colors from encoders for each portal
  CRGB c1 = CRGB(
    constrain(encoders[0], 0, 255),
    constrain(encoders[1], 0, 255),
    constrain(encoders[2], 0, 255)
  );
  c1.nscale8_video(constrain(encoders[3], 0, 255));

  CRGB c2 = CRGB(
    constrain(encoders[4], 0, 255),
    constrain(encoders[5], 0, 255),
    constrain(encoders[6], 0, 255)
  );
  c2.nscale8_video(constrain(encoders[7], 0, 255));

  // Draw tail for portal 1
  for (int i = 0; i < TAIL_LENGTH; i++) {
    int pos = (gamePos1 - i + NUM_LEDS) % NUM_LEDS;
    uint8_t brightness = 255 - (255 / TAIL_LENGTH) * i;
    leds1[pos] = c1;
    leds1[pos].nscale8_video(brightness);
  }

  // Draw tail for portal 2
  for (int i = 0; i < TAIL_LENGTH; i++) {
    int pos = (gamePos2 - i + NUM_LEDS) % NUM_LEDS;
    uint8_t brightness = 255 - (255 / TAIL_LENGTH) * i;
    leds2[pos] = c2;
    leds2[pos].nscale8_video(brightness);
  }

  FastLED.show();
}

void showWinEffect() {
  static uint8_t startIndex = 0;
  startIndex = startIndex + 1; /* motion speed */

  fill_rainbow(leds1, NUM_LEDS, startIndex, 7);
  fill_rainbow(leds2, NUM_LEDS, startIndex, 7);

  FastLED.show();
}

bool colorsMatch() {
  // Check if colors (RGB) match for both portals with tolerance, using Euclidean distance, ignoring brightness (E4 and E8)
  int dr = constrain(encoders[0], 0, 255) - constrain(encoders[4], 0, 255);
  int dg = constrain(encoders[1], 0, 255) - constrain(encoders[5], 0, 255);
  int db = constrain(encoders[2], 0, 255) - constrain(encoders[6], 0, 255);
  long dist2 = (long)dr * dr + (long)dg * dg + (long)db * db;
  return dist2 <= (long)COLOR_TOLERANCE * COLOR_TOLERANCE;
}

bool positionsMatch() {
  // Check if game positions match with tolerance and wrap-around
  int diff = abs(gamePos1 - gamePos2);
  if (diff > NUM_LEDS / 2) {
    diff = NUM_LEDS - diff;
  }
  return diff <= POSITION_TOLERANCE;
}

// ----------------- SETUP -----------------
void setup() {
  Serial.begin(115200);
  NanoSerial.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);

  FastLED.addLeds<LED_TYPE, LED_PIN_PORTAL1, COLOR_ORDER>(leds1, NUM_LEDS);
  FastLED.addLeds<LED_TYPE, LED_PIN_PORTAL2, COLOR_ORDER>(leds2, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);

  Serial.println("ESP32C3 ready, passive mode only");
}

// ----------------- LOOP -----------------
void loop() {
  static String inputLine = "";

  while (NanoSerial.available()) {
    char c = NanoSerial.read();
    if (c == '\n') {
      parseUARTData(inputLine);
      Serial.print("Encoders: ");
      for (int i = 0; i < 8; i++) {
        Serial.print("E");
        Serial.print(i + 1);
        Serial.print("=");
        Serial.print(encoders[i]);
        if (i < 7) Serial.print(", ");
      }
      Serial.println();
      inputLine = "";
    } else if (c != '\r') {
      inputLine += c;
      if (inputLine.length() > 120) inputLine = ""; // захист
    }
  }

  // Read buttons and toggle states
  static bool lastBtn1State = false;
  static bool lastBtn2State = false;

  if ((lastBtn1State == false && btn1State == true) || (lastBtn2State == false && btn2State == true)) {
    // Toggle both portals PASSIVE <-> GAME
    if (statePortal1 == PASSIVE && statePortal2 == PASSIVE) {
      statePortal1 = GAME;
      statePortal2 = GAME;
      // Ініціалізація випадкових кольорів, швидкостей і позицій
      gamePos1 = random(NUM_LEDS);
      gamePos2 = random(NUM_LEDS);
      for (int i = 0; i <= 2; i++) {
        encoders[i] = random(0, 256);
      }
      for (int i = 4; i <= 6; i++) {
        encoders[i] = random(0, 256);
      }
      encoders[3] = random(50, 201);
      encoders[7] = random(50, 201);
      lastUpdateTime1 = millis();
      lastUpdateTime2 = millis();
      Serial.println("Both portals: GAME mode");
    } else if (statePortal1 == GAME && statePortal2 == GAME) {
      statePortal1 = PASSIVE;
      statePortal2 = PASSIVE;
      Serial.println("Both portals: PASSIVE mode");
    } else {
      // In any other state, set both to PASSIVE
      statePortal1 = PASSIVE;
      statePortal2 = PASSIVE;
      Serial.println("Both portals: PASSIVE mode");
    }
    winTimerActive = false;
  }

  lastBtn1State = btn1State;
  lastBtn2State = btn2State;

  // Check if both portals are in GAME mode
  if (statePortal1 == GAME && statePortal2 == GAME) {
    // Check if colors and positions match
    if (colorsMatch() && positionsMatch()) {
      if (!winTimerActive) {
        winStartTime = millis();
        winTimerActive = true;
      } else {
        if (millis() - winStartTime >= 3000) {
          statePortal1 = WIN_EFFECT;
          statePortal2 = WIN_EFFECT;
          Serial.println("WIN_EFFECT activated!");
          winTimerActive = false;
        }
      }
    } else {
      winTimerActive = false;
    }
  } else {
    winTimerActive = false;
  }

  // Show according to states
  if (statePortal1 == PASSIVE && statePortal2 == PASSIVE) {
    showPassive();
  } else if (statePortal1 == GAME || statePortal2 == GAME) {
    showGame();
  } else if (statePortal1 == WIN_EFFECT && statePortal2 == WIN_EFFECT) {
    showWinEffect();
  } else {
    // fallback to passive if inconsistent states
    showPassive();
  }
}