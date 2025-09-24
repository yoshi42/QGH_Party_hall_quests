/*
=============================================================
  LED ПОРТАЛИ (2 штуки) на базі Arduino Nano + WS2811
=============================================================

СТРУКТУРА ЛОГІКИ:
- PASSIVE: плавні інтер’єрні ефекти, налаштовуються незалежно.
  * 3 енкодери: hue, saturation, brightness
  * 4-й: режим переливання
- GAME: вогники біжать по колу, синхронізуються між порталами
  * 3 енкодери: RGB-вогника з дискретним кроком
  * 4-й: швидкість
  * Натискання 4го енкодера -> старт гри / скидання / повернення у PASSIVE
- Перемога: колір, позиція та швидкість збігаються → бонусний ефект
=============================================================
*/
/*
  Оновлений код для Arduino Nano
  - 2 портали, 8 енкодерів (A/B сигнали), 2 кнопки (SW з енкодерів D)
  - PASSIVE: 3 енкодери (hue/sat/val), 4-й енкодер (mode)
  - GAME: 3 енкодери -> RGB (дискретно), 4-й енкодер -> speed (натискання D - старт/скидання)
  - Натискання тільки енкодера D (SW) запускає/скидає гру для відповідного порталу
*/

#include <FastLED.h>

#define LED_PIN_PORTAL1 6
#define LED_PIN_PORTAL2 13
#define NUM_LEDS 216 //к-сть діодів
#define BRIGHTNESS 200
#define COLOR_STEP 20  // крок зміни кольору
#define LED_TYPE WS2812
#define COLOR_ORDER GRB

// Мінімальний інтервал між кроками (мс)
const unsigned long ENCODER_DEBOUNCE = 3;

// Час останнього апдейта по кожному енкодеру
unsigned long lastUpdate1A = 0, lastUpdate1B = 0, lastUpdate1C = 0, lastUpdate1D = 0;
unsigned long lastUpdate2A = 0, lastUpdate2B = 0, lastUpdate2C = 0, lastUpdate2D = 0;

// ------------------ ЕНКОДЕРИ ------------------
struct EncoderPins {
  int pinA;
  int pinB;
  int lastState;
};

// Портал 1 енкодери: Hue / R, Sat / G, Bri / B, Mode / Speed (D)
EncoderPins encoders[8] = {
  {12, 11, 0}, // enc1A
  {10, 9, 0},  // enc1B
  {8, 7, 0},   // enc1C
  {5, 4, 0},   // enc1D
  {3, 2, 0},     // enc2A
  {A5, A4, 0},   // enc2B
  {A2, A3, 0},   // enc2C
  {A0, A1, 0}    // enc2D
};

// Додано: лічильники кроків для енкодерів
int encoderStepCounter[8] = {0};
const int STEP_THRESHOLD = 1; // N - кількість кроків для зміни значення

// ------------------ КНОПКИ ------------------
#define BTN_MODE1_PIN A6  
#define BTN_MODE2_PIN A7  

const unsigned long DEBOUNCE_MS = 50;
unsigned long lastBtnPressTime1 = 0;
unsigned long lastBtnPressTime2 = 0;
int lastBtnState1 = 1023;
int lastBtnState2 = 1023;

// ------------------ ПАРАМЕТРИ ------------------
#define ENC_STEP 5 

CRGB leds1[NUM_LEDS];
CRGB leds2[NUM_LEDS];

enum State { PASSIVE, GAME, WIN_EFFECT };
State statePortal1 = PASSIVE;
State statePortal2 = PASSIVE;

// PASSIVE
int hue1 = 0, sat1 = 255, val1 = 150, mode1 = 0;
int hue2 = 100, sat2 = 255, val2 = 150, mode2 = 0;

// GAME
int pos1 = 0, pos2 = 20;
int speed1 = 2, speed2 = 3;
CRGB color1 = CRGB::Red;
CRGB color2 = CRGB::Blue;

