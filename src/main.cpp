/*
An Arduino program for an ESP32 board that sends and receives Modbus commands
via a MAX485 module and also has a web server with an API that allows to get and
post to the Modbus.

The MAX485 module has four pins for the RS-485 communication (A, B, A', and B'),
and two control pins (~RE and DE). The A and B pins are connected to the RX and
TX pins of the ESP32, respectively. The ~RE pin is connected to a digital output
pin of the ESP32 and is used to enable or disable the receiver. The DE pin is
connected to a digital output pin of the ESP32 and is used to enable or disable
the transmitter.
*/

#include <Arduino.h>

#include <map>

// ESP32 filesystem
#include "SPIFFS.h"

// Wifi connection
#include <DNSServer.h>
#ifdef ESP32
#include <WiFi.h>
// #include <AsyncTCP.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#endif

// ModbusClient RTU client
#include <ModbusClientRTU.h>

// Web server
#include <ESPAsyncWebServer.h>

// Store/retrieve preferences (network credentials)
#include <Preferences.h>

// Comment this to disable output to serial
#define OUTPUT_SERIAL Serial

#define MODBUS_SERIAL Serial2
#define MODBUS_DEVICE_ID 1
#define MODBUS_TIMEOUT 2000
#define MODBUS_BAUDRATE 9600
#define MODBUS_CONFIG SERIAL_8N1
#define MODBUS_RX_PIN GPIO_NUM_17
#define MODBUS_TX_PIN GPIO_NUM_16
#define MODBUS_DERE_PIN GPIO_NUM_5

#define HTTP_PARAM_ADDRESS "a"
#define HTTP_PARAM_LENGTH "l"

#ifdef OUTPUT_SERIAL
#define OUTPUT_SERIAL_print(...) OUTPUT_SERIAL.print(__VA_ARGS__)
#define OUTPUT_SERIAL_printf(...) OUTPUT_SERIAL.printf(__VA_ARGS__)
#define OUTPUT_SERIAL_println(...) OUTPUT_SERIAL.println(__VA_ARGS__)
#else
#define OUTPUT_SERIAL_print(...)
#define OUTPUT_SERIAL_printf(...)
#define OUTPUT_SERIAL_println(...)
#endif

// Create a ModbusRTU client instance
ModbusClientRTU modbus(MODBUS_SERIAL, MODBUS_DERE_PIN);
// ModbusClientRTU modbus(MODBUS_SERIAL);

DNSServer dnsServer;
AsyncWebServer server(80);

// Dictionary for holding the request objects with their associated modbus
// tokens to respond
std::map<uint32_t, AsyncWebServerRequest *> tokenRequests;

// Modbus request token counter
uint32_t tokenCounter = 1;

// Whether the modbus connection has been enabled
bool modbusEnabled = false;

Preferences preferences;
String ssid;
String password;
bool captivePortalMode = false;

/********* LED blinking *********/
int ledBlinkState = LOW;           // LED off by default
unsigned long previousMillis = 0;  // will store last time LED was updated

#define CAPTIVE_DELAYS 4
long ledCaptiveDelays[CAPTIVE_DELAYS] = {150, 150, 150, 1000};
int ledBlinkDelayNum = 0;

long ledConnectingInterval = 1000;
/********* LED blinking *********/

