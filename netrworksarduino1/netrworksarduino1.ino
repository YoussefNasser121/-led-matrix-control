#include <WiFi.h>
#include <PubSubClient.h>
#include <MD_Parola.h>
#include <MD_MAX72XX.h>
#include <SPI.h>
#include <Keypad.h>
#include <LiquidCrystal_I2C.h>
#include <ArduinoJson.h>

// Function declarations
void updateLCD(const char* msg);
void updateMatrix(int value);
void setup_wifi();
void mqtt_reconnect();
void callback(char* topic, byte* payload, unsigned int length);
void processCommand(char cmd);
void publishStatus();

// WiFi and MQTT settings
const char* ssid = "iPhone";  // Replace with your WiFi name
const char* password = "88888888";  // Replace with your WiFi password
const char* mqtt_server = "broker.hivemq.com";
const int mqtt_port = 1883;  // Raw MQTT port (not WebSocket)
const char* mqtt_client_id = "ESP32Client";

// MQTT Topics
const char* MQTT_TOPIC_COMMAND = "led/mode";
const char* MQTT_TOPIC_STATUS = "ledmatrix/status";

// Timing constants
const unsigned long WIFI_CHECK_INTERVAL = 2000;    // Check WiFi every 2 seconds
const unsigned long MQTT_CHECK_INTERVAL = 1000;    // Check MQTT every 1 second
const unsigned long STATUS_UPDATE_INTERVAL = 1000; // Send status every 1 second
const unsigned long COMMAND_TIMEOUT = 200;         // 200ms command debounce
const unsigned long WIFI_TIMEOUT = 20000;         // 20 second WiFi timeout
const unsigned long MQTT_TIMEOUT = 10000;         // 10 second MQTT timeout

// Global timing variables
unsigned long lastStatusUpdate = 0;
unsigned long lastWiFiCheck = 0;
unsigned long lastMQTTCheck = 0;
unsigned long lastCommandTime = 0;
String lastCommandId = "";

// Network clients
WiFiClient espClient;
PubSubClient client(espClient);

// LED Matrix configuration
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 1
#define CLK_PIN   18
#define DATA_PIN  23
#define CS_PIN    5
MD_Parola matrix = MD_Parola(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);

// LCD configuration
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Keypad configuration
char keys[4][4] = {
    {'1', '2', '3', 'A'},
    {'4', '5', '6', 'B'},
    {'7', '8', '9', 'C'},
    {'*', '0', '#', 'D'}
};
byte rowPins[4] = {13, 12, 14, 27};
byte colPins[4] = {26, 25, 33, 32};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, 4, 4);

// Mode enumeration and state variables
enum Mode {MODE_IDLE, MODE_SET, MODE_UP, MODE_DOWN, MODE_TIMER};
Mode currentMode = MODE_IDLE;
int currentDigit = 0;
bool waitingForSetDigit = false;
unsigned long lastUpdate = 0;
bool stateChanged = false;

// Function to update LCD display
void updateLCD(const char* msg) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Mode:");
    lcd.setCursor(0, 1);
    lcd.print(msg);
}

// Function to update LED Matrix
void updateMatrix(int value) {
    matrix.displayClear();
    matrix.print(value);
    matrix.displayAnimate();
}

