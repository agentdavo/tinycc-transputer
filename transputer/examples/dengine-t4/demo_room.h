#ifndef T4_DEMO_ROOM_H
#define T4_DEMO_ROOM_H
#include "../distributed-fb/material_raster.h"
void demo_room_init(float center_x,float center_y,float center_z,float scale,
                    float camera_x,float camera_y,float camera_z);
int demo_room_draw(tg_context *,const tg_fragment_texture *shadow);
#endif
