#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <HX711.h>
#include <LiquidCrystal_I2C.h>
#include <NTPClient.h>
#include <NewPing.h>
#include <Servo.h>
#include <WiFiUdp.h>
#include <Wire.h>

#include "config.h"
#include "globals.h"
#include "pins.h"
#include "secret.h"

LiquidCrystal_I2C lcd(LCD_ADDR, LCD_X, LCD_Y);
NewPing sonar(TRIG_PIN, ECHO_PIN, MAX_DISTANCE);
Servo hatchServo;
HX711 scale;

// Define NTP Client to get time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, NTP_SERVER, NTP_OFFSET, NTP_UPDATE_INTERVAL);

// Function prototypes
// Setup functions
static bool setupLCD();
static bool setupPins();
static bool setupScale();
static bool setupWiFi();
static bool setupNTPTimer();
// Helper functions
static unsigned int nonBlockingMedianPing(NewPing& sonar, uint8_t iterations,
                                          unsigned int maxDistance,
                                          unsigned int maxDuration);
static float getDistance();
static void checkWaterLevel();
static void feeding();
static bool checkAnyButtonPressed();
// Utility functions
static float calculateFilteredWeight(float* buffer, uint8_t size);
static void nonBlockingWait(uint32_t waitTime, uint32_t startDisplayTime = 0);
static void showSetupStep(const char* setupName, bool (*setupFunction)(),
                          uint32_t timeout);
static void clearLineLCD(uint8_t col, uint8_t row, uint8_t length = 0);
static void progressBar(float percentage);
static void scrollTextContinuous(const char* message, uint8_t col, uint8_t row,
                                 uint8_t limitAnimation = 0,
                                 uint16_t scrollSpeed = 300,
                                 uint16_t pauseBeforeMs = 500,
                                 uint16_t pauseAfterMs = 300);

void setup() {
#ifdef DEBUG
  Serial.begin(115200);
  // while (Serial.available() <= 0);  // Wait for serial to connect
  delay(50);  // Delay for stability
  Serial.setDebugOutput(true);
  DEBUG_PRINTLN("IoT Pet Feeder Initializing");
#endif

  // Step 1: Setting up the LCD display
  setupLCD();
  // Step 2: Setting up GPIO pins
  showSetupStep("Setting up GPIOs", setupPins, 5000);
  // Step 3: Setting up the HX711 load cell amplifier
  showSetupStep("Setting up scale", setupScale, 15000);
  // Step 4: Setting up WiFi connection
  showSetupStep("Connect to WiFi", setupWiFi, 20000);
  // Step 5: Setting up NTP time synchronization
  showSetupStep("Setting up NTP", setupNTPTimer, 8000);

  // Show overall status but ALWAYS continue to loop()
  lcd.clear();
  lcd.print(F("Setup completed!"));
  lcd.setCursor(0, 1);
  lcd.print(WiFi.status() == WL_CONNECTED ? F("System ready!")
                                          : F("Offline Mode"));
  nonBlockingWait(LCD_TIMEOUT);
}

void loop() {
  static uint32_t lastUserActivityTime = millis();  // Initialize on first run

  // Static variables for interval management
  static uint32_t lastWaterCheckTime = 0;
  static uint32_t lastNtpUpdateTime = 0;
  // static const uint32_t WATER_CHECK_INTERVAL =
  //     10000;  // Check water every 10 seconds instead of 1 second

  // Allow background tasks to run
  yield();

  // Get current time once per loop iteration
  uint32_t currentMillis = millis();

  // Check for any button press - this happens on every loop for responsiveness
  if (checkAnyButtonPressed()) {
    // Button press detected - turn on lcd backlight
    lcd.backlight();

    // Always update activity timestamp on button press
    lastUserActivityTime = currentMillis;

    // Handle manual feeding button specifically
    if (digitalRead(MANUAL_FEED_BUTTON_PIN) == LOW) {
      feeding();  // Pass false to indicate this is a manual feeding
    }
  }

  // Check if LCD should be turned off due to inactivity
  if (currentMillis - lastUserActivityTime >= 60000UL) {
    lcd.noBacklight();
  }

  // Update NTP time every minute
  if (currentMillis - lastNtpUpdateTime >= 60000) {
    lastNtpUpdateTime = currentMillis;
    timeClient.update();
  }

  // Check water level at intervals
  if (currentMillis - lastWaterCheckTime >= WATER_CHECK_INTERVAL) {
    lastWaterCheckTime = currentMillis;
    checkWaterLevel();
  }

  // Important: Short delay with yield to prevent WDT
  delay(10);
  yield();
}

/* ----- Functions ------ */

// Show a setup step with state machine for timeout detection
enum SetupState {
  SETUP_START,
  SETUP_RUNNING,
  SETUP_COMPLETE,
  SETUP_TIMEOUT,
  SETUP_ERROR,
  SETUP_FINISHED
};

