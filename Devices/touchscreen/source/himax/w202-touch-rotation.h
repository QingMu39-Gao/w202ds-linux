/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef W202_TOUCH_ROTATION_H
#define W202_TOUCH_ROTATION_H
/* Clockwise mounting correction in input coordinates (Y points down).
 * Quarter turns swap both the coordinates and their advertised axis ranges.
 * Raw controller limits remain unchanged for packet validation.
 */
static inline void w202_touch_rotate(unsigned int degrees,
        int xmin, int xmax, int ymin, int ymax, int *x, int *y)
{
    int raw_x = *x, raw_y = *y;
    switch (degrees) {
    case 90:  *x = ymin + ymax - raw_y; *y = raw_x; break;
    case 180: *x = xmin + xmax - raw_x;
              *y = ymin + ymax - raw_y; break;
    case 270: *x = raw_y; *y = xmin + xmax - raw_x; break;
    default: break;
    }
}
#endif
