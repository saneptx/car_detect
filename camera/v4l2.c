#include "v4l2.h"


int v4l2_fd = -1;
cam_buf_info buf_infos[FRAMEBUFFER_COUNT]; 
cam_fmt cam_fmts[10]; 
int frm_width, frm_height;
tjhandle tj_decoder = NULL;

static int ensure_tj_decoder(void) {
    if (tj_decoder)
        return 0;
    tj_decoder = tjInitDecompress();
    if (!tj_decoder) {
        fprintf(stderr, "tjInitDecompress failed: %s\n", tjGetErrorStr());
        return -1;
    }
    return 0;
}

static void destroy_tj_decoder(void) {
    if (tj_decoder) {
        tjDestroy(tj_decoder);
        tj_decoder = NULL;
    }
}

static void yuv422p_to_yuv420p(
    const uint8_t *y422, const uint8_t *u422, const uint8_t *v422,
    int strideY422, int strideU422, int strideV422,

    uint8_t *y420, uint8_t *u420, uint8_t *v420,
    int strideY420, int strideU420, int strideV420,

    int width, int height
)
{
    /* 1. Y 平面：原样拷贝 */
    for (int y = 0; y < height; y++) {
        memcpy(
            y420 + y * strideY420,
            y422 + y * strideY422,
            width
        );
    }

    /* 2. U/V 平面：纵向 2 → 1 */
    int chromaWidth = width / 2;

    for (int y = 0; y < height / 2; y++) {
        const uint8_t *u_src0 = u422 + (2 * y)     * strideU422;
        const uint8_t *u_src1 = u422 + (2 * y + 1) * strideU422;
        uint8_t *u_dst = u420 + y * strideU420;

        const uint8_t *v_src0 = v422 + (2 * y)     * strideV422;
        const uint8_t *v_src1 = v422 + (2 * y + 1) * strideV422;
        uint8_t *v_dst = v420 + y * strideV420;

        for (int x = 0; x < chromaWidth; x++) {
            u_dst[x] = (u_src0[x] + u_src1[x] + 1) >> 1;
            v_dst[x] = (v_src0[x] + v_src1[x] + 1) >> 1;
        }
    }
}