void showSetupStep(const char* setupName, bool (*setupFunction)(),
                   uint32_t timeout = SETUP_STEP_TIMEOUT) {
  // Variables needed throughout the state machine
  uint32_t startTime = millis();
  SetupState state = SETUP_START;
  bool setupSuccess = false;  // Default to failure

  while (state != SETUP_FINISHED) {
    // Always yield to keep WiFi stack happy
    yield();
    delay(5);

    // Switch based on current state
    switch (state) {
      case SETUP_START:
        // Initial display
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print(setupName);
        lcd.setCursor(0, 1);
        lcd.print(F("Initializing..."));
        nonBlockingWait(LCD_TIMEOUT, startTime);

        // Move to next state
        state = SETUP_RUNNING;
        break;

      case SETUP_RUNNING:
        setupSuccess = false;  // Reset the variable
        // Run the setup function
        setupSuccess = setupFunction();

        // Check for timeout
        if (millis() - startTime > timeout) {
          state = SETUP_TIMEOUT;
        } else {
          setupSuccess ? state = SETUP_COMPLETE : state = SETUP_ERROR;
        }
        break;

      case SETUP_COMPLETE:
        // Success - show completion message
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print(setupName);
        lcd.setCursor(0, 1);
        lcd.print(F("Setup completed!"));
        // Wait for LCD_TIMEOUT using our improved nonBlockingWait
        nonBlockingWait(LCD_TIMEOUT);
        // Set the result and exit state machine
        state = SETUP_FINISHED;
        break;

      case SETUP_TIMEOUT:
        // Timeout occurred - show error
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print(setupName);
        lcd.setCursor(0, 1);
        lcd.print(F("Setup timeout!"));

        // Wait so user can read the error
        nonBlockingWait(LCD_TIMEOUT);

        // Set result and exit state machine
        state = SETUP_FINISHED;
        break;

      case SETUP_ERROR:
        // Error occurred - show error
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print(setupName);
        lcd.setCursor(0, 1);
        lcd.print(F("Setup failed!"));

        // Wait so user can read the error
        nonBlockingWait(LCD_TIMEOUT);

        // Set result and exit state machine
        state = SETUP_FINISHED;
        break;

      case SETUP_FINISHED:
        // Do nothing, exit state machine
        DEBUG_PRINTLN(F("Setup finished"));
        break;
    }
  }
}

// Setup LCD display

bool setupLCD() {
  DEBUG_PRINT("\n[1/5] Setting up LCD display...");

  Wire.begin(SDA_PIN, SCL_PIN);  // Setting up I2C protocols
  delay(50);  // Fixed: Changed from delayMicroseconds to delay
  DEBUG_PRINT("\nI2C protocol initiated successfully");
  DEBUG_PRINT("\nInitializing LCD...");

  Wire.beginTransmission(LCD_ADDR);
  byte error = Wire.endTransmission();

  if (error != 0) {
    DEBUG_PRINT("\nLCD not found. Please checking the wiring connection");
    return false;
  } else {
    lcd.init();
    lcd.backlight();
    delay(5);
    lcd.clear();
    DEBUG_PRINT("\nLCD initiation completed successful");

    // Show welcome message
    lcd.clear();
    lcd.print(F("IoT Pet Feeder"));
    lcd.setCursor(0, 1);
    lcd.print(F("Starting up..."));
    nonBlockingWait(LCD_TIMEOUT);
    return true;
  }
}

// Setup GPIO pins
bool setupPins() {
  DEBUG_PRINT("\n[2/5] Configuring I/O pins...");

  pinMode(TRIG_PIN, OUTPUT);  // Sets the TRIG_PIN as an Output
  pinMode(ECHO_PIN, INPUT);   // Sets the ECHO_PIN as an Input
  pinMode(MANUAL_FEED_BUTTON_PIN, INPUT_PULLUP);
  pinMode(WATER_PUMP_RELAY_PIN, OUTPUT);

  delay(20);
  digitalWrite(WATER_PUMP_RELAY_PIN,
               LOW);  // set digital output as LOW for deactive relay, reduce
                      // power comsume

  hatchServo.attach(SERVO_PIN);
  hatchServo.write(SERVO_CLOSE_ANGLE);

  DEBUG_PRINT("\nPins initiation completed successful");
  return true;
}

/**
 * Sets up the HX711 load cell amplifier
 * @return true if setup was successful, false otherwise
 */

bool setupScale() {
  DEBUG_PRINTLN(F("[3/5] Initializing load cell scale..."));

  // Initialize HX711 communication
  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  yield();

  // Check if scale is responding
  DEBUG_PRINTLN(F("Checking for HX711 connection..."));

  // Wait for scale to be ready with timeout
  uint32_t startTime = millis();
  uint32_t timeout = 3000;  // 3 seconds timeout
  bool scaleDetected = false;
  bool tareSuccess = false;

  // Wait for scale with timeout
  while (millis() - startTime < timeout) {
    if (scale.is_ready()) {
      scaleDetected = true;
      break;
    }
    yield();
    delay(10);
  }

  // Check if scale was detected
  if (!scaleDetected) {
    DEBUG_PRINTLN(F("HX711 not detected. Check wiring connections."));
    yield();  // for stability

    clearLineLCD(0, 1, 16);
    lcd.setCursor(0, 1);
    lcd.print(F("HX711 not found!"));
    nonBlockingWait(LCD_TIMEOUT);

    return false;
  }

  // Scale detected, continue with setup
  DEBUG_PRINTLN(F("HX711 detected successfully!"));

  // Show progress on LCD
  clearLineLCD(0, 1, 16);
  lcd.setCursor(0, 1);
  lcd.print(F("HX711 detected!"));
  nonBlockingWait(1500);  // Short delay to show success message

  // Apply calibration factor
  scale.set_scale(CALIBRATION_FACTOR);
  yield();

  // Reset the scale to zero with better user feedback
  DEBUG_PRINTLN(F("Taring scale (setting to zero)..."));

  // Update LCD with taring status
  lcd.setCursor(0, 1);
  lcd.print(F("Taring scale... "));
  nonBlockingWait(LCD_TIMEOUT);  // Short delay to show tare message
  // Take multiple tare readings for accuracy
  scale.tare(5);  // Average of 5 readings
  yield();

  // Verify scale is working after tare
  startTime = millis();
  while (millis() - startTime < 2000) {
    if (scale.is_ready()) {
      tareSuccess = true;
      break;
    }
    delay(100);
    yield();
  }

  // Check if scale is still not responding
  if (tareSuccess) {
    DEBUG_PRINTLN(F("Scale tared successfully!"));

    lcd.setCursor(0, 1);
    lcd.print(F("Tare completed! "));
    nonBlockingWait(LCD_TIMEOUT);

  } else {
    DEBUG_PRINTLN(F("Scale not responding after tare!"));

    lcd.setCursor(0, 1);
    lcd.print(F("Scale not ready!"));
    nonBlockingWait(LCD_TIMEOUT);

    return false;
  }

  DEBUG_PRINTLN(F("Scale initialization complete!"));

  // Update LCD with final status
  lcd.setCursor(0, 1);
  lcd.print(F("Scale ready!    "));
  nonBlockingWait(LCD_TIMEOUT);
  return true;
}

