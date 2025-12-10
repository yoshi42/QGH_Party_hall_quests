#include <FastLED.h>
#include <SoftwareSerial.h>

// ---------------- LED CONFIG -------------------
#define LED_PIN 3
#define NUM_LEDS 216
CRGB leds[NUM_LEDS];

// ------------- ENCODERS ------------------------
struct Enc { byte pinA; byte pinB; int value; int last; };
Enc encs[4] = {
  {12, 11, 0, 0},   // R
  {10,  9, 0, 0},   // G
  {8,   7, 0, 0},   // B
  {4,   5, 0, 0}    // Speed encoder corrected pin order
};

#define BTN_PIN 6
bool modeGame = false;
bool lastBtn = false;

// ---------- HC12 ----------------
#define HC_RX A0
#define HC_TX A1
SoftwareSerial HC12(HC_RX, HC_TX);

// SLAVE → MASTER packet fields
int pos = 0;
unsigned long lastMove = 0;

// ---------- WIN ----------
bool win = false;
unsigned long winStart = 0;
const int WIN_DURATION = 60000;

unsigned long lastDebug = 0;

// -----------------------------------------------
void setup() {
  Serial.begin(115200);
  HC12.begin(9600);

  FastLED.addLeds<WS2812, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(120);

  for (int i=0;i<4;i++) {
    pinMode(encs[i].pinA, INPUT_PULLUP);
    pinMode(encs[i].pinB, INPUT_PULLUP);
  }

  pinMode(BTN_PIN, INPUT_PULLUP);

  randomSeed(analogRead(0));

  // Random initial color
  encs[0].value = random(50, 200);  // R
  encs[1].value = random(50, 200);  // G
  encs[2].value = random(50, 200);  // B
  encs[3].value = 120;              // brightness/speed default mid

  // Startup blink: red 3 times for SLAVE
  for (int i = 0; i < 3; i++) {
    fill_solid(leds, NUM_LEDS, CRGB(255, 0, 0));
    FastLED.show();
    delay(200);
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    FastLED.show();
    delay(200);
  }
}

// -----------------------------------------------
void readEncoders() {
  for (int i = 0; i < 4; i++) {

    int a = digitalRead(encs[i].pinA);
    int b = digitalRead(encs[i].pinB);
    int encoded = (a << 1) | b;
    int last = encs[i].last;
    int sum = (last << 2) | encoded;

    // Standard robust quadrature decoding (from working portal.ino)
    int delta = 0;
    if (sum == 0b1101 || sum == 0b0100 || sum == 0b0010 || sum == 0b1011)
      delta = +1;
    else if (sum == 0b1110 || sum == 0b0111 || sum == 0b0001 || sum == 0b1000)
      delta = -1;

    // Sensitivity (set to moderate, not excessive)
    encs[i].value += delta * 10;   // increased sensitivity

    // Clamp
    if (encs[i].value < 0) encs[i].value = 0;
    if (encs[i].value > 255) encs[i].value = 255;

    encs[i].last = encoded;
  }
}

// -----------------------------------------------
void sendStateToMaster() {
  static unsigned long lastTx = 0;
  unsigned long now = millis();

  if (now - lastTx < 350) return;   // fixed TX interval
  lastTx = now;

  String out = "{S ";
  out += pos;
  out += "}";
  out += "#";

  HC12.println(out);
}

// -----------------------------------------------
void parseMasterPacket(String s) {
  if (s.indexOf("WIN:1") > 0) {
    win = true;
    winStart = millis();
  }
}

// -----------------------------------------------
void runPassive() {
  fill_solid(leds, NUM_LEDS, CRGB(encs[0].value, encs[1].value, encs[2].value));
  FastLED.setBrightness( map(encs[3].value, 0, 255, 20, 255) );
  FastLED.show();
}

// -----------------------------------------------
void runGame() {
  int raw = encs[3].value;
  int spd = map(raw, 0, 255, 1, 45);
  unsigned long now = millis();
  unsigned long interval = 50 - spd;
  if (interval < 3) interval = 3;

  if (now - lastMove >= interval && spd > 0) {
    pos = (pos + 1) % NUM_LEDS;
    lastMove = now;
  }

  int R = encs[0].value;
  int G = encs[1].value;
  int B = encs[2].value;

  fill_solid(leds, NUM_LEDS, CRGB::Black);
  for (int i=0;i<10;i++) {
    int p = (pos - i + NUM_LEDS) % NUM_LEDS;
    uint8_t fade = 255 - (i*25);
    leds[p] = CRGB(R, G, B).nscale8(fade);
  }

  FastLED.setBrightness(175);
  FastLED.show();

  sendStateToMaster();
}

// -----------------------------------------------
void runWin() {
  unsigned long now = millis();
  if (now - winStart > WIN_DURATION) {
    win = false;
    return;
  }

  // Match MASTER win effect: smooth rainbow sweep
  uint8_t hueBase = (now / 6) & 255;

  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = CHSV(hueBase + (i >> 2), 255, 255);
  }

  FastLED.show();
}

// -----------------------------------------------
void loop() {
  /*Serial.print("BTN="); Serial.print(!digitalRead(BTN_PIN));
  Serial.print(" MODE="); Serial.print(modeGame);
  Serial.print(" POS="); Serial.print(pos);
  Serial.print(" SPDraw="); Serial.print(encs[3].value);
  Serial.print(" SPDmap="); Serial.println(map(encs[3].value, 0, 255, 0, 20));
*/
  // ---- High-frequency encoder polling (every 2 ms) ----
  static unsigned long lastEnc = 0;
  unsigned long encNow = millis();
  if (encNow - lastEnc >= 2) {
    lastEnc = encNow;
    readEncoders();
  }

  // BUTTON
  bool btn = !digitalRead(BTN_PIN);
  if (btn && !lastBtn) {
    win = false;                 // allow mode switch during WIN
    modeGame = !modeGame;
    pos = random(NUM_LEDS);

    encs[0].value = random(50, 200);
    encs[1].value = random(50, 200);
    encs[2].value = random(50, 200);
  }
  lastBtn = btn;

  // RECEIVE MASTER PACKETS
// ---- NON-BLOCKING HC12 READER ----
static String hcBuf = "";
while (HC12.available()) {
  char c = HC12.read();
  if (c == '\n') {
    parseMasterPacket(hcBuf);
    hcBuf = "";
  } else {
    hcBuf += c;
  }
}

// ---- PERIODIC DEBUG (every 300 ms) ----
static unsigned long dbgT = 0;
if (millis() - dbgT >= 300) {
  dbgT = millis();
  Serial.print("SLAVE MODE=");
  Serial.print(modeGame ? "GAME" : "PASSIVE");
  Serial.print(" POS=");
  Serial.print(pos);
  Serial.print(" ENC R/G/B/S = ");
  Serial.print(encs[0].value); Serial.print("/");
  Serial.print(encs[1].value); Serial.print("/");
  Serial.print(encs[2].value); Serial.print("/");
  Serial.print(encs[3].value);
  Serial.print("  WIN=");
  Serial.println(win ? 1 : 0);
}

  // WIN MODE PRIORITY
  if (win) {
    runWin();
    return;
  }

  if (!modeGame) runPassive();
  else runGame();
}