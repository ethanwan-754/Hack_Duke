#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ─────────────────────────────────────────────
// CONFIGURATION
// ─────────────────────────────────────────────

const int IR_SENSOR_PIN = A0;

const float SPO2_MIN        = 70.0;
const float SPO2_AT_AVERAGE = 98.0;
const float SPO2_MAX        = 100.0;

const unsigned long DISPLAY_INTERVAL_MS  = 1000;  // 1 Hz
const unsigned long SERIAL_INTERVAL_MS   = 100;   // 10 Hz
const unsigned long CALIBRATION_WAIT_MS  = 7000;  // Wait 2s before calibrating
const unsigned long CALIBRATION_DURATION = 1000;  // Sample for 1s to get average

// ─────────────────────────────────────────────
// HARDWARE SETUP
// ─────────────────────────────────────────────

LiquidCrystal_I2C lcd(0x27, 16, 2);

// ─────────────────────────────────────────────
// CALIBRATION STATE
// ─────────────────────────────────────────────

enum State {
    WAITING,      // Waiting 2 seconds before calibration
    CALIBRATING,  // Sampling for 1 second to find average
    RUNNING       // Normal operation
};

State currentState      = WAITING;
float baselineAverage   = 0;
float baselineNoFinger  = 0;

long  calibrationSum    = 0;
int   calibrationCount  = 0;

unsigned long stateStartTime = 0;

// ─────────────────────────────────────────────
// SPO2 ESTIMATION
// ─────────────────────────────────────────────

float estimateSpO2(int rawValue) {
    if (rawValue <= baselineNoFinger) {
        return -1.0;  // No finger detected
    }

    float sensorRange = baselineAverage - baselineNoFinger;
    float spo2Range   = SPO2_AT_AVERAGE - SPO2_MIN;

    float spo2 = SPO2_MIN + ((rawValue - baselineNoFinger) / sensorRange) * spo2Range;

    if (spo2 < SPO2_MIN) spo2 = SPO2_MIN;
    if (spo2 > SPO2_MAX) spo2 = SPO2_MAX;

    return spo2;
}

// ─────────────────────────────────────────────
// DISPLAY
// ─────────────────────────────────────────────

void updateDisplay(int rawValue, float spo2) {
    lcd.clear();

    if (currentState == WAITING) {
        lcd.setCursor(0, 0);
        lcd.print("  Place Finger  ");
        lcd.setCursor(0, 1);
        lcd.print(" Hold Still...  ");
        return;
    }

    if (currentState == CALIBRATING) {
        lcd.setCursor(0, 0);
        lcd.print("  Calibrating   ");
        lcd.setCursor(0, 1);
        lcd.print("  Please Wait   ");
        return;
    }

    // RUNNING state
    if (spo2 < 0) {
        lcd.setCursor(0, 0);
        lcd.print("  Place Finger  ");
        lcd.setCursor(0, 1);
        lcd.print("  on Sensor...  ");
    } else {
        lcd.setCursor(0, 0);
        lcd.print("SpO2: ");
        lcd.print(spo2, 1);
        lcd.print("%  ");

        lcd.setCursor(0, 1);
        lcd.print("Status: ");
        if (spo2 >= 95.0) {
            lcd.print("Normal  ");
        } else if (spo2 >= 90.0) {
            lcd.print("Low     ");
        } else {
            lcd.print("Critical");
        }
    }
}

// ─────────────────────────────────────────────
// SERIAL OUTPUT
// ─────────────────────────────────────────────

void sendSerial(int rawValue, float spo2) {
    Serial.print(millis());
    Serial.print(",");
    Serial.print(rawValue);
    Serial.print(",");
    Serial.print(baselineAverage);
    Serial.print(",");
    if (currentState != RUNNING) {
        Serial.println("CAL");  // Let receiving script know we're calibrating
    } else if (spo2 < 0) {
        Serial.println("---");
    } else {
        Serial.println(spo2, 1);
    }
    
}

// ─────────────────────────────────────────────
// SETUP
// ─────────────────────────────────────────────

void setup() {
    Serial.begin(115200);

    lcd.init();
    lcd.backlight();

    lcd.setCursor(0, 0);
    lcd.print("  Pseudo-Oxy    ");
    lcd.setCursor(0, 1);
    lcd.print("  Starting...   ");
    delay(1000);
    lcd.clear();

    stateStartTime = millis();  // Begin WAITING phase
}

// ─────────────────────────────────────────────
// MAIN LOOP
// ─────────────────────────────────────────────

unsigned long lastDisplayTime = 0;
unsigned long lastSerialTime  = 0;

void loop() {
    unsigned long now = millis();

    int   rawValue = analogRead(IR_SENSOR_PIN);
    float spo2     = -1.0;

    // ── State Machine ──
    if (currentState == WAITING) {
        if (now - stateStartTime >= CALIBRATION_WAIT_MS) {
            // Move to calibration phase
            currentState     = CALIBRATING;
            stateStartTime   = now;
            calibrationSum   = 0;
            calibrationCount = 0;
        }

    } else if (currentState == CALIBRATING) {
        // Accumulate samples for 1 second
        calibrationSum += rawValue;
        calibrationCount++;

        if (now - stateStartTime >= CALIBRATION_DURATION) {
            // Compute calibrated baselines
            baselineAverage  = (float)calibrationSum / calibrationCount;
            baselineNoFinger = baselineAverage - 20.0;  // 20 points below average

            currentState = RUNNING;

            Serial.print("CAL_DONE,avg=");
            Serial.print(baselineAverage, 1);
            Serial.print(",no_finger=");
            Serial.println(baselineNoFinger, 1);
        }

    } else if (currentState == RUNNING) {
        spo2 = estimateSpO2(rawValue);
    }

    // ── Serial: 10x per second ──
    if (now - lastSerialTime >= SERIAL_INTERVAL_MS) {
        sendSerial(rawValue, spo2);
        lastSerialTime = now;
    }

    // ── Display: once per second ──
    if (now - lastDisplayTime >= DISPLAY_INTERVAL_MS) {
        updateDisplay(rawValue, spo2);
        lastDisplayTime = now;
    }
}