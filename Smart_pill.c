#include "stm32f4xx.h"
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

// ============================================
// SYSTEM CONFIGURATION
// ============================================
#define LCD1_ADDRESS 0x27
#define LCD2_ADDRESS 0x27
#define RTC_ADDRESS 0x68

// IR SENSOR LOGIC - REVERSED (Active HIGH)
#define IR_PIN_READ(pin) (GPIOB->IDR & (1 << pin))
#define IR_SENSOR1_PRESENT (!IR_PIN_READ(0))
#define IR_SENSOR2_PRESENT (!IR_PIN_READ(1))

// Software I2C pins for LCD2
#define SOFT_SDA_PIN 5
#define SOFT_SCL_PIN 4
#define SOFT_SDA_HIGH() GPIOB->BSRR = (1 << SOFT_SDA_PIN)
#define SOFT_SDA_LOW()  GPIOB->BSRR = (1 << (SOFT_SDA_PIN + 16))
#define SOFT_SCL_HIGH() GPIOB->BSRR = (1 << SOFT_SCL_PIN)
#define SOFT_SCL_LOW()  GPIOB->BSRR = (1 << (SOFT_SCL_PIN + 16))
#define SOFT_I2C_DELAY_US 2

// Chennai Timing (IST - UTC+5:30)
#define SET_INITIAL_TIME 1
#define INIT_HOUR 14
#define INIT_MINUTE 30
#define INIT_SECOND 0
#define INIT_DAY 18
#define INIT_MONTH 11
#define INIT_YEAR 2025

// LCD commands
#define LCD_CLEARDISPLAY 0x01
#define LCD_RETURNHOME   0x02
#define LCD_ENTRYMODESET 0x04
#define LCD_DISPLAYCONTROL 0x08
#define LCD_FUNCTIONSET  0x20
#define LCD_SETDDRAMADDR 0x80
#define LCD_2LINE        0x08
#define LCD_5x8DOTS      0x00
#define LCD_BACKLIGHT    0x08
#define LCD_ENABLE_BIT   0x04
#define LCD_RS_CMD       0x00
#define LCD_RS_DATA      0x01

// Tablet names
const char* TABLET_NAMES[] = {
  "Aspirin",
  "Vitamin C",
  "Calcium",
  "Multivitamin"
};

#define NUM_TABLET_TYPES (sizeof(TABLET_NAMES) / sizeof(const char*))

// Alarm configuration
#define NUM_ALARMS_MAX 10

// **NEW: Alarm status tracking**
typedef enum {
  ALARM_STATUS_PENDING,    // Not yet time
  ALARM_STATUS_ACTIVE,     // Currently being dispensed
  ALARM_STATUS_TAKEN,      // Pill taken successfully
  ALARM_STATUS_MISSED,     // Time expired, pill not taken
  ALARM_STATUS_DELAYED_TAKEN // Taken after missing deadline
} AlarmStatus;

typedef struct {
  uint8_t hour;
  uint8_t minute;
  bool enabled;
  AlarmStatus status; // **CHANGED: from triggered bool to status enum**
  uint8_t tabletIndex;
} AlarmTime;

AlarmTime alarms[NUM_ALARMS_MAX] = {
  {14, 31, true, ALARM_STATUS_PENDING, 0},
  {14, 33, true, ALARM_STATUS_PENDING, 1},
  {14, 35, true, ALARM_STATUS_PENDING, 2},
};
volatile uint8_t current_num_alarms = 3;

// **NEW: Alarm queue system**
#define ALARM_QUEUE_MAX 10
typedef struct {
  uint8_t alarmIndex;
  bool isMissedAlarm; // true if this is a delayed pickup
} AlarmQueueItem;

AlarmQueueItem alarmQueue[ALARM_QUEUE_MAX];
volatile uint8_t queueHead = 0;
volatile uint8_t queueTail = 0;
volatile uint8_t queueCount = 0;

// System states
typedef enum {
  STATE_IDLE,
  STATE_BUZZER_ALARM,
  STATE_DISPENSING,
  STATE_MONITOR_IR,
  STATE_PILL_TAKEN,
  STATE_WAITING_MISSED_PICKUP // **NEW: Special state for missed pills**
} SystemState;

// Global variables
volatile SystemState currentState = STATE_IDLE;
volatile uint32_t stateTimer = 0;
volatile bool updateDisplay1Flag = true;
volatile bool updateDisplay2Flag = true;
volatile uint32_t monitorTimer = 0;
uint8_t currentAlarmIndex = 0;
volatile uint8_t lastCheckedMinute = 61;
volatile bool currentAlarmIsMissed = false; // **NEW: Track if current is delayed**
volatile uint32_t missedAlarmBlinkTimer = 0;
volatile bool ir1_status = false;
volatile bool ir2_status = false;
volatile bool bothIRSensorsActive = false;

// RTC variables
volatile uint8_t rtc_hours = 0;
volatile uint8_t rtc_minutes = 0;
volatile uint8_t rtc_seconds = 0;
volatile uint8_t rtc_day = 1;
volatile uint8_t rtc_month = 1;
volatile uint16_t rtc_year = 2025;

// UART variables
char uartRxBuffer[128];
volatile uint8_t uartRxIndex = 0;
volatile bool uartCommandReady = false;

// Email notification tracking
volatile bool emailSentFor30Sec = false;

// Function prototypes
void SystemClock_Config(void);
void GPIO_Init(void);
void I2C1_Init(void);
void I2C3_Init(void);
void SoftI2C_Init(void);
void UART1_Init(void);
void TIM2_Init(void);
void TIM3_Init(void);
void DelayMs(uint32_t ms);
void DelayUs(uint32_t us);
uint8_t BCD_to_Decimal(uint8_t bcd);
uint8_t Decimal_to_BCD(uint8_t dec);
void RTC_Init(void);
void RTC_SetTime(uint8_t hour, uint8_t minute, uint8_t second);
void RTC_SetDate(uint8_t day, uint8_t month, uint16_t year);
void RTC_ReadTime(void);
void LCD1_Init(void);
void LCD2_Init(void);
void LCD1_Clear(void);
void LCD2_Clear(void);
void LCD1_PrintLine(uint8_t row, const char *str);
void LCD2_PrintLine(uint8_t row, const char *str);
void ServoSetAngle(uint8_t angle);
void BuzzerBeep(uint16_t duration_ms);
void BuzzerOn(void);
void BuzzerOff(void);
void LED_On(void);
void LED_Off(void);
bool IR_Sensor1_PillPresent(void);
bool IR_Sensor2_PillPresent(void);
void UpdateDisplay1(void);
void UpdateDisplay2(void);
void CheckAlarms(void);
void ResetAlarmStatuses(void);
uint32_t GetTimeUntilNextAlarm(void);
void UART_SendStatus(void);
void UART_SendChar(char c);
void UART_SendString(const char *str);
void UART_ParseCommand(char *cmd);
void UART_SendAlarms(void);
void UART_SendTabletNames(void);
void AddNewAlarm(uint8_t hour, uint8_t minute, uint8_t tabletIndex);
void DeleteAlarm(uint8_t index);
void IR_DebugStatus(void);

