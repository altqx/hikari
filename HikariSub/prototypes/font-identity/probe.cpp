// Throwaway Windows feasibility probe, not a production diagnostic ABI.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dwrite.h>
#include <cstdio>
#include <cstdarg>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
extern "C" {
#include "ass.h"
}
namespace fs = std::filesystem;
static fs::path output;
static std::string current;
static unsigned sequence = 0;
static std::string jsonQuote(const std::string& value) {
    std::ostringstream s; s << '"';
    for (unsigned char c : value) {
        if (c=='"' || c=='\\') s << '\\' << c;
        else if (c < 32) s << "\\u" << std::hex << std::setw(4) << std::setfill('0') << (int)c;
        else s << c;
    }
    return s.str()+'"';
}
static void emit(const std::string& event, const std::string& fields) {
    std::cout << "{\"case\":" << jsonQuote(current) << ",\"event\":" << jsonQuote(event) << ',' << fields << "}" << std::endl;
}
static std::string utf8(const std::wstring& s) {
    if (s.empty()) return {};
    std::string result(WideCharToMultiByte(CP_UTF8,0,s.data(),(int)s.size(),nullptr,0,nullptr,nullptr),0);
    WideCharToMultiByte(CP_UTF8,0,s.data(),(int)s.size(),result.data(),(int)result.size(),nullptr,nullptr);
    return result;
}
static std::wstring wide(const std::string& s) {
    std::wstring result(MultiByteToWideChar(CP_UTF8,0,s.data(),(int)s.size(),nullptr,0),0);
    MultiByteToWideChar(CP_UTF8,0,s.data(),(int)s.size(),result.data(),(int)result.size());
    return result;
}
static std::vector<char> read(const fs::path& path) {
    std::ifstream f(path,std::ios::binary);
    return std::vector<char>(std::istreambuf_iterator<char>(f),{});
}
static void message(int level,const char* format,va_list args,void*) {
    // The existing local .lib was built with bare CONFIG_SOURCEVERSION (=1).
    // Its %s argument is therefore not a string. Guard this one diagnostic in
    // both runs; do not pretend the archive has a trustworthy revision label.
    if (!strcmp(format,"libass source: %s")) {
        va_list copy; va_copy(copy,args); auto pointer=va_arg(copy,const char*);va_end(copy);
        if (reinterpret_cast<uintptr_t>(pointer)<65536) {
            emit("archive_source_label_invalid","\"pointer_value\":"+std::to_string(reinterpret_cast<uintptr_t>(pointer)));
            return;
        }
    }
    char text[16384]; vsnprintf(text,sizeof(text),format,args);
    emit("log","\"level\":"+std::to_string(level)+",\"text\":"+jsonQuote(text));
}

// Called only from the generated, pinned-source selector copy. Capture the
// provider's actual selected stream, not a second name lookup. Size guard is
// a probe limit, not an accepted product limit. No copyrighted fonts committed.
using StreamRead = size_t(*)(void*,unsigned char*,size_t,size_t);
extern "C" void hikari_font_probe_v1(const char* display,const char* path,
        int index,int uid,uint32_t code,unsigned weight,unsigned italic,
        const char* ps,StreamRead get,void* opaque) {
    std::vector<char> bytes;
    std::string failure;
    if (path) bytes=read(fs::u8path(path));
    else if (get) {
        size_t n=get(opaque,nullptr,0,0);
        if (n && n <= 128*1024*1024) {
            bytes.resize(n);
            if (get(opaque,reinterpret_cast<unsigned char*>(bytes.data()),0,n)!=n) {
                bytes.clear(); failure="short provider read";
            }
        } else failure="empty or over probe size limit";
    } else failure="no path or stream";
    std::string file;
    if (!bytes.empty()) {
        file="selected/"+current+"-"+std::to_string(sequence++)+".font";
        std::ofstream f(output/file,std::ios::binary); f.write(bytes.data(),bytes.size());
        if (!f) { file.clear(); failure="write failed"; }
    }
    emit("selected_v1","\"display\":"+jsonQuote(display?display:"")+",\"postscript\":"+jsonQuote(ps?ps:"")+
         ",\"face\":"+std::to_string(index)+",\"uid_local\":"+std::to_string(uid)+
         ",\"codepoint\":"+std::to_string(code)+",\"weight\":"+std::to_string(weight)+
         ",\"italic_request\":"+std::to_string(italic)+",\"file\":"+jsonQuote(file)+
         ",\"size\":"+std::to_string(bytes.size())+",\"failure\":"+jsonQuote(failure));
}