// Non-blocking WiFi connection
void setup_wifi() {
    static unsigned long lastAttempt = 0;
    static bool connecting = false;
    static int retryCount = 0;
    
    // If we're connected, periodically verify the connection is still good
    if (WiFi.status() == WL_CONNECTED) {
        if (millis() - lastAttempt >= WIFI_CHECK_INTERVAL) {
            lastAttempt = millis();
            Serial.print("WiFi still connected. IP: ");
            Serial.print(WiFi.localIP());
            Serial.print(" RSSI: ");
            Serial.println(WiFi.RSSI());
        }
        return;
    }
    
    // Not connected, attempt to connect
    if (!connecting) {
        Serial.println("\n=== Starting WiFi connection ===");
        Serial.print("Connecting to: ");
        Serial.println(ssid);
        
        WiFi.disconnect(true);  // Disconnect and clear saved credentials
        delay(1000);
        WiFi.mode(WIFI_STA);    // Set station mode
        delay(100);
        WiFi.begin(ssid, password);
        connecting = true;
        lastAttempt = millis();
        retryCount = 0;
        return;
    }
    
    // Check connection progress
    if (millis() - lastAttempt >= 500) {  // Check every 500ms
        lastAttempt = millis();
        
        switch (WiFi.status()) {
            case WL_CONNECTED:
                Serial.println("\n=== WiFi Connected Successfully ===");
                Serial.print("IP address: ");
                Serial.println(WiFi.localIP());
                Serial.print("Signal strength (RSSI): ");
                Serial.print(WiFi.RSSI());
                Serial.println(" dBm");
                connecting = false;
                retryCount = 0;
                break;
                
            case WL_CONNECT_FAILED:
                Serial.println("Connection failed! Please check credentials.");
                connecting = false;
                break;
                
            case WL_NO_SSID_AVAIL:
                Serial.println("Network not found! Please check SSID.");
                connecting = false;
                break;
                
            default:
                if (millis() - lastAttempt > WIFI_TIMEOUT) {
                    Serial.println("\nWiFi connection timeout!");
                    Serial.print("Retry count: ");
                    Serial.println(++retryCount);
                    
                    if (retryCount >= 3) {
                        Serial.println("Multiple failures. Resetting WiFi...");
                        WiFi.disconnect(true);  // Disconnect and clear saved credentials
                        delay(1000);
                        ESP.restart();  // Restart the ESP32
                        return;
                    }
                    
                    connecting = false;
                } else {
                    Serial.print(".");  // Show connection progress
                }
                break;
        }
    }
}

// Non-blocking MQTT reconnection
void mqtt_reconnect() {
    if (client.connected()) {
        static unsigned long lastPing = 0;
        if (millis() - lastPing >= 30000) {  // Ping every 30 seconds
            lastPing = millis();
            if (!client.publish(MQTT_TOPIC_STATUS, "ping", false)) {
                Serial.println("MQTT ping failed!");
                client.disconnect();
            }
        }
        return;
    }
    
    static unsigned long lastAttempt = 0;
    static int retryCount = 0;
    
    if (millis() - lastAttempt < MQTT_CHECK_INTERVAL) return;
    
    lastAttempt = millis();
    Serial.println("\n=== Attempting MQTT connection ===");
    Serial.print("Server: ");
    Serial.print(mqtt_server);
    Serial.print(":");
    Serial.println(mqtt_port);
    
    // Generate a unique client ID
    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);
    
    if (client.connect(clientId.c_str())) {
        Serial.println("MQTT connected successfully");
        Serial.print("Client ID: ");
        Serial.println(clientId);
        
        // Subscribe to command topic with QoS 1
        if (client.subscribe(MQTT_TOPIC_COMMAND, 1)) {
            Serial.println("Subscribed to led/mode");
        } else {
            Serial.println("Failed to subscribe!");
        }
        
        // Publish initial status
        publishStatus();
        retryCount = 0;
    } else {
        Serial.print("MQTT connection failed, rc=");
        Serial.print(client.state());
        Serial.print(" try=");
        Serial.println(++retryCount);
        
        if (retryCount >= 5) {
            Serial.println("Too many MQTT failures, restarting...");
            ESP.restart();
        }
    }
}

// Process incoming commands
void processCommand(char cmd) {
    // Debounce commands
    unsigned long currentTime = millis();
    if (currentTime - lastCommandTime < COMMAND_TIMEOUT) {
        Serial.println("Command debounced");
        return;
    }
    lastCommandTime = currentTime;

    Serial.print("Processing command: ");
    Serial.println(cmd);

    switch (cmd) {
        case 'A':
            currentMode = MODE_SET;
            waitingForSetDigit = true;
            updateLCD("Set Count");
            break;
        case 'B':
            currentMode = MODE_UP;
            currentDigit = (currentDigit + 1) % 10;
            updateMatrix(currentDigit);
            updateLCD("Up Count");
            break;
        case 'C':
            currentMode = MODE_DOWN;
            if (currentDigit == 0) {
                currentDigit = 9;
            } else {
                currentDigit = currentDigit - 1;
            }
            updateMatrix(currentDigit);
            updateLCD("Down Count");
            break;
        case 'D':
            currentMode = MODE_TIMER;
            currentDigit = 0;
            lastUpdate = millis();
            updateLCD("Timer");
            updateMatrix(currentDigit);
            break;
        case 'S':  // Stop command
            currentMode = MODE_IDLE;
            updateLCD("Idle");
            break;
        default:
            if (waitingForSetDigit && isDigit(cmd)) {
                currentDigit = cmd - '0';
                updateMatrix(currentDigit);
                waitingForSetDigit = false;
                updateLCD("Set Count");
                currentMode = MODE_IDLE;
            }
            break;
    }
    stateChanged = true;
}

