// Add reference start sketch 

/*
  MEMORY ORB — Joy edition
  ------------------------------------------------------------
  An Inside Out–inspired memory orb for a Joy costume.

  INTERACTION MODEL
    IDLE      Soft golden swirl (Joy's resting state), with faint
              hints of other emotions drifting through.
    TAP       A Joy memory surfaces (gold sparkles).
    TAP AGAIN Cycles to the next emotion's memory:
              Joy -> Sadness -> Anger -> Fear -> Disgust -> Joy ...
    (no tap)  After MEMORY_MS the memory fades back to idle.
    HOLD      Sadness "touches" the memory: gold slowly swirls
              with blue. Let go early and Joy wins it back.
              Hold all the way and it becomes a CORE MEMORY
              (gold + blue together), which stays until you tap.

  HARDWARE (Uno or Nano — same code)
    WS2812B / NeoPixel ring  DIN -> 470 ohm resistor -> pin 6
                             5V  -> 5V,  GND -> GND
                             1000 uF capacitor across 5V/GND near ring
    TTP223 touch sensor      SIG -> pin 2, VCC -> 5V, GND -> GND
    Power                    USB power bank into the Arduino's USB port

  LIBRARY
    Install "FastLED" from the Arduino Library Manager.
*/

#include <FastLED.h>

// Settings 
#define LED_PIN        6
#define TOUCH_PIN      2
#define NUM_LEDS       16      // change to match your ring/strip
#define BRIGHTNESS     120     // 0–255; keep moderate for battery + heat
#define MAX_MILLIAMPS  500     // FastLED caps power draw to this

const uint32_t HOLD_MS   = 1000;  // how long a press counts as "hold"
const uint32_t MEMORY_MS = 8000;  // how long a memory plays before fading
const uint32_t FRAME_MS  = 16;    // ~60 frames per second

// Emotions 
enum Scene { JOY, SADNESS, ANGER, FEAR, DISGUST, NUM_EMOTIONS, CORE_SCENE = NUM_EMOTIONS };

const CRGB emotionColor[NUM_EMOTIONS] = {
  CRGB(255, 170,  20),  // Joy      — warm gold
  CRGB( 30,  80, 255),  // Sadness  — deep blue
  CRGB(255,  20,   0),  // Anger    — red
  CRGB(150,  40, 255),  // Fear     — purple
  CRGB( 40, 220,  60)   // Disgust  — green
};

// State machine 
enum State { IDLE, MEMORY, FADING, BLENDING, CORE };

State    state      = IDLE;
uint8_t  scene      = JOY;
uint32_t stateStart = 0;
uint8_t  level      = 0;   // how strongly the memory shows over idle (0–255)
uint8_t  blendAmt   = 0;   // 0 = pure Joy, 255 = full Joy+Sadness core memory

// ---------- Touch tracking ----------
bool     touching      = false;
uint32_t touchStart    = 0;
bool     holdTriggered = false;

CRGB leds[NUM_LEDS];
CRGB memBuf[NUM_LEDS];

// =============================================================
void setup() {
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, MAX_MILLIAMPS);
  pinMode(TOUCH_PIN, INPUT);
  random16_add_entropy(analogRead(A0));
}

void loop() {
  uint32_t now = millis();
  handleTouch(now);

  static uint32_t lastFrame = 0;
  if (now - lastFrame < FRAME_MS) return;   // non-blocking frame timer
  lastFrame = now;

  updateState(now);
  render(now);
  FastLED.show();
}

// Input 
void handleTouch(uint32_t now) {
  bool t = digitalRead(TOUCH_PIN) == HIGH;

  if (t && !touching) {                       // press began
    touching = true;
    touchStart = now;
    holdTriggered = false;
  } else if (t && touching && !holdTriggered && now - touchStart > HOLD_MS) {
    holdTriggered = true;                     // became a hold
    onHoldStart(now);
  } else if (!t && touching) {                // released
    touching = false;
    if (!holdTriggered && now - touchStart > 30) onTap(now);  // 30 ms debounce
  }
}

void enter(State s, uint32_t now) {
  state = s;
  stateStart = now;
}

void onTap(uint32_t now) {
  switch (state) {
    case IDLE:
    case FADING:
      scene = JOY;                             // Joy always surfaces first
      enter(MEMORY, now);
      break;
    case MEMORY:
      scene = (scene + 1) % NUM_EMOTIONS;      // next emotion
      stateStart = now;                        // restart the play timer
      break;
    case CORE:
      enter(FADING, now);                      // release the core memory
      break;
    default:
      break;
  }
}

void onHoldStart(uint32_t now) {
  if (state == CORE) return;
  scene = CORE_SCENE;
  enter(BLENDING, now);
}

