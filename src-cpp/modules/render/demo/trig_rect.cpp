// ============================================================
// OpenGL 3.3 Core Profile Demo
// Draws a green triangle (left) and an orange rectangle (right)
// Uses GLFW for window/context, GLAD for OpenGL extension loading
// ============================================================

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>

// ---------- Shader sources ----------

/// 顶点着色器
/// 该着色器将 CPU 传入的顶点位置传入到渲染管线的下一步，不做任何矩阵变换
static const char* vertexShaderSrc = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
void main() {
    gl_Position = vec4(aPos, 1.0);
}
)";

/// @brief 绿色三角形片段着色器
/// 该着色器将顶点位置转换为绿色三角形的颜色 RGB(0.2, 0.8, 0.2) ≈ 浅绿色
static const char* fragShaderGreen = R"(
#version 330 core
out vec4 FragColor;
void main() {
    FragColor = vec4(0.2, 0.8, 0.2, 1.0);
}
)";

/// @brief 橙色矩形片段着色器
/// 该着色器将顶点位置转换为橙色矩形的颜色 RGB(1.0, 0.6, 0.1) ≈ 橙色
static const char* fragShaderOrange = R"(
#version 330 core
out vec4 FragColor;
void main() {
    FragColor = vec4(1.0, 0.6, 0.1, 1.0);
}
)";

// ---------- Utility functions ----------

static GLuint compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::cerr << "ERROR: Shader compilation failed (" << (type == GL_VERTEX_SHADER ? "VS" : "FS") << ")\n" << log << std::endl;
    }
    return shader;
}

static GLuint createProgram(const char* vertSrc, const char* fragSrc) {
    GLuint vs = compileShader(GL_VERTEX_SHADER, vertSrc);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragSrc);

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);

    GLint success;
    glGetProgramiv(prog, GL_LINK_STATUS, &success);
    if (!success) {
        char log[512];
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        std::cerr << "ERROR: Program linking failed\n" << log << std::endl;
    }

    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

// ---------- main ----------

int main() {
    // 1. Init GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return -1;
    }

    // 2. Set OpenGL 3.3 Core Profile
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

    // 3. Create window (800x600)
    GLFWwindow* window = glfwCreateWindow(800, 600, "OpenGL Demo - Triangle & Rectangle", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);

    // 4. Load OpenGL function pointers via GLAD
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to initialize GLAD" << std::endl;
        return -1;
    }

    // 5. Set viewport & auto-resize callback
    int fbW, fbH; ///< 窗口缓冲区大小
    glfwGetFramebufferSize(window, &fbW, &fbH);
    std::cout << "Framebuffer size: " << fbW << "x" << fbH << std::endl;
    glViewport(0, 0, fbW, fbH); // 设置视口大小
    // 注册 framebuffer 尺寸变化回调，用户拖拽窗口边缘改变大小时，自动更新视口
    glfwSetFramebufferSizeCallback(window, [](GLFWwindow*, int w, int h) {
        std::cout << "viewport: " << w << "x" << h << std::endl;
        glViewport(0, 0, w, h);
    });

    // ========== Triangle (left, green) ==========
    float triVerts[] = {
        -0.6f, -0.5f, 0.0f,
        -0.1f, -0.5f, 0.0f,
        -0.35f, 0.5f, 0.0f
    };

    GLuint triVAO, triVBO;
    glGenVertexArrays(1, &triVAO);
    glGenBuffers(1, &triVBO);

    // 第 1 步：绑定 VAO（开始记录配置）
    glBindVertexArray(triVAO);
    // 第 2 步：绑定 VBO，把顶点数据上传到 GPU
    glBindBuffer(GL_ARRAY_BUFFER, triVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(triVerts), triVerts, GL_STATIC_DRAW);
    // 第 3 步：告诉 OpenGL 如何读取 VBO 中的数据
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // 第 4 步：解绑，避免后续误操作
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    GLuint triProg = createProgram(vertexShaderSrc, fragShaderGreen);

    // ========== Rectangle (right, orange) ==========
    float rectVerts[] = {
         0.1f, -0.5f, 0.0f,
         0.6f, -0.5f, 0.0f,
         0.6f,  0.5f, 0.0f,
         0.1f,  0.5f, 0.0f
    };
    unsigned int rectIdx[] = { 0, 1, 2, 0, 2, 3 };

    GLuint rectVAO, rectVBO, rectEBO;
    glGenVertexArrays(1, &rectVAO);
    glGenBuffers(1, &rectVBO);
    glGenBuffers(1, &rectEBO);

    glBindVertexArray(rectVAO);
    glBindBuffer(GL_ARRAY_BUFFER, rectVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(rectVerts), rectVerts, GL_STATIC_DRAW);

    // 使用 EBO 进行顶点复用。一个矩形->两个三角形
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(rectIdx), rectIdx, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glBindVertexArray(0);

    GLuint rectProg = createProgram(vertexShaderSrc, fragShaderOrange);

    // ========== Main loop ==========
    while (!glfwWindowShouldClose(window)) {
        // 设置清除颜色：深灰色
        glClearColor(0.15f, 0.15f, 0.18f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // Draw triangle
        glUseProgram(triProg);
        glBindVertexArray(triVAO);
        glDrawArrays(GL_TRIANGLES, 0, 3);   // 从顶点 0 开始画 3 个顶点

        // Draw rectangle
        glUseProgram(rectProg);
        glBindVertexArray(rectVAO);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);    // 用 EBO 的 6 个索引画 2 个三角形

        glfwSwapBuffers(window);    // 把后缓冲（back buffer）交换到前缓冲（front buffer）显示
        glfwPollEvents();   // 处理窗口事件（鼠标、键盘、关闭等）
    }

    // ========== Cleanup ==========
    glDeleteVertexArrays(1, &triVAO);
    glDeleteBuffers(1, &triVBO);
    glDeleteProgram(triProg);

    glDeleteVertexArrays(1, &rectVAO);
    glDeleteBuffers(1, &rectVBO);
    glDeleteBuffers(1, &rectEBO);
    glDeleteProgram(rectProg);

    glfwTerminate();
    return 0;
}
