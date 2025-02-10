#ifndef MAIN_H
#define MAIN_H
#include <Arduino.h>
#include <stdint.h>
#include <string>
#include "WebServer.h"

// Json document size to hold the commands send between client/server
#define COMMAND_DOC_SIZE 255
// Json document size to hold the config (depends on config size)
#define CONFIG_DOC_SIZE 20000

struct Config
{
  struct
  {
    uint16_t max_milliamps = 18000;
    float brightness = 1;
    int16_t motor_speed = -150;
  } power;

  struct
  {
    char ssid[32] = "yourAp";
    char password[64] = "1122334466";
    char hostname[64] = "dClock";
    uint16_t port = 80;
  } network;
};
// All cpp files that include this link to a single config struct
extern struct Config config;
#endif