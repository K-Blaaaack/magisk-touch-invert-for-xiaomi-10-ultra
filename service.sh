#!/system/bin/sh
# service.sh - magisk module service
# 以 root 在 boot 时启动 touch_remap 守护进程

BIN_PATH="/data/adb/modules/touch-invert/bin/touch_remap"
LOG="/data/adb/modules/touch-invert/touch_remap.log"

# 如果模块安装到其他目录，请修改 BIN_PATH
if [ ! -x "$BIN_PATH" ]; then
  echo "$(date) touch_remap not found or not executable: $BIN_PATH" >> "$LOG"
  exit 1
fi

# 等待系统 input/dev 可用
sleep 2

# 你想监听的设备路径（可按需改）
# 默认为 /dev/input/event5；如果不确定可改为自动按 name 匹配
TARGET_INPUT="/dev/input/event5"

# 如果目标不存在，尝试用设备名匹配 /dev/input/by-path 或列出 input
if [ ! -e "$TARGET_INPUT" ]; then
  # 这里尝试按设备名匹配 "fts" 或 "xiaomi-touch" 等
  for ev in /dev/input/event*; do
    if [ -e "$ev" ]; then
      name=$(getevent -pl "$ev" 2>/dev/null | awk -F: '/name/{gsub(/"/,"",$2); print $2; exit}')
      case "$name" in
        *fts*|*goodix*|*xiaomi-touch*|*touch*)
          TARGET_INPUT="$ev"
          break
          ;;
      esac
    fi
  done
fi

echo "$(date) starting touch_remap, target=$TARGET_INPUT" >> "$LOG"

# 启动守护进程（后台运行）
# 参数： <input_device> <invert_y (0/1)> <invert_x (0/1)> <logfile>
"$BIN_PATH" "$TARGET_INPUT" 1 0 "$LOG" &

exit 0
