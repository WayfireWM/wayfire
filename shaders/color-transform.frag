// Adapted from wlroots, MIT license
// Copyright (c) 2023 The wlroots contributors

layout (constant_id = 0) const int TEXTURE_TRANSFORM = 0;

// Matches enum wlr_color_transfer_function
#define TEXTURE_TRANSFORM_IDENTITY (1 << 2)
#define TEXTURE_TRANSFORM_SRGB (1 << 0)
#define TEXTURE_TRANSFORM_ST2084_PQ  (1 << 1)
#define TEXTURE_TRANSFORM_GAMMA22 (1 << 3)
#define TEXTURE_TRANSFORM_BT1886 (1 << 4)

float srgb_channel_to_linear(float x) {
	return mix(x / 12.92,
		pow((x + 0.055) / 1.055, 2.4),
		x > 0.04045);
}

vec3 srgb_color_to_linear(vec3 color) {
	return vec3(
		srgb_channel_to_linear(color.r),
		srgb_channel_to_linear(color.g),
		srgb_channel_to_linear(color.b)
	);
}

vec3 pq_color_to_linear(vec3 color) {
	float inv_m1 = 1 / 0.1593017578125;
	float inv_m2 = 1 / 78.84375;
	float c1 = 0.8359375;
	float c2 = 18.8515625;
	float c3 = 18.6875;
	vec3 num = max(pow(color, vec3(inv_m2)) - c1, 0);
	vec3 denom = c2 - c3 * pow(color, vec3(inv_m2));
	return pow(num / denom, vec3(inv_m1));
}

vec3 bt1886_color_to_linear(vec3 color) {
	float Lmin = 0.01;
	float Lmax = 100.0;
	float lb = pow(Lmin, 1.0 / 2.4);
	float lw = pow(Lmax, 1.0 / 2.4);
	float a  = pow(lw - lb, 2.4);
	float b  = lb / (lw - lb);
	vec3 L = a * pow(color + vec3(b), vec3(2.4));
	return (L - Lmin) / (Lmax - Lmin);
}

vec3 transform_color(vec3 rgb) {
    if (TEXTURE_TRANSFORM != TEXTURE_TRANSFORM_IDENTITY) {
        rgb = max(rgb, vec3(0));
    }
    if (TEXTURE_TRANSFORM == TEXTURE_TRANSFORM_SRGB) {
        rgb = srgb_color_to_linear(rgb);
    } else if (TEXTURE_TRANSFORM == TEXTURE_TRANSFORM_ST2084_PQ) {
        rgb = pq_color_to_linear(rgb);
    } else if (TEXTURE_TRANSFORM == TEXTURE_TRANSFORM_GAMMA22) {
        rgb = pow(rgb, vec3(2.2));
    } else if (TEXTURE_TRANSFORM == TEXTURE_TRANSFORM_BT1886) {
        rgb = bt1886_color_to_linear(rgb);
    }

    return rgb;
}
