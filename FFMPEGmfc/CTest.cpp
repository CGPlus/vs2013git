#include "stdafx.h"
#include "CTest.h"

SDL_mutex *text_mutex;
SDL_Window *win = NULL;
SDL_Renderer *renderer;
SDL_Texture *texture;


static int screen_left = SDL_WINDOWPOS_CENTERED;
static int screen_top = SDL_WINDOWPOS_CENTERED;
static int screen_width = 0;
static int screen_height = 0;
static int resize = 1;


///* Since we only have one decoding thread, the Big Struct



//声明

//void packet_queue_init(PacketQueue *q);
//int packet_queue_put(PacketQueue *q, AVPacket *pkt);
//int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block);
//double get_audio_clock(VideoState *is);
//int audio_decode_frame(VideoState *is, uint8_t *audio_buf, int buf_size, double *pts_ptr);
//void audio_callback(void *userdata, Uint8 *stream, int len);
//static Uint32 sdl_refresh_timer_cb(Uint32 interval, void *opaque);
//static void schedule_refresh(VideoState *is, int delay);
//void video_display(VideoState *is);
//void video_refresh_timer(void *userdata);
//int queue_picture(VideoState *is, AVFrame *pFrame, double pts);
//double synchronize_video(VideoState *is, AVFrame *src_frame, double pts);
//int decode_video_thread(void *arg);
//int stream_component_open(VideoState *is, int stream_index);
//int demux_thread(void *arg);
//int WinMain1();
//size_t my_strlcpy(char *des, const char *src, size_t length);


// 初始化队列
void CTest::packet_queue_init(PacketQueue *q) {
	memset(q, 0, sizeof(PacketQueue));
	q->mutex = SDL_CreateMutex();
	q->cond = SDL_CreateCond();
}

//向队列中加入数据包
int CTest::packet_queue_put(PacketQueue *q, AVPacket *pkt) {

	AVPacketList *pkt1;
	if (av_packet_make_refcounted(pkt) < 0) {
		return -1;
	}
	pkt1 = (AVPacketList*)av_malloc(sizeof(AVPacketList));
	if (!pkt1)
		return -1;
	pkt1->pkt = *pkt;
	pkt1->next = NULL;

	SDL_LockMutex(q->mutex);

	if (!q->last_pkt)
		q->first_pkt = pkt1;
	else
		q->last_pkt->next = pkt1;
	q->last_pkt = pkt1;
	q->nb_packets++;
	q->size += pkt1->pkt.size;
	SDL_CondSignal(q->cond);

	SDL_UnlockMutex(q->mutex);
	return 0;
}

//数据包出队列
int CTest::packet_queue_get(PacketQueue *q, AVPacket *pkt, int block) {
	AVPacketList *pkt1;
	int ret;

	SDL_LockMutex(q->mutex);

	for (;;) {

		if (global_video_state->quit) {
			ret = -1;
			break;
		}

		pkt1 = q->first_pkt;
		if (pkt1) {
			q->first_pkt = pkt1->next;
			if (!q->first_pkt)
				q->last_pkt = NULL;
			q->nb_packets--;
			q->size -= pkt1->pkt.size;
			*pkt = pkt1->pkt;
			av_free(pkt1);
			ret = 1;
			break;
		}
		else if (!block) {
			ret = 0;
			break;
		}
		else {
			SDL_CondWait(q->cond, q->mutex);
		}
	}
	SDL_UnlockMutex(q->mutex);
	return ret;
}

//
double CTest::get_audio_clock(VideoState *is) {
	double pts;
	int hw_buf_size, bytes_per_sec, n;

	//上一步获取的PTS
	pts = is->audio_clock;
	// 音频缓冲区还没有播放的数据
	hw_buf_size = is->audio_buf_size - is->audio_buf_index;
	// 每秒钟音频播放的字节数
	bytes_per_sec = 0;
	n = is->audio_ctx->channels * 2;
	if (is->audio_st) {
		bytes_per_sec = is->audio_ctx->sample_rate * n;
	}
	if (bytes_per_sec) {
		pts -= (double)hw_buf_size / bytes_per_sec;
	}
	return pts;
}