// API endpoint to get the values of holding registers
void handleHTTPGet(AsyncWebServerRequest *request) {
    uint8_t address;
    uint8_t length;

    if (request->hasParam(HTTP_PARAM_ADDRESS)) {
        address = request->getParam(HTTP_PARAM_ADDRESS)->value().toInt();
    } else {
        address = 0;
    }

    if (request->hasParam(HTTP_PARAM_LENGTH)) {
        length = request->getParam(HTTP_PARAM_LENGTH)->value().toInt();
    } else {
        length = 0;
    }

    if ((address == 0) || (length == 0)) {
        request->send(400, "application/json", "{\"error\": \"Invalid address or length\"}");
        return;
    }

    OUTPUT_SERIAL_print("Address: ");
    OUTPUT_SERIAL_println(address);

    OUTPUT_SERIAL_print("Length: ");
    OUTPUT_SERIAL_println(length);

    Error err = modbus.addRequest(tokenCounter, 1, READ_HOLD_REGISTER, address, length);
    if (err != SUCCESS) {
        ModbusError e(err);

        char responseBuf[256];
        snprintf(responseBuf, 256, "Error creating request: %02X - %s\n", (int)e, (const char *)e);
        request->send(500, "text/plain", responseBuf);

        OUTPUT_SERIAL_printf("Error creating request: %02X - %s\n", (int)e, (const char *)e);
    } else {
        tokenRequests[tokenCounter] = request;
        tokenCounter += 1;
    }

    // application/octet-stream

    // // send a modbus command to slave device with id MODBUS_DEVICE_ID
    // // requesting the values of holding registers starting at address
    // `address`
    // // for a total of `length` registers
    // if (!ModbusRTUClient.requestFrom(MODBUS_DEVICE_ID, HOLDING_REGISTERS,
    // address, length))
    // {
    //   // send an error message if there was a problem reading the response
    //   String error = ModbusRTUClient.lastError();
    //   request->send(500, "text/plain", "Error reading Modbus response: " +
    //   error);
    // }
    // else
    // {
    //   request->send(200, "text/plain", "OK");

    //   /*
    //   // create a JSON object to store the values of the holding registers
    //   DynamicJsonDocument doc(1024);
    //   JsonArray data = doc.createNestedArray("data");

    //   // add the values of the holding registers to the JSON object
    //   for (int i = 0; i < 10; i++)
    //   {
    //     data.add(modbus.readHoldingRegisters(5 + i));
    //   }

    //   // convert the JSON object to a string and send it as the response
    //   String output;
    //   serializeJson(doc, output);
    //   server.send(200, "application/json", output);
    //   */
    // }
}

// API endpoint to set the values of holding registers
void handleHTTPPost(AsyncWebServerRequest *request) {
    // // parse the request body as a JSON object
    // DynamicJsonDocument doc(1024);
    // deserializeJson(doc, server.arg("plain"));

    // // get the values of the holding registers from the JSON object
    // JsonArray data = doc["data"];

    // // send a modbus command to slave device with address 2
    // // setting the values of holding registers starting at address 5
    // for (int i = 0; i < data.size(); i++) {
    //   modbus.writeSingleRegister(2, 5 + i, data[i]);
    // }

    // // send a success message as the response
    // server.send(200, "text/plain", "Success");
}

// Define an onData handler function to receive the regular responses
// Arguments are Modbus server ID, the function code requested, the message data
// and length of it, plus a user-supplied token to identify the causing request
void handleModbusData(ModbusMessage response, uint32_t token) {
    if (tokenRequests.count(token) == 0) {
        return;
    }

    AsyncWebServerRequest *request = tokenRequests[token];

    uint16_t bufSize = 100 + (3 * response.size());
    char responseBuf[bufSize];

    //     //     OUTPUT_SERIAL_printf("Response: serverID=%d, FC=%d, Token=%08X, length=%d:\n",
    //     // response.getServerID(), response.getFunctionCode(), token,
    // response.size());
    snprintf(
        responseBuf,
        bufSize,
        "Response: serverID=%d, FC=%d, Token=%08X, length=%d:\n",
        response.getServerID(),
        response.getFunctionCode(),
        token,
        response.size());

    for (auto &byte : response) {
        size_t currentSize = strlen(responseBuf);
        snprintf(responseBuf + currentSize, bufSize - currentSize, "%02X ", byte);
    }

    OUTPUT_SERIAL_print(responseBuf);

    request->send(200, "text/plain", responseBuf);

    // Remove the request/token pair from the map
    tokenRequests.erase(token);
}

