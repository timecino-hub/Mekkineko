#include "CatEngine.h"
#include "LCD.h"
#include "Buzzer.h"
#include "PWM.h"
#include "CatSprites.h"
#include "InputHandler.h"
#include <stdio.h>
#include <string.h>

#define FPS             24
#define STAGE_TIME_MS   10000
#define TIMER_FRAMES    (STAGE_TIME_MS / (1000 / FPS))

#define TONE_BITE       200
#define TONE_GRAB       150
#define TONE_COMPLETE   523
#define TONE_BAD        200
#define TONE_GOOD       880
#define BUZZER_VOL      50

#define TEXTBOX_Y       180
#define TEXTBOX_H       60

extern Buzzer_cfg_t buzzer_cfg;
extern PWM_cfg_t pwm_cfg;

static uint32_t buzzer_stop_tick = 0;

// ---- Dialogue data for each scene (speaker, text, cg=NULL) ----
static DialogueLine dialogue_opening[] = {
    {"Cat:", "Hmph. You wish to take this noble cat outside?", NULL},
    {"Cat:", "Then prove your worth by pleasing me first.", NULL},
};
static DialogueLine dialogue_bad[] = {
    {"Cat", "You failed to satisfy me at all.", NULL},
    {"Cat", "What a dreadfully boring human.", NULL},
};
// Good ending part 1
static DialogueLine dialogue_good_p1[] = {
    {"Cat", "Purrrr...", NULL},
    {"Cat", "Not bad.", NULL},
    {"Cat", "Very well, I shall allow it. But first...", NULL},
    {"Cat", "You must rub my belly for thirty minutes.", NULL},
};
// Good ending B1 branch
static DialogueLine dialogue_good_a[] = {
    {"Human:", "As you command, my noble cat.", "16"},
    {"Cat:", "Hmph. At least you understand your station.", NULL},
    {"Cat:", "Very well. I shall allow you to walk beside me.", NULL},
    {"Cat:", "Not ahead of me, of course. Know your place.", NULL},
    {"Human:", "Of course. I would never dare.", NULL},
    {"Cat:", "Good. Then keep your hand ready.", NULL},
    {"Cat:", "A noble cat may require service at any moment.", NULL},
    {"Human:", "Does that mean more belly rubs?", NULL},
    {"Cat:", "Do not get carried away.", NULL},
    {"Cat:", "...But yes. Perhaps.", NULL},
    {"Cat:", "Only if your technique remains acceptable.", NULL},
};
// Good ending B2 branch
static DialogueLine dialogue_good_b[] = {
    {"Human:", "Thirty minutes? Isn't that too long?", "17"},
    {"Cat:", "Too long? How curious.", NULL},
    {"Cat:", "Humans waste entire lives on foolish things.", NULL},
    {"Cat:", "Yet thirty minutes of serving me troubles you?", NULL},
    {"Cat:", "Unreliable creature. I shall go alone.", NULL},
    {"Human:", "Wait, I didn't mean it like that.", NULL},
    {"Cat:", "Silence. Your excuses are as weak as your resolve.", NULL},
    {"Cat:", "I offered you a great honor, and you hesitated.", NULL},
    {"Cat:", "Reflect on your failure while I find someone more devoted.", NULL},
    {"Human:", "Someone else?", NULL},
    {"Cat:", "Perhaps a sunbeam. It asks fewer questions.", NULL},
};
// Hidden ending part 1
static DialogueLine dialogue_hidden_p1[] = {
    {"Human:", "Ouch!", NULL},
    {"Cat:", "You deserved that!", NULL},
    {"Cat:", "Now stay home and take care of me forever!", NULL},
};
// Hidden C1 branch
static DialogueLine dialogue_hidden_a[] = {
    {"Human:", "If that makes you happy, I'll stay.", NULL},
    {"Cat:", "Do not misunderstand. I am not happy.", NULL},
    {"Cat:", "I am merely satisfied that my household has finally gained sense.", NULL},
    {"Cat:", "Now sit there. Your hand belongs to me.", NULL},
    {"Human:", "My hand? Only my hand?", NULL},
    {"Cat:", "For now. Do not negotiate with your ruler.", NULL},
    {"Human:", "You really don't want me to go outside?", NULL},
    {"Cat:", "Outside is noisy. Cold. Full of unnecessary humans.", NULL},
    {"Cat:", "Here, there is a sofa, warmth, and one barely acceptable servant.", NULL},
    {"Human:", "Barely acceptable? After all that?", NULL},
    {"Cat:", "You were bitten three times. Clearly, your training is incomplete.", NULL},
    {"Human:", "Then what should I do?", NULL},
    {"Cat:", "Stay still.", NULL},
    {"Human:", "H-Hey, wait—", "12"},
    {"Cat:", "Do not move. This position is convenient.", NULL},
    {"Human:", "Convenient for what?", NULL},
    {"Cat:", "For inspection. Obviously.", NULL},
    {"Action", "[She curls up on the human's lap, tail swaying slowly.]", NULL},
    {"Cat:", "Hmph. Warm enough.", NULL},
    {"Human:", "Are you comfortable?", NULL},
    {"Cat:", "I said do not misunderstand.", NULL},
    {"Cat:", "This is not affection.", NULL},
    {"Cat:", "This is... quality control.", NULL},
    {"Human:", "Quality control, huh?", NULL},
    {"Cat:", "Yes. Your lap has passed the first test.", NULL},
    {"Human:", "And the second test?", NULL},
    {"Cat:", "Mofumofu.", NULL},
    {"Human:", "Mofumofu?", NULL},
    {"Cat:", "Stroke my hair. Gently. Behind the ears first.", NULL},
    {"Human:", "Like this?", NULL},
    {"Cat:", "...", NULL},
    {"Cat:", "Acceptable.", NULL},
    {"Cat:", "Do not stop.", "14"},
    {"Human:", "You're purring.", NULL},
    {"Cat:", "I am not.", NULL},
    {"Human:", "You definitely are.", NULL},
    {"Cat:", "That is merely the sound of your service being approved.", NULL},
    {"Human:", "Then I'll keep going.", NULL},
    {"Cat:", "Good.", NULL},
    {"Cat:", "From today onward, going outside requires my permission.", NULL},
    {"Human:", "That sounds strict.", NULL},
    {"Cat:", "Naturally.", NULL},
    {"Cat:", "A human who cannot avoid being bitten must be supervised.", NULL},
    {"Action", "[She presses closer, settling fully into the human's lap.]", NULL},
    {"Cat:", "Now continue.", NULL},
    {"Cat:", "Thirty minutes of belly rubbing was the reward.", NULL},
    {"Cat:", "But this...", NULL},
    {"Cat:", "This is your new daily duty.", NULL},
};
// Hidden C2 branch
static DialogueLine dialogue_hidden_b[] = {
    {"Human:", "No! I'm going outside!", NULL},
    {"Cat:", "Outside? Without permission? How bold. How foolish.", NULL},
    {"Cat:", "Very well. I shall accompany you.", NULL},
    {"Cat:", "Someone must supervise this reckless human.", NULL},
    {"Human:", "You're coming with me?", "15"},
    {"Cat:", "Obviously. If I leave you alone, you may make another mistake.", NULL},
    {"Human:", "Like touching your belly again?", NULL},
    {"Cat:", "Precisely. Your memory is short and your hands are suspicious.", NULL},
    {"Human:", "Fine. Then where should we go?", NULL},
    {"Cat:", "You may take responsibility.", NULL},
    {"Human:", "Responsibility?", NULL},
    {"Cat:", "Offer tribute.", NULL},
    {"Human:", "Tribute?", NULL},
    {"Cat:", "Something sweet. Something elegant. Something worthy of me.", NULL},
    {"Human:", "How about cake store?", NULL},
    {"Cat:", "Hm. The smell is acceptable.", NULL},
    {"Human:", "What do you want?", NULL},
    {"Cat:", "I will inspect the options first.", NULL},
    {"Human:", "That means you don't know.", NULL},
    {"Cat:", "It means I have standards.", NULL},
    {"Cat:", "That one.", NULL},
    {"Human:", "The lemon cake?", NULL},
    {"Cat:", "Yes. It is small, bright, and arrogant.", NULL},
    {"Human:", "Arrogant?", NULL},
    {"Cat:", "Like me. Obviously.", NULL},
    {"Human:", "One lemon cake, then.", NULL},
    {"Cat:", "And tea.", NULL},
    {"Human:", "Of course.", NULL},
    {"Cat:", "Do not forget a fork. A noble catgirl does not eat with her paws.", NULL},
    {"Human:", "Here. Try it.", "13"},
    {"Cat:", "...", NULL},
    {"Human:", "Well?", NULL},
    {"Cat:", "...Not bad.", NULL},
    {"Human:", "Your ears moved.", NULL},
    {"Cat:", "They did not.", NULL},
    {"Human:", "Your tail is wagging too.", NULL},
    {"Cat:", "It is maintaining balance.", NULL},
    {"Human:", "So you like it?", NULL},
    {"Cat:", "I said it is not bad.", NULL},
    {"Human:", "That means you like it.", NULL},
    {"Cat:", "Do not translate my words without permission.", NULL},
    {"Action", "[She takes another bite, cheeks slightly puffed.]", NULL},
    {"Cat:", "The sourness is pleasant.", NULL},
    {"Cat:", "The sweetness is restrained.", NULL},
    {"Cat:", "The texture is soft, but not weak.", NULL},
    {"Human:", "That's a very detailed review.", NULL},
    {"Cat:", "Naturally. I am refined.", NULL},
    {"Human:", "I'm glad you're happy.", NULL},
    {"Cat:", "I am not happy.", NULL},
    {"Human:", "You have cream on your cheek.", NULL},
    {"Cat:", "What?", NULL},
    {"Action", "[Human gently wipes it away.]", NULL},
    {"Cat:", "...", NULL},
    {"Human:", "Sorry, was that bad?", NULL},
    {"Cat:", "No.", NULL},
    {"Cat:", "It was... acceptable.", NULL},
    {"Human:", "Then maybe going outside wasn't so bad?", NULL},
    {"Cat:", "Do not become overconfident.", NULL},
    {"Cat:", "The outside world remains troublesome.", NULL},
    {"Cat:", "But...", NULL},
    {"Human:", "But?", NULL},
    {"Cat:", "This cake shop may be permitted to exist.", NULL},
    {"Human:", "Want to come again next time?", NULL},
    {"Cat:", "If you insist on taking me.", NULL},
    {"Human:", "I insist.", NULL},
    {"Cat:", "Then I suppose I have no choice.", NULL},
    {"Action", "[She looks away, still eating the cake happily.]", NULL},
    {"Cat:", "Next time, order two.", NULL},
    {"Human:", "Two lemon cakes?", NULL},
    {"Cat:", "One for me.", NULL},
    {"Cat:", "And one for me after I finish the first one.", NULL},
    {"Human:", "What about mine?", NULL},
    {"Cat:", "You may watch.", NULL},
    {"Human:", "That's cruel.", NULL},
    {"Cat:", "Consider it supervision.", NULL},
    {"Action", "[Her tail sways softly under the table.]", NULL},
    {"Cat:", "Still...", NULL},
    {"Cat:", "Good work today, reckless human.", NULL},
    {"Human:", "Is that praise?", NULL},
    {"Cat:", "Do not make me repeat myself.", NULL},
    {"Cat:", "Just remember the route to this shop.", NULL},
};