/**
 * Setup WiFi connection with retry and timeout functionality
 */
bool setupWiFi() {
  DEBUG_PRINTLN(F("\n[4/5] Setting up WiFi..."));

  // Disconnect and prepare WiFi
  WiFi.disconnect();
  delay(150);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);

  // Try connecting up to MAX_RETRY_COUNT times
  for (uint8_t attempt = 1; attempt <= MAX_RETRY_COUNT; attempt++) {
    lcd.clear();
    lcd.print(F("Attempt no. #"));
    lcd.print(attempt);
    // scrollTextContinuous(WIFI_SSID, 0, 1, 3);
    lcd.setCursor(0, 1);
    lcd.print(F(WIFI_SSID));
    nonBlockingWait(LCD_TIMEOUT);

    // Start connection attempt
    DEBUG_PRINT(F("Connecting to SSID: "));
    DEBUG_PRINTLN(WIFI_SSID);

    // Display attempt information
    DEBUG_PRINT(F("Connection attempt "));
    DEBUG_PRINT(attempt);
    DEBUG_PRINT(F(" of "));
    DEBUG_PRINTLN(MAX_RETRY_COUNT);

    WiFi.begin(WIFI_SSID, WIFI_PSWD);

    // Important: Set status update interval
    uint32_t startTime = millis();
    uint32_t lastStatusTime = 0;
    uint8_t dotCount = 0;

    // Connection loop with timeout and regular status updates
    while (WiFi.status() != WL_CONNECTED &&
           millis() - startTime < CONNECTION_TIMEOUT) {
      // Print status update every 500ms regardless of connection state
      if (millis() - lastStatusTime >= 500) {
        lastStatusTime = millis();

        // Print dots to serial monitor
        DEBUG_PRINT(F("."));

        // Show elapsed time
        DEBUG_PRINT((millis() - startTime) / 1000);
        DEBUG_PRINTLN(F("s"));

        // Update LCD dots
        lcd.setCursor(strlen(WIFI_SSID), 1);
        lcd.print(F("    "));  // Clear dot area
        lcd.setCursor(strlen(WIFI_SSID), 1);
        for (uint8_t i = 0; i < (dotCount % 4); i++) {
          lcd.print('.');
        }
        dotCount++;
      }

      // Short delay to prevent CPU hogging
      delay(100);
      yield();
    }

    // Connection result
    DEBUG_PRINTLN();
    DEBUG_PRINT(F("Connection result: "));
    DEBUG_PRINTLN(WiFi.status());

    // Check if connected
    if (WiFi.status() == WL_CONNECTED) {
      // Success!
      WiFi.persistent(true);

      DEBUG_PRINTLN(F("WiFi connected successfully!"));
      DEBUG_PRINT(F("IP: "));
      DEBUG_PRINTLN(WiFi.localIP().toString());

      lcd.clear();
      lcd.print(F("WiFi Connected!"));
      lcd.setCursor(0, 1);
      lcd.print(WiFi.localIP().toString());
      nonBlockingWait(2000);
      return true;
    }

    // Failed attempt - show which specific error occurred
    DEBUG_PRINT(F("Connection failed. Status code: "));
    DEBUG_PRINTLN(WiFi.status());

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("WiFi Connection"));
    lcd.setCursor(0, 1);

    // Translate WiFi status to human-readable message
    switch (WiFi.status()) {
      case WL_NO_SSID_AVAIL:
        lcd.print(F("SSID unavaiable "));
        break;
      case WL_CONNECT_FAILED:
        lcd.print(F("WiFi Auth Failed"));
        break;
      case WL_DISCONNECTED:
        lcd.print(F("WiFi Disconnect "));
        break;
      default:
        lcd.print(F("Connect Failed"));
        break;
    }
    nonBlockingWait(LCD_TIMEOUT);
  }

  // All attempts failed
  DEBUG_PRINTLN(F("Failed to connect - all attempts exhausted"));

  lcd.clear();
  lcd.print(F("Connect failed"));
  lcd.setCursor(0, 1);
  lcd.print(F("OFFLINE MODE"));
  nonBlockingWait(LCD_TIMEOUT);
  return false;
}

bool setupNTPTimer() {
  if (WiFi.status() != WL_CONNECTED) {
    DEBUG_PRINTLN("\nCannot start NTP timer without WiFi connection");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("NTP Timer"));
    lcd.setCursor(0, 1);
    lcd.print(F("Skipped: No WiFi"));
    nonBlockingWait(LCD_TIMEOUT);
    return false;  // Return false instead of handling display
  } else {
    DEBUG_PRINT("\n[5/5] Synchronizing time...\nSetting up NTP Client...");
    timeClient.begin();
    timeClient.update();

    DEBUG_PRINT("\nSetting up NTP Client successful!\nNTP time: ");
    DEBUG_PRINTLN(timeClient.getFormattedTime());
    return true;
  }
}

