const WebSocket = require("ws");
const fs = require("fs");
const path = require("path");

// Configuration
const PORT = process.env.PORT || 3001;
const DB_FILE = path.join(__dirname, "db.json");
const LOGS_DIR = path.join(__dirname, "logs");

// Ensure logs directory exists
if (!fs.existsSync(LOGS_DIR)) {
  fs.mkdirSync(LOGS_DIR, { recursive: true });
}

// Configure simple logger
const logger = {
  info: (message, data) => {
    const timestamp = new Date().toISOString();
    const logData = data ? ` ${JSON.stringify(data)}` : "";
    console.log(`[${timestamp}] INFO: ${message}${logData}`);
    appendToLogFile(`[${timestamp}] INFO: ${message}${logData}`);
  },
  warn: (message, data) => {
    const timestamp = new Date().toISOString();
    const logData = data ? ` ${JSON.stringify(data)}` : "";
    console.warn(`[${timestamp}] WARN: ${message}${logData}`);
    appendToLogFile(`[${timestamp}] WARN: ${message}${logData}`);
  },
  error: (message, data) => {
    const timestamp = new Date().toISOString();
    const logData = data ? ` ${JSON.stringify(data)}` : "";
    console.error(`[${timestamp}] ERROR: ${message}${logData}`);
    appendToLogFile(`[${timestamp}] ERROR: ${message}${logData}`);
  },
};

// Append to log file
function appendToLogFile(message) {
  try {
    const logFile = path.join(LOGS_DIR, "server.log");
    fs.appendFileSync(logFile, message + "\n");
  } catch (err) {
    console.error("Failed to write log:", err);
  }
}

// Initialize database with default data if it doesn't exist
if (!fs.existsSync(DB_FILE)) {
  const defaultData = {
    settings: {
      deviceName: "Auto Cat Feeder",
      theme: "dark",
      notifications: true,
      portionSize: 3,
      waterAmount: 150,
      autoWatering: true,
      clientId: "main-controller",
      debugMode: false,
      logRetention: 30,
      firmwareVersion: "1.2.3",
    },
    schedules: [
      { id: "1", time: "08:00", enabled: true },
      { id: "2", time: "12:00", enabled: true },
      { id: "3", time: "18:00", enabled: true },
    ],
    feedingData: {
      foodLevel: 75,
      waterLevel: 65,
      lastFeed: Date.now() - 2 * 60 * 60 * 1000,
      nextFeed: Date.now() + 4 * 60 * 60 * 1000,
      logs: [],
    },
    systemStatus: {
      feeding: "ready",
      watering: "ready",
    },
    clients: [
      {
        id: "default",
        name: "Default Controller",
        description: "Standard controller with basic features",
        icon: "🏠",
      },
      {
        id: "mobile-app",
        name: "Mobile App",
        description: "Optimized for smartphone access",
        icon: "📱",
      },
      {
        id: "web-dashboard",
        name: "Web Dashboard",
        description: "Full-featured web interface",
        icon: "🖥️",
      },
      {
        id: "esp8266-feeder",
        name: "ESP8266 Device",
        description: "Hardware IoT feeder device",
        icon: "🤖",
      },
    ],
  };

  fs.writeFileSync(DB_FILE, JSON.stringify(defaultData, null, 2));
  logger.info("Created default database file");
}

// Load database
let db;
try {
  const data = fs.readFileSync(DB_FILE);
  db = JSON.parse(data);
} catch (err) {
  logger.error("Error loading database", { error: err.message });
  process.exit(1);
}

// Setup WebSocket server
const wss = new WebSocket.Server({ port: PORT });

// Track connected clients
const clients = new Map();

// Generate a log entry
function createLogEntry(action) {
  const now = new Date();
  const days = [
    "Sunday",
    "Monday",
    "Tuesday",
    "Wednesday",
    "Thursday",
    "Friday",
    "Saturday",
  ];
  const day = days[now.getDay()];
  const hours = now.getHours().toString().padStart(2, "0");
  const minutes = now.getMinutes().toString().padStart(2, "0");
  const seconds = now.getSeconds().toString().padStart(2, "0");

  return {
    time: `${day} ${hours}:${minutes}:${seconds}`,
    action,
  };
}

