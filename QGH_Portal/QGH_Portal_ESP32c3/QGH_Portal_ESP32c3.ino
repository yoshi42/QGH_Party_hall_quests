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

#define LED_PIN_PORTAL1 9
#define LED_PIN_PORTAL2 10
#define NUM_LEDS 216 //к-сть діодів
#define BRIGHTNESS 200
#define COLOR_STEP 20  // крок зміни кольору
#define LED_TYPE WS2812
#define COLOR_ORDER GRB

// UART1 pins for ESP32-C3
#define RX_PIN 2
#define TX_PIN 4

const unsigned long DEBOUNCE_MS = 50;
unsigned long lastBtnPressTime1 = 0;
unsigned long lastBtnPressTime2 = 0;
int lastBtnState1 = 1023;
int lastBtnState2 = 1023;

// ------------------ ПАРАМЕТРИ ------------------
CRGB leds1[NUM_LEDS];
CRGB leds2[NUM_LEDS];

enum State { PASSIVE, GAME, WIN_EFFECT };
State statePortal1 = PASSIVE;
State statePortal2 = PASSIVE;

// Змінні, які будуть оновлюватись по UART (отримані з Arduino)
CRGB color1 = CRGB::Red;
CRGB color2 = CRGB::Blue;
int bright1 = 150;
int bright2 = 150;
int pos1 = 0, pos2 = 20;
int speed1 = 2, speed2 = 3;

// Таймери для неблокуючого виконання
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


// ------------------ SETUP ------------------
void setup() {
  Serial.begin(115200);
  delay(100); // Дати час Serial піднятися

  // Ініціалізація UART1 для прийому даних від Arduino Nano
  // Arduino Nano передає дані по своєму TX, ESP32-C3 приймає на RX_PIN (6)
  Serial1.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);
  delay(100);

  FastLED.addLeds<LED_TYPE, LED_PIN_PORTAL1, COLOR_ORDER>(leds1, NUM_LEDS).setCorrection(TypicalLEDStrip);
  FastLED.addLeds<LED_TYPE, LED_PIN_PORTAL2, COLOR_ORDER>(leds2, NUM_LEDS).setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(BRIGHTNESS);

  // Початковий стан для генератора випадкових чисел
  randomSeed(analogRead(0));
}

