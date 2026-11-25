shader_type canvas_item;
render_mode unshaded;
uniform sampler2D light_chart : filter_linear, repeat_disable;
uniform float tide;
uniform bool accessible;
float grain(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}
float mist(vec2 p) {
    vec2 i = floor(p);
    vec2 f = smoothstep(vec2(0.0), vec2(1.0), fract(p));
    return mix(mix(grain(i), grain(i + vec2(1,0)), f.x),
               mix(grain(i + vec2(0,1)), grain(i + vec2(1,1)), f.x), f.y);
}
void fragment() {
    float light = texture(light_chart, UV).r;
    float fog = mist(UV * vec2(14, 9) + vec2(tide * 0.07, tide * 0.025));
    vec3 pigment = mix(vec3(0.106,0.122,0.227), vec3(0.373,0.498,0.525), fog);
    COLOR = vec4(pigment, (1.0 - light) * (accessible ? 0.36 : 0.4 + 0.3 * fog));
}