extern "C" void hikari_font_face_v1(int uid,long index,long faces,const char* ps) {
    emit("opened_face_v1","\"uid_local\":"+std::to_string(uid)+",\"face\":"+std::to_string(index)+
         ",\"face_count\":"+std::to_string(faces)+",\"postscript\":"+jsonQuote(ps?ps:""));
}
extern "C" void hikari_font_simulation_v1(int uid,int glyph,int bold,int italic) {
    emit("glyph_simulation_v1","\"uid_local\":"+std::to_string(uid)+",\"glyph\":"+std::to_string(glyph)+
         ",\"embolden\":"+(bold?"true":"false")+",\"italicize\":"+(italic?"true":"false"));
}

// Independent GDI-compatible DirectWrite resolution. This produces candidates,
// never a renderer identity by itself, even when its family/face appears equal.
static void metadata(const std::string& family,int bold,int italic) {
    IDWriteFactory* factory=nullptr; IDWriteGdiInterop* interop=nullptr; IDWriteFontFace* face=nullptr;
    HRESULT hr=DWriteCreateFactory(DWRITE_FACTORY_TYPE_ISOLATED,__uuidof(IDWriteFactory),(IUnknown**)&factory);
    HDC dc=CreateCompatibleDC(nullptr);
    LOGFONTW lf={}; lf.lfHeight=60; lf.lfWeight=bold?700:400; lf.lfItalic=italic?1:0;
    wcsncpy_s(lf.lfFaceName,wide(family).c_str(),_TRUNCATE);
    HFONT font=CreateFontIndirectW(&lf); HGDIOBJ previous=SelectObject(dc,font);
    WCHAR matched[256]={}; GetTextFaceW(dc,256,matched);
    if (SUCCEEDED(hr)) hr=factory->GetGdiInterop(&interop);
    if (SUCCEEDED(hr)) hr=interop->CreateFontFaceFromHdc(dc,&face);
    if (SUCCEEDED(hr)) {
        UINT32 count=0; hr=face->GetFiles(&count,nullptr);
        std::vector<IDWriteFontFile*> files(count);
        if (SUCCEEDED(hr)) hr=face->GetFiles(&count,files.data());
        if (SUCCEEDED(hr)) for (UINT32 i=0;i<count;i++) {
            const void* key=nullptr; UINT32 keySize=0; IDWriteFontFileLoader* loader=nullptr;
            IDWriteFontFileStream* stream=nullptr; IDWriteLocalFontFileLoader* local=nullptr;
            std::wstring path;
            HRESULT step=files[i]->GetReferenceKey(&key,&keySize);
            if (SUCCEEDED(step)) step=files[i]->GetLoader(&loader);
            if (SUCCEEDED(step) && SUCCEEDED(loader->QueryInterface(__uuidof(IDWriteLocalFontFileLoader),(void**)&local))) {
                UINT32 n=0;
                if (SUCCEEDED(local->GetFilePathLengthFromKey(key,keySize,&n))) {
                    path.resize(n+1);
                    if (SUCCEEDED(local->GetFilePathFromKey(key,keySize,path.data(),n+1))) path.resize(n);
                    else path.clear();
                }
            }
            std::string file;
            if (SUCCEEDED(step)) step=loader->CreateStreamFromKey(key,keySize,&stream);
            if (SUCCEEDED(step)) {
                UINT64 size=0; step=stream->GetFileSize(&size);
                const void* data=nullptr; void* context=nullptr;
                if (SUCCEEDED(step) && size && size<=128*1024*1024) step=stream->ReadFileFragment(&data,0,size,&context);
                if (SUCCEEDED(step) && data) {
                    file="metadata/"+current+"-"+std::to_string(i)+".font";
                    std::ofstream f(output/file,std::ios::binary); f.write((const char*)data,size);
                    stream->ReleaseFileFragment(context);
                }
            }
            emit("metadata_candidate","\"gdi_family\":"+jsonQuote(utf8(matched))+
                 ",\"face\":"+std::to_string(face->GetIndex())+
                 ",\"simulations\":"+std::to_string(face->GetSimulations())+
                 ",\"path\":"+jsonQuote(utf8(path))+",\"file\":"+jsonQuote(file)+
                 ",\"hresult\":"+std::to_string((long)step));
            if(local)local->Release(); if(stream)stream->Release(); if(loader)loader->Release(); files[i]->Release();
        }
    }
    if(FAILED(hr))emit("metadata_error","\"hresult\":"+std::to_string((long)hr));
    if(face)face->Release(); if(interop)interop->Release(); if(factory)factory->Release();
    SelectObject(dc,previous); DeleteObject(font); DeleteDC(dc);
}

