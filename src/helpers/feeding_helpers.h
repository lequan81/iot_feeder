#ifndef FEEDING_HELPERS_H
#define FEEDING_HELPERS_H

#include <Arduino.h>
#include <ArduinoJson.h>  // Add JSON support
#include <HX711.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>

#include "../config.h"
#include "../pins.h"

// Forward declarations for external functions and objects
extern LiquidCrystal_I2C lcd;
extern Servo hatchServo;
extern HX711 scale;
extern StaticJsonDocument<512> jsonDoc;  // Reference the document from main.cpp
extern void nonBlockingWait(uint32_t waitTime, uint32_t startDisplayTime);
extern void progressBar(float percentage);
extern float getStableWeight(int numReadings, int samplesPerReading,
                             float stabilityThreshold);
extern bool isWebConnected();
extern bool sendLogEvent(const char* eventType, const char* details);
extern void updateFeedingToServer(float dispensedWeight, bool isScheduled);
extern void lcdMessage(const char* line1, const char* line2, uint32_t waitTime,
                       bool clearScreen);
extern bool sendMessage(const char* eventType, JsonVariant data);
extern bool sendFeedingComplete(bool isScheduled, const char* details,
                                float foodLevel, float waterLevel);

/**
 * Check if scale is ready and responsive
 * @param timeout Time to wait for scale response in milliseconds
 * @return True if scale is ready, false otherwise
 */
bool checkScaleReady(uint16_t timeout = SCALE_TIMEOUT) {
  uint32_t startCheck = millis();
  while (millis() - startCheck < timeout) {
    if (scale.is_ready()) {
      return true;
    }
    delay(50);
    yield();
  }
  return false;
}

/**
 * Display feeding progress and information
 * @param dispensedWeight Current dispensed weight in grams
 * @param targetWeight Target weight in grams
 * @param showProgressBar Whether to show a progress bar for >80% progress
 */
void updateFeedingDisplay(float dispensedWeight, float targetWeight,
                          bool showProgressBar = true) {
  // Calculate progress percentage
  float progressPercent = (dispensedWeight / targetWeight) * 100.0;
  progressPercent = constrain(progressPercent, 0, 100);

  // Format the progress text
  char progressText[16];
  snprintf(progressText, sizeof(progressText), "Feeding: %d%%",
           (int)progressPercent);

  // Show appropriate display based on progress
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(progressText);

  if (!showProgressBar || progressPercent < 80) {
    // Show target weight when not close to completion
    char targetText[16];
    snprintf(targetText, sizeof(targetText), "Target: %.0fg", targetWeight);
    lcd.setCursor(0, 1);
    lcd.print(targetText);
  } else {
    // Draw a progress bar when we're close to completion
    int barWidth = (progressPercent * LCD_X) / 100;
    lcd.setCursor(0, 1);
    for (int i = 0; i < LCD_X; i++) {
      lcd.write(i < barWidth ? (byte)255 : ' ');  // Full block or space
    }
  }
}

/**
 * Wait for food to settle and take multiple weight readings
 * @param numReadings Number of readings to take
 * @param samplesPerReading Samples per reading for better accuracy
 * @return Average weight after settling
 */
float measureSettledWeight(int numReadings = 5, int samplesPerReading = 2) {
  float settledWeight = 0;
  int validReadings = 0;

  // Take multiple readings
  for (int i = 0; i < numReadings; i++) {
    if (scale.is_ready()) {
      settledWeight += scale.get_units(samplesPerReading);
      validReadings++;

      // Show progress indicator
      lcd.setCursor(15, 1);
      lcd.print(i + 1);
      yield();
      delay(100);
    }
  }

  return (validReadings > 0) ? (settledWeight / validReadings) : 0;
}

/**
 * Check if bowl has enough food already
 * @param currentWeight Current weight in bowl (grams)
 * @param threshold Minimum threshold to consider "enough food"
 * @return True if user wants to continue feeding, false to cancel
 */
