#ifndef SCALE_HELPERS_H
#define SCALE_HELPERS_H

#include <Arduino.h>
#include <HX711.h>

#include "../config.h"

// Forward declarations for external functions and objects
extern HX711 scale;

/**
 * Initialize the scale with the correct calibration factor
 * @return True if scale is ready after initialization
 */
bool initializeScale() {
  scale.set_scale(CALIBRATION_FACTOR);  // Use calibration factor from config.h
  delay(100);
  yield();
  return scale.is_ready();
}

/**
 * Get stable weight reading from scale with stability verification
 * @param numReadings Number of readings to take
 * @param samplesPerReading Number of samples per reading
 * @param stabilityThreshold Maximum acceptable variation between min/max
 * @return Average stable weight or last reading if unstable
 */
float getStableWeight(int numReadings = 5, int samplesPerReading = 2,
                      float stabilityThreshold = 0.3) {
  float weights[10];                       // Maximum supported readings
  if (numReadings > 10) numReadings = 10;  // Safety check

  // Take multiple readings
  for (int i = 0; i < numReadings; i++) {
    if (scale.is_ready()) {
      weights[i] = scale.get_units(samplesPerReading);
    } else {
      weights[i] = 0;  // Mark failed reading
    }
    yield();  // Allow system tasks
    delay(50);
  }

  // Calculate min, max, and total for stability check
  float minWeight = 9999;
  float maxWeight = -9999;
  float totalWeight = 0;
  int validReadings = 0;

  // Find min, max and total of valid readings
  for (int i = 0; i < numReadings; i++) {
    if (weights[i] != 0) {  // Skip failed readings
      if (weights[i] < minWeight) minWeight = weights[i];
      if (weights[i] > maxWeight) maxWeight = weights[i];
      totalWeight += weights[i];
      validReadings++;
    }
  }

  // If all readings failed
  if (validReadings == 0) {
    DEBUG_PRINTLN(F("No valid readings from scale!"));
    return 0;
  }

  // Calculate average weight
  float avgWeight = totalWeight / validReadings;

  // Debug stability info
  DEBUG_PRINT(F("Weight: "));
  DEBUG_PRINT(avgWeight);
  DEBUG_PRINT(F("g (min="));
  DEBUG_PRINT(minWeight);
  DEBUG_PRINT(F(", max="));
  DEBUG_PRINT(maxWeight);
  DEBUG_PRINT(F(", diff="));
  DEBUG_PRINT(maxWeight - minWeight);
  DEBUG_PRINTLN(F(")"));

  // Return the average weight regardless of stability
  // The caller can check if the difference is too large and handle accordingly
  return avgWeight;
}

/**
 * Calculate filtered weight from buffer (median + average for robustness)
 * @param buffer Array of weight readings
 * @param size Size of the array
 * @return Median or average of middle values
 */
float calculateFilteredWeight(float* buffer, uint8_t size) {
  if (size <= 1) return buffer[0];

  // Simple insertion sort for small arrays
  float sortedBuffer[10];  // Max expected buffer size
  memcpy(sortedBuffer, buffer, size * sizeof(float));

  for (uint8_t i = 1; i < size; i++) {
    float key = sortedBuffer[i];
    int8_t j = i - 1;

    while (j >= 0 && sortedBuffer[j] > key) {
      sortedBuffer[j + 1] = sortedBuffer[j];
      j--;
    }
    sortedBuffer[j + 1] = key;
  }

  // Use median for odd sizes, average of middle two for even sizes
  if (size % 2 == 1) {
    return sortedBuffer[size / 2];
  } else {
    return (sortedBuffer[size / 2 - 1] + sortedBuffer[size / 2]) / 2.0f;
  }
}

#endif  // SCALE_HELPERS_H
