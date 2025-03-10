#ifndef WEB_HELPERS_H
#define WEB_HELPERS_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <WebSocketsClient.h>

#include "../config.h"

// Forward declaration of externally defined objects
extern NTPClient timeClient;
extern StaticJsonDocument<512> jsonDoc;  // Reference the document from main.cpp
extern void handleWebSocketCommand(
    const char* command,
    const JsonDocument& doc);  // Changed to const reference

// Global variables for WebSocket communication - defined in main.cpp
extern WebSocketsClient webSocket;
extern String clientId;
extern bool webConnected;

// Static variables for this file only
static uint32_t lastReconnectAttempt = 0;
static uint32_t lastHeartbeat = 0;
static uint32_t lastScheduleCheck = 0;

// Simple min function to replace std::min
template <typename T>
T minVal(T a, T b) {
  return (a < b) ? a : b;
}

// Add a global variable to track the next scheduled feeding
uint32_t nextScheduledFeeding = 0;  // Unix timestamp of next feeding
bool hasActiveSchedule = false;

// Function declarations
bool webInit(const char* url, const char* id);
bool webConnect();
void webUpdate();
bool sendMessage(const char* eventType, JsonVariant data);
void processWebSocketMessage(uint8_t* payload, size_t length);
bool sendFeedingComplete(bool isScheduled, const char* details, float foodLevel,
                         float waterLevel);
void checkSchedules();
bool isWebConnected();
uint32_t getNextScheduledFeeding();
bool hasSchedules();

// Declare webSocketEvent as an extern function that's implemented in main.cpp
extern void webSocketEvent(WStype_t type, uint8_t* payload, size_t length);

/**
 * Initialize WebSocket connection with server
 * @param url Server URL (from config.h: WEB_SERVER_URL)
 * @param id Client identifier (from config.h: WEB_CLIENT_ID)
 * @return true if initialization was successful
 */
bool webInit(const char* url, const char* id) {
  clientId = id;

  DEBUG_PRINTLN(F("Initializing WebSocket client..."));
  DEBUG_PRINT(F("Server URL: "));
  DEBUG_PRINTLN(url);
  DEBUG_PRINT(F("Client ID: "));
  DEBUG_PRINTLN(id);

  // Initialize WebSocket client
  webSocket.begin(url, WEB_SERVER_PORT, "/");
  webSocket.setReconnectInterval(WEB_RECONNECT_INTERVAL);
  webSocket.onEvent(webSocketEvent);

  // Try to connect right away
  return webConnect();
}

/**
 * Attempt to connect to the WebSocket server
 * @return true if connection process started
 */
bool webConnect() {
  // First check if WiFi is connected
  if (WiFi.status() !=
      WL_CONNECTED) {  // Fixed: Using WiFi.status() != WL_CONNECTED instead of
                       // !WiFi.isConnected()
    DEBUG_PRINTLN(F("Cannot connect to server: WiFi not connected"));
    webConnected = false;
    return false;
  }

  DEBUG_PRINT(F("Connecting to WebSocket server at "));
  DEBUG_PRINTLN(WEB_SERVER_URL);

  // Remember when we tried to connect
  lastReconnectAttempt = millis();
  lastHeartbeat = millis();

  // Connection attempt in progress
  return true;
}

/**
 * Process WebSocket messages and maintain connection
 * Call this function regularly in loop()
 */
void webUpdate() {
  // Loop to process WebSocket events
  webSocket.loop();

  // Send heartbeat every 25 seconds to keep connection alive
  if (webConnected && millis() - lastHeartbeat > 25000) {
    // Send ping using WebSocket ping frame
    webSocket.sendPing();
    lastHeartbeat = millis();
    DEBUG_PRINTLN(F("Sent WebSocket ping"));
  }

  // Check feeding schedules periodically
  if (webConnected && millis() - lastScheduleCheck > 60000 &&
      timeClient.isTimeSet()) {
    lastScheduleCheck = millis();
    checkSchedules();
  }
}

/**
 * Send message using WebSocket
 * @param eventType Type of event (like "get-settings", "feed-now", etc.)
 * @param data JSON object to send as data
 * @return true if sent successfully
 */
