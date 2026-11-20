/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "relay.h"
#include <string.h>

#define SPEED_LIMIT (10 * FIXED)
#define RAIL_RADIUS 8
#define TICKS 120
enum { ON_TABLE, WEST_RIDE, EAST_RIDE, SHOOTER, LEFT_OUT, RIGHT_OUT };

typedef struct { short x1, y1, x2, y2; } Rail;
const short bumpers[3][2] = {{135,120},{174,96},{210,134}};
const short goals[STATION_COUNT][2] = {{55,230},{160,156},{245,266},{161,206}};
const short flipper_tip[4][2] = {{39,21},{44,10},{46,-3},{42,-19}};

/* Ramps and orbits leave the collision plane only after an entry crossing. */
static const Rail rails[] = {
    {27,37,16,73}, {16,73,16,402}, {16,402,34,462}, {34,462,76,516},
    {27,37,73,24}, {73,24,253,24}, {253,24,299,50}, {299,50,307,90},
    {307,90,307,528}, {281,116,281,529}, {293,534,307,528},
    {281,116,266,64}, {266,64,237,47},
    {16,347,49,405}, {49,405,68,467}, {68,467,105,487},
    {281,347,263,406}, {263,406,252,467}, {252,467,215,487},
    {75,382,104,444}, {104,444,72,461}, {72,461,75,382},
    {247,382,216,444}, {216,444,248,461}, {248,461,247,382},
    {24,180,33,233}, {33,233,34,270}, {73,223,71,251},
    {222,260,205,301}, {268,244,268,288},
    {79,87,103,67}, {218,79,245,96}
};

static int absolute(int value)
{
    return value < 0 ? -value : value;
}

static int count_bits(int mask)
{
    return (mask & 1) + ((mask >> 1) & 1) + ((mask >> 2) & 1);
}

static void serve(Ball *ball, int power)
{
    memset(ball, 0, sizeof(*ball));
    ball->x = 295 * FIXED;
    ball->y = 510 * FIXED;
    ball->active = 1;
    ball->vy = (-5 * FIXED) - power * 12;
    ball->route = SHOOTER;
    ball->ride = 0;
}

void relay_reset(Relay *g)
{
    unsigned best = g->best;
    memset(g, 0, sizeof(*g));
    g->best = best;
    g->mode = WAITING;
    g->balls = 3;
}

void relay_launch(Relay *g)
{
    serve(&g->ball[0], g->power);
    g->protect = 4 * TICKS;
    g->mode = PLAYING;
}

static void award(Relay *g, unsigned points)
{
    if (g->tilt >= 100)
        return;
    g->score += points;
    if (g->score > g->best)
        g->best = g->score;
}

static void station(Relay *g, int which)
{
    if (g->tilt >= 100)
        return;
    if (g->multi) {
        if (which != ARRAY)
            g->jackpots |= 1 << which;
        award(g, 750);
    } else {
        award(g, 1500);
        g->stamps |= 1 << which;
        if (g->stamps == 7 && !g->dish_time)
            g->dish_time = 10 * TICKS;
    }
    g->flash = 16;
}

/* Work in sixteenths of a pixel for local normals and use a small integer
 * square root. The bounding box keeps contact products within 32 bits. */