// Function to check for any button press
bool checkAnyButtonPressed() {
  // Check your button on analog pin (A0)

  int analogValue = analogRead(A0);

  // DEBUG_PRINTLN("Analog value: " + String(analogValue));

  // // Check if any button value is detected (adjust thresholds as needed)
  // if ((analogValue >= BTN_PLUS_MIN && analogValue <= BTN_PLUS_MAX) ||
  //     (analogValue >= BTN_SEL_MIN && analogValue <= BTN_SEL_MAX) ||
  //     (analogValue >= BTN_MINUS_MIN && analogValue <= BTN_MINUS_MAX)) {
  //   return true;
  // }

  // Check manual feed button
  if (digitalRead(MANUAL_FEED_BUTTON_PIN) == LOW) {
    return true;
  }

  return false;
}

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
  unsigned long pingStart, pingEnd;
  unsigned long startTime = millis();

  // Take multiple samples with yields between them
  for (uint8_t i = 0; i < iterations; i++) {
    // Check if we've exceeded maximum duration
    if (millis() - startTime > maxDuration) {
      return 0;  // Timeout occurred
    }

    // Start a ping measurement
    pingStart = millis();
    samples[i] = sonar.ping_cm();
    pingEnd = millis();

    // If ping returns 0 (no echo or out of range), set to max distance
    if (samples[i] == 0) {
      samples[i] = maxDistance;
    }

    // Yield to allow ESP8266 background tasks to run
    yield();

    // Add small delay between pings while yielding
    unsigned long delayTime = 30;  // 30ms between pings
    unsigned long delayStart = millis();
    while (millis() - delayStart < delayTime) {
      yield();
      delay(1);
    }
  }

  // Sort the array to find median (bubble sort is fine for small arrays)
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
  uint32_t totalDistance = 0;  // Use unsigned 32-bit to accumulate distance
  uint8_t validReadings = 0;   // 8-bit counter is enough for small sample count

  // Take readings with minimal memory usage
  for (uint8_t pingIndex = 0; pingIndex < PING_SAMPLES; pingIndex++) {
    yield();                     // Single yield before measurement
    uint32_t uS = sonar.ping();  // Get ping duration
    yield();                     // yeild after measurement for better stability

    // Process valid readings only (non-zero response)
    if (uS > 0) {
      // Add to total (convert to cm directly to avoid float)
      // Distance = microseconds / 58 (for cm)
      totalDistance += sonar.convert_cm(uS);
      validReadings++;
    }

    delay(10);
  }

  // Return 0 if no valid readings
  if (validReadings == 0) {
    return 0;
  }

  // Calculate and return integer average
  return totalDistance / validReadings;
}

/**
 * Checks water level and activates relay if water is too low
 */
/**
 * Checks water level and activates relay if water is too low
 * Non-blocking implementation with cooldown period
 */

void checkWaterLevel() {
  // Static variables to track state between function calls
  static enum WaterState {
    CHECK_WATER,     // Normal water level checking
    REFILL_RUNNING,  // Water pump active
    COOLDOWN         // Waiting after refill
  } state = CHECK_WATER;

  static uint32_t stateStartTime = 0;
  static uint32_t lastDisplayUpdate = 0;
  // static const uint32_t REFILL_DURATION = 10000;   // 10 seconds for refill
  // static const uint32_t COOLDOWN_PERIOD = 300000;  // 5 minutes cooldown
  static const uint32_t DISPLAY_UPDATE_INTERVAL =
      1000;  // Update display every second

  // Get current time once
  uint32_t currentMillis = millis();

  // State machine for water level management
  switch (state) {
    case CHECK_WATER: {  // Added braces to create proper scope for local
                         // variables
      DEBUG_PRINTLN(F("Checking Water Level"));
      yield();  // Add yield for ESP8266 stability

      // Get distance from water surface
      float distanceCm = getDistance();

      // Allow ESP8266 to handle background tasks
      yield();

      // If readings failed completely, show error
      if (distanceCm <= 0 || distanceCm > 400) {  // Better range checking
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print(F("Sensor Error"));
        lcd.setCursor(0, 1);
        lcd.print(F("Check ultrasonic"));
        // Just display the message but continue normal operation
        return;
      }

      // Calculate water height (from bottom of tank)
      float waterHeight = DISTANCE_WATER_EMPTY - distanceCm;

      // Ensure readings are within expected range
      waterHeight =
          constrain(waterHeight, 0, DISTANCE_WATER_EMPTY - DISTANCE_WATER_FULL);

      // Calculate water percentage
      float waterPercentage =
          (waterHeight / (DISTANCE_WATER_EMPTY - DISTANCE_WATER_FULL)) * 100;
      waterPercentage = constrain(waterPercentage, 0, 100);

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
        // Water level critically low - activate relay
        DEBUG_PRINTLN(F("Water level critically low! Activating relay."));

        lcd.setCursor(0, 1);
        lcd.print(F("LOW! "));
        lcd.print(waterHeight, 1);
        lcd.print(F("cm ("));
        lcd.print(int(waterPercentage));
        lcd.print(F("%)"));
        yield();  // Add yield before activating relay

        // Activate the relay
        digitalWrite(WATER_PUMP_RELAY_PIN, HIGH);

        // Show activation message
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print(F("Water low!"));
        lcd.setCursor(0, 1);
        lcd.print(F("Refilling..."));

        // Change state and record start time
        state = REFILL_RUNNING;
        stateStartTime = currentMillis;
        lastDisplayUpdate = currentMillis;
      } else {
        // Water level OK
        lcd.setCursor(0, 1);
        lcd.print(F("OK "));
        lcd.print(waterHeight, 1);
        lcd.print(F("cm ("));
        lcd.print(int(waterPercentage));
        lcd.print(F("%)"));

        // Display water level bar graph
        progressBar(waterPercentage);
      }
      break;
    }

    case REFILL_RUNNING: {
      // Check if it's time to update the display
      if (currentMillis - lastDisplayUpdate >= 200) {
        lastDisplayUpdate = currentMillis;
        yield();  // Add yield before display updates

        // Update progress dots for animation
        uint32_t elapsedSecs = (currentMillis - stateStartTime) / 1000;
        lcd.setCursor(11, 1);
        lcd.print(F("   "));  // Clear previous dots with F() macro
        lcd.setCursor(11, 1);
        for (int i = 0; i < (elapsedSecs % 3) + 1; i++) {
          lcd.print(F("."));  // Add F() macro
        }

        // Show countdown if desired - ensure we don't go negative
        int remainingSecs = max(
            0,
            (int)((REFILL_DURATION - (currentMillis - stateStartTime)) / 1000 +
                  1));
        lcd.setCursor(15, 1);
        lcd.print(remainingSecs);
      }

      // Check if refill duration has elapsed
      if (currentMillis - stateStartTime >= REFILL_DURATION) {
        yield();  // Add yield before relay state change

        // Turn off relay
        digitalWrite(WATER_PUMP_RELAY_PIN, LOW);

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
      // Check if it's time to update the display (every second)
      if (currentMillis - lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL) {
        lastDisplayUpdate = currentMillis;
        yield();  // Add yield before display updates

        // Calculate and display remaining cooldown time
        uint32_t elapsedMs = currentMillis - stateStartTime;
        uint32_t remainingSecs = (COOLDOWN_PERIOD - elapsedMs) / 1000;

        // Display in MM:SS format
        lcd.setCursor(10, 1);
        lcd.print(F("     "));  // Clear previous time with F() macro
        lcd.setCursor(10, 1);

        // Handle potential overflow (if someone changed state timing variables)
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
        state = CHECK_WATER;
      }
      break;
    }
  }
}

