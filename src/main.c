#include "inttypes.h"
#include "raylib.h"
#include "raymath.h"
#include <math.h>
#include <stdio.h>

#define SCREEN_WIDTH 1024
#define SCREEN_HEIGHT 1024

#define GRAVITY_ACCELERATION 50.0
#define MAX_N_OBSTACLES 64

#define PLAYER_MAX_HEALTH 100.0
#define MAX_SPEED_WITHOUT_DAMAGE 30.0

static const Color BACKGROUND_COLOR = {20, 20, 20, 255};
static const Color OBSTACLE_COLOR = {80, 80, 80, 255};
static const Color UI_BACKGROUND_COLOR = {40, 40, 40, 255};

typedef struct Player {
    Vector2 position;
    Vector2 velocity;
    Vector2 size;

    float speed;
    float jump_impulse;

    float health;
    float max_health;

    bool is_grounded;
} Player;

static Player PLAYER = {0};

// -----------------------------------------------------------------------
// utils

// returns float uniform value from 0 to 1
float randf(void) {
    return GetRandomValue(0, INT32_MAX) / (float)INT32_MAX;
}

// returns float uniform value from min to max
float randf_min_max(float min, float max) {
    float p = randf();
    return min + p * (max - min);
}

Vector2 get_aabb_mtv(Rectangle r1, Rectangle r2) {
    Vector2 mtv = Vector2Zero();
    if (!CheckCollisionRecs(r1, r2)) return mtv;

    float x_west = r2.x - r1.x - r1.width;
    float x_east = r2.x + r2.width - r1.x;
    if (fabs(x_west) < fabs(x_east)) mtv.x = x_west;
    else mtv.x = x_east;

    float y_south = r2.y + r2.height - r1.y;
    float y_north = r2.y - r1.y - r1.height;
    if (fabs(y_south) < fabs(y_north)) mtv.y = y_south;
    else mtv.y = y_north;

    if (fabsf(mtv.x) > fabsf(mtv.y)) mtv.x = 0.0;
    else mtv.y = 0.0;

    return mtv;
}

Color lerp_color(Color min_color, Color max_color, float ratio) {
    return (Color){
        .r = (1.0 - ratio) * min_color.r + ratio * max_color.r,
        .g = (1.0 - ratio) * min_color.g + ratio * max_color.g,
        .b = (1.0 - ratio) * min_color.b + ratio * max_color.b,
        .a = (1.0 - ratio) * min_color.a + ratio * max_color.a,
    };
}

// -----------------------------------------------------------------------
// camera
static Camera2D CAMERA = {
    .offset = {0.5 * SCREEN_WIDTH, 0.5 * SCREEN_HEIGHT},
    .target = {0.0, 0.0},
    .rotation = 0.0,
    .zoom = 20.0,
};

// -----------------------------------------------------------------------
// obstacle
typedef struct Obstacle {
    Rectangle rect;

    // platform
    Vector2 start;
    Vector2 end;
    float speed;
    bool is_moving_to_start;
    bool is_player_attached;
} Obstacle;

static int N_OBSTACLES = 0;
static Obstacle OBSTACLES[MAX_N_OBSTACLES] = {0};

int spawn_obstacle(Rectangle rect, Vector2 start, Vector2 end, float speed) {
    if (N_OBSTACLES == MAX_N_OBSTACLES) return -1;

    int idx = N_OBSTACLES++;
    Obstacle *obstacle = &OBSTACLES[idx];
    obstacle->rect = rect;
    obstacle->start = start;
    obstacle->end = end;
    obstacle->speed = speed;

    return idx;
}

int spawn_static_obstacle(Rectangle rect) {
    Vector2 direction = Vector2Zero();
    Vector2 start = {rect.x, rect.y};
    Vector2 end = start;
    float speed = 0.0;
    return spawn_obstacle(rect, start, end, speed);
}

void draw_obstacles(void) {
    for (int i = 0; i < N_OBSTACLES; ++i) {
        Obstacle *obstacle = &OBSTACLES[i];
        DrawRectangleRec(obstacle->rect, OBSTACLE_COLOR);
    }
}

