"""
udp_log.py - ESP32 UDP 日志接收器
用法：python udp_log.py
确保电脑和 ESP32 在同一个 WiFi 下
"""
import socket
from datetime import datetime

PORT = 12345

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
sock.bind(("", PORT))

print(f"=== ESP32 UDP 日志监听中，端口 {PORT} ===")
print("等待 ESP32 连上 WiFi 后自动开始输出...\n")

while True:
    try:
        data, addr = sock.recvfrom(1024)
        msg = data.decode("utf-8", errors="replace").rstrip("\n")
        ts  = datetime.now().strftime("%H:%M:%S.%f")[:-3]
        print(f"[{ts}] {msg}")
    except KeyboardInterrupt:
        print("\n已退出")
        break