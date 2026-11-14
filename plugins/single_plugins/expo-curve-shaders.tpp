#pragma once
/* Curved expo row with reflection: a
 * post-processing pass over the finished expo frame (texture v = 0 is the
 * top of the screen). Each column of the workspace band is stretched
 * vertically about the band's centre line by s(u) = 1 + kt * u^2 (u = -1 at
 * the band's left end, 1 at its right end), so the row looks like a curved
 * wall; below it a mirrored copy fades out. */
static const char *expo_curve_vertex =
    R"(#version 100
attribute highp vec2 position;
attribute highp vec2 uvPosition;
varying highp vec2 uvpos;

void main() {
    gl_Position = vec4(position.xy, 0.0, 1.0);
    uvpos = uvPosition;
})";

static const char *expo_curve_fragment =
    R"(#version 100
varying highp vec2 uvpos;
uniform sampler2D smp;
uniform highp float center;   /* band centre, v */
uniform highp float half_h;   /* half band height, v */
uniform highp float band_x;   /* band left end, u */
uniform highp float band_w;   /* band width, u */
uniform highp float kt;       /* curve strength * progress */
uniform highp float refl;     /* reflection intensity * progress */
uniform highp float fade;     /* reflection length, in band heights */

void main() {
    highp float u  = clamp((uvpos.x - band_x) / band_w, 0.0, 1.0) * 2.0 - 1.0;
    highp float s  = 1.0 + kt * u * u;
    highp float dy = uvpos.y - center;
    if (abs(dy) <= half_h * s) {
        gl_FragColor = texture2D(smp, vec2(uvpos.x, center + dy / s));
        return;
    }

    highp vec4 col = texture2D(smp, uvpos);
    highp float d  = dy - half_h * s;            /* distance below the band */
    highp float len = fade * 2.0 * half_h * s;
    if ((dy > 0.0) && (refl > 0.0) && (d < len)) {
        highp vec4 r = texture2D(smp, vec2(uvpos.x, center + half_h - d / s));
        col = mix(col, r, refl * (1.0 - d / len));
    }

    gl_FragColor = col;
})";
