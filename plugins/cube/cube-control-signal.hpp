#ifndef CUBE_CONTROL_SIGNAL
#define CUBE_CONTROL_SIGNAL

#include <wayfire/signal-definitions.hpp>

/* A private signal, currently shared by idle & cube
 *
 * It is used to rotate the cube from the idle plugin as a screensaver.
 */

/* Rotate cube to given angle and zoom level */
struct cube_control_signal
{
    double angle; // cube rotation in radians
    double zoom; // 1.0 means 100%; increase value to zoom
    double ease; // for cube deformation; range 0.0-1.0
    bool last_frame; // ends cube animation if true
    bool carried_out; // false if cube is disabled
};

#endif /* end of include guard: CUBE_CONTROL_SIGNAL */
