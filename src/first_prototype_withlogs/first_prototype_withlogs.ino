/*
   Filename:    fist_prototype_withlogs.ino
   Created by:  Tiago Hatta
   Release:     1.0
   Date:        22/02/2018
   Description: This code is part of a project with a Battery IoT Collector. See more info: https://smartcampus.prefeitura.unicamp.br/ (in Portuguese).
                Measure distance with ultrasonic sensor and sends data to Konker Platform (https://www.konkerlabs.com/). It does the measurement each
                SLEEP_TIME hours (value defined below), otherwise it uses deep sleep mode to save battery. The protocol used is MQTT.
                The device also sends log messages to the cloud in case of any errors or warnings during its operation.
*/

#include <string.h> 
#include <PubSubClient.h>
#include <ESP8266WiFi.h>
#include <ArduinoJson.h>
// Library for SPFFIS manipulation
#include <espEasyMemory.h> // Credits to Luan Ferreira: https://github.com/Luanf/espEasyPersistentMemory

/* Ultrasonic Sensor */
// defines pins numbers
const int trigPin = 4;  //D2
const int echoPin = 14;  //D5
// variables for measure
long duration;
float distance;
// collector volume
const float MAX_HEIGHT = 50.0;
const float MIN_HEIGHT = 10.0;
const float RADIUS = 11.0;
const float MAX_VOLUME = (MAX_HEIGHT-MIN_HEIGHT)*PI*pow(RADIUS,2);
float volumeCalc, capacityCalc;

/* Wi-Fi Connection*/
const char* ssid     = "Ninja";
const char* password = "turtle10";

/* MQTT Protocol */
#define message_data  0
#define message_log   1
const char* mqtt_user = "mdal5af32cp0";
const char* mqtt_key = "eaARPRy9hcYh";
const char* mqtt_server = "mqtt.demo.konkerlabs.net";
const int mqtt_port = 1883;
char* mqtt_topic = "data/mdal5af32cp0/pub/";
char* mqtt_msg;
char* log_msg;
char bufferJ[256];

WiFiClient espClient;
PubSubClient client(espClient);

/* SPIFFS File System */
espEasyMemory memManager;
const int SLEEP_TIME = 6;

/**
   Callback function for receive message from MQTT Broker. This function is not being used at this moment.
   @param topic: MQTT topic subscribed, payload, length: length of the message
   @return
   @see https://www.ibm.com/support/knowledgecenter/en/SSFKSJ_7.1.0/com.ibm.mq.doc/tt60390_.htm

  void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
  }
*/

/**
   Returns received data (distance measure) in Json format
   @param device_id, metric, value
   @return Data in Json format
   @see https://konker.atlassian.net/wiki/display/DEV/Guia+de+Uso+da+Plataforma+Konker
*/
char *jsonMQTTmsgDATA(const char *device_id, float volume, float height, float capacity) {
  StaticJsonBuffer<200> jsonMQTT;
  JsonObject& jsonMSG = jsonMQTT.createObject();
  jsonMSG["deviceId"] = device_id;
  jsonMSG["volume[cm3]"] = volume;
  jsonMSG["height[cm]"] = height;
  jsonMSG["capacity[%]"] = capacity;
  jsonMSG.printTo(bufferJ, sizeof(bufferJ));
  return bufferJ;
}

/**
   Returns received data (log) in Json format
   @param device_id, log_msg
   @return Data in Json format
   @see https://konker.atlassian.net/wiki/display/DEV/Guia+de+Uso+da+Plataforma+Konker
*/
char *jsonMQTTmsgLOG(const char *device_id, char *log_msg) {
  StaticJsonBuffer<200> jsonMQTT;
  JsonObject& jsonMSG = jsonMQTT.createObject();
  jsonMSG["deviceId"] = device_id;
  jsonMSG["log_message"] = log_msg;
  jsonMSG.printTo(bufferJ, sizeof(bufferJ));
  return bufferJ;
}

/**
   Send any MQTT message to the broker. The message content can be either distance measure or log.
   @param tMessage: message type (message_data or message_log)
   @return
   @see https://konker.atlassian.net/wiki/display/DEV/Guia+de+Uso+da+Plataforma+Konker
*/
void sendMessageToBroker(int tMessage) {
  connectWifi();

  client.setServer(mqtt_server, 1883);

  if (!client.connected()) {
    // Loop until we're reconnected
    while (!client.connected()) {
      Serial.print("Attempting MQTT connection...");
      // Attempt to connect
      if (client.connect(mqtt_user, mqtt_user, mqtt_key)) {
        Serial.println("connected");
      } else {
        Serial.print("failed, rc=");
        Serial.print(client.state());
        Serial.println(" try again in 5 seconds");
        // Wait 5 seconds before retrying
        delay(5000);
      }
    }
  }

  if (tMessage == message_data) {
    strcat(mqtt_topic, "distance");
    mqtt_msg = jsonMQTTmsgDATA("Iot Battery Collector", volumeCalc, distance, capacityCalc);
  } else if (tMessage == message_log) {
    strcat(mqtt_topic, "logs");
    mqtt_msg = jsonMQTTmsgLOG("Iot Battery Collector", log_msg);
  }

  client.publish(mqtt_topic, mqtt_msg);
  client.loop();
  mqtt_topic = "data/mdal5af32cp0/pub/";

}

