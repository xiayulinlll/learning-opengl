// ============================================================
//  sky.vert - 程序化天空的顶点着色器
//  把单位立方体「贴在相机上」并强制深度为最远，形成天空盒效果
// ============================================================
#version 330 core

layout (location = 0) in vec3 aPos;

uniform mat4 uProjection;
uniform mat4 uView;

out vec3 vWorldPos;

void main()
{
    // 去掉视图矩阵的平移分量，让天空盒始终跟随相机
    mat4 viewNoTranslation = mat4(mat3(uView));

    vec4 pos = uProjection * viewNoTranslation * vec4(aPos, 1.0);

    // z = w 表示深度正好在远裁剪面，配合 glDepthFunc(GL_LEQUAL) 可绘制在所有物体之后
    gl_Position = pos.xyww;

    vWorldPos = aPos;
}