// **NEW: Queue management functions**
bool EnqueueAlarm(uint8_t alarmIndex, bool isMissed);
bool DequeueAlarm(AlarmQueueItem *item);
void ProcessNextAlarmInQueue(void);
uint8_t GetMissedAlarmCount(void);

// ============================================
// ALARM QUEUE FUNCTIONS
// ============================================
bool EnqueueAlarm(uint8_t alarmIndex, bool isMissed) {
  if (queueCount >= ALARM_QUEUE_MAX) {
    return false; // Queue full
  }
  alarmQueue[queueTail].alarmIndex = alarmIndex;
  alarmQueue[queueTail].isMissedAlarm = isMissed;
  queueTail = (queueTail + 1) % ALARM_QUEUE_MAX;
  queueCount++;
  return true;
}

bool DequeueAlarm(AlarmQueueItem *item) {
  if (queueCount == 0) {
    return false; // Queue empty
  }
  *item = alarmQueue[queueHead];
  queueHead = (queueHead + 1) % ALARM_QUEUE_MAX;
  queueCount--;
  return true;
}

uint8_t GetMissedAlarmCount(void) {
  uint8_t count = 0;
  for (uint8_t i = 0; i < current_num_alarms; i++) {
    if (alarms[i].enabled && alarms[i].status == ALARM_STATUS_MISSED) {
      count++;
    }
  }
  return count;
}

void ProcessNextAlarmInQueue(void) {
  if (currentState != STATE_IDLE) {
    return; // Already processing an alarm
  }
  AlarmQueueItem item;
  if (DequeueAlarm(&item)) {
    currentAlarmIndex = item.alarmIndex;
    currentAlarmIsMissed = item.isMissedAlarm;
    // Update alarm status
    alarms[currentAlarmIndex].status = ALARM_STATUS_ACTIVE;
    if (currentAlarmIsMissed) {
      // Missed alarm - skip buzzer, go straight to dispensing
      currentState = STATE_DISPENSING;
      stateTimer = 50; // 5 seconds
      ServoSetAngle(90);
    } else {
      // Fresh alarm - normal flow with buzzer
      currentState = STATE_BUZZER_ALARM;
      stateTimer = 200; // 20 seconds
      BuzzerOn();
      LED_On();
    }
    updateDisplay2Flag = true;
    // Send notification
    char notif[128];
    snprintf(notif, sizeof(notif),
             "{\"cmd\":\"ALARM_ACTIVE\",\"tablet\":\"%s\",\"alarm_id\":%d,\"missed\":%d}\n",
             TABLET_NAMES[alarms[currentAlarmIndex].tabletIndex],
             currentAlarmIndex,
             currentAlarmIsMissed ? 1 : 0);
    UART_SendString(notif);
  }
}

// ============================================
// DELAY FUNCTIONS
// ============================================
void DelayUs(uint32_t us) {
  us *= 21;
  while (us--) {
    __NOP();
  }
}

void DelayMs(uint32_t ms) {
  while (ms--) {
    DelayUs(1000);
  }
}

// ============================================
// GPIO CONTROL
// ============================================
void LED_On(void) {
  GPIOA->BSRR = GPIO_BSRR_BS6;
}
void LED_Off(void) {
  GPIOA->BSRR = GPIO_BSRR_BR6;
}
void BuzzerOn(void) {
  GPIOA->BSRR = GPIO_BSRR_BS5;
}
void BuzzerOff(void) {
  GPIOA->BSRR = GPIO_BSRR_BR5;
}
void BuzzerBeep(uint16_t duration_ms) {
  BuzzerOn();
  DelayMs(duration_ms);
  BuzzerOff();
}

// ============================================
// IR SENSOR FUNCTIONS
// ============================================
bool IR_Sensor1_PillPresent(void) {
  return IR_SENSOR1_PRESENT;
}
bool IR_Sensor2_PillPresent(void) {
  return IR_SENSOR2_PRESENT;
}

void IR_DebugStatus(void) {
  char debug[128];
  uint8_t pin0_raw = (GPIOB->IDR & GPIO_IDR_ID0) ? 1 : 0;
  uint8_t pin1_raw = (GPIOB->IDR & GPIO_IDR_ID1) ? 1 : 0;
  snprintf(debug, sizeof(debug),
           "IR Debug - Pin0(raw)=%d Pin1(raw)=%d | IR1(pill)=%d IR2(pill)=%d | Both=%d\n",
           pin0_raw, pin1_raw, ir1_status ? 1 : 0, ir2_status ? 1 : 0, bothIRSensorsActive ? 1 : 0);
  UART_SendString(debug);
}

// ============================================
// RTC DS3231 FUNCTIONS
// ============================================
uint8_t BCD_to_Decimal(uint8_t bcd) {
  return ((bcd >> 4) * 10) + (bcd & 0x0F);
}
uint8_t Decimal_to_BCD(uint8_t dec) {
  return ((dec / 10) << 4) | (dec % 10);
}

void I2C1_Write(uint8_t addr, uint8_t reg, uint8_t data) {
  volatile uint32_t timeout = 100000;
  while ((I2C1->SR2 & I2C_SR2_BUSY) && timeout--);
  if (timeout == 0) return;
  I2C1->CR1 |= I2C_CR1_START;
  timeout = 100000;
  while (!(I2C1->SR1 & I2C_SR1_SB) && timeout--);
  if (timeout == 0) return;
  I2C1->DR = RTC_ADDRESS << 1;
  timeout = 100000;
  while (!(I2C1->SR1 & I2C_SR1_ADDR) && timeout--);
  if (timeout == 0) return;
  (void)I2C1->SR2;
  I2C1->DR = reg;
  timeout = 100000;
  while (!(I2C1->SR1 & I2C_SR1_TXE) && timeout--);
  if (timeout == 0) return;
  I2C1->DR = data;
  timeout = 100000;
  while (!(I2C1->SR1 & I2C_SR1_BTF) && timeout--);
  I2C1->CR1 |= I2C_CR1_STOP;
  DelayMs(1);
}

