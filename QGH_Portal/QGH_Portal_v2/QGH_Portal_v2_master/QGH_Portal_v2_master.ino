#include <FastLED.h>
#define HC12 Serial

/*/IMPORTANT//
FOR MASTER AND SLAVE HC-12 ARE SET TO C006 Channel to avoid interference with other devices
to change channel:

Connect GND+SET pins
Set 9600 baud
send commands to serial
AT - AT mode
AT+RX - to check current speed
AT+C006 - to set channel
*/

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

// SLAVE incoming state
int sPos = 0;
int sR = 0, sG = 0, sB = 0;
int sSpeed = 0;
bool sValid = false;
unsigned long lastFresh = 0;


String lastRawPacket = "";

#define RX_BUF_MAX 50

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

static int winRepeats = 0;
static unsigned long lastWinSend = 0;

// -----------------------------------------------
void setup() {
  Serial.begin(9600);

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

  if (!s.startsWith("{S") || !s.endsWith("}")) return;

  // тіло: між "{S" і "}"
  String body = s.substring(2, s.length() - 1);
  body.trim();

  // Забираємо все, що не цифра і не +
  for (int i = 0; i < body.length(); i++) {
    if (!isDigit(body[i])) body[i] = ' ';
  }

  body.trim();

  // читаємо ОДНЕ число — POS
  int pos_;
  int matched = sscanf(body.c_str(), "%d", &pos_);

  if (matched != 1) return;
  if (pos_ < 0 || pos_ >= NUM_LEDS) return;

  sPos = pos_;
  sValid = true;
  lastFresh = millis();
}

// -----------------------------------------------
void sendWinToSlave() {
  Serial.println("{M WIN:1}");
}

// -----------------------------------------------
bool checkWin() {
  if (!sValid) return false;

  // ignore outdated slave data
  if (millis() - lastFresh > 400) return false;

  int dPos = abs(pos - sPos);
  if (dPos > NUM_LEDS / 2) dPos = NUM_LEDS - dPos;
  bool posOK = (dPos <= POS_TOL);

  static unsigned long stableSince = 0;
  static int stableCount = 0;

  if (posOK) {
    if (stableSince == 0) stableSince = millis();
    stableCount++;

    // потрібно 4 збіги + 500 мс справжньої стабільності
    if (stableCount >= 4 && millis() - stableSince >= 500) {
      stableSince = 0;
      stableCount = 0;
      winRepeats = 3;
      return true;
    }
    return false;
  } else {
    stableSince = 0;
    stableCount = 0;
    return false;
  }
}

// -----------------------------------------------
void runPassive() {
int br = map(encs[3].value, 0, 255, 10, 255);
FastLED.setBrightness(br);

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
  FastLED.show();
}

void readHC12() {
    static bool inPacket = false;
    static String packet = "";

    while (Serial.available()) {
        char c = Serial.read();

        // start of packet
        if (c == '{') {
            inPacket = true;
            packet = "{";
            continue;
        }

        // accumulating packet
        if (inPacket) {
            packet += c;

            // end of packet
            if (c == '#') {

                // fully clean packet from extra characters
                packet.replace("#", "");
                packet.replace("\r", "");
                packet.replace("\n", "");
                packet.trim();

                // packet must end with '}'
                if (packet.endsWith("}")) {
                    lastRawPacket = packet;
                    parseSlavePacket(packet);
                }

                // fully reset
                inPacket = false;
                packet = "";
            }
        }
    }
}

// -----------------------------------------------
void loop() {
  readHC12();

  if (winRepeats > 0 && millis() - lastWinSend >= 10) {
    sendWinToSlave();
    winRepeats--;
    lastWinSend = millis();
  }

  /*// 
  Periodic send to HC12 every 500ms
  static unsigned long lastHCSend = 0;
  unsigned long now = millis();
  if (now - lastHCSend >= 500) {
    Serial.print("{DBG M_POS:");
    Serial.print(pos);
    // RGB ignored in pos-only mode
    Serial.print(" S_POS:");
    Serial.print(sPos);
    // no color debug in pos-only mode
    Serial.println("}");
    lastHCSend = now;
  } 
  *///

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

  /*/ -------- periodic debug print (non-blocking) ----------
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
  }//*/

  // WIN CHECK
  if (modeGame && checkWin() && !win) {
    win = true;
    winStart = millis();
    sendWinToSlave();
  }

  if (win) {
    runWin();
    FastLED.show();
    return;
  }

  if (!modeGame) runPassive();
  else runGame();

// update LEDs much more often — prevents packet loss
static unsigned long lastFrame = 0;
unsigned long nowFrame = millis();
if (nowFrame - lastFrame >= 6) {  // ~160 FPS, but very short blocking
    lastFrame = nowFrame;
    FastLED.show();
}
}