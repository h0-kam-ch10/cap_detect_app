#include <Arduino.h>
#include <SimpleFOC.h>

// ——— 您自己的 PID ——//

// 单级 PID 结构体
typedef struct
{
    float kp, ki, kd;
    float error, lastError;
    float integral, maxIntegral;
    float output, maxOutput;
} PID;

// 串级 PID 结构体
typedef struct
{
    PID outer;    // 位置环
    PID inner;    // 速度环
    float output; // 最终 Uq 电压
} CascadePID;

// 初始化单级 PID
void PID_Init(PID *pid, float p, float i, float d, float maxI, float maxOut)
{
    pid->kp = p;
    pid->ki = i;
    pid->kd = d;
    pid->error = pid->lastError = 0;
    pid->integral = 0;
    pid->maxIntegral = maxI;
    pid->maxOutput = maxOut;
    pid->output = 0;
}

// 一次 PID 计算
void PID_Calc(PID *pid, float ref, float fdb)
{
    pid->lastError = pid->error;
    pid->error = ref - fdb;
    float P = pid->kp * pid->error;
    pid->integral += pid->ki * pid->error;
    // 积分限幅
    if (pid->integral > pid->maxIntegral)
        pid->integral = pid->maxIntegral;
    if (pid->integral < -pid->maxIntegral)
        pid->integral = -pid->maxIntegral;
    float I = pid->integral;
    float D = pid->kd * (pid->error - pid->lastError);
    pid->output = P + I + D;
    // 输出限幅
    if (pid->output > pid->maxOutput)
        pid->output = pid->maxOutput;
    if (pid->output < -pid->maxOutput)
        pid->output = -pid->maxOutput;
}

// 串级调用
void PID_CascadeCalc(CascadePID *cp, float posRef, float posFdb, float velFdb)
{
    PID_Calc(&cp->outer, posRef, posFdb);
    PID_Calc(&cp->inner, cp->outer.output, velFdb);
    cp->output = cp->inner.output;
}

// ——— SimpleFOC 对象声明 ——//

// 硬件引脚
const int pinPWM_A = 32;
const int pinPWM_B = 33;
const int pinPWM_C = 25;
const int pinEn = 12;

// FOC 对象
BLDCMotor motor = BLDCMotor(7); // 7 极对数
BLDCDriver3PWM driver = BLDCDriver3PWM(pinPWM_A, pinPWM_B, pinPWM_C, pinEn);
MagneticSensorI2C sensor = MagneticSensorI2C(AS5600_I2C);

// 串级 PID 实例
CascadePID pid;

// 用于速度计算
float last_angle = 0;
unsigned long last_time = 0;

// 目标角度（度）
float target_deg = 0;

void setup()
{
    Serial.begin(115200);

    // 1) 驱动初始化
    driver.voltage_power_supply = 12.0;
    driver.init();

    // 2) 传感器初始化
    sensor.init();
    motor.linkSensor(&sensor);

    // 3) 链接驱动与电机
    motor.linkDriver(&driver);
    motor.voltage_limit = 12.0;

    // 4) 选择 torque 模式，不使用库内置 PID
    motor.controller = MotionControlType::torque;
    // 禁掉库内速度 PI，避免与您的 inner PID 干扰
    motor.PID_velocity.P = 0;
    motor.PID_velocity.I = 0;
    motor.PID_velocity.D = 0;
    motor.LPF_velocity.Tf = 0.01; // 低通滤波时间常数

    // 5) FOC 初始化
    motor.init();
    motor.initFOC();

    // 6) 自己的 PID 参数，需根据电机与负载调参
    // 外环：位置→输出速度 (rad/s)
    PID_Init(&pid.outer, 1, 0.0, 1.0, 0.0, 12);
    // 内环：速度→输出 Uq 电压 (V)
    PID_Init(&pid.inner, 0.1, 0.01, 0.00, 1000, 6.0);

    // 初始化定时与角度
    last_time = micros();
    last_angle = motor.shaftAngle();
    delay(1000); // 等待串口稳定
    Serial.println("Custom cascade PID + SimpleFOC in TORQUE mode");
}