bool checkExistingFood(float currentWeight, float threshold) {
  if (currentWeight >= threshold) {
    // Food already present! Show notification
    char weightText[16];
    snprintf(weightText, sizeof(weightText), "Weight: %.1fg", currentWeight);

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("Food detected!"));
    lcd.setCursor(0, 1);
    lcd.print(weightText);
    nonBlockingWait(INFO_DISPLAY_TIME);

    // Show options
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("Food already >10g"));
    lcd.setCursor(0, 1);
    lcd.print(F("Btn:feed / Wait:20s"));

    // Wait for button press or timeout
    uint32_t notifyStartTime = millis();
    const uint32_t NOTIFY_TIMEOUT = 20000;  // 20 second timeout
    bool buttonPressed = false;

    // Loop until button press or timeout
    while (millis() - notifyStartTime < NOTIFY_TIMEOUT) {
      // Check for button press with proper debouncing
      if (digitalRead(MANUAL_FEED_BUTTON_PIN) == LOW) {
        delay(BUTTON_DEBOUNCE_TIME);
        if (digitalRead(MANUAL_FEED_BUTTON_PIN) == LOW) {
          // Wait for release with yield
          while (digitalRead(MANUAL_FEED_BUTTON_PIN) == LOW) {
            delay(BUTTON_RELEASE_TIME);
            yield();
          }
          buttonPressed = true;
          break;
        }
      }

      // Update countdown timer every second
      if ((millis() - notifyStartTime) / 1000 !=
          ((millis() - notifyStartTime - 100) / 1000)) {
        int secondsLeft =
            (NOTIFY_TIMEOUT - (millis() - notifyStartTime)) / 1000;
        lcd.setCursor(14, 1);
        lcd.print(F("  "));  // Clear previous time
        lcd.setCursor(14, 1);
        lcd.print(secondsLeft);
      }

      delay(100);
      yield();
    }

    // If no button press, cancel feeding
    if (!buttonPressed) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print(F("Feeding canceled"));
      lcd.setCursor(0, 1);
      lcd.print(F("Bowl already has:"));
      nonBlockingWait(QUICK_DISPLAY_TIME);

      char foodText[16];
      snprintf(foodText, sizeof(foodText), "%.1fg in bowl", currentWeight);
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print(F("Food weight:"));
      lcd.setCursor(0, 1);
      lcd.print(foodText);
      nonBlockingWait(INFO_DISPLAY_TIME);
      return false;
    }

    // User confirmed continuation
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("Continuing..."));
    lcd.setCursor(0, 1);
    lcd.print(F("Adding more food"));
    nonBlockingWait(QUICK_DISPLAY_TIME);
  }

  return true;  // Continue with feeding
}

/**
 * Handle the food dispensing process
 */