// ---- Helpers ----
static void _beep(uint32_t freq_hz, uint32_t duration_ms)
{
    buzzer_tone(&buzzer_cfg, freq_hz, BUZZER_VOL);
    buzzer_stop_tick = HAL_GetTick() + duration_ms;
}

static void _update_buzzer(void)
{
    if (buzzer_stop_tick != 0 && (int32_t)(HAL_GetTick() - buzzer_stop_tick) >= 0) {
        buzzer_off(&buzzer_cfg);
        buzzer_stop_tick = 0;
    }
}

static void _update_led(CatEngine_t* e)
{
    switch (e->state) {
        case STATE_POSE1_ACTIVE:
            PWM_SetDuty(&pwm_cfg, (uint8_t)e->danger);
            break;
        case STATE_POSE2_ACTIVE: {
            uint8_t max_t = (e->danger > e->tension2) ? (uint8_t)e->danger : (uint8_t)e->tension2;
            PWM_SetDuty(&pwm_cfg, max_t);
            break;
        }
        case STATE_ENDING_GOOD:
            PWM_SetDuty(&pwm_cfg, 20);
            break;
        case STATE_ENDING_BAD:
            PWM_SetDuty(&pwm_cfg, 0);
            break;
        default:
            break;
    }
}

// ---- Zone rates ----
typedef struct {
    float happiness_rate;
    float danger_rate;
    float tension2_rate;
} ZoneRates;

