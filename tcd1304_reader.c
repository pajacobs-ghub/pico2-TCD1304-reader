// tcd1304_reader.c
// Adapted from adc_sampler.c, with bits from the examples at
// https://github.com/raspberrypi/pico-examples/adc/
//
// PJ 2024-12-30: simple interpreter and simple sampling
//    2024-12-31: add adc_capture() function from adc_console example
//    2024-12-31: port to dedicated stripboard with TCD1304DG board attached.
//    2025-01-01: added period-setting command (via I2C to driver board)
//    2025-01-08: quick reporting of pixel data, using base64 encoding
//
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/adc.h"
#include "hardware/uart.h"
#include "hardware/i2c.h"
#include "pico/binary_info.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#define VERSION_STR "v0.3 2025-01-08 TCD1304DG linear-image-sensor reader"

const uint LED_PIN = PICO_DEFAULT_LED_PIN;
uint8_t override_led = 0;
const uint ICG_PIN = 16;

// I2C communication with PIC18F16Q41 driver board.
const uint SDA_PIN = 20;
const uint SCL_PIN = 21;
uint8_t msg_bytes[4];

// We want to capture a batch of samples.
const uint ADC_PIN = 26;
#define N_SAMPLES 3800
uint16_t adc_samples[N_SAMPLES];

void __not_in_flash_func(adc_capture)(uint16_t *buf, size_t count)
{
	adc_run(true);
	for (size_t i=0; i < count; i++) {
		buf[i] = adc_fifo_get_blocking();
	}
	adc_run(false);
	adc_fifo_drain();
	return;
}

// For incoming serial comms
#define NBUFA 80
char bufA[NBUFA];

int getstr(char* buf, int nbuf)
// Read (without echo) a line of characters into the buffer,
// stopping when we see a new-line character.
// Returns the number of characters collected,
// excluding the terminating null char.
{
    int i = 0;
    char c;
    uint8_t done = 0;
    while (!done) {
        c = getc(stdin);
        if (c != '\n' && c != '\r' && c != '\b' && i < (nbuf-1)) {
            // Append a normal character.
            buf[i] = c;
            i++;
        }
        if (c == '\n') {
            done = 1;
            buf[i] = '\0';
        }
        if (c == '\b' && i > 0) {
            // Backspace.
            i--;
        }
    }
    return i;
} // end getstr()

//   0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
const char base64_alphabet[64] = {
	'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P',
	'Q','R','S','T','U','V','W','X','Y','Z','a','b','c','d','e','f',
	'g','h','i','j','k','l','m','n','o','p','q','r','s','t','u','v',
	'w','x','y','z','0','1','2','3','4','5','6','7','8','9','+','/'
};