float dispenseFoodWithFeedback(float initialWeight, float targetAmount) {
  // Setup moving average for stable readings
  const int movingAvgSize = 3;
  float weightReadings[movingAvgSize] = {initialWeight, initialWeight,
                                         initialWeight};
  int readingIndex = 0;
  float currentWeight = initialWeight;

  // Track variables during feeding
  uint32_t startTime = millis();
  uint32_t lastDisplayUpdate = 0;
  uint32_t lastWeightRead = 0;

  bool targetReached = false;
  bool preCloseExecuted = false;
  float dispensedWeight = 0;
  int retryCount = 0;
  int stabilityCounter = 0;

  // Open servo to begin dispensing
  hatchServo.write(SERVO_OPEN_ANGLE);
  delay(500);  // Give servo time to open

  // Main feeding loop
  while (!targetReached && (millis() - startTime < FEED_TIMEOUT)) {
    uint32_t now = millis();

    // Read weight at regular intervals
    if (now - lastWeightRead >= WEIGHT_READ_INTERVAL) {
      lastWeightRead = now;

      // Verify scale is still working
      if (!scale.is_ready()) {
        // Try a few times before giving up
        bool recovered = false;
        for (int i = 0; i < 5; i++) {
          delay(100);
          yield();
          if (scale.is_ready()) {
            recovered = true;
            break;
          }
        }

        if (!recovered) {
          lcdMessage("Scale error!", "Closing hatch", LCD_TIMEOUT);
          hatchServo.write(SERVO_CLOSE_ANGLE);
          nonBlockingWait(LCD_TIMEOUT);
          return dispensedWeight;
        }
      }

      // Update moving average
      weightReadings[readingIndex] = scale.get_units(1);  // Fast read
      readingIndex = (readingIndex + 1) % movingAvgSize;

      // Calculate current weight and dispensed amount
      float sumWeight = 0;
      for (int i = 0; i < movingAvgSize; i++) {
        sumWeight += weightReadings[i];
      }
      currentWeight = sumWeight / movingAvgSize;

      dispensedWeight = currentWeight - initialWeight;
      if (dispensedWeight < 0) dispensedWeight = 0;

      // Pre-close logic when approaching target
      if (!preCloseExecuted &&
          dispensedWeight >= targetAmount * FEED_CALIBRATION_FACTOR) {
        // Pre-close when we reach threshold
        hatchServo.write(SERVO_CLOSE_ANGLE);
        preCloseExecuted = true;

        // Show pre-close message
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print(F("Almost there..."));
        lcd.setCursor(0, 1);
        lcd.print(F("Food settling"));

        // Give food time to fall and settle
        delay(SETTLE_FINAL_TIME);  // Using value from config.h
        yield();

        // Re-measure after settling
        float settledWeight = measureSettledWeight(5, 2);
        dispensedWeight = settledWeight - initialWeight;
        if (dispensedWeight < 0) dispensedWeight = 0;

        // Update current weight and readings array
        currentWeight = settledWeight;
        for (int i = 0; i < movingAvgSize; i++) {
          weightReadings[i] = currentWeight;
        }

        // Check if we need to open again (underfeeding)
        if (dispensedWeight < targetAmount * FEED_COMPLETE_FACTOR &&
            retryCount < FEED_RETRY_TIMEOUT) {
          retryCount++;
          char retryText[16];
          snprintf(retryText, sizeof(retryText), "Retry #%d", retryCount);
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print(F("Need more food"));
          lcd.setCursor(0, 1);
          lcd.print(retryText);

          hatchServo.write(SERVO_OPEN_ANGLE);
          delay(300);
          preCloseExecuted = false;
        }
        // Handle successful dispense
        else if (dispensedWeight >= targetAmount * FEED_COMPLETE_FACTOR) {
          targetReached = true;
          char dispensedText[16];
          snprintf(dispensedText, sizeof(dispensedText), "Dispensed: %.1fg",
                   dispensedWeight);
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print(F("Target reached!"));
          lcd.setCursor(0, 1);
          lcd.print(dispensedText);
          nonBlockingWait(QUICK_DISPLAY_TIME);
        }
        // Handle case where we can't reach target despite retries
        else if (retryCount >= FEED_RETRY_TIMEOUT) {
          char warningText[16];
          snprintf(warningText, sizeof(warningText), "%.1fg dispensed",
                   dispensedWeight);
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print(F("Warning: Only"));
          lcd.setCursor(0, 1);
          lcd.print(warningText);
          nonBlockingWait(INFO_DISPLAY_TIME);
          targetReached = true;
        }
      }

      // Standard target check for when pre-close isn't active
      if (!preCloseExecuted) {
        if (dispensedWeight >= targetAmount) {
          hatchServo.write(SERVO_CLOSE_ANGLE);
          preCloseExecuted = true;

          // Confirm with stable readings
          if (stabilityCounter < 2) {
            stabilityCounter++;
          } else {
            targetReached = true;
          }
        }

        // Emergency stop if way too much food dispensed (use 125% threshold)
        if (dispensedWeight >= targetAmount * 1.25f) {
          hatchServo.write(SERVO_CLOSE_ANGLE);
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print(F("Warning!"));
          lcd.setCursor(0, 1);
          lcd.print(F("Excess food!"));
          nonBlockingWait(QUICK_DISPLAY_TIME);
          targetReached = true;
        }
      }
    }

    // Update display periodically
    if (now - lastDisplayUpdate >= LCD_UPDATE_INTERVAL) {
      lastDisplayUpdate = now;
      updateFeedingDisplay(dispensedWeight, targetAmount);
    }

    yield();
    delay(10);
  }

  // Ensure servo is closed
  hatchServo.write(SERVO_CLOSE_ANGLE);

  return dispensedWeight;
}

/**
 * Show final feeding results
 * @param initialWeight Weight before feeding
 * @param finalWeight Weight after feeding
 * @param targetAmount Target amount to dispense
 */
void showFeedingResults(float initialWeight, float finalWeight,
                        float targetAmount) {
  float dispensedWeight = finalWeight - initialWeight;
  if (dispensedWeight < 0) dispensedWeight = 0;

  // Calculate percentage of target
  int feedPct = (dispensedWeight / targetAmount) * 100;
  feedPct = constrain(feedPct, 0, 999);

  // Show feeding complete message
  char addedText[16];
  snprintf(addedText, sizeof(addedText), "Added: %.1fg", dispensedWeight);
  lcdMessage("Feeding complete", addedText, INFO_DISPLAY_TIME);

  // Show accuracy info
  char accuracyText[16];
  snprintf(accuracyText, sizeof(accuracyText), "Accuracy: %d%%", feedPct);

  // Show quality assessment with proper function call
  const char* qualityMsg;
  if (feedPct >= 95 && feedPct <= 105) {
    qualityMsg = "Perfect portion!";
  } else if (feedPct < 80) {
    qualityMsg = "Underfed - retry?";
  } else if (feedPct > 120) {
    qualityMsg = "Overfed - adjust";
  } else {
    qualityMsg = "Good enough";
  }

  // Fixed: Use lcdMessage since both strings are now char*
  lcdMessage(accuracyText, qualityMsg, INFO_DISPLAY_TIME);
  nonBlockingWait(INFO_DISPLAY_TIME);

  // Show total food in bowl
  char totalText[16];
  snprintf(totalText, sizeof(totalText), "Total: %.1fg", finalWeight);
  lcdMessage("Bowl now contains", totalText, INFO_DISPLAY_TIME);
}

