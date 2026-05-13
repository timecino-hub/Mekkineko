#ifndef PROGRESSBAR_H
#define PROGRESSBAR_H

#include <stdint.h>
#include "Utils.h"

typedef struct {
    Position2D pos;
    int16_t    width, height;
    uint8_t    value;        // 0-100
    uint8_t    color_fill;
    uint8_t    color_bg;
    uint8_t    color_border;
    char       label[8];
    uint8_t    visible;
} ProgressBar_t;

void ProgressBar_Init(ProgressBar_t* bar, int16_t x, int16_t y,
                      int16_t w, int16_t h, uint8_t color_fill,
                      uint8_t color_bg, uint8_t color_border,
                      const char* label);
void ProgressBar_SetValue(ProgressBar_t* bar, uint8_t value);
void ProgressBar_Draw(ProgressBar_t* bar);

#endif
