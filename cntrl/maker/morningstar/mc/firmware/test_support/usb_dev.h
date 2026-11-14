#pragma once
#include <stdint.h>

constexpr unsigned IRQ_USB1 = 1;
constexpr uint32_t USB_USBCMD_RS = 1;
extern volatile uint8_t usb_configuration;
extern volatile uint32_t USB1_USBCMD, USB1_ENDPTFLUSH;

uint32_t mock_nvic_is_enabled(unsigned irq);
void mock_nvic_disable(unsigned irq);
void mock_nvic_enable(unsigned irq);
#define NVIC_IS_ENABLED(irq) mock_nvic_is_enabled(irq)
#define NVIC_DISABLE_IRQ(irq) mock_nvic_disable(irq)
#define NVIC_ENABLE_IRQ(irq) mock_nvic_enable(irq)
