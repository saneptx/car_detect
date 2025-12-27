#include "videoopenglwidget.h"
#include <QDebug>
#include <QElapsedTimer>

VideoOpenGLWidget::VideoOpenGLWidget(QWidget *parent)
    : QOpenGLWidget(parent),
      m_program(nullptr)
{
    // 设置 Widget 为黑色背景
    setStyleSheet("background-color: black;");
}

VideoOpenGLWidget::~VideoOpenGLWidget()
{
    // 确保在主线程中释放 OpenGL 资源
    makeCurrent();
    delete m_textureY;
    delete m_textureU;
    delete m_textureV;
    delete m_program;
    doneCurrent();
}

// 顶点着色器 (GLSL)
// 告诉 GPU 在哪里绘制矩形以及纹理坐标
const char *vertexShader =
    "attribute vec4 vertexIn;\n"     // 输入：顶点坐标 (x, y, z, w)
    "attribute vec2 textureIn;\n"    // 输入：纹理坐标 (s, t)
    "varying vec2 textureOut;\n"     // 输出给片段着色器
    "void main(void)\n"
    "{\n"
    "    gl_Position = vertexIn;\n"  // 直接使用输入的顶点坐标
    "    textureOut = textureIn;\n"
    "}\n";

// 片段着色器 (GLSL)
// YUV -> RGB 转换公式 (使用常用的 BT.601 标准)
const char *fragmentShader =
    "varying vec2 textureOut;\n"     // 接收来自顶点着色器的纹理坐标
    "uniform sampler2D tex_y;\n"     // Y 分量纹理单元 0
    "uniform sampler2D tex_u;\n"     // U 分量纹理单元 1
    "uniform sampler2D tex_v;\n"     // V 分量纹理单元 2
    "void main(void)\n"
    "{\n"
    "    // 1. 采样 YUV 三个分量，它们的值范围是 [0, 1]\n"
    "    float y = texture2D(tex_y, textureOut).r;\n"
    "    float u = texture2D(tex_u, textureOut).r - 0.5;\n" // U/V 分量需要偏移 0.5 (即 128/255)
    "    float v = texture2D(tex_v, textureOut).r - 0.5;\n"

    "    // 2. YUV 转 RGB (标准的 BT.601 转换矩阵)\n"
    "    float r = y + 1.403 * v;\n"
    "    float g = y - 0.344 * u - 0.714 * v;\n"
    "    float b = y + 1.770 * u;\n"
    "    // 3. 设置最终颜色\n"
    "    gl_FragColor = vec4(r, g, b, 1.0);\n"
    "}\n";

// 顶点数据：一个全屏矩形，以及对应的纹理坐标
// 顶点坐标: {x, y, z, 1.0}，纹理坐标: {s, t}
static const GLfloat g_vertices[] = {
    // 顶点位置        // 纹理坐标 (T 轴翻转，将 0.0 和 1.0 对调)
    // 左下角：
    -1.0f, -1.0f,  0.0f, 1.0f, // T=1.0 (纹理顶部)
    // 右下角：
     1.0f, -1.0f,  1.0f, 1.0f, // T=1.0 (纹理顶部)
    // 左上角：
    -1.0f,  1.0f,  0.0f, 0.0f, // T=0.0 (纹理底部)
    // 右上角：
     1.0f,  1.0f,  1.0f, 0.0f  // T=0.0 (纹理底部)
};

void VideoOpenGLWidget::initShaders()
{
    m_program = new QOpenGLShaderProgram(this);
    // 编译链接着色器
    if (!m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShader) ||
        !m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentShader) ||
        !m_program->link())
    {
        qWarning() << "VideoOpenGLWidget: Shader compilation/linking failed!";
        // 建议在这里处理错误，例如 delete m_program;
    }

    m_program->bind();
    // 设置纹理单元索引，告诉 GLSL 哪个 uniform 对应哪个纹理单元
    m_program->setUniformValue("tex_y", 0); // GL_TEXTURE0
    m_program->setUniformValue("tex_u", 1); // GL_TEXTURE1
    m_program->setUniformValue("tex_v", 2); // GL_TEXTURE2
    m_program->release();
}

