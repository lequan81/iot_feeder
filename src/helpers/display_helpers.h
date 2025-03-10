#ifndef DISPLAY_HELPERS_H
#define DISPLAY_HELPERS_H

#include <Arduino.h>
#include <ESP8266WiFi.h>  // Include ESP8266WiFi explicitly
#include <LiquidCrystal_I2C.h>
#include <NTPClient.h>

#include "../config.h"

// Forward declarations
extern LiquidCrystal_I2C lcd;
extern NTPClient timeClient;
extern float getDistance();
extern float getStableWeight(int numReadings, int samplesPerReading,
                             float stabilityThreshold);
extern bool hasSchedules();
extern uint32_t getNextScheduledFeeding();
extern void nonBlockingWait(uint32_t waitTime, uint32_t startDisplayTime = 0);
extern bool webConnected;
extern bool offlineModeActive;
extern bool isWebConnected();

// Display state enumeration - make static for better RAM usage
enum DisplayState {
  DISPLAY_STATUS,
  DISPLAY_LEVELS,
  DISPLAY_NEXT_FEEDING,  // Fixed: Corrected typo in enum value
  DISPLAY_MENU
};

// Global variables
static DisplayState currentDisplayState = DISPLAY_STATUS;
static uint32_t displayStateChangeTime = 0;
static const uint32_t DISPLAY_STATE_DURATION =
    5000;  // 5 seconds per display state
static bool displayActive = false;

// Function declarations
void showSystemStatusScreen();
void showLevelsScreen();
void showNextFeedingScreen();

/**
 * Update the info display based on current display state
 */
void updateInfoDisplay() {
  switch (currentDisplayState) {
    case DISPLAY_STATUS:
      showSystemStatusScreen();
      break;
    case DISPLAY_LEVELS:
      showLevelsScreen();
      break;
    case DISPLAY_NEXT_FEEDING:
      showNextFeedingScreen();
      break;
    case DISPLAY_MENU:
      // Reserved for future menu implementation
      currentDisplayState = DISPLAY_STATUS;  // Default back to status
      break;
  }
}

/**
 * Show system status screen (connection info, time)
 */
void showSystemStatusScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);

  if (offlineModeActive) {
    lcd.print(F("OFFLINE MODE"));
  } else if (isWebConnected()) {
    lcd.print(F("Connected"));
  } else if (WiFi.status() ==
             WL_CONNECTED) {  // Fixed: Using WiFi.status() == WL_CONNECTED
                              // instead of WiFi.isConnected()
    lcd.print(F("WiFi Only"));
  } else {
    lcd.print(F("No Connection"));
  }

  lcd.setCursor(0, 1);

  // Show time if available, otherwise uptime
  if (timeClient.isTimeSet()) {
    lcd.print(timeClient.getFormattedTime());
  } else {
    // Show uptime in format HH:MM:SS
    uint32_t uptime = millis() / 1000;  // seconds
    uint8_t seconds = uptime % 60;
    uint8_t minutes = (uptime / 60) % 60;
    uint8_t hours = (uptime / 3600) % 24;

    char uptimeStr[16];
    snprintf(uptimeStr, sizeof(uptimeStr), "Up: %02d:%02d:%02d", hours, minutes,
             seconds);
    lcd.print(uptimeStr);
  }
}

/**
 * Show food and water levels screen
 */
