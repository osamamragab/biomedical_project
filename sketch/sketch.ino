#define USE_ARDUINO_INTERRUPTS true
#include <Arduino.h>
#include <LCD_I2C.h>
#include <Adafruit_MLX90614.h>
#include <PulseSensorPlayground.h>

#define PULSE_PIN        (PIN_A0)      // analog
#define BUZZER_PIN       (6)           // digital
#define RESTART_PIN      (9)           // digital
#define USS_TRIG_PIN     (2)           // digital
#define USS_ECHO_PIN     (3)           // digital
#define LED_PULSE_PIN    (10)          // digital
#define LED_NORM_PIN     (8)           // digital
#define LED_WARN_PIN     (7)           // digital
#define LED_READY_PIN    (4)           // digital
#define LED_ERROR_PIN    (5)           // digital
#define LED_STATE_PIN    (LED_BUILTIN) // digital

#define USS_DURATION     (10)        // us
#define USS_ECHO_TIMEOUT (30 * 1000) // ms
#define USS_MIN_DISTANCE (5)         // cm

#define LCD_ADDR (0x27)
#define LCD_ROWS (2)
#define LCD_COLS (16)

#define TEMP_HIGH  (37.0)
#define TEMP_LOW   (30.0)

#define PULSE_THRESHOLD (500)
#define PULSE_BPM_LOW   (60)
#define PULSE_BPM_HIGH  (100)
#define PULSE_TIMEOUT   (5 * 1000) // ms

#define LENGTH(x) (sizeof(x) / sizeof(x[0]))

typedef enum State {
	STATE_INIT,
	STATE_READY,
	STATE_ERROR,
	STATE_RESET,
	STATE_RUN_TEMP,
	STATE_RUN_PULSE,
	STATE_RUN_OUTPUT,
} State;

State state_current = STATE_INIT;

char *state_string(State state) {
	switch (state) {
	case STATE_INIT:       return "INIT";
	case STATE_READY:      return "READY";
	case STATE_ERROR:      return "ERROR";
	case STATE_RESET:      return "RESET";
	case STATE_RUN_TEMP:   return "RUN_TEMP";
	case STATE_RUN_PULSE:  return "RUN_PULSE";
	case STATE_RUN_OUTPUT: return "RUN_OUTPUT";
	}
	return "UNKNOWN";
}

void state_set(State state) {
	if (state_current == state) return;
	digitalWrite(LED_STATE_PIN, HIGH);
	Serial.print("State change: ");
	Serial.print(state_string(state_current));
	Serial.print(" -> ");
	Serial.println(state_string(state));
	state_current = state;
	digitalWrite(LED_STATE_PIN, LOW);
}

LCD_I2C lcd = LCD_I2C(LCD_ADDR, LCD_COLS, LCD_ROWS);
PulseSensorPlayground pulse = PulseSensorPlayground();
Adafruit_MLX90614 mlx = Adafruit_MLX90614();

unsigned int melody_detect[]            = { 1000 };
unsigned long melody_detect_durations[] = { 80   };
unsigned int melody_bad[]               = { 392, 330, 262, 330, 262 };
unsigned long melody_bad_durations[]    = { 120, 120, 200, 120, 300 };
unsigned int melody_good[]              = { 262, 330, 392, 523, 392, 523 };
unsigned long melody_good_durations[]   = { 150, 150, 150, 250, 150, 300 };

void melody_play(unsigned int *melody, unsigned long *durations, size_t len) {
	for (size_t i = 0; i < len; i++) {
		tone(BUZZER_PIN, melody[i], durations[i]);
		delay(durations[i] * 1.3);
	}
	noTone(BUZZER_PIN);
}

short heart_shape = 0;
unsigned long heart_last_animation = 0;
unsigned char heart_char_big[8]   = { 0x00, 0x1b, 0x1f, 0x1f, 0x1f, 0x0e, 0x04, 0x00 };
unsigned char heart_char_small[8] = { 0x00, 0x0a, 0x1f, 0x1f, 0x0e, 0x04, 0x00, 0x00 };

void display_ready() {
	if (millis() - heart_last_animation < 400) return;
	heart_shape = !heart_shape;
	lcd.setCursor(0, 0);
	lcd.write(heart_shape);
	lcd.print("  Ready when  ");
	lcd.setCursor(15, 0);
	lcd.write(heart_shape);
	lcd.setCursor(0, 1);
	lcd.write(heart_shape);
	lcd.print("   you are!   ");
	lcd.setCursor(15, 1);
	lcd.write(heart_shape);
	heart_last_animation = millis();
}

void display_reset() {
	lcd.clear();
	digitalWrite(LED_NORM_PIN, LOW);
	digitalWrite(LED_WARN_PIN, LOW);
	digitalWrite(LED_READY_PIN, LOW);
	digitalWrite(LED_ERROR_PIN, LOW);
	digitalWrite(LED_PULSE_PIN, LOW);
}

double uss_get_distance() {
	digitalWrite(USS_TRIG_PIN, HIGH);
	delayMicroseconds(USS_DURATION);
	digitalWrite(USS_TRIG_PIN, LOW);
	unsigned long duration = pulseIn(USS_ECHO_PIN, HIGH, USS_ECHO_TIMEOUT);
	return duration * 0.017;
}