// енкодери попередні значення (для кольорів/швидкості)
// (більше не потрібні prev-змінні)

// Таймери для неблокуючого виконання
unsigned long lastTickPassive = 0;
const unsigned long PASSIVE_INTERVAL = 20; // ~50 FPS passive effects

unsigned long lastTickGame = 0;
const unsigned long GAME_INTERVAL = 25; // ~40 FPS game effects

unsigned long lastPrintStatus = 0;
const unsigned long PRINT_INTERVAL = 200; // 200ms for printStatus

unsigned long lastMove1 = 0;
unsigned long lastMove2 = 0;

// Для winEffect
unsigned long winEffectStartTime1 = 0;
unsigned long winEffectStartTime2 = 0;
const unsigned long WIN_EFFECT_DURATION = 600;
bool winEffectActive1 = false;
bool winEffectActive2 = false;

unsigned long winEffectLastUpdate1 = 0;
unsigned long winEffectLastUpdate2 = 0;
const unsigned long WIN_EFFECT_UPDATE_INTERVAL = 50; // update win effect every 50ms

// Для пульсації в showGame
uint16_t gamePulseTick = 0;
// Для пасивних ефектів
uint16_t passiveTick = 0;

// ------------------ Функція оновлення енкодерів ------------------
void updateEncoders() {
  for (int i = 0; i < 8; i++) {
    EncoderPins &enc = encoders[i];
    int pinAState = digitalRead(enc.pinA);
    int pinBState = digitalRead(enc.pinB);
    int currentState = (pinAState << 1) | pinBState;
    int lastState = enc.lastState;
    int delta = 0;

    // Визначаємо напрямок обертання енкодера по зміні стану A/B
    if (lastState == 0b00) {
      if (currentState == 0b01) delta = 1;
      else if (currentState == 0b10) delta = -1;
    } else if (lastState == 0b01) {
      if (currentState == 0b11) delta = 1;
      else if (currentState == 0b00) delta = -1;
    } else if (lastState == 0b11) {
      if (currentState == 0b10) delta = 1;
      else if (currentState == 0b01) delta = -1;
    } else if (lastState == 0b10) {
      if (currentState == 0b00) delta = 1;
      else if (currentState == 0b11) delta = -1;
    }

    // Інвертуємо напрямок один раз
    delta = -delta;

    if (delta != 0) {
      encoderStepCounter[i] += abs(delta);
      if (encoderStepCounter[i] >= STEP_THRESHOLD) {
        // Змінюємо значення лише при досягненні порогу кроків
        int stepDelta = (delta > 0) ? 1 : -1;
        switch (i) {
          case 0: // enc1A (color1.r)
            color1.r = constrain(color1.r + stepDelta * COLOR_STEP, 0, 255);
            break;
          case 1: // enc1B (color1.g)
            color1.g = constrain(color1.g + stepDelta * COLOR_STEP, 0, 255);
            break;
          case 2: // enc1C (color1.b)
            color1.b = constrain(color1.b + stepDelta * COLOR_STEP, 0, 255);
            break;
          case 3: // enc1D (speed1)
            speed1 = constrain(speed1 + stepDelta, 1, 10);
            break;
          case 4: // enc2A (color2.r)
            color2.r = constrain(color2.r + stepDelta * COLOR_STEP, 0, 255);
            break;
          case 5: // enc2B (color2.g)
            color2.g = constrain(color2.g + stepDelta * COLOR_STEP, 0, 255);
            break;
          case 6: // enc2C (color2.b)
            color2.b = constrain(color2.b + stepDelta * COLOR_STEP, 0, 255);
            break;
          case 7: // enc2D (speed2)
            speed2 = constrain(speed2 + stepDelta, 1, 10);
            break;
        }
        encoderStepCounter[i] = 0; // скидаємо лічильник після зміни
      }
    }

    enc.lastState = currentState;
  }
}