// ------------------ ПАСИВНИЙ РЕЖИМ: просте керування RGB+яскравість ------------------
void showPassive(CRGB *leds, CRGB color, int bright) {
  CRGB c = color;
  c.nscale8_video(bright);
  fill_solid(leds, NUM_LEDS, c);
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

// --------------- Вивід значень ---------------
void printStatus() {
  Serial.print(F("P1 R=")); Serial.print(color1.r);
  Serial.print(F(" G=")); Serial.print(color1.g);
  Serial.print(F(" B=")); Serial.print(color1.b);
  Serial.print(F(" Bright=")); Serial.print(bright1);
  Serial.print(F(" | P2 R=")); Serial.print(color2.r);
  Serial.print(F(" G=")); Serial.print(color2.g);
  Serial.print(F(" B=")); Serial.print(color2.b);
  Serial.print(F(" Bright=")); Serial.println(bright2);

  Serial.print(F("State P1: "));
  if(statePortal1 == PASSIVE) Serial.print("PASSIVE");
  else if(statePortal1 == GAME) Serial.print("GAME");
  else if(statePortal1 == WIN_EFFECT) Serial.print("WIN_EFFECT");

  Serial.print(F(" | State P2: "));
  if(statePortal2 == PASSIVE) Serial.println("PASSIVE");
  else if(statePortal2 == GAME) Serial.println("GAME");
  else if(statePortal2 == WIN_EFFECT) Serial.println("WIN_EFFECT");
}

// ------------------ Функція для парсингу вхідних даних по UART ------------------
// Формат прийому (новий):
// E1:84 E2:80 E3:0 E4:120 E5:0 E6:0 E7:0 E8:0 BTN1:0 BTN2:0\n
// де E1..E8 - значення енкодерів (long), BTN1, BTN2 - кнопки (0 або 1)
long encoders[8] = {0};
int btn1State = 0;
int btn2State = 0;

void parseUARTData(String &line) {
  // Розділити рядок на поля по пробілу
  int startIdx = 0;
  while (startIdx < line.length()) {
    int spaceIdx = line.indexOf(' ', startIdx);
    if (spaceIdx == -1) spaceIdx = line.length();
    String token = line.substring(startIdx, spaceIdx);
    startIdx = spaceIdx + 1;

    int colonIdx = token.indexOf(':');
    if (colonIdx == -1) continue;

    String key = token.substring(0, colonIdx);
    String valStr = token.substring(colonIdx + 1);
    long val = valStr.toInt();

    if (key == "E1") encoders[0] = val;
    else if (key == "E2") encoders[1] = val;
    else if (key == "E3") encoders[2] = val;
    else if (key == "E4") encoders[3] = val;
    else if (key == "E5") encoders[4] = val;
    else if (key == "E6") encoders[5] = val;
    else if (key == "E7") encoders[6] = val;
    else if (key == "E8") encoders[7] = val;
    else if (key == "BTN1") btn1State = (val != 0) ? 1 : 0;
    else if (key == "BTN2") btn2State = (val != 0) ? 1 : 0;
  }

  // Тепер оновити color1, bright1, color2, bright2 для пасивного режиму
  // Використовуємо E1..E3 як RGB для Portal1, E4 як яскравість Portal1
  // E5..E7 як RGB для Portal2, E8 як яскравість Portal2

  color1.r = constrain(encoders[0], 0, 255);
  color1.g = constrain(encoders[1], 0, 255);
  color1.b = constrain(encoders[2], 0, 255);
  bright1 = constrain(encoders[3], 0, 255);

  color2.r = constrain(encoders[4], 0, 255);
  color2.g = constrain(encoders[5], 0, 255);
  color2.b = constrain(encoders[6], 0, 255);
  bright2 = constrain(encoders[7], 0, 255);
}

// ------------------ ГОЛОВНИЙ ЦИКЛ ------------------
void loop() {
  unsigned long now = millis();

  // Прийом даних по UART1 (ESP32-C3 приймає дані від Arduino Nano)
  // Arduino передає дані по TX, ESP приймає їх на RX_PIN (6)
  static String inputLine = "";
  while (Serial1.available()) {
    char c = Serial1.read();
    if (c == '\n') {
      Serial.print("Received UART line: ");
      Serial.println(inputLine);
      parseUARTData(inputLine);
      inputLine = "";

      Serial.print("Portal1 state: ");
      if(statePortal1==PASSIVE) Serial.println("PASSIVE");
      else if(statePortal1==GAME) Serial.println("GAME");
      else if(statePortal1==WIN_EFFECT) Serial.println("WIN_EFFECT");
      
      Serial.print("Portal2 state: ");
      if(statePortal2==PASSIVE) Serial.println("PASSIVE");
      else if(statePortal2==GAME) Serial.println("GAME");
      else if(statePortal2==WIN_EFFECT) Serial.println("WIN_EFFECT");
    } else if (c != '\r') {
      inputLine += c;
      if (inputLine.length() > 100) inputLine = ""; // обмеження довжини рядка
    }
  }

  // Оновлення таймера гри
  if (now - lastTickGame >= GAME_INTERVAL) {
    lastTickGame = now;
  }

  int diff = abs(pos1 - pos2);
  if (diff > NUM_LEDS/2) diff = NUM_LEDS - diff; 

  // Якщо Win Effect активний - оновлюємо його
  if (statePortal1 == WIN_EFFECT) {
    updateWinEffect(1, leds1);
  } else if (statePortal1 == PASSIVE) {
    showPassive(leds1, color1, bright1);
  } else if (statePortal1 == GAME) {
    showGame(leds1, pos1, color1, speed1, diff, lastMove1);
  }

  if (statePortal2 == WIN_EFFECT) {
    updateWinEffect(2, leds2);
  } else if (statePortal2 == PASSIVE) {
    showPassive(leds2, color2, bright2);
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