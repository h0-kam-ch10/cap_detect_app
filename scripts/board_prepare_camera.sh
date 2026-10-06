#!/usr/bin/env bash
set -euo pipefail

kernel="$(uname -r)"
module_src="/lib/modules/${kernel}/kernel/bsp/drivers/vin/modules/sensor/imx219.ko"
module_patch="/tmp/imx219_2lane.ko"

if [[ ! -f "$module_patch" ]]; then
  sudo python3 -c 'from pathlib import Path
src=Path("'"$module_src"'")
dst=Path("'"$module_patch"'")
data=bytearray(src.read_bytes())
old=bytes.fromhex("01038052")
new=bytes.fromhex("41028052")
pos=data.find(old)
if pos < 0:
    raise SystemExit("imx219 4-lane pattern not found")
data[pos:pos+4]=new
dst.write_bytes(data)
print(f"patched {dst} at offset {pos}")'
fi

sudo rmmod vin_v4l2 2>/dev/null || true
sudo rmmod ov13850_mipi 2>/dev/null || true
sudo rmmod imx219 2>/dev/null || true
sudo rmmod vin_io 2>/dev/null || true

sudo insmod "/lib/modules/${kernel}/kernel/bsp/drivers/vin/vin_io.ko"
sudo insmod "$module_patch"
sudo modprobe vin_v4l2

echo "Camera nodes:"
ls -l /dev/video* /dev/media* /dev/v4l-subdev* 2>/dev/null || true

echo
echo "Loaded modules:"
lsmod | egrep 'imx219|vin_v4l2|vin_io|ov13850' || true

echo
echo "Media topology head:"
media-ctl -d /dev/media0 -p 2>/dev/null | sed -n '1,100p' || true