void draw_ui(void) {
    static const float margin = 10.0;
    static const float pad = 5.0;

    float dt = GetFrameTime();

    // -------------------------------------------------------------------
    // healthbar
    static const float width = 300.0;
    static const float height = 40.0;
    static float health_view_speed = 80.0;
    static float health_view = PLAYER_MAX_HEALTH;

    // update health view
    if (PLAYER.health < health_view) {
        float health_view_step = dt * health_view_speed;
        health_view -= health_view_step;
        health_view = health_view < PLAYER.health ? PLAYER.health : health_view;
    } else {
        health_view = PLAYER.health;
    }

    // background
    Rectangle background_rect = {
        .x = margin,
        .y = margin,
        .width = width,
        .height = height,
    };

    // healthbar
    Rectangle healthbar_rect = {
        .x = background_rect.x + pad,
        .y = background_rect.y + pad,
        .width = background_rect.width - 2.0 * pad,
        .height = background_rect.height - 2.0 * pad,
    };
    float health_ratio = PLAYER.health / PLAYER.max_health;
    healthbar_rect.width *= health_ratio;

    Color healthbar_color = lerp_color(RED, GREEN, health_ratio);

    // difference
    Rectangle difference_rect = {
        .x = healthbar_rect.x,
        .y = healthbar_rect.y,
        .width = background_rect.width - 2.0 * pad,
        .height = healthbar_rect.height,
    };
    float difference_ratio = health_view / PLAYER.max_health;
    difference_rect.width *= difference_ratio;

    DrawRectangleRounded(background_rect, 0.2, 16, UI_BACKGROUND_COLOR);
    DrawRectangleRounded(difference_rect, 0.2, 16, WHITE);
    DrawRectangleRounded(healthbar_rect, 0.2, 16, healthbar_color);
}

void update_obstacles(void) {
    float dt = GetFrameTime();

    for (int i = 0; i < N_OBSTACLES; ++i) {
        Obstacle *obstacle = &OBSTACLES[i];

        // don't update non-platform obstacles (zero speed)
        if (!(obstacle->speed > 0.0)) continue;

        // get platform direction
        Vector2 direction = Vector2Subtract(obstacle->end, obstacle->start);
        direction = Vector2Normalize(direction);
        if (obstacle->is_moving_to_start) direction = Vector2Negate(direction);

        // moving (immediate position change)
        Vector2 position_step = Vector2Scale(direction, dt * obstacle->speed);
        obstacle->rect.x += position_step.x;
        obstacle->rect.y += position_step.y;

        if (obstacle->is_player_attached) {
            PLAYER.position = Vector2Add(PLAYER.position, position_step);
        }

        // reverse platform movement if it reached the target
        Vector2 position = {obstacle->rect.x, obstacle->rect.y};
        Vector2 target = obstacle->is_moving_to_start ? obstacle->start : obstacle->end;
        Vector2 to_target_direction = Vector2Subtract(target, position);
        bool is_to_target = Vector2DotProduct(direction, to_target_direction) > 0.0;
        if (!is_to_target) {
            Vector2 clamped_position;
            if (obstacle->is_moving_to_start) {
                clamped_position = obstacle->start;
            } else {
                clamped_position = obstacle->end;
            }

            obstacle->rect.x = clamped_position.x;
            obstacle->rect.y = clamped_position.y;
            obstacle->is_moving_to_start ^= 1;
        }
    }
}

// -----------------------------------------------------------------------
// player
Rectangle get_player_rect(void) {
    return (Rectangle){
        .x = PLAYER.position.x + 0.5 * PLAYER.size.x,
        .y = PLAYER.position.y + PLAYER.size.y,
        .width = PLAYER.size.x,
        .height = PLAYER.size.y,
    };
}

void update_player(void) {
    float dt = GetFrameTime();

    // gravity
    PLAYER.velocity.y += GRAVITY_ACCELERATION * dt;

    // -------------------------------------------------------------------
    // keyboard inputs

    // moving (immediate position change)
    Vector2 direction = Vector2Zero();
    if (IsKeyDown(KEY_A)) direction.x -= 1.0;
    if (IsKeyDown(KEY_D)) direction.x += 1.0;

    direction = Vector2Normalize(direction);
    Vector2 position_step = Vector2Scale(direction, PLAYER.speed * dt);

    // jumping (velocity change)
    if (IsKeyPressed(KEY_W) && PLAYER.is_grounded) {
        PLAYER.velocity.y -= PLAYER.jump_impulse;
    }

    // velocity
    position_step = Vector2Add(position_step, Vector2Scale(PLAYER.velocity, dt));

    // apply position step
    PLAYER.position = Vector2Add(PLAYER.position, position_step);
}

