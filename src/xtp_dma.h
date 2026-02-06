#pragma once

#include "xtp_config.h"

// Compile-time options (default off)
// #define XTP_ADC_USE_PB0
// #define XTP_ADC_USE_PB1

// ADC channel numbers on STM32F411:
// PB0 = ADC1_IN8, PB1 = ADC1_IN9
#ifdef XTP_ADC_USE_PB0
  #define XTP_ADC_PB0_CH 8
#endif
#ifdef XTP_ADC_USE_PB1
  #define XTP_ADC_PB1_CH 9
#endif

// Count extra PB channels
#if defined(XTP_ADC_USE_PB0) && defined(XTP_ADC_USE_PB1)
  #define XTP_ADC_PB_COUNT 2
#elif defined(XTP_ADC_USE_PB0) || defined(XTP_ADC_USE_PB1)
  #define XTP_ADC_PB_COUNT 1
#else
  #define XTP_ADC_PB_COUNT 0
#endif

// Channel counts per board variant
#if defined(XTP14A6E)
  constexpr uint8_t XTP_ADC_N_CH = 7 + XTP_ADC_PB_COUNT;  // PA0-PA4 + PC4 + PC5 [+ PB0] [+ PB1]
#else // XTP12A6E
  constexpr uint8_t XTP_ADC_N_CH = 6 + XTP_ADC_PB_COUNT;  // PA0-PA5 [+ PB0] [+ PB1]
#endif

__attribute__((aligned(4))) volatile uint16_t xtpAdcBuf[XTP_ADC_N_CH];
__attribute__((aligned(4))) volatile uint16_t xtpAdcBufSnapshot[XTP_ADC_N_CH];

inline void xtpAnalogGetAll() {
#ifdef XTP_ADC_DMA
    __DMB();
    memcpy((void*)xtpAdcBufSnapshot, (void*)xtpAdcBuf, sizeof(xtpAdcBuf));
#endif
}

/* ############# ADC + DMA ############# */

static bool adc_dma_setup_done = false;

static inline void ___XTP_dma_common_init() {
    __HAL_RCC_DMA2_CLK_ENABLE();
    __HAL_RCC_ADC1_CLK_ENABLE();

    DMA2_Stream0->CR = 0;
    DMA2_Stream0->PAR  = (uint32_t)&ADC1->DR;
    DMA2_Stream0->M0AR = (uint32_t)xtpAdcBuf;
    DMA2_Stream0->NDTR = XTP_ADC_N_CH;
    DMA2_Stream0->CR  |= (0 << 25) |                 // CHSEL = 0
                         (1 << 11) | (1 << 13) |     // PSIZE & MSIZE = 16-bit
                         DMA_SxCR_MINC | DMA_SxCR_CIRC |
                         DMA_SxCR_PL_1;
    DMA2_Stream0->CR  |= DMA_SxCR_EN;

    HAL_NVIC_SetPriority(DMA2_Stream0_IRQn, 0, 0);
    HAL_NVIC_SetPriority(ADC_IRQn, 1, 0);
}

static inline void ___XTP_tim3_trgo_init() {
    __HAL_RCC_TIM3_CLK_ENABLE();
    TIM3->PSC = 0;
    TIM3->ARR = 8399;          // example: 10 kHz @ 84 MHz
    TIM3->CR2 = (3 << 4);      // MMS=010: TRGO on update
    TIM3->CR1 = TIM_CR1_CEN;
}

#if defined(XTP14A6E)

