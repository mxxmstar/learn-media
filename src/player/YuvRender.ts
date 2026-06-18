/**
 * WebGL2 YUV→RGB 渲染器
 *
 * 接收 I420 (YUV420p) 格式的帧数据，通过 GPU Fragment Shader
 * 转换为 RGB 并绘制到 Canvas 上。
 */

// ─── Vertex Shader ────────────────────────────────────
const VS_SRC = `#version 300 es
in vec2 a_pos;
in vec2 a_tex;
out vec2 v_tex;
void main() {
  gl_Position = vec4(a_pos, 0.0, 1.0);
  v_tex = a_tex;
}
`;

// ─── Fragment Shader（YUV420p → RGB）─────────────────
const FS_SRC = `#version 300 es
precision mediump float;
uniform sampler2D u_texY;
uniform sampler2D u_texU;
uniform sampler2D u_texV;
in  vec2 v_tex;
out vec4 fragColor;

void main() {
  float y = texture(u_texY, v_tex).r;
  float u = texture(u_texU, v_tex).r - 0.5;
  float v = texture(u_texV, v_tex).r - 0.5;

  // 标准 BT.601 YUV→RGB 转换
  float r = y + 1.13983 * v;
  float g = y - 0.39465 * u - 0.58060 * v;
  float b = y + 2.03211 * u;

  fragColor = vec4(r, g, b, 1.0);
}
`;

export interface YuvFrame {
  /** Y 平面数据 */
  y: Uint8Array;
  /** U 平面数据（宽高为 Y 的一半） */
  u: Uint8Array;
  /** V 平面数据（宽高为 Y 的一半） */
  v: Uint8Array;
  /** 原始视频宽度 */
  width: number;
  /** 原始视频高度 */
  height: number;
}

export class YuvRenderer {
  private gl: WebGL2RenderingContext;
  private program: WebGLProgram;

  private texY: WebGLTexture;
  private texU: WebGLTexture;
  private texV: WebGLTexture;

  private vao: WebGLVertexArrayObject;

  /** 当前帧的尺寸，用于判断是否需要重设纹理 */
  private frameWidth = 0;
  private frameHeight = 0;

  // ─── 构造函数 ──────────────────────────────────────
  constructor(canvas: HTMLCanvasElement) {
    const gl = canvas.getContext("webgl2", {
      // 后续要叠加 AI 控件，需要透明
      alpha: true,
      premultipliedAlpha: false,
    });
    if (!gl) throw new Error("浏览器不支持 WebGL2");

    this.gl = gl;

    // 编译 shader
    const vs = this.compileShader(gl.VERTEX_SHADER, VS_SRC);
    const fs = this.compileShader(gl.FRAGMENT_SHADER, FS_SRC);
    this.program = this.createProgram(vs, fs);

    // 全屏四边形
    this.vao = this.createFullQuad();

    // 创建纹理（初始尺寸 2x2，渲染时重新设置）
    this.texY = this.createDummyTexture();
    this.texU = this.createDummyTexture();
    this.texV = this.createDummyTexture();

    // 绑定 uniform 位置
    gl.useProgram(this.program);
    gl.uniform1i(gl.getUniformLocation(this.program, "u_texY"), 0);
    gl.uniform1i(gl.getUniformLocation(this.program, "u_texU"), 1);
    gl.uniform1i(gl.getUniformLocation(this.program, "u_texV"), 2);
  }

