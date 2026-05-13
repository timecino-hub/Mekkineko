#ifndef CATENGINE_H
#define CATENGINE_H

#include <stdint.h>
#include "Cat.h"
#include "Cursor.h"
#include "ProgressBar.h"
#include "Joystick.h"

typedef enum {
    STATE_TITLE,
    STATE_POSE1_ACTIVE,
    STATE_POSE1_BITE,
    STATE_POSE1_COMPLETE,
    STATE_POSE2_ACTIVE,
    STATE_POSE2_BITE,
    STATE_POSE2_GRAB,
    STATE_POSE2_COMPLETE,
    STATE_ENDING_GOOD,
    STATE_ENDING_BAD,
    STATE_ENDING_HIDDEN
} GameState;

typedef struct {
    const char* speaker;
    const char* text;
    const char* cg;   // NULL or sprite name to switch (e.g. "16" for cg_16)
} DialogueLine;

typedef struct {
    Cat_t          cat;
    Cursor_t       cursor;
    ProgressBar_t  bar_happiness;
    ProgressBar_t  bar_danger;
    ProgressBar_t  bar_tension2;
    GameState      state;

    uint8_t  pose1_done;
    uint8_t  pose2_done;
    uint8_t  total_bites;

    float    happiness;
    float    danger;
    float    tension2;

    uint16_t timer_frames;
    uint16_t state_frame;
    uint8_t  btn_released;

    // Dialogue system
    DialogueLine* dialogue_lines;
    uint8_t  dialogue_count;
    uint8_t  dialogue_idx;
    uint16_t dialogue_char;
    uint8_t  title_started;
    uint8_t  dialogue_phase;  // 0=wait 1.5s, 1=typewriter, 2=wait B1, 3=done, 4=choice
    uint16_t dialogue_end_frame;

    // Choice system
    uint8_t  dialogue_has_choice;
    uint8_t  dialogue_choice_sel;  // 0=A, 1=B (joystick nav)
    const char* choice_a;
    const char* choice_b;
    DialogueLine* dialogue_lines_a;
    uint8_t  dialogue_count_a;
    DialogueLine* dialogue_lines_b;
    uint8_t  dialogue_count_b;
} CatEngine_t;

void CatEngine_Init(CatEngine_t* engine);
void CatEngine_Update(CatEngine_t* engine, UserInput input, uint8_t btn_pet);
void CatEngine_Draw(CatEngine_t* engine);
GameState CatEngine_GetState(CatEngine_t* engine);
uint8_t CatEngine_IsFinished(CatEngine_t* engine);

#endif
