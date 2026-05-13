#ifndef CAT_H
#define CAT_H

#include <stdint.h>
#include "Utils.h"

typedef enum {
    CAT_POSE_HEAD  = 0,
    CAT_POSE_BELLY = 1
} CatPose;

typedef enum {
    CAT_EXPR_NEUTRAL = 0,
    CAT_EXPR_HAPPY,
    CAT_EXPR_ANNOYED,
    CAT_EXPR_BITING,
    CAT_EXPR_GRABBING,
    CAT_EXPR_PURRING
} CatExpression;

typedef enum {
    ZONE_NONE = 0,
    ZONE_LEFT_EAR,
    ZONE_RIGHT_EAR,
    ZONE_CHIN,
    ZONE_BELLY_CENTER,
    ZONE_BELLY_SIDE,
    ZONE_BELLY_LEG
} CatZone;

typedef struct {
    CatZone zone_id;
    AABB    bounds;
} CatZoneDef_t;

typedef struct {
    CatPose       pose;
    CatExpression expression;
    uint8_t       expression_timer;
    uint8_t       blink_phase;       // 0=open, 1=closed
    uint16_t      blink_timer;
    uint8_t       anim_frame;
    uint8_t       anim_max;
    uint8_t       anim_active;
    CatZoneDef_t  zones[6];
    uint8_t       zone_count;
    const uint8_t* current_sprite;   // pointer to current sprite data
} Cat_t;

void Cat_Init(Cat_t* cat);
void Cat_SetPose(Cat_t* cat, CatPose pose);
void Cat_Update(Cat_t* cat);
void Cat_Draw(Cat_t* cat);
void Cat_SetSprite(Cat_t* cat, const uint8_t* sprite);

CatZoneDef_t Cat_GetZoneDef(Cat_t* cat, uint8_t index);
CatZone Cat_HitTest(Cat_t* cat, AABB* cursor_box);
void Cat_TriggerBite(Cat_t* cat);
void Cat_TriggerGrab(Cat_t* cat);

#endif