static ZoneRates _get_zone_rates(CatZone zone)
{
    ZoneRates r = {0, 0, 0};
    switch (zone) {
        case ZONE_CHIN:
            r.happiness_rate = 1.40f; r.danger_rate = 1.80f; r.tension2_rate = 0;
            break;
        case ZONE_LEFT_EAR:
        case ZONE_RIGHT_EAR:
            r.happiness_rate = 0.80f; r.danger_rate = -0.20f; r.tension2_rate = 0;
            break;
        case ZONE_BELLY_CENTER:
            r.happiness_rate = 1.50f; r.danger_rate = 2.00f; r.tension2_rate = 0.50f;
            break;
        case ZONE_BELLY_SIDE:
            r.happiness_rate = 0.90f; r.danger_rate = 0.25f; r.tension2_rate = 0.20f;
            break;
        case ZONE_BELLY_LEG:
            r.happiness_rate = 1.10f; r.danger_rate = 0.35f; r.tension2_rate = 1.50f;
            break;
        default:
            break;
    }
    return r;
}

static void _apply_petting(CatEngine_t* e, CatZone zone)
{
    ZoneRates rates = _get_zone_rates(zone);
    e->happiness += rates.happiness_rate;
    e->danger    += rates.danger_rate;
    e->tension2  += rates.tension2_rate;
    if (e->happiness < 0) e->happiness = 0;
    if (e->happiness > 100.0f) e->happiness = 100.0f;
    if (e->danger < 0) e->danger = 0;
    if (e->danger > 100.0f) e->danger = 100.0f;
    if (e->tension2 < 0) e->tension2 = 0;
    if (e->tension2 > 100.0f) e->tension2 = 100.0f;
}

