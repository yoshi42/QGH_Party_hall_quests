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
const int THRESHOLD = 3000;       // поріг спрацювання датчика
const int NUM_SOUNDS = 5;         // кількість файлів у кожній папці (01, 02, ...)

int lastState[16];  // попередні стани каналів (0/1)
int currentState[16];
int nextSound[16] = {0};  // наступний трек для кожного каналу (0..NUM_SOUNDS-1)

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

  // --- DFPlayer ---
  dfSerial.begin(9600, SERIAL_8N1, DFPLAYER_RX, DFPLAYER_TX);
  if (!dfPlayer.begin(dfSerial)) {
    Serial.println("❌ DFPlayer Mini not detected!");
    while (true) delay(100);
  }
  Serial.println("✅ DFPlayer Mini online.");
  dfPlayer.volume(10);  // Гучність (0-30)
  delay(500);
}

void loop() {
  for (int ch = 0; ch < NUM_CHANNELS; ch++) {
    int value = readChannel(ch);
    currentState[ch] = (value < THRESHOLD) ? 0 : 1;

    Serial.printf("CH%02d: %4d → %d\n", ch, value, currentState[ch]);

    if (currentState[ch] == 1 && lastState[ch] == 0) {
      int sound = nextSound[ch] + 1;   // 1..NUM_SOUNDS
      Serial.printf("▶️ TRIGGER: sensor %d → play %02d/%02d (seq)\n",
                    ch, ch + 1, sound);
      dfPlayer.playFolder(ch + 1, sound);
      nextSound[ch] = (nextSound[ch] + 1) % NUM_SOUNDS;  // інкремент з циклом
      delay(300);
    }

    lastState[ch] = currentState[ch];
  }
  delay(150);
}