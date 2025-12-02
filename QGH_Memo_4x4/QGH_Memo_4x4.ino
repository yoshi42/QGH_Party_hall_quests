#define FASTLED_NO_FRAMEBUFFER
#include <FastLED.h>

// ================= LED CONFIG =================
#define SEGMENT_LEDS 38
#define SEGMENT_COUNT 12
#define GROUP_LEDS (SEGMENT_LEDS * 3)

#define PIN_G0 2
#define PIN_G1 A2
#define PIN_G2 A3
#define PIN_G3 A4

CRGB leds0[GROUP_LEDS];
CRGB leds1[GROUP_LEDS];
CRGB leds2[GROUP_LEDS];
CRGB leds3[GROUP_LEDS];

CRGB scenarioColors[12] = {
  CRGB::Blue, CRGB::Green, CRGB::Red, CRGB::Purple,
  CRGB::Yellow, CRGB::White, CRGB(0,200,200),
  CRGB(200,0,200), CRGB(255,128,0), CRGB(0,150,255),
  CRGB(255,0,100), CRGB(0,255,150)
};
CRGB currentColor = CRGB::Blue;

CRGB* getGroup(int seg, int &localIndex){
  if(seg < 3){ localIndex = seg * SEGMENT_LEDS; return leds0; }
  else if(seg < 6){ localIndex = (seg-3)*SEGMENT_LEDS; return leds1; }
  else if(seg < 9){ localIndex = (seg-6)*SEGMENT_LEDS; return leds2; }
  else { localIndex = (seg-9)*SEGMENT_LEDS; return leds3; }
}

void lightSegment(int seg, CRGB c){
  int idx;
  CRGB* arr = getGroup(seg, idx);
  for(int i=0;i<SEGMENT_LEDS;i++) arr[idx+i] = c;
  FastLED.show();
  delayMicroseconds(1500);
}

void clearSegment(int seg){ lightSegment(seg, CRGB::Black); }

void clearAll(){
  fill_solid(leds0, GROUP_LEDS, CRGB::Black);
  fill_solid(leds1, GROUP_LEDS, CRGB::Black);
  fill_solid(leds2, GROUP_LEDS, CRGB::Black);
  fill_solid(leds3, GROUP_LEDS, CRGB::Black);
  FastLED.show();
  delayMicroseconds(1500);
}

// ================= BUTTON CONFIG =================
#define BUTTON_COUNT 12
uint8_t buttonPins[12] = {A0,A1,7,4,9,10,11,3,5,8,A5,12};
uint8_t buttonToSegment[12] = {0,1,2,3,4,5,6,7,8,9,10,11};

unsigned long lastPress[12];
bool lastState[12];
#define DEBOUNCE_MS 400

int readButton(){
  // Robust multi-sample read + debug output to diagnose phantom triggers
  const uint8_t SAMPLES = 3;          // number of samples per check
  const uint16_t SAMPLE_DELAY_MS = 3; // ms between samples

  for(int i=0;i<12;i++){
    uint8_t lowCount = 0;
    for(uint8_t s=0; s<SAMPLES; s++){
      if(digitalRead(buttonPins[i]) == LOW) lowCount++;
      delay(SAMPLE_DELAY_MS);
    }
    if(lowCount >= 2){ // accept if 2/3 samples are LOW
      return i;
    }
  }
  return -1;
}

// ================= SIMPLE TEST =================
void testButtonsToggle(){
  int b = readButton();
  if(b < 0) return;
  int seg = buttonToSegment[b];
  static bool on[12] = {0};
  on[seg] = !on[seg];
  if(on[seg]) lightSegment(seg, CRGB::Blue);
  else clearSegment(seg);
  delay(30);
}

void testLedsCycle(){
  for(int s=0;s<12;s++){
    lightSegment(s, CRGB::Blue);
    delay(200);
    clearSegment(s);
    delay(60);
  }
}

/* ================= SIMPLE PUZZLE (BLUE ONLY) ================= */