// Define an onError handler function to receive error responses
// Arguments are the error code returned and a user-supplied token to identify
// the causing request
void handleModbusError(Error error, uint32_t token) {
    if (tokenRequests.count(token) == 0) {
        return;
    }

    AsyncWebServerRequest *request = tokenRequests[token];

    char responseBuf[256];

    // ModbusError wraps the error code and provides a readable error message
    // for it
    ModbusError modbusErr(error);

    snprintf(responseBuf, 256, "Error response: %02X - %s\n", (int)modbusErr, (const char *)modbusErr);

    OUTPUT_SERIAL_print(responseBuf);

    int responseCode = 502;

    if ((int)modbusErr == 0xE0) {
        responseCode = 504;
    }

    request->send(responseCode, "text/plain", responseBuf);

    // Remove the request/token pair from the map
    tokenRequests.erase(token);
}

void send404(AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = request->beginResponse(SPIFFS, "/404.html");
    response->setCode(404);
    request->send(response);
}

// For debugging purposes, lists all available files in the internal file system
void handleListInternalFiles(AsyncWebServerRequest *request) {
    AsyncResponseStream *response = request->beginResponseStream("text/html");
    response->print("<!DOCTYPE html><html><head><title>Internal files</title></head><body><ul>");

    File root = SPIFFS.open("/");
    File file = root.openNextFile();
    while (file) {
        response->printf("<li><a href=\"%s\">%s - %s</a></li>", file.path(), file.name(), file.path());
        file = root.openNextFile();
    }

    response->print("</ul></body></html>");
    request->send(response);
}

void handleCaptivePortalMain(AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/index.html");
}

void handleCaptivePortalScan(AsyncWebServerRequest *request) {
    String json = "[";
    int n = WiFi.scanComplete();
    if (n == -2) {
        WiFi.scanNetworks(true);
    } else if (n) {
        for (int i = 0; i < n; ++i) {
            if (i) {
                json += ",";
            }
            json += "{";
            json += "\"rssi\":" + String(WiFi.RSSI(i));
            json += ",\"ssid\":\"" + WiFi.SSID(i) + "\"";
            json += ",\"bssid\":\"" + WiFi.BSSIDstr(i) + "\"";
            json += ",\"channel\":" + String(WiFi.channel(i));
            json += ",\"secure\":" + String(WiFi.encryptionType(i));
            // json += ",\"hidden\":"+String(WiFi.isHidden(i)?"true":"false");
            json += "}";
        }
        WiFi.scanDelete();
        if (WiFi.scanComplete() == -2) {
            WiFi.scanNetworks(true);
        }
    }
    json += "]";
    request->send(200, "application/json", json);
    json = String();
}

void handleCaptivePortalConfig(AsyncWebServerRequest *request) {
    if (!request->hasParam("ssid", true) || !request->hasParam("password", true)) {
        request->send(400, "application/json", "{\"error\":\"Missing parameters\"}");
        return;
    }

    String ssid = request->getParam("ssid", true)->value();
    String password = request->getParam("password", true)->value();

    OUTPUT_SERIAL_printf("Saving WiFi '%s' with password '%s'\n", ssid, password);

    preferences.putString("ssid", ssid);
    preferences.putString("password", password);

    request->send(200, "text/html", "OK");
    ESP.restart();
}

void startCaptivePortalServer() {
    if (!SPIFFS.begin(true)) {
        OUTPUT_SERIAL_println("An Error has occurred while mounting SPIFFS");
        return;
    }

    if (!WiFi.softAP("easun-wifi")) {
        OUTPUT_SERIAL_println("Failed to start WiFi in AP mode!");
        return;
    }

    OUTPUT_SERIAL_println("Captive Portal started with IP: " + WiFi.softAPIP().toString());

    if (!dnsServer.start(53, "*", WiFi.softAPIP())) {
        OUTPUT_SERIAL_println("WARNING: Failed to start DNS server in AP mode!");
    }

    captivePortalMode = true;

    // start WiFi scan so that we have data available when the user requests it
    WiFi.scanNetworks(/*async=*/true);

    // server.addHandler(new CaptiveRequestHandler()).setFilter(ON_AP_FILTER);
    // // only when requested from AP

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (ON_AP_FILTER) {
            request->send(SPIFFS, "/index.html");
        }
    });

    server.on("/scan", HTTP_GET, handleCaptivePortalScan).setFilter(ON_AP_FILTER);
    server.on("/config", HTTP_POST, handleCaptivePortalConfig).setFilter(ON_AP_FILTER);
    server.on("/_files", HTTP_GET, handleListInternalFiles).setFilter(ON_AP_FILTER);

    server.onNotFound([](AsyncWebServerRequest *request) {
        if (ON_AP_FILTER(request) && (request->method() == HTTP_GET)) {
            if (SPIFFS.exists(request->url())) {
                request->send(SPIFFS, request->url());
                return;
            }
        }
        send404(request);
    });

    server.begin();
}

