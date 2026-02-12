/*
  Project: Happy Birthday Niche
  Description:
  Система відкриття потаємної ніші з тортом.
  При натисканні кнопки або сигналу з пульта відкриваються два електромагнітні замки (із затримкою)
  і вмикається підсвітка. Кнопка "ВИКЛ" повертає систему в початковий стан.
  Використовується бібліотека RCSwitch для прийому кодів з пульта 315/433 MHz.
*/

// --- Підключення бібліотеки --- //
#include <RCSwitch.h>
#include <SoftwareSerial.h>
#include <DFRobotDFPlayerMini.h>

// --- Налаштування пінів --- //
#define RELAY1 8   // Замок 1 (NC)
#define RELAY2 3   // Замок 2 (NC)
#define RELAY3 4   // Підсвітка (NO)
#define RELAY4 5   // Резерв

#define BUTTON_ON 6    // Кнопка ВКЛ
#define BUTTON_OFF 7   // Кнопка ВИКЛ

#define RF_PIN 2       // Вхід приймача MX-RM-5V (DATA)

// --- Ініціалізація об’єкта пульта --- //
RCSwitch mySwitch = RCSwitch();

// --- DFPlayer --- //
#define DF_RX 11   // Arduino RX  (підключено до TX DFPlayer)
#define DF_TX 12   // Arduino TX  (підключено до RX DFPlayer)

SoftwareSerial dfSerial(DF_RX, DF_TX);
DFRobotDFPlayerMini dfPlayer;
bool dfReady = false;

// --- Параметри --- //
const unsigned long unlockDelay = 5000; // затримка між замками, мс
bool systemActive = false;               // поточний стан системи

// --- Для антидребезгу кнопок --- //
unsigned long lastButtonTime = 0;
const unsigned long debounceDelay = 200;

// --- Списки дозволених кодів пульта --- //
const unsigned long RF_ON_CODES[]  = {3282337, 1234567};   // “вкл”
const unsigned long RF_OFF_CODES[] = {3282338, 1234568};   // “викл”
const int NUM_RF_ON_CODES  = sizeof(RF_ON_CODES)  / sizeof(RF_ON_CODES[0]);
const int NUM_RF_OFF_CODES = sizeof(RF_OFF_CODES) / sizeof(RF_OFF_CODES[0]);

// --- Прототипи функцій --- //
void activateSystem();
void deactivateSystem();
void resetRelays();
bool buttonPressed(int pin);
bool isCodeInList(unsigned long code, const unsigned long* list, int length);

void setup() {
  Serial.begin(9600);
  Serial.println("=== Happy Birthday Niche ===");
  Serial.println("Очікування сигналів пульта...");

  // --- DFPlayer init --- //
dfSerial.begin(9600);

if (dfPlayer.begin(dfSerial, false)) {   // без ACK (non-blocking)
  dfPlayer.setTimeOut(200);
  dfPlayer.volume(20);                  // 0..30
  delay(100);
  dfReady = true;
  Serial.println("DFPlayer OK");
} else {
  Serial.println("DFPlayer FAIL");
}

  // Ініціалізація реле
  pinMode(RELAY1, OUTPUT);
  pinMode(RELAY2, OUTPUT);
  pinMode(RELAY3, OUTPUT);
  pinMode(RELAY4, OUTPUT);
  resetRelays();

  // Кнопки
  pinMode(BUTTON_ON, INPUT_PULLUP);
  pinMode(BUTTON_OFF, INPUT_PULLUP);

  // Приймач
  mySwitch.enableReceive(digitalPinToInterrupt(RF_PIN)); // RX pin
}

void loop() {
  // --- Зчитування кнопок --- //
  if (buttonPressed(BUTTON_ON)) activateSystem();
  if (buttonPressed(BUTTON_OFF)) deactivateSystem();

  // --- Зчитування сигналів з пульта --- //
  if (mySwitch.available()) {
    unsigned long code = mySwitch.getReceivedValue();
    Serial.print("Отримано RF код: ");
    Serial.println(code);
    Serial.print("Біт довжина: ");
    Serial.println(mySwitch.getReceivedBitlength());
    Serial.print("Протокол: ");
    Serial.println(mySwitch.getReceivedProtocol());
    Serial.println("-----------------------------");

    if (isCodeInList(code, RF_ON_CODES, NUM_RF_ON_CODES)) {
      Serial.println("→ Розпізнано код ВКЛ");
      delay(500);
      activateSystem();
    }
    if (isCodeInList(code, RF_OFF_CODES, NUM_RF_OFF_CODES)) {
      Serial.println("→ Розпізнано код ВИКЛ");
      delay(500);
      deactivateSystem();
    }
    else {
      Serial.println("→ Невідомий код (ігнорується)");
    }

    mySwitch.resetAvailable();
  }
}

// --- Функції --- //

bool buttonPressed(int pin) {
  if (digitalRead(pin) == LOW && millis() - lastButtonTime > debounceDelay) {
    lastButtonTime = millis();
    return true;
  }
  return false;
}

void activateSystem() {
  if (systemActive) return;
  systemActive = true;

  Serial.println("System ACTIVATED");

  digitalWrite(RELAY1, HIGH); // розімкнути замок 1
  digitalWrite(RELAY3, LOW); // увімкнути підсвітку
  if (dfReady) {
  dfPlayer.play(1);   // грає файл 0001.mp3
}
  delay(unlockDelay);
  digitalWrite(RELAY2, HIGH); // розімкнути замок 2
  digitalWrite(RELAY4, LOW); // розімкнути резерв
}

void deactivateSystem() {
  if (!systemActive) return;
  systemActive = false;
  Serial.println("System DEACTIVATED");

  if (dfReady) {
  dfPlayer.stop();
}

  resetRelays();
}

void resetRelays() {
  // NC-замки — активні при LOW
  digitalWrite(RELAY1, LOW); // замкнути замок 1
  digitalWrite(RELAY2, LOW); // замкнути замок 2
  digitalWrite(RELAY3, HIGH);  // вимкнути підсвітку
  digitalWrite(RELAY4, HIGH);  // резерв
}

bool isCodeInList(unsigned long code, const unsigned long* list, int length) {
  for (int i = 0; i < length; i++) {
    if (code == list[i]) return true;
  }
  return false;
}