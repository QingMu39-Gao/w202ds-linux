/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef W202_ROTATION_H
#define W202_ROTATION_H
/* Invert the board input mount when going from KMS to native panel space.
 * r6p0 and DRM use the same quarter-turn indices (0, 90, 180, 270).
 */
static inline unsigned int w202_rotation_code(unsigned int drm_quarter,
                                              unsigned int mount_degrees)
{
    return (drm_quarter + 4 - mount_degrees / 90) & 3;
}
#endif
