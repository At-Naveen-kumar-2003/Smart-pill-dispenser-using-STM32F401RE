
# 🏥 Smart Medical Pill Dispenser

An advanced **IoT + Embedded + Python GUI** based medication management system designed for elderly and chronically ill patients.
This project integrates an **STM32F4 microcontroller**, **dual LCDs**, **IR pill detection**, **servo-based dispensing**, **DS3231 RTC**, **Python Tkinter GUI**, and **Email Alerts**.

---

## ⭐ Features

### 🔧 **Hardware (STM32F4 + Sensors)**

* Real-time clock scheduling (DS3231)
* Servo-motor based pill dispensing
* Dual LCD Display (Hardware I2C + Software I2C)
* IR sensors for pill detection & verification
* Buzzer & LED alerts
* Robust alarm queue system (handles missed alarms automatically)
* UART communication with desktop application

---

### 🖥 **Python GUI Application**

* Clean Tkinter interface with multiple tabs:

  * Dashboard (status, next alarm, missed count)
  * Alarm Management
  * Tablet Inventory Management
  * History & event logging
  * Email configuration
  * System logs
* JSON-based persistent storage
* Real-time serial communication with STM32
* Stock tracking with automatic decrement after each dispense
* Displays unavailable medications when stock is zero

---

### 📧 **Smart Email Notifications**

Automatic emails sent when:

* 🚨 Pill is missed
* ❌ Medication runs out of stock
* 📦 Refill is required

Includes a **professional HTML template** for caregivers.

---

## 📂 Project Structure

```
/Smart-Pill-Dispenser
│
├── gui_smart_pill.py     # Full-featured Tkinter desktop GUI
├── uart.c                # Simple serial monitor for debugging STM32 JSON output
├── Smart_pill.c          # STM32 firmware: alarms, LCD, sensors, queueing, UART
│
├── tablet_db.json        # Auto-generated storage for tablets & stock
├── README.md             # Documentation
└── images/               # Photos & diagrams for README
```

---

## 💡 System Workflow

1. **STM32 checks RTC time** → If alarm matches, it triggers queue
2. **Buzzer rings + LCD alerts**
3. **Servo dispenses pill**
4. **IR sensors confirm pill pickup**
5. **GUI updates status through UART JSON**
6. **Email sent if pill is missed or stock becomes 0**

---

## 🛠 Technologies Used

### **Embedded**

* STM32F401/STM32F411 (C)
* Hardware I2C (LCD1, RTC)
* Software bit-banged I2C (LCD2)
* UART for PC communication
* Timers, GPIO, NVIC interrupts
* Servo PWM generation

### **Software**

* Python Tkinter (GUI)
* JSON data storage
* Multithreading
* SMTP Email integration
* CSV export for history

---

## 📷 Adding Images to README

Upload images to your GitHub repository → inside `/images`.

Then use:

```md
![Dashboard](images/dashboard.png)
![Hardware](images/hardware.jpg)
```

---

## 🚀 How to Run

### **1. Flash STM32**

Compile & flash `Smart_pill.c` using Keil/uVision or STM32CubeIDE.

### **2. Run Python GUI**

```bash
python gui_smart_pill.py
```

### **3. Connect Serial Port**

Set the correct COM port in the script:

```python
SERIAL_PORT = "COM5"
```

---

## 📌 Future Enhancements

* Mobile app integration
* WiFi/ESP32 module support
* Cloud-based health analytics
* Battery backup & UPS monitoring

---

