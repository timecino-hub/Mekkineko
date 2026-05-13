#include "Game_1.h"
#include "CatEngine.h"
#include "InputHandler.h"
#include "Joystick.h"
#include "LCD.h"
#include "Buzzer.h"
#include "PWM.h"
#include "CatSprites.h"

#define FPS 24
#define FRAME_TIME_MS (1000 / FPS)

extern ST7789V2_cfg_t cfg0;
extern Joystick_cfg_t joystick_cfg;
extern Joystick_t joystick_data;
extern Buzzer_cfg_t buzzer_cfg;
extern PWM_cfg_t pwm_cfg;

MenuState Game1_Run(void)
{
    CatEngine_t engine;
    CatEngine_Init(&engine);

    uint32_t last_tick = HAL_GetTick();

    while (1) {
        uint32_t now = HAL_GetTick();
        if ((now - last_tick) < FRAME_TIME_MS) continue;
        last_tick = now;

        // INPUT
        Input_Read();
        Joystick_Read(&joystick_cfg, &joystick_data);
        UserInput input = Joystick_GetInput(&joystick_data);
        uint8_t btn_pet = current_input.btn2_held;

        // UPDATE (BT2 = pet/dialogue advance/choice confirm, joystick = cursor/navigate choice)
        CatEngine_Update(&engine, input, btn_pet);

        // RENDER
        LCD_Fill_Buffer(0);
        CatEngine_Draw(&engine);
        LCD_Refresh(&cfg0);

        // Natural ending or BT2 on finished screen → back to menu
        if (CatEngine_IsFinished(&engine)) {
            HAL_Delay(500);
            buzzer_off(&buzzer_cfg);
            PWM_SetDuty(&pwm_cfg, 0);
            return MENU_STATE_HOME;
        }
    }
}
