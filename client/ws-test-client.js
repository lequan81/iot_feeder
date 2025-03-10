/**
 * WebSocket Test Client for IoT Feeder
 *
 * This file provides functions to test the WebSocket server communication
 * with both the ESP8266 device and web clients.
 */

// WebSocket connection
let socket = null;
let clientType = "web-client"; // Options: 'web-client', 'esp8266'
let deviceId = `test-${Date.now().toString(36)}`;
let feedbackLog = [];

// Connection state
let isConnected = false;
let settings = {};
let schedules = [];
let feedingData = {};
let systemStatus = {};

/**
 * Connect to WebSocket server
 */
function connect(serverUrl = "ws://localhost:3001") {
  return new Promise((resolve, reject) => {
    try {
      console.log(`Connecting to ${serverUrl}...`);
      socket = new WebSocket(serverUrl);

      // Set up event handlers
      socket.onopen = () => {
        console.log("WebSocket connected!");
        isConnected = true;

        // Register client type
        if (clientType === "esp8266") {
          registerAsESP8266();
        } else {
          registerAsWebClient();
        }

        resolve(socket);
      };

      socket.onclose = (event) => {
        console.log(`WebSocket disconnected: ${event.code} - ${event.reason}`);
        isConnected = false;
      };

      socket.onerror = (error) => {
        console.error("WebSocket error:", error);
        reject(error);
      };

      // Message handler
      socket.onmessage = (event) => {
        try {
          const message = JSON.parse(event.data);
          handleMessage(message);
        } catch (err) {
          console.error("Error parsing message:", err, event.data);
        }
      };
    } catch (err) {
      console.error("Connection error:", err);
      reject(err);
    }
  });
}

/**
 * Register as ESP8266 device
 */
function registerAsESP8266() {
  send({
    eventType: "register",
    deviceType: "feeder-device",
    version: "1.0",
    clientId: "esp8266-feeder",
  });

  // Request initial data
  send({ eventType: "get-settings" });
  send({ eventType: "get-schedules" });
}

/**
 * Register as web client
 */
function registerAsWebClient() {
  send({
    eventType: "register",
    deviceType: "web-dashboard",
    version: "1.0",
    clientId: deviceId,
  });

  // Request initial data
  send({ eventType: "get-settings" });
  send({ eventType: "get-schedules" });
}

/**
 * Send message to server
 */
function send(data) {
  if (!isConnected) {
    console.warn("Cannot send message: WebSocket not connected");
    return false;
  }

  const jsonData = JSON.stringify(data);
  socket.send(jsonData);
  console.log(">>> Sent:", data);
  return true;
}

/**
 * Handle incoming message
 */
function handleMessage(message) {
  // Log message
  console.log("<<< Received:", message);

  // Store in feedback log
  feedbackLog.push({
    timestamp: new Date(),
    message,
  });

  // Process by event type
  const { eventType, data } = message;

  switch (eventType) {
    case "settings":
      settings = data;
      console.log("Updated settings:", settings);
      break;

    case "schedules":
      schedules = data;
      console.log("Updated schedules:", schedules);
      break;

    case "feeding-data":
      feedingData = data;
      console.log("Updated feeding data:", feedingData);
      break;

    case "system-status":
      systemStatus = data;
      console.log("Updated system status:", systemStatus);
      break;

    case "command":
      // Handle commands for ESP8266 simulation
      if (clientType === "esp8266") {
        handleCommand(message);
      }
      break;

    case "command-sent":
    case "command-simulated":
    case "feed-ack":
      console.log(`Command acknowledged: ${eventType}`);
      break;
  }
}

/**
 * Handle command for ESP8266 simulation
 */
function handleCommand(message) {
  const { command, portionSize, waterAmount } = message;

  console.log(`Received command: ${command}`);

  // Simulate ESP8266 receiving command
  if (command === "feed") {
    console.log(
      `Simulating feeding with portion size: ${portionSize || "default"}`
    );

    // Send acknowledgment
    send({
      eventType: "commandResponse",
      status: "executing",
      command: "feed",
    });

    // After a delay, send completion
    setTimeout(() => {
      simulateFeeding(portionSize, false);
    }, 3000);
  } else if (command === "water") {
    console.log(`Simulating watering with amount: ${waterAmount || "default"}`);

    // Send acknowledgment
    send({
      eventType: "commandResponse",
      status: "executing",
      command: "water",
    });

    // After a delay, send completion
    setTimeout(() => {
      simulateWatering(waterAmount);
    }, 2000);
  }
}

/**
 * Trigger feeding from web client
 */
function triggerFeedNow(portionSize = 0) {
  if (clientType !== "web-client") {
    console.warn("This function is for web clients only");
    return false;
  }

  return send({
    eventType: "feed-now",
    portionSize: portionSize,
  });
}

/**
 * Trigger watering from web client
 */
function triggerWaterNow(waterAmount = 0) {
  if (clientType !== "web-client") {
    console.warn("This function is for web clients only");
    return false;
  }

  return send({
    eventType: "water-now",
    waterAmount: waterAmount,
  });
}

/**
 * Update settings from web client
 */
function updateSettings(newSettings) {
  if (clientType !== "web-client") {
    console.warn("This function is for web clients only");
    return false;
  }

  return send({
    eventType: "update-settings",
    ...newSettings,
  });
}

/**
 * Update schedules from web client
 */