/**
 * Real-time feeding function with weight monitoring
 * Checks for existing food, monitors dispensing, and ensures proper amount
 */
void feeding() {
  // Constants for fine tuning the feeding behavior
  const float PRE_CLOSE_THRESHOLD = 0.85;   // Pre-close at 85% of target
  const float FINAL_ACCURACY = 0.90;        // Accept 90% as "complete"
  const float EXCESSIVE_THRESHOLD = 1.25;   // 125% is too much
  const uint32_t MAX_FEEDING_TIME = 30000;  // 30 second timeout
  const uint32_t SETTLE_TIME = 1500;        // 1.5s for food to settle
  const float STABILITY_THRESHOLD = 0.3;    // 0.3g stability for readings
  const int RETRY_MAX = 2;                  // Max retries for underfeeding

  // Step 1: Initialize and check scale
  DEBUG_PRINTLN("Start feeding sequence...");

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Feeding time"));
  lcd.setCursor(0, 1);
  lcd.print(F("Checking scale"));
  nonBlockingWait(LCD_TIMEOUT);

  // Check if scale is responding
  uint32_t startCheck = millis();
  bool scaleReady = false;
  uint16_t timeout = 3000UL;

  while (millis() - startCheck < timeout) {
    if (scale.is_ready()) {
      scaleReady = true;
    }
    delay(50);
    yield();
  }

  if (!scaleReady) {
    lcd.setCursor(0, 1);
    lcd.print(F("Scale not ready!"));
    nonBlockingWait(LCD_TIMEOUT);
    return;
  }

  // Step 2: Check if there's already food on the scale
  lcd.clear();
  lcd.setCursor(0, 1);
  lcd.print(F("Checking bowl..."));
  nonBlockingWait(LCD_TIMEOUT);

  // Get multiple readings for accuracy with stability check
  float foodWeightReadings[5];
  bool stableReading = false;
  int readingAttempts = 0;

  // Try to get stable initial reading
  while (!stableReading && readingAttempts < 3) {
    readingAttempts++;

    // Take 5 readings
    for (int i = 0; i < 5; i++) {
      if (scale.is_ready()) {
        foodWeightReadings[i] = scale.get_units(2);
        lcd.setCursor(12, 1);
        lcd.print(F("   "));  // Clear previous dots
        lcd.setCursor(12, 1);
        for (int j = 0; j <= i; j++) {
          lcd.print(F("."));
        }
        delay(200);
        yield();
      } else {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print(F("Scale error!"));
        lcd.setCursor(0, 1);
        lcd.print(F("Retry reading..."));
        delay(500);
        break;
      }
    }

    // Check stability of readings
    float minWeight = foodWeightReadings[0];
    float maxWeight = foodWeightReadings[0];
    float totalWeight = foodWeightReadings[0];

    for (int i = 1; i < 5; i++) {
      totalWeight += foodWeightReadings[i];
      if (foodWeightReadings[i] < minWeight) minWeight = foodWeightReadings[i];
      if (foodWeightReadings[i] > maxWeight) maxWeight = foodWeightReadings[i];
    }

    // If max variation is less than threshold, readings are stable
    if ((maxWeight - minWeight) < STABILITY_THRESHOLD * 2) {
      stableReading = true;
    } else if (readingAttempts < 3) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print(F("Unstable reading"));
      lcd.setCursor(0, 1);
      lcd.print(F("Retrying..."));
      delay(1000);
    }
  }

  // Calculate average weight
  float currentFoodWeight = 0;
  if (stableReading) {
    for (int i = 0; i < 5; i++) {
      currentFoodWeight += foodWeightReadings[i];
    }
    currentFoodWeight /= 5;
  } else {
    // If still unstable, use the last reading but warn
    currentFoodWeight = foodWeightReadings[4];
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("Warning: Unstable"));
    lcd.setCursor(0, 1);
    lcd.print(F("scale readings"));
    delay(2000);
  }

  // Ensure non-negative weight
  if (currentFoodWeight < 0) currentFoodWeight = 0;

  if (currentFoodWeight >= FEED_THRESHOLD) {
    // Food already present! Show notification
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("Food detected!"));
    lcd.setCursor(0, 1);
    lcd.print(F("Weight: "));
    lcd.print(currentFoodWeight, 1);
    lcd.print(F("g"));
    delay(2000);

    // Show options
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("Food already >10g"));
    lcd.setCursor(0, 1);
    lcd.print(F("Btn:feed / "));
    lcd.print(F("Wait:20s"));

    // Wait for button press or timeout
    uint32_t notifyStartTime = millis();
    const uint32_t NOTIFY_TIMEOUT = 20000;  // 20 second timeout
    bool buttonPressed = false;

    // Loop until button press or timeout
    while (millis() - notifyStartTime < NOTIFY_TIMEOUT) {
      // Check for button press
      if (digitalRead(MANUAL_FEED_BUTTON_PIN) == LOW) {
        // Debounce
        delay(50);
        if (digitalRead(MANUAL_FEED_BUTTON_PIN) == LOW) {
          // Wait for button release
          while (digitalRead(MANUAL_FEED_BUTTON_PIN) == LOW) {
            delay(10);
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

    // If no button press, return to main screen
    if (!buttonPressed) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print(F("Feeding canceled"));
      lcd.setCursor(0, 1);
      lcd.print(F("Bowl already has:"));
      delay(1000);

      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print(F("Food weight:"));
      lcd.setCursor(0, 1);
      lcd.print(currentFoodWeight, 1);
      lcd.print(F("g in bowl"));
      delay(3000);
      return;
    }

    // User confirmed continuation
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("Continuing..."));
    lcd.setCursor(0, 1);
    lcd.print(F("Adding more food"));
    delay(1500);
  }

  // Step 4: Get initial weight (tare) before feeding
  float initialWeight = currentFoodWeight;

  // Step 5: Start feeding process
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Starting feed"));
  lcd.setCursor(0, 1);
  lcd.print(F("Opening hatch..."));

  // Open servo to begin dispensing
  hatchServo.write(SERVO_OPEN_ANGLE);
  delay(500);  // Give servo time to open

  // Step 6: Setup moving average for stable readings
  const int movingAvgSize = 3;
  float weightReadings[movingAvgSize] = {initialWeight, initialWeight,
                                         initialWeight};
  int readingIndex = 0;
  float currentWeight = initialWeight;

  // Step 7: Feed until target weight or timeout
  uint32_t startTime = millis();
  uint32_t lastDisplayUpdate = 0;
  uint32_t lastWeightRead = 0;

  float targetWeight = initialWeight + FEED_WEIGHT;
  bool targetReached = false;
  bool preCloseExecuted = false;
  float dispensedWeight = 0;
  int retryCount = 0;
  int stabilityCounter = 0;

  // Main feeding loop - until target reached or timeout
  while (!targetReached && (millis() - startTime < MAX_FEEDING_TIME)) {
    uint32_t now = millis();

    // Read weight at regular intervals
    if (now - lastWeightRead >= 100) {
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
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print(F("Scale error!"));
          lcd.setCursor(0, 1);
          lcd.print(F("Closing hatch"));
          hatchServo.write(SERVO_CLOSE_ANGLE);
          nonBlockingWait(LCD_TIMEOUT);
          return;
        }
      }

      // Update moving average
      weightReadings[readingIndex] = scale.get_units(1);  // Fast read
      readingIndex = (readingIndex + 1) % movingAvgSize;

      // Calculate current weight
      float sumWeight = 0;
      for (int i = 0; i < movingAvgSize; i++) {
        sumWeight += weightReadings[i];
      }
      currentWeight = sumWeight / movingAvgSize;

      // Calculate dispensed amount
      dispensedWeight = currentWeight - initialWeight;

      // Ensure non-negative dispensed amount
      if (dispensedWeight < 0) dispensedWeight = 0;

      // IMPROVED PRE-CLOSE LOGIC: Close early to account for falling food
      if (!preCloseExecuted &&
          dispensedWeight >= FEED_WEIGHT * PRE_CLOSE_THRESHOLD) {
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
        delay(SETTLE_TIME);
        yield();

        // Re-measure after settling
        float settledWeight = 0;
        int validReadings = 0;

        for (int j = 0; j < 5; j++) {
          if (scale.is_ready()) {
            settledWeight += scale.get_units(2);
            validReadings++;
            yield();
            delay(100);
          }
        }

        if (validReadings > 0) {
          settledWeight /= validReadings;
          dispensedWeight = settledWeight - initialWeight;
          if (dispensedWeight < 0) dispensedWeight = 0;

          // Update current weight and readings array
          currentWeight = settledWeight;
          for (int i = 0; i < movingAvgSize; i++) {
            weightReadings[i] = currentWeight;
          }

          // Check if we need to open again (underfeeding)
          if (dispensedWeight < FEED_WEIGHT * FINAL_ACCURACY &&
              retryCount < RETRY_MAX) {
            retryCount++;
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print(F("Need more food"));
            lcd.setCursor(0, 1);
            lcd.print(F("Retry #"));
            lcd.print(retryCount);

            hatchServo.write(SERVO_OPEN_ANGLE);
            delay(300);                // Brief open
            preCloseExecuted = false;  // Reset to try again
          }
          // Handle successful dispense
          else if (dispensedWeight >= FEED_WEIGHT * FINAL_ACCURACY) {
            targetReached = true;
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print(F("Target reached!"));
            lcd.setCursor(0, 1);
            lcd.print(F("Dispensed: "));
            lcd.print(dispensedWeight, 1);
            lcd.print(F("g"));
            delay(1000);
          }
          // Handle case where we can't reach target despite retries
          else if (retryCount >= RETRY_MAX) {
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print(F("Warning: Only"));
            lcd.setCursor(0, 1);
            lcd.print(dispensedWeight, 1);
            lcd.print(F("g dispensed"));
            delay(1500);
            targetReached = true;  // End the process after max retries
          }
        }
      }

      // Standard target check for when pre-close isn't active
      if (!preCloseExecuted) {
        // Check if target already reached (possible with fast-falling food)
        if (dispensedWeight >= FEED_WEIGHT) {
          hatchServo.write(SERVO_CLOSE_ANGLE);
          preCloseExecuted = true;

          // Confirm with stable readings
          if (stabilityCounter < 2) {
            stabilityCounter++;
          } else {
            targetReached = true;
          }
        }

        // Emergency stop if way too much food dispensed
        if (dispensedWeight >= FEED_WEIGHT * EXCESSIVE_THRESHOLD) {
          hatchServo.write(SERVO_CLOSE_ANGLE);
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print(F("Warning!"));
          lcd.setCursor(0, 1);
          lcd.print(F("Excess food!"));
          delay(1000);
          targetReached = true;
        }
      }
    }

    // Update display periodically
    if (now - lastDisplayUpdate >= 500) {
      lastDisplayUpdate = now;

      // Calculate progress percentage
      float progressPercent = (dispensedWeight / FEED_WEIGHT) * 100.0;
      progressPercent = constrain(progressPercent, 0, 100);

      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print(F("Feeding: "));
      lcd.print((int)progressPercent);
      lcd.print(F("%"));

      // Show additional info based on progress
      if (progressPercent < 80) {
        lcd.setCursor(0, 1);
        lcd.print(F("Target: "));
        lcd.print(FEED_WEIGHT, 0);
        lcd.print(F("g"));
      } else {
        // Show progress bar when getting close
        lcd.setCursor(0, 1);
        int barWidth = (progressPercent * 16) / 100;
        for (int i = 0; i < 16; i++) {
          if (i < barWidth) {
            lcd.write(255);  // Full block character
          } else {
            lcd.write(' ');
          }
        }
      }
    }

    // Allow ESP8266 background processing
    yield();
    delay(10);
  }

  // Step 8: Ensure servo is closed
  hatchServo.write(SERVO_CLOSE_ANGLE);

  // Show closing message
  lcd.clear();
  lcd.setCursor(0, 0);
  if (millis() - startTime >= MAX_FEEDING_TIME) {
    lcd.print(F("Timeout reached!"));
    lcd.setCursor(0, 1);
    lcd.print(F("Closing hatch"));
  } else {
    lcd.print(F("Closing hatch"));
    lcd.setCursor(0, 1);
    lcd.print(F("Please wait..."));
  }
  delay(1000);  // Give servo time to close

  // Step 9: Wait for food to settle and take final measurement
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Measuring final"));
  lcd.setCursor(0, 1);
  lcd.print(F("weight..."));

  // Wait for food to fully settle
  delay(2000);

  // Get final stable weight
  float finalWeight = 0;
  int validFinalReadings = 0;

  for (int i = 0; i < 5; i++) {
    if (scale.is_ready()) {
      finalWeight += scale.get_units(5);
      validFinalReadings++;
      lcd.setCursor(15, 1);
      lcd.print(i + 1);
      yield();
      delay(200);
    }
  }

  // Calculate final dispensed weight
  if (validFinalReadings > 0) {
    finalWeight /= validFinalReadings;
    dispensedWeight = finalWeight - initialWeight;
    if (dispensedWeight < 0) dispensedWeight = 0;
  }

  // Step 10: Show results
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Feeding complete"));
  lcd.setCursor(0, 1);
  lcd.print(F("Added: "));
  lcd.print(dispensedWeight, 1);
  lcd.print(F("g"));

  // Calculate percentage of target
  int feedPct = (dispensedWeight / FEED_WEIGHT) * 100;
  feedPct = constrain(feedPct, 0, 999);

  delay(2000);

  // Show accuracy info
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Accuracy: "));
  lcd.print(feedPct);
  lcd.print(F("%"));
  lcd.setCursor(0, 1);

  // Show quality assessment
  if (feedPct >= 95 && feedPct <= 105) {
    lcd.print(F("Perfect portion!"));
  } else if (feedPct < 80) {
    lcd.print(F("Underfed - retry?"));
  } else if (feedPct > 120) {
    lcd.print(F("Overfed - adjust"));
  } else {
    lcd.print(F("Good enough"));
  }
  nonBlockingWait(3000);

  // Show total food in bowl
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Bowl now contains"));
  lcd.setCursor(0, 1);
  lcd.print(F("Total: "));
  lcd.print(finalWeight, 1);
  lcd.print(F("g"));
  nonBlockingWait(3000);
}