uint8_t I2C1_Read(uint8_t addr, uint8_t reg) {
  uint8_t data = 0;
  volatile uint32_t timeout = 100000;
  while ((I2C1->SR2 & I2C_SR2_BUSY) && timeout--);
  if (timeout == 0) return 0;
  I2C1->CR1 |= I2C_CR1_START;
  timeout = 100000;
  while (!(I2C1->SR1 & I2C_SR1_SB) && timeout--);
  if (timeout == 0) return 0;
  I2C1->DR = RTC_ADDRESS << 1;
  timeout = 100000;
  while (!(I2C1->SR1 & I2C_SR1_ADDR) && timeout--);
  if (timeout == 0) return 0;
  (void)I2C1->SR2;
  I2C1->DR = reg;
  timeout = 100000;
  while (!(I2C1->SR1 & I2C_SR1_TXE) && timeout--);
  if (timeout == 0) return 0;
  I2C1->CR1 |= I2C_CR1_START;
  timeout = 100000;
  while (!(I2C1->SR1 & I2C_SR1_SB) && timeout--);
  if (timeout == 0) return 0;
  I2C1->DR = (RTC_ADDRESS << 1) | 0x01;
  timeout = 100000;
  while (!(I2C1->SR1 & I2C_SR1_ADDR) && timeout--);
  if (timeout == 0) return 0;
  I2C1->CR1 &= ~I2C_CR1_ACK;
  (void)I2C1->SR2;
  I2C1->CR1 |= I2C_CR1_STOP;
  timeout = 100000;
  while (!(I2C1->SR1 & I2C_SR1_RXNE) && timeout--);
  data = I2C1->DR;
  I2C1->CR1 |= I2C_CR1_ACK;
  return data;
}

void RTC_SetTime(uint8_t hour, uint8_t minute, uint8_t second) {
  I2C1_Write(RTC_ADDRESS, 0x00, Decimal_to_BCD(second));
  I2C1_Write(RTC_ADDRESS, 0x01, Decimal_to_BCD(minute));
  I2C1_Write(RTC_ADDRESS, 0x02, Decimal_to_BCD(hour));
}

void RTC_SetDate(uint8_t day, uint8_t month, uint16_t year) {
  I2C1_Write(RTC_ADDRESS, 0x04, Decimal_to_BCD(day));
  I2C1_Write(RTC_ADDRESS, 0x05, Decimal_to_BCD(month));
  I2C1_Write(RTC_ADDRESS, 0x06, Decimal_to_BCD(year - 2000));
}

void RTC_ReadTime(void) {
  rtc_seconds = BCD_to_Decimal(I2C1_Read(RTC_ADDRESS, 0x00) & 0x7F);
  rtc_minutes = BCD_to_Decimal(I2C1_Read(RTC_ADDRESS, 0x01) & 0x7F);
  rtc_hours   = BCD_to_Decimal(I2C1_Read(RTC_ADDRESS, 0x02) & 0x3F);
  rtc_day     = BCD_to_Decimal(I2C1_Read(RTC_ADDRESS, 0x04) & 0x3F);
  rtc_month   = BCD_to_Decimal(I2C1_Read(RTC_ADDRESS, 0x05) & 0x1F);
  rtc_year    = BCD_to_Decimal(I2C1_Read(RTC_ADDRESS, 0x06)) + 2000;
}

void RTC_Init(void) {
  DelayMs(100);
  uint8_t control_status = I2C1_Read(RTC_ADDRESS, 0x0F);
  control_status &= ~0x80;
  I2C1_Write(RTC_ADDRESS, 0x0F, control_status);
  uint8_t control = I2C1_Read(RTC_ADDRESS, 0x0E);
  control &= ~0x80;
  I2C1_Write(RTC_ADDRESS, 0x0E, control);
  DelayMs(50);

#if SET_INITIAL_TIME == 1
  RTC_SetTime(INIT_HOUR, INIT_MINUTE, INIT_SECOND);
  RTC_SetDate(INIT_DAY, INIT_MONTH, INIT_YEAR);
  DelayMs(100);
#endif
}

// ============================================
// LCD1 (I2C3) - Hardware I2C
// ============================================
void I2C3_Write_LCD(uint8_t data) {
  volatile uint32_t timeout = 50000;
  while ((I2C3->SR2 & I2C_SR2_BUSY) && timeout--);
  if (timeout == 0) return;
  I2C3->CR1 |= I2C_CR1_START;
  timeout = 50000;
  while (!(I2C3->SR1 & I2C_SR1_SB) && timeout--);
  if (timeout == 0) return;
  I2C3->DR = LCD1_ADDRESS << 1;
  timeout = 50000;
  while (!(I2C3->SR1 & I2C_SR1_ADDR) && timeout--);
  if (timeout == 0) { I2C3->CR1 |= I2C_CR1_STOP; return; }
  (void)I2C3->SR1;
  (void)I2C3->SR2;
  timeout = 50000;
  while (!(I2C3->SR1 & I2C_SR1_TXE) && timeout--);
  if (timeout == 0) return;
  I2C3->DR = data;
  timeout = 50000;
  while (!(I2C3->SR1 & I2C_SR1_BTF) && timeout--);
  I2C3->CR1 |= I2C_CR1_STOP;
}

void LCD1_Send4Bits(uint8_t data) {
  uint8_t high_nibble = data | LCD_BACKLIGHT;
  I2C3_Write_LCD(high_nibble | LCD_ENABLE_BIT);
  DelayUs(1);
  I2C3_Write_LCD(high_nibble & ~LCD_ENABLE_BIT);
  DelayUs(50);
}

void LCD1_SendCommand(uint8_t command) {
  uint8_t high_nibble = command & 0xF0;
  uint8_t low_nibble = (command << 4) & 0xF0;
  LCD1_Send4Bits(high_nibble | LCD_RS_CMD);
  LCD1_Send4Bits(low_nibble | LCD_RS_CMD);
  if (command == LCD_CLEARDISPLAY || command == LCD_RETURNHOME) {
    DelayMs(2);
  } else {
    DelayUs(100);
  }
}

void LCD1_SendData(uint8_t data) {
  uint8_t high_nibble = data & 0xF0;
  uint8_t low_nibble = (data << 4) & 0xF0;
  high_nibble |= LCD_RS_DATA;
  low_nibble  |= LCD_RS_DATA;
  LCD1_Send4Bits(high_nibble);
  LCD1_Send4Bits(low_nibble);
  DelayUs(100);
}

void LCD1_Init(void) {
  DelayMs(50);
  LCD1_Send4Bits(0x30 | LCD_RS_CMD); DelayMs(5);
  LCD1_Send4Bits(0x30 | LCD_RS_CMD); DelayUs(150);
  LCD1_Send4Bits(0x30 | LCD_RS_CMD); DelayUs(150);
  LCD1_Send4Bits(0x20 | LCD_RS_CMD); DelayMs(2);
  LCD1_SendCommand(0x28);
  LCD1_SendCommand(0x0C);
  LCD1_SendCommand(0x01);
  LCD1_SendCommand(0x06);
}

void LCD1_SendString(const char *str) {
  while (*str) {
    LCD1_SendData(*str++);
  }
}

