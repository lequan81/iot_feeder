// WebSocket connection
let socket = null;
let isConnected = false;
let settings = {};
let schedules = [];
let feedingData = {};
let systemStatus = {};

// DOM Elements
const statusIndicator = document.getElementById("status-indicator");
const foodProgress = document.getElementById("food-progress");
const foodPercentage = document.getElementById("food-percentage");
const waterProgress = document.getElementById("water-progress");
const waterPercentage = document.getElementById("water-percentage");
const feedingStatus = document.getElementById("feeding-status");
const wateringStatus = document.getElementById("watering-status");
const lastFeeding = document.getElementById("last-feeding");
const nextFeeding = document.getElementById("next-feeding");
const portionSize = document.getElementById("portion-size");
const portionDisplay = document.getElementById("portion-display");
const waterAmount = document.getElementById("water-amount");
const waterDisplay = document.getElementById("water-display");
const feedButton = document.getElementById("feed-button");
const waterButton = document.getElementById("water-button");
const autoWatering = document.getElementById("auto-watering");
const saveSettings = document.getElementById("save-settings");
const scheduleContainer = document.getElementById("schedule-container");
const addScheduleButton = document.getElementById("add-schedule");
const saveSchedulesButton = document.getElementById("save-schedules");
const logContainer = document.getElementById("log-container");
const messageElement = document.getElementById("message");

// Connect to WebSocket server
function connect() {
  const serverUrl = `ws://${window.location.hostname}:3001`;

  socket = new WebSocket(serverUrl);

  socket.onopen = () => {
    console.log("WebSocket connected");
    isConnected = true;
    updateConnectionStatus();

    // Register as web client
    socket.send(
      JSON.stringify({
        eventType: "register",
        deviceType: "web-dashboard",
        version: "1.0",
        clientId: "web-client-" + Date.now().toString(36),
      })
    );
  };

  socket.onclose = () => {
    console.log("WebSocket disconnected");
    isConnected = false;
    updateConnectionStatus();

    // Try to reconnect after a delay
    setTimeout(connect, 5000);
  };

  socket.onerror = (error) => {
    console.error("WebSocket error:", error);
    showMessage("Connection error. Check if server is running.", "error");
  };

  socket.onmessage = (event) => {
    try {
      const message = JSON.parse(event.data);
      handleMessage(message);
    } catch (err) {
      console.error("Error parsing message:", err);
    }
  };
}

// Update UI based on connection status
function updateConnectionStatus() {
  if (isConnected) {
    statusIndicator.textContent = "Connected";
    statusIndicator.className = "connected";
    feedButton.disabled = false;
    waterButton.disabled = false;
  } else {
    statusIndicator.textContent = "Disconnected";
    statusIndicator.className = "disconnected";
    feedButton.disabled = true;
    waterButton.disabled = true;
  }
}

// Format date/time for display
function formatDateTime(timestamp) {
  if (!timestamp) return "Never";

  const date = new Date(timestamp);
  return date.toLocaleString();
}

// Handle incoming WebSocket messages
function handleMessage(message) {
  const { eventType, data } = message;

  switch (eventType) {
    case "settings":
      settings = data;
      updateSettingsDisplay();
      break;

    case "schedules":
      schedules = data;
      updateScheduleDisplay();
      break;

    case "feeding-data":
      feedingData = data;
      updateFeedingDataDisplay();
      break;

    case "system-status":
      systemStatus = data;
      updateSystemStatusDisplay();
      break;

    case "command-sent":
      showMessage("Command sent to feeder device.");
      break;

    case "command-simulated":
      showMessage("Command simulated (device not connected).");
      break;
  }
}

// Update settings display
function updateSettingsDisplay() {
  if (settings) {
    portionSize.value = settings.portionSize || 3;
    portionDisplay.textContent = `${portionSize.value}g`;

    waterAmount.value = settings.waterAmount || 150;
    waterDisplay.textContent = `${waterAmount.value}ml`;

    autoWatering.checked = settings.autoWatering || false;
  }
}

// Update feeding data display
function updateFeedingDataDisplay() {
  if (feedingData) {
    // Update progress bars
    const foodLevel = feedingData.foodLevel || 0;
    foodProgress.style.width = `${foodLevel}%`;
    foodPercentage.textContent = `${Math.round(foodLevel)}%`;

    const waterLevel = feedingData.waterLevel || 0;
    waterProgress.style.width = `${waterLevel}%`;
    waterPercentage.textContent = `${Math.round(waterLevel)}%`;

    // Update feeding times
    lastFeeding.textContent = formatDateTime(feedingData.lastFeed);
    nextFeeding.textContent = formatDateTime(feedingData.nextFeed);

    // Update logs
    updateLogDisplay(feedingData.logs || []);
  }
}

// Update system status display
function updateSystemStatusDisplay() {
  if (systemStatus) {
    feedingStatus.textContent = systemStatus.feeding || "Unknown";
    feedingStatus.className =
      systemStatus.feeding === "ready" ? "status-ready" : "status-busy";

    wateringStatus.textContent = systemStatus.watering || "Unknown";
    wateringStatus.className =
      systemStatus.watering === "ready" ? "status-ready" : "status-busy";

    // Update button states
    feedButton.disabled = systemStatus.feeding !== "ready" || !isConnected;
    waterButton.disabled = systemStatus.watering !== "ready" || !isConnected;
  }
}