void setup() {
	Serial.begin(9600);
	while (!Serial);

	pinMode(LED_STATE_PIN, OUTPUT);
	pinMode(LED_READY_PIN, OUTPUT);
	pinMode(LED_ERROR_PIN, OUTPUT);
	pinMode(LED_NORM_PIN, OUTPUT);
	pinMode(LED_WARN_PIN, OUTPUT);
	pinMode(BUZZER_PIN, OUTPUT);
	pinMode(USS_TRIG_PIN, OUTPUT);
	pinMode(USS_ECHO_PIN, INPUT);
	pinMode(LED_PULSE_PIN, OUTPUT);
	pinMode(RESTART_PIN, INPUT_PULLUP);

	pulse.analogInput(PULSE_PIN);
	pulse.blinkOnPulse(LED_PULSE_PIN);
	pulse.setThreshold(PULSE_THRESHOLD);
	while (!pulse.begin()) {
		state_set(STATE_ERROR);
		Serial.println("Pulse Sensor init failed!");
		digitalWrite(LED_ERROR_PIN, HIGH);
		delay(250);
	}

	while (!mlx.begin()) {
		state_set(STATE_ERROR);
		Serial.println("MLX init failed!");
		digitalWrite(LED_ERROR_PIN, HIGH);
		delay(250);
	}

	lcd.begin(PIN_WIRE_SDA, PIN_WIRE_SCL, false);
	lcd.backlight();
	lcd.createChar(0, heart_char_small);
	lcd.createChar(1, heart_char_big);

	display_reset();
	state_set(STATE_READY);
}

double current_temp = 0.0;
int current_bpm = 0;
bool pulse_new = false;
unsigned long pulse_start_time = 0;
unsigned long output_start_time = 0;

void loop() {
	if (digitalRead(RESTART_PIN) == LOW) {
		// debounce
		delay(50);
		if (digitalRead(RESTART_PIN) == LOW) {
			digitalWrite(LED_ERROR_PIN, HIGH);
			state_set(STATE_RESET);
			// wait release
			while (digitalRead(RESTART_PIN) == LOW);
			digitalWrite(LED_ERROR_PIN, LOW);
		}
	}
	switch (state_current) {
	case STATE_INIT:
		// just wait and hope for the best
		delay(250);
		break;
	case STATE_ERROR:
		digitalWrite(LED_ERROR_PIN, HIGH);
		// wait and pray
		delay(250);
		break;
	case STATE_RESET:
		current_temp = 0.0;
		current_bpm = 0;
		pulse_new = false;
		pulse_start_time = 0;
		output_start_time = 0;
		display_reset();
		state_set(STATE_READY);
		break;
	case STATE_READY: {
		digitalWrite(LED_READY_PIN, HIGH);
		double distance = uss_get_distance();
		if (distance > 0 && distance <= USS_MIN_DISTANCE) {
			digitalWrite(LED_READY_PIN, LOW);
			melody_play(melody_detect, melody_detect_durations, LENGTH(melody_detect));
			display_reset();
			state_set(STATE_RUN_TEMP);
			break;
		}
		display_ready();
		break;
	}
	case STATE_RUN_TEMP:
		delay(250);
		current_temp = mlx.readObjectTempC();
		lcd.setCursor(0, 0);
		lcd.print("Temp: ");
		lcd.print(current_temp, 1);
		lcd.print("C ");
		if (current_temp >= TEMP_HIGH)    lcd.print("HIGH");
		else if (current_temp < TEMP_LOW) lcd.print(" LOW");
		else                              lcd.print("NORM");
		pulse_new = true;
		pulse_start_time = millis();
		state_set(STATE_RUN_PULSE);
		break;
	case STATE_RUN_PULSE:
		lcd.setCursor(0, 1);
		lcd.print("  Measuring...  ");
		if (pulse.sawStartOfBeat()) {
			current_bpm = pulse.getBeatsPerMinute();
			if (pulse_new) {
				pulse_new = false;
				pulse_start_time = millis();
				break;
			}
			pulse_start_time = millis();
			lcd.setCursor(0, 1);
			lcd.print("BPM:  ");
			lcd.print(current_bpm);
			lcd.print("   ");
			if (current_bpm < 100) lcd.print(" ");
			if (current_bpm > PULSE_BPM_HIGH)     lcd.print("HIGH");
			else if (current_bpm < PULSE_BPM_LOW) lcd.print(" LOW");
			else                                  lcd.print("NORM");
			state_set(STATE_RUN_OUTPUT);
			break;
		}
		if (millis() - pulse_start_time > PULSE_TIMEOUT) {
			current_bpm = 0;
			lcd.setCursor(0, 1);
			lcd.print("  No pulse! :(  ");
			state_set(STATE_RUN_OUTPUT);
		}
		break;
	case STATE_RUN_OUTPUT:
		if (output_start_time == 0) {
			output_start_time = millis();
			if (current_temp >= TEMP_LOW && current_temp < TEMP_HIGH && current_bpm >= PULSE_BPM_LOW && current_bpm <= PULSE_BPM_HIGH) {
				digitalWrite(LED_NORM_PIN, HIGH);
				melody_play(melody_good, melody_good_durations, LENGTH(melody_good));
			} else {
				digitalWrite(LED_WARN_PIN, HIGH);
				melody_play(melody_bad, melody_bad_durations, LENGTH(melody_bad));
			}
		}
		if (millis() - output_start_time > 5000) {
			output_start_time = 0;
			display_reset();
			state_set(STATE_READY);
		}
		break;
	}
	delay(20);
}
