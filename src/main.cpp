#include "Arduino.h"
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <WebSocketsServer.h>
#include "SD.h"
#include "SPI.h"
#include "Config.h"
#include "WebServer.h"

#define LED_PIN 2
/*------------------------------------------------------------------------------
 * Globals
 *----------------------------------------------------------------------------*/
// Global configuration parameters
Config config;
// TaskHandle for running task on different core
TaskHandle_t WEB_Task;
void web_task(void *);

// Flags and variables
bool sendFileList = false;
uint8_t currentClientID = 0;
bool SD_present = false;

void printDirectoryWebSocket(File dir, uint8_t clientID);

// Setup function
void setup()
{
  Serial.begin(115200);
  delay(2);
  // Initialize web server communication
  WebServer::begin();

  // Initialize SD card
  if (!SD.begin(5))
  {
    Serial.println("SD Card Mount Failed");
    return;
  }
  SD_present = true;
  Serial.println("SD Card Mount Successful");
  // Create task on core 0
  xTaskCreatePinnedToCore(web_task, "WEB", 20000, NULL, 8, &WEB_Task, 0);
}

// Main loop
void loop()
{
  if (sendFileList)
  {
    sendFileList = false;
    File root = SD.open("/");
    if (root)
    {
      Serial.println("File Start Sending....");
      WebServer::wsSendTXT(currentClientID, "{\"SD\":{");
      printDirectoryWebSocket(root, currentClientID);
      WebServer::wsSendTXT(currentClientID, "}}");
      root.close();
      Serial.println("File Sent.");
    }
  }
}

/*------------------------------------------------------------------------------
 * Webserver
 *----------------------------------------------------------------------------*/
void web_task(void *parameter)
{
  Serial.printf("Webserver running on core %d\n", xPortGetCoreID());
  static float LED_PHASE = 0;
  while (true)
  {
    // Prevents watchdog timeout
    vTaskDelay(1);
    // Check for Web server events
    WebServer::update();
    // Blink led button
    LED_PHASE += 0.001f;
    analogWrite(LED_PIN, fabs(sinf(LED_PHASE) * 255));
  }
}

// Send directory structure over WebSocket
void printDirectoryWebSocket(File dir, uint8_t clientID)
{
  bool isFirstEntry = true;
  while (true)
  {
    File entry = dir.openNextFile();
    if (!entry)
      break;

    if (!isFirstEntry)
      WebServer::wsSendTXT(clientID, ",");
    isFirstEntry = false;

    String jsonEntry = "\"" + String(entry.name()) + "\": ";
    if (entry.isDirectory())
    {
      jsonEntry += "{";
      WebServer::wsSendTXT(clientID, jsonEntry);
      printDirectoryWebSocket(entry, clientID);
      WebServer::wsSendTXT(clientID, "}");
    }
    else
    {
      time_t t = entry.getLastWrite();
      struct tm *tmstruct = localtime(&t);
      jsonEntry += "{\"type\": \"file\", \"size\": " + String(entry.size()) +
                   ", \"lastModified\": \"" + String((tmstruct->tm_year) + 1900) + "-" +
                   String((tmstruct->tm_mon) + 1) + "-" +
                   String(tmstruct->tm_mday) + " " +
                   String(tmstruct->tm_hour) + ":" +
                   String(tmstruct->tm_min) + ":" +
                   String(tmstruct->tm_sec) + "\"}";
      WebServer::wsSendTXT(clientID, jsonEntry);
      jsonEntry = "";
    }
    entry.close();
  }
}