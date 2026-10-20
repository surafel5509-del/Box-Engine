#version 300 es
precision mediump float;

in vec2 vTexCoord;
in vec4 vColor;

uniform sampler2D uTexture;
uniform int uUseTexture;

out vec4 fragColor;

void main() {
    vec4 texel = (uUseTexture == 1) ? texture(uTexture, vTexCoord) : vec4(1.0);
    fragColor = texel * vColor;
}
