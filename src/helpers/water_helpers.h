#ifndef WATER_HELPERS_H
#define WATER_HELPERS_H

#include <Arduino.h>
#include <ESP8266WiFi.h>  // Add WiFi support
#include <NewPing.h>

#include "../config.h"
#include "../pins.h"

// Forward declarations
extern NewPing sonar;
extern LiquidCrystal_I2C lcd;
extern void lcdMessage(const char* line1, const char* line2, uint32_t waitTime,
                       bool clearScreen);
extern void nonBlockingWait(uint32_t waitTime, uint32_t startDisplayTime = 0);
extern void progressBar(float percentage);
extern void updateWaterLevelToServer(float waterHeight);
extern bool updateWaterStatus(const char* status, float waterLevel);
extern bool addLogEntry(const char* event, const char* details);
extern bool isWebConnected();
extern bool sendLogEvent(const char* eventType, const char* details);

// Add constants to config.h for water refill and cooldown
#ifndef REFILL_DURATION
#define REFILL_DURATION 10000  // 10 seconds for refill
#endif
#ifndef COOLDOWN_PERIOD
#define COOLDOWN_PERIOD 300000  // 5 minutes cooldown
#endif

/**
 * Non-blocking median ping measurement for ESP8266
 * @param sonar NewPing object for the ultrasonic sensor
 * @param iterations Number of measurements to take (should be odd for true
 * median)
 * @param maxDistance Maximum distance in cm
 * @param maxDuration Maximum timeout duration in milliseconds
 * @return Median distance in cm, or 0 if failed
 */
unsigned int nonBlockingMedianPing(NewPing& sonar, uint8_t iterations = 5,
                                   unsigned int maxDistance = 400,
                                   unsigned int maxDuration = 10000) {
  unsigned int samples[iterations];
  unsigned long startTime = millis();

  // Take multiple samples with yields between them
  for (uint8_t i = 0; i < iterations; i++) {
    // Check if we've exceeded maximum duration
    if (millis() - startTime > maxDuration) {
      return 0;  // Timeout occurred
    }

    // Get ping measurement
    samples[i] = sonar.ping_cm();

    // If ping returns 0 (no echo or out of range), set to max distance
    if (samples[i] == 0) {
      samples[i] = maxDistance;
    }

    // Yield to allow ESP8266 background tasks to run
    yield();

    // Add small delay between pings
    unsigned long delayTime = 30;  // 30ms between pings
    unsigned long delayStart = millis();
    while (millis() - delayStart < delayTime) {
      yield();
      delay(1);
    }
  }

  // Sort the array to find median (bubble sort for small arrays)
  for (uint8_t i = 0; i < iterations - 1; i++) {
    for (uint8_t j = 0; j < iterations - i - 1; j++) {
      if (samples[j] > samples[j + 1]) {
        // Swap
        unsigned int temp = samples[j];
        samples[j] = samples[j + 1];
        samples[j + 1] = temp;
        yield();  // Yield during sorting
      }
    }
  }

  // Return median value
  return samples[iterations / 2];
}

/**
 * Get distance from ultrasonic sensor with minimal memory usage
 * @return Average distance in centimeters, or 0 if all readings failed
 */
float getDistance() {
  uint32_t totalDistance = 0;  // Use 32-bit to accumulate distance
  uint8_t validReadings = 0;

  // Take readings with minimal memory usage
  for (uint8_t pingIndex = 0; pingIndex < PING_SAMPLES; pingIndex++) {
    yield();                     // Single yield before measurement
    uint32_t uS = sonar.ping();  // Get ping duration
    yield();                     // Yield after measurement

    // Process valid readings only (non-zero response)
    if (uS > 0) {
      totalDistance += sonar.convert_cm(uS);
      validReadings++;
    }

    delay(10);
  }

  // Return 0 if no valid readings
  if (validReadings == 0) {
    return 0;
  }

  // Calculate and return average
  return totalDistance / validReadings;
}

/**
 * Check water level and manage pump activation
 */