// MQTT callback for incoming messages
void callback(char* topic, byte* payload, unsigned int length) {
    String message = "";
    for (int i = 0; i < length; i++) {
        message += (char)payload[i];
    }

    Serial.print("Received message: ");
    Serial.println(message);

    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, message);
    
    if (error) {
        Serial.print("deserializeJson() failed: ");
        Serial.println(error.c_str());
        return;
    }

    // Check for duplicate command using ID
    const char* commandId = doc["id"];
    if (commandId && String(commandId) == lastCommandId) {
        Serial.println("Duplicate command ignored");
        return;
    }
    if (commandId) {
        lastCommandId = String(commandId);
    }

    const char* command = doc["command"];
    if (!command) return;

    if (strcmp(command, "status") == 0) {
        publishStatus();
        return;
    }

    if (strcmp(command, "stop") == 0) {
        processCommand('S');
        return;
    }

    processCommand(command[0]);
}

// Publish status to MQTT
void publishStatus() {
    if (!client.connected()) return;

    StaticJsonDocument<200> doc;
    doc["status"] = "online";
    doc["ip"] = WiFi.localIP().toString();
    doc["mode"] = currentMode;
    doc["count"] = currentDigit;
    doc["timer_active"] = (currentMode == MODE_TIMER);
    doc["timestamp"] = millis();
    doc["rssi"] = WiFi.RSSI();

    char buffer[200];
    serializeJson(doc, buffer);
    
    Serial.print("Publishing status: ");
    Serial.println(buffer);
    
    if (!client.publish(MQTT_TOPIC_STATUS, buffer, true)) {
        Serial.println("Failed to publish status!");
    }
    stateChanged = false;
}

void setup() {
    // Initialize Serial for debugging
    Serial.begin(115200);
    while (!Serial) delay(100);  // Wait for Serial to be ready
    
    Serial.println("\n=== ESP32 LED Matrix Controller Starting ===");
    Serial.print("WiFi SSID: ");
    Serial.println(ssid);
    Serial.print("MQTT Server: ");
    Serial.print(mqtt_server);
    Serial.print(":");
    Serial.println(mqtt_port);
    
    // Initialize LED Matrix
    matrix.begin();
    matrix.setIntensity(5);
    matrix.displayClear();
    matrix.displayText("0", PA_CENTER, 100, 0, PA_PRINT, PA_NO_EFFECT);
    Serial.println("LED Matrix initialized");

    // Initialize LCD
    lcd.begin();
    lcd.backlight();
    updateLCD("Idle");
    Serial.println("LCD initialized");

    // Initialize WiFi
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);
    delay(100);
    
    // Configure MQTT
    client.setServer(mqtt_server, mqtt_port);
    client.setCallback(callback);
    client.setKeepAlive(60);
    
    // Set keypad to be non-blocking
    keypad.setDebounceTime(50);
    keypad.setHoldTime(500);
    
    Serial.println("Setup complete. Starting main loop...");
}

void loop() {
    unsigned long currentMillis = millis();

    // Non-blocking network maintenance
    if (currentMillis - lastWiFiCheck >= WIFI_CHECK_INTERVAL) {
        setup_wifi();
        lastWiFiCheck = currentMillis;
    }

    if (WiFi.status() == WL_CONNECTED && currentMillis - lastMQTTCheck >= MQTT_CHECK_INTERVAL) {
        mqtt_reconnect();
        lastMQTTCheck = currentMillis;
    }

    if (client.connected()) {
        client.loop();
    }

    // Handle keypad input
    char key = keypad.getKey();
    if (key) {
        processCommand(key);
    }

    // Timer mode updates
    if (currentMode == MODE_TIMER) {
        if (currentMillis - lastUpdate >= 1000) {
            lastUpdate = currentMillis;
            currentDigit++;
            if (currentDigit > 9) {
                currentDigit = 0;
                currentMode = MODE_IDLE;
                updateLCD("Idle");
            }
            updateMatrix(currentDigit);
            stateChanged = true;
        }
    }

    // Status updates
    if ((stateChanged || currentMillis - lastStatusUpdate >= STATUS_UPDATE_INTERVAL) && client.connected()) {
        publishStatus();
        lastStatusUpdate = currentMillis;
    }

    // Update matrix display
    matrix.displayAnimate();
    
    // Small delay to prevent CPU hogging
    delay(1);
}