bool sendMessage(const char* eventType, JsonVariant data) {
  if (!webConnected) return false;

  // Create complete message with event type
  JsonObject root = jsonDoc.to<JsonObject>();
  root["eventType"] = eventType;
  root["clientId"] = clientId;

  if (!data.isNull()) {
    // If data is provided, add it to the message
    if (data.is<JsonObject>()) {
      JsonObject dataObj = data.as<JsonObject>();
      for (JsonPair kv : dataObj) {
        root[kv.key()] = kv.value();
      }
    }
  }

  String output;
  serializeJson(jsonDoc, output);

  DEBUG_PRINT(F("Sending: "));
  DEBUG_PRINTLN(output);

  webSocket.sendTXT(output);
  jsonDoc.clear();
  return true;
}

/**
 * Process WebSocket messages from server
 */
void processWebSocketMessage(uint8_t* payload, size_t length) {
  // Parse message as JSON
  DeserializationError error = deserializeJson(jsonDoc, payload, length);

  if (error) {
    DEBUG_PRINT(F("JSON parsing failed: "));
    DEBUG_PRINTLN(error.c_str());
    return;
  }

  // Get event type from message
  const char* eventType = jsonDoc["eventType"];
  if (!eventType) {
    // For backward compatibility, try original Socket.IO format
    if (jsonDoc[0].is<const char*>()) {
      eventType = jsonDoc[0];

      // Extract data from Socket.IO format if present
      if (jsonDoc[1].is<JsonObject>()) {
        jsonDoc.set(jsonDoc[1]);
      }
    } else {
      DEBUG_PRINTLN(F("Message missing eventType"));
      return;
    }
  }

  DEBUG_PRINT(F("Received event: "));
  DEBUG_PRINTLN(eventType);

  // Process based on event type
  if (strcmp(eventType, "settings") == 0) {
    if (jsonDoc.containsKey("data")) {
      processSettings(jsonDoc["data"]);
    } else {
      processSettings(jsonDoc);
    }
  } else if (strcmp(eventType, "schedules") == 0) {
    if (jsonDoc.containsKey("data")) {
      processSchedules(jsonDoc["data"]);
    } else {
      processSchedules(jsonDoc);
    }
  } else if (strcmp(eventType, "feeding-data") == 0) {
    if (jsonDoc.containsKey("data")) {
      processFeedingData(jsonDoc["data"]);
    } else {
      processFeedingData(jsonDoc);
    }
  } else if (strcmp(eventType, "system-status") == 0) {
    if (jsonDoc.containsKey("data")) {
      processSystemStatus(jsonDoc["data"]);
    } else {
      processSystemStatus(jsonDoc);
    }
  } else if (strcmp(eventType, "command") == 0) {
    // Process command requests
    const char* command = NULL;

    // Check if command is directly in the eventType field or in a separate
    // command field
    if (jsonDoc.containsKey("command")) {
      command = jsonDoc["command"];
    } else if (jsonDoc.containsKey("data") &&
               jsonDoc["data"].containsKey("command")) {
      command = jsonDoc["data"]["command"];
    }

    if (command) {
      DEBUG_PRINT(F("Processing command: "));
      DEBUG_PRINTLN(command);

      // Instead of creating a reference, directly pass the appropriate document
      if (jsonDoc.containsKey("data")) {
        handleWebSocketCommand(command, jsonDoc["data"]);
      } else {
        handleWebSocketCommand(command, jsonDoc);
      }
    }
  }
}

/**
 * Register the device with the server
 */
void registerDevice() {
  jsonDoc.clear();
  jsonDoc["deviceType"] = "feeder-device";
  jsonDoc["version"] = "1.0";
  jsonDoc["capabilities"] = "feeding,water";

  DEBUG_PRINTLN(F("Registering device with server..."));
  sendMessage("register", jsonDoc);
}

/**
 * Request initial data from server
 */
