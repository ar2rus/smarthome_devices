.equ DHT22_MAXCOUNT0 =  6				;28 mkS * 1 MHz = 28 тактов / 7 =~ 6

;port aliases
.set DHT22_PORT = PORTB
.set DHT22_DDR  = DDRB
.set DHT22_PIN  = PINB

.set DHT22_P    = PB1