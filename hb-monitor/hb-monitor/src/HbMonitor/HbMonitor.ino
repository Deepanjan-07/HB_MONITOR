/*
 * ============================================================
 *  Non-Invasive Hemoglobin Estimator — v7
 * ============================================================
 *  Hardware : MAX30102 Pulse Oximeter + Proximity Sensor
 *             1.3" OLED Display (SH1106 / SSD1306, 128x64)
 *             SPDT Toggle Switch (Male / Female selection)
 *             Arduino Uno / Nano
 *
 *  Method   : AC/DC ratio regression
 *             R = (AC_red / DC_red) / (AC_ir / DC_ir)
 *             Hb = -16.15*R² + 35.02*R - 0.63
 *
 *  Regression source:
 *             Polynomial fit on Abuzairi et al. 2023
 *             Mendeley Dataset — MAX30102, 68 subjects
 *             Recalibrated for observed R range 0.30–0.90
 *
 *  Wiring:
 *             MAX30102  →  Arduino
 *             VIN           3.3V
 *             GND           GND
 *             SDA           A4
 *             SCL           A5
 *
 *             OLED      →  Arduino
 *             VCC           3.3V
 *             GND           GND
 *             SDA           A4
 *             SCL           A5
 *
 *             SPDT Switch → Arduino
 *             COM (middle)  GND
 *             Left pin      D2  (Female)
 *             Right pin     D3  (Male)
 *
 *  Libraries (install via Arduino Library Manager):
 *             1. SparkFun MAX3010x Pulse and Proximity Sensor
 *             2. U8g2 by oliver
 *
 *  Author   : github.com/[your-username]
 *  License  : MIT
 * ============================================================
 */

#include <Wire.h>
#include "MAX30105.h"
#include <U8g2lib.h>

// ── OLED constructor ──
// SH1106 128x64 (most 1.3" OLEDs) — page mode saves 1024 bytes RAM:
U8G2_SH1106_128X64_NONAME_1_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);
// SSD1306 alternative (uncomment if display stays blank):
// U8G2_SSD1306_128X64_NONAME_1_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);

MAX30105 sensor;

// ── SPDT switch pins ──
const uint8_t PIN_FEMALE = 2;  // switch left  → D2 LOW = Female
const uint8_t PIN_MALE   = 3;  // switch right → D3 LOW = Male

// ── Regression coefficients ──
// Hb = A*R² + B*R + C
// Valid R range: 0.25 – 0.95
const float A = -16.15;
const float B =  35.02;
const float C =  -0.63;

// ── Configuration ──
const uint8_t SAMPLES          = 50;     // samples per reading cycle
const long    FINGER_THRESHOLD = 50000;  // IR mean threshold for finger detection
const uint8_t STABLE_NEEDED    = 6;      // consecutive stable reads before display
const float   STABLE_THRESHOLD = 1.5;   // max g/dL jump allowed between readings
const uint8_t AVG_SIZE         = 6;      // R smoothing window size
const uint8_t SETTLE_COUNT     = 150;    // samples to discard on new finger placement

// ── Sample buffers ──
uint32_t redBuf[SAMPLES];
uint32_t irBuf[SAMPLES];
char     sbuf[12];  // shared string buffer for dtostrf

// ── R smoothing ring buffer ──
float    rHistory[AVG_SIZE];
uint8_t  rIdx        = 0;
bool     rFull       = false;

// ── Measurement state ──
float    lastHb      = 0.0;
uint8_t  stableCount = 0;
bool     settled     = false;
uint8_t  rejectCount = 0;
bool     lastMale    = false;

// ─────────────────────────────────────────────────────────────
//  readGender()
//  Reads SPDT switch position directly — no debounce needed
//  Returns true = Male, false = Female
// ─────────────────────────────────────────────────────────────
bool readGender() {
  if (digitalRead(PIN_MALE)   == LOW) return true;
  if (digitalRead(PIN_FEMALE) == LOW) return false;
  return false;  // default Female if switch in undefined state
}

// ─────────────────────────────────────────────────────────────
//  Signal processing helpers
// ─────────────────────────────────────────────────────────────
long arrayMean(uint32_t* buf, uint8_t n) {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < n; i++) sum += buf[i];
  return (long)(sum / n);
}

long arrayPP(uint32_t* buf, uint8_t n) {  // peak-to-peak = AC amplitude
  uint32_t mn = buf[0], mx = buf[0];
  for (uint8_t i = 1; i < n; i++) {
    if (buf[i] < mn) mn = buf[i];
    if (buf[i] > mx) mx = buf[i];
  }
  return (long)(mx - mn);
}