// 12 scenarios × 12 steps each (temporary simple pattern 0..11)
const uint8_t patterns[12][12] PROGMEM = {
  {0,1,2,3,4,5,6,7,8,9,10,11},
  {1,10,0,11,9,6,2,3,5,4,7,8},
  {2,3,5,4,7,8,6,10,1,0,11,9},
  {3,2,10,1,0,11,9,6,8,5,7,4},
  {4,7,8,5,3,2,1,0,11,10,6,9},
  {5,7,4,3,2,1,0,10,11,9,6,8},
  {6,10,9,11,0,1,2,3,5,4,7,8},
  {7,4,5,3,2,1,10,0,11,9,6,8},
  {8,6,10,9,11,0,1,2,5,3,4,7},
  {9,6,10,11,0,1,2,5,8,7,4,3},
  {10,11,9,6,8,5,7,4,3,2,1,0},
  {11,0,10,1,2,5,3,4,7,8,6,9}
};

uint8_t puzzleIndex = 0;
uint8_t stepIndex = 0;

// ==== blink one segment ====
void blinkSegment(int seg){
  lightSegment(seg, currentColor);
  delay(500);
  clearSegment(seg);
  delay(500);
}

// ==== restart current scenario ====
void restartPuzzle(){
  clearAll();
  stepIndex = 0;

  currentColor = scenarioColors[puzzleIndex];

  uint8_t first = pgm_read_byte(&patterns[puzzleIndex][0]);
  int seg = buttonToSegment[first];
  lightSegment(seg, currentColor);
}

// ==== correct press ====
void handleCorrect(int seg){
  // light all correct ones so far
  for(int i=0;i<=stepIndex;i++){
    int s = buttonToSegment[ pgm_read_byte(&patterns[puzzleIndex][i]) ];
    lightSegment(s, currentColor);
    delayMicroseconds(1500);
  }

  stepIndex++;

  if(stepIndex >= 12){
    // success: blink all, advance scenario
    for(int k=0;k<3;k++){
      fill_solid(leds0, GROUP_LEDS, currentColor);
      fill_solid(leds1, GROUP_LEDS, currentColor);
      fill_solid(leds2, GROUP_LEDS, currentColor);
      fill_solid(leds3, GROUP_LEDS, currentColor);
      FastLED.show();
      delay(250);
      clearAll();
      delay(200);
    }
    // next scenario
    puzzleIndex = (puzzleIndex + 1) % 12;
    restartPuzzle();
  }
}

// ==== wrong press ====
void handleWrong(int seg){
  for(int k=0;k<3;k++){
    lightSegment(seg, currentColor);
    delay(250);
    clearSegment(seg);
    delay(250);
  }
  restartPuzzle();
}

// ==== button processing ====
void processPuzzle(){
  static unsigned long lastBlink = 0;
  uint8_t first = pgm_read_byte(&patterns[puzzleIndex][0]);
  int segFirst = buttonToSegment[first];
  if(stepIndex == 0){
    unsigned long t = millis();
    if(t - lastBlink >= 500){
      lastBlink = t;
      static bool visible = false;
      visible = !visible;
      if(visible) lightSegment(segFirst, currentColor);
      else clearSegment(segFirst);
    }
  }

  int b = readButton();
  if(b < 0) return;

  // ignore already-correct earlier presses
  for(int i=0;i<stepIndex;i++){
    if(b == pgm_read_byte(&patterns[puzzleIndex][i])) return;
  }

  uint8_t expected = pgm_read_byte(&patterns[puzzleIndex][stepIndex]);

  if(b == expected){
    handleCorrect(buttonToSegment[b]);
  } else {
    handleWrong(buttonToSegment[b]);
  }
}

// ================= SETUP =================
void setup(){
  Serial.begin(115200);
  delay(200);

  FastLED.addLeds<NEOPIXEL, PIN_G0>(leds0, GROUP_LEDS);
  FastLED.addLeds<NEOPIXEL, PIN_G1>(leds1, GROUP_LEDS);
  FastLED.addLeds<NEOPIXEL, PIN_G2>(leds2, GROUP_LEDS);
  FastLED.addLeds<NEOPIXEL, PIN_G3>(leds3, GROUP_LEDS);
  FastLED.clear(true);

  for(int i=0;i<12;i++){
    pinMode(buttonPins[i], INPUT_PULLUP);
    lastPress[i] = 0;
    lastState[i] = false;
  }

  restartPuzzle();
}

// ================= LOOP =================
void loop(){
  // ---- puzzle mode ----
  processPuzzle();

  delay(5);
}