// Add a log entry to the database
function addLogEntry(action) {
  const logEntry = createLogEntry(action);
  db.feedingData.logs.unshift(logEntry);

  // Respect log retention setting
  const retentionDays = db.settings.logRetention || 30;

  if (db.feedingData.logs.length > 100) {
    db.feedingData.logs = db.feedingData.logs.slice(0, 100);
  }

  return logEntry;
}

// Save database to file
function saveDatabase() {
  try {
    fs.writeFileSync(DB_FILE, JSON.stringify(db, null, 2));
  } catch (err) {
    logger.error("Failed to save database", { error: err.message });
  }
}

// Broadcast to all connected clients
function broadcast(eventType, data) {
  const message = JSON.stringify({ eventType, data, timestamp: Date.now() });
  wss.clients.forEach((client) => {
    if (client.readyState === WebSocket.OPEN) {
      client.send(message);
    }
  });
}

// Update the next scheduled feeding time
function updateNextFeedTime() {
  if (!db.schedules || db.schedules.length === 0) {
    db.feedingData.nextFeed = Date.now() + 24 * 60 * 60 * 1000; // Default to 24 hours
    return;
  }

  const now = new Date();
  const currentHour = now.getHours();
  const currentMinute = now.getMinutes();
  const currentTimeInMinutes = currentHour * 60 + currentMinute;

  // Convert schedule times to minutes since midnight for comparison
  const schedulesInMinutes = db.schedules
    .filter((s) => s.enabled)
    .map((schedule) => {
      const [hours, minutes] = schedule.time.split(":").map(Number);
      return {
        original: schedule,
        minutesSinceMidnight: hours * 60 + minutes,
      };
    });

  // Find the next schedule (first one that's later today)
  let next = schedulesInMinutes.find(
    (s) => s.minutesSinceMidnight > currentTimeInMinutes
  );

  // If no schedule is found for later today, take the first one for tomorrow
  if (!next && schedulesInMinutes.length > 0) {
    next = schedulesInMinutes.reduce(
      (earliest, current) =>
        current.minutesSinceMidnight < earliest.minutesSinceMidnight
          ? current
          : earliest,
      schedulesInMinutes[0]
    );
  }

  if (next) {
    const nextTime = new Date(now);
    const [nextHours, nextMinutes] = next.original.time.split(":").map(Number);
    nextTime.setHours(nextHours, nextMinutes, 0, 0);

    // If the next time is earlier today, it must be for tomorrow
    if (nextTime < now) {
      nextTime.setDate(nextTime.getDate() + 1);
    }

    db.feedingData.nextFeed = nextTime.getTime();
  } else {
    // Default to 24 hours if no schedules
    db.feedingData.nextFeed = Date.now() + 24 * 60 * 60 * 1000;
  }
}

// Check if it's time to feed based on schedules
function checkScheduledFeedings() {
  if (!db.schedules || db.schedules.length === 0) return;

  const now = new Date();
  const currentHour = now.getHours();
  const currentMinute = now.getMinutes();

  // Check each schedule
  db.schedules.forEach((schedule) => {
    if (!schedule.enabled) return;

    const [scheduleHour, scheduleMinute] = schedule.time.split(":").map(Number);

    // If it's exactly the scheduled time
    if (currentHour === scheduleHour && currentMinute === scheduleMinute) {
      // Only trigger if we haven't already fed at this exact time
      const lastFeedTime = new Date(db.feedingData.lastFeed);
      if (
        lastFeedTime.getHours() !== currentHour ||
        lastFeedTime.getMinutes() !== currentMinute
      ) {
        triggerFeed("scheduled");
      }
    }
  });
}

