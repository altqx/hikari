// Original synthetic fixture. No external media or old prebuilt FFMS2 library.
#include <ffms.h>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}
#include <iostream>
#include <stdexcept>
#include <string>

static void require(bool ok, const char* message) {
    if(!ok) throw std::runtime_error(message);
}
static void checked(int result, const char* operation) {
    if(result<0) {
        char error[256]{}; av_strerror(result,error,sizeof(error));
        throw std::runtime_error(std::string(operation)+": "+error);
    }
}
static void make_fixture(const char* path) {
    AVFormatContext* mux=nullptr;
    checked(avformat_alloc_output_context2(&mux,nullptr,"matroska",path),"allocate mux");
    require(mux!=nullptr,"missing Matroska muxer");
    const AVCodec* encoder=avcodec_find_encoder(AV_CODEC_ID_FFV1);
    require(encoder!=nullptr,"missing built-in FFV1 encoder");
    AVCodecContext* codec=avcodec_alloc_context3(encoder);
    require(codec!=nullptr,"allocate encoder");
    codec->width=16;codec->height=16;codec->pix_fmt=AV_PIX_FMT_GRAY8;
    codec->time_base=AVRational{1,25};codec->framerate=AVRational{25,1};codec->gop_size=1;
    if(mux->oformat->flags & AVFMT_GLOBALHEADER)codec->flags|=AV_CODEC_FLAG_GLOBAL_HEADER;
    checked(avcodec_open2(codec,encoder,nullptr),"open encoder");
    AVStream* stream=avformat_new_stream(mux,nullptr);
    require(stream!=nullptr,"create stream");stream->time_base=codec->time_base;
    checked(avcodec_parameters_from_context(stream->codecpar,codec),"copy codec parameters");
    av_dict_set(&stream->metadata,"title","Overlay private API proof",0);
    av_dict_set(&stream->metadata,"language","eng",0);
    checked(avio_open(&mux->pb,path,AVIO_FLAG_WRITE),"open authored output");
    checked(avformat_write_header(mux,nullptr),"write header");
    AVFrame* frame=av_frame_alloc();AVPacket* packet=av_packet_alloc();
    require(frame && packet,"allocate frame/packet");
    frame->width=16;frame->height=16;frame->format=AV_PIX_FMT_GRAY8;
    checked(av_frame_get_buffer(frame,32),"allocate pixels");
    auto drain=[&] {
        for(;;) {
            int result=avcodec_receive_packet(codec,packet);
            if(result==AVERROR(EAGAIN) || result==AVERROR_EOF)break;
            checked(result,"receive packet");
            av_packet_rescale_ts(packet,codec->time_base,stream->time_base);
            packet->stream_index=stream->index;
            checked(av_interleaved_write_frame(mux,packet),"write packet");
            av_packet_unref(packet);
        }
    };
    for(int n=0;n<3;n++) {
        checked(av_frame_make_writable(frame),"writable frame");frame->pts=n;
        for(int y=0;y<16;y++)for(int x=0;x<16;x++)frame->data[0][y*frame->linesize[0]+x]=40+50*n+x;
        checked(avcodec_send_frame(codec,frame),"send frame");drain();
    }
    checked(avcodec_send_frame(codec,nullptr),"flush encoder");drain();
    checked(av_write_trailer(mux),"write trailer");
    av_packet_free(&packet);av_frame_free(&frame);avcodec_free_context(&codec);
    checked(avio_closep(&mux->pb),"close authored output");avformat_free_context(mux);
}

int main(int argc,char** argv) {
    if(argc!=2){std::cerr<<"Supply an owned scratch Matroska output path\n";return 2;}
    try {
        static_assert(FFMS_VERSION==((5<<24)|(1<<16)),"wrong public fork header");
        FFMS_Init(0,0);
        require(FFMS_GetVersion()==FFMS_VERSION,"header/library version mismatch");
        make_fixture(argv[1]);
        char error[1024]{};FFMS_ErrorInfo e{0,0,sizeof(error),error};
        FFMS_Indexer* indexer=FFMS_CreateIndexer(argv[1],&e);
        require(indexer!=nullptr,error);
        require(FFMS_GetNumTracksI(indexer)==1,"authored fixture should have one track");
        const char* title=FFMS_GetTrackName(indexer,0);
        const char* language=FFMS_GetTrackLanguage(indexer,0);
        require(title && std::string(title)=="Overlay private API proof","private title API mismatch");
        require(language && std::string(language)=="eng","private language API mismatch");
        // Borrowed metadata is consumed before DoIndexing2 deletes its indexer.
        FFMS_TrackIndexSettings(indexer,0,1,0);
        FFMS_Index* index=FFMS_DoIndexing2(indexer,FFMS_IEH_ABORT,&e);indexer=nullptr;
        require(index!=nullptr,error);
        require(FFMS_GetNumFrames(FFMS_GetTrackFromIndex(index,0))==3,"indexed frame count mismatch");
        FFMS_VideoSource* video=FFMS_CreateVideoSource(argv[1],0,index,1,FFMS_SEEK_NORMAL,&e);
        require(video!=nullptr,error);
        const int formats[]={FFMS_GetPixFmt("gray"),-1};
        require(formats[0]>=0,"missing gray pixel format");
        require(FFMS_SetOutputFormatV2(video,formats,16,16,FFMS_RESIZER_POINT,&e)==0,error);
        for(int n:{2,0,1}) {
            const FFMS_Frame* f=FFMS_GetFrame(video,n,&e);require(f!=nullptr,error);
            require(f->ScaledWidth==16 && f->ScaledHeight==16 && f->Linesize[0]>=16,"bad frame layout");
            for(int y=0;y<16;y++)for(int x=0;x<16;x++)
                require(f->Data[0][y*f->Linesize[0]+x]==40+50*n+x,"decoded pixel/index mismatch");
        }
        FFMS_DestroyVideoSource(video);FFMS_DestroyIndex(index);
        std::cout<<"{\"private_track_title\":true,\"private_track_language\":true,\"frames_checked\":3,"
                 <<"\"request_order\":[2,0,1],\"ffms_version\":"<<FFMS_GetVersion()
                 <<",\"ffmpeg_version\":\""<<av_version_info()<<"\"}\n";
        return 0;
    } catch(const std::exception& e) {
        std::cerr<<"PROBE_FAILED: "<<e.what()<<'\n';return 1;
    }
}
