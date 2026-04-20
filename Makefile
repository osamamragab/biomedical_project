BOARD=arduino:avr:uno
PORT=/dev/ttyACM0

compile: sketch/sketch.ino
	arduino-cli compile -b ${BOARD} sketch
.PHONY: compile

upload: compile
	arduino-cli upload --verify -b ${BOARD} sketch
.PHONY: upload

serial: compile
	arduino-cli upload -b ${BOARD} --port /dev/ttyUSB0 sketch

deps:
	arduino-cli core update-index
	arduino-cli core install arduino:avr
	arduino-cli lib install "LCD_I2C"
	arduino-cli lib install "PulseSensor Playground"
	arduino-cli lib install "Adafruit MLX90614 Library"
.PHONY: deps