// Trigger a feeding
function triggerFeed(type = "manual", portionSize = null) {
  // Only feed if the system is ready
  if (db.systemStatus.feeding !== "ready") {
    logger.warn("Attempted to feed while system is not ready");
    return false;
  }

  // Set system to busy
  db.systemStatus.feeding = "busy";

  // Set custom portion size if provided
  if (portionSize !== null) {
    db.settings.portionSize = portionSize;
  }

  // Log the action
  const action = `Feed now (${type})`;
  addLogEntry(action);
  logger.info(action, { portionSize: db.settings.portionSize });

  // Update food level (decrease by portion size)
  const portionSizeUsed = db.settings.portionSize || 3;
  db.feedingData.foodLevel = Math.max(
    0,
    db.feedingData.foodLevel - portionSizeUsed * 2
  );

  // Update last feed time
  db.feedingData.lastFeed = Date.now();

  // Calculate next feed time based on schedules
  updateNextFeedTime();

  // Auto water if enabled
  if (db.settings.autoWatering) {
    triggerWater("auto");
  }

  // Broadcast updates
  broadcast("feeding-data", db.feedingData);
  broadcast("system-status", db.systemStatus);
  saveDatabase();

  // Simulate feeding process
  setTimeout(() => {
    db.systemStatus.feeding = "ready";
    broadcast("system-status", db.systemStatus);
    saveDatabase();
  }, 5000);

  return true;
}

// Trigger watering
function triggerWater(type = "manual", waterAmount = null) {
  // Only water if the system is ready
  if (db.systemStatus.watering !== "ready") {
    logger.warn("Attempted to water while system is not ready");
    return false;
  }

  // Set system to busy
  db.systemStatus.watering = "busy";

  // Set custom water amount if provided
  if (waterAmount !== null) {
    db.settings.waterAmount = waterAmount;
  }

  // Log the action
  const action = `Water now (${type})`;
  addLogEntry(action);
  logger.info(action, { waterAmount: db.settings.waterAmount });

  // Update water level (decrease by water amount)
  const waterAmountUsed = db.settings.waterAmount || 150;
  db.feedingData.waterLevel = Math.max(
    0,
    db.feedingData.waterLevel - waterAmountUsed / 10
  );

  // Broadcast updates
  broadcast("feeding-data", db.feedingData);
  broadcast("system-status", db.systemStatus);
  saveDatabase();

  // Simulate watering process
  setTimeout(() => {
    db.systemStatus.watering = "ready";
    broadcast("system-status", db.systemStatus);
    saveDatabase();
  }, 3000);

  return true;
}

