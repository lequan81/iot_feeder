#ifndef LCD_HELPERS_H
#define LCD_HELPERS_H

#include <Arduino.h>
#include <LiquidCrystal_I2C.h>

#include "../config.h"

// Forward declarations for external functions used
extern void nonBlockingWait(uint32_t waitTime, uint32_t startDisplayTime = 0);
extern LiquidCrystal_I2C lcd;

/**
 * Display message on LCD with non-blocking delay
 * Use F() in call site for string literals to save RAM
 * @param line1 First line text (NULL to leave unchanged)
 * @param line2 Second line text (NULL to leave unchanged)
 * @param waitTime Time to display message in ms (0 for no wait)
 * @param clearScreen Whether to clear the screen first
 */
void lcdMessage(const char* line1, const char* line2 = NULL,
                uint32_t waitTime = LCD_TIMEOUT, bool clearScreen = true) {
  if (clearScreen) {
    lcd.clear();
  }

  if (line1) {
    lcd.setCursor(0, 0);
    lcd.print(line1);
  }

  if (line2) {
    lcd.setCursor(0, 1);
    lcd.print(line2);
  }

  if (waitTime > 0) {
    nonBlockingWait(waitTime);
  }
}

/**
 * Display a formatted message with a floating-point value
 * @param line1 First line text
 * @param prefix Text before the value on line 2
 * @param value Float value to display
 * @param precision Number of decimal places
 * @param suffix Text after the value
 * @param waitTime Time to display message
 */
void lcdMessageWithValue(const char* line1, const char* prefix, float value,
                         int precision, const char* suffix = NULL,
                         uint32_t waitTime = LCD_TIMEOUT) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line1);

  lcd.setCursor(0, 1);
  lcd.print(prefix);
  lcd.print(value, precision);

  if (suffix) {
    lcd.print(suffix);
  }

  if (waitTime > 0) {
    nonBlockingWait(waitTime);
  }
}

/**
 * High-performance LCD line clearing function
 * @param col Starting column position (0-based)
 * @param row Row position (0-based)
 * @param length Number of characters to erase (default: until end of line)
 */
void clearLineLCD(uint8_t col, uint8_t row, uint8_t length = 0) {
  static char buffer[17];  // Buffer for characters (+1 for null terminator)

  // If length is 0 or exceeds available space, fill to end of line
  if (length == 0 || col + length > LCD_X) {
    length = LCD_X - col;
  }

  // Skip if nothing to clear
  if (length <= 0) return;

  // Fill buffer with spaces
  memset(buffer, ' ', length);
  buffer[length] = '\0';  // Null-terminate

  // Position cursor once and print the buffer
  lcd.setCursor(col, row);
  lcd.print(buffer);
}

/**
 * Display a progress bar on the LCD
 * @param percentage Value from 0-100 to display
 */
void progressBar(float percentage) {
  // Define the width of the progress bar in characters
  uint8_t partialBlock[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

  // Constrain percentage between 0-100
  float percent = constrain(percentage, 0, 100);

  // Calculate the percentage to display on top right
  char percentStr[5];
  snprintf(percentStr, sizeof(percentStr), "%3d%%", int(percent));

  // Display the percentage on the right side of row 0
  lcd.setCursor(LCD_X - 4, 0);
  lcd.print(percentStr);

  // Calculate the width of the progress bar in pixels
  // Each LCD character is 5 pixels wide
  float pixelWidth = LCD_X * 5.0;
  int filledPixels = (percent / 100.0) * pixelWidth;

  // Calculate complete blocks and remainder
  byte completeBlocks = filledPixels / 5;
  byte remainderPixels = filledPixels % 5;

  // Draw the progress bar on row 1
  lcd.setCursor(0, 1);

  // Define custom characters for smooth transitions
  if (remainderPixels > 0) {
    // Fill the appropriate number of columns in the custom character
    for (byte row = 0; row < 8; row++) {
      for (byte col = 0; col < remainderPixels; col++) {
        bitWrite(partialBlock[row], 4 - col, 1);
      }
    }

    // Create the custom character
    lcd.createChar(0, partialBlock);
  }

  // Draw the complete blocks (filled character)
  for (byte i = 0; i < completeBlocks && i < LCD_X; i++) {
    lcd.write(0xFF);  // Solid block character
  }

  // Draw the partial block if any
  if (remainderPixels > 0 && completeBlocks < LCD_X) {
    lcd.write(byte(0));  // Custom partial block character
    completeBlocks++;    // Account for the partial block
  }

  // Fill the rest with empty space
  for (byte i = completeBlocks; i < LCD_X; i++) {
    lcd.print(" ");
  }
}

/**
 * Wait for button press with timeout and update countdown on LCD
 * @param buttonPin Pin to check (using INPUT_PULLUP)
 * @param timeout Timeout in milliseconds
 * @param position Position to show countdown [column, row]
 * @return true if button pressed, false if timeout
 */
bool waitForButtonWithTimeout(uint8_t buttonPin, uint32_t timeout,
                              uint8_t posCol = 14, uint8_t posRow = 1) {
  uint32_t startTime = millis();
  uint32_t lastUpdate = 0;
  int lastSecond = -1;

  while (millis() - startTime < timeout) {
    // Calculate seconds left
    int secondsLeft = (timeout - (millis() - startTime)) / 1000;

    // Update display once per second
    if (secondsLeft != lastSecond) {
      lastSecond = secondsLeft;
      lcd.setCursor(posCol, posRow);
      lcd.print(F("  "));  // Clear previous time
      lcd.setCursor(posCol, posRow);
      lcd.print(secondsLeft);
    }

    // Check for button press with debounce
    if (digitalRead(buttonPin) == LOW) {
      delay(BUTTON_DEBOUNCE_TIME);  // Debounce
      if (digitalRead(buttonPin) == LOW) {
        // Wait for release
        while (digitalRead(buttonPin) == LOW) {
          delay(BUTTON_RELEASE_TIME);
          yield();
        }
        return true;
      }
    }

    yield();
    delay(10);
  }

  return false;  // Timeout
}

#endif  // LCD_HELPERS_H
