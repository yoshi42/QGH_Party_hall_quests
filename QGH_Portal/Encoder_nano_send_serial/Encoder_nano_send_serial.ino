// Тест усіх 8 енкодерів без бібліотек
// A/B виходи підключені згідно з твоєю схемою

#include <SoftwareSerial.h>

struct EncoderPins {
  int pinA;
  int pinB;
  long counter;
  int lastA;
  int lastB;
  int lastState;
};

// Налаштуй пін-мапінг для своїх енкодерів
EncoderPins encoders[8] = {
  {12, 11, 0, HIGH, HIGH, 3}, // Portal1 A
  {10, 9,  0, HIGH, HIGH, 3}, // Portal1 B
  {8, 7,   0, HIGH, HIGH, 3}, // Portal1 C
  {5, 4,   0, HIGH, HIGH, 3}, // Portal1 D
  {3, 2,   0, HIGH, HIGH, 3}, // Portal2 A
  {A5, A4, 0, HIGH, HIGH, 3}, // Portal2 B
  {A2, A3, 0, HIGH, HIGH, 3}, // Portal2 C
  {A0, A1, 0, HIGH, HIGH, 3}  // Portal2 D
};

#define BTN_MODE1_PIN A6  
#define BTN_MODE2_PIN A7  

SoftwareSerial ss(13, 6); // RX, TX (RX можна залишити не підключеним)

void setup() {
  Serial.begin(115200);
  ss.begin(115200);
  delay(200);

  for (int i = 0; i < 8; i++) {
    pinMode(encoders[i].pinA, INPUT_PULLUP);
    pinMode(encoders[i].pinB, INPUT_PULLUP);
    encoders[i].lastA = digitalRead(encoders[i].pinA);
    encoders[i].lastB = digitalRead(encoders[i].pinB);
    encoders[i].lastState = (encoders[i].lastA << 1) | encoders[i].lastB;
  }

  pinMode(BTN_MODE1_PIN, INPUT_PULLUP);
  pinMode(BTN_MODE2_PIN, INPUT_PULLUP);

  Serial.println("Encoder test started");
}

void loop() {
  for (int i = 0; i < 8; i++) {
    int curA = digitalRead(encoders[i].pinA);
    int curB = digitalRead(encoders[i].pinB);
    int currentState = (curA << 1) | curB;

    if (currentState != encoders[i].lastState) {
      // Квадратичний детектор напрямку (повний цикл)
      int transition = (encoders[i].lastState << 2) | currentState;
      // Таблиця переходів: 4-бітний код переходу
      // 00->01 : 0b0001 = 1, CW
      // 01->11 : 0b0111 = 7, CW
      // 11->10 : 0b1110 = 14, CW
      // 10->00 : 0b1000 = 8, CW
      // 00->10 : 0b0010 = 2, CCW
      // 10->11 : 0b1011 = 11, CCW
      // 11->01 : 0b1101 = 13, CCW
      // 01->00 : 0b0100 = 4, CCW

      if (transition == 1 || transition == 7 || transition == 14 || transition == 8) {
        encoders[i].counter--; // CW (інвертовано)
      } else if (transition == 2 || transition == 11 || transition == 13 || transition == 4) {
        encoders[i].counter++; // CCW (інвертовано)
      }
      encoders[i].lastState = currentState;
      encoders[i].lastA = curA;
      encoders[i].lastB = curB;

      // Вивести значення одразу при зміні
      Serial.print("Enc");
      Serial.print(i + 1);
      Serial.print(" = ");
      Serial.println(encoders[i].counter);
    }
  }

  int btn1State = analogRead(BTN_MODE1_PIN) < 512 ? 1 : 0;
  int btn2State = analogRead(BTN_MODE2_PIN) < 512 ? 1 : 0;

  // Відправка даних через SoftwareSerial у форматі:
  // E1:<counter> E2:<counter> ... E8:<counter> BTN1:<0|1> BTN2:<0|1>\n
  for (int i = 0; i < 8; i++) {
    ss.print("E");
    ss.print(i + 1);
    ss.print(":");
    ss.print(encoders[i].counter);
    ss.print(" ");
    // Дублюємо у апаратний Serial
    Serial.print("E");
    Serial.print(i + 1);
    Serial.print(":");
    Serial.print(encoders[i].counter);
    Serial.print(" ");
  }
  ss.print("BTN1:");
  ss.print(btn1State);
  ss.print(" BTN2:");
  ss.print(btn2State);
  ss.print("\n");
  // Дублюємо кнопки у апаратний Serial
  Serial.print("BTN1:");
  Serial.print(btn1State);
  Serial.print(" BTN2:");
  Serial.print(btn2State);
  Serial.print("\n");

  /*Serial.print("BTN1 = ");
  Serial.print(analogRead(BTN_MODE1_PIN));
  Serial.print("   BTN2 = ");
  Serial.println(analogRead(BTN_MODE2_PIN));*/
}