// 音频帧解码
int CTest::audio_decode_frame(VideoState *is, uint8_t *audio_buf, int buf_size, double *pts_ptr)
{

	int len1, data_size = 0;
	AVPacket *pkt = &is->audio_pkt;
	double pts;
	int n;


	for (;;) {
		while (is->audio_pkt_size > 0) {
			avcodec_send_packet(is->audio_ctx, pkt);
			while (avcodec_receive_frame(is->audio_ctx, &is->audio_frame) == 0) {
				len1 = is->audio_frame.pkt_size;

				if (len1 < 0) {
					/* if error, skip frame */
					is->audio_pkt_size = 0;
					break;
				}

				data_size = 2 * is->audio_frame.nb_samples * 2;
				assert(data_size <= buf_size);

				swr_convert(is->audio_swr_ctx,
					&audio_buf,
					MAX_AUDIO_FRAME_SIZE * 3 / 2,
					(const uint8_t **)is->audio_frame.data,
					is->audio_frame.nb_samples);

			}
			is->audio_pkt_data += len1;
			is->audio_pkt_size -= len1;
			if (data_size <= 0) {
				/* No data yet, get more frames */
				continue;
			}
			pts = is->audio_clock;
			*pts_ptr = pts;
			n = 2 * is->audio_ctx->channels;
			is->audio_clock += (double)data_size /
				(double)(n * is->audio_ctx->sample_rate);
			/* We have data, return it and come back for more later */
			return data_size;
		}
		if (pkt->data)
			av_packet_unref(pkt);

		if (is->quit) {
			return -1;
		}
		/* next packet */
		if (packet_queue_get(&is->audioq, pkt, 1) < 0) {
			return -1;
		}
		is->audio_pkt_data = pkt->data;
		is->audio_pkt_size = pkt->size;
		/* if update, update the audio clock w/pts */
		if (pkt->pts != AV_NOPTS_VALUE) {
			is->audio_clock = av_q2d(is->audio_st->time_base) * pkt->pts;
		}
	}
}

//// 音频设备回调
void CTest::audio_callback(void *userdata, Uint8 *stream, int len) {

	VideoState *is = (VideoState *)userdata;
	int len1, audio_size;
	double pts;

	SDL_memset(stream, 0, len);

	while (len > 0) {
		if (is->audio_buf_index >= is->audio_buf_size) {
			// 音频解码
			audio_size = audio_decode_frame(is, is->audio_buf, sizeof(is->audio_buf), &pts);
			if (audio_size < 0) {
				// 音频解码错误，播放静音
				is->audio_buf_size = 1024 * 2 * 2;
				memset(is->audio_buf, 0, is->audio_buf_size);
			}
			else {
				is->audio_buf_size = audio_size;
			}
			is->audio_buf_index = 0;
		}
		len1 = is->audio_buf_size - is->audio_buf_index;
		if (len1 > len)
			len1 = len;
		SDL_MixAudio(stream, (uint8_t *)is->audio_buf + is->audio_buf_index, len1, SDL_MIX_MAXVOLUME);
		len -= len1;
		stream += len1;
		is->audio_buf_index += len1;
	}
}

//// 定时器回调函数，发送FF_REFRESH_EVENT事件，更新显示视频帧
Uint32 CTest::sdl_refresh_timer_cb(Uint32 interval, void *opaque) {
	SDL_Event event;
	event.type = FF_REFRESH_EVENT;
	event.user.data1 = opaque;
	SDL_PushEvent(&event);
	return 0;
}

//// 设置定时器
void CTest::schedule_refresh(VideoState *is, int delay) {
	SDL_AddTimer(delay, sdl_refresh_timer_cb, is);
}

//// 视频播放
void CTest::video_display(VideoState *is) {

	SDL_Rect rect;
	VideoPicture *vp;

	if (screen_width && resize) {
		SDL_SetWindowSize(win, screen_width, screen_height);
		SDL_SetWindowPosition(win, screen_left, screen_top);
		SDL_ShowWindow(win);

		Uint32 pixformat = SDL_PIXELFORMAT_IYUV;

		//create texture for render
		texture = SDL_CreateTexture(renderer,
			pixformat,
			SDL_TEXTUREACCESS_STREAMING,
			screen_width,
			screen_height);
		resize = 0;
	}

	vp = &is->pictq[is->pictq_rindex];

	// 渲染播放
	if (vp->frame) {
		SDL_UpdateYUVTexture(texture, NULL,
			vp->frame->data[0], vp->frame->linesize[0],
			vp->frame->data[1], vp->frame->linesize[1],
			vp->frame->data[2], vp->frame->linesize[2]);

		rect.x = 0;
		rect.y = 0;
		rect.w = is->video_ctx->width;
		rect.h = is->video_ctx->height;
		SDL_LockMutex(text_mutex);
		SDL_RenderClear(renderer);
		SDL_RenderCopy(renderer, texture, NULL, &rect);
		SDL_RenderPresent(renderer);
		SDL_UnlockMutex(text_mutex);
	}
}

