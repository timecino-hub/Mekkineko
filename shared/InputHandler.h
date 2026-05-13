#ifndef INPUT_HANDLER_H
#define INPUT_HANDLER_H

#include <stdint.h>

typedef struct {
    uint8_t btn1_pressed;  // B1 (PC13) - pressed this frame (edge)
    uint8_t btn1_held;     // B1 (PC13) - currently held down (level)
    uint8_t btn2_pressed;  // BT2 (PC2) - shoot (Game 3)
    uint8_t btn2_held;     // BT2 (PC2) - currently held down (level)
    uint8_t btn3_pressed;  // BT3 (PC3) - exit / menu confirm
} InputState;

extern InputState current_input;

void Input_Init(void);
void Input_Read(void);

#endif