void VideoOpenGLWidget::createTextures(int width, int height)
{
    // 清理旧纹理
    delete m_textureY;
    delete m_textureU;
    delete m_textureV;

    // Y 分量
    m_textureY = new QOpenGLTexture(QOpenGLTexture::Target2D);
    m_textureY->create();
    m_textureY->bind();
    m_textureY->setFormat(QOpenGLTexture::LuminanceFormat); // 修改为 LuminanceFormat
    m_textureY->setSize(width, height);
    m_textureY->allocateStorage();
    m_textureY->setWrapMode(QOpenGLTexture::ClampToEdge); // 防止边缘鬼影


    // U 分量
    m_textureU = new QOpenGLTexture(QOpenGLTexture::Target2D);
    m_textureU->create();
    m_textureU->bind();
    m_textureU->setFormat(QOpenGLTexture::LuminanceFormat); // 修改为 LuminanceFormat
    m_textureU->setSize(width / 2, height / 2);
    m_textureU->allocateStorage();
    m_textureU->setWrapMode(QOpenGLTexture::ClampToEdge);

    // V 分量
    m_textureV = new QOpenGLTexture(QOpenGLTexture::Target2D);
    m_textureV->create();
    m_textureV->bind();
    m_textureV->setFormat(QOpenGLTexture::LuminanceFormat); // 修改为 LuminanceFormat
    m_textureV->setSize(width / 2, height / 2);
    m_textureV->allocateStorage();
    m_textureV->setWrapMode(QOpenGLTexture::ClampToEdge);

    // 设置纹理过滤模式（保证缩放不失真）
    m_textureY->setMinificationFilter(QOpenGLTexture::Linear);
    m_textureY->setMagnificationFilter(QOpenGLTexture::Linear);
    m_textureU->setMinificationFilter(QOpenGLTexture::Linear);
    m_textureU->setMagnificationFilter(QOpenGLTexture::Linear);
    m_textureV->setMinificationFilter(QOpenGLTexture::Linear);
    m_textureV->setMagnificationFilter(QOpenGLTexture::Linear);
}

void VideoOpenGLWidget::initializeGL()
{
    // 必须调用，否则 QOpenGLFunctions 无法使用
    initializeOpenGLFunctions();

    // 初始化着色器
    initShaders();

    // 清空颜色设置为黑色
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    // 创建一个初始尺寸的纹理（可以避免 paintGL 时的空指针）
    createTextures(16, 16);
}

void VideoOpenGLWidget::resizeGL(int w, int h)
{
    // 视口大小变化
    glViewport(0, 0, w, h);
}

void VideoOpenGLWidget::paintGL()
{
    QMutexLocker locker(&m_mutex);

    // 检查是否有数据，如果 YUVFrame 为空，则只清屏
    if (m_currentFrame.data.isNull() || !m_program) {
        glClear(GL_COLOR_BUFFER_BIT);
        return;
    }

    // 1. 检查尺寸是否变化，如果变化需要重新创建纹理
    if (m_currentFrame.width != m_width || m_currentFrame.height != m_height) {
        createTextures(m_currentFrame.width, m_currentFrame.height);
        m_width = m_currentFrame.width;
        m_height = m_currentFrame.height;
    }

    // 2. 绑定着色器程序
    m_program->bind();
    // 设置像素存储方式为 1 字节对齐 (非常重要！)
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    const int w = m_currentFrame.width;
    const int h = m_currentFrame.height;

    // 3. 上传纹理数据 (这是性能关键步骤: CPU -> GPU)
    const uint8_t *y_ptr = (const uint8_t*)m_currentFrame.data->constData();

    // Y分量大小：width×height
    int ySize = w * h;
    // U/V分量大小
    int uvW    = w / 2;
    int uvH    = h / 2;
    int uvSize = uvW * uvH;

    // U分量指针：Y之后偏移ySize
    const uint8_t *u_ptr = y_ptr + ySize;
    // V分量指针：U之后偏移uvSize
    const uint8_t *v_ptr = u_ptr + uvSize;

    // 上传Y纹理（不变）
    glActiveTexture(GL_TEXTURE0);
    m_textureY->bind();
    m_textureY->setData(QOpenGLTexture::Luminance, QOpenGLTexture::UInt8, y_ptr);

    // 上传U纹理
    glActiveTexture(GL_TEXTURE1);
    m_textureU->bind();
    m_textureU->setData(QOpenGLTexture::Luminance, QOpenGLTexture::UInt8, u_ptr);

    // 上传V纹理
    glActiveTexture(GL_TEXTURE2);
    m_textureV->bind();
    m_textureV->setData(QOpenGLTexture::Luminance, QOpenGLTexture::UInt8, v_ptr);

    // 4. 绘制顶点
    // 获取着色器中的 attribute 位置
    int vertexLoc = m_program->attributeLocation("vertexIn");
    int textureLoc = m_program->attributeLocation("textureIn");

    m_program->enableAttributeArray(vertexLoc);
    m_program->enableAttributeArray(textureLoc);

    // 设置顶点数据，前两个是位置，后两个是纹理坐标
    // 步长是 4 * sizeof(GLfloat)
    m_program->setAttributeArray(vertexLoc, GL_FLOAT, g_vertices, 2, 4 * sizeof(GLfloat));
    m_program->setAttributeArray(textureLoc, GL_FLOAT, g_vertices + 2, 2, 4 * sizeof(GLfloat));

    // 绘制三角形扇形 (4 个顶点绘制一个矩形)
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    // 5. 释放资源
    m_program->disableAttributeArray(vertexLoc);
    m_program->disableAttributeArray(textureLoc);
    m_program->release();
}

void VideoOpenGLWidget::updateFrame(const YUVFrame &frame)
{
    // 这个槽函数在主线程（UI 线程）执行

    // 1. 锁定互斥锁，确保在 paintGL 读取数据时，这里不会写入
    QMutexLocker locker(&m_mutex);

    // 2. 缓存新数据
    m_currentFrame = frame;

    // 3. 触发重绘：QOpenGLWidget::update() 确保在主线程中安全地调用 paintGL()
    update();
}
