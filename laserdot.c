/* laserdot.c - 2019/09/16 - Willy Tarreau - public domain
 *
 * This is a C AVR implementation of a PWM minimum value to control a laser
 * module. The idea is that on laser engravers it's mandatory to see the dot
 * to adjust the axis, and some modules do not provide a button to enable the
 * laser. Since the laser at very low power is harmless to whatever surface it
 * is sent to, this simply makes sure that a minimum duty cycle is always
 * enforced when the PWM is off for too long.
 *
 * The principle is to wait for a pulse for no more than 2 milliseconds. If it
 * does not happen in two milliseconds then a short pulse is sent to turn the
 * beam on. The pulse length is configured in the eeprom at address 0, as a
 * thousands of a period, thus with a maximum of 25% duty cycle. Value 0xFF is
 * considered unset and is replaced by the default value, 0x05 (0.5%). The
 * code is small enough to be flashed next to a Digispark's boot loader. It
 * can also be ported to an ATTINY13A (without boot loader though).
 *
 * The code is written in Arduino style using setup() and loop() so that it is
 * easily ported to other environments. But it has no external dependency.
 */

/* Internal frequency, 16 MHz for T25/45/85, 9.6 MHz for T13 */
#ifndef F_CPU
#error "Must set F_CPU to the CPU's frequency in Hz"
#endif

#include <avr/io.h>
#include <avr/eeprom.h>
#include <stdint.h>

/* intensity value for the pulse length in microseconds per millisecond, stored
 * in the EEPROM. Thus it corresponds to the duty cycle in thousands. Write it
 * using avrdude with "-U eeprom:w:" $value ":m". Example with 0x0A :
 *    $ avrdude ... -U eeprom:w:0x0a:m
 */
static const uint8_t intensity EEMEM;
static uint16_t pulse_high; // pulse width in cycles, high state
static uint16_t pulse_low;  // pulse width in cycles, low state

/* max loops with the signal not changing, that's about 1.25 milliseconds
 * at 16 MHz and 2 ms at 9.6 MHz.
 */
#define STUCK 4000

/* Convert microseconds to loops around an empty asm statement. Each loop was
 * measured to take 5 cycles on gcc 5.4 (vs 4 on 4.3)! This is limited to 64k
 * loops, or ~20ms at 8 MHz, 17ms at 9.6 MHz.
 */
static inline uint16_t us2loops(uint16_t us)
{
	return (uint32_t)us * (F_CPU / 100000) / 50 + 1;
}

/* Sleeps this number of cycles. Limited ~32ms at 8 MHz, 27ms at 9.6 MHz. */
static inline void delay(uint16_t loops)
{
	while (loops--)
		asm volatile("");
}

/* send a positive pulse of <width> cycles on port B<port> */
static inline void send_pulse(uint8_t port, uint16_t width)
{
	PORTB |= _BV(port);
	while (width--)
		asm volatile("");
	PORTB &= ~_BV(port);
}

/* Sleeps this number of microseconds. Limited ~32ms at 8 MHz, 27ms
 * at 9.6 MHz. May overflow on 8 MHz CPUs above 8 milliseconds at once.
 */
static inline void delay_us(uint16_t us)
{
	delay(us2loops(us));
}

/* send a positive pulse of <width> microseconds on port B<port> */
static inline void send_pulse_us(uint8_t port, uint16_t width)
{
	send_pulse(port, us2loops(width));
}

/* The loop is pretty simple. We wait for the input to change or a timeout to
 * appear. If the timeout strikes, we send a short pulse of <pulse> width in a
 * period of 1 ms. Otherwise we simply copy the signal and wait for the next
 * change. This way it covers disconnected inputs and zero as well.
 */
void loop(void)
{
	uint16_t tout;

	while (1) {
		tout = STUCK;
		while (!(PINB & _BV(PB0))) {
			if (__builtin_expect(!tout--, 0)) {
				send_pulse(PB1, pulse_high);
				delay(pulse_low);
				tout = STUCK;
			}
		}
		/* the pulse started */
		PORTB |= _BV(PB1);

		tout = STUCK;
		while (PINB & _BV(PB0)) {
#ifdef LIMIT_PULSE_UP
			if (__builtin_expect(!tout--, 0)) {
				send_pulse(PB1, pulse_high);
				delay(pulse_low);
				tout = STUCK;
			}
#endif
		}
		/* the pulse stopped */
		PORTB &= ~_BV(PB1);
	}
}

void setup(void)
{
	/* PB#  pin  dir  role
	 * PB0   5    in  PWM in
	 * PB1   6   out  PWM out
	 * PB2   7    -   unused
	 * PB3   2    -   unused
	 * PB4   3    -   unused
	 * PB5   1    -   RESET
	 */

	/* set output pins */
	DDRB = _BV(DDB1);

	/* enable pull-up on PB0 */
	PORTB |= _BV(PB0);

	/* stop pulse output on PB1 */
	PORTB &= ~_BV(PB1);

	pulse_high = eeprom_read_byte(&intensity);
	if (pulse_high == 0xff)
		pulse_high = 5; // 0.5% by default
	pulse_low  = us2loops(1000 - pulse_high);
	pulse_high = us2loops(pulse_high);
}

int main(void)
{
	setup();
#if TEST
	/* must produce exactly 1 KHz signal on PB1 */
	while (1) {
		delay_us(500);
		PORTB ^= _BV(PB1);
	}
#else
	while (1)
		loop();
#endif
}
