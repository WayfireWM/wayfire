// Texture rotation, matches wl_output_transform
layout (constant_id = 1) const int TEXTURE_ROTATION = 0;

/**
 * Apply texture rotation to the given UV coordinates, as well as scale them with a base and offset, which
 * allows limiting texturing to a subset of the texture.
 */
vec2 transform_texture_uv(vec2 uv, vec2 scale, vec2 off) {
    if (TEXTURE_ROTATION == 1) { // WL_OUTPUT_TRANSFORM_90
        return vec2(1.0 - uv.y, uv.x) * scale + off;
    } else if (TEXTURE_ROTATION == 2) { // WL_OUTPUT_TRANSFORM_180
        return vec2(1.0 - uv.x, 1.0 - uv.y) * scale + off;
    } else if (TEXTURE_ROTATION == 3) { // WL_OUTPUT_TRANSFORM_270
        return vec2(uv.y, 1.0 - uv.x) * scale + off;
    } else if (TEXTURE_ROTATION == 4) { // WL_OUTPUT_TRANSFORM_FLIPPED
        return vec2(1.0 - uv.x, uv.y) * scale + off;
    } else if (TEXTURE_ROTATION == 5) { // WL_OUTPUT_TRANSFORM_FLIPPED_90
        return vec2(1.0 - uv.y, 1.0 - uv.x) * scale + off;
    } else if (TEXTURE_ROTATION == 6) { // WL_OUTPUT_TRANSFORM_FLIPPED_180
        return vec2(uv.x, 1.0 - uv.y) * scale + off;
    } else if (TEXTURE_ROTATION == 7) { // WL_OUTPUT_TRANSFORM_FLIPPED_270
        return vec2(uv.y, uv.x) * scale + off;
    } else { // WL_OUTPUT_TRANSFORM_NORMAL
        return uv * scale + off;
    }
}
