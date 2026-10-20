#version 300 es
precision mediump float;

in vec2 vTexCoord;

uniform sampler2D uFontAtlas;
uniform vec4 uTextColor;
uniform float uSmoothing;

out vec4 fragColor;

void main() {
    float distanceValue = texture(uFontAtlas, vTexCoord).r;
    float alpha = smoothstep(0.5 - uSmoothing, 0.5 + uSmoothing, distanceValue);
    fragColor = vec4(uTextColor.rgb, uTextColor.a * alpha);
}