// Update log display
function updateLogDisplay(logs) {
  if (!logs || !logs.length) return;

  // Clear existing logs if needed
  if (logs.length > 20) {
    logContainer.innerHTML = "";
  }

  // Add the most recent logs at the top
  const recentLogs = logs.slice(0, 20);

  recentLogs.forEach((log, index) => {
    // Check if log entry already exists
    const existingLog = document.querySelector(`[data-log-id="${index}"]`);
    if (existingLog) return;

    const logEntry = document.createElement("div");
    logEntry.className = "log-entry";
    logEntry.setAttribute("data-log-id", index);

    const logTime = document.createElement("span");
    logTime.className = "log-time";
    logTime.textContent = log.time || "Unknown time";

    const logDetails = document.createElement("span");
    logDetails.textContent = log.details || log.action || "No details";

    logEntry.appendChild(logTime);
    logEntry.appendChild(logDetails);

    // Insert at top
    if (logContainer.firstChild) {
      logContainer.insertBefore(logEntry, logContainer.firstChild);
    } else {
      logContainer.appendChild(logEntry);
    }
  });
}

// Update schedule display
function updateScheduleDisplay() {
  scheduleContainer.innerHTML = "";

  if (schedules && schedules.length) {
    schedules.forEach((schedule, index) => {
      const scheduleItem = document.createElement("div");
      scheduleItem.className = "schedule-item";
      scheduleItem.setAttribute("data-schedule-id", schedule.id || index);

      const timeInput = document.createElement("input");
      timeInput.type = "time";
      timeInput.value = schedule.time || "12:00";
      timeInput.className = "schedule-time";

      const enabledInput = document.createElement("input");
      enabledInput.type = "checkbox";
      enabledInput.checked = schedule.enabled !== false;
      enabledInput.className = "schedule-enabled";

      const enabledLabel = document.createElement("label");
      enabledLabel.textContent = "Enabled";

      const deleteButton = document.createElement("button");
      deleteButton.textContent = "✕";
      deleteButton.className = "delete-schedule";
      deleteButton.onclick = () => removeSchedule(index);

      scheduleItem.appendChild(timeInput);
      scheduleItem.appendChild(enabledInput);
      scheduleItem.appendChild(enabledLabel);
      scheduleItem.appendChild(deleteButton);

      scheduleContainer.appendChild(scheduleItem);
    });
  }
}

// Send feed command
function sendFeedCommand() {
  if (!isConnected) return;

  const size = parseInt(portionSize.value);

  socket.send(
    JSON.stringify({
      eventType: "feed-now",
      portionSize: size,
    })
  );

  showMessage(`Sent feed command with ${size}g portion size`);
}

// Send water command
function sendWaterCommand() {
  if (!isConnected) return;

  const amount = parseInt(waterAmount.value);

  socket.send(
    JSON.stringify({
      eventType: "water-now",
      waterAmount: amount,
    })
  );

  showMessage(`Sent water command with ${amount}ml amount`);
}

// Save settings
function saveUserSettings() {
  if (!isConnected) return;

  const updatedSettings = {
    eventType: "update-settings",
    portionSize: parseInt(portionSize.value),
    waterAmount: parseInt(waterAmount.value),
    autoWatering: autoWatering.checked,
  };

  socket.send(JSON.stringify(updatedSettings));
  showMessage("Settings saved successfully");
}

// Add a new schedule
function addNewSchedule() {
  const newSchedule = {
    id: `schedule_${Date.now()}`,
    time: "12:00",
    enabled: true,
  };

  if (!schedules) schedules = [];
  schedules.push(newSchedule);

  updateScheduleDisplay();
  showMessage("New schedule added - remember to save!");
}

// Remove a schedule
function removeSchedule(index) {
  schedules.splice(index, 1);
  updateScheduleDisplay();
  showMessage("Schedule removed - remember to save!");
}

// Save schedules
function saveSchedules() {
  if (!isConnected) return;

  const updatedSchedules = [];
  const scheduleItems = document.querySelectorAll(".schedule-item");

  scheduleItems.forEach((item) => {
    const id = item.getAttribute("data-schedule-id");
    const timeInput = item.querySelector(".schedule-time");
    const enabledInput = item.querySelector(".schedule-enabled");

    updatedSchedules.push({
      id: id,
      time: timeInput.value,
      enabled: enabledInput.checked,
    });
  });

  socket.send(
    JSON.stringify({
      eventType: "update-schedules",
      schedules: updatedSchedules,
    })
  );

  schedules = updatedSchedules;
  showMessage("Schedules saved successfully");
}

// Show message notification
function showMessage(text, type = "info") {
  messageElement.textContent = text;
  messageElement.className = `message show ${type}`;

  setTimeout(() => {
    messageElement.className = "message";
  }, 3000);
}

// Event listeners for range inputs
portionSize.addEventListener("input", () => {
  portionDisplay.textContent = `${portionSize.value}g`;
});

waterAmount.addEventListener("input", () => {
  waterDisplay.textContent = `${waterAmount.value}ml`;
});

// Event listeners for buttons
feedButton.addEventListener("click", sendFeedCommand);
waterButton.addEventListener("click", sendWaterCommand);
saveSettings.addEventListener("click", saveUserSettings);
addScheduleButton.addEventListener("click", addNewSchedule);
saveSchedulesButton.addEventListener("click", saveSchedules);

// Initialize connection
connect();