void checkWaterLevel() {
  // Static variables for state machine
  static enum WaterState {
    CHECK_WATER,     // Normal water level checking
    REFILL_RUNNING,  // Water pump active
    COOLDOWN         // Waiting after refill
  } state = CHECK_WATER;

  static uint32_t stateStartTime = 0;
  static uint32_t lastDisplayUpdate = 0;

  // Get current time once
  uint32_t currentMillis = millis();

  // State machine for water level management using switch
  switch (state) {
    case CHECK_WATER: {
      DEBUG_PRINTLN(F("Checking Water Level"));
      yield();

      // Get distance from water surface
      float distanceCm = getDistance();
      yield();

      // If readings failed completely, show error
      if (distanceCm <= 0 || distanceCm > 400) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print(F("Sensor Error"));
        lcd.setCursor(0, 1);
        lcd.print(F("Check ultrasonic"));
        nonBlockingWait(INFO_DISPLAY_TIME);
        return;
      }

      // Calculate water height and percentage
      float waterHeight = DISTANCE_WATER_EMPTY - distanceCm;
      waterHeight =
          constrain(waterHeight, 0, DISTANCE_WATER_EMPTY - DISTANCE_WATER_FULL);

      float waterPercentage =
          (waterHeight / (DISTANCE_WATER_EMPTY - DISTANCE_WATER_FULL)) * 100;
      waterPercentage = constrain(waterPercentage, 0, 100);

      // Update server with water level if connected
      if (WiFi.status() == WL_CONNECTED &&
          isWebConnected()) {  // Fixed: Using WiFi.status() == WL_CONNECTED
        const char* status =
            (waterHeight <= WATER_CRITICAL_HEIGHT) ? "low" : "ok";
        updateWaterStatus(status, waterPercentage);
      }

      // Call function to update server if connected
      updateWaterLevelToServer(waterHeight);

      // Debug info
      DEBUG_PRINT(F("Water height: "));
      DEBUG_PRINT(waterHeight);
      DEBUG_PRINT(F("cm ("));
      DEBUG_PRINT(waterPercentage);
      DEBUG_PRINT(F("%), Distance: "));
      DEBUG_PRINT(distanceCm);
      DEBUG_PRINTLN(F("cm"));

      // Display on LCD
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print(F("Water Level:"));

      // Check if water level is critically low
      if (waterHeight <= WATER_CRITICAL_HEIGHT) {
        ;
        DEBUG_PRINTLN(F("Water level critically low! Activating relay."));

        // Display low water information
        char waterInfo[16];
        snprintf(waterInfo, sizeof(waterInfo), "LOW! %.1fcm (%d%%)",
                 waterHeight, (int)waterPercentage);
        lcd.setCursor(0, 1);
        lcd.print(waterInfo);
        yield();

        // If water level is low, update server before activating pump
        if (waterHeight <= WATER_CRITICAL_HEIGHT &&
            WiFi.status() == WL_CONNECTED &&
            isWebConnected()) {  // Fixed: Using WiFi.status() == WL_CONNECTED
          updateWaterStatus("refilling", waterPercentage);
          addLogEntry("water_low", "Water level critically low, refilling");
        }

        // Activate relay - uncomment the next line when hardware is ready
        // digitalWrite(WATER_PUMP_RELAY_PIN, HIGH);

        // Show activation message
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print(F("Water low!"));
        ;
        lcd.setCursor(0, 1);
        lcd.print(F("Refilling..."));

        // Change state and record start time
        state = REFILL_RUNNING;
        stateStartTime = currentMillis;
        lastDisplayUpdate = currentMillis;
      } else {
        // Water level OK - display info and progress bar
        char waterInfo[16];
        snprintf(waterInfo, sizeof(waterInfo), "OK %.1fcm (%d%%)", waterHeight,
                 (int)waterPercentage);
        lcd.setCursor(0, 1);
        lcd.print(waterInfo);

        // Display water level bar graph
        progressBar(waterPercentage);
      }
      break;
    }

    case REFILL_RUNNING: {
      // Update display periodically
      if (currentMillis - lastDisplayUpdate >= 200) {
        lastDisplayUpdate = currentMillis;
        yield();

        // Show animation and countdown
        uint32_t elapsedSecs = (currentMillis - stateStartTime) / 1000;
        lcd.setCursor(11, 1);
        lcd.print(F("   "));  // Clear previous dots
        lcd.setCursor(11, 1);
        for (int i = 0; i < (elapsedSecs % 3) + 1; i++) {
          lcd.print(F("."));
        }

        // Countdown display
        int remainingSecs = max(
            0,
            (int)((REFILL_DURATION - (currentMillis - stateStartTime)) / 1000 +
                  1));
        lcd.setCursor(15, 1);
        lcd.print(remainingSecs);
      }

      // Check if refill duration has elapsed
      if (currentMillis - stateStartTime >= REFILL_DURATION) {
        yield();

        // Turn off relay
        digitalWrite(WATER_PUMP_RELAY_PIN, LOW);

        // When refill is complete, update server
        if (currentMillis - stateStartTime >= REFILL_DURATION &&
            isWebConnected()) {
          updateWaterStatus("cooldown", 100);
          addLogEntry("water_refilled",
                      "Water refill completed, entering cooldown");
        }

        // Show completion message
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print(F("Refill complete"));
        lcd.setCursor(0, 1);
        lcd.print(F("Cooldown: 5 min"));

        // Change state to cooldown
        state = COOLDOWN;
        stateStartTime = currentMillis;
        lastDisplayUpdate = currentMillis;

        DEBUG_PRINTLN(F("Water refill completed, entering 5-min cooldown"));
      }
      break;
    }

    case COOLDOWN: {
      // Update display periodically
      if (currentMillis - lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL) {
        lastDisplayUpdate = currentMillis;
        yield();

        // Show remaining cooldown time in MM:SS format
        uint32_t elapsedMs = currentMillis - stateStartTime;
        uint32_t remainingSecs = (COOLDOWN_PERIOD - elapsedMs) / 1000;

        lcd.setCursor(10, 1);
        lcd.print(F("     "));  // Clear previous time
        lcd.setCursor(10, 1);

        if (elapsedMs > COOLDOWN_PERIOD) {
          lcd.print(F("00:00"));
        } else {
          lcd.print(remainingSecs / 60);
          lcd.print(F(":"));
          if ((remainingSecs % 60) < 10) lcd.print(F("0"));
          lcd.print(remainingSecs % 60);
        }
      }

      // Check if cooldown period has elapsed
      if (currentMillis - stateStartTime >= COOLDOWN_PERIOD) {
        DEBUG_PRINTLN(F("Water level cooldown complete, resuming checks"));

        // When cooldown completes, update server
        if (isWebConnected()) {
          updateWaterStatus("ready", 100);
          addLogEntry("water_ready", "Water system ready after cooldown");
        }

        state = CHECK_WATER;
      }
      break;
    }
  }
}

#endif  // WATER_HELPERS_H