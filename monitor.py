import serial

ser = serial.Serial(
    port='COM3',\
    baudrate=115200,\
    parity=serial.PARITY_NONE,\
    stopbits=serial.STOPBITS_ONE,\
    bytesize=serial.EIGHTBITS,\
        timeout=0)

print("connected to: " + ser.portstr)
count=1

while True:
    for line in ser.read():
        print(chr(line),end="")

ser.close()