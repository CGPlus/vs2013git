#pragma once

#include "stdafx.h"
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
#include <math.h>
#include "FFMPEGmfcDlg.h"
extern "C"
{
#include <SDL.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/time.h>
#include <libswresample/swresample.h>
#include "libavutil/imgutils.h"

}

#define SDL_AUDIO_BUFFER_SIZE 1024
#define MAX_AUDIO_FRAME_SIZE 192000 //channels(2) * data_size(2) * sample_rate(48000)

#define MAX_AUDIOQ_SIZE (5 * 16 * 1024)
#define MAX_VIDEOQ_SIZE (5 * 256 * 1024)

#define AV_SYNC_THRESHOLD 0.01
#define AV_NOSYNC_THRESHOLD 10.0

#define FF_REFRESH_EVENT (SDL_USEREVENT)
#define FF_QUIT_EVENT (SDL_USEREVENT + 1)

#define VIDEO_PICTURE_QUEUE_SIZE 1

typedef struct PacketQueue {
	AVPacketList *first_pkt, *last_pkt;
	int nb_packets;
	int size;
	SDL_mutex *mutex;
	SDL_cond *cond;
} PacketQueue;


typedef struct VideoPicture {
	AVFrame *frame;
	int width, height; /* source height & width */
	double pts;
} VideoPicture;

//存储视频基本所有信息
typedef struct VideoState {

	//multi-media file
	char filename[1024];
	AVFormatContext *pFormatCtx;
	int videoStream, audioStream;


	double audio_clock;
	double frame_timer;
	double frame_last_pts;
	double frame_last_delay;

	double video_clock; ///<pts of last decoded frame / predicted pts of next decoded frame
	double video_current_pts; ///<current displayed pts (different from video_clock if frame fifos are used)
	int64_t video_current_pts_time;  ///<time (av_gettime) at which we updated video_current_pts - used to have running video pts

	//audio
	AVStream *audio_st;
	AVCodecContext *audio_ctx;
	PacketQueue audioq;
	uint8_t audio_buf[(MAX_AUDIO_FRAME_SIZE * 3) / 2];
	unsigned int audio_buf_size;
	unsigned int audio_buf_index;
	AVFrame audio_frame;

	AVPacket audio_pkt;
	uint8_t *audio_pkt_data;
	int audio_pkt_size;
	int audio_hw_buf_size;
	struct SwrContext *audio_swr_ctx;


	//video
	AVStream *video_st;
	AVCodecContext *video_ctx;
	PacketQueue videoq;


	VideoPicture pictq[VIDEO_PICTURE_QUEUE_SIZE];
	int pictq_size, pictq_rindex
		, pictq_windex;
	SDL_mutex *pictq_mutex;
	SDL_cond *pictq_cond;

	SDL_Thread *parse_tid;
	SDL_Thread *video_tid;

	int quit;
	EM_PLAY_TYPE playtype;
} VideoState;



//播放状态
enum EM_PLAY_TYPE
{
	EM_PLAY_TYPE_PLAY,//播放
	EM_PLAY_TYPE_PAUSE,//暂停
	EM_PLAY_TYPE_STOP,//停止
	EM_PLAY_TYPE_MAX
};



class CTest
{
public:
	CTest();
	~CTest();
	static void packet_queue_init(PacketQueue *q);
	static int packet_queue_put(PacketQueue *q, AVPacket *pkt);
	static int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block);
	double get_audio_clock(VideoState *is);
	static int audio_decode_frame(VideoState *is, uint8_t *audio_buf, int buf_size, double *pts_ptr);
	static void audio_callback(void *userdata, Uint8 *stream, int len);
	static Uint32 sdl_refresh_timer_cb(Uint32 interval, void *opaque);
	static void schedule_refresh(VideoState *is, int delay);
	void video_display(VideoState *is);
	void video_refresh_timer(void *userdata);
	static int queue_picture(VideoState *is, AVFrame *pFrame, double pts);
	static double synchronize_video(VideoState *is, AVFrame *src_frame, double pts);
	static int decode_video_thread(void *arg);
	static int stream_component_open(VideoState *is, int stream_index);
	static int demux_thread(void *arg);
	int WinMain1(CWnd *wnd, char *filename);
	size_t my_strlcpy(char *des, const char *src, size_t length);
	static void SetValue(AVCodecContext *recevicer, uint64_t &out_channel_layout, int &out_nb_samples, int &out_sample_rate, int &out_channels, int64_t &in_channel_layout);
	static int stopAndStart(void *arg);
	int setDecode(void *arg);
public:
	SDL_mutex *text_mutex;
	SDL_Window *win = NULL;
	SDL_Renderer *renderer;
	SDL_Texture *texture;

public:
	HANDLE           m_hSupendEvent;//暂停信号
	HANDLE           m_hDecodeEvent;//DecodeThread暂停标志
	HANDLE           m_hVideoEvent;//VideoThread暂停标志
	HANDLE           m_hAudioEvent;//AudioThread暂停标志
	HANDLE           m_hACallEvent;//AudioCallPlay暂停标志
	HANDLE           m_hDecodeHandel;//解码线程句柄
	HANDLE           m_hVPlayHandel;//视频播放线程句柄
	HANDLE           m_hAPlayHandel;//音频播放线程句柄
};
static VideoState *global_video_state;