shader_type canvas_item;
render_mode unshaded;

uniform vec2 layout = vec2(3.0, 2.0);
varying vec2 frame;
varying float flash;

void vertex() {
    float index = floor(INSTANCE_CUSTOM.r * 16.0 + 0.5);
    frame = vec2(mod(index, layout.x), floor(index / layout.x));
    flash = INSTANCE_CUSTOM.g;
}

void fragment() {
    vec4 pigment = texture(TEXTURE, (UV + frame) / layout);
    COLOR = vec4(mix(pigment.rgb, vec3(0.94, 0.90, 0.82), flash), pigment.a);
}
