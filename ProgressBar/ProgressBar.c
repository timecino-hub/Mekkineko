#include "ProgressBar.h"
#include "LCD.h"
#include <string.h>

void ProgressBar_Init(ProgressBar_t* bar, int16_t x, int16_t y,
                      int16_t w, int16_t h, uint8_t color_fill,
                      uint8_t color_bg, uint8_t color_border,
                      const char* label)
{
    bar->pos.x = x;
    bar->pos.y = y;
    bar->width  = w;
    bar->height = h;
    bar->value  = 0;
    bar->color_fill   = color_fill;
    bar->color_bg     = color_bg;
    bar->color_border = color_border;
    bar->visible = 1;
    strncpy(bar->label, label, 7);
    bar->label[7] = '\0';
}

void ProgressBar_SetValue(ProgressBar_t* bar, uint8_t value)
{
    if (value > 100) value = 100;
    bar->value = value;
}

void ProgressBar_Draw(ProgressBar_t* bar)
{
    if (!bar->visible) return;

    // Draw label text with black outline at 2x size
    LCD_printString(bar->label, bar->pos.x-1, bar->pos.y,   15, 2);
    LCD_printString(bar->label, bar->pos.x+1, bar->pos.y,   15, 2);
    LCD_printString(bar->label, bar->pos.x,   bar->pos.y-1, 15, 2);
    LCD_printString(bar->label, bar->pos.x,   bar->pos.y+1, 15, 2);
    LCD_printString(bar->label, bar->pos.x,   bar->pos.y,   1,  2);

    // Bar position (after label; 2x font ~12px/char, max 6 chars = 72px + margin)
    int16_t bar_x = bar->pos.x + 80;
    int16_t bar_y = bar->pos.y;

    // Border (full bar outline)
    LCD_Draw_Rect(bar_x, bar_y, bar->width, bar->height, bar->color_border, 0);

    // Background fill
    LCD_Draw_Rect(bar_x + 1, bar_y + 1, bar->width - 2, bar->height - 2, bar->color_bg, 1);

    // Filled portion
    int16_t fill_w = (int16_t)((int32_t)bar->value * (bar->width - 2) / 100);
    if (fill_w > 0) {
        LCD_Draw_Rect(bar_x + 1, bar_y + 1, fill_w, bar->height - 2, bar->color_fill, 1);
    }
}
