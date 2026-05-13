#include "Cat.h"
#include "LCD.h"

// Zone bounds for both poses
static void _setup_pose1_zones(Cat_t* cat)
{
    cat->zone_count = 3;
    // Left ear: left half of 猫头 polygon (103,20)-(212,93), split at x=158
    cat->zones[0].zone_id = ZONE_LEFT_EAR;
    cat->zones[0].bounds.x = 103;
    cat->zones[0].bounds.y = 20;
    cat->zones[0].bounds.width  = 55;
    cat->zones[0].bounds.height = 73;

    // Right ear: right half of 猫头 polygon
    cat->zones[1].zone_id = ZONE_RIGHT_EAR;
    cat->zones[1].bounds.x = 158;
    cat->zones[1].bounds.y = 20;
    cat->zones[1].bounds.width  = 54;
    cat->zones[1].bounds.height = 73;

    // Chin: 猫下巴 polygon (96,103)-(172,163)
    cat->zones[2].zone_id = ZONE_CHIN;
    cat->zones[2].bounds.x = 96;
    cat->zones[2].bounds.y = 103;
    cat->zones[2].bounds.width  = 76;
    cat->zones[2].bounds.height = 60;
}

static void _setup_pose2_zones(Cat_t* cat)
{
    cat->zone_count = 4;
    // Belly center: 猫肚子 polygon (90,78)-(138,184)
    cat->zones[0].zone_id = ZONE_BELLY_CENTER;
    cat->zones[0].bounds.x = 90;
    cat->zones[0].bounds.y = 78;
    cat->zones[0].bounds.width  = 48;
    cat->zones[0].bounds.height = 106;

    // Belly side left: 边缘左 polygon (59,77)-(101,172)
    cat->zones[1].zone_id = ZONE_BELLY_SIDE;
    cat->zones[1].bounds.x = 59;
    cat->zones[1].bounds.y = 77;
    cat->zones[1].bounds.width  = 42;
    cat->zones[1].bounds.height = 95;

    // Belly side right: 边缘右 polygon (139,87)-(168,181)
    cat->zones[2].zone_id = ZONE_BELLY_SIDE;
    cat->zones[2].bounds.x = 139;
    cat->zones[2].bounds.y = 87;
    cat->zones[2].bounds.width  = 29;
    cat->zones[2].bounds.height = 94;

    // Belly leg: 后腿 polygon (25,174)-(162,235)
    cat->zones[3].zone_id = ZONE_BELLY_LEG;
    cat->zones[3].bounds.x = 25;
    cat->zones[3].bounds.y = 174;
    cat->zones[3].bounds.width  = 137;
    cat->zones[3].bounds.height = 61;
}

void Cat_Init(Cat_t* cat)
{
    cat->pose = CAT_POSE_HEAD;
    cat->expression = CAT_EXPR_NEUTRAL;
    cat->expression_timer = 0;
    cat->blink_phase = 0;
    cat->blink_timer = 120;
    cat->anim_frame = 0;
    cat->anim_max = 0;
    cat->anim_active = 0;
    cat->current_sprite = 0;
    _setup_pose1_zones(cat);
}

void Cat_SetPose(Cat_t* cat, CatPose pose)
{
    cat->pose = pose;
    cat->expression = CAT_EXPR_NEUTRAL;
    cat->expression_timer = 0;
    cat->anim_frame = 0;
    cat->anim_active = 0;

    if (pose == CAT_POSE_HEAD) {
        _setup_pose1_zones(cat);
    } else {
        _setup_pose2_zones(cat);
    }
}

void Cat_SetSprite(Cat_t* cat, const uint8_t* sprite)
{
    cat->current_sprite = sprite;
}

void Cat_Update(Cat_t* cat)
{
    if (cat->blink_timer > 0) {
        cat->blink_timer--;
    }
    if (cat->blink_phase == 1 && cat->blink_timer == 0) {
        cat->blink_phase = 0;
        cat->blink_timer = 90 + (Random_U16(90));
    }

    if (cat->expression_timer > 0) {
        cat->expression_timer--;
        if (cat->expression_timer == 0) {
            cat->expression = CAT_EXPR_NEUTRAL;
        }
    }

    if (cat->anim_active) {
        cat->anim_frame++;
        if (cat->anim_frame >= cat->anim_max) {
            cat->anim_active = 0;
            cat->anim_frame = 0;
            cat->expression = CAT_EXPR_NEUTRAL;
        }
    }
}

void Cat_Draw(Cat_t* cat)
{
    if (cat->current_sprite) {
        LCD_Blit_Fullscreen(cat->current_sprite);
    }
}

CatZoneDef_t Cat_GetZoneDef(Cat_t* cat, uint8_t index)
{
    if (index < cat->zone_count) {
        return cat->zones[index];
    }
    CatZoneDef_t empty = { ZONE_NONE, {0, 0, 0, 0} };
    return empty;
}

CatZone Cat_HitTest(Cat_t* cat, AABB* cursor_box)
{
    for (uint8_t i = 0; i < cat->zone_count; i++) {
        if (AABB_Collides(&cat->zones[i].bounds, cursor_box)) {
            return cat->zones[i].zone_id;
        }
    }
    return ZONE_NONE;
}

void Cat_TriggerBite(Cat_t* cat)
{
    cat->anim_active = 1;
    cat->anim_frame  = 0;
    cat->anim_max    = 12;
    cat->expression  = CAT_EXPR_BITING;
}

void Cat_TriggerGrab(Cat_t* cat)
{
    cat->anim_active = 1;
    cat->anim_frame  = 0;
    cat->anim_max    = 16;
    cat->expression  = CAT_EXPR_GRABBING;
}
