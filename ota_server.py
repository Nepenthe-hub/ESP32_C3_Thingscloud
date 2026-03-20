#!/usr/bin/env python3
"""
ESP32 OTA 本地固件服务器
用法：python ota_server.py

把这个脚本和 firmware.bin、version.json 放在同一个目录下运行。
ESP32 会从这里下载固件。
"""

import http.server
import socketserver
import os

PORT = 8080

class OTAHandler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, format, *args):
        # 打印带时间戳的请求日志，方便调试
        print(f"[OTA Server] {self.address_string()} - {format % args}")

if __name__ == "__main__":
    os.chdir(os.path.dirname(os.path.abspath(__file__)))
    print(f"[OTA Server] Serving at http://0.0.0.0:{PORT}")
    print(f"[OTA Server] Files in directory: {os.listdir('.')}")
    with socketserver.TCPServer(("", PORT), OTAHandler) as httpd:
        httpd.serve_forever()