// ------------------ ЧИТАННЯ ЕНКОДЕРІВ ------------------
// Функція більше не потрібна, оскільки все обробляється в updateEncoders()

// ------------------ ПАСИВНІ ЕФЕКТИ ------------------
void showPassive(CRGB *leds, int hue, int sat, int val, int mode) {
  if (mode == 0) {
    fill_rainbow(leds, NUM_LEDS, passiveTick / 5, 5);
  } else if (mode == 1) {
    uint8_t pulse = 128 + 127 * ((uint8_t)(sin8(passiveTick / 4))) / 255; 
    fill_solid(leds, NUM_LEDS, CHSV(hue, sat, (val * pulse) / 255));
  } else if (mode == 2) {
    for (int i = 0; i < NUM_LEDS; i++) {
      leds[i] = CHSV((hue + i*10 + passiveTick/3) % 255, sat, val);
    }
  }
}

// ------------------ ГРА ------------------
void showGame(CRGB *leds, int &pos, CRGB color, int speed, int diff, unsigned long &lastMove) {
  fill_solid(leds, NUM_LEDS, CRGB::Black);

  const int TAIL = 10;  // у 2 рази довший хвіст

  // малюємо хвіст з 2 світлодіодів на крок
  for (int t = 0; t < TAIL; t++) {
    int index = (pos - t + NUM_LEDS) % NUM_LEDS;
    int index2 = (index + 1) % NUM_LEDS;
    uint8_t baseBright = 255 - t * (255 / TAIL); 
    CRGB c = color;
    leds[index] = c;
    leds[index].fadeLightBy(255 - baseBright);
    leds[index2] = c;
    leds[index2].fadeLightBy(255 - baseBright);
  }

  // пульсація для динаміки
  static uint16_t pulseTick = 0;
  pulseTick++;
  uint8_t pulse = 180 + 75 * ((uint8_t)(sin8(pulseTick / 5))) / 255;
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i].fadeLightBy(255 - pulse);
  }

  // контроль швидкості через millis()
  unsigned long now = millis();

  // робимо швидкість у 4 рази повільніше
  unsigned long interval = map(speed, 1, 10, 200, 20); 
  // speed=1 -> 200мс на крок (повільно), speed=10 -> 20мс на крок (швидко)

  if (now - lastMove >= interval) {
    lastMove = now;
    pos = (pos + 1) % NUM_LEDS;
  }
}

void showGame_old(CRGB *leds, int &pos, CRGB color, int speed, int diff) {
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  const int TAIL = 5;
  for (int t = 0; t < TAIL; t++) {
    int index = (pos - t + NUM_LEDS) % NUM_LEDS;
    uint8_t baseBright = 255 - t * 40; 
    int bright = constrain(baseBright, 50, 255);
    CRGB c = color;
    leds[index] = c;
    leds[index].fadeLightBy(255 - bright);
  }

  uint8_t pulse = 180 + 75 * ((uint8_t)(sin8(gamePulseTick / 5))) / 255;
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i].fadeLightBy(255 - pulse);
  }

  pos = (pos + speed + NUM_LEDS) % NUM_LEDS;
}

bool isWin(CRGB c1, CRGB c2, int p1, int p2, int s1, int s2){
  if (abs(c1.r - c2.r) > 12) return false;
  if (abs(c1.g - c2.g) > 12) return false;
  if (abs(c1.b - c2.b) > 12) return false;
  if (abs(p1 - p2) > 2) return false;
  if (abs(s1 - s2) > 1) return false;
  return true;
}

// ------------------ WIN EFFECT ------------------
void startWinEffect(int portal) {
  if (portal == 1) {
    statePortal1 = WIN_EFFECT;
    winEffectActive1 = true;
    winEffectStartTime1 = millis();
    winEffectLastUpdate1 = 0;
  } else if (portal == 2) {
    statePortal2 = WIN_EFFECT;
    winEffectActive2 = true;
    winEffectStartTime2 = millis();
    winEffectLastUpdate2 = 0;
  }
}