float pushR(float newR) {  // push R into ring buffer, return smoothed average
  rHistory[rIdx] = newR;
  rIdx = (rIdx + 1) % AVG_SIZE;
  if (rIdx == 0) rFull = true;
  uint8_t n = rFull ? AVG_SIZE : rIdx;
  float s = 0;
  for (uint8_t i = 0; i < n; i++) s += rHistory[i];
  return s / n;
}

void resetAll() {
  stableCount = 0;
  lastHb      = 0.0;
  rFull       = false;
  rIdx        = 0;
  rejectCount = 0;
  settled     = false;
  for (uint8_t i = 0; i < AVG_SIZE; i++) rHistory[i] = 0.0;
}

// ─────────────────────────────────────────────────────────────
//  classifyHb()
//  Returns WHO anemia classification string based on gender
// ─────────────────────────────────────────────────────────────
const char* classifyHb(float hb, bool male) {
  if (male) {
    if      (hb < 8.0)  return "Severe anemia";
    else if (hb < 11.0) return "Moderate";
    else if (hb < 13.0) return "Mild anemia";
    else if (hb < 13.5) return "Below normal";
    else if (hb < 17.5) return "Normal (M)";
    else                return "High";
  } else {
    if      (hb < 8.0)  return "Severe anemia";
    else if (hb < 11.0) return "Moderate";
    else if (hb < 12.0) return "Mild anemia";
    else if (hb < 12.5) return "Below normal";
    else if (hb < 15.5) return "Normal (F)";
    else                return "High";
  }
}

// ─────────────────────────────────────────────────────────────
//  OLED display functions
// ─────────────────────────────────────────────────────────────
void oledMsg(const char* l1, const char* l2) {
  oled.firstPage();
  do {
    oled.setFont(u8g2_font_ncenB10_tr);
    oled.drawStr(2, 22, l1);
    oled.setFont(u8g2_font_ncenB08_tr);
    oled.drawStr(2, 44, l2);
  } while (oled.nextPage());
}

void oledResult(float hb, float pi, bool male) {
  dtostrf(hb, 4, 1, sbuf);
  const char* label = classifyHb(hb, male);
  oled.firstPage();
  do {
    oled.setFont(u8g2_font_ncenB08_tr);
    oled.drawStr(0, 10, "Hemoglobin");
    oled.drawStr(90, 10, male ? "[M]" : "[F]");
    oled.drawHLine(0, 13, 128);
    oled.setFont(u8g2_font_ncenB18_tr);
    oled.drawStr(0, 40, sbuf);
    oled.setFont(u8g2_font_ncenB08_tr);
    oled.drawStr(70, 38, "g/dL");
    oled.drawStr(0, 54, label);
    oled.drawHLine(0, 57, 128);
    uint8_t bw = (uint8_t)constrain((int)(pi * 10.0), 0, 127);
    oled.drawBox(0, 58, bw, 6);
  } while (oled.nextPage());
}

void oledProgress(uint8_t current, uint8_t total, bool male) {
  char prog[16];
  sprintf(prog, "Pass %d of %d", current, total);
  oled.firstPage();
  do {
    oled.setFont(u8g2_font_ncenB10_tr);
    oled.drawStr(2, 16, "Measuring...");
    oled.setFont(u8g2_font_ncenB08_tr);
    oled.drawStr(2, 30, male ? "Mode: Male" : "Mode: Female");
    oled.drawStr(2, 44, prog);
    oled.drawFrame(2, 50, 124, 10);
    uint8_t fill = (uint8_t)((float)current / total * 122);
    oled.drawBox(3, 51, fill, 8);
  } while (oled.nextPage());
}

// ─────────────────────────────────────────────────────────────
//  setup()
// ─────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(9600);
  Serial.println(F("=== Hb Monitor v7 ==="));

  pinMode(PIN_FEMALE, INPUT_PULLUP);
  pinMode(PIN_MALE,   INPUT_PULLUP);

  oled.begin();
  oledMsg("Hb Monitor v7", "Starting...");
  delay(800);

  if (!sensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println(F("ERR: MAX30102 not found"));
    oledMsg("Sensor ERROR", "Check wiring");
    while (1);
  }

  // ledBrightness=60, sampleAvg=4, ledMode=2(Red+IR),
  // sampleRate=100, pulseWidth=411, adcRange=4096
  sensor.setup(60, 4, 2, 100, 411, 4096);
  sensor.setPulseAmplitudeRed(0x3C);
  sensor.setPulseAmplitudeIR(0x3C);
  sensor.setPulseAmplitudeGreen(0);

  resetAll();

  bool male = readGender();
  lastMale  = male;
  Serial.print(F("Switch: "));
  Serial.println(male ? F("MALE") : F("FEMALE"));
  oledMsg(male ? "Mode: MALE" : "Mode: FEMALE",
          male ? "Normal 13.5-17.5" : "Normal 12.0-15.5");
  delay(1500);
  oledMsg("Place finger", "Cover sensor");
}