static void ___XTP_initADC_DMA() {
    if (adc_dma_setup_done) return;
    adc_dma_setup_done = true;

    // GPIO INIT: PA0-4 + PC4 + PC5 (+ optional PB0/PB1)
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    GPIOA->MODER |= GPIO_MODER_MODE0 | GPIO_MODER_MODE1 | GPIO_MODER_MODE2 | GPIO_MODER_MODE3 | GPIO_MODER_MODE4;
    GPIOC->MODER |= GPIO_MODER_MODE4 | GPIO_MODER_MODE5;

#if defined(XTP_ADC_USE_PB0) || defined(XTP_ADC_USE_PB1)
    __HAL_RCC_GPIOB_CLK_ENABLE();
#ifdef XTP_ADC_USE_PB0
    GPIOB->MODER |= GPIO_MODER_MODE0; // PB0 analog
#endif
#ifdef XTP_ADC_USE_PB1
    GPIOB->MODER |= GPIO_MODER_MODE1; // PB1 analog
#endif
#endif

    ___XTP_dma_common_init();

    ADC1->CR2  = 0;
    ADC1->SQR1 = (XTP_ADC_N_CH - 1) << 20; // sequence length

    // Sequence order:
    // 1..5: ch0..4 (PA0..PA4)
    // 6:    ch15   (PC5)
    // 7:    ch14   (PC4)
    // 8:    ch8    (PB0) optional
    // 9:    ch9    (PB1) optional
    ADC1->SQR3 = (0) | (1 << 5) | (2 << 10) | (3 << 15) | (4 << 20) | (15 << 25);

#if defined(XTP_ADC_USE_PB0) && defined(XTP_ADC_USE_PB1)
    ADC1->SQR2 = (14 << 0) | (XTP_ADC_PB0_CH << 5) | (XTP_ADC_PB1_CH << 10); // ranks 7..9
#elif defined(XTP_ADC_USE_PB0)
    ADC1->SQR2 = (14 << 0) | (XTP_ADC_PB0_CH << 5); // ranks 7..8
#elif defined(XTP_ADC_USE_PB1)
    ADC1->SQR2 = (14 << 0) | (XTP_ADC_PB1_CH << 5); // ranks 7..8
#else
    ADC1->SQR2 = (14 << 0);                         // rank 7
#endif

    // Sampling time (keep consistent with your existing choices)
    // SMPR1: channels 10-17, SMPR2: channels 0-9
    ADC1->SMPR1 |= (1 << (3 * (14 - 10))) | (1 << (3 * (15 - 10))); // ch14, ch15
#ifdef XTP_ADC_USE_PB0
    ADC1->SMPR2 |= (1 << (3 * XTP_ADC_PB0_CH)); // ch8
#endif
#ifdef XTP_ADC_USE_PB1
    ADC1->SMPR2 |= (1 << (3 * XTP_ADC_PB1_CH)); // ch9
#endif

    ADC1->CR1 = ADC_CR1_SCAN;
    ADC1->CR2 = ADC_CR2_EXTSEL_3 | ADC_CR2_EXTEN_0 | ADC_CR2_DMA | ADC_CR2_DDS | ADC_CR2_ADON;

    ___XTP_tim3_trgo_init();
}

static inline int xtpAnalogRead(int pin) {
#ifndef XTP_ADC_DMA
    return analogRead(pin);
#else
    switch (pin) {
        case ANALOG_0_pin:     return xtpAdcBufSnapshot[0]; // PA0
        case ANALOG_1_pin:     return xtpAdcBufSnapshot[1]; // PA1
        case ANALOG_2_pin:     return xtpAdcBufSnapshot[2]; // PA2
        case ANALOG_3_pin:     return xtpAdcBufSnapshot[3]; // PA3
        case ANALOG_4_pin:     return xtpAdcBufSnapshot[4]; // PA4
        case ANALOG_5_pin:     return xtpAdcBufSnapshot[5]; // PC5 (ch15)
        case ANALOG_24V_pin:   return xtpAdcBufSnapshot[6]; // PC4 (ch14)
#if defined(XTP_ADC_USE_PB0) && defined(XTP_ADC_USE_PB1)
        case MISC_0_pin:       return xtpAdcBufSnapshot[7]; // PB0 (ch8)
        case MISC_1_pin:       return xtpAdcBufSnapshot[8]; // PB1 (ch9)
#elif defined(XTP_ADC_USE_PB0)
        case MISC_0_pin:       return xtpAdcBufSnapshot[7]; // PB0 (ch8)
#elif defined(XTP_ADC_USE_PB1)
        case MISC_0_pin:       return xtpAdcBufSnapshot[7]; // PB1 (ch9)
#endif
        default: return -1;
    }
#endif
}

#elif defined(XTP12A6E)

