# FOC 无刷电机串口操作命令指南

本文档基于 `motor_driver/M创动工坊分享资料2.93.1/代码开源/ESP32-SimpleFOC-串级PID控制/ESP32-SimpleFOC-PID/src/main.cpp` 当前代码整理。

## 串口连接

开发板串口初始化参数：

```text
波特率: 115200
数据位: 8
校验位: None
停止位: 1
换行: CR、LF 或 CRLF 均可
```

使用 `minicom` 连接示例：

```bash
minicom -D /dev/ttyUSB1 -b 115200
```

退出 `minicom`：按 `Ctrl-A`，再按 `X`，确认退出。

## 启动信息

ESP32 固件启动并完成 FOC 初始化后，会输出：

```text
Custom cascade PID + SimpleFOC in TORQUE mode
```

该程序使用 SimpleFOC 的 `torque` 模式，库内置速度 PID 被关闭，实际控制由代码里的串级 PID 完成：

```text
目标角度(度) -> 外环位置 PID -> 目标速度(rad/s) -> 内环速度 PID -> Uq 电压 -> motor.move()
```

## 目标角度命令

直接发送数字即可设置目标角度，单位是度。

格式：

```text
<角度>
```

示例：

```text
0
90
-90
45.5
```

开发板成功接收后会回显：

```text
New target: 90.00 deg
```

常用转动测试序列：

```text
0
90
-90
0
```

说明：当前代码注释中注明多圈控制未完善，`360` 和 `720` 这类目标都会受单圈角度反馈限制影响，不应按完整多圈位置控制理解。

## PID 查询命令

查询当前 PID 参数使用 `G` 命令。

格式：

```text
G <环路>
```

环路参数：

```text
o: outer，外环位置 PID
i: inner，内环速度 PID
```

示例：

```text
G o
G i
```

典型回显：

```text
Outer PID -> kp: 1.00, ki: 0.00, kd: 1.00
Inner PID -> kp: 0.10, ki: 0.01, kd: 0.00
```

## PID 设置命令

可以在线设置外环或内环的 `P/I/D` 参数。

格式：

```text
<参数类型> <环路> <数值>
```

参数类型：

```text
P: 设置 kp
I: 设置 ki
D: 设置 kd
```

环路参数：

```text
o: outer，外环位置 PID
i: inner，内环速度 PID
```

示例：

```text
P o 1.0
I o 0.0
D o 1.0
P i 0.1
I i 0.01
D i 0.0
```

成功设置后的回显示例：

```text
Set outer PID P to 1.0000
Set inner PID I to 0.0100
```

## 当前代码默认 PID 参数

外环位置 PID：

```text
kp = 1.0
ki = 0.0
kd = 1.0
maxIntegral = 0.0
maxOutput = 12
```

内环速度 PID：

```text
kp = 0.1
ki = 0.01
kd = 0.0
maxIntegral = 1000
maxOutput = 6.0
```

## 命令解析规则和注意事项

每条命令需要以回车或换行结束。

如果命令第一个字符是数字或 `-`，程序会按目标角度解析，例如 `90`、`-45`。

如果命令第一个字符不是数字或 `-`，程序会按 PID 命令解析，例如 `G o`、`P i 0.1`。

PID 命令依赖固定字符位置解析：

```text
第 1 个字符: P/I/D/G
第 3 个字符: o/i
```

因此建议严格使用空格分隔格式，例如：

```text
G o
P o 1.0
```

不要写成：

```text
Go
P  o 1.0
P o=1.0
```

当前固件中的实时角度、速度、Uq 调试打印已被注释掉，所以串口默认只能看到启动信息、目标角度回显和 PID 查询/设置回显，不能直接通过串口确认电机实际位置。

## 推荐调试流程

1. 连接串口。

```bash
minicom -D /dev/ttyUSB1 -b 115200
```

2. 查询当前 PID 参数。

```text
G o
G i
```

3. 发送小幅目标角度测试。

```text
0
45
0
```

4. 再进行较大角度测试。

```text
0
90
-90
0
```

5. 如需调参，先小幅修改 PID，并观察电机响应。

```text
P o 0.8
D o 0.5
P i 0.08
```

6. 调参后再次查询确认。

```text
G o
G i
```

## 安全提醒

测试前确认电机固定牢靠，供电电压与驱动能力匹配。

首次调试建议从小角度和较低 PID 参数开始，避免电机突然大幅摆动。

代码中 `driver.voltage_power_supply` 和 `motor.voltage_limit` 均设置为 `12.0`，内环 PID 输出限幅为 `6.0V`。如硬件供电或电机规格不同，应先调整代码参数后再测试。
