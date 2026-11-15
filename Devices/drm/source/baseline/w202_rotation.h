/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef W202_ROTATION_H
#define W202_ROTATION_H
/* Installation correction is clockwise in input coordinates. DRM plane
 * requests are counterclockwise; r6p0 source codes are clockwise quarter turns.
 * KMS mode is already the installed 1920x1200 rectangle.
 */
static inline unsigned int w202_rotation_code(unsigned int drm_quarter,
                                              unsigned int mount_degrees)
{
    return (4 - mount_degrees / 90 + 4 - drm_quarter) & 3;
}
#endif