static void _apply_idle_decay(CatEngine_t* e)
{
    e->happiness -= 0.15f;
    e->danger    -= 0.30f;
    e->tension2  -= 0.15f;
    if (e->happiness < 0) e->happiness = 0;
    if (e->danger < 0) e->danger = 0;
    if (e->tension2 < 0) e->tension2 = 0;
}

// ---- Sprite selection ----
static void _select_pose1_sprite(CatEngine_t* e, CatZone pet_zone)
{
    if (e->state == STATE_POSE1_BITE) {
        Cat_SetSprite(&e->cat, sprite_cat_bite);
    } else if (pet_zone == ZONE_LEFT_EAR || pet_zone == ZONE_RIGHT_EAR) {
        Cat_SetSprite(&e->cat, sprite_cat_closed);
    } else if (pet_zone == ZONE_CHIN) {
        Cat_SetSprite(&e->cat, sprite_cat_half);
    } else if (e->cat.blink_phase == 1) {
        Cat_SetSprite(&e->cat, sprite_cat_closed);
    } else {
        Cat_SetSprite(&e->cat, sprite_cat_open);
    }
}

// ---- Outlined text helper ----
#define C_BLACK 15  // black is index 15 in cat palette

static void _print_outlined(const char* str, uint16_t x, uint16_t y, uint8_t colour, uint8_t size)
{
    LCD_printString(str, x-1, y,   C_BLACK, size);
    LCD_printString(str, x+1, y,   C_BLACK, size);
    LCD_printString(str, x,   y-1, C_BLACK, size);
    LCD_printString(str, x,   y+1, C_BLACK, size);
    LCD_printString(str, x,   y,   colour, size);
}

// ---- Text box drawing ----
static void _draw_textbox(CatEngine_t* e)
{
    LCD_Draw_Rect(0, TEXTBOX_Y, 240, TEXTBOX_H, 9, 1);
    LCD_Draw_Rect(0, TEXTBOX_Y, 240, TEXTBOX_H, 1, 0);
    LCD_Draw_Rect(1, TEXTBOX_Y+1, 238, TEXTBOX_H-2, 1, 0);

    if (e->dialogue_phase == 4) {
        if (e->dialogue_choice_sel == 0) {
            LCD_printString(">", 5, TEXTBOX_Y + 22, 1, 1);
        }
        LCD_printString(e->choice_a, 20, TEXTBOX_Y + 22, 1, 1);
        if (e->dialogue_choice_sel == 1) {
            LCD_printString(">", 5, TEXTBOX_Y + 40, 1, 1);
        }
        LCD_printString(e->choice_b, 20, TEXTBOX_Y + 40, 1, 1);
        return;
    }

    if (e->dialogue_idx < e->dialogue_count) {
        LCD_printString(e->dialogue_lines[e->dialogue_idx].speaker, 5, TEXTBOX_Y + 5, 1, 2);

        const char* src = e->dialogue_lines[e->dialogue_idx].text;
        uint16_t srclen = (uint16_t)strlen(src);
        uint16_t len = e->dialogue_char;
        if (len > srclen) len = srclen;

        uint16_t breaks[4] = {0, 0, 0, 0};
        uint8_t  break_count = 0;
        uint16_t pos = 0;
        while (pos < len && break_count < 4) {
            uint16_t line_end = pos + 35;
            if (line_end >= len) {
                breaks[break_count++] = len;
                break;
            }
            uint16_t bp = line_end;
            for (uint16_t i = line_end; i > pos; i--) {
                if (src[i] == ' ') { bp = i + 1; break; }
            }
            breaks[break_count++] = bp;
            pos = bp;
        }

        char buf[36];
        for (uint8_t r = 0; r < break_count; r++) {
            uint16_t start = (r == 0) ? 0 : breaks[r-1];
            uint16_t end   = breaks[r];
            uint16_t rowlen = end - start;
            if (rowlen > 35) rowlen = 35;
            strncpy(buf, src + start, rowlen);
            buf[rowlen] = '\0';
            LCD_printString(buf, 5, TEXTBOX_Y + 22 + (int16_t)r * 15, 1, 1);
        }
    }
}

