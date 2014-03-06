
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <libconfig.h>

//#include <SDL.h>

#include <opencv2/imgproc/imgproc.hpp>
#include <cv.h>
#include <highgui.h>


#ifdef HAVE_AV_CONFIG_H
#undef HAVE_AV_CONFIG_H
#endif

extern "C"
{
//#define PRId64 "d"
//#define __STDC_CONSTANT_MACROS // for UINT64_C
#include "libavutil/opt.h"
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/mathematics.h"
#include "libavutil/samplefmt.h"
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
//#include <libavutil/timestamp.h>
}
#define INBUF_SIZE 4096
#define AUDIO_INBUF_SIZE 20480
#define AUDIO_REFILL_THRESH 4096
#define STARTING_FRAMES_COUNT 125

#define STREAM_DURATION   5.0
#define STREAM_FRAME_RATE 25 /* 25 images/s */
#define STREAM_NB_FRAMES  ((int)(STREAM_DURATION * STREAM_FRAME_RATE))
#define STREAM_PIX_FMT PIX_FMT_YUV420P /* default pix_fmt */

using namespace cv;
using namespace std;
// Config defaults
static int SENSETIVITY_MOTION_PIX=120;
static int SENSETIVITY_FRAME_COUNT=10;

void start_cam(char* camurl, char* output_dir, int motion_detect_pix, int motion_detect_frames);


static AVFormatContext *fmt_ctx = NULL;
static AVCodecContext *video_dec_ctx = NULL, *audio_dec_ctx;
static AVStream *video_stream = NULL, *audio_stream = NULL;
static int video_stream_idx = -1, audio_stream_idx = -1;

static AVOutputFormat *out_fmt=NULL;
static AVFormatContext *fmt_ctx_out = NULL;
static AVCodecContext *video_dec_ctx_out = NULL, *audio_dec_ctx_out;
static AVStream *video_stream_out = NULL, *audio_stream_out = NULL;
static int video_stream_idx_out = -1, audio_stream_idx_out = -1;
int64_t outpts,outdts;
int outduration;

static int max_record_frames=125; /// записывать 5 сек после окончания движения
static int starting_frames_count=STARTING_FRAMES_COUNT;
int record_frames=0;
    int first_frame=1;

static IplImage *_previous_img = NULL;
static IplImage *m_bluredImage = NULL;
static IplImage *mask = NULL;

void read_config()
{
FILE *config_file;
config_t cfg;
const char *str;
config_setting_t *setting;

config_init(&cfg);
if (!config_read_file(&cfg,"ffdvr.conf"))
{
  fprintf(stderr, "%s:%d - %s\n", config_error_file(&cfg),
            config_error_line(&cfg), config_error_text(&cfg));
    config_destroy(&cfg);
    return(EXIT_FAILURE);
}

 if(config_lookup_string(&cfg, "name", &str))
    printf("Store name: %s\n\n", str);
  else
    fprintf(stderr, "No 'name' setting in configuration file.\n");

setting = config_lookup(&cfg, "camera");
  if(setting != NULL)
  {
    int count = config_setting_length(setting);
    int i;
     for(i = 0; i < count; ++i)
        {
printf("1\n");
	  config_setting_t *cam = config_setting_get_elem(setting, i);
	  const char *url, *name, *output_dir;
	  int motion_detect_pix;
	  int motion_detect_frames;
	    
	    
	    
	    
	    
	  if((config_setting_lookup_string(cam, "url", &url) && config_setting_lookup_string(cam, "name", &name) && 
	    config_setting_lookup_string(cam, "output_dir", &output_dir) &&
	    config_setting_lookup_int(cam, "motion_detect_pix", &motion_detect_pix) &&
	    config_setting_lookup_int(cam, "motion_detect_frames", &motion_detect_frames))) 
		{
		printf("%s %s %s %3d %3d\n", name, url, output_dir, motion_detect_pix, motion_detect_frames); 

		start_cam(url, output_dir, motion_detect_pix, motion_detect_frames);

		}
;

	}

  }




}