//串口在线调试 PID 参数
//输入格式 例如：
// P o 1.0  // 设置外环位置 PID 的 kp 为 1.0
// I o 0.5  // 设置外环位置 PID 的 ki 为 0.5
// D o 0.1  // 设置外环位置 PID 的 kd 为 0.1
// P i 0.1  // 设置内环速度 PID 的 kp 为 0.1
void handlePIDCommand(String cmd) {
  cmd.trim();
  if (cmd.length() < 3) return;

  char type = toupper(cmd.charAt(0)); // 'P'/'I'/'D'/'G'
  char loop = tolower(cmd.charAt(2)); // 'o'/'i'

  PID *targetPID = (loop == 'o') ? &pid.outer : &pid.inner;

  if (type == 'G') {
    Serial.print(loop == 'o' ? "Outer" : "Inner");
    Serial.print(" PID -> kp: ");
    Serial.print(targetPID->kp);
    Serial.print(", ki: ");
    Serial.print(targetPID->ki);
    Serial.print(", kd: ");
    Serial.println(targetPID->kd);
    return;
  }

  int sep = cmd.indexOf(' ', 3);
  if (sep < 0) return;

  float val = cmd.substring(sep + 1).toFloat();
  if (isnan(val)) return;

  switch (type) {
    case 'P': targetPID->kp = val; break;
    case 'I': targetPID->ki = val; break;
    case 'D': targetPID->kd = val; break;
    default: return;
  }

  Serial.print("Set ");
  Serial.print(loop == 'o' ? "outer" : "inner");
  Serial.print(" PID ");
  Serial.print(type);
  Serial.print(" to ");
  Serial.println(val, 4);
}

// 读取串口命令，输入角度时只有360度有效，多圈未完善，比如360度和720度都是1圈
void readSerialCommand() {
  static String inputString = "";
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (inputString.length() > 0) {
        if (isDigit(inputString.charAt(0)) || inputString.charAt(0) == '-') {
          float d = inputString.toFloat();
          if (!isnan(d)) {
            target_deg = d;
            Serial.print("New target: ");
            Serial.print(target_deg);
            Serial.println(" deg");
          }
        } else {
          handlePIDCommand(inputString);  // 解析 PID 指令
        }
        inputString = "";
      }
    } else {
      inputString += c;
    }
  }
}

void loop()
{
    // 1) 串口读取新目标（度）
    readSerialCommand();
    // 2) FOC 电角度计算
    motor.loopFOC();

    // 3) 读取机械角与速度
    float now = micros();
    float dt = (now - last_time) * 1e-6; // s
    float ang = motor.shaftAngle();        // rad
    float vel = (ang - last_angle) / dt; // rad/s
    last_time = now;
    last_angle = ang;

    // 4) 外环：目标角度 → 目标弧度
    float pos_ref = target_deg * M_PI / 180.0;

    // 5) 串级 PID 计算
    PID_CascadeCalc(&pid, pos_ref, ang, vel);

    // 6) 内环输出即 q 轴电压，直接当作 torque 模式下的 target
    motor.target = pid.output;

    // 7) 更新输出
    motor.move();

    // // 8) 串口实时打印调试
    // // 非阻塞打印逻辑
    // static unsigned long lastPrint = 0;
    // unsigned long now2 = millis();
    // if (now2 - lastPrint >= 300)
    // { // 每 300ms 打印一次
    //     lastPrint = now2;
    //     Serial.print("ang:");
    //     Serial.print(ang * 180 / M_PI, 1);
    //     Serial.print("deg vel:");
    //     Serial.print(vel, 2);
    //     Serial.print("uq:");
    //     Serial.println(pid.output, 3);
    // }
}