void LCD1_SetCursor(uint8_t row, uint8_t column) {
  uint8_t address = (row == 0) ? 0x00 : 0x40;
  address += column;
  LCD1_SendCommand(LCD_SETDDRAMADDR | address);
}

void LCD1_Clear(void) {
  LCD1_SendCommand(LCD_CLEARDISPLAY);
}

void LCD1_PrintLine(uint8_t row, const char *str) {
  char buffer[17];
  snprintf(buffer, sizeof(buffer), "%-16s", str);
  LCD1_SetCursor(row, 0);
  LCD1_SendString(buffer);
}

// ============================================
// LCD2 (Software I2C)
// ============================================
void SoftI2C_Delay(void) {
  DelayUs(SOFT_I2C_DELAY_US);
}

void SoftI2C_Start(void) {
  SOFT_SDA_HIGH(); SOFT_SCL_HIGH(); SoftI2C_Delay();
  SOFT_SDA_LOW();  SoftI2C_Delay();
  SOFT_SCL_LOW();  SoftI2C_Delay();
}

void SoftI2C_Stop(void) {
  SOFT_SDA_LOW(); SoftI2C_Delay();
  SOFT_SCL_HIGH(); SoftI2C_Delay();
  SOFT_SDA_HIGH(); SoftI2C_Delay();
}

void SoftI2C_WriteByte(uint8_t data) {
  for (int i = 0; i < 8; i++) {
    if (data & 0x80) { SOFT_SDA_HIGH(); } else { SOFT_SDA_LOW(); }
    SoftI2C_Delay();
    SOFT_SCL_HIGH(); SoftI2C_Delay(); SoftI2C_Delay();
    SOFT_SCL_LOW();  SoftI2C_Delay();
    data <<= 1;
  }
  SOFT_SDA_HIGH(); SoftI2C_Delay();
  SOFT_SCL_HIGH(); SoftI2C_Delay(); SoftI2C_Delay();
  SOFT_SCL_LOW(); SoftI2C_Delay();
}

void SoftI2C_Write_LCD(uint8_t data) {
  SoftI2C_Start();
  SoftI2C_WriteByte(LCD2_ADDRESS << 1);
  SoftI2C_WriteByte(data);
  SoftI2C_Stop();
}

void LCD2_Send4Bits(uint8_t data) {
  uint8_t high_nibble = data | LCD_BACKLIGHT;
  SoftI2C_Write_LCD(high_nibble | LCD_ENABLE_BIT);
  DelayUs(1);
  SoftI2C_Write_LCD(high_nibble & ~LCD_ENABLE_BIT);
  DelayUs(50);
}

void LCD2_SendCommand(uint8_t command) {
  uint8_t high_nibble = command & 0xF0;
  uint8_t low_nibble = (command << 4) & 0xF0;
  LCD2_Send4Bits(high_nibble | LCD_RS_CMD);
  LCD2_Send4Bits(low_nibble | LCD_RS_CMD);
  if (command == LCD_CLEARDISPLAY || command == LCD_RETURNHOME) {
    DelayMs(2);
  } else {
    DelayUs(100);
  }
}

void LCD2_SendData(uint8_t data) {
  uint8_t high_nibble = data & 0xF0;
  uint8_t low_nibble = (data << 4) & 0xF0;
  high_nibble |= LCD_RS_DATA;
  low_nibble  |= LCD_RS_DATA;
  LCD2_Send4Bits(high_nibble);
  LCD2_Send4Bits(low_nibble);
  DelayUs(100);
}

void LCD2_Init(void) {
  DelayMs(50);
  LCD2_Send4Bits(0x30 | LCD_RS_CMD); DelayMs(5);
  LCD2_Send4Bits(0x30 | LCD_RS_CMD); DelayUs(150);
  LCD2_Send4Bits(0x30 | LCD_RS_CMD); DelayUs(150);
  LCD2_Send4Bits(0x20 | LCD_RS_CMD); DelayMs(2);
  LCD2_SendCommand(0x28);
  LCD2_SendCommand(0x0C);
  LCD2_SendCommand(0x01);
  LCD2_SendCommand(0x06);
}

void LCD2_SendString(const char *str) {
  while (*str) {
    LCD2_SendData(*str++);
  }
}

void LCD2_SetCursor(uint8_t row, uint8_t column) {
  uint8_t address = (row == 0) ? 0x00 : 0x40;
  address += column;
  LCD2_SendCommand(LCD_SETDDRAMADDR | address);
}

void LCD2_Clear(void) {
  LCD2_SendCommand(LCD_CLEARDISPLAY);
}

void LCD2_PrintLine(uint8_t row, const char *str) {
  char buffer[17];
  snprintf(buffer, sizeof(buffer), "%-16s", str);
  LCD2_SetCursor(row, 0);
  LCD2_SendString(buffer);
}

// ============================================
// UART1 FUNCTIONS
// ============================================
void UART1_Init(void) {
  RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
  RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
  GPIOA->MODER &= ~(GPIO_MODER_MODER9 | GPIO_MODER_MODER10);
  GPIOA->MODER |= (GPIO_MODER_MODER9_1 | GPIO_MODER_MODER10_1);
  GPIOA->AFR[1] |= (7 << GPIO_AFRH_AFSEL9_Pos) | (7 << GPIO_AFRH_AFSEL10_Pos);
  USART1->BRR = 84000000 / 115200;
  USART1->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE | USART_CR1_UE;
  NVIC_EnableIRQ(USART1_IRQn);
  NVIC_SetPriority(USART1_IRQn, 1);
}

void UART_SendChar(char c) {
  while (!(USART1->SR & USART_SR_TXE));
  USART1->DR = c;
}

void UART_SendString(const char *str) {
  while (*str) {
    UART_SendChar(*str++);
  }
}

void UART_SendStatus(void) {
  char buffer[300];
  uint8_t display_hour = rtc_hours;
  const char* am_pm = "AM";
  if (display_hour >= 12) {
    am_pm = "PM";
    if (display_hour > 12) display_hour -= 12;
  }
  if (display_hour == 0) display_hour = 12;

  uint32_t minutesLeft = GetTimeUntilNextAlarm();
  uint8_t hoursLeft = minutesLeft / 60;
  uint8_t minsLeft = minutesLeft % 60;
  uint8_t missedCount = GetMissedAlarmCount();

  snprintf(buffer, sizeof(buffer),
           "{\"cmd\":\"STATUS\",\"time\":\"%02d:%02d:%02d\",\"ampm\":\"%s\","
           "\"date\":\"%02d/%02d/%04d\",\"state\":%d,\"ir1\":%d,\"ir2\":%d,"
           "\"nextAlarm\":\"%02dh %02dm\",\"currentTablet\":\"%s\","
           "\"missedCount\":%d,\"queueCount\":%d,\"alarmId\":%d}\n",
           display_hour, rtc_minutes, rtc_seconds, am_pm,
           rtc_day, rtc_month, rtc_year,
           (int)currentState, ir1_status ? 1 : 0, ir2_status ? 1 : 0,
           hoursLeft, minsLeft,
           (currentState != STATE_IDLE && currentState != STATE_WAITING_MISSED_PICKUP) ?
             TABLET_NAMES[alarms[currentAlarmIndex].tabletIndex] : "None",
           missedCount, queueCount, currentAlarmIndex);
  UART_SendString(buffer);
}