/**
   This function freezes the program until wifi is connected.
   WiFi.begin() needs to be called before this function.
   @param none
   @return
   @see http://arduino-esp8266.readthedocs.io/en/latest/esp8266wifi/readme.html
*/
void connectWifi() {
  int countDelay = 0;
  Serial.print("Your are connecting to: ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  Serial.print("Trying to connect");
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");

    // if the wifi connection delays more than 60s then generate log warning
    if (countDelay > 60) {
      log_msg = "WARNING: TAKING TOO MUCH TIME TO CONNECT WI-FI\n";
      sendMessageToBroker(message_log);
      countDelay = 0;
    } else {
      countDelay++;
    }
  }

  Serial.println();
  Serial.println("Device connected.");
}

/**
   Set trigger pin and get the time until the echo pin receive signal. Then the distance is calculated.
   @param none
   @return
   @see https://www.tindie.com/products/upgradeindustries/hc-sr05--hy-srf05-precision-ultrasonic-sensor/
*/
void measureDistance() {
  float sum = 0;
  int i;

  // gets the distance average from 10 measures
  for (i = 0; i < 10; i++) {
    // Clears the trigPin
    digitalWrite(trigPin, LOW);
    delayMicroseconds(2);

    // Sets the trigPin on HIGH state for 10 micro seconds
    digitalWrite(trigPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPin, LOW);

    // Reads the echoPin, returns the sound wave travel time in microseconds
    duration = pulseIn(echoPin, HIGH);

    // Calculating the distance
    distance = duration * 0.034 / 2;

    // just ignore unreasonable values
    if (distance <= MAX_HEIGHT) {
      sum += distance;
    } else {
      Serial.println("Weird value for distance, ignoring the last measure.");
      i--;
    }
    delayMicroseconds(500);
  }

  distance = sum / 10.0;

  // Prints the distance on the Serial Monitor
  Serial.print("Distance: ");
  Serial.println(distance);
}

void setup() {
  int sleepCounter = 0;

  // SPIFSS File System Initialization
  SPIFFS.begin();

  // Serial Initialization
  Serial.begin(115200); // Starts the serial communication
  Serial.setTimeout(2000);
  while (!Serial) {}
  Serial.println("");
  Serial.println("");

  // Recover the counter variable
  if (memManager.recoverIntVariable(&sleepCounter, "/store/counter") < 0 ) {
    Serial.println("Error! File did not exist!");
    log_msg = "WARNING: FILE DID NOT EXIST\n";
    sendMessageToBroker(message_log);
  }
  else {
    Serial.print("File was opened and counter variable was recovered. Its value is: ");
    Serial.println(sleepCounter);
  }

  // Just execute the measure and submission to cloud if it's the proper time
  if (sleepCounter >= SLEEP_TIME) {
    sleepCounter  = 0;

    // Ultrasonic Sensor Pins
    pinMode(trigPin, OUTPUT); // Sets the trigPin as an Output
    pinMode(echoPin, INPUT); // Sets the echoPin as an Input

    measureDistance();
    volumeCalc = (MAX_HEIGHT-distance)*PI*pow(RADIUS,2);
    capacityCalc = (volumeCalc/MAX_VOLUME)*100;
    sendMessageToBroker(message_data);

    // Send data to cloud service via MQQT
    Serial.println("Send data to Konker Platform...");
  }
  else {
    // if not enough time has passed then just increase the counter and return to deep sleep mode
    sleepCounter++;
  }

  // Try to store the new counter value
  if (memManager.storeIntVariable(sleepCounter, "/store/counter") < -1) {
    Serial.println("FATAL ERROR WITH SPIFFS!");
    log_msg = "ERROR: SLEEP COUNTER COULD NOT BE STORED IN MEMORY\n";
    sendMessageToBroker(message_log);

  }

  Serial.println("Done, let's sleep...");
  ESP.deepSleep(36e8, WAKE_RF_DEFAULT);  // 36e8 us = 1 hour
  //ESP.deepSleep(10e6, WAKE_RF_DEFAULT);
}

void loop() {}
