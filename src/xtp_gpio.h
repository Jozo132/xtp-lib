#pragma once

#include "xtp_config.h"
#include "xtp_dma.h"

void gpio_custom_init(void);



void gpio_setup() {
    analogReadResolution(12);

    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(BUTTON_pin, INPUT);

    pinMode(ENC_A_pin, INPUT);
    pinMode(ENC_B_pin, INPUT);

    pinMode(INPUT_0_pin, INPUT_PULLDOWN);
    pinMode(INPUT_1_pin, INPUT_PULLDOWN);
    pinMode(INPUT_2_pin, INPUT_PULLDOWN);
    pinMode(INPUT_3_pin, INPUT_PULLDOWN);
    pinMode(INPUT_4_pin, INPUT_PULLDOWN);
    pinMode(INPUT_5_pin, INPUT_PULLDOWN);
    pinMode(INPUT_6_pin, INPUT_PULLDOWN);
    pinMode(INPUT_7_pin, INPUT_PULLDOWN);
#ifdef XTP_14A6_E
    pinMode(INPUT_8_pin, INPUT_PULLDOWN);
    pinMode(INPUT_9_pin, INPUT_PULLDOWN);
#endif // XTP_14A6_E

    pinMode(OUTPUT_0_pin, OUTPUT);
    pinMode(OUTPUT_1_pin, OUTPUT);
    pinMode(OUTPUT_2_pin, OUTPUT);
    pinMode(OUTPUT_3_pin, OUTPUT);

    
#ifdef XTP_ADC_DMA // High-speed ADC with DMA
    ___XTP_initADC_DMA();
#else // Generic Arduino ADC
    pinMode(ANALOG_0_pin, INPUT_ANALOG);
    pinMode(ANALOG_1_pin, INPUT_ANALOG);
    pinMode(ANALOG_2_pin, INPUT_ANALOG);
    pinMode(ANALOG_3_pin, INPUT_ANALOG);
    pinMode(ANALOG_4_pin, INPUT_ANALOG);
    pinMode(ANALOG_5_pin, INPUT_ANALOG);
    pinMode(ANALOG_24V_pin, INPUT_ANALOG);
#endif // XTP_ADC_DMA

    // pinMode(MISC_0_pin, INPUT);
    // pinMode(MISC_1_pin, INPUT);
    // pinMode(MISC_2_pin, INPUT);
    // pinMode(MISC_3_pin, INPUT);
    // pinMode(MISC_4_pin, INPUT);
    // pinMode(MISC_5_pin, INPUT);

    
#ifdef XTP_GPIO_CUSTOM_INIT_FUNCTION
    gpio_custom_init();
#endif

}


#define vin_adc_r1 200e3
#define vin_adc_r2 10e3

constexpr float _voltageRatio = (vin_adc_r1 + vin_adc_r2) / vin_adc_r2 * 3.3 / 4095.0;


float readVoltage() {
    // return ((float) analogRead(ANALOG_24V_pin)) * _voltageRatio;
    return analogRead(ANALOG_24V_pin);
}