#pragma once

#include "xtp_config.h"

#ifdef XTP14A6E
constexpr uint8_t  XTP_ADC_N_CH = 7;     // PA0-PA4 + PC4 + PC5
#else // XTP12A6E
constexpr uint8_t  XTP_ADC_N_CH = 6;     // PA0-PA5
#endif // XTP14A6E

volatile uint16_t  xtpAdcBuf[XTP_ADC_N_CH];
volatile uint16_t xtpAdcBufSnapshot[XTP_ADC_N_CH];


void xtpAnalogGetAll() {
#ifdef XTP_ADC_DMA
    memcpy((void*) xtpAdcBufSnapshot, (void*) xtpAdcBuf, sizeof(xtpAdcBuf));
#endif
}

/* ############# ADC + DMA ############# */
#if defined XTP14A6E
static void ___XTP_initADC_DMA() {
    // XTP14A6E has 7 channels: PA0-5 + PC5
    // GPIO INIT    
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    GPIOA->MODER |= GPIO_MODER_MODE0 | GPIO_MODER_MODE1 | GPIO_MODER_MODE2 | GPIO_MODER_MODE3 | GPIO_MODER_MODE4 | GPIO_MODER_MODE5; // PA0-5 analog
    GPIOC->MODER |= GPIO_MODER_MODE5;               // PC5 analog

    // DMA INIT
    __HAL_RCC_DMA2_CLK_ENABLE();
    __HAL_RCC_ADC1_CLK_ENABLE();

    DMA2_Stream0->CR = 0;                           // reset
    DMA2_Stream0->PAR = (uint32_t) &ADC1->DR;       // ADC data reg
    DMA2_Stream0->M0AR = (uint32_t) xtpAdcBuf;      // buffer base
    DMA2_Stream0->NDTR = XTP_ADC_N_CH;              // items
    DMA2_Stream0->CR |= (0 << 25) |                 // CHSEL = 0
        (1 << 11) | (1 << 13) |                     // PSIZE & MSIZE = 16 bit
        DMA_SxCR_MINC | DMA_SxCR_CIRC;              // mem inc, circular
    DMA2_Stream0->CR |= DMA_SxCR_EN;                // enable DMA

    ADC1->CR2 = 0;
    ADC1->SQR1 = (XTP_ADC_N_CH - 1) << 20;                  // sequence length 6
    ADC1->SQR3 = 0 | (1 << 5) | (2 << 10) | (3 << 15) | (4 << 20) | (15 << 25);     // ch0-4,15
    ADC1->SMPR2 = 1 | (1 << 3) | (1 << 6) | (1 << 9) | (1 << 12);                   // ch0-4 samp-time
    ADC1->SMPR1 = 1 << (3 * (15 - 10));             // ch15 samp-time
    ADC1->CR1 = ADC_CR1_SCAN;                       // scan mode
    ADC1->CR2 = ADC_CR2_EXTSEL_3 |                  // EXTSEL = 8 : TIM3_TRGO
        ADC_CR2_EXTEN_0 |                           // rising edge
        ADC_CR2_DMA | ADC_CR2_DDS |                 // DMA cont.
        ADC_CR2_ADON;                               // ADC on
}
int xtpAnalogRead(int pin) {
#ifndef XTP_ADC_DMA
    return analogRead(pin);
#else
    switch (pin) {
        case ANALOG_0_pin: return xtpAdcBufSnapshot[0];   // PA0
        case ANALOG_1_pin: return xtpAdcBufSnapshot[1];   // PA1
        case ANALOG_2_pin: return xtpAdcBufSnapshot[2];   // PA2
        case ANALOG_3_pin: return xtpAdcBufSnapshot[3];   // PA3
        case ANALOG_4_pin: return xtpAdcBufSnapshot[4];   // PA4
        case ANALOG_24V_pin: return xtpAdcBufSnapshot[5]; // PC4
        case ANALOG_5_pin: return xtpAdcBufSnapshot[6];   // PC5
        default: return -1;
    }
#endif // XTP_ADC_DMA
}
#elif defined XTP12A6E
static void ___XTP_initADC_DMA() {
    // XTP12A6E has 6 channels: PA0-PA5
    // GPIO INIT
    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIOA->MODER |= GPIO_MODER_MODE0 | GPIO_MODER_MODE1 | GPIO_MODER_MODE2 | GPIO_MODER_MODE3 | GPIO_MODER_MODE4 | GPIO_MODER_MODE5; // PA0-5 analog

    // DMA INIT
    __HAL_RCC_DMA2_CLK_ENABLE();
    __HAL_RCC_ADC1_CLK_ENABLE();
    DMA2_Stream0->CR = 0;                           // reset
    DMA2_Stream0->PAR = (uint32_t) &ADC1->DR;       // ADC data reg
    DMA2_Stream0->M0AR = (uint32_t) xtpAdcBuf;      // buffer base
    DMA2_Stream0->NDTR = XTP_ADC_N_CH;              // items
    DMA2_Stream0->CR |= (0 << 25) |                 // CHSEL = 0
        (1 << 11) | (1 << 13) |                     // PSIZE & MSIZE = 16 bit
        DMA_SxCR_MINC | DMA_SxCR_CIRC;              // mem inc, circular
    DMA2_Stream0->CR |= DMA_SxCR_EN;                // enable DMA
    ADC1->CR2 = 0;
    ADC1->SQR1 = (XTP_ADC_N_CH - 1) << 20;                  // sequence length 6
    ADC1->SQR3 = 0 | (1 << 5) | (2 << 10) | (3 << 15) | (4 << 20) | (5 << 25);      // ch0-5
    ADC1->SMPR2 = 1 | (1 << 3) | (1 << 6) | (1 << 9) | (1 << 12) | (1 << 15);       // ch0-5 samp-time
    ADC1->CR1 = ADC_CR1_SCAN;                       // scan mode
    ADC1->CR2 = ADC_CR2_EXTSEL_3 |                  // EXTSEL = 8 : TIM3_TRGO
        ADC_CR2_EXTEN_0 |                           // rising edge
        ADC_CR2_DMA | ADC_CR2_DDS |                 // DMA cont.
        ADC_CR2_ADON;                               // ADC on
}
int xtpAnalogRead(int pin) {
#ifndef XTP_ADC_DMA
    return analogRead(pin);
#else
    switch (pin) {
        case ANALOG_0_pin: return xtpAdcBufSnapshot[0]; // PA0
        case ANALOG_1_pin: return xtpAdcBufSnapshot[1]; // PA1
        case ANALOG_2_pin: return xtpAdcBufSnapshot[2]; // PA2
        case ANALOG_3_pin: return xtpAdcBufSnapshot[3]; // PA3
        case ANALOG_4_pin: return xtpAdcBufSnapshot[4]; // PA4
        case ANALOG_5_pin: return xtpAdcBufSnapshot[5]; // PA5
        case ANALOG_24V_pin: return -1;                 // Not available
        default: return -1;
    }
#endif // XTP_ADC_DMA
}
#else  // Unknown board variant
static void ___XTP_initADC_DMA() {} // Unknown board variant, do nothing
inline int xtpAnalogRead(int pin) {
    return analogRead(pin);
}
#endif // XTP14A6E