// Add this function to handle feed requests with schedule information
function handleFeedRequest(ws, message) {
  try {
    const msg = JSON.parse(message);
    const client = clients.get(ws);

    // Extract data from message
    const isScheduled = msg.isScheduled || false;
    const portionSize = msg.portionSize || db.settings.portionSize || 3;

    logger.info(
      `Feed request received from ${client?.id || "unknown client"}`,
      {
        portionSize,
        isScheduled: isScheduled ? "scheduled" : "manual",
      }
    );

    // Check if this is from an ESP8266 device (reporting a feeding) or a web client (requesting a feeding)
    const isEspDevice = client?.type === "feeder-device";

    if (isEspDevice) {
      // ESP8266 is reporting that it has performed a feeding
      // Update food level (decrease by portion size)
      db.feedingData.foodLevel = Math.max(
        0,
        db.feedingData.foodLevel - portionSize * 2
      );
      db.feedingData.lastFeed = Date.now();

      // Update system status
      db.systemStatus.feeding = "busy";

      // Add log entry
      const logEntry = {
        time: new Date().toISOString(),
        clientId: client.id,
        type: isScheduled ? "scheduled_feed" : "manual_feed",
        details: `${
          isScheduled ? "Scheduled" : "Manual"
        } feeding: ${portionSize}g dispensed`,
      };

      db.feedingData.logs.unshift(logEntry);

      // Broadcast updates to all clients
      broadcast("feeding-data", db.feedingData);
      broadcast("system-status", db.systemStatus);

      // Simulate feeding process completion
      setTimeout(() => {
        db.systemStatus.feeding = "ready";
        broadcast("system-status", db.systemStatus);
        saveDatabase();
      }, 5000);

      saveDatabase();

      // Send acknowledgment
      ws.send(
        JSON.stringify({
          eventType: "feed-ack",
          success: true,
          timestamp: Date.now(),
        })
      );
    } else {
      // This is from a web client requesting a feeding
      // Check if we have a connected ESP8266 device
      let espDevice = null;
      clients.forEach((clientInfo, clientWs) => {
        if (clientInfo.type === "feeder-device") {
          espDevice = clientWs;
        }
      });

      if (espDevice && espDevice.readyState === WebSocket.OPEN) {
        // Forward the command to the ESP8266 device
        espDevice.send(
          JSON.stringify({
            eventType: "command",
            command: "feed",
            portionSize: portionSize,
            timestamp: Date.now(),
          })
        );

        logger.info(`Forwarded feed command to ESP8266`, { portionSize });

        // Send acknowledgment back to the requesting client
        ws.send(
          JSON.stringify({
            eventType: "command-sent",
            command: "feed",
            success: true,
            timestamp: Date.now(),
          })
        );
      } else {
        // No ESP8266 device connected, simulate the feeding directly
        logger.warn(`No ESP8266 device connected, simulating feeding`);

        // Update food level (decrease by portion size)
        db.feedingData.foodLevel = Math.max(
          0,
          db.feedingData.foodLevel - portionSize * 2
        );
        db.feedingData.lastFeed = Date.now();

        // Update system status
        db.systemStatus.feeding = "busy";

        // Add log entry
        const logEntry = {
          time: new Date().toISOString(),
          clientId: client.id,
          type: "web_feed",
          details: `Web feeding request: ${portionSize}g dispensed (simulated)`,
        };

        db.feedingData.logs.unshift(logEntry);

        // Broadcast updates
        broadcast("feeding-data", db.feedingData);
        broadcast("system-status", db.systemStatus);

        // Simulate feeding process completion
        setTimeout(() => {
          db.systemStatus.feeding = "ready";
          broadcast("system-status", db.systemStatus);
          saveDatabase();
        }, 5000);

        saveDatabase();

        // Send simulated acknowledgment
        ws.send(
          JSON.stringify({
            eventType: "command-simulated",
            command: "feed",
            success: true,
            timestamp: Date.now(),
          })
        );
      }
    }
  } catch (err) {
    logger.error("Error handling feed request", { error: err.message });
  }
}