int motion_detect(IplImage *src)
{
    cvCvtColor(src,mask,CV_RGB2GRAY);

    cvSmooth(mask, m_bluredImage,CV_GAUSSIAN, 0, 0,1,0);

    if (!_previous_img) { cvCopy(m_bluredImage,_previous_img); return 0;}

    cvAbsDiff( m_bluredImage, _previous_img, mask );
    cvThreshold( mask, mask,  32, 255, THRESH_BINARY );
//    cvMorphologyEx( mask, mask, _previous_img, MORPH_CLOSE, cv::Mat() );
//    cvMorphologyEx( mask, mask, _previous_img, MORPH_OPEN, m_openingKernel,
//                      cv::Point( -1, -1 ), 1, :BORDER_CONSTANT, cv::Scalar( 0 ) );
//    cv::updateMotionHistory( mask, m_motionHistoryImage, timestamp, m_motionHistoryDuration );
//    cv::segmentMotion( m_motionHistoryImage, m_segmask, targets, timestamp, m_maxMotionGradient );
    cvCopy(m_bluredImage,_previous_img);
    int cnz=cvCountNonZero(mask);

    cvShowImage("src",src);
    cvWaitKey(1);
//    IplImage *temp;

//    if (cnz>0) { return cnz/((mask->width*mask->height)/100); } else { return 0;}
    return cnz;

}


int av2ipl(AVFrame *src, IplImage *dst)
{

    struct SwsContext *swscontext = sws_getContext(src->width, src->height, PIX_FMT_YUV420P,
					dst->width, dst->height, PIX_FMT_BGR24, SWS_BILINEAR, 0, 0, 0);
    if (swscontext == 0) { printf ("ret0");return(0);}
    int linesize[4] = {dst->widthStep, 0, 0, 0 };
    sws_scale(swscontext, src->data, src->linesize, 0, src->height, (uint8_t **) & (dst->imageData), linesize);
    sws_freeContext(swscontext);
    return 1;
}

static AVStream *add_video_stream(AVFormatContext *oc, enum CodecID codec_id)
{
     AVCodecContext *c;
     AVStream *st;

     st = avformat_new_stream(oc, 0);
     if (!st) {
         fprintf(stderr, "Could not alloc stream\n");
         exit(1);
     }

     c = st->codec;
     c->codec_id = codec_id;
     c->codec_type = AVMEDIA_TYPE_VIDEO;
//    st->stream_copy = 1;	
//    st->id=0x1e0;
//st->bit_rate=4000000;
     /* put sample parameters */
//     c->bit_rate = ;
//     c->bit_rate = 4000000;
     /* resolution must be a multiple of two */
     //c->width = 352;
     //c->height = 288;
     /* time base: this is the fundamental unit of time (in seconds) in terms
       of which frame timestamps are represented. for fixed-fps content,
        timebase should be 1/framerate and timestamp increments should be
        identically 1. */
     c->time_base.den = STREAM_FRAME_RATE;
     c->time_base.num = 1;


     c->gop_size = 12; /* emit one intra frame every twelve frames at most */
     c->pix_fmt = STREAM_PIX_FMT;
     if (c->codec_id == CODEC_ID_MPEG2VIDEO) {
         /* just for testing, we also add B frames */
         c->max_b_frames = 2;
     }
     if (c->codec_id == CODEC_ID_MPEG1VIDEO){
         /* Needed to avoid using macroblocks in which some coeffs overflow.
            This does not happen with normal video, it just happens here as
            the motion of the chroma plane does not match the luma plane. */
         c->mb_decision=2;
     }
     // some formats want stream headers to be separate
     if(oc->oformat->flags & AVFMT_GLOBALHEADER)
         c->flags |= CODEC_FLAG_GLOBAL_HEADER;

     return st;
}

static void close_video(AVFormatContext *oc, AVStream *st)
{
    avcodec_close(st->codec);
//    av_free(src_picture.data[0]);
//    av_free(dst_picture.data[0]);
//    av_free(frame);
}

