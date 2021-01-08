;avrdude -p t24 -P \\.\COM11 -c avrisp -b 19200 -U flash:w:hex\Transmitter.hex
avrdude -p t24 -c usbasp -U flash:w:hex\Transmitter24DHT.hex