void requestInitialData() {
  // Request settings
  jsonDoc.clear();
  DEBUG_PRINTLN(F("Requesting settings..."));
  sendMessage("getSettings", jsonDoc);

  // Request schedules
  jsonDoc.clear();
  DEBUG_PRINTLN(F("Requesting schedules..."));
  sendMessage("getSchedules", jsonDoc);

  // Request system status
  jsonDoc.clear();
  DEBUG_PRINTLN(F("Requesting system status..."));
  sendMessage("getStatus", jsonDoc);
}

/**
 * Process settings received from server
 */
void processSettings(JsonDocument& doc) {
  DEBUG_PRINTLN(F("Received settings from server"));

  JsonObject data = doc["data"];
  if (!data) return;

  // Extract portion size
  if (data.containsKey("portionSize")) {
    int portionSize = data["portionSize"];
    DEBUG_PRINT(F("Portion size: "));
    DEBUG_PRINTLN(portionSize);
  }

  // Extract water amount
  if (data.containsKey("waterAmount")) {
    int waterAmount = data["waterAmount"];
    DEBUG_PRINT(F("Water amount: "));
    DEBUG_PRINTLN(waterAmount);
  }
}

/**
 * Process schedules received from server
 */
void processSchedules(JsonDocument& doc) {
  JsonArray schedules = doc["data"];
  if (!schedules) {
    DEBUG_PRINTLN(F("No schedules data in message"));
    return;
  }

  DEBUG_PRINT(F("Received "));
  DEBUG_PRINT(schedules.size());
  DEBUG_PRINTLN(F(" schedule(s)"));

  // Get current time
  if (!timeClient.isTimeSet()) {
    DEBUG_PRINTLN(F("Cannot process schedules - time not set"));
    return;
  }

  uint32_t currentEpoch = timeClient.getEpochTime();
  int currentHour = timeClient.getHours();
  int currentMinute = timeClient.getMinutes();
  int currentTimeMinutes = currentHour * 60 + currentMinute;

  // Reset next scheduled feeding
  nextScheduledFeeding = 0;
  hasActiveSchedule = false;

  int activeCount = 0;

  // Temporary array to store all enabled schedules
  const int MAX_SCHEDULES = 10;
  struct ScheduleTime {
    int minutes;       // Minutes since midnight
    const char* time;  // Original time string
  };
  ScheduleTime enabledSchedules[MAX_SCHEDULES];
  int enabledCount = 0;

  // First loop: count active schedules and store their times
  for (JsonObject schedule : schedules) {
    if (schedule["enabled"].as<bool>()) {
      activeCount++;
      String timeStr = schedule["time"].as<String>();

      // Parse HH:MM format
      int hour = timeStr.substring(0, 2).toInt();
      int minute = timeStr.substring(3, 5).toInt();
      int scheduleMinutes = hour * 60 + minute;

      DEBUG_PRINT(F("Schedule #"));
      DEBUG_PRINT(activeCount);
      DEBUG_PRINT(F(": "));
      DEBUG_PRINTLN(timeStr);

      if (enabledCount < MAX_SCHEDULES) {
        enabledSchedules[enabledCount].minutes = scheduleMinutes;
        enabledSchedules[enabledCount].time = timeStr.c_str();
        enabledCount++;
      }
    }
  }

  // If we have active schedules, find the next one
  if (enabledCount > 0) {
    hasActiveSchedule = true;

    // Find the next schedule (first one later today)
    int nextScheduleIndex = -1;
    int minTimeDiff = 24 * 60 + 1;  // More than a day's minutes

    for (int i = 0; i < enabledCount; i++) {
      int scheduleMinutes = enabledSchedules[i].minutes;

      // Calculate time difference in minutes, handling wrap-around at midnight
      int timeDiff;
      if (scheduleMinutes > currentTimeMinutes) {
        // Schedule is later today
        timeDiff = scheduleMinutes - currentTimeMinutes;
      } else {
        // Schedule is tomorrow
        timeDiff = scheduleMinutes + (24 * 60 - currentTimeMinutes);
      }

      if (timeDiff < minTimeDiff) {
        minTimeDiff = timeDiff;
        nextScheduleIndex = i;
      }
    }

    if (nextScheduleIndex >= 0) {
      // Calculate next feeding timestamp
      struct tm timeinfo;
      time_t now = currentEpoch;
      gmtime_r(&now, &timeinfo);

      int nextHour = enabledSchedules[nextScheduleIndex].minutes / 60;
      int nextMinute = enabledSchedules[nextScheduleIndex].minutes % 60;

      // If next schedule is earlier today, it must be for tomorrow
      if (enabledSchedules[nextScheduleIndex].minutes <= currentTimeMinutes) {
        // Add a day
        timeinfo.tm_mday++;
      }

      timeinfo.tm_hour = nextHour;
      timeinfo.tm_min = nextMinute;
      timeinfo.tm_sec = 0;

      // Convert to timestamp and store
      nextScheduledFeeding = mktime(&timeinfo);

      DEBUG_PRINT(F("Next feeding scheduled at: "));
      DEBUG_PRINTLN(enabledSchedules[nextScheduleIndex].time);
      DEBUG_PRINT(F("Countdown: "));
      DEBUG_PRINT(minTimeDiff / 60);
      DEBUG_PRINT(F("h "));
      DEBUG_PRINT(minTimeDiff % 60);
      DEBUG_PRINTLN(F("m"));
    }
  } else {
    DEBUG_PRINTLN(F("No active schedules found"));
  }
}