int openCreateNewFile(AVFrame *src,AVPacket pkt, char* out_dir)
{
    // Новый файл
    char *outfilename;
    outpts=pkt.pts;
    outdts=pkt.dts;
    outduration=pkt.duration;
    outfilename = (char*)malloc(255);
    time_t t(time(NULL));// current time
    tm tm(*localtime(&t));
    sprintf(outfilename,"%s/outfile_%d_%d_%d_%d_%d_%d.mp4",out_dir,tm.tm_year+1900,tm.tm_mon,tm.tm_mday,tm.tm_hour,tm.tm_min,tm.tm_sec);

    record_frames=max_record_frames;
    printf("open out file\n");
    out_fmt=av_guess_format("mpeg", outfilename,NULL);
    if (!out_fmt) {
         printf("Could not deduce output format from file extension: using MPEG.\n");
         out_fmt = av_guess_format("mpeg", NULL, NULL);
         }

    fmt_ctx_out = avformat_alloc_context();
    if (!fmt_ctx_out) {
         fprintf(stderr, "Memory error\n");
         exit(1);
     }
    fmt_ctx_out->oformat = out_fmt;
//                        fmt_ctx_out->oformat->name = (const char*)"enhanced rtsp media recorder";
    fmt_ctx_out->oformat->flags|= AVFMT_NOTIMESTAMPS;
//                        fmt_ctx_out->oformat->flags|= AVFMT_ALLOW_FLUSH;

    fmt_ctx_out->max_picture_buffer=3000000;
    snprintf(fmt_ctx_out->filename, sizeof(fmt_ctx_out->filename), "%s", outfilename);
    if (out_fmt->video_codec != CODEC_ID_NONE) 
        {
         video_stream_out = add_video_stream(fmt_ctx_out, out_fmt->video_codec);
         video_stream_out->codec->codec_id = out_fmt->video_codec;
         video_stream_out->codec->codec_type = AVMEDIA_TYPE_VIDEO;
         video_stream_out->codec->codec_tag = 0x31637661;

         video_stream_out->start_time=0;
         video_stream_out->codec->rc_buffer_size=8000000;
        }
    int nvideo_stream_out;
    for (nvideo_stream_out = 0; nvideo_stream_out < fmt_ctx_out->nb_streams; ++nvideo_stream_out) {
       if (fmt_ctx->streams[nvideo_stream_out]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
        break;
        }
    }

    fmt_ctx_out->streams[nvideo_stream_out]->codec=fmt_ctx->streams[video_stream_idx]->codec;
    printf("open out file1\n");

    if (!(out_fmt->flags & AVFMT_NOFILE)) {
       if ( avio_open(&fmt_ctx_out->pb, outfilename, AVIO_FLAG_WRITE) < 0)
        {
        printf("CreateOutFile::Cann't create file %s", outfilename);
        exit(1);
        }
    }

    printf("Writing header\n");
    if (avformat_write_header(fmt_ctx_out, NULL) < 0) {
        fprintf(stderr, "Error occurred when opening output file\n");
        return 1;
    }
first_frame=1;
}

