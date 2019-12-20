#include <FS.h>
#include <iostream>
#include <cstring>
#include <string>
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESPAsyncWiFiManager.h>         //https://github.com/tzapu/WiFiManager
#include <PubSubClient.h>
#include <EEPROM.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

const int RELAYTOPICPREFIXIDX = 0;
const int CONFIRMRELAYTOPICPREFIXIDX = 50;
const int USERNAMEIDX = 100;
const int PASSWORDIDX = 150;
const int MQTTSERVERIDX = 200;
const int PORTIDX = 250;
const int ONVALUEIDX = 255;
const int OFFVALUEIDX = 305;
const int CLIENTIDIDX = 355;
const char template_html[] PROGMEM = "<!DOCTYPE HTML><html><head><title>Power Center Admin - MQTT Setup</title><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"><link rel=\"stylesheet\" href=\"https://maxcdn.bootstrapcdn.com/bootstrap/3.4.1/css/bootstrap.min.css\"></head><body><div class=\"container\">%content%</div><script src=\"https://ajax.googleapis.com/ajax/libs/jquery/3.4.1/jquery.min.js\"></script><script src=\"https://maxcdn.bootstrapcdn.com/bootstrap/3.4.1/js/bootstrap.min.js\"></script></body></html>";
const char index_html[] PROGMEM = "Saved";
const byte DNS_PORT = 53;
bool hasSetOutputs = false;
bool restartRequested = false;
int mqttConnectAttempts = 0;
AsyncWebServer server(80);
DNSServer dns;
void mqttCallBack(char* topic, byte* payload, unsigned int length);
int relayPins[8] = {D1, D2, D3, D4, D5, D6, D7, D8};
String mqtt_server;
String mqtt_port;
String mqtt_username;
String mqtt_password;
String topicPrefix;
String confirmTopicPrefix;
String offValue;
String onValue;
String clientId;

WiFiClient wifiClient;
PubSubClient client(wifiClient);

void mqttCallBack(char* topic, byte* payload, unsigned int length) {
  //convert topic to string to make it easier to work with
  String topicStr = topic;
  String payloadStr = (char*)payload;
  String realPayload = payloadStr.substring(0, length);
  String relayNumber = topicStr.substring(topicPrefix.length() - 1, topicStr.length());
  Serial.println("Callback update.");
  Serial.print("Topic: ");
  Serial.print(topicStr);
  Serial.print( " Payload: ");
  Serial.println(realPayload);
  bool isGoodData = true;
  int pinState = HIGH; //HIGH is OFF
  if (realPayload == onValue) {
    pinState = LOW;
  } else if (realPayload == offValue)
  {
    pinState = HIGH;
  }  else  {
    isGoodData = false;
    Serial.println("Don't understand that payload value.");
  }
  if (isGoodData)
  {
    digitalWrite(relayPins[relayNumber.toInt() - 1], pinState);
    if (confirmTopicPrefix.length() > 0)
    {
      String pubTopic = confirmTopicPrefix.substring(0, confirmTopicPrefix.length() - 1) + relayNumber;
      client.publish((char*)pubTopic.c_str(), (char*)realPayload.c_str());
      Serial.print("Published confirm to:");
      Serial.print(pubTopic);
      Serial.print(" with payload '");
      Serial.print(realPayload);
      Serial.println("'.");
    }
    outputPinState(relayNumber.toInt() - 1);
  }
}

void outputPinState(int idx)
{
  int ch = idx + 1;
  int val = digitalRead(relayPins[idx]);
  String pinStateTxt = "LOW";
  String onOffStateTxt = "ON";
  Serial.print("Relay CH");
  Serial.print(ch);
  Serial.print("(");
  Serial.print(relayPins[idx]);
  Serial.print(") = ");
  if (val == HIGH)
  {
    pinStateTxt = "HIGH";
    onOffStateTxt = "OFF";
  }
  Serial.print(pinStateTxt);
  Serial.print(" ");
  Serial.println(onOffStateTxt);

}
String read_string(int l, int p) {
  String temp;
  for (int n = p; n < l + p; ++n)
  {
    if (char(EEPROM.read(n)) != ';') {
      if (isWhitespace(char(EEPROM.read(n)))) {
        //do nothing
      } else temp += String(char(EEPROM.read(n)));

    } else n = l + p;

  }
  return temp;
}
void setupOTA()
{
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_FS
      type = "filesystem";
    }

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();
}
void setupPins()
{
  if (!hasSetOutputs)
  {
    for (int i = 0; i < 8; i++)
    {
      pinMode(relayPins[i], OUTPUT);
      digitalWrite(relayPins[i], HIGH);
    }
    hasSetOutputs = true;
  }
}

//write to memory
void write_EEPROM(String x, int pos) {
  for (int n = pos; n < x.length() + pos; n++) {
    EEPROM.write(n, x[n - pos]);
  }
}