// Getter for next scheduled feeding time
uint32_t getNextScheduledFeeding() { return nextScheduledFeeding; }

// Getter to check if we have any active schedules
bool hasSchedules() { return hasActiveSchedule; }

/**
 * Process feeding data received from server
 */
void processFeedingData(JsonDocument& doc) {
  JsonObject data = doc["data"];
  if (!data) return;

  // Extract food level
  if (data.containsKey("foodLevel")) {
    int foodLevel = data["foodLevel"];
    DEBUG_PRINT(F("Food level from server: "));
    DEBUG_PRINTLN(foodLevel);
  }

  // Extract water level
  if (data.containsKey("waterLevel")) {
    int waterLevel = data["waterLevel"];
    DEBUG_PRINT(F("Water level from server: "));
    DEBUG_PRINTLN(waterLevel);
  }
}

/**
 * Process system status received from server
 */
void processSystemStatus(JsonDocument& doc) {
  JsonObject data = doc["data"];
  if (!data) return;

  // Extract feeding status
  if (data.containsKey("feeding")) {
    const char* status = data["feeding"];
    DEBUG_PRINT(F("Feeding status: "));
    DEBUG_PRINTLN(status);
  }

  // Extract watering status
  if (data.containsKey("watering")) {
    const char* status = data["watering"];
    DEBUG_PRINT(F("Watering status: "));
    DEBUG_PRINTLN(status);
  }
}

/**
 * Process command received from server
 */
void processCommand(JsonDocument& doc) {
  const char* command = doc["command"];
  if (!command) return;

  DEBUG_PRINT(F("Received command: "));
  DEBUG_PRINTLN(command);

  if (strcmp(command, "feed") == 0) {
    DEBUG_PRINTLN(F("Remote feeding command received"));

    // Get portion size if provided
    int portionSize = FEED_WEIGHT;
    if (doc.containsKey("portionSize")) {
      portionSize = doc["portionSize"];
    }

    // Send acknowledgement that we received the command
    jsonDoc.clear();
    jsonDoc["status"] = "executing";
    jsonDoc["command"] = "feed";
    sendMessage("commandResponse", jsonDoc);

    // External function to perform the feeding (defined in new_main.cpp)
    handleWebSocketCommand(command, doc);
  } else if (strcmp(command, "water") == 0) {
    // ...existing water command handling...
  }
}

/**
 * Send feeding complete notification to server
 * @param isScheduled Whether this was a scheduled feeding
 * @param details Additional details about the feeding
 * @param foodLevel Current food level percentage
 * @param waterLevel Current water level percentage
 */
bool sendFeedingComplete(bool isScheduled, const char* details, float foodLevel,
                         float waterLevel) {
  if (!webConnected) return false;

  jsonDoc.clear();
  jsonDoc["isScheduled"] = isScheduled;
  jsonDoc["details"] = details;
  jsonDoc["foodLevel"] = foodLevel;
  jsonDoc["waterLevel"] = waterLevel;

  return sendMessage("feeding-complete", jsonDoc);
}