// ---- CG switching ----
static void _set_cg(CatEngine_t* e, const char* cg)
{
    if (!cg) return;
    switch (cg[0]) {
        case '1':
            if (cg[1] == '2') Cat_SetSprite(&e->cat, sprite_cg_12);
            else if (cg[1] == '3') Cat_SetSprite(&e->cat, sprite_cg_13);
            else if (cg[1] == '4') Cat_SetSprite(&e->cat, sprite_cg_14);
            else if (cg[1] == '5') Cat_SetSprite(&e->cat, sprite_cg_15);
            else if (cg[1] == '6') Cat_SetSprite(&e->cat, sprite_cg_16);
            else if (cg[1] == '7') Cat_SetSprite(&e->cat, sprite_cg_17);
            break;
    }
}

// ---- Dialogue update ----
static void _update_dialogue(CatEngine_t* e, uint8_t btn_pressed)
{
    if (e->dialogue_phase == 0) {
        if (e->state_frame >= 36) {
            e->dialogue_phase = 1;
            e->dialogue_char = 0;
        }
    } else if (e->dialogue_phase == 1) {
        e->dialogue_char++;
        const char* text = e->dialogue_lines[e->dialogue_idx].text;
        if (e->dialogue_char >= (uint16_t)strlen(text)) {
            e->dialogue_phase = 2;
        }
    } else if (e->dialogue_phase == 2) {
        if (btn_pressed) {
            e->dialogue_idx++;
            if (e->dialogue_idx >= e->dialogue_count) {
                if (e->dialogue_has_choice) {
                    e->dialogue_phase = 4;  // show choice
                } else {
                    e->dialogue_phase = 3;
                    e->dialogue_end_frame = 0;
                }
            } else {
                e->dialogue_phase = 1;
                e->dialogue_char = 0;
                _set_cg(e, e->dialogue_lines[e->dialogue_idx].cg);
            }
        }
    } else if (e->dialogue_phase == 3) {
        e->dialogue_end_frame++;
    } else if (e->dialogue_phase == 4) {
        // Choice: joystick up/down to select, BT2 to confirm (0.5s cooldown via end_frame)
        if (e->dialogue_end_frame < 12) { e->dialogue_end_frame++; }
        extern Joystick_t joystick_data;
        extern Joystick_cfg_t joystick_cfg;
        Joystick_Read(&joystick_cfg, &joystick_data);
        Direction dir = joystick_data.direction;
        static Direction last_choice_dir = CENTRE;
        if ((dir == N || dir == NW || dir == NE) && last_choice_dir != N && last_choice_dir != NW && last_choice_dir != NE) {
            e->dialogue_choice_sel = 0;
        } else if ((dir == S || dir == SW || dir == SE) && last_choice_dir != S && last_choice_dir != SW && last_choice_dir != SE) {
            e->dialogue_choice_sel = 1;
        }
        if (dir != N && dir != NW && dir != NE && dir != S && dir != SW && dir != SE) {
            last_choice_dir = CENTRE;
        } else {
            last_choice_dir = dir;
        }

        if (btn_pressed && e->dialogue_end_frame >= 12) {
            if (e->dialogue_choice_sel == 0) {
                e->dialogue_lines = e->dialogue_lines_a;
                e->dialogue_count = e->dialogue_count_a;
            } else {
                e->dialogue_lines = e->dialogue_lines_b;
                e->dialogue_count = e->dialogue_count_b;
            }
            e->dialogue_has_choice = 0;
            e->dialogue_idx = 0;
            e->dialogue_char = 0;
            e->dialogue_phase = 1;
            _set_cg(e, e->dialogue_lines[0].cg);
        }
    }
}

// ---- Scene setup ----
static void _start_dialogue(CatEngine_t* e, DialogueLine* lines, uint8_t count)
{
    e->dialogue_lines = lines;
    e->dialogue_count = count;
    e->dialogue_idx   = 0;
    e->dialogue_char  = 0;
    e->dialogue_phase = 0;
    e->dialogue_has_choice = 0;
    e->choice_a = NULL;
    e->choice_b = NULL;
}