/* 摄像头初始化 */
int v4l2_dev_init(const char *device) {
    struct v4l2_capability cap = {0}; 

    /* 打开摄像头 */ 
    v4l2_fd = open(device, O_RDWR); 
    if (0 > v4l2_fd) { 
        fprintf(stderr, "open error: %s: %s\n", device, strerror(errno)); 
        return -1; 
    } 
    int flags = fcntl(v4l2_fd, F_GETFL, 0);
    fcntl(v4l2_fd, F_SETFL, flags | O_NONBLOCK);
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

    /* 设置帧格式为MJPEG */ 
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = width;
    fmt.fmt.pix.height = height;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    fmt.fmt.pix.field = V4L2_FIELD_ANY;
    
    if (0 > ioctl(v4l2_fd, VIDIOC_S_FMT, &fmt)) { 
        fprintf(stderr, "ioctl error: VIDIOC_S_FMT (MJPEG): %s\n", strerror(errno)); 
        return -1; 
    } 

    if (fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_MJPEG) {
        fprintf(stderr, "Error: camera does not support MJPEG format (got 0x%x)\n",
                fmt.fmt.pix.pixelformat);
        return -1;
    }

    frm_width = fmt.fmt.pix.width;
    frm_height = fmt.fmt.pix.height;
    printf("视频帧大小<%d * %d>，像素格式<MJPEG>\n", frm_width, frm_height); 

    /* 获取streamparm */ 
    streamparm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; 
    ioctl(v4l2_fd, VIDIOC_G_PARM, &streamparm); 
    
    /* 判断是否支持帧率设置 */ 
    if (V4L2_CAP_TIMEPERFRAME & streamparm.parm.capture.capability) { 
        streamparm.parm.capture.timeperframe.numerator = 1; 
        streamparm.parm.capture.timeperframe.denominator = 60; // 60fps
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

int h264_encoder_init(h264_encoder_t *encoder, int width, int height, int fps) {
    x264_param_t param;
    x264_t *x264_enc;
    
    // 基本设置
    x264_param_default_preset(&param, "ultrafast", "zerolatency");
    param.i_width = width;
    param.i_height = height;
    param.i_fps_num = fps;//帧率分子
    param.i_fps_den = 1;//帧率分母
    param.i_csp = X264_CSP_I420; //图像格式YUV422P
    //GOP相关设置
    /*
        GOP（Group of Pictures，图像组）是指一组连续的帧序列，通常从一个I帧（关键帧）开始，后面跟随多个P帧和B帧。
    */
    param.i_keyint_max = fps; // GOP size
    param.i_keyint_min = fps/2;
    //码率控制
    /*
    码率 ≈ 分辨率 × 帧率 × 每像素平均比特数
    码率控制有三种模式
    CQP(恒定量化参数):
        特点:
            不控制码率，画质稳定，编码速度快。
        结果:
            画面复杂 → 
            码率暴涨画面简单 → 码率很低
        使用:
            param.rc.i_rc_method = X264_RC_CQP;
            param.rc.i_qp_constant = 26;
    ABR(平均码率，x264的"CBR"近似实现)
        特点:
            控制“平均码率”,支持 VBV（关键）,实时流媒体最常用
        使用:
            param.rc.i_rc_method = X264_RC_ABR;
            param.rc.i_bitrate = 2000; // kbps
    CRF(恒定质量):
        特点:
            主观化之稳定，码率不稳定
        结果:
            适合录像/存文件，不适合实时网络。
        使用:
            param.rc.i_rc_method = X264_RC_CRF;
            param.rc.f_rf_constant = 23;
    */
    param.rc.i_rc_method = X264_RC_ABR;
    param.rc.i_bitrate   = 2000;  // 平均码率
    /*
        VBV =Virtual Buffer Verifier
        用一个“虚拟缓冲区”限制瞬时码率，
        防止一瞬间把网络/播放器打爆。
    */
    param.rc.i_vbv_max_bitrate = 2000;//峰值码率，瞬时不允许超过的码率
    param.rc.i_vbv_buffer_size = 2000;//缓冲大小，允许“借用”的缓冲
    param.rc.i_qp_min = 20;//最高画面质量，qp值越小画质越清晰
    param.rc.i_qp_max = 45;//最低画面质量
    //其他设置
    // x264_param_apply_profile(&param, "high422"); // 或者 baseline
    x264_param_apply_profile(&param, "baseline"); // 或者 baseline
    param.b_annexb = 1;  /* 使用Annex-B格式（包含0x00000001起始码） */
    param.b_repeat_headers = 1; /* 每个IDR重复 SPS/PPS，便于新客户端加入 */
    param.i_threads = 1;//指定线程数（与切片数匹配）
    param.b_deterministic = 1;// 强制确定性输出
    param.i_bframe = 0;//禁用 B 帧
    
    x264_enc = x264_encoder_open(&param);
    if (!x264_enc) return -1;
    
    encoder->x264_encoder = x264_enc;
    encoder->width = width;
    encoder->height = height;
    encoder->fps = fps;
    encoder->initialized = 1;
    return 0;
}

int mjpeg_to_h264(h264_encoder_t *encoder, const unsigned char *mjpeg,
                  size_t mjpeg_size, unsigned char *h264_data,
                  size_t h264_buf_size, int *h264_len)
{
    if (!encoder || !encoder->initialized || !h264_data || !h264_len) return -1;
    if (ensure_tj_decoder() != 0)
        return -1;
    x264_t *x264_enc = (x264_t *)encoder->x264_encoder;
    x264_picture_t pic_in,pic_out;//创建输入帧和输出帧
    x264_nal_t *nals = NULL;
    int i_nal = 0;
    
    if (x264_picture_alloc(&pic_in, X264_CSP_I422,
                           encoder->width, encoder->height) < 0) {//为输入帧分配内存空间
        fprintf(stderr, "x264_picture_alloc failed\n");
        return -1;
    }

    x264_picture_t pic420;
    x264_picture_init(&pic420);

    pic420.img.i_csp   = X264_CSP_I420;
    pic420.img.i_plane = 3;

    x264_picture_alloc(&pic420, X264_CSP_I420, encoder->width, encoder->height);
    /*
        MJPE转YUV422P
        MJPEG = Motion JPEG = 一帧一帧独立的 JPEG 图片,它不是 H.264 那种帧间预测编码，没有 I/P/B 帧概念。
        一帧MJPEG的编码流程：RGB->颜色空间转换(YCbCr)->色度抽样（4:2:0 / 4:2:2 / 4:4:4）->8×8 分块->DCT（离散余弦变换）->量化->ZigZag->Huffman 编码
    */
    unsigned char *planes[3];
    int strides[3];

    planes[0] = pic_in.img.plane[0];
    planes[1] = pic_in.img.plane[1];
    planes[2] = pic_in.img.plane[2];
 
    strides[0] = pic_in.img.i_stride[0];
    strides[1] = pic_in.img.i_stride[1];
    strides[2] = pic_in.img.i_stride[2];

    int flags = TJFLAG_FASTUPSAMPLE | TJFLAG_FASTDCT;

    if (tjDecompressToYUVPlanes(
                        tj_decoder,
                        mjpeg,
                        (unsigned long)mjpeg_size,
                        planes,             // ← 第4个参数
                        encoder->width,     // ← 第5个参数 width
                        strides,            // ← 第6个参数 strides
                        encoder->height,    // ← 第7个参数 height
                        flags               // ← 第8个参数 flags
    ) != 0){
        fprintf(stderr, "tjDecompressToYUVPlanes failed: %s\n", tjGetErrorStr());
        x264_picture_clean(&pic_in);
        return -1;
    }

    yuv422p_to_yuv420p(
        pic_in.img.plane[0], pic_in.img.plane[1], pic_in.img.plane[2],
        pic_in.img.i_stride[0], pic_in.img.i_stride[1], pic_in.img.i_stride[2],

        pic420.img.plane[0], pic420.img.plane[1], pic420.img.plane[2],
        pic420.img.i_stride[0], pic420.img.i_stride[1], pic420.img.i_stride[2],

        encoder->width,
        encoder->height
    );
    // 3. 更新 PTS
    static int64_t pts = 0;
    pic_in.i_pts = pts++;


    // 4. 编码
    int i_frame_size = x264_encoder_encode(x264_enc, &nals, &i_nal, &pic420, &pic_out);
    
    if (i_frame_size < 0) {
        fprintf(stderr, "x264_encoder_encode failed\n");
        return -1;
    }

    // 5. 拷贝数据
    *h264_len = 0;
    for (int i = 0; i < i_nal; ++i) {
        if (*h264_len + nals[i].i_payload > h264_buf_size) break;
        memcpy(h264_data + *h264_len, nals[i].p_payload, nals[i].i_payload);
        *h264_len += nals[i].i_payload;
    }
    x264_picture_clean(&pic_in);
    x264_picture_clean(&pic420);
    return 0;
}


/* 清理H264编码器 */
void h264_encoder_cleanup(h264_encoder_t *encoder) {
    if (encoder->initialized) {
        x264_t *x264_enc = (x264_t *)encoder->x264_encoder;
        x264_encoder_close(x264_enc);
        // free(encoder->yuv_buffer);
        encoder->initialized = 0;
    }
    destroy_tj_decoder();
}

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
