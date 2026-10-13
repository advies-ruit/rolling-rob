#ifndef ROLLING_ROB_ROB3D_VIEW_H
#define ROLLING_ROB_ROB3D_VIEW_H

#include <stdbool.h>

/*
 * Rolling Rob seen in 3D (2.5D): the same game, drawn by the 3D rasterizer
 * (src/r3d) instead of draw.c. It only reads the game's state -- the view
 * game.c hands its renderer, and the entity arrays -- so the play is the
 * same to the bit: physics, collisions and the camera stay in game.c's 2D
 * world. The world's plane is z = 0; the ground is a strip with depth, Rob
 * a ball, the monsters, saucers, mines and Julian simple solid shapes, and
 * hills and mountains behind give the depth.
 *
 * The 3 key switches it on and off while playing; ROLLING_ROB_3D=1 starts
 * with it on.
 */
void rob3d_view_init(void);
bool rob3d_view_on(void);
void rob3d_view_set(bool on);

#endif /* ROLLING_ROB_ROB3D_VIEW_H */
