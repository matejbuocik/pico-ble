#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "temp.h"

void start_temp() {
	adc_init();
	adc_set_temp_sensor_enabled(true);
	adc_select_input(4);
}

float get_temp() {
	// 12 bit conversion, ADC_VREF == 3.3V
	const float conversion = 3.3f / (1<<12);

    float temp = (float)adc_read() * conversion;
	return 27.0f - (temp - 0.706f) / 0.001721f;
}
