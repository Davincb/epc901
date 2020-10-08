# Wrapper library for EPC901 breakout board

import serial
import sys
from textwrap import wrap

class Camera:    
    ser = serial.Serial()
    def open(self, port, speed=115200):
        try:
            self.ser = serial.Serial(port, speed, timeout=1)
            self.ser.write(b"@echo off\n")
            self.ser.readline()
            self.ser.readline()
            self.ser.write(b"@reset sensor\n")
            self.ser.readline()     # ok
        except Exception as e:
            print("Failed to connect to sensor on port ", port)
            print("Exception: " + str(e))
            sys.exit()

    def close(self):
        self.ser.write(b"@echo on\n")
        self.ser.readline()
        self.ser.close()

    def setExposure(self, exposure):
        exposure_str = "@exposure {}\n".format(exposure).encode("utf-8");
        self.ser.write(exposure_str)
        self.ser.readline()     # ok

    def capture(self):
        self.ser.write(b"@capture\n")
        self.ser.readline()     # ok

    def getPixels(self):
        self.ser.write(b"@transfer\n")
        frame_meta = self.ser.readline()     # frame number, timestamp, exposure
        frame_data = [int(x, 16) for x in wrap(self.ser.readline().decode("utf-8"), 4)]
        return frame_data