//// 视频刷新播放，并预测下一帧的播放时间，设置新的定时器
void CTest::video_refresh_timer(void *userdata) {

	VideoState *is = (VideoState *)userdata;
	VideoPicture *vp;
	double actual_delay, delay, sync_threshold, ref_clock, diff;
	
	if (is->video_st) {

		if (is->pictq_size == 0) {
			schedule_refresh(is, 1);
		}
		else {
			// 从数组中取出一帧视频帧
			vp = &is->pictq[is->pictq_rindex];

			is->video_current_pts = vp->pts;
			is->video_current_pts_time = av_gettime();
			// 当前Frame时间减去上一帧的时间，获取两帧间的时差
			delay = vp->pts - is->frame_last_pts;
			if (delay <= 0 || delay >= 1.0) {
				// 延时小于0或大于1秒（太长）都是错误的，将延时时间设置为上一次的延时时间
				delay = is->frame_last_delay;
			}
			// 保存延时和PTS，等待下次使用
			is->frame_last_delay = delay;
			is->frame_last_pts = vp->pts;

			// 获取音频Audio_Clock
			ref_clock = get_audio_clock(is);
			// 得到当前PTS和Audio_Clock的差值
			diff = vp->pts - ref_clock;

			/* Skip or repeat the frame. Take delay into account
			   FFPlay still doesn't "know if this is the best guess." */
			sync_threshold = (delay > AV_SYNC_THRESHOLD) ? delay : AV_SYNC_THRESHOLD;
			if (fabs(diff) < AV_NOSYNC_THRESHOLD) {
				if (diff <= -sync_threshold) {
					delay = 0;
				}
				else if (diff >= sync_threshold) {
					delay = 2 * delay;
				}
			}
			is->frame_timer += delay;
			// 最终真正要延时的时间
			actual_delay = is->frame_timer - (av_gettime() / 1000000.0);
			if (actual_delay < 0.010) {
				// 延时时间过小就设置最小值
				actual_delay = 0.010;
			}
			// 根据延时时间重新设置定时器，刷新视频
			schedule_refresh(is, (int)(actual_delay * 1000 + 0.5));

			// 视频帧显示
			video_display(is);
			/**if (is->stop)			//实现视频暂停
			{
				SDL_PauseAudio(1);
				Sleep(5000.0);//暂停2s
				is->stop = false;
				SDL_PauseAudio(0);
			}**/


			// 更新视频帧数组下标
			if (++is->pictq_rindex == VIDEO_PICTURE_QUEUE_SIZE) {
				is->pictq_rindex = 0;
			}
			SDL_LockMutex(is->pictq_mutex);
			// 视频帧数组减一
			is->pictq_size--;
			SDL_CondSignal(is->pictq_cond);
			SDL_UnlockMutex(is->pictq_mutex);
		}
	}
	else {
		schedule_refresh(is, 100);
	}
}

////解码后视频帧保存
int CTest::queue_picture(VideoState *is, AVFrame *pFrame, double pts) {

	VideoPicture *vp;

	/* wait until we have space for a new pic */
	SDL_LockMutex(is->pictq_mutex);
	while (is->pictq_size >= VIDEO_PICTURE_QUEUE_SIZE && !is->quit) {
		SDL_CondWait(is->pictq_cond, is->pictq_mutex);
	}
	SDL_UnlockMutex(is->pictq_mutex);

	if (is->quit)
		return -1;

	// windex is set to 0 initially
	vp = &is->pictq[is->pictq_windex];


	//    /* allocate or resize the buffer! */
	if (!vp->frame ||
		vp->width != is->video_ctx->width ||
		vp->height != is->video_ctx->height) {

		vp->frame = av_frame_alloc();
		if (is->quit) {
			return -1;
		}
	}

	/* We have a place to put our picture on the queue */
	if (vp->frame) {

		vp->pts = pts;

		vp->frame = pFrame;
		/* now we inform our display thread that we have a pic ready */
		if (++is->pictq_windex == VIDEO_PICTURE_QUEUE_SIZE) {
			is->pictq_windex = 0;
		}

		SDL_LockMutex(is->pictq_mutex);
		is->pictq_size++;
		SDL_UnlockMutex(is->pictq_mutex);
	}
	return 0;
}