function updateSchedules(newSchedules) {
  if (clientType !== "web-client") {
    console.warn("This function is for web clients only");
    return false;
  }

  return send({
    eventType: "update-schedules",
    schedules: newSchedules,
  });
}

/**
 * Simulate feeding from ESP8266 device
 */
function simulateFeeding(portionSize = 0, isScheduled = false) {
  if (clientType !== "esp8266") {
    console.warn("This function is for ESP8266 simulation only");
    return false;
  }

  // Send feed-now event
  send({
    eventType: "feed-now",
    portionSize: portionSize,
    isScheduled: isScheduled,
  });

  // After a delay, send completion
  setTimeout(() => {
    // Calculate new levels
    const foodLevel = Math.max(
      0,
      (feedingData.foodLevel || 75) - (portionSize || 3) * 2
    );
    const waterLevel = feedingData.waterLevel || 65;

    // Send feeding completion
    send({
      eventType: "feeding-complete",
      isScheduled: isScheduled,
      details: `Fed ${portionSize || settings.portionSize || 3}g of food`,
      foodLevel: foodLevel,
      waterLevel: waterLevel,
    });

    // Log the event
    send({
      eventType: "log-event",
      type: isScheduled ? "scheduled_feed" : "manual_feed",
      details: `Dispensed ${portionSize || settings.portionSize || 3}g of food`,
    });
  }, 4000);

  return true;
}

/**
 * Simulate watering from ESP8266 device
 */
function simulateWatering(waterAmount = 0) {
  if (clientType !== "esp8266") {
    console.warn("This function is for ESP8266 simulation only");
    return false;
  }

  // Send water-now event
  send({
    eventType: "water-now",
    waterAmount: waterAmount,
  });

  // Calculate new water level
  const waterAmountUsed = waterAmount || settings.waterAmount || 150;
  const newWaterLevel = Math.max(
    0,
    (feedingData.waterLevel || 65) - waterAmountUsed / 10
  );

  // After a delay, send completion
  setTimeout(() => {
    // Send water status update
    send({
      eventType: "log-event",
      type: "water_event",
      details: `Dispensed ${waterAmountUsed}ml of water`,
    });

    // Update water level
    send({
      eventType: "updateFeedingData",
      waterLevel: newWaterLevel,
    });
  }, 2000);

  return true;
}

/**
 * Simulate changing client type
 */
function setClientType(type) {
  if (!["web-client", "esp8266"].includes(type)) {
    console.error('Invalid client type. Use "web-client" or "esp8266"');
    return false;
  }

  clientType = type;
  console.log(`Client type changed to: ${type}`);

  if (isConnected) {
    // Re-register with new type
    if (type === "esp8266") {
      registerAsESP8266();
    } else {
      registerAsWebClient();
    }
  }

  return true;
}

/**
 * Run full ESP8266 test scenario
 */
async function runESP8266Test() {
  setClientType("esp8266");
  await connect();

  console.log("Starting ESP8266 test sequence...");

  // Wait for initial data
  await new Promise((resolve) => setTimeout(resolve, 2000));

  // Simulate manual feeding
  console.log("Simulating manual feeding...");
  simulateFeeding(5, false);

  // Wait for completion
  await new Promise((resolve) => setTimeout(resolve, 5000));

  // Simulate scheduled feeding
  console.log("Simulating scheduled feeding...");
  simulateFeeding(3, true);

  // Wait for completion
  await new Promise((resolve) => setTimeout(resolve, 5000));

  // Simulate watering
  console.log("Simulating watering...");
  simulateWatering(100);

  console.log("ESP8266 test complete!");
}

/**
 * Run full web client test scenario
 */
async function runWebClientTest() {
  setClientType("web-client");
  await connect();

  console.log("Starting web client test sequence...");

  // Wait for initial data
  await new Promise((resolve) => setTimeout(resolve, 2000));

  // Update settings
  console.log("Updating settings...");
  updateSettings({
    portionSize: 4,
    waterAmount: 120,
    notifications: true,
    theme: "light",
  });

  // Wait for update
  await new Promise((resolve) => setTimeout(resolve, 1000));

  // Update schedules
  console.log("Updating schedules...");
  updateSchedules([
    { id: "1", time: "07:00", enabled: true },
    { id: "2", time: "12:30", enabled: true },
    { id: "3", time: "18:00", enabled: true },
    { id: "4", time: "22:00", enabled: false },
  ]);

  // Wait for update
  await new Promise((resolve) => setTimeout(resolve, 1000));

  // Trigger feeding
  console.log("Triggering feed now...");
  triggerFeedNow(5);

  // Wait for completion
  await new Promise((resolve) => setTimeout(resolve, 5000));

  // Trigger watering
  console.log("Triggering water now...");
  triggerWaterNow(150);

  console.log("Web client test complete!");
}

// Export functions when in Node.js environment
if (typeof module !== "undefined" && module.exports) {
  module.exports = {
    connect,
    send,
    triggerFeedNow,
    triggerWaterNow,
    updateSettings,
    updateSchedules,
    setClientType,
    simulateFeeding,
    simulateWatering,
    runESP8266Test,
    runWebClientTest,
  };
}

// Usage examples:
// 1. Connect as web client:
//    connect().then(() => console.log("Ready to test!"));
//
// 2. Simulate ESP8266:
//    setClientType('esp8266');
//    connect().then(() => console.log("ESP8266 simulation ready"));
//
// 3. Run full tests:
//    runWebClientTest();
//    // or
//    runESP8266Test();