  // ─── 渲染一帧 ──────────────────────────────────────
  render(frame: YuvFrame): void {
    const { gl } = this;
    const { y, u, v, width, height } = frame;

    // 如果尺寸变了，重新分配纹理存储
    if (width !== this.frameWidth || height !== this.frameHeight) {
      this.frameWidth = width;
      this.frameHeight = height;
      gl.canvas.width = width;
      gl.canvas.height = height;

      // 设置纹理尺寸
      gl.activeTexture(gl.TEXTURE0);
      gl.bindTexture(gl.TEXTURE_2D, this.texY);
      gl.texStorage2D(gl.TEXTURE_2D, 1, gl.R8, width, height);

      gl.activeTexture(gl.TEXTURE1);
      gl.bindTexture(gl.TEXTURE_2D, this.texU);
      gl.texStorage2D(gl.TEXTURE_2D, 1, gl.R8, width / 2, height / 2);

      gl.activeTexture(gl.TEXTURE2);
      gl.bindTexture(gl.TEXTURE_2D, this.texV);
      gl.texStorage2D(gl.TEXTURE_2D, 1, gl.R8, width / 2, height / 2);
    }

    // 上传 Y/U/V 数据
    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, this.texY);
    gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, width, height, gl.RED, gl.UNSIGNED_BYTE, y);

    gl.activeTexture(gl.TEXTURE1);
    gl.bindTexture(gl.TEXTURE_2D, this.texU);
    gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, width / 2, height / 2, gl.RED, gl.UNSIGNED_BYTE, u);

    gl.activeTexture(gl.TEXTURE2);
    gl.bindTexture(gl.TEXTURE_2D, this.texV);
    gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, width / 2, height / 2, gl.RED, gl.UNSIGNED_BYTE, v);

    // 绘制
    gl.useProgram(this.program);
    gl.bindVertexArray(this.vao);
    gl.viewport(0, 0, width, height);
    gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
  }

  /** 生成测试用 YUV 彩条帧（I420 格式） */
  static createTestFrame(width: number, height: number): YuvFrame {
    const ySize = width * height;
    const uvSize = (width / 2) * (height / 2);
    const y = new Uint8Array(ySize);
    const u = new Uint8Array(uvSize);
    const v = new Uint8Array(uvSize);

    // 8 条垂直彩条
    const bars = [
      { y: 235, u: 128, v: 128 }, // 白
      { y: 210, u: 44, v: 140 },  // 黄
      { y: 145, u: 54, v: 34 },   // 青
      { y: 170, u: 34, v: 110 },  // 绿
      { y: 81, u: 184, v: 168 },  // 紫
      { y: 106, u: 222, v: 202 }, // 红
      { y: 41, u: 110, v: 240 },  // 蓝
      { y: 16, u: 128, v: 128 },  // 黑
    ];
    const barW = width / bars.length;

    for (let row = 0; row < height; row++) {
      for (let col = 0; col < width; col++) {
        const barIdx = Math.min(Math.floor(col / barW), bars.length - 1);
        y[row * width + col] = bars[barIdx].y;
      }
    }

    for (let row = 0; row < height / 2; row++) {
      for (let col = 0; col < width / 2; col++) {
        const barIdx = Math.min(Math.floor((col * 2) / barW), bars.length - 1);
        u[row * (width / 2) + col] = bars[barIdx].u;
        v[row * (width / 2) + col] = bars[barIdx].v;
      }
    }

    return { y, u, v, width, height };
  }

  // ─── 私有辅助方法 ──────────────────────────────────
  private compileShader(type: number, src: string): WebGLShader {
    const { gl } = this;
    const shader = gl.createShader(type)!;
    gl.shaderSource(shader, src);
    gl.compileShader(shader);
    if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS))
      throw new Error(`Shader 编译失败: ${gl.getShaderInfoLog(shader)}`);
    return shader;
  }

  private createProgram(vs: WebGLShader, fs: WebGLShader): WebGLProgram {
    const { gl } = this;
    const prog = gl.createProgram()!;
    gl.attachShader(prog, vs);
    gl.attachShader(prog, fs);
    gl.linkProgram(prog);
    if (!gl.getProgramParameter(prog, gl.LINK_STATUS))
      throw new Error(`Program 链接失败: ${gl.getProgramInfoLog(prog)}`);
    return prog;
  }

  private createDummyTexture(): WebGLTexture {
    const { gl } = this;
    const tex = gl.createTexture()!;
    gl.bindTexture(gl.TEXTURE_2D, tex);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    return tex;
  }

  private createFullQuad(): WebGLVertexArrayObject {
    const { gl } = this;

    // 两个三角形组成全屏四边形
    // 位置 (x,y) 和 纹理坐标 (s,t)
    const data = new Float32Array([
      -1, -1,  0, 0,
       1, -1,  1, 0,
      -1,  1,  0, 1,
       1,  1,  1, 1,
    ]);

    const vao = gl.createVertexArray()!;
    gl.bindVertexArray(vao);

    const buf = gl.createBuffer()!;
    gl.bindBuffer(gl.ARRAY_BUFFER, buf);
    gl.bufferData(gl.ARRAY_BUFFER, data, gl.STATIC_DRAW);

    // a_pos (位置)
    const aPos = gl.getAttribLocation(this.program, "a_pos");
    gl.enableVertexAttribArray(aPos);
    gl.vertexAttribPointer(aPos, 2, gl.FLOAT, false, 16, 0);

    // a_tex (纹理坐标)
    const aTex = gl.getAttribLocation(this.program, "a_tex");
    gl.enableVertexAttribArray(aTex);
    gl.vertexAttribPointer(aTex, 2, gl.FLOAT, false, 16, 8);

    gl.bindVertexArray(null);
    return vao;
  }

  destroy(): void {
    const { gl } = this;
    gl.deleteTexture(this.texY);
    gl.deleteTexture(this.texU);
    gl.deleteTexture(this.texV);
    gl.deleteProgram(this.program);
    gl.deleteVertexArray(this.vao);
  }
}