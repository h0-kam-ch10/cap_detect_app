#pragma once
#include <Arduino.h>
#include <SimpleFOC.h>

struct PIDParams {
    float P;
    float I;
    float D;
    float LPF_Tf;
  };
// 声明全局变量（在main.cpp中定义）
extern PIDParams pos_pid;
extern PIDParams vel_pid;

enum ControlMode { POSITION, VELOCITY, TORQUE };
extern BLDCMotor motor;
extern float target_angle;
extern ControlMode current_mode;


// 串口指令处理函数声明
void process_serial();
void handle_command(String cmd);
void print_help();