void notFound(AsyncWebServerRequest *request) {
  request->send(404, "text/plain", "Not found");
}
String savedProcessor(const String& var)
{
  return F("<h2>Save successful!</h2> <a href=\"/\" class=\"btn btn-primary\">Back</a>");
}
void handleRelayStatus(AsyncWebServerRequest *request)
{
  String json = "[";
  for (int i = 0; i <8; ++i){
      int pinStatus = digitalRead(relayPins[i]);
      String logicState = pinStatus == HIGH ? "OFF" : "ON";
      String pinState = pinStatus == HIGH ? "HIGH" : "LOW";
      if(i!=0)
      {
        json += ",";
      }
      json += "{";
      json += "\"relayNumber\":"+String((i+1));
      json += ",\"espPinNumber\":"+String(relayPins[i]);
      json += ",\"wemosPinNumber\":\"D"+String(i+1)+"\"";
      json += ",\"status\":\""+logicState+"\"";
      json += ",\"pinState\":\""+pinState+"\"";
      json += "}";
    }
  json += "]";
  request->send(200, "application/json", json);
  json = String();
}
void handleRelayToggle(AsyncWebServerRequest *request)
{
  if (request->hasParam("relay")) {
    String value = request->getParam("relay")->value();
    int idx = value.toInt()-1;
    int pinStatus = digitalRead(relayPins[idx]);
    if(pinStatus == HIGH)
    {
      digitalWrite(relayPins[idx], LOW);
    }
    else
    {
      digitalWrite(relayPins[idx], HIGH);
    }
    outputPinState(idx);
    handleRelayStatus(request);
  }
  request->send(400, "application/text", "invalid data");
}
void handleMqttSave(AsyncWebServerRequest *request)
{
  String value;
  //Save the topic prefix for the MQTT Server
  if (request->hasParam("topicprefix", true)) {
    value = request->getParam("topicprefix", true)->value();
    value += ";";
    write_EEPROM(value, RELAYTOPICPREFIXIDX);
  }
  //Save the topic prefix for the MQTT Server
  if (request->hasParam("confirmtopicprefix", true)) {
    value = request->getParam("confirmtopicprefix", true)->value();
    value += ";";
    write_EEPROM(value, CONFIRMRELAYTOPICPREFIXIDX);
  }
  //Save the username for the MQTT Server
  if (request->hasParam("username", true)) {
    value = request->getParam("username", true)->value();
    value += ";";
    write_EEPROM(value, USERNAMEIDX);
  }
  //Save the password for the MQTT Server
  if (request->hasParam("password", true)) {
    value = request->getParam("password", true)->value();
    value += ";";
    write_EEPROM(value, PASSWORDIDX);
  }
  //Save the DNS/IP address for the MQTT server
  if (request->hasParam("server", true)) {
    value = request->getParam("server", true)->value();
    value += ";";
    write_EEPROM(value, MQTTSERVERIDX);
  }
  //Save the On value for relay topics
  if (request->hasParam("onvalue", true)) {
    value = request->getParam("onvalue", true)->value();
    value += ";";
    write_EEPROM(value, ONVALUEIDX);
  }
  //Save the Off value for the relay topics
  if (request->hasParam("offvalue", true)) {
    value = request->getParam("offvalue", true)->value();
    value += ";";
    write_EEPROM(value, OFFVALUEIDX);
  }
  //Save the mqtt port number
  if (request->hasParam("port", true)) {
    value = request->getParam("port", true)->value();
    value += ";";
    write_EEPROM(value, PORTIDX);
  }
  //Save the client id
  if (request->hasParam("clientId", true)) {
    value = request->getParam("clientId", true)->value();
    value += ";";
    write_EEPROM(value, CLIENTIDIDX);
  }

  EEPROM.commit();
  
  request->send_P(200, "text/html", template_html, savedProcessor);
}
void reloadReconnect()
{
  delay(100);
  mqttConnectAttempts = 0; //Give MQTT another attempt;
  loadDataFromEEPROM();
  delay(100);
  if (client.connected())
  {
    Serial.println("MQTT currently connected...Disconnecting...");
    client.disconnect();
  }
  reconnect();
}
void handleWifiSetup()
{
  WiFi.hostname("ESP_PowerCenter");
  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  AsyncWiFiManager wifiManager(&server, &dns);

  //exit after config instead of connecting
  wifiManager.setBreakAfterConfig(true);

  //reset settings - for testing
  //wifiManager.resetSettings();

  //tries to connect to last known settings
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP" with password "password"
  //and goes into a blocking loop awaiting configuration
  if (!wifiManager.autoConnect("PowerCenterAP", "password")) {
    Serial.println("failed to connect, we should reset as see if it connects");
    delay(3000);
    ESP.reset();
    delay(5000);
  }

  //if you get here you have connected to the WiFi
  Serial.println("connected...");
  Serial.println("local ip");
  Serial.println(WiFi.localIP());
}
void handleWebServerSetup()
{
  //handle HTTP requests
  server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(SPIFFS, "/www/index.htm");
  });
  server.on("/reset", HTTP_GET, [](AsyncWebServerRequest * request) {
    Serial.println("Reboot requested");
    restartRequested = true;
    request->send(SPIFFS, "/www/reset.htm");    
  });
  server.on("/mqttsaved", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(SPIFFS, "/www/mqttsaved.htm");    
  });
  server.on("/reconnect", HTTP_GET, [](AsyncWebServerRequest * request) {
    reloadReconnect();
    request->send_P(200, "text/plain", "resetting");
  });
  server.on("/relay/toggle", HTTP_GET, handleRelayToggle);
  server.on("/mqttsetup", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(SPIFFS, "/www/mqttsetup.htm", String(), false, mqttSetupProcessor);
  });
  server.on("/mqttsetupsave", HTTP_POST, handleMqttSave);
  server.on("/relay/status", HTTP_GET, handleRelayStatus);
  server.onNotFound(notFound);
  server.serveStatic("/", SPIFFS, "/www");
  server.begin();
}
String mqttSetupProcessor(const String& var)
{
  if(var == "mqtt_server")
  {
      return mqtt_server;
  }
  if(var == "mqtt_port")
  {
      return mqtt_port;
  }
  if(var == "mqtt_username")
  {
      return mqtt_username;
  }
  if(var == "mqtt_password")
  {
      return mqtt_password;
  } 
  if(var == "topicprefix")
  {
    return topicPrefix;
  } 
  if(var == "confirmtopicprefix")
  {
      return confirmTopicPrefix;
  } 
  if(var == "onvalue")
  {
      return onValue;
  }
  if(var == "offvalue")
  {
      return offValue;
  } 
  if(var == "clientid")
  {
      return clientId;
  }
  return String();
}
void loadDataFromEEPROM()
{
  topicPrefix = read_string(50, RELAYTOPICPREFIXIDX);
  onValue = read_string(50, ONVALUEIDX);
  offValue = read_string(50, OFFVALUEIDX);
  mqtt_server = read_string(50, MQTTSERVERIDX);
  mqtt_username = read_string(50, USERNAMEIDX);
  mqtt_password = read_string(50, PASSWORDIDX);
  mqtt_port = read_string(4, PORTIDX);
  confirmTopicPrefix = read_string(50, CONFIRMRELAYTOPICPREFIXIDX);
  clientId = read_string(50, CLIENTIDIDX);
}
void setup() {
  Serial.begin(115200);
  EEPROM.begin(512);
  SPIFFS.begin();
  loadDataFromEEPROM();
  handleWifiSetup();
  setupOTA();
  handleWebServerSetup();
  client.setCallback(mqttCallBack);
}
void loop() {
  ArduinoOTA.handle();
  setupPins();
  //reconnect if connection is lost
  if (!client.connected() && WiFi.status() == 3) {
    reconnect();
  }
  //maintain MQTT connection
  client.loop();
  //MUST delay to allow ESP8266 WIFI functions to run
  if(restartRequested)
  {
    restartRequested = false;
    delay(3000);
    ESP.reset();
    delay(5000);
  }
  delay(100);
}

