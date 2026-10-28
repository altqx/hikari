/* Original throwaway Windows C helper. Native Lua callbacks never enter Python.
 * Protocol v1: LE length, version/type u16, generation/run/request u32,
 * then bounded tagged Lua values. Not an accepted production protocol/ABI. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#define LIMIT 65536
enum {HELLO=1, START=2, DIALOG=3, REPLY=4, RESULT=5, CANCEL=6, PROGRESS=7,
      STOP=8, ERROR_MSG=9, REJECT=11};
typedef struct { unsigned char b[LIMIT]; uint32_t n,p; int bad; } Buffer;
static HANDLE pipe_handle;
static uint32_t generation, run_id, request_id;
static int cancelled;
static char failure[120];
static int exact(void *buf, DWORD count, int writing) {
    unsigned char *p=(unsigned char *)buf; DWORD done;
    while(count) {
        BOOL ok=writing?WriteFile(pipe_handle,p,count,&done,NULL):ReadFile(pipe_handle,p,count,&done,NULL);
        if(!ok || !done) return 0;
        p+=done;count-=done;
    } return 1;
}
static void put(Buffer *b,const void *p,uint32_t n){if(n>LIMIT-b->n){b->bad=1;return;}memcpy(b->b+b->n,p,n);b->n+=n;}
static void u8(Buffer*b,unsigned char v){put(b,&v,1);}
static void u16(Buffer*b,uint16_t v){put(b,&v,2);}
static void u32(Buffer*b,uint32_t v){put(b,&v,4);}
static void get(Buffer*b,void*p,uint32_t n){if(n>b->n-b->p){b->bad=1;memset(p,0,n);return;}memcpy(p,b->b+b->p,n);b->p+=n;}
static uint32_t get32(Buffer*b){uint32_t v;get(b,&v,4);return v;}
static int idx(lua_State*L,int i){return i>0?i:lua_gettop(L)+i+1;}
static void encode(lua_State*L,int i,Buffer*b,int depth){
    size_t len; const char*s; double number; uint32_t count=0,mark;
    if(depth>12||!lua_checkstack(L,8)){b->bad=1;return;} i=idx(L,i);
    switch(lua_type(L,i)){
    case LUA_TNIL:u8(b,0);break;
    case LUA_TBOOLEAN:u8(b,lua_toboolean(L,i)?2:1);break;
    case LUA_TNUMBER:u8(b,3);number=lua_tonumber(L,i);put(b,&number,8);break;
    case LUA_TSTRING:s=lua_tolstring(L,i,&len);if(len>LIMIT){b->bad=1;return;}u8(b,4);u32(b,(uint32_t)len);put(b,s,(uint32_t)len);break;
    case LUA_TTABLE:
        u8(b,5);mark=b->n;u32(b,0);lua_pushnil(L);
        while(lua_next(L,i)){
            if(++count>128){lua_pop(L,2);b->bad=1;return;}
            encode(L,-2,b,depth+1);encode(L,-1,b,depth+1);lua_pop(L,1);
        } if(!b->bad)memcpy(b->b+mark,&count,4);break;
    default:b->bad=1;
    }
}
static void decode(lua_State*L,Buffer*b,int depth){
    unsigned char type;double number;uint32_t n,k;int top=lua_gettop(L);
    if(depth>12||!lua_checkstack(L,8)){b->bad=1;return;}get(b,&type,1);if(b->bad)return;
    switch(type){
    case 0:lua_pushnil(L);break;
    case 1:case 2:lua_pushboolean(L,type==2);break;
    case 3:get(b,&number,8);if(!b->bad)lua_pushnumber(L,number);break;
    case 4:n=get32(b);if(b->bad||n>b->n-b->p){b->bad=1;break;}lua_pushlstring(L,(char*)b->b+b->p,n);b->p+=n;break;
    case 5:
        n=get32(b);if(n>128){b->bad=1;break;}lua_newtable(L);
        for(k=0;k<n&&!b->bad;k++){
            decode(L,b,depth+1);if(b->bad)break;
            if(!(lua_type(L,-1)==LUA_TSTRING||lua_type(L,-1)==LUA_TNUMBER)){b->bad=1;break;}
            decode(L,b,depth+1);if(!b->bad)lua_rawset(L,-3);
        }break;
    default:b->bad=1;
    }if(b->bad)lua_settop(L,top);
}
static int send_value(lua_State*L,int type,uint32_t request,int value){
    Buffer b={0};u16(&b,1);u16(&b,(uint16_t)type);u32(&b,generation);u32(&b,run_id);u32(&b,request);
    encode(L,value,&b,0);if(b.bad)return 0;return exact(&b.n,4,1)&&exact(b.b,b.n,1);
}
static void notice(lua_State*L,int type,const char*message){
    lua_pushstring(L,message);send_value(L,type,0,-1);lua_pop(L,1);
}
/* Returns one decoded value on success. Failure never unwinds into Python. */
static int receive(lua_State*L,uint16_t*type,uint32_t*gen,uint32_t*run,uint32_t*request){
    Buffer b={0};uint16_t version;failure[0]=0;
    if(!exact(&b.n,4,0)){strcpy(failure,"pipe EOF before frame header");return 0;}
    if(b.n<17||b.n>LIMIT){strcpy(failure,"invalid bounded frame length");return 0;}
    if(!exact(b.b,b.n,0)){strcpy(failure,"truncated frame body");return 0;}
    get(&b,&version,2);get(&b,type,2);*gen=get32(&b);*run=get32(&b);*request=get32(&b);
    if(version!=1){strcpy(failure,"protocol version mismatch");return 0;}
    decode(L,&b,0);
    if(b.bad||b.p!=b.n){strcpy(failure,"invalid typed value or trailing bytes");return 0;}
    return 1;
}
static int dialog(lua_State*L){
    uint16_t type;uint32_t gen,run,request;uint32_t own=++request_id;
    if(lua_gettop(L)==1)lua_newtable(L);
    lua_settop(L,2); /* Current entry truncates the third button-ID argument. */
    if(!lua_istable(L,1)||!lua_istable(L,2))return luaL_error(L,"fixture expects control/button tables");
    lua_newtable(L);lua_pushvalue(L,1);lua_setfield(L,-2,"controls");lua_pushvalue(L,2);lua_setfield(L,-2,"buttons");
    if(!send_value(L,DIALOG,own,-1))return luaL_error(L,"bounded dialog encode/write failed");lua_pop(L,1);
    for(;;){
        if(!receive(L,&type,&gen,&run,&request))return luaL_error(L,"%s",failure);
        if(gen!=generation||run!=run_id){lua_pop(L,1);notice(L,REJECT,"stale generation/run while waiting");continue;}
        if(type==CANCEL){cancelled=1;lua_pop(L,1);continue;}
        if(type!=REPLY||request!=own){lua_pop(L,1);notice(L,REJECT,"unexpected/duplicate dialog reply");continue;}
        if(!lua_istable(L,-1))return luaL_error(L,"reply must be a typed table");
        lua_getfield(L,-1,"button");lua_getfield(L,-2,"values");
        if(!(lua_isboolean(L,-2)||lua_type(L,-2)==LUA_TSTRING)||!lua_istable(L,-1))return luaL_error(L,"invalid dialog return types");
        return 2;
    }
}
static int is_cancelled(lua_State*L){
    DWORD available=0;uint16_t type;uint32_t gen,run,req;
    if(PeekNamedPipe(pipe_handle,NULL,0,NULL,&available,NULL)&&available>=4){
        if(!receive(L,&type,&gen,&run,&req))return luaL_error(L,"%s",failure);
        if(gen==generation&&run==run_id&&type==CANCEL)cancelled=1;
        else if(gen==generation&&run==run_id&&type==REPLY&&req<=request_id)notice(L,REJECT,"duplicate dialog reply at polling boundary");
        else notice(L,REJECT,"stale/unexpected polling message");
        lua_pop(L,1);
    }lua_pushboolean(L,cancelled);return 1;
}
static int pause_fixture(lua_State*L){Sleep((DWORD)luaL_checkinteger(L,1));return 0;}
static int progress(lua_State*L){send_value(L,PROGRESS,0,1);return 0;}
static int abrupt_exit(lua_State*L){(void)L;TerminateProcess(GetCurrentProcess(),77);return 0;}
static void fn(lua_State*L,const char*name,lua_CFunction f){lua_pushcfunction(L,f);lua_setfield(L,-2,name);}
static int load(lua_State*L,const char*path){return luaL_loadfile(L,path)||lua_pcall(L,0,0,0);}
static char*utf8(const wchar_t*s){int n=WideCharToMultiByte(CP_UTF8,0,s,-1,NULL,0,NULL,NULL);char*p=malloc(n);WideCharToMultiByte(CP_UTF8,0,s,-1,p,n,NULL,NULL);return p;}
int wmain(int argc,wchar_t**argv){
    lua_State*L;uint16_t type;uint32_t gen,run,req;char*fixture,*edge;
    if(argc!=5)return 10;
    setvbuf(stdout,NULL,_IONBF,0);setvbuf(stderr,NULL,_IONBF,0);
    generation=(uint32_t)wcstoul(argv[2],NULL,10);
    pipe_handle=CreateFileW(argv[1],GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);
    if(pipe_handle==INVALID_HANDLE_VALUE){fprintf(stderr,"pipe open failed %lu\n",GetLastError());return 11;}
    L=luaL_newstate();if(!L)return 12;luaL_openlibs(L);
    lua_newtable(L);fn(L,"dialog",dialog);fn(L,"is_cancelled",is_cancelled);fn(L,"sleep",pause_fixture);fn(L,"progress",progress);fn(L,"abrupt_exit",abrupt_exit);lua_setglobal(L,"host");
    fixture=utf8(argv[3]);edge=utf8(argv[4]);
    if(load(L,fixture)||load(L,edge)){fprintf(stderr,"load failed: %s\n",lua_tostring(L,-1));return 13;}free(fixture);free(edge);
    lua_getglobal(L,"runtime_identity");if(lua_pcall(L,0,1,0)){fprintf(stderr,"runtime failed: %s\n",lua_tostring(L,-1));return 14;}
    if(!send_value(L,HELLO,0,-1))return 15;lua_settop(L,0);
    printf("C_STDOUT helper ready; this channel is not protocol\n");
    for(;;){
        if(!receive(L,&type,&gen,&run,&req)){fprintf(stderr,"PROTOCOL_FAILURE: %s\n",failure);notice(L,ERROR_MSG,failure);break;}
        if(gen!=generation){lua_pop(L,1);notice(L,REJECT,"stale generation at dispatcher");continue;}
        if(type==STOP){lua_pop(L,1);lua_close(L);CloseHandle(pipe_handle);return 0;}
        if(type!=START||!lua_istable(L,-1)){lua_pop(L,1);notice(L,REJECT,"unexpected/duplicate dispatcher message");continue;}
        run_id=run;cancelled=0;lua_getfield(L,-1,"mode");
        if(lua_type(L,-1)!=LUA_TSTRING){lua_settop(L,0);notice(L,ERROR_MSG,"mode must be a string");continue;}
        lua_getglobal(L,"run_fixture");lua_pushvalue(L,-2);
        if(lua_pcall(L,1,1,0))send_value(L,ERROR_MSG,0,-1);else send_value(L,RESULT,0,-1);
        lua_settop(L,0);
    }
    lua_close(L);CloseHandle(pipe_handle);return 21;
}
