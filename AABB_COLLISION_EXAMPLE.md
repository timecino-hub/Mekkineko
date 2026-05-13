# AABB Collision Detection in Pong

## What is AABB?

**AABB** stands for **Axis-Aligned Bounding Box**. It's the simplest collision detection method:
- Uses rectangles (boxes) aligned with X and Y axes
- No rotation - boxes are always horizontal and vertical
- Ideal for games like Pong where objects are axis-aligned
- **Super fast** - only needs 4 comparison operations

## How AABB Collision Works

Two rectangles collide if they **overlap** in BOTH the X-axis AND the Y-axis simultaneously.

### The Test (from Core/Inc/Utils.h)

```c
static inline uint8_t AABB_Collides(AABB* a, AABB* b) {
    return (a->x < b->x + b->width &&
            a->x + a->width > b->x &&
            a->y < b->y + b->height &&
            a->y + a->height > b->y);
}
```


### Visual Example in Pong Context

```
BEFORE COLLISION - Ball and Paddle separated:
    ┌────┐           ┌───┐
    │Ball│           │   │
    │    │           │Pad│
    └────┘           │dle│
                     └───┘

COLLISION DETECTED - Ball overlaps with Paddle:
    ┌────┐           ┌───┐
    │Ball│ overlaps →│   │
    │    │  with     │Pad│
    └────┘           │dle│
                     └───┘
      → Ball velocity X reversed
      → Buzzer beeps
      → Score incremented

NO HORIZONTAL OVERLAP - Too far apart:
    ┌────┐                              ┌───┐
    │Ball│                              │   │
    │    │                              │Pad│
    └────┘                              │dle│
                                        └───┘
      → No collision detected
```

## How AABB Collision Works in Pong

### 1. AABB Structure (from Core/Inc/Utils.h)

```c
typedef struct {
    int16_t x;        // Top-left X coordinate
    int16_t y;        // Top-left Y coordinate
    int16_t width;    // Width in pixels
    int16_t height;   // Height in pixels
} AABB;
```

Each game object gets an AABB via helper functions:
- `Ball_GetAABB(&ball)` - returns 6×6 bounding box around ball
- `Paddle_GetAABB(&paddle)` - returns paddle's width × height box

### 2. Ball-Paddle Collision (The ONLY place we use AABB_Collides)

In **PongEngine.c**, the `PongEngine_CheckPaddleCollision()` function:

```c
static void PongEngine_CheckPaddleCollision(PongEngine_t* engine) {
    Ball_t* ball = &engine->ball;
    Paddle_t* paddle = &engine->paddle;
    Vector2D vel = Ball_GetVelocity(ball);
    
    // Get bounding boxes for collision detection
    AABB ball_box = Ball_GetAABB(ball);
    AABB paddle_box = Paddle_GetAABB(paddle);
    
    // Check if ball and paddle AABBs collide
    if (AABB_Collides(&ball_box, &paddle_box)) {
        // Collision detected! Reverse ball X velocity
        Ball_SetVelocity(ball, -vel.x, vel.y);
        
        // Move ball to prevent getting stuck in paddle
        ball->x = paddle_box.x + paddle_box.width;
        
        // Increment paddle score
        Paddle_AddScore(paddle);
        
        // Play collision sound (800 Hz)
        PongEngine_Beep(BUZZER_PADDLE_FREQ_HZ);
    }
}
```

**This is the ONLY collision test that uses AABB_Collides().**

### 3. Wall Collisions (Simple Boundary Checks - NOT AABB)

In **PongEngine.c**, the `PongEngine_CheckWallCollision()` function uses simple `if` statements:

```c
static void PongEngine_CheckWallCollision(PongEngine_t* engine) {
    Ball_t* ball = &engine->ball;
    Vector2D vel = Ball_GetVelocity(ball);
    
    // Top wall collision - reverse Y velocity
    if (ball->y <= 0) {
        ball->y = 2;
        Ball_SetVelocity(ball, vel.x, -vel.y);
        PongEngine_Beep(BUZZER_WALL_FREQ_HZ);  // 1200 Hz beep
    }
    // Bottom wall collision - reverse Y velocity
    else if (ball->y + ball->size >= SCREEN_HEIGHT) {
        ball->y = SCREEN_HEIGHT - ball->size - 2;
        Ball_SetVelocity(ball, vel.x, -vel.y);
        PongEngine_Beep(BUZZER_WALL_FREQ_HZ);
    }
    
    // Right wall collision - reverse X velocity
    if (ball->x + ball->size >= SCREEN_WIDTH) {
        ball->x = SCREEN_WIDTH - ball->size - 2;
        Ball_SetVelocity(ball, -vel.x, vel.y);
        PongEngine_Beep(BUZZER_WALL_FREQ_HZ);
    }
}
```

