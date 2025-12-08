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
  {10, 9,  0, 0},   // G
  {8,  7,  0, 0},   // B
  {5,  4,  0, 0}    // Speed
};

#define BTN_PIN 6
bool modeGame = false;
bool lastBtn = false;

// ---------- HC12 ----------------
#define HC_RX A0
#define HC_TX A1
SoftwareSerial HC12(HC_RX, HC_TX);

// SLAVE incoming state
int sPos = 0;
int sR = 0, sG = 0, sB = 0;
int sSpeed = 0;
bool sValid = false;

// ----------- GAME -------------
int pos = 0;
unsigned long lastMove = 0;

// ----------- WIN -------------
bool win = false;
unsigned long winStart = 0;
const int WIN_DURATION = 5000;

// tolerances
const int POS_TOL = 10;
const int COL_TOL = 80;

// -----------------------------------------------
void setup() {
  Serial.begin(115200);
  HC12.begin(9600);

  FastLED.addLeds<WS2812, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(180);

  // --- Startup green blink (master) ---
  for (int i = 0; i < 3; i++) {
    fill_solid(leds, NUM_LEDS, CRGB(0, 255, 0));  // green
    FastLED.show();
    delay(80);
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    FastLED.show();
    delay(80);
  }

  // Reset encoder states after LED blinking (prevents quadrature desync)
  for (int i = 0; i < 4; i++) {
    encs[i].value = 128; // mid-point for predictable start
    encs[i].last = (digitalRead(encs[i].pinA) << 1) | digitalRead(encs[i].pinB);
  }

  for (int i=0;i<4;i++) {
    pinMode(encs[i].pinA, INPUT_PULLUP);
    pinMode(encs[i].pinB, INPUT_PULLUP);
  }

  pinMode(BTN_PIN, INPUT_PULLUP);

  randomSeed(analogRead(0));
}

// -----------------------------------------------
void readEncoders() {
  for (int i=0;i<4;i++) {
    int a = digitalRead(encs[i].pinA);
    int b = digitalRead(encs[i].pinB);
    int encoded = (a << 1) | b;
    int sum = (encs[i].last << 2) | encoded;

    if (sum == 0b1101 || sum == 0b0100 || sum == 0b0010 || sum == 0b1011)
      encs[i].value++;
    if (sum == 0b1110 || sum == 0b0111 || sum == 0b0001 || sum == 0b1000)
      encs[i].value--;

    if (encs[i].value < 0) encs[i].value = 0;
    if (encs[i].value > 255) encs[i].value = 255;

    encs[i].last = encoded;
  }
}

// -----------------------------------------------
void parseSlavePacket(String s) {
  if (!s.startsWith("{S")) return;

  int p = s.indexOf("POS:");  
  int r = s.indexOf("R:");
  int g = s.indexOf("G:");
  int b = s.indexOf("B:");
  int sp = s.indexOf("SPD:");

  if (p<0||r<0||g<0||b<0||sp<0) return;

  sPos = s.substring(p+4).toInt();
  sR   = s.substring(r+2).toInt();
  sG   = s.substring(g+2).toInt();
  sB   = s.substring(b+2).toInt();
  sSpeed = s.substring(sp+4).toInt();

  sValid = true;
}

// -----------------------------------------------
void sendWinToSlave() {
  HC12.println("{M WIN:1}");
}

// -----------------------------------------------
bool checkWin() {
  if (!sValid) return false;

  int myR = encs[0].value;
  int myG = encs[1].value;
  int myB = encs[2].value;

  if (abs(pos - sPos) > POS_TOL) return false;
  if (abs(myR - sR) > COL_TOL) return false;
  if (abs(myG - sG) > COL_TOL) return false;
  if (abs(myB - sB) > COL_TOL) return false;

  return true;
}

// -----------------------------------------------
void runPassive() {
  FastLED.setBrightness(180);   // restore normal brightness
  fill_solid(leds, NUM_LEDS, CRGB(encs[0].value, encs[1].value, encs[2].value));
  FastLED.show();
}

// -----------------------------------------------
void runGame() {
  int raw = encs[3].value;
  int spd = map(raw, 0, 255, 1, 30);    // smoother control, never fully stops
  unsigned long now = millis();
  unsigned long interval = 40 - spd;    // improved curve identical to slave
  if (interval < 5) interval = 5;       // minimum interval ensures movement

  if (now - lastMove >= interval && spd>0) {
    pos = (pos + 1) % NUM_LEDS;
    lastMove = now;
  }

  fill_solid(leds, NUM_LEDS, CRGB::Black);
  for (int i=0;i<10;i++) {
    int p = (pos - i + NUM_LEDS) % NUM_LEDS;
    uint8_t fade = 255 - (i*25);
    leds[p] = CRGB(encs[0].value, encs[1].value, encs[2].value).nscale8(fade);
  }
  FastLED.setBrightness(175);   // fixed brightness in game mode
  FastLED.show();
}

// -----------------------------------------------
void runWin() {
  unsigned long now = millis();
  if (now - winStart > WIN_DURATION) {
    win = false;
    return;
  }

  uint8_t hue = (now / 5) & 255;
  fill_solid(leds, NUM_LEDS, CHSV(hue, 255, 255));
  FastLED.show();
}

// -----------------------------------------------
void loop() {
  readEncoders();

  // BUTTON
  bool btn = !digitalRead(BTN_PIN);
  static unsigned long lastBtnTime = 0;

  if (btn && !lastBtn && millis() - lastBtnTime > 200) {
    modeGame = !modeGame;
    pos = random(NUM_LEDS);
    lastBtnTime = millis();
  }
  lastBtn = btn;

  // RECEIVE SLAVE
  while (HC12.available()) {
    String s = HC12.readStringUntil('\n');
    parseSlavePacket(s);
  }

  // WIN CHECK
  if (modeGame && checkWin() && !win) {
    win = true;
    winStart = millis();
    sendWinToSlave();
  }

  if (win) {
    runWin();
    return;
  }

  if (!modeGame) runPassive();
  else runGame();
}