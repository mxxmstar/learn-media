// ============================================================
// OpenGL 3.3 Core Profile - Texture Mapping Demo
// 功能：用 stb_image 加载 .jpg 图片，上传为 OpenGL 纹理，
//       然后绘制带纹理贴图的四边形。
// 用法：texture_demo [图片路径，默认 image.jpg]
// ============================================================

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <iostream>

// ============================================================
// 顶点着色器
// -----------------
// layout(location = 0) → aPos：顶点位置 (x, y)，裁剪坐标范围 [-1,1]
// layout(location = 1) → aUV ：纹理 UV 坐标 (u, v)，范围 [0,1]
//
// 输出 vUV 给片段着色器做 texture() 采样。
// gl_Position 是内置输出变量，表示最终裁剪坐标。
// ============================================================

static const char* vertSrc = R"(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
out vec2 vUV;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    vUV = aUV;
}
)";

// ============================================================
// 片段着色器
// -----------------
// uTex 是采样器 uniform，绑定到纹理单元 0。
// texture(uTex, vUV) 根据 UV 坐标从纹理中采样颜色。
// FragColor 是片段着色器的输出颜色。
// ============================================================

static const char* fragSrc = R"(
#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uTex;
void main() {
    FragColor = texture(uTex, vUV);
}
)";

// ============================================================
// 编译单个着色器（顶点或片段）
// type   : GL_VERTEX_SHADER 或 GL_FRAGMENT_SHADER
// source : GLSL 源码字符串
// 返回值：着色器对象句柄（非零表示成功）
// ============================================================

static GLuint compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::cerr << "ERROR: Shader compile (" << (type == GL_VERTEX_SHADER ? "VS" : "FS") << ")\n" << log << std::endl;
    }
    return shader;
}

// ============================================================
// 创建着色器程序（链接顶点 + 片段着色器）
// vs / fs : GLSL 源码
// 返回值  ：程序对象句柄
// 注意    ：链接后删除中间着色器对象以释放资源
// ============================================================

static GLuint createProgram(const char* vs, const char* fs) {
    GLuint v = compileShader(GL_VERTEX_SHADER, vs);
    GLuint f = compileShader(GL_FRAGMENT_SHADER, fs);
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    GLint ok;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        std::cerr << "ERROR: Program link\n" << log << std::endl;
    }
    glDeleteShader(v);
    glDeleteShader(f);
    return p;
}

// ============================================================
// main — 完整流程：
// 1. 初始化 GLFW（窗口 + OpenGL 上下文）
// 2. 用 GLAD 加载 OpenGL 函数指针
// 3. stb_image 加载图片到 CPU 内存
// 4. 创建 GPU 纹理并上传像素数据
// 5. 设置四边形顶点数据（位置 + UV 交错排列）
// 6. 编译着色器程序
// 7. 主循环：清屏 → 绑定纹理 → 绘制四边形 → 交换缓冲
// 8. 清理资源
// ============================================================