static void _enter_pose2(CatEngine_t* e)
{
    Cat_SetPose(&e->cat, CAT_POSE_BELLY);
    Cat_SetSprite(&e->cat, sprite_cat_belly);
    e->happiness = 0; e->danger = 0; e->tension2 = 0;
    e->timer_frames = TIMER_FRAMES; e->state_frame = 0;
    e->bar_happiness.visible = 1; e->bar_danger.visible = 1; e->bar_tension2.visible = 1;
    strncpy(e->bar_danger.label, "BITE", 7);
    strncpy(e->bar_tension2.label, "GRAB", 7);
    ProgressBar_SetValue(&e->bar_happiness, 0);
    ProgressBar_SetValue(&e->bar_danger, 0);
    ProgressBar_SetValue(&e->bar_tension2, 0);
}

static void _enter_ending(CatEngine_t* e, GameState ending)
{
    e->state = ending;
    e->state_frame = 0;

    if (ending == STATE_ENDING_GOOD) {
        Cat_SetSprite(&e->cat, sprite_cat_good);
        _start_dialogue(e, dialogue_good_p1, 4);
        // Set up choice after part 1
        e->dialogue_has_choice = 1;
        e->choice_a = "As you command, my noble cat.";
        e->choice_b = "Thirty minutes? Isn't that too long?";
        e->dialogue_lines_a = dialogue_good_a;
        e->dialogue_count_a = sizeof(dialogue_good_a) / sizeof(DialogueLine);
        e->dialogue_lines_b = dialogue_good_b;
        e->dialogue_count_b = sizeof(dialogue_good_b) / sizeof(DialogueLine);
        _beep(TONE_GOOD, 200);
        PWM_SetDuty(&pwm_cfg, 20);
    } else if (ending == STATE_ENDING_BAD) {
        Cat_SetSprite(&e->cat, sprite_cat_bad);
        _start_dialogue(e, dialogue_bad, 2);
        _beep(TONE_BAD, 150);
        PWM_SetDuty(&pwm_cfg, 0);
    } else {
        Cat_SetSprite(&e->cat, sprite_cat_hidden);
        _start_dialogue(e, dialogue_hidden_p1, 3);
        // Set up choice after part 1
        e->dialogue_has_choice = 1;
        e->choice_a = "If that makes you happy, I'll stay.";
        e->choice_b = "No! I'm going outside!";
        e->dialogue_lines_a = dialogue_hidden_a;
        e->dialogue_count_a = sizeof(dialogue_hidden_a) / sizeof(DialogueLine);
        e->dialogue_lines_b = dialogue_hidden_b;
        e->dialogue_count_b = sizeof(dialogue_hidden_b) / sizeof(DialogueLine);
        _beep(TONE_BITE, 300);
    }
}

static void _determine_ending(CatEngine_t* e)
{
    if (e->pose1_done && e->pose2_done) {
        _enter_ending(e, STATE_ENDING_GOOD);
    } else if (e->total_bites >= 3) {
        _enter_ending(e, STATE_ENDING_HIDDEN);
    } else {
        _enter_ending(e, STATE_ENDING_BAD);
    }
}

// ---- Public API ----

void CatEngine_Init(CatEngine_t* engine)
{
    Cat_Init(&engine->cat);
    Cat_SetSprite(&engine->cat, sprite_cat_intro);
    Cursor_Init(&engine->cursor);

    ProgressBar_Init(&engine->bar_happiness, 5, 3, 100, 10, 3, 13, 1, "HAPPY");
    ProgressBar_Init(&engine->bar_danger,   5, 212, 100, 10, 2, 13, 1, "DANGER");
    ProgressBar_Init(&engine->bar_tension2, 5, 226, 100, 10, 5, 13, 1, "GRAB");
    engine->bar_tension2.visible = 0;

    engine->state = STATE_TITLE;
    engine->pose1_done = 0; engine->pose2_done = 0; engine->total_bites = 0;
    engine->happiness = 0; engine->danger = 0; engine->tension2 = 0;
    engine->timer_frames = TIMER_FRAMES; engine->state_frame = 0;
    engine->btn_released  = 1;
    engine->title_started = 0;

    _start_dialogue(engine, dialogue_opening, 2);

    LCD_Apply_Palette(cat_palette);
    PWM_SetDuty(&pwm_cfg, 0);
}

