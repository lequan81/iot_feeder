#ifndef CONFIG_H
#define CONFIG_H

#pragma once

//==============================================================================
// Debug Configuration
//==============================================================================
#define DEBUG  // comment to remove serial output

#ifdef DEBUG
#define DEBUG_PRINT(x) Serial.print(x)
#define DEBUG_PRINTLN(x) Serial.println(x)
#else
#define DEBUG_PRINT(x)
#define DEBUG_PRINTLN(x)
#endif

// Arduino Cloud variables
#define DEVICE_ID ""  // Update this
#define THING_ID ""   // Update this

//==============================================================================
// Display Settings
//==============================================================================
// Display timeouts
#define LCD_TIMEOUT 3000         // Default time to show message on LCD in ms
#define INFO_DISPLAY_TIME 3000   // 3 seconds for informational messages
#define QUICK_DISPLAY_TIME 2000  // 2 seconds for quick notifications
#define LCD_UPDATE_INTERVAL 500  // Update LCD display every 500ms
#define LCD_BACKLIGHT_TIMEOUT 60000UL  // Turn off backlight after 1 minute

// LCD hardware settings
#define LCD_ADDR 0x27                  // LCD I2C address
#define LCD_X 16                       // LCD width in characters
#define LCD_Y 2                        // LCD height in characters
#define EMPTY_LINE "                "  // 16 spaces for clearing line

//==============================================================================
// WiFi & Network Settings
//==============================================================================
#define MAX_RETRY_COUNT 5           // Maximum number of connection attempts
#define CONNECTION_TIMEOUT 10000UL  // Timeout for each attempt (10 seconds)
#define SETUP_STEP_TIMEOUT 30000UL  // Timeout for each setup step

// NTP Configuration
#define NTP_SERVER "asia.pool.ntp.org"
#define NTP_OFFSET 25200UL            // Timezone offset (UTC +7:00)
#define NTP_UPDATE_INTERVAL 360000UL  // Update interval (6 minutes)

//==============================================================================
// Sensor Configuration
//==============================================================================
// Ultrasonic Sensor
#define PING_SAMPLES 5    // Times to read the sensor for filtering
#define MAX_DISTANCE 100  // Maximum distance in centimeters

// Water Tank
#define WATER_CRITICAL_HEIGHT 2.0   // Water level in cm to trigger feeding
#define DISTANCE_WATER_EMPTY 19.0   // Distance when tank is empty
#define DISTANCE_WATER_FULL 16.0    // Distance when tank is full
#define WATER_CHECK_INTERVAL 10000  // Check water every 10 seconds

//==============================================================================
// Water Management Settings
//==============================================================================
#define REFILL_DURATION 10000   // 10 seconds for refill
#define COOLDOWN_PERIOD 300000  // 5 minutes cooldown

//==============================================================================
// Servo Configuration
//==============================================================================
#define SERVO_OPEN_ANGLE 60
#define SERVO_CLOSE_ANGLE 180
#define SERVO_OPEN_INTERVAL 200  // Time to open the hatch in milliseconds

//==============================================================================
// Load Cell & Feeding Configuration
//==============================================================================
// Basic Feeding Parameters
#define FEED_THRESHOLD 50.0f        // Minimum weight for food portions (g)
#define FEED_WEIGHT 65.0f           // Weight of food to dispense in grams
#define FEED_TOTAL_WEIGHT 200.0f    // Total weight of the container (g)
#define FEED_TIMEOUT 30000UL        // Maximum time for feeding sequence (30s)
#define FEED_RETRY_TIMEOUT 2        // Maximum number of retry attempts
#define CALIBRATION_FACTOR 374.13f  // Calibration factor for load cell

// Feed Control Factors
#define FEED_CALIBRATION_FACTOR 0.9f  // Close hatch at 85% of target
#define FEED_COMPLETE_FACTOR 0.95f    // Accept 95% of target as complete

// Scale Stability Parameters
#define WEIGHT_STABILITY_THRESHOLD 0.3f  // Maximum variance for stable readings
#define WEIGHT_READ_INTERVAL 100         // Read weight every 100ms
#define SCALE_TIMEOUT 3000               // Scale initialization timeout (ms)
#define SETTLE_FINAL_TIME 2000           // Time to wait for final settling (ms)

//==============================================================================
// Button Configuration
//==============================================================================
#define BUTTON_DEBOUNCE_TIME 50  // Button debounce time in ms
#define BUTTON_RELEASE_TIME 10   // Button release check interval in ms

//==============================================================================
// Web Client Configuration
//==============================================================================
#define WEB_SERVER_URL \
  "192.168.1.100"             // Server address without http:// or port
#define WEB_SERVER_PORT 3001  // Socket.IO server port
#define WEB_CLIENT_ID "esp8266-feeder"  // Client ID for this device
#define WEB_RECONNECT_INTERVAL 10000    // Reconnect interval (10 seconds)
#define WEB_UPDATE_INTERVAL 5000  // Update interval for status (5 seconds)

#endif  // CONFIG_H