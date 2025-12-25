#include <Arduino.h>
#include <DFRobotDFPlayerMini.h>

// --- Мультиплексор ---
#define S0 5
#define S1 6
#define S2 7
#define S3 8
#define SIG 1   // ADC1_CH5 (GPIO5)

// --- DFPlayer Mini ---
#define DFPLAYER_RX 20   // RX ESP32-C3 ← TX DFPlayer
#define DFPLAYER_TX 21  // TX ESP32-C3 → RX DFPlayer

HardwareSerial dfSerial(1);  
DFRobotDFPlayerMini dfPlayer;

// --- Налаштування ---
const int NUM_CHANNELS = 16;      // скільки датчиків (0..16)
const int DEFAULT_THRESHOLD = 30;       // базовий поріг (у відхиленні від baseline)
const int NUM_SOUNDS = 5;         // кількість файлів у кожній папці (01, 02, ...)

const int TRIGGER_SAMPLES = 4;    // скільки послідовних вимірювань потрібно для спрацювання (3-5 ок)
const int RELEASE_SAMPLES = 3;    // скільки послідовних вимірювань для "відпускання" (щоб не дрібезжало)
const int RELEASE_HYST = 10;      // гістерезис на відпускання (нижче порога на 10)
const int LOOP_DELAY_MS = 50;     // затримка циклу (менше, щоб 3-5 вимірів набирались швидко)

int lastState[16];  // попередні стани каналів (0/1)
int currentState[16];
int threshold[16];              // індивідуальний поріг для кожного датчика
int aboveCount[16] = {0};        // лічильник послідовних перевищень порога
int belowCount[16] = {0};        // лічильник послідовних значень нижче порога (для відпускання)
int nextSound[16] = {0};
int baseline[16] = {0};  // базові значення для калібровки

// --- Функція вибору каналу мультиплексора ---
void selectChannel(int channel) {
  digitalWrite(S0, channel & 0x01);
  digitalWrite(S1, (channel >> 1) & 0x01);
  digitalWrite(S2, (channel >> 2) & 0x01);
  digitalWrite(S3, (channel >> 3) & 0x01);
}

int readChannel(int ch) {
  selectChannel(ch);
  delayMicroseconds(200);
  long s = 0;
  for (int i = 0; i < 10; i++) {
    s += analogRead(SIG);
    delayMicroseconds(50);
  }
  return s / 10;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("ESP32-C3 Game Board Starting...");

  pinMode(S0, OUTPUT);
  pinMode(S1, OUTPUT);
  pinMode(S2, OUTPUT);
  pinMode(S3, OUTPUT);

  for (int i = 0; i < 16; i++) lastState[i] = 0;
  for (int i = 0; i < 16; i++) nextSound[i] = 0;

  // Пороги можна налаштувати індивідуально. За замовчуванням 30.
  // Якщо хочеш – вистав вручну конкретні значення нижче.
  for (int i = 0; i < 16; i++) threshold[i] = DEFAULT_THRESHOLD;

  // приклад ручної підстройки (розкоментуй і підправ за потреби):
  threshold[0] = 20;   // S1
  threshold[1] = 30;   // S2
  threshold[2] = 50;   // S3
  threshold[3] = 20;   // S4
  threshold[4] = 30;   // S5
  threshold[5] = 50;   // S6
  threshold[6] = 20;   // S7
  threshold[7] = 30;   // S8
  threshold[8] = 50;   // S9
  threshold[9] = 20;   // S10
  threshold[10] = 30;   // S11
  threshold[11] = 50;   // S12
  threshold[12] = 20;   // S13
  threshold[13] = 30;   // S14
  threshold[14] = 50;   // S15
  threshold[15] = 20;   // S16
  // ...

  for (int i = 0; i < 16; i++) aboveCount[i] = 0;
  for (int i = 0; i < 16; i++) belowCount[i] = 0;

  Serial.println("Thresholds (delta from baseline) per sensor:");
  for (int i = 0; i < NUM_CHANNELS; i++) {
    Serial.printf("S%d threshold = +%d\n", i + 1, threshold[i]);
  }
  Serial.println();

  Serial.println("Calibrating sensors...");
  for (int ch = 0; ch < NUM_CHANNELS; ch++) {
    long sum = 0;
    for (int i = 0; i < 10; i++) {   // 10 вимірів
      sum += readChannel(ch);
      delay(20);
    }
    baseline[ch] = sum / 10;
    Serial.printf("Baseline S%d = %d\n", ch + 1, baseline[ch]);
  }
  Serial.println("Calibration complete.\n");

  // --- DFPlayer ---
  dfSerial.begin(9600, SERIAL_8N1, DFPLAYER_RX, DFPLAYER_TX);
  if (!dfPlayer.begin(dfSerial)) {
    Serial.println("❌ DFPlayer Mini not detected!");
    while (true) delay(100);
  }
  Serial.println("✅ DFPlayer Mini online.");
  dfPlayer.volume(25);  // Гучність (0-30)
  delay(500);
}

void loop() {
  for (int ch = 0; ch < NUM_CHANNELS; ch++) {
    int value = readChannel(ch);
    int adjusted = value - baseline[ch];
    Serial.printf("S%d: %d (adj: %+d)\n", ch + 1, value, adjusted);

    // multi-sample debounce on adjusted deviation
    int onTh = threshold[ch];
    int offTh = threshold[ch] - RELEASE_HYST; // відпускання трохи нижче, щоб не дрібезжало

    // рахуємо послідовні перевищення / зниження
    if (adjusted > onTh) {
      aboveCount[ch] = min(aboveCount[ch] + 1, 1000);
      belowCount[ch] = 0;
    } else if (adjusted < offTh) {
      belowCount[ch] = min(belowCount[ch] + 1, 1000);
      aboveCount[ch] = 0;
    } else {
      // в зоні гістерезису – не міняємо стани, але й не накопичуємо
      // (можна залишити лічильники як є, але так стабільніше)
      aboveCount[ch] = 0;
      belowCount[ch] = 0;
    }

    // визначаємо підтверджений стан магніта
    if (lastState[ch] == 0) {
      // зараз "нема магніта" – чекаємо TRIGGER_SAMPLES послідовних перевищень
      currentState[ch] = (aboveCount[ch] >= TRIGGER_SAMPLES) ? 1 : 0;
    } else {
      // зараз "магніт є" – чекаємо RELEASE_SAMPLES послідовних значень нижче offTh
      currentState[ch] = (belowCount[ch] >= RELEASE_SAMPLES) ? 0 : 1;
    }

    // тригер лише при переході 0 -> 1
    if (currentState[ch] == 1 && lastState[ch] == 0) {
      int sound = nextSound[ch] + 1;   // 1..NUM_SOUNDS
      Serial.printf("▶️ TRIGGER (CONFIRMED): S%d → play %02d/%02d\n",
                    ch + 1, ch + 1, sound);
      dfPlayer.playFolder(ch + 1, sound);
      nextSound[ch] = (nextSound[ch] + 1) % NUM_SOUNDS;  // цикл 1..5
    }

    lastState[ch] = currentState[ch];
  }
  delay(LOOP_DELAY_MS);
}