// State management
let currentState = {
    mode: '',
    connected: false,
    lastError: null,
    count: 0,
    timerActive: false,
    mqttConnected: false,
    deviceIp: '-',
    lastUpdate: '-',
    lastActiveMode: ''
};

// MQTT client setup
const mqttClient = mqtt.connect('ws://broker.hivemq.com:8000/mqtt', {
    clientId: 'webClient_' + Math.random().toString(16).substr(2, 8),
    clean: true,
    connectTimeout: 4000,
    reconnectPeriod: 1000,
    qos: 1
});

const MQTT_TOPIC_COMMAND = 'led/mode';
const MQTT_TOPIC_STATUS = 'ledmatrix/status';

// UI Elements
const connectionStatus = document.getElementById('connection-status');
const mqttStatus = document.getElementById('mqtt-status');
const errorMessage = document.getElementById('error-message');
const currentMode = document.getElementById('current-mode');
const currentCount = document.getElementById('current-count');
const timerStatus = document.getElementById('timer-status');
const deviceIp = document.getElementById('device-ip');
const lastUpdate = document.getElementById('last-update');

// MQTT Connection handling
mqttClient.on('connect', () => {
    console.log('Connected to MQTT broker');
    currentState.mqttConnected = true;
    mqttStatus.textContent = 'MQTT: Connected';
    mqttStatus.className = 'status connected';
    
    // Subscribe to status updates
    mqttClient.subscribe(MQTT_TOPIC_STATUS, { qos: 1 }, (err) => {
        if (err) {
            console.error('Error subscribing to status:', err);
            showError('Failed to subscribe to device status');
        } else {
            console.log('Subscribed to', MQTT_TOPIC_STATUS);
            // Request initial status
            sendCommand('status');
        }
    });
});

mqttClient.on('message', (topic, message) => {
    console.log(`Received message on ${topic}: ${message.toString()}`);
    
    if (topic === MQTT_TOPIC_STATUS) {
        try {
            const status = JSON.parse(message.toString());
            updateDeviceStatus(status);
        } catch (error) {
            console.error('Error parsing status message:', error);
        }
    }
});

mqttClient.on('error', (error) => {
    console.error('MQTT Error:', error);
    currentState.mqttConnected = false;
    mqttStatus.textContent = 'MQTT: Error';
    mqttStatus.className = 'status error';
    showError(`MQTT Error: ${error.message}`);
});

mqttClient.on('close', () => {
    currentState.mqttConnected = false;
    mqttStatus.textContent = 'MQTT: Disconnected';
    mqttStatus.className = 'status disconnected';
});

mqttClient.on('reconnect', () => {
    console.log('Attempting to reconnect to MQTT broker...');
});

// Command functions
function sendCommand(command) {
    if (!currentState.mqttConnected) {
        showError('Not connected to MQTT broker');
        return;
    }

    console.log('Sending command:', command);
    const message = {
        command: command,
        timestamp: new Date().toISOString(),
        id: Math.random().toString(16).substr(2, 8)
    };

    mqttClient.publish(MQTT_TOPIC_COMMAND, JSON.stringify(message), { qos: 1 }, (error) => {
        if (error) {
            console.error('Error publishing message:', error);
            showError('Failed to send command');
        } else {
            console.log('Command sent successfully');
        }
    });
}

// Add direct button click handlers
document.addEventListener('DOMContentLoaded', () => {
    // Count Up button
    document.querySelector('button[data-command="B"]').addEventListener('click', () => {
        console.log('Count Up clicked');
        sendCommand('B');
    });

    // Count Down button
    document.querySelector('button[data-command="C"]').addEventListener('click', () => {
        console.log('Count Down clicked');
        sendCommand('C');
    });

    // Timer button
    document.querySelector('button[data-command="D"]').addEventListener('click', () => {
        console.log('Timer clicked');
        sendCommand('D');
    });
});

function setCount() {
    if (!currentState.mqttConnected) {
        showError('Not connected to MQTT broker');
        return;
    }
    
    const newCount = prompt('Enter a number (0-9):', '0');
    if (newCount === null) return;

    const num = parseInt(newCount);
    if (isNaN(num) || num < 0 || num > 9) {
        showError('Please enter a valid number between 0 and 9');
        return;
    }

    console.log('Setting count to:', num);
    sendCommand('A');
    setTimeout(() => {
        sendCommand(num.toString());
    }, 200);
}

function stopOperation() {
    if (!currentState.mqttConnected) {
        showError('Not connected to MQTT broker');
        return;
    }

    console.log('Stop clicked');
    sendCommand('stop');
    updateDisplay();
}

function updateDisplay() {
    currentMode.textContent = currentState.mode;
    currentCount.textContent = currentState.count;
    timerStatus.textContent = currentState.timerActive ? 'Active' : 'Inactive';
    
    if (currentState.lastError) {
        errorMessage.textContent = currentState.lastError;
        errorMessage.style.display = 'block';
    } else {
        errorMessage.style.display = 'none';
    }
}

function updateDeviceStatus(status) {
    console.log('Received status update:', status);
    if (status.status === 'online') {
        currentState.deviceIp = status.ip;
        currentState.lastUpdate = new Date().toLocaleString();
        deviceIp.textContent = status.ip;
        lastUpdate.textContent = currentState.lastUpdate;
        
        // Map numeric mode to text
        const modeMap = {
            1: 'SET',
            2: 'UP',
            3: 'DOWN',
            4: 'TIMER'
        };
        
        // Update mode only if it's not 0 (IDLE) or if we don't have a last active mode
        if (status.mode !== 0 || !currentState.lastActiveMode) {
            currentState.mode = modeMap[status.mode] || status.mode;
            if (status.mode !== 0) {
                currentState.lastActiveMode = currentState.mode;
            }
        } else {
            // If current mode is IDLE, display last active mode
            currentState.mode = currentState.lastActiveMode;
        }
        
        currentState.count = status.count;
        currentState.timerActive = status.timer_active;
        updateDisplay();
    }
}

function showError(message) {
    console.error(message);
    currentState.lastError = message;
    errorMessage.textContent = message;
    errorMessage.style.display = 'block';
    setTimeout(() => {
        errorMessage.style.display = 'none';
        currentState.lastError = null;
    }, 5000);
}

// Add touch feedback for mobile devices
document.addEventListener('touchstart', function() {}, {passive: true});

// Handle visibility change
document.addEventListener('visibilitychange', () => {
    if (document.visibilityState === 'visible' && (!mqttClient || !mqttClient.connected)) {
        connectMQTT();
    }
}); 