**Note:** Wall collisions are NOT using AABB_Collides(). They're simple boundary checks because the paddle only moves vertically - we don't need a full AABB collision test for walls.

### 4. Goal Detection (Ball Missed - Simple Boundary Check)

In **PongEngine.c**, the `PongEngine_CheckGoal()` function:

```c
static void PongEngine_CheckGoal(PongEngine_t* engine) {
    Ball_t* ball = &engine->ball;
    
    // Ball left the left edge (missed by paddle)
    if (ball->x < 0) {
        engine->lives--;
        
        // Reset ball near center with random offset
        Position2D center_pos;
        center_pos.x = (SCREEN_WIDTH - ball->size) / 2;
        center_pos.y = (SCREEN_HEIGHT - ball->size) / 2;
        
        // Add random offset to vary reset position
        int16_t dx = (int16_t)Random_U16((uint16_t)(2 * BALL_RESET_OFFSET + 1)) - BALL_RESET_OFFSET;
        int16_t dy = (int16_t)Random_U16((uint16_t)(2 * BALL_RESET_OFFSET + 1)) - BALL_RESET_OFFSET;
        
        center_pos.x += dx;
        center_pos.y += dy;
        
        Ball_SetPos(ball, center_pos);
        
        // Reset velocity to initial direction (move right)
        Ball_SetVelocity(ball, 8.0f * 0.707f, 8.0f * 0.707f);
    }
}
```

**Note:** Goal detection is also NOT using AABB_Collides(). It's just checking `if (ball->x < 0)` to detect when the ball goes off the left edge.

## How Pong Actually Uses Collision

### The Full PongEngine_Update() Loop

```c
uint8_t PongEngine_Update(PongEngine_t* engine, UserInput input) {
    // Step 1: Update paddle based on input
    Paddle_Update(&engine->paddle, input);
    
    // Step 2: Update ball position
    Ball_Update(&engine->ball);
    
    // Step 3: Check collisions
    PongEngine_CheckWallCollision(engine);      // Simple boundary checks
    PongEngine_CheckPaddleCollision(engine);    // AABB_Collides() here
    PongEngine_CheckGoal(engine);               // Simple boundary check
    
    // Step 4: Update buzzer timing (decay beep)
    PongEngine_UpdateBuzzer();
    
    return engine->lives;
}
```

### Collision Method Summary

Each collision type uses the method best suited to its constraints:

1. **Ball vs Paddle** - Uses `AABB_Collides()` because both objects move and we need to detect precise overlap
2. **Ball vs Walls** - Uses simple `if` checks (e.g., `if (ball->y <= 0)`) because walls are static and axis-aligned
3. **Ball vs Goal** - Uses simple `if (ball->x < 0)` because we only check one axis

## Common Pitfalls in the Actual Code (and How We Solve Them)

### Pitfall 1: Ball Getting Stuck Inside the Paddle

**Problem:** If we only reverse velocity, ball overlaps with paddle and gets stuck bouncing.

**Solution from PongEngine.c:** Push the ball out after reversing velocity

```c
if (AABB_Collides(&ball_box, &paddle_box)) {
    Ball_SetVelocity(ball, -vel.x, vel.y);        // Reverse
    ball->x = paddle_box.x + paddle_box.width;     // Push out to the right
    Paddle_AddScore(paddle);
    PongEngine_Beep(BUZZER_PADDLE_FREQ_HZ);
}
```

### Pitfall 2: Using AABB for Everything (Inefficient)

**Wasteful approach:** Creating AABB structs for wall checks

```c
// DON'T DO THIS - wasteful
AABB wall = {0, -5, 240, 5};
if (AABB_Collides(&ball_box, &wall)) { ... }
```

**Better approach:** Use simple boundary checks for static walls

```c
// DO THIS - fast and clear
if (ball->y <= 0) {
    ball->y = 2;
    Ball_SetVelocity(ball, vel.x, -vel.y);
}
```

This is what the actual code does. It's 2x faster and way more readable.

### Pitfall 3: Ball Position Still in Wall After Bounce

**Problem:** Reverse velocity but don't clamp position - ball stays overlapped.

**Solution from PongEngine.c:** Clamp position BEFORE reversing velocity

```c
// Top wall
if (ball->y <= 0) {
    ball->y = 2;                              // ← Clamp first
    Ball_SetVelocity(ball, vel.x, -vel.y);   // ← Then bounce
    PongEngine_Beep(BUZZER_WALL_FREQ_HZ);
}
```

## Key Takeaway

AABB collision detection is useful, but not universally. The Pong code pragmatically uses:
- 1 call to `AABB_Collides()` for ball-paddle overlap detection
- 3 simple `if` statements for wall bounces and goal detection

This teaches an important lesson: match the algorithm to the problem, not vice versa. Simpler solutions are often better.