void showLevelsScreen() {
  lcd.clear();

  // Get food level
  float foodLevel = 0;
  float currentWeight = getStableWeight(3, 2, 0.5);
  if (currentWeight > 0) {
    foodLevel = (currentWeight / FEED_TOTAL_WEIGHT) * 100.0;
    foodLevel = constrain(foodLevel, 0, 100);
  }

  // Get water level
  float waterLevel = 0;
  float distanceCm = getDistance();
  if (distanceCm > 0) {
    float waterHeight = DISTANCE_WATER_EMPTY - distanceCm;
    waterLevel =
        (waterHeight / (DISTANCE_WATER_EMPTY - DISTANCE_WATER_FULL)) * 100;
    waterLevel = constrain(waterLevel, 0, 100);
  }

  // Show food level on first line with visual indicator
  lcd.setCursor(0, 0);
  lcd.print(F("Food: "));
  lcd.print((int)foodLevel);
  lcd.print(F("% "));

  // Simple visual indicator
  int blocks = map(constrain(foodLevel, 0, 100), 0, 100, 0, 5);
  for (int i = 0; i < 5; i++) {
    lcd.write(i < blocks ? (byte)0xFF : ' ');
  }

  // Show water level on second line with visual indicator
  lcd.setCursor(0, 1);
  lcd.print(F("Water: "));
  lcd.print((int)waterLevel);
  lcd.print(F("% "));

  // Simple visual indicator
  blocks = map(constrain(waterLevel, 0, 100), 0, 100, 0, 5);
  for (int i = 0; i < 5; i++) {
    lcd.write(i < blocks ? (byte)0xFF : ' ');
  }
}

/**
 * Show next feeding time screen
 */
void showNextFeedingScreen() {
  if (!timeClient.isTimeSet() || !hasSchedules()) {
    // Fall back to levels display if we can't show next feeding
    showLevelsScreen();
    return;
  }

  uint32_t currentEpoch = timeClient.getEpochTime();
  uint32_t nextFeeding = getNextScheduledFeeding();

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Next feeding:"));

  if (nextFeeding <= 0) {
    lcd.setCursor(0, 1);
    lcd.print(F("No schedule set"));
    return;
  }

  // Calculate time until feeding
  int32_t secondsToFeeding = nextFeeding - currentEpoch;

  lcd.setCursor(0, 1);

  if (secondsToFeeding <= 0) {
    lcd.print(F("Due now!"));
  } else {
    // Format based on how far away the feeding is
    if (secondsToFeeding < 60) {
      // Less than a minute
      lcd.print(F("In < 1 minute"));
    } else if (secondsToFeeding < 3600) {
      // Less than an hour
      int minutes = secondsToFeeding / 60;
      lcd.print(F("In "));
      lcd.print(minutes);
      lcd.print(F(" minute"));
      if (minutes != 1) lcd.print(F("s"));
    } else if (secondsToFeeding < 86400) {
      // Less than a day
      int hours = secondsToFeeding / 3600;
      int minutes = (secondsToFeeding % 3600) / 60;
      char timeStr[16];
      snprintf(timeStr, sizeof(timeStr), "In %dh %dm", hours, minutes);
      lcd.print(timeStr);
    } else {
      // More than a day
      int days = secondsToFeeding / 86400;
      lcd.print(F("In "));
      lcd.print(days);
      lcd.print(F(" day"));
      if (days != 1) lcd.print(F("s"));
    }
  }
}

/**
 * Advance to the next display state
 */
void advanceDisplayState() {
  displayStateChangeTime = millis();

  // Cycle through display states
  switch (currentDisplayState) {
    case DISPLAY_STATUS:
      currentDisplayState = DISPLAY_LEVELS;
      break;
    case DISPLAY_LEVELS:
      if (hasSchedules() && timeClient.isTimeSet()) {
        currentDisplayState =
            DISPLAY_NEXT_FEEDING;  // Fixed: Corrected typo here
      } else {
        currentDisplayState = DISPLAY_STATUS;
      }
      break;
    case DISPLAY_NEXT_FEEDING:  // Fixed typo here
    case DISPLAY_MENU:
      currentDisplayState = DISPLAY_STATUS;
      break;
  }

  // Update display immediately
  updateInfoDisplay();
}

/**
 * Check if it's time to update the display
 */
void checkDisplayUpdate(uint32_t currentMillis) {
  // Automatic rotation of display states
  if (displayActive &&
      currentMillis - displayStateChangeTime > DISPLAY_STATE_DURATION) {
    advanceDisplayState();
  }
}

/**
 * Activate the display system
 */
void activateDisplay() {
  lcd.backlight();
  displayActive = true;
  displayStateChangeTime = millis();
  updateInfoDisplay();
}

/**
 * Deactivate the display system
 */
void deactivateDisplay() {
  lcd.noBacklight();
  displayActive = false;
}

#endif  // DISPLAY_HELPERS_H