/**
 * Check for scheduled feedings and calculate next feeding time
 */
void checkSchedules() {
  if (!timeClient.isTimeSet()) {
    DEBUG_PRINTLN(F("Cannot check schedules - time not set"));
    return;
  }

  // Get current time
  int currentHour = timeClient.getHours();
  int currentMinute = timeClient.getMinutes();
  uint32_t currentEpoch = timeClient.getEpochTime();

  // Format as HH:MM for comparison
  char currentTimeStr[6];
  snprintf(currentTimeStr, sizeof(currentTimeStr), "%02d:%02d", currentHour,
           currentMinute);

  DEBUG_PRINT(F("Checking schedules at "));
  DEBUG_PRINTLN(currentTimeStr);

  // Request current schedules from server
  jsonDoc.clear();
  sendMessage("getSchedules", jsonDoc);

  // Reset next scheduled feeding
  nextScheduledFeeding = 0;
  hasActiveSchedule = false;

  // Calculate next feeding time based on received schedules
  // This will be updated when schedules are received from server
}

/**
 * Send feed now command to server
 * @param portionSize Size of portion to feed (0 for default)
 * @return true if sent successfully
 */
bool sendFeedNow(int portionSize = 0) {
  if (!webConnected) return false;

  jsonDoc.clear();
  if (portionSize > 0) {
    jsonDoc["portionSize"] = portionSize;
  }

  return sendMessage("feed-now", jsonDoc);
}

/**
 * Send water now command to server
 * @param waterAmount Amount of water to dispense (0 for default)
 * @return true if sent successfully
 */
bool sendWaterNow(int waterAmount = 0) {
  if (!webConnected) return false;

  jsonDoc.clear();
  if (waterAmount > 0) {
    jsonDoc["waterAmount"] = waterAmount;
  }

  return sendMessage("water-now", jsonDoc);
}

/**
 * Send a log event to the server
 * @param eventType Type of event
 * @param details Additional details
 * @return true if sent successfully
 */
bool sendLogEvent(const char* eventType, const char* details) {
  if (!webConnected) return false;

  jsonDoc.clear();
  jsonDoc["type"] = eventType;
  jsonDoc["details"] = details;

  return sendMessage("log-event", jsonDoc);
}

/**
 * Update feeding status on server
 * @param status Current status (e.g., "active", "complete", "ready")
 * @param foodLevel Food level percentage (0-100)
 * @param foodWeight Amount of food dispensed in grams
 * @return true if successful
 */
bool updateFeedingStatus(const char* status, float foodLevel,
                         float foodWeight = 0.0f) {
  if (!webConnected) return false;

  // Update feeding data
  jsonDoc.clear();
  jsonDoc["foodLevel"] = foodLevel;

  if (foodWeight > 0) {
    jsonDoc["lastFeedWeight"] = foodWeight;
  }

  sendMessage("updateFeedingData", jsonDoc);

  // Update system status
  jsonDoc.clear();
  jsonDoc["feeding"] = status;

  return sendMessage("updateSystemStatus", jsonDoc);
}

/**
 * Update water status on server
 * @param status Current status (e.g., "ok", "low", "refilling", "ready")
 * @param waterLevel Water level percentage (0-100)
 * @return true if successful
 */
bool updateWaterStatus(const char* status, float waterLevel) {
  if (!webConnected) return false;

  // Update water level data
  jsonDoc.clear();
  jsonDoc["waterLevel"] = waterLevel;

  sendMessage("updateFeedingData", jsonDoc);

  // Update system status
  jsonDoc.clear();
  jsonDoc["watering"] = status;

  return sendMessage("updateSystemStatus", jsonDoc);
}

/**
 * Check if connected to web server
 * @return true if connected
 */
bool isWebConnected() {
  return webConnected &&
         (WiFi.status() ==
          WL_CONNECTED);  // Fixed: Using WiFi.status() == WL_CONNECTED
}

#endif  // WEB_HELPERS_H