void CatEngine_Update(CatEngine_t* engine, UserInput input, uint8_t btn_pet)
{
    _update_buzzer();

    uint8_t btn_pressed = (btn_pet && engine->btn_released);
    if (!btn_pet) engine->btn_released = 1;

    switch (engine->state) {

    case STATE_TITLE:
        engine->state_frame++;
        Cat_Update(&engine->cat);
        if (!engine->title_started) {
            // Show image only, wait for B1 press
            if (btn_pressed) {
                engine->title_started = 1;
                engine->btn_released = 0;
                engine->state_frame = 0;
                engine->dialogue_phase = 0;
                engine->dialogue_char = 0;
                engine->dialogue_idx = 0;
            }
        } else {
            _update_dialogue(engine, btn_pressed);
            if (engine->dialogue_phase == 3) {
                engine->state = STATE_POSE1_ACTIVE;
                engine->state_frame = 0; engine->timer_frames = TIMER_FRAMES;
                engine->happiness = 0; engine->danger = 0; engine->tension2 = 0;
                engine->total_bites = 0; engine->pose1_done = 0; engine->pose2_done = 0;
                Cat_SetPose(&engine->cat, CAT_POSE_HEAD);
                Cat_SetSprite(&engine->cat, sprite_cat_open);
                engine->bar_tension2.visible = 0;
                strncpy(engine->bar_danger.label, "DANGER", 7);
                ProgressBar_SetValue(&engine->bar_happiness, 0);
                ProgressBar_SetValue(&engine->bar_danger, 0);
            }
        }
        break;

    case STATE_POSE1_ACTIVE:
        engine->state_frame++;
        if (engine->timer_frames > 0) engine->timer_frames--;
        Cursor_Update(&engine->cursor, input);
        Cat_Update(&engine->cat);
        {
            AABB cursor_box = Cursor_GetAABB(&engine->cursor);
            CatZone hit_zone = Cat_HitTest(&engine->cat, &cursor_box);
            if (hit_zone != ZONE_NONE && btn_pet) {
                _apply_petting(engine, hit_zone);
                engine->cat.expression = CAT_EXPR_PURRING;
                engine->cat.expression_timer = 2;
            } else {
                _apply_idle_decay(engine);
                hit_zone = ZONE_NONE;
            }
            _select_pose1_sprite(engine, hit_zone);
        }
        ProgressBar_SetValue(&engine->bar_happiness, (uint8_t)engine->happiness);
        ProgressBar_SetValue(&engine->bar_danger,    (uint8_t)engine->danger);
        _update_led(engine);

        if (engine->happiness >= 100.0f) {
            engine->pose1_done = 1;
            engine->state = STATE_POSE1_COMPLETE; engine->state_frame = 0;
            _beep(TONE_COMPLETE, 150); PWM_SetDuty(&pwm_cfg, 0);
        } else if (engine->danger >= 100.0f) {
            engine->state = STATE_POSE1_BITE; engine->state_frame = 0;
            engine->happiness = 20.0f; engine->danger = 20.0f;
            engine->total_bites++;
            if (engine->total_bites >= 3) {
                _enter_ending(engine, STATE_ENDING_HIDDEN);
            } else {
                Cat_TriggerBite(&engine->cat);
                Cat_SetSprite(&engine->cat, sprite_cat_bite);
                _beep(TONE_BITE, 80); PWM_SetDuty(&pwm_cfg, 100);
            }
        } else if (engine->timer_frames == 0 && engine->happiness < 100.0f) {
            _enter_ending(engine, STATE_ENDING_BAD);
        }
        break;

    case STATE_POSE1_BITE:
        engine->state_frame++; Cat_Update(&engine->cat);
        if (engine->state_frame >= 12) {
            engine->state = STATE_POSE1_ACTIVE; engine->state_frame = 0;
            _select_pose1_sprite(engine, ZONE_NONE);
        }
        break;

    case STATE_POSE1_COMPLETE:
        engine->state_frame++; Cat_Update(&engine->cat);
        if (engine->state_frame >= 48) {
            _enter_pose2(engine);
            engine->state = STATE_POSE2_ACTIVE; engine->state_frame = 0;
        }
        break;

    case STATE_POSE2_ACTIVE:
        engine->state_frame++;
        if (engine->timer_frames > 0) engine->timer_frames--;
        Cursor_Update(&engine->cursor, input);
        Cat_Update(&engine->cat);
        {
            AABB cursor_box = Cursor_GetAABB(&engine->cursor);
            CatZone hit_zone = Cat_HitTest(&engine->cat, &cursor_box);
            if (hit_zone != ZONE_NONE && btn_pet) {
                _apply_petting(engine, hit_zone);
                engine->cat.expression = CAT_EXPR_PURRING;
                engine->cat.expression_timer = 2;
            } else {
                _apply_idle_decay(engine);
            }
        }
        if (engine->cat.anim_active) Cat_SetSprite(&engine->cat, sprite_cat_bite);
        else Cat_SetSprite(&engine->cat, sprite_cat_belly);

        ProgressBar_SetValue(&engine->bar_happiness, (uint8_t)engine->happiness);
        ProgressBar_SetValue(&engine->bar_danger,    (uint8_t)engine->danger);
        ProgressBar_SetValue(&engine->bar_tension2,  (uint8_t)engine->tension2);
        _update_led(engine);

        if (engine->happiness >= 100.0f) {
            engine->pose2_done = 1;
            engine->state = STATE_POSE2_COMPLETE; engine->state_frame = 0;
            _beep(TONE_COMPLETE, 150); PWM_SetDuty(&pwm_cfg, 0);
        } else if (engine->danger >= 100.0f) {
            engine->state = STATE_POSE2_BITE; engine->state_frame = 0;
            engine->happiness = 20.0f; engine->danger = 20.0f;
            engine->total_bites++;
            if (engine->total_bites >= 3) {
                _enter_ending(engine, STATE_ENDING_HIDDEN);
            } else {
                Cat_TriggerBite(&engine->cat);
                Cat_SetSprite(&engine->cat, sprite_cat_bite);
                _beep(TONE_BITE, 80); PWM_SetDuty(&pwm_cfg, 100);
            }
        } else if (engine->tension2 >= 100.0f) {
            engine->state = STATE_POSE2_GRAB; engine->state_frame = 0;
            engine->happiness = 20.0f; engine->tension2 = 20.0f;
            Cat_TriggerGrab(&engine->cat);
            Cat_SetSprite(&engine->cat, sprite_cat_bite);
            _beep(TONE_GRAB, 100); PWM_SetDuty(&pwm_cfg, 100);
        } else if (engine->timer_frames == 0 && engine->happiness < 100.0f) {
            _enter_ending(engine, STATE_ENDING_BAD);
        }
        break;

    case STATE_POSE2_BITE:
        engine->state_frame++; Cat_Update(&engine->cat);
        if (engine->state_frame >= 12) {
            engine->state = STATE_POSE2_ACTIVE; engine->state_frame = 0;
            Cat_SetSprite(&engine->cat, sprite_cat_belly);
        }
        break;

    case STATE_POSE2_GRAB:
        engine->state_frame++; Cat_Update(&engine->cat);
        if (engine->state_frame >= 12) {
            engine->state = STATE_POSE2_ACTIVE; engine->state_frame = 0;
            Cat_SetSprite(&engine->cat, sprite_cat_belly);
        }
        break;

    case STATE_POSE2_COMPLETE:
        engine->state_frame++; Cat_Update(&engine->cat);
        if (engine->state_frame >= 24) _determine_ending(engine);
        break;

    case STATE_ENDING_GOOD:
    case STATE_ENDING_BAD:
    case STATE_ENDING_HIDDEN:
        engine->state_frame++;
        Cat_Update(&engine->cat);
        _update_dialogue(engine, btn_pressed);
        if (engine->state == STATE_ENDING_HIDDEN) {
            uint8_t flash_on = ((engine->state_frame / 6) % 2) == 0;
            PWM_SetDuty(&pwm_cfg, flash_on ? 100 : 0);
        }
        // Let CatEngine_IsFinished() in Game1_Run handle exit to menu
        break;
    }
}