void modbusSetup() {
    // Set up Serial2 connected to Modbus RTU
    MODBUS_SERIAL.begin(MODBUS_BAUDRATE, MODBUS_CONFIG, MODBUS_RX_PIN, MODBUS_TX_PIN);
    // MODBUS_SERIAL.begin(MODBUS_BAUDRATE, MODBUS_CONFIG);

    // Set up ModbusRTU client.
    // - provide onData handler function
    modbus.onDataHandler(&handleModbusData);

    // - provide onError handler function
    modbus.onErrorHandler(&handleModbusError);

    // Set message timeout to 2000ms
    modbus.setTimeout(MODBUS_TIMEOUT);

    // Start ModbusRTU background task
    modbus.begin();

    modbusEnabled = true;
}

void ledFlipState() {
    if (ledBlinkState == LOW) {
        ledBlinkState = HIGH;
    } else {
        ledBlinkState = LOW;
    }

    // set the LED with the ledState of the variable:
    digitalWrite(LED_BUILTIN, ledBlinkState);
}

void startServer() {
    // Connect to Wi-Fi
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());

    OUTPUT_SERIAL_print("Connecting to WiFi ..");

    while (WiFi.status() != WL_CONNECTED) {
        OUTPUT_SERIAL_print('.');
        ledFlipState();
        delay(ledConnectingInterval);
    }

    // Once connected we want to leave the LED on
    digitalWrite(LED_BUILTIN, LOW);

    OUTPUT_SERIAL_print(" Connected! IP: ");
    OUTPUT_SERIAL_println(WiFi.localIP());

    ledFlipState();  // Light the onboard LED to signal that we're connected to WiFi

    server.on("/disable", HTTP_GET, [](AsyncWebServerRequest *request) {
        modbus.end();
        request->send(200, "text/plain", "Modbus connection disabled");
    });

    server.on("/enable", HTTP_GET, [](AsyncWebServerRequest *request) {
        modbus.begin();
        request->send(200, "text/plain", "Modbus connection started");
    });

    server.on("/", HTTP_GET, handleHTTPGet);
    server.on("/", HTTP_POST, handleHTTPPost);

    server.on("/reset", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", "Device resetting to default settings and restarting");
        preferences.clear();
        ESP.restart();
    });

    server.on("/_files", HTTP_GET, handleListInternalFiles);

    server.onNotFound(send404);

    server.begin();
}

void setup() {
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, ledBlinkState);

    // initialize the serial communication at 9600 baud rate
    OUTPUT_SERIAL.begin(9600);

    // Initialize preferences store
    preferences.begin("credentials", false);

    ssid = preferences.getString("ssid", "");
    password = preferences.getString("password", "");

    OUTPUT_SERIAL_printf("\n\nSSID=\"%s\" PASSWORD=\"%s\"\n", ssid, password);

    if ((ssid == "") || (password == "")) {
        startCaptivePortalServer();
    } else {
        modbusSetup();
        startServer();
    }
}

void loop() {
    if (captivePortalMode) {
        dnsServer.processNextRequest();

        // Blink the onboard LED to signal we're in Captive Portal mode
        unsigned long currentMillis = millis();
        if (currentMillis - previousMillis >= ledCaptiveDelays[ledBlinkDelayNum]) {
            // save the last time you blinked the LED
            previousMillis = currentMillis;

            if (ledBlinkDelayNum == (CAPTIVE_DELAYS - 1)) {
                ledBlinkDelayNum = 0;
            } else {
                ledBlinkDelayNum += 1;
            }

            ledFlipState();
        }
    }
}