PIO ?= pio
FIRMWARE_DIR := firmware

.PHONY: all test build upload clean

all: build

test:
	cd $(FIRMWARE_DIR) && $(PIO) test -e native

build:
	cd $(FIRMWARE_DIR) && $(PIO) run -e teensy41

upload: build
	cd $(FIRMWARE_DIR) && $(PIO) run -e teensy41 -t upload

clean:
	cd $(FIRMWARE_DIR) && $(PIO) run -e teensy41 -t clean