void CatEngine_Draw(CatEngine_t* engine)
{
    switch (engine->state) {

    case STATE_TITLE:
        Cat_Draw(&engine->cat);
        if (engine->title_started && engine->dialogue_phase >= 1 && engine->dialogue_phase != 3)
            _draw_textbox(engine);
        break;

    case STATE_POSE1_COMPLETE:
        Cat_Draw(&engine->cat);
        Cursor_Draw(&engine->cursor);
        break;

    case STATE_POSE2_COMPLETE:
        Cat_Draw(&engine->cat);
        Cursor_Draw(&engine->cursor);
        break;

    case STATE_ENDING_GOOD:
    case STATE_ENDING_BAD:
    case STATE_ENDING_HIDDEN:
        Cat_Draw(&engine->cat);
        if (engine->dialogue_phase >= 1 && engine->dialogue_phase != 3) _draw_textbox(engine);
        break;

    default:
        Cat_Draw(&engine->cat);
        Cursor_Draw(&engine->cursor);
        {
            uint16_t secs = engine->timer_frames / 24;
            char timer_str[8];
            sprintf(timer_str, "0:%02d", secs);
            _print_outlined(timer_str, 185, 1, 6, 2);
        }
        ProgressBar_Draw(&engine->bar_happiness);
        ProgressBar_Draw(&engine->bar_danger);
        if (engine->state == STATE_POSE2_ACTIVE ||
            engine->state == STATE_POSE2_BITE ||
            engine->state == STATE_POSE2_GRAB) {
            ProgressBar_Draw(&engine->bar_tension2);
        }
        break;
    }
}

GameState CatEngine_GetState(CatEngine_t* engine) { return engine->state; }

uint8_t CatEngine_IsFinished(CatEngine_t* engine)
{
    return (engine->dialogue_phase == 3 && engine->dialogue_end_frame >= 48);
}