void UART_SendTabletNames(void) {
  char buffer[512];
  int offset = 0;
  offset += snprintf(buffer + offset, sizeof(buffer) - offset, "{\"cmd\":\"GET_TABLETS\",\"names\":[");
  for (uint8_t i = 0; i < NUM_TABLET_TYPES; i++) {
    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "\"%s\"", TABLET_NAMES[i]);
    if (i < NUM_TABLET_TYPES - 1) {
      offset += snprintf(buffer + offset, sizeof(buffer) - offset, ",");
    }
  }
  snprintf(buffer + offset, sizeof(buffer) - offset, "]}\n");
  UART_SendString(buffer);
}

void UART_SendAlarms(void) {
  char buffer[1024];
  int offset = 0;
  offset += snprintf(buffer + offset, sizeof(buffer) - offset, "{\"cmd\":\"GET_ALARMS\",\"alarms\":[");
  for (uint8_t i = 0; i < current_num_alarms; i++) {
    const char* statusStr = "PENDING";
    switch (alarms[i].status) {
      case ALARM_STATUS_ACTIVE: statusStr = "ACTIVE"; break;
      case ALARM_STATUS_TAKEN:  statusStr = "TAKEN";  break;
      case ALARM_STATUS_MISSED: statusStr = "MISSED"; break;
      case ALARM_STATUS_DELAYED_TAKEN: statusStr = "DELAYED_TAKEN"; break;
      default: statusStr = "PENDING"; break;
    }
    offset += snprintf(buffer + offset, sizeof(buffer) - offset,
                       "{\"id\":%u,\"h\":%u,\"m\":%u,\"t\":\"%s\",\"enabled\":%d,\"status\":\"%s\"}",
                       i, alarms[i].hour, alarms[i].minute, TABLET_NAMES[alarms[i].tabletIndex],
                       alarms[i].enabled ? 1 : 0, statusStr);
    if (i < current_num_alarms - 1) {
      offset += snprintf(buffer + offset, sizeof(buffer) - offset, ",");
    }
  }
  snprintf(buffer + offset, sizeof(buffer) - offset, "]}\n");
  UART_SendString(buffer);
}

void AddNewAlarm(uint8_t hour, uint8_t minute, uint8_t tabletIndex) {
  if (current_num_alarms < NUM_ALARMS_MAX) {
    alarms[current_num_alarms].hour = hour;
    alarms[current_num_alarms].minute = minute;
    alarms[current_num_alarms].tabletIndex = tabletIndex;
    alarms[current_num_alarms].enabled = true;
    alarms[current_num_alarms].status = ALARM_STATUS_PENDING;
    current_num_alarms++;
    UART_SendString("OK:ALARM_ADDED\n");
    updateDisplay2Flag = true;
  } else {
    UART_SendString("ERROR:MAX_ALARMS_REACHED\n");
  }
}

void DeleteAlarm(uint8_t index) {
  if (index < current_num_alarms) {
    for (uint8_t i = index; i < current_num_alarms - 1; i++) {
      alarms[i] = alarms[i + 1];
    }
    current_num_alarms--;
    UART_SendString("OK:ALARM_DELETED\n");
    updateDisplay2Flag = true;
  } else {
    UART_SendString("ERROR:INVALID_ALARM_INDEX\n");
  }
}

void UART_ParseCommand(char *cmd) {
  if (strncmp(cmd, "STATUS", 6) == 0) {
    UART_SendStatus();
  } else if (strncmp(cmd, "GET_ALARMS", 10) == 0) {
    UART_SendAlarms();
  } else if (strncmp(cmd, "GET_TABLETS", 11) == 0) {
    UART_SendTabletNames();
  } else if (strncmp(cmd, "IR_DEBUG", 8) == 0) {
    IR_DebugStatus();
  } else if (strncmp(cmd, "ADD_ALARM:", 10) == 0) {
    char *h_str = cmd + 10;
    char *m_str = strchr(h_str, ':');
    char *t_str = (m_str != NULL) ? strchr(m_str + 1, ':') : NULL;
    if (m_str != NULL) *m_str = '\0';
    if (t_str != NULL) *t_str = '\0';
    if (m_str != NULL && t_str != NULL) {
      uint8_t h = atoi(h_str);
      uint8_t m = atoi(m_str + 1);
      uint8_t t = atoi(t_str + 1);
      if (h < 24 && m < 60 && t < NUM_TABLET_TYPES) {
        AddNewAlarm(h, m, t);
      } else {
        UART_SendString("ERROR:INVALID_ALARM_DATA\n");
      }
    } else {
      UART_SendString("ERROR:INVALID_ADD_COMMAND_FORMAT\n");
    }
  } else if (strncmp(cmd, "DEL_ALARM:", 10) == 0) {
    DeleteAlarm(atoi(cmd + 10));
  } else if (strncmp(cmd, "RESET", 5) == 0) {
    // Clear all missed alarms
    for (uint8_t i = 0; i < current_num_alarms; i++) {
      if (alarms[i].status == ALARM_STATUS_MISSED) {
        alarms[i].status = ALARM_STATUS_PENDING;
      }
    }
    // Clear queue
    queueHead = 0;
    queueTail = 0;
    queueCount = 0;
    missedAlarmBlinkTimer = 0;
    emailSentFor30Sec = false;
    BuzzerOff();
    LED_Off();
    currentState = STATE_IDLE;
    updateDisplay2Flag = true;
    UART_SendString("OK:SYSTEM_RESET\n");
  } else {
    UART_SendString("ERROR:UNKNOWN_COMMAND\n");
  }
}

void USART1_IRQHandler(void) {
  if (USART1->SR & USART_SR_RXNE) {
    char receivedChar = USART1->DR;
    if (receivedChar == '\n' || receivedChar == '\r') {
      uartRxBuffer[uartRxIndex] = '\0';
      uartCommandReady = true;
      uartRxIndex = 0;
    } else if (uartRxIndex < sizeof(uartRxBuffer) - 1) {
      uartRxBuffer[uartRxIndex++] = receivedChar;
    }
  }
}