void update_player_collisions(void) {
    float mtv_min_x = 0.0;
    float mtv_max_x = 0.0;
    float mtv_min_y = 0.0;
    float mtv_max_y = 0.0;
    for (int i = 0; i < N_OBSTACLES; ++i) {
        Obstacle *obstacle = &OBSTACLES[i];
        Rectangle obstacle_rect = obstacle->rect;
        Rectangle player_rect = get_player_rect();

        Vector2 mtv = get_aabb_mtv(player_rect, obstacle_rect);

        mtv_min_x = fminf(mtv_min_x, mtv.x);
        mtv_max_x = fmaxf(mtv_max_x, mtv.x);
        mtv_min_y = fminf(mtv_min_y, mtv.y);
        mtv_max_y = fmaxf(mtv_max_y, mtv.y);

        // attach player to the platform if needed
        obstacle->is_player_attached = mtv.y < 0.0 && obstacle->speed > 0.0;
    }

    Vector2 mtv = {mtv_min_x, mtv_min_y};
    if (fabsf(mtv_max_x) > fabsf(mtv_min_x)) mtv.x = mtv_max_x;
    if (fabsf(mtv_max_y) > fabsf(mtv_min_y)) mtv.y = mtv_max_y;
    PLAYER.position = Vector2Add(PLAYER.position, mtv);

    bool is_just_grounded = mtv.y < 0.0 && PLAYER.velocity.y > 0.0;
    if (is_just_grounded) {
        float speed = Vector2Length(PLAYER.velocity);
        float damage = speed - MAX_SPEED_WITHOUT_DAMAGE;
        damage = damage < 0.0 ? 0.0 : damage;

        PLAYER.health -= damage;

        PLAYER.velocity = Vector2Zero();
        PLAYER.is_grounded = true;
    } else if (mtv.y > 0.0 && PLAYER.velocity.y < 0.0) {
        PLAYER.velocity.y = 0.0;
    } else {
        PLAYER.is_grounded = false;
    }
}

void draw_player(void) {
    Rectangle rect = get_player_rect();
    DrawRectangleRec(rect, ORANGE);
}

// -----------------------------------------------------------------------
// game
void load_game(void) {
    // player
    PLAYER.position = Vector2Zero();
    PLAYER.velocity = Vector2Zero();
    PLAYER.size = (Vector2){1.0, 2.0};
    PLAYER.speed = 15.0;
    PLAYER.jump_impulse = 30.0;

    PLAYER.max_health = PLAYER_MAX_HEALTH;
    PLAYER.health = PLAYER.max_health;

    N_OBSTACLES = 0;

    // ground
    spawn_static_obstacle((Rectangle){.x = -20.0, .y = 20.0, .width = 40.0, .height = 2.5}
    );

    // left wall
    spawn_static_obstacle((Rectangle
    ){.x = -20.0, .y = -100.0, .width = 2.5, .height = 120.0});

    // left stair
    spawn_static_obstacle((Rectangle){.x = -17.5, .y = 15.0, .width = 2.5, .height = 5.0}
    );

    // right wall
    spawn_static_obstacle((Rectangle
    ){.x = 17.5, .y = -100.0, .width = 2.5, .height = 120.0});

    // platforms
    float x_min = -15.0;
    float x_max = 5.0;
    for (int i = 0; i < 10; ++i) {
        float y = 8.0 - i * 8.0;
        float x = randf_min_max(x_min, x_max);
        float speed = randf_min_max(5.0, 9.0);

        spawn_obstacle(
            (Rectangle){.x = x, .y = y, .width = 10.0, .height = 2.5},
            (Vector2){.x = x_min, .y = y},
            (Vector2){.x = x_max, .y = y},
            speed
        );
    }
}

void load(void) {
    // raylib window
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Platforms");
    SetTargetFPS(60);

    load_game();
}

void update_reset(void) {
    if (IsKeyPressed(KEY_R)) load_game();
}

void update_camera() {
    Vector2 target = PLAYER.position;
    float distance = Vector2Distance(target, CAMERA.target);
    Vector2 direction = Vector2Normalize(Vector2Subtract(target, CAMERA.target));
    Vector2 position_step = Vector2Scale(direction, 0.1 * distance);

    CAMERA.target = Vector2Add(CAMERA.target, position_step);
}

void update(void) {
    update_reset();
    update_player();
    update_obstacles();

    update_player_collisions();
    update_camera();
}

void draw(void) {
    BeginDrawing();
    ClearBackground(BACKGROUND_COLOR);

    BeginMode2D(CAMERA);
    draw_player();
    draw_obstacles();
    EndMode2D();

    draw_ui();

    EndDrawing();
}

void unload(void) {
    CloseWindow();
}

int main(void) {
    load();

    while (!WindowShouldClose()) {
        update();
        draw();
    }

    unload();
}
