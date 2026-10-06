#pragma once
#include <Arduino.h>
#include <SimpleFOC.h>
#include "commands.h"


// 声明全局变量
extern BLDCMotor motor;
extern float target_angle;
extern float current_speed;
extern float target_value;

void report_status();