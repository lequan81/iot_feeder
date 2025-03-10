#ifndef WEBSOCKET_DEFS_H
#define WEBSOCKET_DEFS_H

#include <ArduinoJson.h>

// Create a global JSON document for WebSocket communication
StaticJsonDocument<512> jsonDoc;

// Forward declaration for the WebSocket command handler
void handleWebSocketCommand(const char* command, JsonDocument& doc);

// Forward declaration for WebSocket message sending
bool sendMessage(const char* eventType, JsonVariant data);

#endif  // WEBSOCKET_DEFS_H