int main(int argc, char* argv[]) {
    // 允许命令行指定图片路径，未指定则默认为 image.jpg
    const char* imagePath = (argc > 1) ? argv[1] : "image.jpg";

    // ---- 1. 初始化 GLFW ----
    if (!glfwInit()) {
        std::cerr << "Failed to init GLFW" << std::endl;
        return -1;
    }
    // 请求 OpenGL 3.3 Core Profile（不包含已废弃的旧 API）
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    // macOS 需要 FORWARD_COMPAT，Windows 上无影响但保留兼容性
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

    // 创建窗口（宽 800、高 600），此时尚未创建 OpenGL 上下文
    GLFWwindow* window = glfwCreateWindow(800, 600, "Texture Mapping Demo", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create window" << std::endl;
        glfwTerminate();
        return -1;
    }
    // 将当前线程的 OpenGL 上下文设为该窗口的上下文
    glfwMakeContextCurrent(window);

    // ---- 2. 用 GLAD 加载 OpenGL 函数指针 ----
    // 必须在 MakeContextCurrent 之后调用
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to init GLAD" << std::endl;
        return -1;
    }

    // ---- 视口（Viewport）：将裁剪坐标映射到窗口像素坐标 ----
    int fbW, fbH;
    glfwGetFramebufferSize(window, &fbW, &fbH);
    glViewport(0, 0, fbW, fbH);
    // 注册窗口大小变化回调，动态更新视口
    glfwSetFramebufferSizeCallback(window, [](GLFWwindow*, int w, int h) {
        glViewport(0, 0, w, h);
    });

    // ---- 3. 用 stb_image 加载图片到 CPU 内存 ----
    //
    // stb_image 是单头文件图片加载库，本文件顶部已 define
    // STB_IMAGE_IMPLEMENTATION，使其仅在此编译单元生成实现代码。
    //
    // 注意：OpenGL 的纹理坐标系原点在左下角（Y 轴向上），
    // 而常见图片格式原点在左上角（Y 轴向下）。
    // stbi_set_flip_vertically_on_load(true) 在加载时翻转 Y 轴，
    // 使得图片在屏幕上显示为正立方向。
    int imgW, imgH, channels;
    unsigned char* imgData = nullptr;
    stbi_set_flip_vertically_on_load(true);
    imgData = stbi_load(imagePath, &imgW, &imgH, &channels, 0);
    if (!imgData) {
        std::cerr << "Failed to load image: " << imagePath << std::endl;
        std::cerr << "stb_image error: " << stbi_failure_reason() << std::endl;
        glfwTerminate();
        return -1;
    }
    std::cout << "Loaded " << imagePath << ": " << imgW << "x" << imgH
              << " (" << channels << " channels)" << std::endl;

    // ---- 4. 创建 GPU 纹理并上传像素数据 ----
    //
    // OpenGL 纹理管线：
    //   glGenTextures  → 分配纹理对象（返回句柄）
    //   glBindTexture  → 绑定到目标（GL_TEXTURE_2D），后续操作针对该纹理
    //   glTexParameter → 设置采样参数（环绕方式、过滤方式）
    //   glTexImage2D   → 从 CPU 内存上传像素数据到 GPU
    //   glBindTexture(..., 0) → 解绑，避免误操作
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    // 纹理环绕方式（当 UV 坐标超出 [0,1] 时的行为）：
    //   GL_CLAMP_TO_EDGE — 边缘像素拉伸
    //   GL_REPEAT        — 重复平铺（默认）
    //   GL_MIRRORED_REPEAT — 镜像重复
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // 纹理过滤方式（像素放大/缩小时的插值算法）：
    //   GL_NEAREST — 最近邻（块状效果，性能好）
    //   GL_LINEAR  — 双线性插值（平滑效果）
    //   如使用 mipmap，可将 GL_TEXTURE_MIN_FILTER 设为
    //   GL_LINEAR_MIPMAP_LINEAR（三线性过滤）。
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // 根据图片通道数选择格式
    GLenum format = (channels == 4) ? GL_RGBA : GL_RGB;

    // 设置像素行对齐方式：
    //   默认 GL_UNPACK_ALIGNMENT = 4，要求每行像素数据按 4 字节对齐。
    //   RGB 格式每像素 3 字节，若宽度为 1170，则行大小 = 1170×3 = 3510 字节，
    //   3510 % 4 = 2 → 不满足 4 字节对齐。设置 ALIGNMENT = 1 即可解决。
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, format, imgW, imgH, 0, format, GL_UNSIGNED_BYTE, imgData);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4); // 恢复默认值

    glBindTexture(GL_TEXTURE_2D, 0);

    // 注意：imgData 在此不会立即释放。
    // 部分 Intel 显卡驱动在 glTexImage2D 后仍异步引用像素缓冲，
    // 过早释放会导致驱动崩溃。我们将释放推迟到程序退出前（见 cleanup 段）。

    // ---- 5. 设置四边形顶点数据 ----
    //
    // 交错布局（interleaved）：position 和 UV 交替存储在同一个 VBO 中
    // 每个顶点占用 4 个 float：[pos.x, pos.y, uv.u, uv.v]
    //
    // 顶点顺序（逆时针）：
    //   3 ── 2
    //   │    │
    //   0 ── 1
    //
    // 索引数组通过两个三角形组成一个四边形：
    //   三角形1：0 → 1 → 2
    //   三角形2：0 → 2 → 3

    float verts[] = {
        // x      y        u     v
        -0.8f, -0.8f,    0.0f, 0.0f,  // 0: 左下
         0.8f, -0.8f,    1.0f, 0.0f,  // 1: 右下
         0.8f,  0.8f,    1.0f, 1.0f,  // 2: 右上
        -0.8f,  0.8f,    0.0f, 1.0f,  // 3: 左上
    };
    unsigned int idx[] = { 0, 1, 2, 0, 2, 3 };

    // VAO（Vertex Array Object）  ：记录顶点属性与 VBO 的关联关系
    // VBO（Vertex Buffer Object） ：存储顶点数据的 GPU 缓冲
    // EBO（Element Buffer Object）：存储索引数据的 GPU 缓冲
    GLuint VAO, VBO, EBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);

    // 绑定 VAO → 后续所有顶点属性设置都记录到此 VAO 中
    glBindVertexArray(VAO);

    // 上传顶点数据到 VBO
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);

    // 上传索引数据到 EBO
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(idx), idx, GL_STATIC_DRAW);

    // 设置顶点属性指针，告诉 OpenGL 如何解释 VBO 中的数据：
    //
    // 属性 0 (aPos)：vec2，位置
    //   步长 = 4 个 float（跳过 UV 部分）
    //   偏移 = 0（从顶点起始位置开始）
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // 属性 1 (aUV)：vec2，纹理坐标
    //   步长 = 4 个 float
    //   偏移 = 2 个 float（跳过 position 部分）
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // 解绑 VAO（防止外部意外修改）
    glBindVertexArray(0);

    // ---- 6. 编译着色器程序 ----
    GLuint program = createProgram(vertSrc, fragSrc);
    glUseProgram(program);

    // 获取 uniform "uTex" 的位置并设置纹理单元：
    //   uniform sampler2D uTex 的值是一个整数，表示纹理单元编号。
    //   这里设为 0，对应 GL_TEXTURE0。渲染时需先激活对应单元再绑定纹理。
    GLint texLoc = glGetUniformLocation(program, "uTex");
    glUniform1i(texLoc, 0);

    // ---- 7. 主循环 ----
    //
    // 每帧流程：
    //   清屏 → 激活纹理单元 0 → 绑定纹理 → 绑定 VAO → 绘制索引网格 → 交换缓冲
    while (!glfwWindowShouldClose(window)) {
        // 用深灰色清屏 (R=0.2, G=0.2, B=0.2, A=1.0)
        glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // 激活纹理单元 0，并将纹理绑定到 GL_TEXTURE_2D
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);

        // 绑定 VAO 并执行索引绘制：
        //   6 个索引（2 个三角形），每个顶点数据来自已设置的 VBO
        glBindVertexArray(VAO);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

        // 交换前后缓冲（显示当前帧），并轮询系统事件
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // ---- 8. 清理资源 ----
    //
    // 按创建顺序的逆序释放，以尊重可能的依赖关系。
    // 注意：stbi_image_free 放在 glDeleteTextures 之后，
    // 确保 GPU 驱动不再引用 CPU 像素缓冲再释放。
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteBuffers(1, &EBO);
    glDeleteTextures(1, &texture);
    glDeleteProgram(program);
    stbi_image_free(imgData);

    glfwTerminate();
    return 0;
}