////  视频同步，获取正确的视频PTS
double CTest::synchronize_video(VideoState *is, AVFrame *src_frame, double pts) {

	double frame_delay;

	if (pts != 0) {
		is->video_clock = pts;
	}
	else {
		pts = is->video_clock;
	}
	/* update the video clock */
	frame_delay = av_q2d(is->video_ctx->time_base);
	/* if we are repeating a frame, adjust clock accordingly */
	frame_delay += src_frame->repeat_pict * (frame_delay * 0.5);
	is->video_clock += frame_delay;
	return pts;
}


//// 视频解码
int CTest::decode_video_thread(void *arg) {
	VideoState *is = (VideoState *)arg;
	AVPacket pkt1, *packet = &pkt1;
	AVFrame *pFrame;
	double pts;

	pFrame = av_frame_alloc();

	for (;;) {
		if (packet_queue_get(&is->videoq, packet, 1) < 0) {
			// means we quit getting packets
			break;
		}

		// Decode video frame
		avcodec_send_packet(is->video_ctx, packet);
		while (avcodec_receive_frame(is->video_ctx, pFrame) == 0) {
			if ((pts = pFrame->best_effort_timestamp) != AV_NOPTS_VALUE) {
			}
			else {
				pts = 0;
			}
			pts *= av_q2d(is->video_st->time_base);

			pts = synchronize_video(is, pFrame, pts);
			if (queue_picture(is, pFrame, pts) < 0) {
				break;
			}
			av_packet_unref(packet);
		}
	}
	av_frame_free(&pFrame);
	return 0;
}