// ─────────────────────────────────────────────────────────────
//  loop()
// ─────────────────────────────────────────────────────────────
void loop() {

  bool isMale = readGender();

  // ── Detect switch flip ──
  if (isMale != lastMale) {
    lastMale = isMale;
    resetAll();
    Serial.print(F("Gender changed: "));
    Serial.println(isMale ? F("MALE") : F("FEMALE"));
    oledMsg(isMale ? "Mode: MALE" : "Mode: FEMALE",
            isMale ? "Normal 13.5-17.5" : "Normal 12.0-15.5");
    delay(1000);
    return;
  }

  // ── 1. Collect samples ──
  for (uint8_t i = 0; i < SAMPLES; i++) {
    while (!sensor.available()) sensor.check();
    redBuf[i] = sensor.getRed();
    irBuf[i]  = sensor.getIR();
    sensor.nextSample();
  }

  long ir_dc  = arrayMean(irBuf,  SAMPLES);
  long red_dc = arrayMean(redBuf, SAMPLES);

  // ── 2. Finger presence ──
  if (ir_dc < FINGER_THRESHOLD) {
    if (settled) {
      Serial.println(F("Finger removed."));
      resetAll();
    }
    oledMsg(isMale ? "Place finger [M]" : "Place finger [F]",
            "Cover sensor");
    delay(300);
    return;
  }

  // ── 3. Settling — discard noisy startup samples ──
  if (!settled) {
    oledMsg("Settling...", "Hold still...");
    Serial.println(F("Settling..."));
    uint8_t drained = 0;
    while (drained < SETTLE_COUNT) {
      sensor.check();
      while (sensor.available() && drained < SETTLE_COUNT) {
        sensor.nextSample();
        drained++;
      }
    }
    settled = true;
    Serial.println(F("Settled. Measuring."));
    return;
  }

  // ── 4. AC amplitudes ──
  long red_ac = arrayPP(redBuf, SAMPLES);
  long ir_ac  = arrayPP(irBuf,  SAMPLES);

  // ── 5. Perfusion Index — signal quality check ──
  float pi = ((float)ir_ac / (float)ir_dc) * 100.0;

  if (pi < 0.4 || pi > 10.0) {
    rejectCount++;
    Serial.print(F("Bad PI="));
    Serial.print(pi, 1);
    Serial.println(F("%"));
    if (rejectCount >= 8) {
      oledMsg("Cover sensor!", "Block all light");
      rejectCount = 0;
    }
    delay(300);
    return;
  }
  rejectCount = 0;

  // ── 6. Weak pulse check ──
  if (red_ac < 200 || ir_ac < 200) {
    Serial.println(F("Weak AC — press harder"));
    oledMsg("Press harder", "Weak pulse");
    delay(800);
    return;
  }

  // ── 7. R ratio ──
  float R_raw = ((float)red_ac / (float)red_dc) /
                ((float)ir_ac  / (float)ir_dc);

  if (R_raw < 0.25 || R_raw > 0.95) {
    Serial.print(F("R out of range: "));
    Serial.println(R_raw, 4);
    oledMsg("Hold still", "Bad reading");
    delay(300);
    return;
  }

  // ── 8. Smooth R ──
  float R = pushR(R_raw);

  // ── 9. Regression: Hb = -16.15*R² + 35.02*R - 0.63 ──
  float hb = A * R * R + B * R + C;
  hb = constrain(hb, 7.0, isMale ? 17.5 : 15.5);

  // ── 10. Stability filter ──
  if (lastHb > 0.0 && fabs(hb - lastHb) > STABLE_THRESHOLD) {
    stableCount = 0;
  } else if (stableCount < 255) {
    stableCount++;
  }
  lastHb = hb;

  // ── 11. Serial log (use for calibration data collection) ──
  Serial.print(isMale ? F("[M] ") : F("[F] "));
  Serial.print(F("R_raw=")); Serial.print(R_raw, 4);
  Serial.print(F(" R_avg=")); Serial.print(R, 4);
  Serial.print(F("  Hb=")); Serial.print(hb, 1);
  Serial.print(F(" g/dL  PI=")); Serial.print(pi, 2);
  Serial.print(F("%  redAC=")); Serial.print(red_ac);
  Serial.print(F("  irAC=")); Serial.print(ir_ac);
  Serial.print(F("  stable=")); Serial.println(stableCount);

  // ── 12. Display ──
  if (stableCount < STABLE_NEEDED) {
    oledProgress(stableCount + 1, STABLE_NEEDED, isMale);
  } else {
    oledResult(hb, pi, isMale);
  }

  delay(500);
}