// ============================================
// SERVO CONTROL
// ============================================
void ServoSetAngle(uint8_t angle) {
  if (angle > 180) angle = 180;
  uint16_t pulse = 1000 + ((uint32_t)angle * 1000 / 180);
  TIM2->CCR1 = pulse;
}

// ============================================
// SYSTEM FUNCTIONS
// ============================================
uint32_t GetTimeUntilNextAlarm(void) {
  uint32_t currentMinutes = rtc_hours * 60 + rtc_minutes;
  uint32_t minDiff = 24 * 60;
  bool foundAlarm = false;
  for (uint8_t i = 0; i < current_num_alarms; i++) {
    if (alarms[i].enabled && alarms[i].status == ALARM_STATUS_PENDING) {
      uint32_t alarmMinutes = alarms[i].hour * 60 + alarms[i].minute;
      uint32_t diff;
      if (alarmMinutes > currentMinutes) {
        diff = alarmMinutes - currentMinutes;
      } else if (alarmMinutes == currentMinutes) {
        diff = 0;
      } else {
        diff = (24 * 60) - currentMinutes + alarmMinutes;
      }
      if (diff < minDiff) {
        minDiff = diff;
        foundAlarm = true;
      }
    }
  }
  if (!foundAlarm) {
    minDiff = 24 * 60;
  }
  return minDiff;
}

// **NEW: Improved CheckAlarms - adds ALL due alarms to queue**
void CheckAlarms(void) {
  if (currentState != STATE_IDLE) return;
  if (rtc_seconds != 0) return;
  if (lastCheckedMinute == rtc_minutes) return;

  lastCheckedMinute = rtc_minutes;
  // Check for fresh alarms that should trigger now
  for (uint8_t i = 0; i < current_num_alarms; i++) {
    if (alarms[i].enabled && alarms[i].status == ALARM_STATUS_PENDING) {
      if (rtc_hours == alarms[i].hour && rtc_minutes == alarms[i].minute) {
        // Add to queue as fresh alarm
        if (EnqueueAlarm(i, false)) {
          char notif[128];
          snprintf(notif, sizeof(notif),
                   "{\"cmd\":\"ALARM_QUEUED\",\"tablet\":\"%s\",\"alarm_id\":%d}\n",
                   TABLET_NAMES[alarms[i].tabletIndex], i);
          UART_SendString(notif);
        }
      }
    }
  }
  // Process next alarm in queue if any
  ProcessNextAlarmInQueue();
}

void ResetAlarmStatuses(void) {
  if (rtc_hours == 0 && rtc_minutes == 0) {
    if (lastCheckedMinute != rtc_minutes) {
      for (uint8_t i = 0; i < current_num_alarms; i++) {
        // Reset all non-missed alarms
        if (alarms[i].status != ALARM_STATUS_MISSED) {
          alarms[i].status = ALARM_STATUS_PENDING;
        }
      }
    }
  }
}

void UpdateDisplay1(void) {
  char line1[17], line2[17];
  uint8_t display_hour = rtc_hours;
  const char* am_pm = "AM";
  if (display_hour >= 12) {
    am_pm = "PM";
    if (display_hour > 12) display_hour -= 12;
  }
  if (display_hour == 0) display_hour = 12;
  snprintf(line1, sizeof(line1), "%02d:%02d:%02d %s", display_hour, rtc_minutes, rtc_seconds, am_pm);
  snprintf(line2, sizeof(line2), "Date:%02d/%02d/%04d", rtc_day, rtc_month, rtc_year);
  LCD1_PrintLine(0, line1);
  LCD1_PrintLine(1, line2);
}

void UpdateDisplay2(void) {
  char line1[17], line2[17];
  uint8_t missedCount = GetMissedAlarmCount();

  switch (currentState) {
    case STATE_IDLE: {
      if (missedCount > 0) {
        // Show missed alarm count
        snprintf(line1, sizeof(line1), "MISSED PILLS:%d", missedCount);
        snprintf(line2, sizeof(line2), "Take them ASAP!");
      } else {
        // Show next alarm
        uint32_t minutesLeft = GetTimeUntilNextAlarm();
        uint8_t hoursLeft = minutesLeft / 60;
        uint8_t minsLeft = minutesLeft % 60;
        snprintf(line1, sizeof(line1), "Next Alarm In:");
        snprintf(line2, sizeof(line2), "%02dh %02dm", hoursLeft, minsLeft);
      }
      break;
    }
    case STATE_BUZZER_ALARM:
      snprintf(line1, sizeof(line1), "TIME REACHED!");
      snprintf(line2, sizeof(line2), "Take %s", TABLET_NAMES[alarms[currentAlarmIndex].tabletIndex]);
      break;
    case STATE_DISPENSING:
      if (currentAlarmIsMissed) {
        snprintf(line1, sizeof(line1), "DELAYED PILL:");
        snprintf(line2, sizeof(line2), "%s", TABLET_NAMES[alarms[currentAlarmIndex].tabletIndex]);
      } else {
        snprintf(line1, sizeof(line1), "Dispensing...");
        snprintf(line2, sizeof(line2), "%s", TABLET_NAMES[alarms[currentAlarmIndex].tabletIndex]);
      }
      break;
    case STATE_MONITOR_IR: {
      uint8_t secLeft = monitorTimer / 10;
      snprintf(line1, sizeof(line1), "IR1:%s IR2:%s",
               ir1_status ? "YES" : "NO ", ir2_status ? "YES" : "NO ");
      snprintf(line2, sizeof(line2), "Time Left: %02ds", secLeft);
      break;
    }
    case STATE_PILL_TAKEN:
      if (currentAlarmIsMissed) {
        snprintf(line1, sizeof(line1), "Delayed Pill OK");
      } else {
        snprintf(line1, sizeof(line1), "Pill Taken!");
      }
      if (missedCount > 0) {
        snprintf(line2, sizeof(line2), "%d more missed!", missedCount);
      } else {
        snprintf(line2, sizeof(line2), "Thank You!");
      }
      break;
    case STATE_WAITING_MISSED_PICKUP:
      snprintf(line1, sizeof(line1), "MISSED:%d QUEUE:%d", missedCount, queueCount);
      snprintf(line2, sizeof(line2), "Take pills ASAP!");
      break;
  }

  LCD2_PrintLine(0, line1);
  LCD2_PrintLine(1, line2);
}

// ============================================
// PERIPHERAL INITIALIZATION
// ============================================
void SystemClock_Config(void) {
  RCC->CR |= RCC_CR_HSION;
  while (!(RCC->CR & RCC_CR_HSIRDY));
  RCC->PLLCFGR = (84 << RCC_PLLCFGR_PLLN_Pos) | (8 << RCC_PLLCFGR_PLLM_Pos) |
                 (0 << RCC_PLLCFGR_PLLP_Pos) | RCC_PLLCFGR_PLLSRC_HSI;
  RCC->CR |= RCC_CR_PLLON;
  while (!(RCC->CR & RCC_CR_PLLRDY));
  FLASH->ACR = FLASH_ACR_ICEN | FLASH_ACR_DCEN | FLASH_ACR_LATENCY_2WS;
  RCC->CFGR |= RCC_CFGR_PPRE1_DIV2;
  RCC->CFGR |= RCC_CFGR_PPRE2_DIV1;
  RCC->CFGR |= RCC_CFGR_SW_PLL;
  while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL);
}

