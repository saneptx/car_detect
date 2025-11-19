#ifndef _V4L2_H_
#define _V4L2_H_
#include <stdio.h> 
#include <stdlib.h> 
#include <sys/types.h> 
#include <sys/stat.h> 
#include <fcntl.h> 
#include <unistd.h> 
#include <sys/ioctl.h> 
#include <string.h> 
#include <errno.h> 
#include <sys/mman.h> 
#include <linux/videodev2.h> 
#include <linux/fb.h> 
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FRAMEBUFFER_COUNT   3               //帧缓冲数量 

/*** 摄像头像素格式及其描述信息 ***/ 
typedef struct camera_format { 
    unsigned char description[32];  //字符串描述信息 
    unsigned int pixelformat;       //像素格式 
} cam_fmt; 
 
/*** 描述一个帧缓冲的信息 ***/ 
typedef struct cam_buf_info { 
    void *start;      //帧缓冲起始地址 
    unsigned long length;       //帧缓冲长度 
} cam_buf_info; 

/* H264编码器上下文 */
typedef struct h264_encoder {
    void *x264_encoder;  // x264编码器指针（实际是x264_t*，但避免暴露x264头文件）
    int width;
    int height;
    int fps;
    unsigned char *yuv_buffer;
    int initialized;
} h264_encoder_t;

extern int v4l2_fd;
extern cam_buf_info buf_infos[FRAMEBUFFER_COUNT]; 
extern cam_fmt cam_fmts[10]; 
extern int frm_width, frm_height;

/* 摄像头初始化 */
int v4l2_dev_init(const char *device);

/* 枚举像素格式 */
void v4l2_enum_formats(void);

/* 打印像素格式 */
void v4l2_print_formats(void);

/* 设置像素格式 */
int v4l2_set_format(int width, int height);

/* 初始化缓冲区 */
int v4l2_init_buffer(void);

/* 启动视频流 */
int v4l2_stream_on(void);

/* 停止视频流 */
int v4l2_stream_off(void);

/* 从队列中取出一个缓冲区 */
int v4l2_dqbuf(struct v4l2_buffer *buf);

/* 将缓冲区放回队列 */
int v4l2_qbuf(struct v4l2_buffer *buf);

/* 初始化H264编码器 */
int h264_encoder_init(h264_encoder_t *encoder, int width, int height, int fps);

/* 将MJPEG数据转换为YUV420P格式（用于编码） */
int mjpeg_to_yuv420p(const unsigned char *mjpeg, size_t mjpeg_size,
                     unsigned char *yuv420p, int width, int height);

/* 编码一帧MJPEG数据为H264 */
int mjpeg_to_h264(h264_encoder_t *encoder, const unsigned char *mjpeg,
                  size_t mjpeg_size, unsigned char *h264_data,
                  size_t h264_buf_size, int *h264_len);

/* 清理H264编码器 */
void h264_encoder_cleanup(h264_encoder_t *encoder);

/* 清理v4l2资源 */
void v4l2_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif
