#ifndef VIDEOOPENGLWIDGET_H
#define VIDEOOPENGLWIDGET_H

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QMutex>
#include "h264decoder.h"

class VideoOpenGLWidget:public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT
public:
    explicit VideoOpenGLWidget(QWidget *parent = nullptr);
    ~VideoOpenGLWidget() override;
public slots:
    // 接收解码线程发来的 YUVFrame 数据，将在主线程执行
    //这是槽函数，在主线程执行。将接收到的 YUVFrame 缓存起来，并调用 update() 触发 paintGL()。
    void updateFrame(const YUVFrame &frame);
protected:
    //初始化 OpenGL 函数，编译和链接 YUV 到 RGB 的着色器
    void initializeGL() override;
    //这是渲染函数，在主线程执行。它将缓存的 YUV 数据作为三个独立的纹理 (tex_y, tex_u, tex_v) 上传到 GPU，然后通过着色器计算最终的 RGB 像素并绘制。
    void paintGL() override;
    void resizeGL(int w, int h) override;

private:
    void initShaders();
    void createTextures(int width, int height); // 用于初次创建 Y, U, V 纹理

    QOpenGLShaderProgram *m_program = nullptr;
    QOpenGLTexture *m_textureY = nullptr;
    QOpenGLTexture *m_textureU = nullptr;
    QOpenGLTexture *m_textureV = nullptr;

    // 缓存最新一帧 YUV 数据
    YUVFrame m_currentFrame;
    QMutex m_mutex;
    int m_width = 0;
    int m_height = 0;
};

#endif // VIDEOOPENGLWIDGET_H
