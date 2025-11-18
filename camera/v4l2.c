#include "v4l2.h"
#include <stdint.h>

/* 注意：需要安装libx264-dev库 */
#ifdef HAVE_X264
#include <x264.h>
#else
/* 如果没有x264库，提供简化版本或编译时链接 */
/* 这里我们提供一个接口，实际使用时需要安装x264库 */
typedef struct {
    int dummy;
} x264_t;
typedef struct {
    int dummy;
} x264_param_t;
typedef struct {
    int dummy;
} x264_picture_t;
typedef struct {
    int dummy;
} x264_nal_t;
#endif

int v4l2_fd = -1;
cam_buf_info buf_infos[FRAMEBUFFER_COUNT]; 
cam_fmt cam_fmts[10]; 
int frm_width, frm_height;

/* 摄像头初始化 */
int v4l2_dev_init(const char *device) {
    struct v4l2_capability cap = {0}; 

    /* 打开摄像头 */ 
    v4l2_fd = open(device, O_RDWR); 
    if (0 > v4l2_fd) { 
        fprintf(stderr, "open error: %s: %s\n", device, strerror(errno)); 
        return -1; 
    } 

    /* 查询设备功能 */ 
    ioctl(v4l2_fd, VIDIOC_QUERYCAP, &cap); 

    /* 判断是否是视频采集设备 */ 
    if (!(V4L2_CAP_VIDEO_CAPTURE & cap.capabilities)) { 
        fprintf(stderr, "Error: %s: No capture video device!\n", device); 
        close(v4l2_fd); 
        return -1; 
    } 

    return 0; 
}

/* 枚举像素格式 */
void v4l2_enum_formats(void) {
    struct v4l2_fmtdesc fmtdesc = {0}; 

    /* 枚举摄像头所支持的所有像素格式以及描述信息 */ 
    fmtdesc.index = 0; 
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; 
    while (0 == ioctl(v4l2_fd, VIDIOC_ENUM_FMT, &fmtdesc)) { 
        // 将枚举出来的格式以及描述信息存放在数组中 
        cam_fmts[fmtdesc.index].pixelformat = fmtdesc.pixelformat; 
        strcpy((char *)cam_fmts[fmtdesc.index].description,(const char *)fmtdesc.description);
        fmtdesc.index++; 
    } 
}

/* 打印像素格式 */
void v4l2_print_formats(void) {
    struct v4l2_frmsizeenum frmsize = {0}; 
    struct v4l2_frmivalenum frmival = {0}; 
    int i; 

    frmsize.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; 
    frmival.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; 
    for (i = 0; cam_fmts[i].pixelformat; i++) { 
        printf("format<0x%x>, description<%s>\n", cam_fmts[i].pixelformat, 
                cam_fmts[i].description); 

        /* 枚举出摄像头所支持的所有视频采集分辨率 */ 
        frmsize.index = 0; 
        frmsize.pixel_format = cam_fmts[i].pixelformat; 
        frmival.pixel_format = cam_fmts[i].pixelformat; 
        while (0 == ioctl(v4l2_fd, VIDIOC_ENUM_FRAMESIZES, &frmsize)) { 
            printf("size<%d*%d> ", 
                    frmsize.discrete.width, 
                    frmsize.discrete.height); 
            frmsize.index++;
            /* 获取摄像头视频采集帧率 */ 
            frmival.index = 0;
            frmival.width = frmsize.discrete.width; 
            frmival.height = frmsize.discrete.height; 
            while (0 == ioctl(v4l2_fd, VIDIOC_ENUM_FRAMEINTERVALS, &frmival)) { 
 
                printf("<%dfps>", frmival.discrete.denominator / 
                        frmival.discrete.numerator); 
                frmival.index++; 
            } 
            printf("\n"); 
        } 
        printf("\n"); 
    } 
}

/* 设置像素格式 */
int v4l2_set_format(int width, int height) {
    struct v4l2_format fmt = {0}; 
    struct v4l2_streamparm streamparm = {0}; 

    /* 设置帧格式 */ 
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = width;
    fmt.fmt.pix.height = height;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    
    if (0 > ioctl(v4l2_fd, VIDIOC_S_FMT, &fmt)) { 
        fprintf(stderr, "ioctl error: VIDIOC_S_FMT: %s\n", strerror(errno)); 
        return -1; 
    } 

    frm_width = fmt.fmt.pix.width;
    frm_height = fmt.fmt.pix.height;
    printf("视频帧大小<%d * %d>\n", frm_width, frm_height); 

    /* 获取streamparm */ 
    streamparm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; 
    ioctl(v4l2_fd, VIDIOC_G_PARM, &streamparm); 
    
    /* 判断是否支持帧率设置 */ 
    if (V4L2_CAP_TIMEPERFRAME & streamparm.parm.capture.capability) { 
        streamparm.parm.capture.timeperframe.numerator = 1; 
        streamparm.parm.capture.timeperframe.denominator = 30; // 30fps
        if (0 > ioctl(v4l2_fd, VIDIOC_S_PARM, &streamparm)) { 
            fprintf(stderr, "ioctl error: VIDIOC_S_PARM: %s\n", strerror(errno)); 
            return -1; 
        } 
        printf("帧率为: %d\n", streamparm.parm.capture.timeperframe.denominator / 
               streamparm.parm.capture.timeperframe.numerator);
    } 

    return 0; 
}

/* 初始化缓冲区 */
int v4l2_init_buffer(void) {
    struct v4l2_requestbuffers reqbuf = {0}; 
    struct v4l2_buffer buf = {0}; 

    /* 申请帧缓冲 */ 
    reqbuf.count = FRAMEBUFFER_COUNT;
    reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; 
    reqbuf.memory = V4L2_MEMORY_MMAP; 
    if (0 > ioctl(v4l2_fd, VIDIOC_REQBUFS, &reqbuf)) { 
        fprintf(stderr, "ioctl error: VIDIOC_REQBUFS: %s\n", strerror(errno)); 
        return -1; 
    } 

    /* 建立内存映射 */ 
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; 
    buf.memory = V4L2_MEMORY_MMAP; 
    for (buf.index = 0; buf.index < FRAMEBUFFER_COUNT; buf.index++) { 
        ioctl(v4l2_fd, VIDIOC_QUERYBUF, &buf);
        buf_infos[buf.index].length = buf.length;
        buf_infos[buf.index].start = mmap(NULL, buf.length,
                PROT_READ | PROT_WRITE, MAP_SHARED,
                v4l2_fd, buf.m.offset);
        if (MAP_FAILED == buf_infos[buf.index].start) { 
            perror("mmap error"); 
            return -1; 
        } 
    }
    
    /* 入队 */ 
    for (buf.index = 0; buf.index < FRAMEBUFFER_COUNT; buf.index++) { 
        if (0 > ioctl(v4l2_fd, VIDIOC_QBUF, &buf)) {
            fprintf(stderr, "ioctl error: VIDIOC_QBUF: %s\n", strerror(errno)); 
            return -1; 
        } 
    } 

    return 0; 
}

/* 启动视频流 */
int v4l2_stream_on(void) {
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE; 

    if (0 > ioctl(v4l2_fd, VIDIOC_STREAMON, &type)) { 
        fprintf(stderr, "ioctl error: VIDIOC_STREAMON: %s\n", strerror(errno)); 
        return -1; 
    } 

    return 0; 
}

/* 停止视频流 */
int v4l2_stream_off(void) {
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE; 

    if (0 > ioctl(v4l2_fd, VIDIOC_STREAMOFF, &type)) { 
        fprintf(stderr, "ioctl error: VIDIOC_STREAMOFF: %s\n", strerror(errno)); 
        return -1; 
    } 

    return 0; 
}

/* 从队列中取出一个缓冲区 */
int v4l2_dqbuf(struct v4l2_buffer *buf) {
    if (0 > ioctl(v4l2_fd, VIDIOC_DQBUF, buf)) {
        return -1;
    }
    return 0;
}

/* 将缓冲区放回队列 */
int v4l2_qbuf(struct v4l2_buffer *buf) {
    if (0 > ioctl(v4l2_fd, VIDIOC_QBUF, buf)) {
        return -1;
    }
    return 0;
}

/* 将YUYV数据转换为YUV420P格式 */
int yuyv_to_yuv420p(unsigned char *yuyv, unsigned char *yuv420p, int width, int height) {
    int i, j;
    unsigned char *y_plane = yuv420p;
    unsigned char *u_plane = yuv420p + width * height;
    unsigned char *v_plane = yuv420p + width * height + (width * height / 4);

    // Y 分量
    for (i = 0; i < height; i++) {
        for (j = 0; j < width; j++) {
            y_plane[i * width + j] = yuyv[i * width * 2 + j * 2];
        }
    }

    // U/V 分量（2x2 下采样平均）
    for (i = 0; i < height; i += 2) {
        for (j = 0; j < width; j += 2) {
            int idx00 = i * width * 2 + j * 2;       // 当前行左像素对
            int idx01 = idx00 + 4;                   // 当前行右像素对
            int idx10 = (i + 1) * width * 2 + j * 2; // 下一行左像素对
            int idx11 = idx10 + 4;                   // 下一行右像素对

            int U = (yuyv[idx00 + 1] + yuyv[idx01 + 1] + yuyv[idx10 + 1] + yuyv[idx11 + 1]) / 4;
            int V = (yuyv[idx00 + 3] + yuyv[idx01 + 3] + yuyv[idx10 + 3] + yuyv[idx11 + 3]) / 4;

            u_plane[(i / 2) * (width / 2) + (j / 2)] = (unsigned char)U;
            v_plane[(i / 2) * (width / 2) + (j / 2)] = (unsigned char)V;
        }
    }

    return 0;
}


