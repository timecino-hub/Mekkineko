#include "InputHandler.h"
#include "main.h"

InputState current_input = {0};

static volatile uint8_t btn1_raw_press = 0;
static volatile uint8_t btn2_raw_press = 0;
static volatile uint8_t btn3_raw_press = 0;

void Input_Init(void)
{
    current_input.btn1_pressed = 0;
    current_input.btn2_pressed = 0;
    current_input.btn3_pressed = 0;
    btn1_raw_press = 0;
    btn2_raw_press = 0;
    btn3_raw_press = 0;
}

void Input_Read(void)
{
    current_input.btn1_pressed = btn1_raw_press;
    current_input.btn2_pressed = btn2_raw_press;
    current_input.btn3_pressed = btn3_raw_press;
    btn1_raw_press = 0;
    btn2_raw_press = 0;
    btn3_raw_press = 0;

    // btn1_held/btn2_held: read actual pin state (active low)
    current_input.btn1_held = (HAL_GPIO_ReadPin(B1_GPIO_Port, B1_Pin) == GPIO_PIN_RESET);
    current_input.btn2_held = (HAL_GPIO_ReadPin(GPIOC, BTN2_Pin) == GPIO_PIN_RESET);
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    static uint32_t last_btn1_interrupt = 0;
    static uint32_t last_btn2_interrupt = 0;
    static uint32_t last_btn3_interrupt = 0;
    uint32_t now = HAL_GetTick();

    if (GPIO_Pin == B1_Pin) {
        if ((now - last_btn1_interrupt) > 80) {
            last_btn1_interrupt = now;
            btn1_raw_press = 1;
        }
    }
    if (GPIO_Pin == BTN2_Pin) {
        if ((now - last_btn2_interrupt) > 200) {
            last_btn2_interrupt = now;
            HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
            btn2_raw_press = 1;
        }
    }
    if (GPIO_Pin == BTN3_Pin) {
        if ((now - last_btn3_interrupt) > 200) {
            last_btn3_interrupt = now;
            HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
            btn3_raw_press = 1;
        }
    }
}