struct Case {
    std::string id,family,text; int provider=0,bold=0,italic=0;
    std::vector<std::string> fonts; std::string fallback="HikariProbeCollision";
};
struct Context { Case spec; ASS_Library* library; ASS_Renderer* renderer; ASS_Track* track; };
static Context create(Case spec,const fs::path& fixtures) {
    current=spec.id;
    emit("request","\"family\":"+jsonQuote(spec.family)+",\"text\":"+jsonQuote(spec.text)+
         ",\"provider_requested\":"+std::to_string(spec.provider)+",\"bold\":"+std::to_string(spec.bold)+
         ",\"italic\":"+std::to_string(spec.italic));
    Context c{spec,ass_library_init(),nullptr,nullptr};
    ass_set_message_cb(c.library,message,nullptr);
    for (auto& name:spec.fonts) {
        auto bytes=read(fixtures/name);
        ass_add_font(c.library,name.c_str(),bytes.data(),(int)bytes.size());
    }
    c.renderer=ass_renderer_init(c.library);
    ass_set_frame_size(c.renderer,800,200);
    ass_set_fonts(c.renderer,nullptr,spec.fallback.c_str(),(ASS_DefaultFontProvider)spec.provider,nullptr,1);
    std::string script="[Script Info]\nScriptType: v4.00+\nPlayResX: 800\nPlayResY: 200\n[V4+ Styles]\n"
        "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, "
        "Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n"
        "Style: Default,"+spec.family+",60,&H00FFFFFF,&H00FFFFFF,&H00000000,&H00000000,"+
        std::to_string(spec.bold?-1:0)+","+std::to_string(spec.italic?-1:0)+",0,0,100,100,0,0,1,0,0,5,0,0,0,1\n"
        "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
        "Dialogue: 0,0:00:00.00,0:00:10.00,Default,,0,0,0,,"+spec.text+"\n";
    c.track=ass_read_memory(c.library,script.data(),script.size(),nullptr);
    if(!c.renderer || !c.track)throw std::runtime_error("libass initialization/track failed");
    return c;
}
static void render(Context& c,const std::string& id) {
    current=id; int changed=0,images=0; uint64_t hash=14695981039346656037ull;
    std::vector<unsigned char> pixels(800*200*3,24);
    for(ASS_Image* im=ass_render_frame(c.renderer,c.track,1000,&changed);im;im=im->next) {
        images++;
        auto mix=[&](uint32_t v){ for(int j=0;j<4;j++){ hash^=(v>>(j*8))&255; hash*=1099511628211ull; } };
        mix(im->w);mix(im->h);mix(im->dst_x);mix(im->dst_y);mix(im->color);
        for(int y=0;y<im->h;y++)for(int x=0;x<im->w;x++) {
            unsigned char coverage=im->bitmap[y*im->stride+x]; hash^=coverage;hash*=1099511628211ull;
            int px=x+im->dst_x,py=y+im->dst_y;
            if(px<0||px>=800||py<0||py>=200)continue;
            unsigned alpha=coverage*(255-(im->color&255))/255;
            for(int ch=0;ch<3;ch++) {
                unsigned color=(im->color>>(24-8*ch))&255;
                auto& p=pixels[(py*800+px)*3+ch]; p=(color*alpha+p*(255-alpha))/255;
            }
        }
    }
    std::ostringstream hex;hex<<std::hex<<hash;
    std::string file="renders/"+id+".ppm";
    std::ofstream f(output/file,std::ios::binary); f<<"P6\n800 200\n255\n";f.write((char*)pixels.data(),pixels.size());
    emit("render","\"images\":"+std::to_string(images)+",\"fnv1a64\":"+jsonQuote(hex.str())+",\"file\":"+jsonQuote(file));
}
static void destroy(Context& c) { ass_free_track(c.track);ass_renderer_done(c.renderer);ass_library_done(c.library); }
int main(int argc,char** argv) {
    if(argc!=3){std::cerr<<"probe OUTPUT FIXTURES\n";return 2;}
    output=fs::u8path(argv[1]);fs::path fixtures=fs::u8path(argv[2]);
    for(auto dir:{"selected","metadata","renders"})fs::create_directories(output/dir);
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    current="runtime";
    emit("version","\"libass_hex\":"+std::to_string(ass_library_version()));
    ASS_Library* lib=ass_library_init();ASS_DefaultFontProvider* providers=nullptr;size_t n=0;
    ass_get_available_font_providers(lib,&providers,&n);
    for(size_t i=0;i<n;i++)emit("available_provider","\"value\":"+std::to_string(providers[i]));
    free(providers);ass_library_done(lib);
    std::vector<Case> cases={
        {"attachment_a","HikariProbeCollision","ABC",0,0,0,{"collision-a.ttf"}},
        {"attachment_b","HikariProbeCollision","ABC",0,0,0,{"collision-b.ttf"}},
        {"collision_ab","HikariProbeCollision","ABC",0,0,0,{"collision-a.ttf","collision-b.ttf"}},
        {"collision_ba","HikariProbeCollision","ABC",0,0,0,{"collision-b.ttf","collision-a.ttf"}},
        {"legacy_name","HikariProbeLegacy","ABC",0,0,0,{"legacy.ttf"}},
        {"full_name","HikariProbeLegacy Regular","ABC",0,0,0,{"legacy.ttf"}},
        {"postscript_name","HikariProbeLegacyPS","ABC",0,0,0,{"legacy.ttf"}},
        {"typographic_name","HikariProbeModern","ABC",0,0,0,{"legacy.ttf"}},
        {"collection_a","HikariProbeCollectionA","ABC",0,0,0,{"collection.ttc"}},
        {"collection_b","HikariProbeCollectionB","ABC",0,0,0,{"collection.ttc"}},
        {"synthetic_style","HikariProbeCollision","ABC",0,1,1,{"collision-a.ttf"}},
        {"missing_request","HikariProbeAbsent","ABC",0,0,0,{"collision-a.ttf"}},
        {"missing_glyph","HikariProbeCollision",u8"A中",0,0,0,{"collision-a.ttf"}},
        {"controlled_fallback","HikariProbeCollision",u8"A中ع́",0,0,0,{"collision-a.ttf","fallback.ttf"},"HikariProbeFallback"},
        {"system_arial","Arial","ABC",4,0,0,{},"Arial"},
        {"system_ps_alias","ArialMT","ABC",4,0,0,{},"Arial"},
        {"system_vertical","@Arial","ABC",4,0,0,{},"Arial"},
        {"system_missing","HikariProbeAbsent","ABC",4,0,0,{},"Arial"},
        {"system_multilingual","Arial",u8"A中ع́😀",4,0,0,{},"Arial"},
        {"system_attachment_collision","Arial","ABC",4,0,0,{"arial-conflict.ttf"},"Arial"},
    };
    for(auto& spec:cases){auto c=create(spec,fixtures);metadata(spec.family,spec.bold,spec.italic);render(c,spec.id);destroy(c);}
    // Two live libraries, interleaved single-thread rendering; not thread stress.
    auto a=create(cases[0],fixtures);auto b=create(cases[1],fixtures);
    render(a,"live_a_before");render(b,"live_b");destroy(b);render(a,"live_a_after_other_destroy");
    auto refreshed=create(cases[1],fixtures);render(refreshed,"new_generation_b");
    render(a,"old_generation_a");destroy(refreshed);destroy(a);
    // Reimport each trace's complete captured set into an attachment-only context.
    // This cannot reproduce the DirectWrite fallback resolver just by copying files.
    std::vector<fs::path> captures;
    for(auto& e:fs::directory_iterator(output/"selected"))if(e.is_regular_file())captures.push_back(e.path());
    for(auto& spec:cases){
        auto replay=spec;replay.id="reimport_"+spec.id;replay.provider=0;replay.fonts.clear();
        for(auto& path:captures){
            auto stem=path.stem().string();auto pos=stem.rfind('-');
            if(pos!=std::string::npos && stem.substr(0,pos)==spec.id)replay.fonts.push_back(path.generic_string());
        }
        if(!replay.fonts.empty()){
            auto c=create(replay,fixtures);render(c,replay.id);destroy(c);
        }
    }
    CoUninitialize();return 0;
}
