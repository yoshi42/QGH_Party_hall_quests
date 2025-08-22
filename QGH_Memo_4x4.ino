/*
  4×4 Memory Puzzle with Button LEDs for Escape Room
  - 16 buttons arranged in a 4×4 grid, numbered 1..16
  - Each button has a controllable LED (on press correct → LED ON, wrong → all blink, success → all blink longer)
  - Automatically cycles through 16 variants, each starting from a different button
*/

const int ROWS = 4;
const int COLS = 4;
int buttonPins[ROWS][COLS] = {
  {2, 3, 4, 5},
  {6, 7, 8, 9},
  {10, 11, 12, 13},
  {A0, A1, A2, A3}
};
int ledPins[ROWS][COLS] = {
  {22, 23, 24, 25},
  {26, 27, 28, 29},
  {30, 31, 32, 33},
  {34, 35, 36, 37}
};
const int SEQ_LEN = 16;
int sequence[SEQ_LEN];
int stepIndex = 0;
int currentStart = 1;

bool lastState[ROWS][COLS];
unsigned long lastChangeMs[ROWS][COLS];
const unsigned long DEBOUNCE_MS = 35;

inline int rcToNum(int r, int c) { return r * COLS + c + 1; }
inline int numToRow(int n) { return (n - 1) / COLS; }
inline int numToCol(int n) { return (n - 1) % COLS; }

bool isNeighbor(int a, int b) {
  int r1 = numToRow(a), c1 = numToCol(a);
  int r2 = numToRow(b), c2 = numToCol(b);
  if (a == b) return false;
  return abs(r1 - r2) <= 1 && abs(c1 - c2) <= 1;
}

// Backtracking generator omitted for brevity (same as before)
bool generateSequence(int startNum, int outSeq[]);

void lightButton(int num, bool state) {
  int r = numToRow(num), c = numToCol(num);
  digitalWrite(ledPins[r][c], state ? HIGH : LOW);
}

void blinkAll(int times, int delayMs) {
  for (int i = 0; i < times; i++) {
    for (int r = 0; r < ROWS; r++)
      for (int c = 0; c < COLS; c++)
        digitalWrite(ledPins[r][c], HIGH);
    delay(delayMs);
    for (int r = 0; r < ROWS; r++)
      for (int c = 0; c < COLS; c++)
        digitalWrite(ledPins[r][c], LOW);
    delay(delayMs);
  }
}

void startNewVariant(int startNum) {
  currentStart = startNum;
  if (!generateSequence(currentStart, sequence)) {
    int altStart = (currentStart % 16) + 1;
    generateSequence(altStart, sequence);
    currentStart = altStart;
  }
  stepIndex = 0;

  // Reset LEDs, turn on start button
  for (int r = 0; r < ROWS; r++)
    for (int c = 0; c < COLS; c++)
      digitalWrite(ledPins[r][c], LOW);
  lightButton(currentStart, true);
}

void setup() {
  for (int r = 0; r < ROWS; r++) {
    for (int c = 0; c < COLS; c++) {
      pinMode(buttonPins[r][c], INPUT_PULLUP);
      pinMode(ledPins[r][c], OUTPUT);
      digitalWrite(ledPins[r][c], LOW);
      lastState[r][c] = false;
      lastChangeMs[r][c] = 0;
    }
  }
  randomSeed(analogRead(A7));
  startNewVariant(1);
}

int readPressedNumber() {
  int pressedNum = 0;
  unsigned long now = millis();
  for (int r = 0; r < ROWS; r++) {
    for (int c = 0; c < COLS; c++) {
      bool raw = digitalRead(buttonPins[r][c]) == LOW;
      if (raw != lastState[r][c] && now - lastChangeMs[r][c] >= DEBOUNCE_MS) {
        lastChangeMs[r][c] = now;
        lastState[r][c] = raw;
        if (raw && pressedNum == 0) pressedNum = rcToNum(r, c);
      }
    }
  }
  return pressedNum;
}

void loop() {
  int pressed = readPressedNumber();
  if (pressed != 0) {
    if (pressed == sequence[stepIndex]) {
      lightButton(pressed, true);
      stepIndex++;
      if (stepIndex >= SEQ_LEN) {
        blinkAll(4, 200); // success
        int nextStart = (currentStart % 16) + 1;
        startNewVariant(nextStart);
      }
    } else {
      blinkAll(2, 100); // error
      startNewVariant(currentStart); // reset, first button ON
    }
  }
}