void updateWinEffect(int portal, CRGB *leds) {
  unsigned long now = millis();
  if (portal == 1 && winEffectActive1) {
    if (now - winEffectStartTime1 >= WIN_EFFECT_DURATION) {
      winEffectActive1 = false;
      statePortal1 = PASSIVE;
    } else if (now - winEffectLastUpdate1 >= WIN_EFFECT_UPDATE_INTERVAL) {
      winEffectLastUpdate1 = now;
      for (int i = 0; i < NUM_LEDS; i++) leds[i] = CHSV(random8(), 255, 255);
    }
  } 
  else if (portal == 2 && winEffectActive2) {
    if (now - winEffectStartTime2 >= WIN_EFFECT_DURATION) {
      winEffectActive2 = false;
      statePortal2 = PASSIVE;
    } else if (now - winEffectLastUpdate2 >= WIN_EFFECT_UPDATE_INTERVAL) {
      winEffectLastUpdate2 = now;
      for (int i = 0; i < NUM_LEDS; i++) leds[i] = CHSV(random8(), 255, 255);
    }
  }
}

// ------------------ ІНІЦІАЛІЗАЦІЯ ------------------
void resetGamePortal1() {
  pos1 = random(NUM_LEDS);
  speed1 = random(1, 6);
  color1 = CHSV(random8(), 200, 255);
}
void resetGamePortal2() {
  pos2 = random(NUM_LEDS);
  speed2 = random(1, 6);
  color2 = CHSV(random8(), 200, 255);
}

// ------------------ КНОПКИ ------------------
void checkModeButtons() {
  int cur1 = analogRead(BTN_MODE1_PIN);
  int cur2 = analogRead(BTN_MODE2_PIN);

  unsigned long now = millis();

  if (((cur1 < 100 && lastBtnState1 > 500) || (cur2 < 100 && lastBtnState2 > 500)) && (now - lastBtnPressTime1 > DEBOUNCE_MS) && (now - lastBtnPressTime2 > DEBOUNCE_MS)) {
    lastBtnPressTime1 = now;
    lastBtnPressTime2 = now;
    if (statePortal1 == PASSIVE && statePortal2 == PASSIVE) {
      statePortal1 = GAME;
      statePortal2 = GAME;
      resetGamePortal1();
      resetGamePortal2();
    } else if (statePortal1 == GAME && statePortal2 == GAME) {
      statePortal1 = PASSIVE;
      statePortal2 = PASSIVE;
    } else {
      // ignore button during win effect or mixed states
    }
    Serial.println("==> BUTTON pressed, switching both portals mode");
  }
  lastBtnState1 = cur1;
  lastBtnState2 = cur2;
}

// --------------- Вивід значень ---------------
void printStatus() {
  Serial.print(F("P1 R=")); Serial.print(color1.r);
  Serial.print(F(" G=")); Serial.print(color1.g);
  Serial.print(F(" B=")); Serial.print(color1.b);
  Serial.print(F(" S=")); Serial.print(speed1);
  Serial.print(F(" | P2 R=")); Serial.print(color2.r);
  Serial.print(F(" G=")); Serial.print(color2.g);
  Serial.print(F(" B=")); Serial.print(color2.b);
  Serial.print(F(" S=")); Serial.println(speed2);

  Serial.print(F("State P1: "));
  if(statePortal1 == PASSIVE) Serial.print("PASSIVE");
  else if(statePortal1 == GAME) Serial.print("GAME");
  else if(statePortal1 == WIN_EFFECT) Serial.print("WIN_EFFECT");

  Serial.print(F(" | State P2: "));
  if(statePortal2 == PASSIVE) Serial.println("PASSIVE");
  else if(statePortal2 == GAME) Serial.println("GAME");
  else if(statePortal2 == WIN_EFFECT) Serial.println("WIN_EFFECT");
}

