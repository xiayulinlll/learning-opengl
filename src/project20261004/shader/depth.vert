#version 330 core

// ============================================================
//  阴影深度着色器：从光源视角渲染深度
// ============================================================

layout(location = 0) in vec3 aPos;

uniform mat4 uLightSpaceMatrix;
uniform mat4 uModel;

void main()
{
    gl_Position = uLightSpaceMatrix * uModel * vec4(aPos, 1.0);
}