void start_cam(char* url, char* output_dir, int motion_detect_pix, int motion_detect_frames)
{
    const char *filename; 
    filename = url;
    int err;
    int motion_pixels;
    char* buf=(char*)malloc(64);
    uint8_t *outbuf;
    static float mux_preload= 1.5;
    static float mux_max_delay= 0.7;
//    fmt_ctx->preload= (int)(mux_preload*AV_TIME_BASE);
//    fmt_ctx->max_delay= (int)(mux_max_delay*AV_TIME_BASE);
    snprintf(buf, sizeof(buf), "%d", (int)(mux_preload*AV_TIME_BASE));
    
    AVDictionary *opts = 0;
    av_dict_set(&opts, "rtsp_transport", "tcp", 0);
    av_dict_set(&opts, "preload", buf, 0);
    snprintf(buf, sizeof(buf), "%d", (int)(mux_max_delay*AV_TIME_BASE));
    av_dict_set(&opts, "delay", buf, 0);

	printf("buf=%s\n",buf);
    IplImage* dst=NULL;
    char recording=0;
    int record_frames=0;
    /* register all the codecs */
    av_register_all();
    avformat_network_init();

    dst=cvCreateImage(cvSize(640,480),IPL_DEPTH_8U,3);
    mask=cvCreateImage(cvSize(640,480),IPL_DEPTH_8U,1);
    m_bluredImage=cvCreateImage(cvSize(640,480),IPL_DEPTH_8U,1);
   _previous_img=cvCreateImage(cvSize(640,480),IPL_DEPTH_8U,1);

        /* open input file, and allocate format context */
    if (avformat_open_input(&fmt_ctx, filename, NULL, &opts) < 0) 
    {
	    printf("Could not open source file %s\n", filename);
	    exit(1);
    }


    /* retrieve stream information */
    if (avformat_find_stream_info(fmt_ctx, NULL) < 0) 
    {
        fprintf(stderr, "Could not find stream information\n");
        exit(1);
    }

    av_dump_format(fmt_ctx, 0, filename, 0);
    printf("nb_streams=%d\n",fmt_ctx->nb_streams);


///    int video_stream=1;
    for (video_stream_idx = 0; video_stream_idx < fmt_ctx->nb_streams; ++video_stream_idx) 
    {
        if (fmt_ctx->streams[video_stream_idx]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            break;
        }
    }
    
    if (video_stream_idx == fmt_ctx->nb_streams) {
        fprintf(stderr, "ffmpeg: Unable to find video stream\n");
        return -1;
    }
    

    AVCodecContext* codec_context = fmt_ctx->streams[video_stream_idx]->codec;
    AVCodec* codec = avcodec_find_decoder(codec_context->codec_id);

//        av_opt_set(codec_context->priv_data, "preset", "slow", 0);
//        av_opt_set(codec_context->priv_data, "rtsp_transport", "tcp", 0);


    err = avcodec_open2(codec_context, codec, NULL);
    if (err < 0) {
        fprintf(stderr, "ffmpeg: Unable to open codec\n");
        return -1;
    }



/// output 


    cvNamedWindow("src",CV_WINDOW_AUTOSIZE);

    AVFrame* frame = avcodec_alloc_frame();
    AVPacket packet;
    AVPacket outputPacket;
    int frame_finished;
    int motion_frames=0;
    int decode_error;
    while (av_read_frame(fmt_ctx, &packet) >= 0) {
        if (packet.stream_index == video_stream_idx) {
            // Video stream packet
            decode_error =  avcodec_decode_video2(codec_context, frame, &frame_finished, &packet);

            // сначала надо проверить нет ли ошибок при распаковке фрейма, если есть то continue;
            if (decode_error<=0) 
            {
                printf ("frame decode error %d\n",decode_error);
                continue;
            }


            if (frame_finished) {
                if (starting_frames_count>0) starting_frames_count--;
                av2ipl(frame, dst);

                motion_pixels=motion_detect(dst);
                printf("motion pixels=%d record_frames=%d  pktflag=%d first=%d motion_framds=%d\n",motion_pixels,record_frames,packet.flags,first_frame, motion_frames);

                if (motion_pixels>=motion_detect_pix)
                    {
		    motion_frames++;
		    if (motion_frames>=motion_detect_frames) {
                    // start record
                    if(record_frames==0)
                        {
                        /// Открыть файл для записи
                           if (starting_frames_count==0){
                            openCreateNewFile(frame,packet,output_dir);
                            record_frames=max_record_frames;
                            }
                        } else
                        {
                            // Файл уже открыт.
                            record_frames=max_record_frames;
                        }

		    }
                    }
else 
{
motion_frames=0;
}
		//motion pixels

                if (record_frames>0)
                {
                    if (first_frame!=1 || (packet.flags&AV_PKT_FLAG_KEY))
                    {
                    first_frame=0;
                    av_init_packet(&outputPacket);
//                    outputPacket.pts = packet.pts;
//                    outputPacket.dts = packet.dts;
                    outputPacket.pts = packet.pts-outpts;
                    outputPacket.dts = packet.dts-outdts;
                    outputPacket.stream_index= video_stream_out->index;
                    outputPacket.data= packet.data;
                    outputPacket.size= packet.size;
                    outputPacket.flags=packet.flags;
                    outputPacket.duration=0;

                    if(av_write_frame(fmt_ctx_out, &outputPacket) != 0)
                    {
                     printf("SetVideoFrame::Unable to write video frame\n");
                    }
                    av_free_packet(&outputPacket);
                    //fmt_ctx_out->streams[nvideo_stream_out]->nb_frames++;
                    record_frames--;
                    if (record_frames==0)
                       {
                        /// Закрываем файл
                        printf("Write trailer\n");
                        av_write_trailer(fmt_ctx_out);
                        avio_close(fmt_ctx_out->pb);
                        av_free(fmt_ctx_out);
                       }
                     } else
                     {
                        printf("waiting AV_PKT_FLAG_KEY frame\n");
                     }
                 }
	    }//frame finished

    }// video streem
    av_free_packet(&packet);
  }// while
    // Free the YUV frame
    av_free(frame);
    
    // Close the codec
    avcodec_close(codec_context);
    
    // Close the video file
    avformat_close_input(&fmt_ctx);
    
    // Quit SDL
//    SDL_Quit();
    return 0;



}

int main(int argc, char **argv)
{

read_config();
    return 0;
}