// ------------------ ГОЛОВНИЙ ЦИКЛ ------------------
void loop() {
  unsigned long now = millis();

  // Постійно оновлюємо енкодери без затримок
  updateEncoders();

  // Перевірка кнопок
  checkModeButtons();

  // Оновлення пасивного таймера
  if (now - lastTickPassive >= PASSIVE_INTERVAL) {
    lastTickPassive = now;
    passiveTick++;
  }

  // Оновлення таймера гри
  if (now - lastTickGame >= GAME_INTERVAL) {
    lastTickGame = now;
    gamePulseTick++;
  }

  int diff = abs(pos1 - pos2);
  if (diff > NUM_LEDS/2) diff = NUM_LEDS - diff; 

  // Якщо Win Effect активний - оновлюємо його
  if (statePortal1 == WIN_EFFECT) {
    updateWinEffect(1, leds1);
  } else if (statePortal1 == PASSIVE) {
    showPassive(leds1, hue1, sat1, val1, mode1);
  } else if (statePortal1 == GAME) {
    showGame(leds1, pos1, color1, speed1, diff, lastMove1);
  }

  if (statePortal2 == WIN_EFFECT) {
    updateWinEffect(2, leds2);
  } else if (statePortal2 == PASSIVE) {
    showPassive(leds2, hue2, sat2, val2, mode2);
  } else if (statePortal2 == GAME) {
    showGame(leds2, pos2, color2, speed2, diff, lastMove2);
  }

  // Перевірка умови перемоги, якщо обидва в грі і не в win effect
  if (statePortal1 == GAME && statePortal2 == GAME && isWin(color1, color2, pos1, pos2, speed1, speed2)) {
    startWinEffect(1);
    startWinEffect(2);
  }

  FastLED.show();

  // Вивід статусу раз на 200мс
  if (now - lastPrintStatus >= PRINT_INTERVAL) {
    lastPrintStatus = now;
    printStatus();
  }
}

void loop2(){
  updateEncoders();
  printStatus();
}


// ------------------ SETUP ------------------
void setup() {
  Serial.begin(115200);
  delay(100); // Дати час Serial піднятися
  FastLED.addLeds<LED_TYPE, LED_PIN_PORTAL1, COLOR_ORDER>(leds1, NUM_LEDS).setCorrection(TypicalLEDStrip);
  FastLED.addLeds<LED_TYPE, LED_PIN_PORTAL2, COLOR_ORDER>(leds2, NUM_LEDS).setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(BRIGHTNESS);

  // Встановити INPUT_PULLUP для всіх A/B сигналів енкодерів
  pinMode(12, INPUT_PULLUP);
  pinMode(11, INPUT_PULLUP);
  pinMode(10, INPUT_PULLUP);
  pinMode(9, INPUT_PULLUP);
  pinMode(8, INPUT_PULLUP);
  pinMode(7, INPUT_PULLUP);
  pinMode(5, INPUT_PULLUP);
  pinMode(4, INPUT_PULLUP);

  pinMode(3, INPUT_PULLUP);
  pinMode(2, INPUT_PULLUP);
  pinMode(A5, INPUT_PULLUP);
  pinMode(A4, INPUT_PULLUP);
  pinMode(A2, INPUT_PULLUP);
  pinMode(A3, INPUT_PULLUP);
  pinMode(A0, INPUT_PULLUP);
  pinMode(A1, INPUT_PULLUP);

  // SW контакти енкодерів (кнопки)
  pinMode(BTN_MODE1_PIN, INPUT_PULLUP);
  pinMode(BTN_MODE2_PIN, INPUT_PULLUP);
  // Початковий стан для генератора випадкових чисел
  randomSeed(analogRead(0));

  // Ініціалізуємо lastState для енкодерів
  for (int i = 0; i < 8; i++) {
    int pinAState = digitalRead(encoders[i].pinA);
    int pinBState = digitalRead(encoders[i].pinB);
    encoders[i].lastState = (pinAState << 1) | pinBState;
  }
}