/**
 * Main feeding function that orchestrates the entire feeding process
 * @param isScheduled Whether this is a scheduled feeding (true) or manual
 * (false)
 */
void feeding(bool isScheduled = false) {
  DEBUG_PRINTLN(F("Start feeding sequence..."));

  // Notify server that feeding is starting
  if (WiFi.status() == WL_CONNECTED &&
      isWebConnected()) {  // Fixed: Using WiFi.status() == WL_CONNECTED
    if (isScheduled) {
      sendLogEvent("feeding_start", "Scheduled feeding initiated");
    } else {
      sendLogEvent("feeding_start", "Manual feeding initiated");
    }
  }

  // Step 1: Initialize and check scale
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Feeding time"));
  lcd.setCursor(0, 1);
  lcd.print(F("Checking scale"));
  nonBlockingWait(QUICK_DISPLAY_TIME);

  // Check if scale is responsive
  if (!checkScaleReady(SCALE_TIMEOUT)) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("Error"));
    lcd.setCursor(0, 1);
    lcd.print(F("Scale not ready!"));
    nonBlockingWait(INFO_DISPLAY_TIME);
    return;
  }

  // Step 2: Check if there's already food on the scale
  lcd.setCursor(0, 1);
  lcd.print(F("Checking bowl..."));
  nonBlockingWait(QUICK_DISPLAY_TIME);

  // Get stable initial weight
  float currentFoodWeight = getStableWeight(5, 2, WEIGHT_STABILITY_THRESHOLD);
  if (currentFoodWeight < 0) currentFoodWeight = 0;

  // Check if there's already food in the bowl
  if (!checkExistingFood(currentFoodWeight, FEED_THRESHOLD)) {
    return;  // User canceled feeding
  }

  // Step 3: Start feeding process
  float initialWeight = currentFoodWeight;
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Starting feed"));
  lcd.setCursor(0, 1);
  lcd.print(F("Opening hatch..."));
  nonBlockingWait(QUICK_DISPLAY_TIME);

  // Step 4: Perform the feeding
  float dispensedAmount = dispenseFoodWithFeedback(initialWeight, FEED_WEIGHT);

  // Step 5: Wait for food to settle and take final measurement
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Measuring final"));
  lcd.setCursor(0, 1);
  lcd.print(F("weight..."));
  nonBlockingWait(SETTLE_FINAL_TIME);  // Wait for food to settle

  // Get final stable weight
  float finalWeight = measureSettledWeight(5, 5);

  // Step 6: Show feeding results
  showFeedingResults(initialWeight, finalWeight, FEED_WEIGHT);

  // Get current levels for server update
  float dispensedWeight = finalWeight - initialWeight;
  if (dispensedWeight < 0) dispensedWeight = 0;

  float foodLevel =
      (FEED_TOTAL_WEIGHT - dispensedWeight) / FEED_TOTAL_WEIGHT * 100.0;
  foodLevel = constrain(foodLevel, 0, 100);

  // Get water level
  float waterLevel = 50.0;  // Default if not available
  float distanceCm = getDistance();
  if (distanceCm > 0) {
    float waterHeight = DISTANCE_WATER_EMPTY - distanceCm;
    waterLevel =
        (waterHeight / (DISTANCE_WATER_EMPTY - DISTANCE_WATER_FULL)) * 100;
    waterLevel = constrain(waterLevel, 0, 100);
  }

  // Update server with feeding results at the end
  if (WiFi.status() == WL_CONNECTED &&
      isWebConnected()) {  // Fixed: Using WiFi.status() == WL_CONNECTED
    // Format detailed completion message
    char details[48];
    snprintf(details, sizeof(details),
             "%.1fg dispensed, food: %.0f%%, water: %.0f%%", dispensedWeight,
             foodLevel, waterLevel);

    // Send the feeding complete notification
    sendFeedingComplete(isScheduled, details, foodLevel, waterLevel);
  }
}

#endif  // FEEDING_HELPERS_H
