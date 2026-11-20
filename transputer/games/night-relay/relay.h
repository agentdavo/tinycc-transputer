/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef NIGHT_RELAY_H
#define NIGHT_RELAY_H

#include <stdint.h>

#define FIELD_W 320
#define FIELD_H 552
#define FIELD_X 240
#define FIELD_Y 24
#define BALL_RADIUS 6
#define FIXED 256

#define LEFT_FLIP 1
#define RIGHT_FLIP 2

enum { WEST, ARRAY, EAST, DISH, STATION_COUNT };
enum { WAITING, CHARGING, PLAYING, GAME_OVER };

typedef struct {
    int x, y, vx, vy;
    int active, route, ride;
    int bump_wait, flip_wait;
} Ball;

typedef struct {
    Ball ball[2];
    unsigned score, best, tick;
    int mode, balls, power, stamps, stars, jackpots, dispatches;
    int dish_time, multi, protect, tilt, nudge, flash, paused;
    int flip[2], held;
} Relay;

extern const short bumpers[3][2];
extern const short goals[STATION_COUNT][2];
extern const short flipper_tip[4][2];

void relay_reset(Relay *game);
void relay_launch(Relay *game);
void relay_tick(Relay *game, int held);
int relay_verify(Relay *game);
void relay_draw_start(uint32_t *screen);
void relay_draw(Relay *game, int all);

#endif