static int distance_root(int squared)
{
    int bit = 1 << 16;
    int root = 0;
    while (bit > squared)
        bit >>= 2;
    while (bit) {
        if (squared >= root + bit) {
            squared -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    return root;
}

static int bounce(Ball *b, int px, int py, int radius, int kick)
{
    int dx = (b->x - px) / 16;
    int dy = (b->y - py) / 16;
    int squared, dist, nx, ny, inward;
    radius *= 16;
    if (absolute(dx) >= radius || absolute(dy) >= radius)
        return 0;
    squared = dx * dx + dy * dy;
    if (squared >= radius * radius)
        return 0;
    dist = distance_root(squared);
    nx = dist ? dx * FIXED / dist : 0;
    ny = dist ? dy * FIXED / dist : FIXED;
    b->x += (radius - dist + 1) * nx / 16;
    b->y += (radius - dist + 1) * ny / 16;
    inward = (b->vx * nx + b->vy * ny) / FIXED;
    if (inward >= 0)
        return 0;
    inward = kick - 7 * inward / 4;
    b->vx += inward * nx / FIXED;
    b->vy += inward * ny / FIXED;
    return 1;
}

static int rail(Ball *b, int x1, int y1, int x2, int y2, int radius)
{
    int x = b->x / FIXED;
    int y = b->y / FIXED;
    int dx = x2 - x1;
    int dy = y2 - y1;
    int along, length;
    if ((x < x1 - radius && x < x2 - radius) ||
        (x > x1 + radius && x > x2 + radius) ||
        (y < y1 - radius && y < y2 - radius) ||
        (y > y1 + radius && y > y2 + radius))
        return 0;
    along = (x - x1) * dx + (y - y1) * dy;
    length = dx * dx + dy * dy;
    if (along <= 0)
        along = 0;
    else if (along >= length)
        along = FIXED;
    else
        along = along * FIXED / length;
    return bounce(b, x1 * FIXED + dx * along, y1 * FIXED + dy * along,
                  radius, 0);
}

static void flippers(Relay *g, Ball *b)
{
    int flip, side, base, tipped, hit;
    for (flip = 0; flip < 2; flip++) {
        side = flip ? -1 : 1;
        base = flip ? 215 : 105;
        tipped = g->flip[flip];
        hit = rail(b, base, 487, base + side * flipper_tip[tipped][0],
                   487 + flipper_tip[tipped][1], 10);
        if (hit && tipped && (g->held & (1 << flip)) && !b->flip_wait) {
            b->vx += side * (2 * FIXED);
            b->vy = -8 * FIXED;
            b->flip_wait = 10;
            award(g, 25);
        }
    }
}

static int crossed(int before, int after, int line)
{
    return before > line * FIXED && after <= line * FIXED;
}

static void gate(Relay *g, Ball *b, int previous_y)
{
    int x = b->x / FIXED;
    if (x >= 35 && x <= 73 && crossed(previous_y, b->y, 230)) {
        b->route = WEST_RIDE;
        b->ride = 0;
        station(g, WEST);
    } else if (x >= 230 && x <= 267 && crossed(previous_y, b->y, 266)) {
        b->route = EAST_RIDE;
        b->ride = 0;
        station(g, EAST);
    } else if (x >= 150 && x <= 172 && crossed(previous_y, b->y, 206) &&
               g->tilt < 100 && (g->dish_time || g->jackpots)) {
        if (g->multi) {
            award(g, 15000u * (unsigned)count_bits(g->jackpots));
            g->jackpots = 0;
        } else {
            award(g, 10000);
            g->dispatches++;
            g->multi = 1;
            g->dish_time = 0;
            serve(b == &g->ball[0] ? &g->ball[1] : &g->ball[0], 55);
        }
        b->x = 161 * FIXED;
        b->y = 210 * FIXED;
        b->vx = -2 * FIXED;
        b->vy = 4 * FIXED;
        g->flash = 90;
    }
}

/* Per-tick movement along deterministic rollover paths. */
static void ride(Relay *g, Ball *b)
{
    static const short path[2][5][2] = {
        {{55,230},{33,94},{71,42},{236,46},{265,368}},
        {{245,266},{263,177},{232,52},{84,56},{52,368}}
    };
    int part, f, x, y;
    if (b->route == LEFT_OUT || b->route == RIGHT_OUT) {
        b->x += b->route == LEFT_OUT ? -96 : 96;
        b->y += 4 * FIXED;
        if (b->y >= 535 * FIXED)
            b->active = 0;
        return;
    }
    if (b->route == SHOOTER) {
        if (!b->ride) {
            b->y += b->vy;
            if (b->y > 60 * FIXED)
                return;
            b->y = 60 * FIXED;
            b->ride = 1;
        } else {
            b->x -= 4 * FIXED;
            b->y -= FIXED / 2;
        }
        if (++b->ride >= 15) {
            b->vx = (-3 * FIXED) - g->power * 7;
            b->vy = FIXED;
            b->route = ON_TABLE;
            if (g->power >= 46 && g->power <= 57 && !g->multi)
                award(g, 2500);
        }
        return;
    }
    part = b->ride >> 4;
    f = b->ride & 15;
    x = path[b->route - 1][part][0];
    y = path[b->route - 1][part][1];
    b->x = x * FIXED + (path[b->route - 1][part + 1][0] - x) * f * 16;
    b->y = y * FIXED + (path[b->route - 1][part + 1][1] - y) * f * 16;
    if (++b->ride == 64) {
        b->vx = b->route == WEST_RIDE ? -FIXED : FIXED;
        b->vy = 3 * FIXED;
        b->route = ON_TABLE;
    }
}

static void physics(Relay *g, Ball *b)
{
    int sub, i, before, speed;
    if (b->route) {
        ride(g, b);
        return;
    }
    if (b->bump_wait)
        --b->bump_wait;
    if (b->flip_wait)
        --b->flip_wait;
    b->vy += 17;
    b->vx += g->nudge;
    speed = absolute(b->vx) + absolute(b->vy);
    if (speed > SPEED_LIMIT) {
        b->vx = b->vx * SPEED_LIMIT / speed;
        b->vy = b->vy * SPEED_LIMIT / speed;
    }
    for (sub = 0; sub < 4; sub++) {
        before = b->y;
        b->x += b->vx / 4;
        b->y += b->vy / 4;
        for (i = 0; i < (int)(sizeof(rails) / sizeof(rails[0])); i++)
            rail(b, rails[i].x1, rails[i].y1, rails[i].x2, rails[i].y2,
                 RAIL_RADIUS);
        for (i = 0; i < 3; i++) {
            if (bounce(b, bumpers[i][0] * FIXED, bumpers[i][1] * FIXED,
                       23, 2 * FIXED) && !b->bump_wait) {
                b->bump_wait = 10;
                award(g, 250);
                if (g->tilt < 100) {
                    g->stars |= 1 << i;
                    if (g->stars == 7) {
                        station(g, ARRAY);
                        g->stars = 0;
                    }
                }
            }
        }
        flippers(g, b);
        gate(g, b, before);
        if (b->route)
            break;
        if (b->y >= 400 * FIXED && b->vy > 0) {
            if (b->x <= 69 * FIXED)
                b->route = LEFT_OUT;
            else if (b->x >= 254 * FIXED)
                b->route = RIGHT_OUT;
            if (b->route)
                break;
        }
    }
    if (b->y >= 535 * FIXED || b->x < 0 || b->x >= FIELD_W * FIXED ||
        b->y < 0)
        b->active = 0;
}

static void collide_balls(Ball *a, Ball *b)
{
    int avx = a->vx;
    int avy = a->vy;
    int dx = (a->x - b->x) / 16;
    int dy = (a->y - b->y) / 16;
    int dist, normal;
    if (absolute(dx) >= 192 || absolute(dy) >= 192 || a->route || b->route)
        return;
    dist = distance_root(dx * dx + dy * dy);
    if (!dist || dist >= 192)
        return;
    normal = ((avx - b->vx) * dx + (avy - b->vy) * dy) / dist;
    if (normal < 0) {
        a->vx -= normal * dx / dist;
        a->vy -= normal * dy / dist;
        b->vx += normal * dx / dist;
        b->vy += normal * dy / dist;
    }
    a->x += (192 - dist) * dx * 16 / dist;
    a->y += (192 - dist) * dy * 16 / dist;
}

void relay_tick(Relay *g, int held)
{
    int i, up;
    if (g->paused)
        return;
    g->tick++;
    g->held = g->tilt >= 100 ? 0 : held;
    for (i = 0; i < 2; i++) {
        up = (g->held >> i) & 1;
        if (up && g->flip[i] < 3)
            g->flip[i]++;
        else if (!up && g->flip[i])
            g->flip[i]--;
    }
    if (g->mode == CHARGING) {
        g->power = (int)(g->tick % 160);
        if (g->power > 80)
            g->power = 160 - g->power;
    }
    if (g->mode != PLAYING)
        return;
    if (g->protect)
        g->protect--;
    if (g->dish_time)
        g->dish_time--;
    if (g->flash)
        g->flash--;
    if (g->tilt > 0 && g->tilt < 100)
        g->tilt--;
    for (i = 0; i < 2; i++) {
        if (g->ball[i].active)
            physics(g, &g->ball[i]);
    }
    g->nudge = 0;
    if (g->ball[0].active && g->ball[1].active)
        collide_balls(&g->ball[0], &g->ball[1]);
    else if (g->multi) {
        g->multi = g->stamps = g->stars = g->jackpots = 0;
    }
    if (g->ball[0].active || g->ball[1].active)
        return;
    if (g->protect && g->tilt < 100) {
        serve(&g->ball[0], 65);
        g->protect = 0;
        return;
    }
    g->tilt = 0;
    g->mode = --g->balls ? WAITING : GAME_OVER;
}

/* Scripted entries still pass through physics and the regular scoring gates. */
int relay_verify(Relay *g)
{
    Ball *b = &g->ball[0];
    unsigned before;
    int i, entered = 0;
    relay_reset(g);
    g->power = 51;
    relay_launch(g);
    for (i = 0; i < 160 && b->route; i++)
        relay_tick(g, 0);
    if (b->route || !g->score)
        return 1;
    for (i = 0; i < 3000 && g->mode == PLAYING; i++) {
        relay_tick(g, 0);
        if (!b->route && b->x < 280 * FIXED && b->y > 90 * FIXED)
            entered = 1;
    }
    if (!entered || g->mode != WAITING || g->balls != 2)
        return 8;
    g->best = 0;
    relay_reset(g);
    g->power = 51;
    relay_launch(g);
    for (i = 0; i < 160 && b->route; i++)
        relay_tick(g, 0);
    b->x = 55 * FIXED;
    b->y = 231 * FIXED;
    b->vx = 0;
    b->vy = -5 * FIXED;
    relay_tick(g, 0);
    if (!(g->stamps & 1))
        return 2;
    b->route = 0;
    b->x = 247 * FIXED;
    b->y = 267 * FIXED;
    b->vx = 0;
    b->vy = -5 * FIXED;
    relay_tick(g, 0);
    if (!(g->stamps & 4))
        return 3;
    b->route = 0;
    for (i = 0; i < 3; i++) {
        b->x = bumpers[i][0] * FIXED;
        b->y = (bumpers[i][1] + 23) * FIXED;
        b->vx = 0;
        b->vy = -4 * FIXED;
        b->bump_wait = 0;
        relay_tick(g, 0);
    }
    if (g->stamps != 7 || !g->dish_time)
        return 4;
    b->x = 161 * FIXED;
    b->y = 207 * FIXED;
    b->vx = 0;
    b->vy = -5 * FIXED;
    relay_tick(g, LEFT_FLIP | RIGHT_FLIP);
    if (!g->multi || !g->ball[1].active || g->dispatches != 1)
        return 5;
    before = g->score;
    g->tilt = 100;
    b->x = 55 * FIXED;
    b->y = 231 * FIXED;
    b->vx = 0;
    b->vy = -5 * FIXED;
    relay_tick(g, LEFT_FLIP);
    if (g->score != before || g->held)
        return 6;
    g->tilt = 0;
    g->ball[1].route = 0;
    g->ball[1].y = 537 * FIXED;
    relay_tick(g, 0);
    if (g->multi || g->balls != 3)
        return 7;

    /* Canonical completed-dispatch frame for host and emulator capture. */
    g->multi = 1;
    g->flash = 80;
    g->stamps = 7;
    g->stars = 5;
    b->active = 1;
    b->route = 0;
    b->x = 143 * FIXED;
    b->y = 445 * FIXED;
    g->ball[1].active = 1;
    g->ball[1].x = 227 * FIXED;
    g->ball[1].y = 188 * FIXED;
    g->flip[0] = 3;
    g->flip[1] = 1;
    return 0;
}