// State updates (once per frame) 
void updateState(uint32_t now) {
  switch (state) {
    case IDLE:
      level = qsub8(level, 4);
      break;

    case MEMORY:
      level = qadd8(level, 4);                 // surface
      if (now - stateStart > MEMORY_MS) enter(FADING, now);
      break;

    case FADING:
      level = qsub8(level, 3);                 // sink back into the fog
      if (level == 0) {
        state = IDLE;
        blendAmt = 0;
      }
      break;

    case BLENDING:
      level = qadd8(level, 4);
      if (touching) blendAmt = qadd8(blendAmt, 2);   // ~2 s to full
      else          blendAmt = qsub8(blendAmt, 4);   // Joy pulls it back

      if (blendAmt == 255) {
        enter(CORE, now);                      // a mixed core memory is born
      } else if (!touching && blendAmt == 0) {
        scene = JOY;
        enter(MEMORY, now);
      }
      break;

    case CORE:
      level = qadd8(level, 4);                 // stays until tapped
      break;
  }
}

// Rendering
void render(uint32_t t) {
  drawIdle(t);
  if (level == 0) return;

  switch (scene) {
    case JOY:        joyPattern(t);            break;
    case SADNESS:    sadnessPattern(t);        break;
    case ANGER:      angerPattern(t);          break;
    case FEAR:       fearPattern(t);           break;
    case DISGUST:    disgustPattern(t);        break;
    case CORE_SCENE: corePattern(t, blendAmt); break;
  }

  for (int i = 0; i < NUM_LEDS; i++) nblend(leds[i], memBuf[i], level);
}

// Idle: dim, slowly drifting gold with faint wisps of other emotions
void drawIdle(uint32_t t) {
  for (int i = 0; i < NUM_LEDS; i++) {
    CRGB c = emotionColor[JOY];
    uint8_t wisp = inoise8(i * 40 + 5000, t / 12);
    if (wisp > 190) c = blend(c, emotionColor[(i % (NUM_EMOTIONS - 1)) + 1], (wisp - 190) * 2);
    c.nscale8(map(inoise8(i * 40, t / 8), 0, 255, 20, 90));
    leds[i] = c;
  }
}

// Joy: bright gold with bouncy sparkles
void joyPattern(uint32_t t) {
  for (int i = 0; i < NUM_LEDS; i++) {
    memBuf[i] = emotionColor[JOY];
    memBuf[i].nscale8(150 + (sin8(t / 4 + i * 30) >> 3));
  }
  if (random8() < 60) memBuf[random8(NUM_LEDS)] = CRGB(255, 240, 180);
}

// Sadness: slow, heavy wave
void sadnessPattern(uint32_t t) {
  for (int i = 0; i < NUM_LEDS; i++) {
    memBuf[i] = emotionColor[SADNESS];
    memBuf[i].nscale8(sin8(t / 20 + i * 8) / 2 + 60);
  }
}

// Anger: simmers, builds, then flares
void angerPattern(uint32_t t) {
  uint16_t phase = t % 3000;
  bool flare = phase >= 2500;
  uint8_t base = flare ? 255 : map(phase, 0, 2500, 60, 200);
  for (int i = 0; i < NUM_LEDS; i++) {
    if (flare) {
      memBuf[i] = blend(emotionColor[ANGER], CRGB(255, 140, 0), 128);
    } else {
      memBuf[i] = emotionColor[ANGER];
      memBuf[i].nscale8(qsub8(base, random8(40)));
    }
  }
}

// Fear: jittery flutter with sudden flinches
void fearPattern(uint32_t t) {
  bool flinch = (t / 150) % 13 == 0;
  for (int i = 0; i < NUM_LEDS; i++) {
    memBuf[i] = emotionColor[FEAR];
    memBuf[i].nscale8(flinch ? 20 : random8(80, 200));
  }
}

// Disgust: cool ripple with an occasional "nope" dim
void disgustPattern(uint32_t t) {
  bool nope = (t / 400) % 10 == 0;
  for (int i = 0; i < NUM_LEDS; i++) {
    memBuf[i] = emotionColor[DISGUST];
    memBuf[i].nscale8(nope ? 30 : sin8(t / 10 + i * 16) / 2 + 80);
  }
}

// Core memory: gold and blue swirling together.
// amt 0 = all gold; amt 255 = roughly half gold, half blue, always moving.
void corePattern(uint32_t t, uint8_t amt) {
  for (int i = 0; i < NUM_LEDS; i++) {
    uint8_t mix = scale8(inoise8(i * 50, t / 6), amt);
    memBuf[i] = blend(emotionColor[JOY], emotionColor[SADNESS], mix);
    memBuf[i].nscale8(200);
  }
}
