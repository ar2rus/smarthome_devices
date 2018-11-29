cradle:
	terminals: 2 - 8
	resistence: normal -> inf; pushed -> 0
	scheme: 2 -> GND; 8 -> PIN, pulled up (internal 50K)
	debounce: 100ms
	levels: HIGH -> put down; LOW -> picked up
	
dial activated:
	terminal: BLACK - GREEN
	resistence:	normal -> inf; activated -> 0
	scheme: BLACK -> GND; GREEN -> PIN, pulled up (internal 50K)
	debounce: off
	levels: HIGH -> inactive; LOW -> active
	
dial impulses:
	terminal: BLUE - YELLOW
	resistence: normal -> inf; activated -> 0; impulse -> inf
	scheme: BLUE -> GND; YELLOW -> PIN, pulled up (internal 50K)
	debounce: off
	levels: HIGH (duration = 60ms, use 5-300) -> impulse; HIGH->[LOW]->HIGH (duration = 40, use 5-250)
	example: digit 3
		364549: "GPIO 3" <GPIO 3> = LOW		
		101: "GPIO 2" <GPIO 2> = LOW
		851: "GPIO 2" <GPIO 2> = HIGH
		67: "GPIO 2" <GPIO 2> = LOW
		39: "GPIO 2" <GPIO 2> = HIGH
		64: "GPIO 2" <GPIO 2> = LOW
		41: "GPIO 2" <GPIO 2> = HIGH
		120: "GPIO 3" <GPIO 3> = HIGH

	
max delay between dial of digits = 2000 ms (dial inactivated -> dial activated)