#include <ffms.h>
#include <cstdio>
int main(int argc,char** argv) {
    if(argc!=2) return 2;
    FFMS_Init(0,0);
    char error[1024]{};
    FFMS_ErrorInfo info{0,0,sizeof(error),error};
    auto* indexer=FFMS_CreateIndexer(argv[1],&info);
    if(!indexer) { std::puts(error);return 3; }
    auto* index=FFMS_DoIndexing2(indexer,FFMS_IEH_ABORT,&info);
    if(!index) { std::puts(error);return 4; }
    auto* video=FFMS_CreateVideoSource(argv[1],0,index,1,FFMS_SEEK_NORMAL,&info);
    if(!video) { std::puts(error);FFMS_DestroyIndex(index);return 5; }
    const int formats[]={FFMS_GetPixFmt("gray"),-1};
    const bool formatOk=FFMS_SetOutputFormatV2(video,formats,16,16,FFMS_RESIZER_POINT,&info)==0;
    const auto* frame=formatOk ? FFMS_GetFrame(video,0,&info) : nullptr;
    const bool ok=frame && frame->ScaledWidth==16 && frame->ScaledHeight==16 && frame->Data[0][0]==40 && FFMS_GetVersion()==FFMS_VERSION;
    FFMS_DestroyVideoSource(video);FFMS_DestroyIndex(index);
    if(!ok) { std::puts(error);return 6; }
    std::puts("{\"ffms_only_target_link\":true,\"authored_frame0_pixel0\":40}");
    return 0;
}