void GPIO_Init(void) {
  RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN;

  GPIOA->MODER &= ~GPIO_MODER_MODER0;
  GPIOA->MODER |= GPIO_MODER_MODER0_1;
  GPIOA->AFR[0] |= (1 << GPIO_AFRL_AFSEL0_Pos);

  GPIOA->MODER &= ~GPIO_MODER_MODER5;
  GPIOA->MODER |= GPIO_MODER_MODER5_0;

  GPIOA->MODER &= ~GPIO_MODER_MODER6;
  GPIOA->MODER |= GPIO_MODER_MODER6_0;

  GPIOA->MODER &= ~GPIO_MODER_MODER8;
  GPIOA->MODER |= GPIO_MODER_MODER8_1;
  GPIOA->OTYPER |= GPIO_OTYPER_OT8;
  GPIOA->PUPDR |= GPIO_PUPDR_PUPD8_0;
  GPIOA->AFR[1] |= (4 << GPIO_AFRH_AFSEL8_Pos);

  GPIOB->MODER &= ~(GPIO_MODER_MODER0 | GPIO_MODER_MODER1);
  GPIOB->PUPDR |= (GPIO_PUPDR_PUPD0_0 | GPIO_PUPDR_PUPD1_0);

  GPIOB->MODER &= ~(GPIO_MODER_MODER4 | GPIO_MODER_MODER5);
  GPIOB->MODER |= (GPIO_MODER_MODER4_0 | GPIO_MODER_MODER5_0);
  GPIOB->OTYPER |= (GPIO_OTYPER_OT4 | GPIO_OTYPER_OT5);
  GPIOB->PUPDR |= (GPIO_PUPDR_PUPD4_0 | GPIO_PUPDR_PUPD5_0);

  SOFT_SDA_HIGH();
  SOFT_SCL_HIGH();

  GPIOB->MODER &= ~(GPIO_MODER_MODER8 | GPIO_MODER_MODER9);
  GPIOB->MODER |= (GPIO_MODER_MODER8_1 | GPIO_MODER_MODER9_1);
  GPIOB->OTYPER |= (GPIO_OTYPER_OT8 | GPIO_OTYPER_OT9);
  GPIOB->PUPDR |= (GPIO_PUPDR_PUPD8_0 | GPIO_PUPDR_PUPD9_0);
  GPIOB->AFR[1] |= (4 << GPIO_AFRH_AFSEL8_Pos) | (4 << GPIO_AFRH_AFSEL9_Pos);

  GPIOC->MODER &= ~GPIO_MODER_MODER9;
  GPIOC->MODER |= GPIO_MODER_MODER9_1;
  GPIOC->OTYPER |= GPIO_OTYPER_OT9;
  GPIOC->PUPDR |= GPIO_PUPDR_PUPD9_0;
  GPIOC->AFR[1] |= (4 << GPIO_AFRH_AFSEL9_Pos);
}

void I2C1_Init(void) {
  RCC->APB1ENR |= RCC_APB1ENR_I2C1EN;
  I2C1->CR1 = 0;
  I2C1->CR2 = 42;
  I2C1->CCR = 210;
  I2C1->TRISE = 43;
  I2C1->CR1 |= I2C_CR1_PE;
}

void I2C3_Init(void) {
  RCC->APB1ENR |= RCC_APB1ENR_I2C3EN;
  I2C3->CR1 = 0;
  I2C3->CR2 = 42;
  I2C3->CCR = 210;
  I2C3->TRISE = 43;
  I2C3->CR1 |= I2C_CR1_PE;
}

void SoftI2C_Init(void) {
  SOFT_SDA_HIGH();
  SOFT_SCL_HIGH();
  DelayMs(10);
}

void TIM2_Init(void) {
  RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
  TIM2->PSC = 83;
  TIM2->ARR = 19999;
  TIM2->CCR1 = 1500;
  TIM2->CCMR1 |= (TIM_CCMR1_OC1M_2 | TIM_CCMR1_OC1M_1);
  TIM2->CCMR1 |= TIM_CCMR1_OC1PE;
  TIM2->CCER |= TIM_CCER_CC1E;
  TIM2->CR1 |= TIM_CR1_CEN;
}

void TIM3_Init(void) {
  RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;
  TIM3->PSC = 83;
  TIM3->ARR = 9999;
  TIM3->DIER |= TIM_DIER_UIE;
  NVIC_EnableIRQ(TIM3_IRQn);
  NVIC_SetPriority(TIM3_IRQn, 2);
  TIM3->CR1 |= TIM_CR1_CEN;
}

void TIM3_IRQHandler(void) {
  if (TIM3->SR & TIM_SR_UIF) {
    TIM3->SR &= ~TIM_SR_UIF;
    static uint16_t timer_100ms_counter = 0;
    static uint16_t timer_1s_counter = 0;

    timer_100ms_counter++;
    if (timer_100ms_counter >= 10) {
      timer_100ms_counter = 0;
      if (stateTimer > 0) { stateTimer--; }
      if (monitorTimer > 0) { monitorTimer--; }
      if (currentState == STATE_MONITOR_IR) {
        updateDisplay2Flag = true;
      }
    }

    timer_1s_counter++;
    if (timer_1s_counter >= 100) {
      timer_1s_counter = 0;
      updateDisplay1Flag = true;
      // Blink for missed alarms
      if (GetMissedAlarmCount() > 0 && currentState == STATE_IDLE) {
        missedAlarmBlinkTimer++;
        if (missedAlarmBlinkTimer % 2 == 0) {
          BuzzerOn();
          LED_On();
        } else {
          BuzzerOff();
          LED_Off();
        }
      }
    }
  }
}

