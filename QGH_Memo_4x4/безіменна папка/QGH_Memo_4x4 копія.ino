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
uint8_t buttonPins[12] = {A0,A1,7,4,9,10,11,3,5,8,6,12};
uint8_t buttonToSegment[12] = {0,1,2,3,4,5,6,7,8,9,10,11};

unsigned long lastPress[12];
bool lastState[12];
#define DEBOUNCE_MS 400

int readButton(){
  for(int i=0;i<12;i++){
    bool nowState = (digitalRead(buttonPins[i]) == LOW);
    unsigned long t = millis();

    if(nowState != lastState[i]){
      if(t - lastPress[i] > DEBOUNCE_MS){
        lastPress[i] = t;
        lastState[i] = nowState;
        if(nowState) return i;
      }
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
  {1,2,3,4,5,6,7,8,9,10,11,0},
  {2,3,4,5,6,7,8,9,10,11,0,1},
  {3,4,5,6,7,8,9,10,11,0,1,2},
  {4,5,6,7,8,9,10,11,0,1,2,3},
  {5,6,7,8,9,10,11,0,1,2,3,4},
  {6,7,8,9,10,11,0,1,2,3,4,5},
  {7,8,9,10,11,0,1,2,3,4,5,6},
  {8,9,10,11,0,1,2,3,4,5,6,7},
  {9,10,11,0,1,2,3,4,5,6,7,8},
  {10,11,0,1,2,3,4,5,6,7,8,9},
  {11,0,1,2,3,4,5,6,7,8,9,10}
};

uint8_t puzzleIndex = 0;
uint8_t stepIndex = 0;

// ==== blink one segment ====
void blinkSegment(int seg){
  lightSegment(seg, CRGB::Blue);
  delay(500);
  clearSegment(seg);
  delay(500);
}

// ==== restart current scenario ====
void restartPuzzle(){
  clearAll();
  stepIndex = 0;

  uint8_t first = pgm_read_byte(&patterns[puzzleIndex][0]);
  int seg = buttonToSegment[first];
  lightSegment(seg, CRGB::Blue);
}

// ==== correct press ====
void handleCorrect(int seg){
  // light all correct ones so far
  for(int i=0;i<=stepIndex;i++){
    int s = buttonToSegment[ pgm_read_byte(&patterns[puzzleIndex][i]) ];
    lightSegment(s, CRGB::Blue);
    delayMicroseconds(1500);
  }

  stepIndex++;

  if(stepIndex >= 12){
    // success: blink all, advance scenario
    for(int k=0;k<3;k++){
      fill_solid(leds0, GROUP_LEDS, CRGB::Blue);
      fill_solid(leds1, GROUP_LEDS, CRGB::Blue);
      fill_solid(leds2, GROUP_LEDS, CRGB::Blue);
      fill_solid(leds3, GROUP_LEDS, CRGB::Blue);
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
    lightSegment(seg, CRGB::Blue);
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
      if(visible) lightSegment(segFirst, CRGB::Blue);
      else clearSegment(segFirst);
    }
  }

  int b = readButton();
  if(b < 0) return;

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
