/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "relay.h"

enum { BLACK, DEEP, PANEL, EDGE, SKY, LACQUER, PLATE, GUIDE, SHADOW,
       BRASS, GOLD, BRIGHT, ICE, CYAN, AMBER, VIOLET, WHITE, RED };

static const uint32_t colours[] = {
    0x090e1b, 0x101829, 0x151e31, 0x24364d, 0x546989, 0x111a31,
    0x1a2742, 0x32486b, 0x0b1020, 0xb98548, 0xd8a460, 0xffdb91,
    0xa5f0ef, 0x55deef, 0xffb344, 0x7770d1, 0xfffaf0, 0xbf645e
};
static const unsigned char font[36][7] = {
    {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},
    {30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
    {14,17,16,23,17,17,14},{17,17,17,31,17,17,17},{31,4,4,4,4,4,31},
    {7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},
    {30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
    {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},
    {17,17,17,17,17,10,4},{17,17,17,21,21,27,17},{17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4},{31,1,2,4,8,16,31},
    {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
    {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
    {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
    {14,17,17,15,1,1,14}
};

static unsigned char field[FIELD_H][FIELD_W];
static uint32_t *fb;
static int building;
static int old_ball_x[2], old_ball_y[2], old_flip[2];
static int old_stamps, old_mode, old_tilt, old_power, old_lights;
static int old_balls, old_dispatches;
static unsigned old_score;

static int absval(int x) { return x < 0 ? -x : x; }

/* Cabinet art is generated into one indexed playfield. The same primitives
 * write animated details directly to the already allocated framebuffer. */
static void span(int x, int y, int width, int colour)
{
    int i;
    if (building) {
        if (y < 0 || y >= FIELD_H || x >= FIELD_W)
            return;
        if (x < 0) {
            width += x;
            x = 0;
        }
        if (x + width > FIELD_W)
            width = FIELD_W - x;
        for (i = 0; i < width; i++)
            field[y][x + i] = (unsigned char)colour;
    } else {
        uint32_t *p;
        uint32_t value = colours[colour];
        if (y < 0 || y >= 600 || x >= 800)
            return;
        if (x < 0) {
            width += x;
            x = 0;
        }
        if (x + width > 800)
            width = 800 - x;
        p = fb + y * 800 + x;
        for (i = 0; i < width; i++)
            p[i] = value;
    }
}

static void box(int x, int y, int w, int h, int colour)
{
    int row;
    for (row = 0; row < h; row++)
        span(x, y + row, w, colour);
}

static void disk(int x, int y, int r, int colour)
{
    int dy, half;
    for (dy = -r; dy <= r; dy++) {
        half = r - absval(dy) / 2;
        while (half * half + dy * dy > r * r)
            half--;
        span(x - half, y + dy, 2 * half + 1, colour);
    }
}

static void line(int x, int y, int x2, int y2, int half, int colour)
{
    int dx = absval(x2 - x);
    int dy = -absval(y2 - y);
    int sx = x < x2 ? 1 : -1;
    int sy = y < y2 ? 1 : -1;
    int err = dx + dy, twice;

    for (;;) {
        box(x - half, y - half, half * 2 + 1, half * 2 + 1, colour);
        if (x == x2 && y == y2)
            break;
        twice = 2 * err;
        if (twice >= dy) {
            err += dy;
            x += sx;
        }
        if (twice <= dx) {
            err += dx;
            y += sy;
        }
    }
}

static void type(int x, int y, const char *s, int scale, int colour)
{
    int c, row, col;
    while (*s) {
        c = *s++;
        if (c >= 'A' && c <= 'Z')
            c -= 'A';
        else if (c >= '0' && c <= '9')
            c = c - '0' + 26;
        else
            c = -1;
        if (c >= 0) {
            for (row = 0; row < 7; row++) {
                for (col = 0; col < 5; col++) {
                    if (font[c][row] & (16 >> col))
                        box(x + col * scale, y + row * scale, scale, scale, colour);
                }
            }
        }
        x += scale * 6;
    }
}

static void number(int x, int y, unsigned value, int digits, int scale, int colour)
{
    char buf[9];
    int i;
    buf[digits] = '\0';
    for (i = digits - 1; i >= 0; i--) {
        buf[i] = (char)('0' + value % 10);
        value /= 10;
    }
    type(x, y, buf, scale, colour);
}

static void restore(int x, int y, int width, int height)
{
    int i, row;
    if (x < 0) {
        width += x;
        x = 0;
    }
    if (y < 0) {
        height += y;
        y = 0;
    }
    if (x + width > FIELD_W)
        width = FIELD_W - x;
    if (y + height > FIELD_H)
        height = FIELD_H - y;
    for (row = 0; row < height; row++) {
        uint32_t *p = fb + (FIELD_Y + y + row) * 800 + FIELD_X + x;
        const unsigned char *src = &field[y + row][x];
        for (i = 0; i < width; i++)
            p[i] = colours[src[i]];
    }
}

static void rail_art(int x, int y, int x2, int y2, int colour)
{
    line(x + 3, y + 4, x2 + 3, y2 + 4, 3, SHADOW);
    line(x, y, x2, y2, 2, colour);
    line(x, y - 1, x2, y2 - 1, 0, colour == BRASS ? GOLD : SKY);
}

static void draw_table(void)
{
    unsigned stars = 0x1483;
    int i, x, y;
    building = 1;
    box(0, 0, FIELD_W, FIELD_H, LACQUER);
    box(5, 5, 310, 20, PLATE);
    for (i = 0; i < 240; i++) {
        stars = stars * 1664525u + 1013904223u;
        x = 19 + (int)((stars >> 16) % 260);
        y = 31 + (int)((stars >> 7) % 445);
        span(x, y, i % 17 == 0 ? 2 : 1, i % 17 == 0 ? SKY : GUIDE);
    }
    for (y = 154; y < 535; y += 28)
        span(23, y, 251, PLATE);
    for (x = 38; x < 280; x += 27)
        line(x, 159, x, 518, 0, PLATE);
    disk(161, 206, 58, SHADOW);
    disk(161, 203, 58, GUIDE);
    disk(161, 203, 55, PLATE);
    disk(161, 203, 45, LACQUER);
    disk(161, 203, 29, PLATE);
    line(117, 204, 205, 204, 0, GUIDE);
    line(161, 160, 161, 247, 0, GUIDE);
    type(93, 269, "TRANSMISSION", 2, SKY);
    type(111, 292, "OBSERVATORY", 1, GUIDE);

    rail_art(27,37,16,73,BRASS);
    rail_art(27,37,73,24,BRASS);
    rail_art(73,24,253,24,BRASS);
    rail_art(253,24,299,50,BRASS);
    rail_art(299,50,307,90,BRASS);
    rail_art(16,73,16,402,BRASS);
    rail_art(307,90,307,528,BRASS);
    rail_art(281,116,281,529,BRASS);
    rail_art(281,116,266,64,BRASS);
    rail_art(16,402,34,462,BRASS);
    rail_art(34,462,76,516,BRASS);
    rail_art(16,347,49,405,GUIDE);
    rail_art(49,405,68,467,BRASS);
    rail_art(68,467,105,487,BRASS);
    rail_art(281,347,263,406,GUIDE);
    rail_art(263,406,252,467,BRASS);
    rail_art(252,467,215,487,BRASS);
    rail_art(75,382,104,444,BRASS);
    rail_art(104,444,72,461,BRASS);
    rail_art(247,382,216,444,BRASS);
    rail_art(216,444,248,461,BRASS);
    rail_art(24,180,33,233,GUIDE);
    rail_art(73,223,71,251,GUIDE);
    rail_art(222,260,205,301,GUIDE);
    rail_art(268,244,268,288,GUIDE);

    /* Heavy amber ramp edges frame a raised cyan middle channel. */
    line(247,249,263,178,5,SHADOW);
    line(263,178,232,54,5,SHADOW);
    line(247,247,263,176,3,GUIDE);
    line(263,176,232,54,3,GUIDE);
    line(244,242,260,174,0,SKY);
    line(260,174,228,56,0,SKY);
    line(51,217,30,116,2,GUIDE);
    line(30,116,61,51,2,GUIDE);
    type(85, 40, "WEST ORBIT", 1, SKY);
    type(178, 62, "EAST", 1, SKY);
    type(92, 326, "NIGHT DISPATCH", 1, SKY);
    type(114, 537, "RETURN", 1, GUIDE);
    for (i = 0; i < 8; i++) {
        span(285, 334 + i * 23, 8, SKY);
        span(295, 334 + i * 23, 4, GUIDE);
    }
    building = 0;
}

static void border(int x, int y, int w, int h)
{
    box(x, y, w, h, EDGE);
    box(x + 1, y + 1, w - 2, h - 2, PANEL);
    span(x + 1, y, w - 2, BRASS);
}

static void backdrop(void)
{
    int i;
    box(0, 0, 800, 600, BLACK);
    for (i = 24; i < 600; i += 6)
        span(0, i, 800, DEEP);
    border(14, 23, 212, 550);
    border(574, 23, 212, 550);
    box(235, 18, 330, 564, BRASS);
    box(237, 20, 326, 560, SHADOW);
    span(235, 18, 330, BRIGHT);
    type(30, 52, "NIGHT", 5, WHITE);
    type(30, 94, "RELAY", 5, CYAN);
    type(31, 142, "SUMMIT OBSERVATORY", 1, SKY);
    type(31, 186, "SCORE", 2, GOLD);
    type(31, 253, "SESSION BEST", 1, SKY);
    type(31, 309, "BALLS LEFT", 1, GOLD);
    type(31, 386, "DISPATCHES", 1, SKY);
    type(31, 540, "TRANSPUTEROS", 1, SKY);
    type(590, 56, "RELAY STATUS", 2, GOLD);
    type(614, 106, "WEST ORBIT", 2, WHITE);
    type(614, 145, "STAR ARRAY", 2, WHITE);
    type(614, 184, "EAST RAMP", 2, WHITE);
    type(590, 315, "LAUNCH POWER", 1, GOLD);
    type(590, 364, "TILT", 1, GOLD);
    type(590, 421, "MOUSE    FLIPPERS", 1, SKY);
    type(590, 441, "Z X      LEFT RIGHT", 1, SKY);
    type(590, 461, "SPACE    LAUNCH", 1, SKY);
    type(590, 481, "A D      NUDGE", 1, SKY);
    type(590, 501, "P        PAUSE", 1, SKY);
    type(590, 521, "ESC      QUIT", 1, SKY);
    line(29, 514, 80, 444, 0, GUIDE);
    line(80, 444, 110, 479, 0, GUIDE);
    line(110, 479, 153, 435, 0, SKY);
    line(153, 435, 211, 514, 0, GUIDE);
    line(146, 446, 153, 435, 0, ICE);
    line(153, 435, 161, 446, 0, ICE);
    span(29, 525, 180, EDGE);
    span(589, 408, 180, EDGE);
}

static void bumper_draw(int x, int y, int on)
{
    x += FIELD_X;
    y += FIELD_Y;
    disk(x + 2, y + 2, 20, SHADOW);
    disk(x, y, 19, on ? CYAN : GUIDE);
    disk(x, y, 16, SHADOW);
    disk(x, y - 1, 13, on ? SKY : PLATE);
    disk(x - 1, y - 3, 8, on ? ICE : SKY);
    disk(x, y, 4, on ? WHITE : GUIDE);
}

static void lamp(int x, int y, int on)
{
    disk(x, y, 12, SHADOW);
    disk(x, y, 10, on ? BRIGHT : GUIDE);
    disk(x, y, 8, on ? AMBER : PLATE);
    span(x - 4, y - 1, 9, on ? ICE : SKY);
}

static void flipper(int right, int pose)
{
    int x = FIELD_X + (right ? 215 : 105);
    int y = FIELD_Y + 487;
    int tip_x = x + (right ? -1 : 1) * flipper_tip[pose][0];
    int tip_y = y + flipper_tip[pose][1];
    line(x, y + 2, tip_x, tip_y + 2, 5, SHADOW);
    line(x, y, tip_x, tip_y, 4, pose ? CYAN : BRASS);
    line(x, y - 2, tip_x, tip_y - 2, 1, pose ? ICE : BRIGHT);
    disk(x, y, 8, SHADOW);
    disk(x, y, 5, GOLD);
    disk(x, y, 2, BRIGHT);
}

static void ball_at(int x, int y)
{
    x += FIELD_X;
    y += FIELD_Y;
    disk(x + 2, y + 3, 7, SHADOW);
    disk(x, y, 6, SKY);
    disk(x, y - 1, 5, ICE);
    disk(x - 2, y - 2, 2, WHITE);
}

static void update_hud(Relay *g, int all, int lights)
{
    static int was_paused;
    int i;
    if (all || old_score != g->score) {
        box(29, 208, 174, 32, PANEL);
        number(30, 211, g->score, 7, 4, WHITE);
        box(30, 265, 158, 20, PANEL);
        number(31, 268, g->best, 7, 2, ICE);
        old_score = g->score;
    }
    if (all || old_balls != g->balls || old_dispatches != g->dispatches) {
        for (i = 0; i < 3; i++)
            disk(45 + 25 * i, 336, 7, i < g->balls ? BRIGHT : GUIDE);
        box(30, 398, 132, 21, PANEL);
        number(31, 398, (unsigned)g->dispatches, 3, 2, CYAN);
        old_balls = g->balls;
        old_dispatches = g->dispatches;
    }
    if (all || old_stamps != g->stamps || old_lights != lights ||
        old_mode != g->mode || was_paused != g->paused) {
        for (i = 0; i < 3; i++)
            lamp(598, 112 + i * 39, (g->stamps >> i) & 1);
        box(590, 229, 179, 62, DEEP);
        span(590, 229, 179, lights ? CYAN : GUIDE);
        if (g->paused)
            type(603, 250, "PAUSED", 3, WHITE);
        else if (g->mode == GAME_OVER) {
            type(603, 247, "GAME OVER", 2, BRIGHT);
            type(603, 272, "SPACE FOR NEW GAME", 1, SKY);
        } else if (g->multi) {
            type(603, 246, "MULTIBALL", 2, CYAN);
            type(603, 272, "SHOOT ORBIT AND RAMP", 1, ICE);
        } else if (g->dish_time) {
            type(603, 246, "DISH OPEN", 2, AMBER);
            type(603, 272, "SHOOT THE DISH", 1, ICE);
        } else if (g->mode == WAITING || g->mode == CHARGING) {
            type(603, 247, "LAUNCH", 3, WHITE);
            type(603, 273, "SPACE TO FIRE", 1, SKY);
        } else {
            type(603, 247, "MAKE A SHOT", 2, WHITE);
            type(603, 272, "LIGHT THREE RELAYS", 1, SKY);
        }
        old_stamps = g->stamps;
        old_lights = lights;
        old_mode = g->mode;
        was_paused = g->paused;
    }
    if (all || old_power != g->power) {
        box(590, 332, 176, 11, SHADOW);
        box(590, 332, g->power * 2, 11, CYAN);
        box(682, 332, 23, 3, BRIGHT);
        old_power = g->power;
    }
    if (all || old_tilt != g->tilt) {
        box(590, 380, 176, 8, SHADOW);
        box(590, 380, g->tilt * 7 / 4, 8, RED);
        old_tilt = g->tilt;
    }
}

void relay_draw_start(uint32_t *screen)
{
    fb = screen;
    old_ball_x[0] = old_ball_x[1] = -1;
    draw_table();
    backdrop();
}

void relay_draw(Relay *g, int all)
{
    int i, lights = g->multi * 2 + (g->dish_time != 0);
    int width;
    if (all)
        restore(0, 0, FIELD_W, FIELD_H);
    for (i = 0; i < 2; i++) {
        if (old_ball_x[i] >= 0)
            restore(old_ball_x[i] - 8, old_ball_y[i] - 8, 19, 20);
        if (old_flip[i] != g->flip[i])
            restore((i ? 158 : 94), 457, 68, 63);
        old_flip[i] = g->flip[i];
    }
    /* Repaint inserts and flippers after restoring any overlapping ball. */
    for (i = 0; i < 3; i++)
        bumper_draw(bumpers[i][0], bumpers[i][1], (g->stars >> i) & 1);
    for (i = 0; i < STATION_COUNT; i++) {
        if (i != ARRAY)
            lamp(FIELD_X + goals[i][0], FIELD_Y + goals[i][1],
                 i == DISH ? lights != 0 : ((g->stamps >> i) & 1) != 0);
    }
    width = g->flash * 3;
    if (width > 304)
        width = 304;
    box(FIELD_X + 8, FIELD_Y + 7, 304, 4, PLATE);
    box(FIELD_X + 8, FIELD_Y + 7, width, 4, lights ? CYAN : AMBER);
    flipper(0, g->flip[0]);
    flipper(1, g->flip[1]);
    for (i = 0; i < 2; i++) {
        old_ball_x[i] = -1;
        if (g->ball[i].active) {
            old_ball_x[i] = g->ball[i].x / FIXED;
            old_ball_y[i] = g->ball[i].y / FIXED;
            ball_at(old_ball_x[i], old_ball_y[i]);
        }
    }
    if (g->mode == WAITING || g->mode == CHARGING) {
        old_ball_x[0] = 295;
        old_ball_y[0] = 510;
        ball_at(295, 510);
    }
    update_hud(g, all, lights);
}