void CTest::SetValue(AVCodecContext *recevicer, uint64_t &out_channel_layout, int &out_nb_samples, int &out_sample_rate, int &out_channels, int64_t &in_channel_layout) {
	out_channel_layout = AV_CH_LAYOUT_STEREO;
	out_nb_samples = recevicer->frame_size;
	out_sample_rate = recevicer->sample_rate;
	out_channels = av_get_channel_layout_nb_channels(out_channel_layout);
	in_channel_layout = av_get_default_channel_layout(recevicer->channels);

}
//// 打开流，准备解码
int CTest::stream_component_open(VideoState *is, int stream_index) {

	AVFormatContext *pFormatCtx = is->pFormatCtx;
	AVCodecContext *codecCtx = NULL;
	AVCodec *codec = NULL;
	SDL_AudioSpec wanted_spec;

	if (stream_index < 0 || stream_index >= pFormatCtx->nb_streams) {
		return -1;
	}

	codecCtx = avcodec_alloc_context3(NULL);
	int ret = avcodec_parameters_to_context(codecCtx, pFormatCtx->streams[stream_index]->codecpar);
	if (ret < 0)
		return -1;

	codec = avcodec_find_decoder(codecCtx->codec_id);
	if (!codec) {
		fprintf(stderr, "Unsupported codec!\n");
		return -1;
	}

	// 打开解码器
	if (avcodec_open2(codecCtx, codec, NULL) < 0) {
		fprintf(stderr, "Unsupported codec!\n");
		return -1;
	}

	uint64_t out_channel_layout = 0;
	int out_channels = 0;
	int out_nb_samples = 0;
	int out_sample_rate = 0;
	int64_t in_channel_layout = 0;
	switch (codecCtx->codec_type) {
		//	 音频流参数配置，打开音频设备，播放音频
	case AVMEDIA_TYPE_AUDIO:
		// Set audio settings from codec info
		wanted_spec.freq = codecCtx->sample_rate;
		wanted_spec.format = AUDIO_S16SYS;
		wanted_spec.channels = 2;//codecCtx->channels;
		wanted_spec.silence = 0;
		wanted_spec.samples = SDL_AUDIO_BUFFER_SIZE;
		wanted_spec.callback = audio_callback;
		wanted_spec.userdata = is;

		fprintf(stderr, "wanted spec: channels:%d, sample_fmt:%d, sample_rate:%d \n",
			2, AUDIO_S16SYS, codecCtx->sample_rate);

		//	 打开音频设备
		if (SDL_OpenAudio(&wanted_spec, NULL) < 0) {
			fprintf(stderr, "SDL_OpenAudio: %s\n", SDL_GetError());
			return -1;
		}

		is->audioStream = stream_index;
		is->audio_st = pFormatCtx->streams[stream_index];
		is->audio_ctx = codecCtx;
		is->audio_buf_size = 0;

		is->audio_buf_index = 0;
		memset(&is->audio_pkt, 0, sizeof(is->audio_pkt));
		packet_queue_init(&is->audioq);

		//	Out Audio Param
			//uint64_t out_channel_layout = AV_CH_LAYOUT_STEREO;

			//int out_nb_samples = is->audio_ctx->frame_size;

			//int out_sample_rate = is->audio_ctx->sample_rate;
			//int out_channels = av_get_channel_layout_nb_channels(out_channel_layout);


			//int64_t in_channel_layout = av_get_default_channel_layout(is->audio_ctx->channels);
		SetValue(is->audio_ctx, out_channel_layout, out_nb_samples, out_sample_rate, out_channels, in_channel_layout);

		//	 音频重采样
		struct SwrContext *audio_convert_ctx;
		audio_convert_ctx = swr_alloc();
		swr_alloc_set_opts(audio_convert_ctx,
			out_channel_layout,
			AV_SAMPLE_FMT_S16,
			out_sample_rate,
			in_channel_layout,
			is->audio_ctx->sample_fmt,
			is->audio_ctx->sample_rate,
			0,
			NULL);

		swr_init(audio_convert_ctx);
		is->audio_swr_ctx = audio_convert_ctx;

		//	 开始播放音频，audio_callback回调
		SDL_PauseAudio(0);

		break;
		//	 视频播放准备，解码、播放视频
	case AVMEDIA_TYPE_VIDEO:
		is->videoStream = stream_index;
		is->video_st = pFormatCtx->streams[stream_index];
		is->video_ctx = codecCtx;

		is->frame_timer = (double)av_gettime() / 1000000.0;
		is->frame_last_delay = 40e-3;
		is->video_current_pts_time = av_gettime();

		packet_queue_init(&is->videoq);

		//	 创建视频解码线程
		is->video_tid = SDL_CreateThread(decode_video_thread, "decode_video_thread", is);
		break;
	default:
		break;
	}
	return 0;
}

int CTest::setDecode(void *arg) {
	int err_code;
	char errors[1024] = { 0, };

	int w, h;

	VideoState *is = (VideoState *)arg;
	AVFormatContext *pFormatCtx = NULL;
	AVPacket pkt1, *packet = &pkt1;
	int video_index = -1;
	int audio_index = -1;
	int i;

	is->videoStream = -1;
	is->audioStream = -1;

	global_video_state = is;
	/*以上为初始化VideoState的代码*/


	/* open input file, and allocate format context */
	if ((err_code = avformat_open_input(&pFormatCtx, is->filename, NULL, NULL)) < 0) {
		av_strerror(err_code, errors, 1024);
		fprintf(stderr, "Could not open source file %s, %d(%s)\n", is->filename, err_code, errors);
		return -1;
	}

	is->pFormatCtx = pFormatCtx;

	// Retrieve stream information
	if (avformat_find_stream_info(pFormatCtx, NULL) < 0)
		return -1; // Couldn't find stream information

	// Dump information about file onto standard error
	av_dump_format(pFormatCtx, 0, is->filename, 0);

	// Find the first video stream
	for (i = 0; i < pFormatCtx->nb_streams; i++) {
		if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
			video_index < 0) {
			video_index = i;
		}
		if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO &&
			audio_index < 0) {
			audio_index = i;
		}
	}
	if (audio_index >= 0) {
		stream_component_open(is, audio_index);
	}
	if (video_index >= 0) {
		stream_component_open(is, video_index);
	}

	if (is->videoStream < 0 || is->audioStream < 0) {
		fprintf(stderr, "%s: could not open codecs\n", is->filename);
	}

	screen_width = is->video_ctx->width;
	screen_height = is->video_ctx->height;
}