void reconnect() {
  //attempt to connect to the wifi if connection is lost
  if (WiFi.status() != WL_CONNECTED) {
    //debug printing
    Serial.print("Connecting");
    //loop while we wait for connection
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }

    //print out some more debug once connected
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
  }
  connectToMQTT();
}
void connectToMQTT()
{
  //make sure we are connected to WIFI before attemping to reconnect to MQTT
  if (WiFi.status() == WL_CONNECTED) {
    // Loop until we're reconnected to the MQTT server
    while (!client.connected() && mqttConnectAttempts < 3) {
      Serial.print("Attempting MQTT connection...");
      Serial.println("Server: " + mqtt_server);
      client.setServer((char*)mqtt_server.c_str(), mqtt_port.toInt());
      //if connected, subscribe to the topic(s) we want to be notified about
      if (client.connect((char*) clientId.c_str(), (char*)mqtt_username.c_str(), (char*)mqtt_password.c_str())) {
        Serial.println("\tMQTT Connected");
        for (int i = 1; i < 9; i++)
        {
          String topic = topicPrefix.substring(0, topicPrefix.length() - 1) + String(i);
          client.subscribe((char*)topic.c_str());
          Serial.print("Subscribed to: ");
          Serial.print(topic);
          Serial.print(" at ");
          Serial.println(mqtt_server);
        }
        mqttConnectAttempts = 0;
      }

      //otherwise print failed for debugging
      else {
        mqttConnectAttempts++;
        Serial.print("\tFailed to connect to MQTT try#");
        Serial.print(mqttConnectAttempts);
        Serial.print(". Check MQTT Settings at ");
        Serial.println(WiFi.localIP());
        delay(1000);
      }
    }
    if (mqttConnectAttempts == 3)
    {
      Serial.println("No more attempts will be made to connect to MQTT. Reset to try again");
    }
  }
}