#ifdef HAVE_X264
/* 初始化H264编码器 */
int h264_encoder_init(h264_encoder_t *encoder, int width, int height, int fps) {
    x264_param_t param;
    x264_t *x264_enc;
    
    x264_param_default_preset(&param, "veryfast", "zerolatency");
    param.i_width = width;
    param.i_height = height;
    param.i_fps_num = fps;
    param.i_fps_den = 1;
    param.i_keyint_max = fps;
    param.i_keyint_min = fps;
    param.i_threads = 1;
    param.b_annexb = 1;  /* 使用Annex-B格式（包含0x00000001起始码） */
    param.b_repeat_headers = 1; /* 每个IDR重复 SPS/PPS，便于新客户端加入 */
    
    x264_param_apply_profile(&param, "baseline");
    
    x264_enc = x264_encoder_open(&param);
    if (!x264_enc) {
        fprintf(stderr, "无法初始化x264编码器\n");
        return -1;
    }
    
    encoder->x264_encoder = x264_enc;
    encoder->width = width;
    encoder->height = height;
    encoder->fps = fps;
    encoder->yuv_buffer = malloc(width * height * 3 / 2);  /* YUV420P */
    encoder->initialized = 1;
    
    return 0;
}

/* 编码一帧YUYV数据为H264 */
int yuyv_to_h264(h264_encoder_t *encoder, unsigned char *yuyv, unsigned char *h264_data, int *h264_len) {
    x264_t *x264_enc = (x264_t *)encoder->x264_encoder;
    x264_picture_t pic_in, pic_out;
    x264_nal_t *nals;
    int i_nal, i_frame_size;

    if (!encoder->initialized)
    return -1;

    // 转换 YUYV -> YUV420P
    yuyv_to_yuv420p(yuyv, encoder->yuv_buffer, encoder->width, encoder->height);

    // 初始化图片
    memset(&pic_in, 0, sizeof(pic_in));
    pic_in.img.i_csp = X264_CSP_I420;
    pic_in.img.i_plane = 3;
    pic_in.img.i_stride[0] = encoder->width;
    pic_in.img.i_stride[1] = encoder->width / 2;
    pic_in.img.i_stride[2] = encoder->width / 2;
    pic_in.img.plane[0] = encoder->yuv_buffer;
    pic_in.img.plane[1] = encoder->yuv_buffer + encoder->width * encoder->height;
    pic_in.img.plane[2] = encoder->yuv_buffer + encoder->width * encoder->height * 5 / 4;
    static int64_t pts = 0;
    pic_in.i_pts = pts++;

    // 编码
    i_frame_size = x264_encoder_encode(x264_enc, &nals, &i_nal, &pic_in, &pic_out);
    if (i_frame_size < 0)
    return -1;

    // 拷贝 NAL 数据
    *h264_len = 0;
    for (int i = 0; i < i_nal; i++) {
        memcpy(h264_data + *h264_len, nals[i].p_payload, nals[i].i_payload);
        *h264_len += nals[i].i_payload;
    }
    return 0;
}


/* 清理H264编码器 */
void h264_encoder_cleanup(h264_encoder_t *encoder) {
    if (encoder->initialized) {
        x264_t *x264_enc = (x264_t *)encoder->x264_encoder;
        x264_encoder_close(x264_enc);
        free(encoder->yuv_buffer);
        encoder->initialized = 0;
    }
}
#else
/* 如果没有x264库，提供占位实现 */
int h264_encoder_init(h264_encoder_t *encoder, int width, int height, int fps) {
    fprintf(stderr, "警告: 未编译x264支持，请安装libx264-dev并定义HAVE_X264\n");
    encoder->width = width;
    encoder->height = height;
    encoder->fps = fps;
    encoder->yuv_buffer = malloc(width * height * 3 / 2);
    encoder->initialized = 0;  /* 标记为未初始化 */
    return -1;
}

int yuyv_to_h264(h264_encoder_t *encoder, unsigned char *yuyv, 
                 unsigned char *h264_data, int *h264_len) {
    (void)encoder; (void)yuyv; (void)h264_data; (void)h264_len;
    fprintf(stderr, "错误: x264未启用\n");
    return -1;
}

void h264_encoder_cleanup(h264_encoder_t *encoder) {
    if (encoder->yuv_buffer) {
        free(encoder->yuv_buffer);
        encoder->yuv_buffer = NULL;
    }
    encoder->initialized = 0;
}
#endif

/* 清理v4l2资源 */
void v4l2_cleanup(void) {
    if (v4l2_fd >= 0) {
        for (int i = 0; i < FRAMEBUFFER_COUNT; i++) {
            if (buf_infos[i].start != MAP_FAILED && buf_infos[i].start != NULL) {
                munmap(buf_infos[i].start, buf_infos[i].length);
            }
        }
        close(v4l2_fd);
        v4l2_fd = -1;
    }
}
