#include "WebServer.h"
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <WiFi.h>
#include "SD.h"

#include "Config.h"

#define WEBSOCKET_PORT 1337
#define ACCESSPOINT_IP 192, 168, 1, 1
#define ACCESSPOINT_SSID "FILESYS"
#define ACCESSPOINT_PASS ""
#define DNSSERVER_PORT 53

extern bool sendFileList;
extern uint8_t currentClientID;

namespace WebServer
{
  AsyncWebServer server = AsyncWebServer(config.network.port);
  WebSocketsServer webSocket = WebSocketsServer(WEBSOCKET_PORT);
  IPAddress APIP = IPAddress(ACCESSPOINT_IP);
  DNSServer dnsServer;
  boolean AP_MODE;

  void notFound(AsyncWebServerRequest *request)
  {
    request->send(404, "text/plain", "Not found");
  }

  // Handle file uploads
  void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final)
  {
    String path = request->url();
    if (!index)
    {
      // open the file on first call and store the file handle in the request object
      request->_tempFile = SD.open(path, "w");
      Serial.println("File Upload Start...");
    }
    if (len)
    {
      // stream the incoming chunk to the opened file
      request->_tempFile.write(data, len);
    }
    if (final)
    {
      request->_tempFile.close();
      Serial.println("Upload Complete: " + path + ",size: " + String(index + len));
      request->redirect("/");
    }
  }

  void begin()
  {
    // Start file system
    if (!LittleFS.begin())
    {
      Serial.println("Error mounting LittleFS");
    }
    // Start WiFi
    Serial.print("Starting WiFi");
    WiFi.begin(config.network.ssid, config.network.password);
    AP_MODE = true;
    for (uint8_t i = 0; i < 100; i++)
    {
      if (WiFi.status() == WL_CONNECTED)
      {
        AP_MODE = false;
        break;
      }
      Serial.print(".");
      delay(500);
    }
    Serial.println();

    // Start as Accesspoint
    if (AP_MODE)
    {
      Serial.println("Starting Wifi Access Point");
      WiFi.mode(WIFI_AP);
      // Quick hack to wait for SYSTEM_EVENT_AP_START
      delay(100);
      WiFi.softAPConfig(APIP, APIP, IPAddress(255, 255, 255, 0));
      WiFi.softAP(ACCESSPOINT_SSID, ACCESSPOINT_PASS);
      Serial.print("AP IP address: ");
      Serial.println(WiFi.softAPIP());
      dnsServer.start(DNSSERVER_PORT, "*", APIP);
    }

    // Start connected to local network
    if (!AP_MODE)
    {
      Serial.print("Connected to ");
      Serial.println(config.network.ssid);
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());
      // Start MDNS
      if (MDNS.begin(config.network.hostname), WiFi.localIP())
      {
        Serial.print("MDNS responder started. Hostname = ");
        Serial.println(config.network.hostname);
      }
      // Add service to MDNS-SD
      MDNS.addService("http", "tcp", 80);
    }

    server.onNotFound(notFound);
    server.onFileUpload(handleUpload);
    // On HTTP request for root, provide index.html file
    server.on("*", HTTP_GET, onIndexRequest);
    // Start web server
    server.begin();
    // Start WebSocket server and assign callback
    webSocket.onEvent(onWebSocketEvent);
    // Start web socket
    webSocket.begin();

    // Initialize Arduino OTA
    ArduinoOTA.setPort(3232);
    ArduinoOTA.setHostname(config.network.hostname);
    ArduinoOTA
        .onStart([]()
                 {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch";
      else
        type = "filesystem";
      Serial.println("Start updating " + type); })
        .onEnd([]()
               { Serial.println("\nEnd"); })
        .onProgress([](unsigned int progress, unsigned int total)
                    { Serial.printf("Progress: %u%%\r", (progress / (total / 100))); })
        .onError([](ota_error_t error)
                 {
              Serial.printf("Error[%u]: ", error);
              if (error == OTA_AUTH_ERROR)
                Serial.println("Auth Failed");
              else if (error == OTA_BEGIN_ERROR)
                Serial.println("Begin Failed");
              else if (error == OTA_CONNECT_ERROR)
                Serial.println("Connect Failed");
              else if (error == OTA_RECEIVE_ERROR)
                Serial.println("Receive Failed");
              else if (error == OTA_END_ERROR)
                Serial.println("End Failed"); });
    ArduinoOTA.begin();
  }

  // Handle network traffic
  void update()
  {
    if (AP_MODE)
    {
      // Handle dns requests in Access Point Mode only
      dnsServer.processNextRequest();
    }
    else if (WiFi.status() != WL_CONNECTED)
    {
      // Handle reconnects only when connected to Lan
      begin();
    }
    // Handle WebSocket data
    webSocket.loop();
    // Handle OTA update
    ArduinoOTA.handle();
  }

  void removeDir(fs::FS &fs, const char *path)
  {
    Serial.printf("Removing Dir: %s\n", path);
    if (fs.rmdir(path))
    {
      Serial.println("Dir removed");
    }
    else
    {
      Serial.println("rmdir failed");
    }
  }

  void fileDownloadDelete(AsyncWebServerRequest *request)
  {
    if (!request->hasParam("name") || !request->hasParam("action"))
    {
      request->send(400, "text/plain", "ERROR: 'name' and 'action' parameters are required");
      return;
    }
    String fileName = request->getParam("name")->value();
    String fileAction = request->getParam("action")->value();

    if (fileName.length() > 50 || fileName.indexOf("..") != -1)
    {
      request->send(400, "text/plain", "ERROR: Invalid file name");
      return;
    }

    if (fileName[0] != '/')
      fileName = "/" + fileName;

    if (!SD.exists(fileName))
    {
      request->send(404, "text/plain", "ERROR: File does not exist");
      return;
    }

    if (fileAction.equalsIgnoreCase("download"))
    {
      request->send(SD, fileName, "application/octet-stream");
    }
    else if (fileAction.equalsIgnoreCase("delete"))
    {
      if (SD.remove(fileName))
      {
        request->send(200, "text/plain", "Deleted File: " + fileName);
      }
      else
      {
        request->send(500, "text/plain", "ERROR: Unable to delete file");
      }
    }
    else
    {
      request->send(400, "text/plain", "ERROR: Invalid action parameter");
    }
  }

  void removeCreateDir(AsyncWebServerRequest *request)
  {

    if (!request->hasParam("name") || !request->hasParam("action"))
    {
      request->send(400, "text/plain", "ERROR: 'name' and 'action' parameters are required");
      return;
    }
    String fileName = request->getParam("name")->value();
    String fileAction = request->getParam("action")->value();

    if (fileName.length() > 50 || fileName.indexOf("..") != -1)
    {
      request->send(400, "text/plain", "ERROR: Invalid file name");
      return;
    }

    if (fileName[0] != '/')
      fileName = "/" + fileName;

    if (fileAction.equalsIgnoreCase("create"))
    {
      if (SD.mkdir(fileName))
      {
        Serial.println("Dir created");
        request->send(200, "text/plain", "Dir Created: " + fileName);
      }
      else
      {
        Serial.println("mkdir failed");
        request->send(500, "text/plain", "ERROR: Unable to create Dir");
      }
    }
    else if (fileAction.equalsIgnoreCase("delete"))
    {
      if (SD.rmdir(fileName))
      {
        request->send(200, "text/plain", "Deleted Dir: " + fileName);
      }
      else
      {
        request->send(500, "text/plain", "ERROR: Unable to delete Dir");
      }
    }
    else
    {
      request->send(400, "text/plain", "ERROR: Invalid action parameter");
    }
  }

  void onIndexRequest(AsyncWebServerRequest *request)
  {
    IPAddress remote_ip = request->client()->remoteIP();
    Serial.println("[" + remote_ip.toString() + "] HTTP GET request of " +
                   request->url());
    if (request->url().equals("/"))
      request->send(LittleFS, "/index.html", "text/html");
    else if (request->url().equals("/file"))
      fileDownloadDelete(request);
    else if (request->url().equals("/dir"))
      removeCreateDir(request);
    else if (LittleFS.exists(request->url()))
      if (request->url().endsWith(".html"))
      {
        request->send(LittleFS, request->url(), "text/html");
      }
      else if (request->url().endsWith(".css"))
      {
        request->send(LittleFS, request->url(), "text/css");
      }
      else if (request->url().endsWith(".json"))
      {
        request->send(LittleFS, request->url(), "application/json");
      }
      else if (request->url().endsWith(".ico"))
      {
        request->send(LittleFS, request->url(), "image/x-icon");
      }
      else if (request->url().endsWith(".png"))
      {
        request->send(LittleFS, request->url(), "image/png");
      }
      else if (request->url().endsWith(".js"))
      {
        request->send(LittleFS, request->url(), "text/javascript");
      }
      else
      {
        request->send(404, "text/plain", "Mime-Type Not Found");
      }
    else
    {
      request->redirect("/index.html");
    }
  }

  void broadcast(uint8_t *payload)
  {
    webSocket.broadcastTXT(payload);
  }

  void wsSendTXT(uint8_t client_num, String payload)
  {
    webSocket.sendTXT(client_num, payload);
  }

  void onWebSocketEvent(uint8_t client_num, WStype_t type, uint8_t *payload,
                        size_t length)
  {
    switch (type)
    {
      // Client has disconnected
    case WStype_DISCONNECTED:
    {
      Serial.printf("[%u] Disconnected!\n", client_num);
    }
    break;
      // New client has connected
    case WStype_CONNECTED:
    {
      Serial.printf("[%u] Connection from %s\n", client_num, webSocket.remoteIP(client_num).toString().c_str());
    }
    break;
      // Text received from a connected client
    case WStype_TEXT:
      if (strcmp((char *)payload, "listFiles") == 0)
      {
        sendFileList = true;
        currentClientID = client_num;
      }
      break;
    case WStype_BIN:
    case WStype_FRAGMENT_TEXT_START:
    case WStype_FRAGMENT_BIN_START:
    case WStype_FRAGMENT:
    case WStype_FRAGMENT_FIN:
    case WStype_ERROR:
    case WStype_PING:
    case WStype_PONG:
      break;
    }
  }
} // namespace WebServer