// WebSocket connection handling
wss.on("connection", (ws, req) => {
  const clientId = `client_${Date.now().toString(36)}`;
  logger.info(`Client connected: ${clientId}`);

  // Store client info
  clients.set(ws, { id: clientId, type: "unknown" });

  // Send initial data to client
  ws.send(JSON.stringify({ eventType: "settings", data: db.settings }));
  ws.send(JSON.stringify({ eventType: "schedules", data: db.schedules }));
  ws.send(JSON.stringify({ eventType: "feeding-data", data: db.feedingData }));
  ws.send(
    JSON.stringify({ eventType: "system-status", data: db.systemStatus })
  );
  ws.send(JSON.stringify({ eventType: "clients", data: db.clients }));

  // Handle incoming messages
  ws.on("message", (message) => {
    try {
      const msg = JSON.parse(message);
      const eventType = msg.eventType;

      if (!eventType) {
        logger.warn("Message missing eventType field", { message });
        return;
      }

      const client = clients.get(ws);
      logger.info(`Received ${eventType} from ${client.id}`, msg);

      // Process message based on event type
      switch (eventType) {
        case "register":
          // Register the client
          client.type = msg.deviceType || "unknown";
          client.version = msg.version || "1.0";
          logger.info(`Client ${client.id} registered as ${client.type}`);
          break;

        case "get-settings":
          ws.send(JSON.stringify({ eventType: "settings", data: db.settings }));
          break;

        case "get-schedules":
          ws.send(
            JSON.stringify({ eventType: "schedules", data: db.schedules })
          );
          break;

        case "update-settings":
          logger.info("Updating settings", { clientId: client.id });
          db.settings = { ...db.settings, ...msg };
          broadcast("settings", db.settings);
          saveDatabase();
          break;

        case "update-schedules":
          logger.info("Updating schedules", { clientId: client.id });
          db.schedules = msg.schedules || [];
          updateNextFeedTime();
          broadcast("schedules", db.schedules);
          broadcast("feeding-data", db.feedingData);
          saveDatabase();
          break;

        case "feed-now":
          handleFeedRequest(ws, message);
          break;

        case "water-now":
          logger.info("Manual water requested", { clientId: client.id });
          triggerWater("manual", msg.waterAmount);
          break;

        case "log-event":
          // Log an event from the client
          const logEntry = {
            time: new Date().toISOString(),
            clientId: msg.clientId || client.id,
            type: msg.type || "info",
            details: msg.details || "No details provided",
          };

          db.feedingData.logs.unshift(logEntry);

          // Respect log retention limit
          const maxLogs = (db.settings.logRetention || 30) * 10;
          if (db.feedingData.logs.length > maxLogs) {
            db.feedingData.logs = db.feedingData.logs.slice(0, maxLogs);
          }

          logger.info(
            `Log from ${client.id}: ${logEntry.type} - ${logEntry.details}`
          );
          saveDatabase();
          break;

        case "feeding-complete":
          // Handle feeding completion reports from ESP8266
          if (client?.type === "feeder-device") {
            const foodLevel = msg.foodLevel || db.feedingData.foodLevel;
            const waterLevel = msg.waterLevel || db.feedingData.waterLevel;
            const isScheduled = msg.isScheduled || false;

            // Update feeding data
            db.feedingData.foodLevel = foodLevel;
            db.feedingData.waterLevel = waterLevel;
            db.feedingData.lastFeed = Date.now();

            // Update system status
            db.systemStatus.feeding = "ready";

            // Add log entry
            const logEntry = {
              time: new Date().toISOString(),
              clientId: client.id,
              type: isScheduled
                ? "scheduled_feed_complete"
                : "manual_feed_complete",
              details: `Feeding completed: ${msg.details || "No details"}`,
            };

            db.feedingData.logs.unshift(logEntry);

            // Broadcast updates
            broadcast("feeding-data", db.feedingData);
            broadcast("system-status", db.systemStatus);
            saveDatabase();

            logger.info(`Feeding completed by ${client.id}`, {
              foodLevel,
              waterLevel,
            });
          }
          break;

        default:
          logger.warn(`Unknown event type: ${eventType}`, msg);
      }
    } catch (err) {
      logger.error("Error processing message", {
        error: err.message,
        rawMessage: message,
      });
    }
  });

  // Handle client disconnection
  ws.on("close", () => {
    const client = clients.get(ws);
    logger.info(`Client disconnected: ${client ? client.id : "unknown"}`);
    clients.delete(ws);
  });
});

// Check for scheduled feedings every minute
setInterval(checkScheduledFeedings, 60000);

// Simulate random food level decrease
setInterval(() => {
  if (Math.random() < 0.3) {
    // 30% chance each interval
    db.feedingData.foodLevel = Math.max(0, db.feedingData.foodLevel - 1);
    db.feedingData.waterLevel = Math.max(0, db.feedingData.waterLevel - 0.5);
    broadcast("feeding-data", db.feedingData);
    saveDatabase();
  }
}, 300000); // Every 5 minutes

// Handle graceful shutdown
process.on("SIGINT", () => {
  logger.info("Server shutting down...");
  saveDatabase();

  // Close all connections
  wss.clients.forEach((client) => {
    try {
      client.close(1000, "Server shutting down");
    } catch (e) {
      // Ignore errors when trying to close
    }
  });

  // Exit process
  setTimeout(() => process.exit(0), 1000);
});

logger.info(`WebSocket server running on port ${PORT}`);
console.log(`
🐱 Cat Feeder WebSocket Server
-----------------------------
Server running on port: ${PORT}
Database: ${DB_FILE}
Logs: ${LOGS_DIR}

Press Ctrl+C to stop the server
`);
