// Bounded diagnosis. Reuse the reviewed fixture/render/capture implementation.
#define main original_probe_main
#include "../probe.cpp"
#undef main

int main(int argc,char** argv) {
    if(argc!=5){std::cerr<<"diagnostic OUTPUT ARIAL YUGOTHIC EMOJI\n";return 2;}
    output=fs::u8path(argv[1]);
    for(auto dir:{"selected","metadata","renders"})fs::create_directories(output/dir);
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    std::vector<std::string> captured={argv[2],argv[3],argv[4]};
    for(auto item:std::vector<std::pair<std::string,std::string>>{
        {"full",u8"A中ع́😀"},{"ascii","A"},{"cjk",u8"中"},
        {"emoji",u8"😀"},{"arabic_combining",u8"ع́"}}) {
        for(int provider:{4,0}) {
            Case spec{item.first+(provider?"_directwrite":"_none"),"Arial",item.second,
                provider,0,0,provider?std::vector<std::string>{}:captured,"Arial"};
            auto c=create(spec,{});render(c,spec.id);destroy(c);
        }
    }
    // One-variable discriminators. Original ASS case and captured bytes stay intact.
    std::vector<std::string> reversed={argv[4],argv[3],argv[2]};
    std::vector<Case> variations={
        {"full_none_reverse","Arial",u8"A中ع́😀",0,0,0,reversed,"Arial"},
        {"full_directwrite_captured","Arial",u8"A中ع́😀",4,0,0,captured,"Arial"},
        {"cjk_none_default_yu","Arial",u8"中",0,0,0,captured,"Yu Gothic UI Semibold"},
        {"emoji_none_default_segoe","Arial",u8"😀",0,0,0,captured,"Segoe UI Emoji"},
        {"cjk_none_explicit_yu","Yu Gothic UI Semibold",u8"中",0,0,0,captured,"Arial"},
        {"emoji_none_explicit_segoe","Segoe UI Emoji",u8"😀",0,0,0,captured,"Arial"},
        {"full_none_default_yu","Arial",u8"A中ع́😀",0,0,0,captured,"Yu Gothic UI Semibold"},
        {"full_none_default_segoe","Arial",u8"A中ع́😀",0,0,0,captured,"Segoe UI Emoji"},
        // Deliberately authored alternate text, NOT a proposed document rewrite.
        {"full_none_authored_families","Arial",u8"A{\\fnYu Gothic UI Semibold}中{\\fnArial}ع́{\\fnSegoe UI Emoji}😀",
            0,0,0,captured,"Arial"}
    };
    for(auto spec:variations){auto c=create(spec,{});render(c,spec.id);destroy(c);}
    CoUninitialize();return 0;
}