static void ___XTP_initADC_DMA() {
    if (adc_dma_setup_done) return;
    adc_dma_setup_done = true;

    // GPIO INIT: PA0-5 (+ optional PB0/PB1)
    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIOA->MODER |= GPIO_MODER_MODE0 | GPIO_MODER_MODE1 | GPIO_MODER_MODE2 |
                    GPIO_MODER_MODE3 | GPIO_MODER_MODE4 | GPIO_MODER_MODE5;

#if defined(XTP_ADC_USE_PB0) || defined(XTP_ADC_USE_PB1)
    __HAL_RCC_GPIOB_CLK_ENABLE();
#ifdef XTP_ADC_USE_PB0
    GPIOB->MODER |= GPIO_MODER_MODE0; // PB0 analog
#endif
#ifdef XTP_ADC_USE_PB1
    GPIOB->MODER |= GPIO_MODER_MODE1; // PB1 analog
#endif
#endif

    ___XTP_dma_common_init();

    ADC1->CR2  = 0;
    ADC1->SQR1 = (XTP_ADC_N_CH - 1) << 20; // sequence length

    // Sequence order:
    // 1..6: ch0..5 (PA0..PA5)
    // 7:    ch8    (PB0) optional
    // 8:    ch9    (PB1) optional
    ADC1->SQR3 = (0) | (1 << 5) | (2 << 10) | (3 << 15) | (4 << 20) | (5 << 25);

#if defined(XTP_ADC_USE_PB0) && defined(XTP_ADC_USE_PB1)
    ADC1->SQR2 = (XTP_ADC_PB0_CH << 0) | (XTP_ADC_PB1_CH << 5); // ranks 7..8
#elif defined(XTP_ADC_USE_PB0)
    ADC1->SQR2 = (XTP_ADC_PB0_CH << 0); // rank 7
#elif defined(XTP_ADC_USE_PB1)
    ADC1->SQR2 = (XTP_ADC_PB1_CH << 0); // rank 7
#else
    ADC1->SQR2 = 0;
#endif

    // Sampling time for ch0..5
    ADC1->SMPR2 = (1 << (3 * 0)) | (1 << (3 * 1)) | (1 << (3 * 2)) |
                  (1 << (3 * 3)) | (1 << (3 * 4)) | (1 << (3 * 5));
#ifdef XTP_ADC_USE_PB0
    ADC1->SMPR2 |= (1 << (3 * XTP_ADC_PB0_CH));
#endif
#ifdef XTP_ADC_USE_PB1
    ADC1->SMPR2 |= (1 << (3 * XTP_ADC_PB1_CH));
#endif

    ADC1->CR1 = ADC_CR1_SCAN;
    ADC1->CR2 = ADC_CR2_EXTSEL_3 | ADC_CR2_EXTEN_0 | ADC_CR2_DMA | ADC_CR2_DDS | ADC_CR2_ADON;

    ___XTP_tim3_trgo_init();
}

static inline int xtpAnalogRead(int pin) {
#ifndef XTP_ADC_DMA
    return analogRead(pin);
#else
    switch (pin) {
        case ANALOG_0_pin:     return xtpAdcBufSnapshot[0]; // PA0
        case ANALOG_1_pin:     return xtpAdcBufSnapshot[1]; // PA1
        case ANALOG_2_pin:     return xtpAdcBufSnapshot[2]; // PA2
        case ANALOG_3_pin:     return xtpAdcBufSnapshot[3]; // PA3
        case ANALOG_4_pin:     return xtpAdcBufSnapshot[4]; // PA4
        case ANALOG_5_pin:     return xtpAdcBufSnapshot[5]; // PA5
#if defined(XTP_ADC_USE_PB0) && defined(XTP_ADC_USE_PB1)
        case MISC_0_pin:       return xtpAdcBufSnapshot[6]; // PB0 (ch8)
        case MISC_1_pin:       return xtpAdcBufSnapshot[7]; // PB1 (ch9)
#elif defined(XTP_ADC_USE_PB0)
        case MISC_0_pin:       return xtpAdcBufSnapshot[6]; // PB0 (ch8)
#elif defined(XTP_ADC_USE_PB1)
        case MISC_0_pin:       return xtpAdcBufSnapshot[6]; // PB1 (ch9)
#endif
        case ANALOG_24V_pin:   return -1;
        default: return -1;
    }
#endif
}

#else
static inline void ___XTP_initADC_DMA() {}
static inline int xtpAnalogRead(int pin) { return analogRead(pin); }
#endif
