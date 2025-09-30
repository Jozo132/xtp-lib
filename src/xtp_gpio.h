#pragma once

#include "xtp_config.h"
#include "xtp_dma.h"

void gpio_custom_init(void);


/* Port A, B, C */
#define PX0_MSK   (1U << 0)
#define PX1_MSK   (1U << 1)
#define PX2_MSK   (1U << 2)
#define PX3_MSK   (1U << 3)
#define PX4_MSK   (1U << 4)
#define PX5_MSK   (1U << 5)
#define PX6_MSK   (1U << 6)
#define PX7_MSK   (1U << 7)
#define PX8_MSK   (1U << 8)
#define PX9_MSK   (1U << 9)
#define PX10_MSK  (1U << 10)
#define PX11_MSK  (1U << 11)
#define PX12_MSK  (1U << 12)
#define PX13_MSK  (1U << 13)
#define PX14_MSK  (1U << 14)
#define PX15_MSK  (1U << 15)

bool gpio_setup_done = false;
void gpio_setup() {
    if (gpio_setup_done) return;
    gpio_setup_done = true;
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

    pinMode(ETH_RST_pin, OUTPUT);
    digitalWrite(ETH_RST_pin, HIGH);

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


bool readInput(int pin) {
    switch (pin) {
#ifdef XTP_12A6_E
        // case INPUT_0_pin: return HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_10); // PC10
        case INPUT_0_pin: return (GPIOC->IDR & PX10_MSK) != 0; // PC10
        case INPUT_1_pin: return (GPIOA->IDR & PX15_MSK) != 0; // PA15
        case INPUT_2_pin: return (GPIOA->IDR & PX12_MSK) != 0; // PA12
        case INPUT_3_pin: return (GPIOA->IDR & PX11_MSK) != 0; // PA11
        case INPUT_4_pin: return (GPIOA->IDR & PX8_MSK) != 0; // PA8
        case INPUT_5_pin: return (GPIOC->IDR & PX9_MSK) != 0; // PC9
        case INPUT_6_pin: return (GPIOC->IDR & PX8_MSK) != 0; // PC8
        case INPUT_7_pin: return (GPIOC->IDR & PX7_MSK) != 0; // PC7
        case  BUTTON_pin: return (GPIOB->IDR & PX2_MSK) != 0; // PB2
#else // XTP_14A6_E
        // case INPUT_0_pin: return HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_12); // PC12
        case INPUT_0_pin: return (GPIOC->IDR & PX12_MSK) != 0; // PC12
        case INPUT_1_pin: return (GPIOC->IDR & PX11_MSK) != 0; // PC11
        case INPUT_2_pin: return (GPIOC->IDR & PX10_MSK) != 0; // PC10
        case INPUT_3_pin: return (GPIOA->IDR & PX15_MSK) != 0; // PA15
        case INPUT_4_pin: return (GPIOA->IDR & PX12_MSK) != 0; // PA12
        case INPUT_5_pin: return (GPIOA->IDR & PX11_MSK) != 0; // PA11
        case INPUT_6_pin: return (GPIOA->IDR & PX8_MSK) != 0; // PA8
        case INPUT_7_pin: return (GPIOC->IDR & PX9_MSK) != 0; // PC9
        case INPUT_8_pin: return (GPIOC->IDR & PX8_MSK) != 0; // PC8
        case INPUT_9_pin: return (GPIOC->IDR & PX7_MSK) != 0; // PC7
        case  BUTTON_pin: return (GPIOB->IDR & PX2_MSK) != 0; // PB2
#endif
        default: return false;
    }
}

void writeOutput(int pin, int value) {
    switch (pin) {
#ifdef XTP_12A6_E
        // case OUTPUT_0_pin: return HAL_GPIO_WritePin(GPIOC, GPIO_PIN_0, value != 0 ? GPIO_PIN_SET : GPIO_PIN_RESET); // PC0
        // case OUTPUT_1_pin: return HAL_GPIO_WritePin(GPIOC, GPIO_PIN_1, value != 0 ? GPIO_PIN_SET : GPIO_PIN_RESET); // PC1
        // case OUTPUT_2_pin: return HAL_GPIO_WritePin(GPIOC, GPIO_PIN_2, value != 0 ? GPIO_PIN_SET : GPIO_PIN_RESET); // PC2
        // case OUTPUT_3_pin: return HAL_GPIO_WritePin(GPIOC, GPIO_PIN_3, value != 0 ? GPIO_PIN_SET : GPIO_PIN_RESET); // PC3
        case OUTPUT_0_pin: GPIOC->BSRR = value ? PX0_MSK : (PX0_MSK << 16); return; // PC0
        case OUTPUT_1_pin: GPIOC->BSRR = value ? PX1_MSK : (PX1_MSK << 16); return; // PC1
        case OUTPUT_2_pin: GPIOC->BSRR = value ? PX2_MSK : (PX2_MSK << 16); return; // PC2
        case OUTPUT_3_pin: GPIOC->BSRR = value ? PX3_MSK : (PX3_MSK << 16); return; // PC3
#else // XTP_14A6_E
        // case OUTPUT_0_pin: return HAL_GPIO_WritePin(GPIOC, GPIO_PIN_0, value != 0 ? GPIO_PIN_SET : GPIO_PIN_RESET); // PC0
        // case OUTPUT_1_pin: return HAL_GPIO_WritePin(GPIOC, GPIO_PIN_1, value != 0 ? GPIO_PIN_SET : GPIO_PIN_RESET); // PC1
        // case OUTPUT_2_pin: return HAL_GPIO_WritePin(GPIOC, GPIO_PIN_2, value != 0 ? GPIO_PIN_SET : GPIO_PIN_RESET); // PC2
        // case OUTPUT_3_pin: return HAL_GPIO_WritePin(GPIOC, GPIO_PIN_3, value != 0 ? GPIO_PIN_SET : GPIO_PIN_RESET); // PC3
        case OUTPUT_0_pin: GPIOC->BSRR = value ? PX0_MSK : (PX0_MSK << 16); return; // PC0
        case OUTPUT_1_pin: GPIOC->BSRR = value ? PX1_MSK : (PX1_MSK << 16); return; // PC1
        case OUTPUT_2_pin: GPIOC->BSRR = value ? PX2_MSK : (PX2_MSK << 16); return; // PC2
        case OUTPUT_3_pin: GPIOC->BSRR = value ? PX3_MSK : (PX3_MSK << 16); return; // PC3
#endif
        case LED_BUILTIN: GPIOC->BSRR = value ? PX13_MSK : (PX13_MSK << 16); return; // PC13
        default: break;
    }
}


#define vin_adc_r1 200e3
#define vin_adc_r2 10e3

constexpr float _voltageRatio = (vin_adc_r1 + vin_adc_r2) / vin_adc_r2 * 3.3 / 4095.0;


float readVoltage() {
    // return ((float) analogRead(ANALOG_24V_pin)) * _voltageRatio;
    return xtpAnalogRead(ANALOG_24V_pin);
}