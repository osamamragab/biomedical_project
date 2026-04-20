#define USE_ARDUINO_INTERRUPTS true
#include <Arduino.h>
#include <LCD_I2C.h>
#include <Adafruit_MLX90614.h>
#include <PulseSensorPlayground.h>
#include "pitches.h"

#define I2C_SDA_PIN    4        // analog
#define I2C_SCL_PIN    5        // analog
#define PULSE_PIN      0        // analog
#define RESTART_PIN    9        // digital
#define BUZZER_PIN     6        // digital
#define USS_TRIG_PIN   2        // digital
#define USS_ECHO_PIN   3        // digital
#define LED_NORM_PIN   8        // digital
#define LED_PULSE_PIN  4        // digital
#define LED_WARN_PIN   7        // digital
#define LED_READY_PIN 12        // digital
#define LED_ERROR_PIN 13        // digital

#define PULSE_THRESHOLD 550

#define LCD_ADDR 0x27
#define LCD_ROWS  2
#define LCD_COLS 16

#define TEMP_HIGH  37.0
#define TEMP_LOW   30.0
#define TEMP_CALIB  2.5

#define DELAY_DURATION      250     // ms
#define LOOP_DELAY          20     // ms
#define READ_DELAY_DURATION 200     // ms
#define USS_DURATION         10     // us
#define BUZZER_DURATION     200     // ms
#define CHECK_DISTANCE       10     // cm

bool running = false;
LCD_I2C lcd = LCD_I2C(LCD_ADDR, LCD_COLS, LCD_ROWS);
PulseSensorPlayground pulse = PulseSensorPlayground();
Adafruit_MLX90614 mlx = Adafruit_MLX90614();

int melody_temp_high[] = {
  NOTE_G4,
  NOTE_E4,
  NOTE_C4,
  NOTE_E4,
  NOTE_C4,
};
int melody_temp_high_durations[] = {
  120,
  120,
  200,
  120,
  300,
};

int melody_temp_norm[] = {
  NOTE_C4,
  NOTE_E4,
  NOTE_G4,
  NOTE_C5,
  NOTE_G4,
  NOTE_C5,
};
int melody_temp_norm_durations[] = {
  150,
  150,
  150,
  250,
  150,
  300,
};

double uss_get_distance() {
  digitalWrite(USS_TRIG_PIN, HIGH);
  delayMicroseconds(USS_DURATION);
  digitalWrite(USS_TRIG_PIN, LOW);
  unsigned long duration = pulseIn(USS_ECHO_PIN, HIGH);
  return (double) duration * 0.017;
}

void play_melody(int *melody, int *durations, size_t len) {
  for (int i = 0; i < len; i++) {
    tone(BUZZER_PIN, melody[i], durations[i]);
    int pause = durations[i] * 1.3;
    delay(pause);
  }
  noTone(BUZZER_PIN);
}

void setup() {
  Serial.begin(9600);
  while (!Serial);

  Serial.println("Initilizing pins");
  pinMode(LED_PULSE_PIN, OUTPUT);
  pinMode(LED_READY_PIN, OUTPUT);
  pinMode(LED_ERROR_PIN, OUTPUT);
  pinMode(LED_NORM_PIN, OUTPUT);
  pinMode(LED_WARN_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(USS_TRIG_PIN, OUTPUT);
  pinMode(USS_ECHO_PIN, INPUT);
  //pinMode(PULSE_PIN, INPUT);
  pinMode(RESTART_PIN, INPUT);

  Serial.println("Initilizing Pulse Sensor");
  pulse.analogInput(PULSE_PIN);
  pulse.blinkOnPulse(LED_PULSE_PIN);
  pulse.setThreshold(PULSE_THRESHOLD);
  while (!pulse.begin()) {
    Serial.println("Error connecting to Pulse Sensor. Check wiring.");
    digitalWrite(LED_ERROR_PIN, HIGH);
    delay(DELAY_DURATION);
  }

  Serial.println("Initilizing MLX90614");
  while (!mlx.begin()) {
    Serial.println("Error connecting to MLX sensor. Check wiring.");
    digitalWrite(LED_ERROR_PIN, HIGH);
    delay(DELAY_DURATION);
  }

  Serial.println("Initilizing LCD");
  lcd.begin(I2C_SDA_PIN, I2C_SCL_PIN, false);

  Serial.println("Resetting");
  reset();

  lcd.backlight();
  digitalWrite(LED_ERROR_PIN, LOW);
  Serial.println("Ready");
}

void loop() {
  digitalWrite(LED_READY_PIN, running ? LOW : HIGH);
  double distance = uss_get_distance();
  if (distance <= 0 || distance > CHECK_DISTANCE) {
    running = false;
    delay(DELAY_DURATION);
    return;
  }
  if (!running) {
    run();
  }
  delay(LOOP_DELAY);
}

void reset() {
  lcd.clear();
  lcd.setCursor(0, 0);
  digitalWrite(LED_NORM_PIN, LOW);
  digitalWrite(LED_WARN_PIN, LOW);
  digitalWrite(LED_READY_PIN, LOW);
  digitalWrite(LED_ERROR_PIN, LOW);
}

void run() {
  running = true;
  tone(BUZZER_PIN, 1000, 100);
  reset();
  delay(READ_DELAY_DURATION);
  run_temp();
  run_pulse();
}

void run_temp() {
  char temp_buf[5];
  double temp = mlx.readObjectTempC() + TEMP_CALIB;
  Serial.print("Temp: ");
  Serial.println(temp);
  lcd.print("Temp: ");
  lcd.print(dtostrf(temp, 4, 1, temp_buf));
  lcd.print("C");
  if (temp >= TEMP_HIGH || temp < TEMP_LOW) {
    digitalWrite(LED_WARN_PIN, HIGH);
    lcd.print(temp > 37.0 ? " HIGH" : " LOW");
    play_melody(melody_temp_high, melody_temp_high_durations, sizeof(melody_temp_high) / sizeof(melody_temp_high[0]));
  } else {
    digitalWrite(LED_NORM_PIN, HIGH);
    lcd.print(" NORM");
    play_melody(melody_temp_norm, melody_temp_norm_durations, sizeof(melody_temp_norm) / sizeof(melody_temp_norm[0]));
  }
}

void run_pulse() {
  int bpm = pulse.getBeatsPerMinute();
  digitalWrite(LED_PULSE_PIN, bpm > PULSE_THRESHOLD ? HIGH : LOW);
  lcd.setCursor(0, 1);
  Serial.print("BPM: ");
  Serial.println(bpm);
  lcd.print("BPM: ");
  lcd.print(bpm);
}