// Non-blocking wait that checks for minimum display time
void nonBlockingWait(uint32_t waitTime, uint32_t startDisplayTime) {
  uint32_t startWait = millis();
  uint32_t actualWaitTime = waitTime;

  // If we need to ensure a minimum display time
  if (LCD_TIMEOUT > 0 && startDisplayTime > 0) {
    uint32_t elapsedDisplayTime = millis() - startDisplayTime;

    // If we've already waited longer than minimum, don't wait more
    if (elapsedDisplayTime >= LCD_TIMEOUT) {
      return;
    }

    // Calculate how much more time we need to wait
    uint32_t remainingDisplayTime = LCD_TIMEOUT - elapsedDisplayTime;

    // Use either the passed wait time or remaining display time, whichever is
    // longer
    actualWaitTime = max(waitTime, remainingDisplayTime);
  }

  // Do the actual waiting
  while (millis() - startWait < actualWaitTime) {
    yield();    // Keep WiFi working
    delay(10);  // Don't hog the CPU
  }
}

/**
 * High-performance LCD line clearing function
 *
 * @param col Starting column position (0-based)
 * @param row Row position (0-based)
 * @param length Number of characters to erase (default: until end of line)
 */
void clearLineLCD(uint8_t col, uint8_t row, uint8_t length) {
  static char buffer[17];  // Buffer to hold characters (+1 for null terminator)

  // If length is 0 or exceeds available space, fill to end of line
  if (length == 0 || col + length > LCD_Y) {
    length = LCD_Y - col;
  }

  // Skip if nothing to clear
  if (length <= 0) return;

  // Fill buffer with the specified character (usually space)
  memset(buffer, ' ', length);
  buffer[length] = '\0';  // Null-terminate

  // Position cursor once and print the buffer
  lcd.setCursor(col, row);
  lcd.print(buffer);
}