void interpret_command(char* cmdStr)
// A command that does not do what is expected should return a message
// that includes the word "error".
{
    char* token_ptr;
    const char* sep_tok = ", ";
    uint8_t i;
    // printf("DEBUG: cmdStr=%s", cmdStr);
    if (!override_led) gpio_put(LED_PIN, 1); // To indicate start of interpreter activity.
    switch (cmdStr[0]) {
	case 'v':
		printf("v %s\n", VERSION_STR);
		break;
	case 'L':
		// Turn LED on or off.
		// Turning the LED on by command overrides its use
		// as an indicator of interpreter activity.
		token_ptr = strtok(&cmdStr[1], sep_tok);
		if (token_ptr) {
			// Found some non-blank text; assume on/off value.
			// Use just the least-significant bit.
			i = (uint8_t) (atoi(token_ptr) & 1);
			gpio_put(LED_PIN, i);
			override_led = i;
			printf("L %d\n", i);
		} else {
			// There was no text to give a value.
			printf("L error: no value\n");
		}
		break;
	case 'a':
		// Sample the previously-initialized ADC channel.
		uint adc_raw = adc_read();
		printf("a %u\n", adc_raw);
		break;
	case 'b':
		// We want the sampling to start immediately on the rise of the ICG signal.
		while (gpio_get(ICG_PIN)) { /* wait */ }
		while (!gpio_get(ICG_PIN)) { /* wait */ }
		// Capture a batch of samples from the previously-initialized ADC channel.
		uint32_t start = time_us_32();
		adc_capture(adc_samples, N_SAMPLES);
		uint32_t time_taken = time_us_32() - start;
		float n = (float)N_SAMPLES;
		float mean = 0;
		for (size_t j=0; j < N_SAMPLES; ++j) {
			mean += (float)adc_samples[j];
		}
		mean /= n;
		float variance = 0;
		for (size_t j=0; j < N_SAMPLES; ++j) {
			float diff = (float)adc_samples[j] - mean;
			variance += diff * diff;
		}
		float stddev = sqrt(variance/(n-1.0f));
		printf("b %g %g %u\n", mean, stddev, time_taken);
		break;
	case 'r':
		// Report the values of previously-captured analog values.
		// Each uint16 value is formatted as a decimal integer and there is one per line.
		for (size_t j=0; j < N_SAMPLES; ++j) {
			printf("%u\n", adc_samples[j]);
		}
		break;
	case 'q':
		// Quickly report the values of previously-captured analog values.
		// Each 12-bit value is formatted as a pair of characters using the base64 alphabet.
		// There are 20 values per line so N_SAMPLES needs to be an exact multiple of 20.
		for (size_t j=0; j < N_SAMPLES/20; ++j) {
			for (size_t k=0; k < 20; ++k) {
				uint16_t val = adc_samples[j*20 + k];
				char hi = base64_alphabet[(val & 0x0FFF) >> 6];
				char lo = base64_alphabet[val & 0x003F];
				printf("%c%c", hi, lo);
			}
			printf("\n");
		}
		break;
	case 'p':
		// Set the SH and ICG periods (counts of microseconds).
		// The clocking out of the Vos data takes about 7.5 milliseconds,
		// so a good minimum value of us_ICG is 8000.
		// To keep the signals aligned, us_ICG needs to be a multiple of us_SH.
		// For example a command that works nicely (on my desktop setup) is
		// p 200 10000\n
		// These periods are the default values in the PIC18 MCU.
		// To get a longer exposure and read a bit quicker the command might be
		// p 400 8000\n
		// This saturates the sensor under my lighting conditions.
		//
		token_ptr = strtok(&cmdStr[1], sep_tok);
		if (token_ptr) {
			// Found some non-blank text; assume on/off value.
			// Use just the least-significant bit.
			uint16_t us_SH = (uint16_t) atoi(token_ptr);
			token_ptr = strtok(NULL, sep_tok);
			if (token_ptr) {
				uint16_t us_ICG = (uint16_t) atoi(token_ptr);
				// Big-endian layout of bytes in message.
				msg_bytes[0] = (uint8_t) ((us_SH & 0xff00) >> 8);
				msg_bytes[1] = (uint8_t) (us_SH & 0x00ff);
				msg_bytes[2] = (uint8_t) ((us_ICG & 0xff00) >> 8);
				msg_bytes[3] = (uint8_t) (us_ICG & 0x00ff);
				uint8_t addr = 0x51;
				int nresult = i2c_write_blocking(i2c0, addr, msg_bytes, 4, false);
				if (nresult != 4) {
					printf("p error: unsuccessful I2C communication\n");
				} else {
					// Successfully sent the I2C message; report the values sent.
					printf("p %d %d\n", us_SH, us_ICG);
				}
			} else {
				printf("p error: no value for us_ICG\n");
			}
		} else {
			// There was no text to give a value.
			printf("p error: no value for us_SH (nor us_ICG)\n");
		}
		break;
	default:
		printf("%c error: Unknown command\n", cmdStr[0]);
    }
    if (!override_led) gpio_put(LED_PIN, 0); // To indicate end of interpreter activity.
} // end interpret_command()


int main()
{
    stdio_init_all();
    // Some information for picotool.
    bi_decl(bi_program_description(VERSION_STR));
    bi_decl(bi_1pin_with_name(ADC_PIN, "ADC input pin"));
    bi_decl(bi_1pin_with_name(LED_PIN, "LED output pin"));
	bi_decl(bi_1pin_with_name(ICG_PIN, "ICG sense pin (digital input)"));
	bi_decl(bi_2pins_with_func(SDA_PIN, SCL_PIN, GPIO_FUNC_I2C));
    //
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
	gpio_init(ICG_PIN);
	gpio_set_dir(ICG_PIN, GPIO_IN);
    //
    adc_init();
    adc_gpio_init(ADC_PIN);
    adc_select_input(0);
	adc_fifo_setup(true, false, 0, false, false); // Just the FIFO, not the DMA
	//
	i2c_init(i2c0, 100*1000);
	gpio_set_function(SDA_PIN, GPIO_FUNC_I2C);
	gpio_set_function(SCL_PIN, GPIO_FUNC_I2C);
	gpio_pull_up(SDA_PIN);
	gpio_pull_up(SCL_PIN);
    //
    while (1) {
        // Characters are not echoed as they are typed.
        // Backspace deleting is allowed.
        // NL (Ctrl-J) signals end of incoming string.
        int m = getstr(bufA, NBUFA);
        // Note that the cmd string may be of zero length,
        // with the null character in the first place.
        // If that is the case, do nothing with it.
        if (m > 0) {
            interpret_command(bufA);
        }
    }
    return 0;
}

