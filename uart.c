import serial
import json
import time

# CONFIGURATION
SERIAL_PORT = 'COM5'  # <--- CHECK YOUR COM PORT IN DEVICE MANAGER
BAUD_RATE = 115200

try:
    # Open Serial Port
    ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
    print(f"Successfully connected to {SERIAL_PORT}")
    print("Waiting for data from STM32...")

    while True:
        # Check if data is waiting in the buffer
        if ser.in_waiting > 0:
            # Read line, decode bytes to string, strip whitespace
            raw_line = ser.readline().decode('utf-8', errors='ignore').strip()

            if raw_line:
                print(f"Raw Data: {raw_line}")

                # Try parsing as JSON (to verify format)
                try:
                    data = json.loads(raw_line)
                    print(f"Parsed JSON -> Command: {data.get('cmd')}")
                    print("-" * 30)
                except json.JSONDecodeError:
                    print("Received data is not valid JSON")

        time.sleep(0.1)

except serial.SerialException as e:
    print(f"Error opening serial port: {e}")
except KeyboardInterrupt:
    print("Exiting...")
    if 'ser' in locals() and ser.is_open:
        ser.close()