// 解复用，获取音频、视频流，并将packet放入队列中
int CTest::demux_thread(void *arg) {

	VideoState *is = (VideoState *)arg;
	AVPacket pkt1, *packet = &pkt1;

	for (;;) {
		if (is->quit) {
			break;
		}
		// seek stuff goes here
		if (is->audioq.size > MAX_AUDIOQ_SIZE ||
			is->videoq.size > MAX_VIDEOQ_SIZE) {
			SDL_Delay(10);
			continue;
		}
		if (av_read_frame(is->pFormatCtx, packet) < 0) {
			if (is->pFormatCtx->pb->error == 0) {
				SDL_Delay(100);
				continue;
			}
			else {
				break;
			}
		}
		// Is this a packet from the video stream?
		if (packet->stream_index == is->videoStream) {
			packet_queue_put(&is->videoq, packet);
		}
		else if (packet->stream_index == is->audioStream) {
			packet_queue_put(&is->audioq, packet);
		}
		else {
			av_packet_unref(packet);
		}
	}
	while (!is->quit) {
		SDL_Delay(100);
	}

fail:
	if (1) {
		SDL_Event event;
		event.type = FF_QUIT_EVENT;
		event.user.data1 = is;
		SDL_PushEvent(&event);
	}
	return 0;
}

int CTest::stopAndStart(void *arg)
{
	SDL_Event event;
	VideoState *is = (VideoState *)arg;
	
	return -1;
}


size_t CTest::my_strlcpy(char *des, const char *src, size_t length)
{
	int i = 0;
	for (i = 0; src[i] != '\0' && i < length - 1; i++)
	{
		des[i] = src[i];

	}
	des[i] = '\0';
	return strlen(src);
}

int CTest::WinMain1(CWnd* wnd, char *filename) {

	char *file = filename;

	SDL_Event event;

	VideoState *is;

	is = (VideoState*)av_mallocz(sizeof(VideoState));//为VideoState分配空间

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
		fprintf(stderr, "Could not initialize SDL - %s\n", SDL_GetError());
		exit(1);
	}

	//创建SDL Window
	/*win = SDL_CreateWindow("Media Player",
		100,
		100,
		640, 480,
		SDL_WINDOW_RESIZABLE);
		播放窗口初始化*/
	win = SDL_CreateWindowFrom(
		wnd->GetSafeHwnd()
	);
	if (!win) {
		fprintf(stderr, "\nSDL: could not set video mode:%s - exiting\n", SDL_GetError());
		exit(1);
	}

	renderer = SDL_CreateRenderer(win, -1, 0);
	text_mutex = SDL_CreateMutex();	//创建线程互斥锁
	my_strlcpy(is->filename, file, sizeof(is->filename));//将视频名字copy给VideoState的filename
	is->pictq_mutex = SDL_CreateMutex();
	is->pictq_cond = SDL_CreateCond();	//条件变量
	// 定时刷新器
	schedule_refresh(is, 40);
	printf("Hello");


	setDecode(is);
	// 创建解复用线程
	is->parse_tid = SDL_CreateThread(demux_thread, "demux_thread", is);
	if (!is->parse_tid) {
		av_free(is);
		return -1;
	}

	//SDL_Thread *stopThread;
	//stopThread = SDL_CreateThread(stopAndStart, "stopAndStart", is);

	for (;;) {
		// 等待SDL事件，否则阻塞
		SDL_WaitEvent(&event);
  		switch (event.type) {
		case FF_QUIT_EVENT:
		case SDL_QUIT: // 退出
			//is->quit = 1;
			//goto Destroy;
		case SDL_KEYDOWN:
			//== SDLK_ESCAPE
			//dlg->MessageBox(_T("按键键被按下"));
			if (event.key.keysym.sym) {
				//dlg->MessageBox(_T("按键键被按下%c", event.key.keysym.sym));
				//is->quit = 1;
				//goto Destroy;
			}
			break;
		case SDL_MOUSEBUTTONDOWN:
			if (SDL_BUTTON_LEFT == event.button.button) {
			}
			break;
		case FF_REFRESH_EVENT: // 定时器刷新事件
			video_refresh_timer(event.user.data1);
			break;
		default:
			break;
		}
	}
Destroy:
	SDL_Quit();
	return 0;

}





CTest::CTest()
{
}


CTest::~CTest()
{
}