void progressBar(float percentage) {
  // Define the width of the progress bar in characters
  uint8_t partialBlock[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

  // Constrain percentage between 0-100
  float percent = constrain(percentage, 0, 100);

  // Calculate the percentage to display on top right
  char percentStr[5];
  snprintf(percentStr, sizeof(percentStr), "%3d%%",
           int(percent));  // Fixed: Changed from sniprintf to snprintf

  // Display the percentage on the right side of row 0
  lcd.setCursor(LCD_X - 4, 0);  // Fixed: Explicit position calculation
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
    completeBlocks++;    // Account for the partial block in total count
  }

  // Fill the rest with empty space
  for (byte i = completeBlocks; i < LCD_X; i++) {
    lcd.print(" ");
  }
}

/**
 * Scrolling text function with animation count limit
 */
void scrollTextContinuous(const char* message, uint8_t col, uint8_t row,
                          uint8_t limitAnimation,  // 0 = infinite
                          uint16_t scrollSpeed, uint16_t pauseBeforeMs,
                          uint16_t pauseAfterMs) {
  // Guard against null message
  if (message == NULL) return;

  static uint32_t previousMillis = 0;
  static int16_t position = 0;
  static const char* currentMessage = NULL;
  static uint32_t stateStartTime = 0;
  static uint8_t currentRow = 0;
  static uint8_t currentCol = 0;
  static uint8_t animationCount = 0;

  static enum ScrollState {
    PAUSE_BEFORE,
    SCROLLING,
    PAUSE_AFTER,
    COMPLETED
  } state = PAUSE_BEFORE;

  uint8_t messageLength = strlen(message);
  uint8_t availableWidth = LCD_Y - col;

  // Check if this is a new message or position
  if (currentMessage != message || currentRow != row || currentCol != col) {
    // Reset everything for new configuration
    position = 0;
    currentMessage = message;
    currentRow = row;
    currentCol = col;
    state = PAUSE_BEFORE;
    stateStartTime = millis();
    animationCount = 0;

    // Display initial message
    clearLineLCD(col, row, availableWidth);
    lcd.setCursor(col, row);
    lcd.print(message);
  }

  uint32_t currentMillis = millis();

  // Skip if not needed
  if (messageLength <= availableWidth || state == COMPLETED ||
      scrollSpeed == 0) {
    return;
  }

  // Allow system tasks to run
  yield();

  // State machine for scrolling
  switch (state) {
    case PAUSE_BEFORE:
      lcd.setCursor(col, row);
      lcd.print(message);

      if (currentMillis - stateStartTime >= pauseBeforeMs) {
        state = SCROLLING;
        position = 0;
        previousMillis = currentMillis;
      }
      break;

    case SCROLLING:
      if (currentMillis - previousMillis >= scrollSpeed) {
        previousMillis = currentMillis;
        position++;

        clearLineLCD(col, row, availableWidth);
        lcd.setCursor(col, row);

        // Show visible portion
        for (uint8_t i = 0; i < availableWidth; i++) {
          int16_t charPosition = i + position;

          if (charPosition >= 0 && charPosition < messageLength) {
            lcd.print(message[charPosition]);
          } else {
            lcd.print(" ");
          }
        }

        // Check if we've reached the end
        if (position >= messageLength) {
          state = PAUSE_AFTER;
          stateStartTime = currentMillis;
        }
      }
      break;

    case PAUSE_AFTER:
      if (currentMillis - stateStartTime >= pauseAfterMs) {
        animationCount++;

        if (limitAnimation > 0 && animationCount >= limitAnimation) {
          state = COMPLETED;
          clearLineLCD(col, row, availableWidth);
          lcd.setCursor(col, row);
          lcd.print(message);
        } else {
          state = PAUSE_BEFORE;
          stateStartTime = currentMillis;
          position = 0;
          clearLineLCD(col, row, availableWidth);
          lcd.setCursor(col, row);
          lcd.print(message);
        }
      }
      break;

    case COMPLETED:
      // Do nothing, animation complete
      break;
  }
}