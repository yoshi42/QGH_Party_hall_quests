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
  {4,  5,  0, 0}    // Speed
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
unsigned long lastFresh = 0;

// ----------- GAME -------------
int pos = 0;
unsigned long lastMove = 0;

// ----------- WIN -------------
bool win = false;
unsigned long winStart = 0;
const int WIN_DURATION = 60000;

// tolerances
const int POS_TOL = 30;      // tighter position tolerance
const int COL_TOL = 30;     // kept for reference (RGB)
const uint8_t SAT_MIN = 30;
const uint8_t VAL_MIN = 30;
const uint8_t HUE_TOL = 20;

unsigned long lastDebug = 0;   // periodic debug timer

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
      encs[i].value += 10;
    if (sum == 0b1110 || sum == 0b0111 || sum == 0b0001 || sum == 0b1000)
      encs[i].value -= 10;

    if (encs[i].value < 0) encs[i].value = 0;
    if (encs[i].value > 255) encs[i].value = 255;

    encs[i].last = encoded;
  }
}

// -----------------------------------------------
void parseSlavePacket(String s) {
  // Очікуємо компактний формат:
  // {S POS R G B SPD}

  if (!s.startsWith("{S")) return;

  int pos_, r_, g_, b_, spd_;
  int matched = sscanf(s.c_str(), "{S %d %d %d %d %d}", 
                       &pos_, &r_, &g_, &b_, &spd_);

  if (matched == 5) {
    sPos   = pos_;
    sR     = r_;
    sG     = g_;
    sB     = b_;
    sSpeed = spd_;

    sValid = true;
    lastFresh = millis();
  }
}

// -----------------------------------------------
void sendWinToSlave() {
  HC12.println("{M WIN:1}");
}

// -----------------------------------------------
bool checkWin() {
if (!sValid) return false;

// if no new data from slave in 120 ms — ignore it
if (millis() - lastFresh > 120) return false;


  // current raw RGB from master encoders
  int myR = encs[0].value;
  int myG = encs[1].value;
  int myB = encs[2].value;

  // --- SIMPLE RGB DIFFERENCE MATCHING ---
  int dR = abs(myR - sR);
  int dG = abs(myG - sG);
  int dB = abs(myB - sB);

  // total RGB difference
  int rgbDiff = dR + dG + dB;

  // allow win if total diff is small
  bool colOK = (rgbDiff <= COL_TOL);

  // --- position tolerance with ring wrap ---
  int dPos = abs(pos - sPos);
  if (dPos > NUM_LEDS / 2) dPos = NUM_LEDS - dPos;
  bool posOK = (dPos <= POS_TOL);

  static unsigned long stableSince = 0;

  if (posOK && colOK) {
    if (stableSince == 0) stableSince = millis();
    // must remain stable for at least 500 ms
    if (millis() - stableSince >= 500) {
      stableSince = 0;
      // resend win command a few times for reliability
      for (int i = 0; i < 3; i++) {
          sendWinToSlave();
          delay(5);
      }
      return true;
    }
    return false;
  } else {
    stableSince = 0;
    return false;
  }
}

// -----------------------------------------------
void runPassive() {
  FastLED.setBrightness(180);   // restore normal brightness
  fill_solid(leds, NUM_LEDS, CRGB(encs[0].value, encs[1].value, encs[2].value));
}

// -----------------------------------------------
void runGame() {
  int raw = encs[3].value;
  int spd = map(raw, 0, 255, 1, 30);

  // ---- FIX: захист від зависання руху ----
  unsigned long now = micros();   // точніший таймер
  static unsigned long lastMoveUs = 0;

  // інтервал у мікросекундах (гарантує рух навіть при блокуваннях)
  unsigned long intervalUs = 5000 + (30000 - spd * 1000); 
  // min ~5ms, max ~35ms

  if (now - lastMoveUs >= intervalUs) {
    pos = (pos + 1) % NUM_LEDS;
    lastMoveUs = now;
  }

  // --- Render tail ---
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  for (int i=0;i<10;i++) {
    int p = (pos - i + NUM_LEDS) % NUM_LEDS;
    uint8_t fade = 255 - (i*25);
    leds[p] = CRGB(encs[0].value, encs[1].value, encs[2].value).nscale8(fade);
  }

  FastLED.setBrightness(175);
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
}

// -----------------------------------------------
void loop() {

  // ---- HC12 continuous receive (robust CR/LF handling) ----
  static String rxBuf = "";
  while (HC12.available()) {
    char c = HC12.read();

    if (c == '\r') {
      // ignore CR completely
      continue;
    }

    if (c == '\n') {
      if (rxBuf.length() > 0) {
        parseSlavePacket(rxBuf);
      }
      rxBuf = "";
    } else {
      rxBuf += c;
    }
  }


  // ---- High-frequency encoder polling (every 2 ms) ----
  static unsigned long lastEnc = 0;
  unsigned long encNow = millis();
  if (encNow - lastEnc >= 2) {   // ~500 Hz polling
    lastEnc = encNow;
    readEncoders();
  }

  // BUTTON
  bool btn = !digitalRead(BTN_PIN);
  static unsigned long lastBtnTime = 0;

  if (btn && !lastBtn && millis() - lastBtnTime > 200) {
    // allow interrupting win-effect
    if (win) {
      win = false;
      modeGame = false;     // return to passive
      lastBtnTime = millis();
    } else {
      modeGame = !modeGame;
      pos = random(NUM_LEDS);
      lastBtnTime = millis();
    }
  }
  lastBtn = btn;

  // -------- periodic debug print (non-blocking) ----------
  if (millis() - lastDebug > 300) {
    lastDebug = millis();
    Serial.print("MODE=");
    Serial.print(modeGame ? "GAME" : "PASSIVE");
    Serial.print("   POS="); Serial.print(pos);
    Serial.print("   ENC R/G/B/S = ");
    Serial.print(encs[0].value); Serial.print("/");
    Serial.print(encs[1].value); Serial.print("/");
    Serial.print(encs[2].value); Serial.print("/");
    Serial.print(encs[3].value);

    Serial.print("   SLAVE POS="); Serial.print(sPos);
    Serial.print("   SLAVE RGB=");
    Serial.print(sR); Serial.print("/");
    Serial.print(sG); Serial.print("/");
    Serial.print(sB);
    Serial.print("   SLAVE SPD=");
    Serial.println(sSpeed);
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

  static unsigned long lastFrame = 0;
  unsigned long nowFrame = millis();
  if (nowFrame - lastFrame >= 30) {   // 30ms = ~33 FPS, stable for SoftwareSerial
      lastFrame = nowFrame;
      FastLED.show();
  }
}