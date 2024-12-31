// tcd1304_reader.c
// Adapted from adc_sampler.c, with bits from the examples at
// https://github.com/raspberrypi/pico-examples/adc/
//
// PJ 2024-12-30: simple interpreter and simple sampling
//    2024-12-31: add adc_capture() function from adc_console example
//    2024-12-31: port to dedicated stripboard with TCD1304DG board attached.
//
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/adc.h"
#include "hardware/uart.h"
#include "pico/binary_info.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#define VERSION_STR "v0.1 2024-12-31 TCD1304DG linear-image-sensor reader"

const uint LED_PIN = PICO_DEFAULT_LED_PIN;
uint8_t override_led = 0;
const uint ICG_PIN = 16;

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
		for (size_t j=0; j < N_SAMPLES; ++j) {
			printf("%u\n", adc_samples[j]);
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