// ============================================
// MAIN FUNCTION
// ============================================
int main(void) {
  SystemClock_Config();
  GPIO_Init();
  I2C1_Init();
  I2C3_Init();
  SoftI2C_Init();
  UART1_Init();
  TIM2_Init();
  TIM3_Init();

  DelayMs(500);
  RTC_Init();
  LCD1_Init();
  DelayMs(200);
  LCD2_Init();
  DelayMs(200);

  ServoSetAngle(0);
  BuzzerOff();
  LED_Off();
  currentState = STATE_IDLE;
  stateTimer = 0;
  monitorTimer = 0;
  missedAlarmBlinkTimer = 0;
  emailSentFor30Sec = false;
  updateDisplay1Flag = true;
  updateDisplay2Flag = true;

  // Initialize queue
  queueHead = 0;
  queueTail = 0;
  queueCount = 0;

  LCD1_Clear();
  LCD2_Clear();
  DelayMs(100);
  LCD1_PrintLine(0, "Pill Dispenser");
  LCD1_PrintLine(1, "System Booted");
  LCD2_PrintLine(0, "Initializing...");
  LCD2_PrintLine(1, "Ready");
  DelayMs(2000);
  LCD1_Clear();
  LCD2_Clear();

  while (1) {
    if (rtc_seconds != I2C1_Read(RTC_ADDRESS, 0x00)) {
      RTC_ReadTime();
      ResetAlarmStatuses();
      CheckAlarms();
    }

    if (updateDisplay1Flag) {
      UpdateDisplay1();
      updateDisplay1Flag = false;
    }

    if (updateDisplay2Flag) {
      UpdateDisplay2();
      updateDisplay2Flag = false;
    }

    if (uartCommandReady) {
      UART_ParseCommand(uartRxBuffer);
      uartCommandReady = false;
    }

    ir1_status = IR_Sensor1_PillPresent();
    ir2_status = IR_Sensor2_PillPresent();
    bothIRSensorsActive = ir1_status && ir2_status;

    switch (currentState) {
      case STATE_IDLE:
        // Check if there are missed alarms waiting to be picked up
        if (GetMissedAlarmCount() > 0 && !bothIRSensorsActive) {
          // Pill removed from missed alarm - mark as delayed taken
          for (uint8_t i = 0; i < current_num_alarms; i++) {
            if (alarms[i].status == ALARM_STATUS_MISSED) {
              alarms[i].status = ALARM_STATUS_DELAYED_TAKEN;
              char taken_msg[128];
              snprintf(taken_msg, sizeof(taken_msg),
                       "{\"cmd\":\"MISSED_PILL_TAKEN\",\"tablet\":\"%s\",\"alarm_id\":%d}\n",
                       TABLET_NAMES[alarms[i].tabletIndex], i);
              UART_SendString(taken_msg);
              BuzzerBeep(200);
              LED_On();
              DelayMs(200);
              LED_Off();
              updateDisplay2Flag = true;
              break; // Handle one at a time
            }
          }
        }

        // Try to process next queued alarm if system is idle
        if (queueCount > 0) {
          ProcessNextAlarmInQueue();
        }
        break;

      case STATE_BUZZER_ALARM:
        if (stateTimer == 0) {
          BuzzerOff();
          LED_Off();
          currentState = STATE_DISPENSING;
          stateTimer = 50;
          updateDisplay2Flag = true;
          ServoSetAngle(90);
        }
        break;

      case STATE_DISPENSING:
        if (stateTimer == 0) {
          ServoSetAngle(0);
          currentState = STATE_MONITOR_IR;
          monitorTimer = 700;
          emailSentFor30Sec = false;
          updateDisplay2Flag = true;
        }
        break;

      case STATE_MONITOR_IR:
        if (!bothIRSensorsActive) {
          // Pill taken successfully
          alarms[currentAlarmIndex].status = currentAlarmIsMissed ?
            ALARM_STATUS_DELAYED_TAKEN : ALARM_STATUS_TAKEN;
          currentState = STATE_PILL_TAKEN;
          stateTimer = 50;
          updateDisplay2Flag = true;
          BuzzerBeep(100);
          LED_On();
          DelayMs(100);
          LED_Off();
          char taken_msg[128];
          snprintf(taken_msg, sizeof(taken_msg),
                   "{\"cmd\":\"PILL_TAKEN\",\"tablet\":\"%s\",\"alarm_id\":%d,\"delayed\":%d}\n",
                   TABLET_NAMES[alarms[currentAlarmIndex].tabletIndex],
                   currentAlarmIndex,
                   currentAlarmIsMissed ? 1 : 0);
          UART_SendString(taken_msg);
        } else if (monitorTimer == 300 && !emailSentFor30Sec) {
          // 30 second warning
          emailSentFor30Sec = true;
          char email_msg[128];
          snprintf(email_msg, sizeof(email_msg),
                   "{\"cmd\":\"EMAIL_WARNING_30\",\"tablet\":\"%s\",\"alarm_id\":%d}\n",
                   TABLET_NAMES[alarms[currentAlarmIndex].tabletIndex],
                   currentAlarmIndex);
          UART_SendString(email_msg);
        } else if (monitorTimer == 0) {
          // Timeout - pill not taken, mark as MISSED
          alarms[currentAlarmIndex].status = ALARM_STATUS_MISSED;
          currentState = STATE_WAITING_MISSED_PICKUP;
          missedAlarmBlinkTimer = 0;
          updateDisplay2Flag = true;
          BuzzerOn();
          LED_On();
          char missed_msg[128];
          snprintf(missed_msg, sizeof(missed_msg),
                   "{\"cmd\":\"PILL_MISSED\",\"tablet\":\"%s\",\"alarm_id\":%d}\n",
                   TABLET_NAMES[alarms[currentAlarmIndex].tabletIndex],
                   currentAlarmIndex);
          UART_SendString(missed_msg);

          // Try to process next alarm in queue after brief delay
          DelayMs(1000);
          BuzzerOff();
          LED_Off();
          currentState = STATE_IDLE;

          // Process next alarm if any
          if (queueCount > 0) {
            ProcessNextAlarmInQueue();
          }
        }
        break;

      case STATE_PILL_TAKEN:
        if (stateTimer == 0) {
          currentState = STATE_IDLE;
          updateDisplay2Flag = true;
          // Try to process next alarm in queue
          if (queueCount > 0) {
            DelayMs(500); // Brief pause between pills
            ProcessNextAlarmInQueue();
          }
        }
        break;

      case STATE_WAITING_MISSED_PICKUP:
        // Check if pill was removed
        if (!bothIRSensorsActive) {
          BuzzerOff();
          LED_Off();
          alarms[currentAlarmIndex].status = ALARM_STATUS_DELAYED_TAKEN;
          char taken_msg[128];
          snprintf(taken_msg, sizeof(taken_msg),
                   "{\"cmd\":\"MISSED_PILL_TAKEN\",\"tablet\":\"%s\",\"alarm_id\":%d}\n",
                   TABLET_NAMES[alarms[currentAlarmIndex].tabletIndex],
                   currentAlarmIndex);
          UART_SendString(taken_msg);
          currentState = STATE_IDLE;
          updateDisplay2Flag = true;

          // Process next alarm if any
          if (queueCount > 0) {
            DelayMs(500);
            ProcessNextAlarmInQueue();
          }
        }
        break;
    } // switch (currentState)

    DelayMs(10);
  } // while (1)
} // main
