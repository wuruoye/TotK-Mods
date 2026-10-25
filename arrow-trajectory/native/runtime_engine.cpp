// Private copied-object adapter. Motion and collision geometry execute native ARM64.
#include "runtime_engine.hpp"
extern "C" {
u64 native_invoke(u64,u64=0,u64=0,u64=0,u64=0);
extern u64 native_checkpoint_sp;extern u32 native_failed;
extern NativeEngine* active;extern u32 busy;
void native_unbound();
}
static void copy(void*d,const void*s,u64 n){for(u64 i=0;i<n;i++)((u8*)d)[i]=((const u8*)s)[i];}
static void clear(void*d,u64 n){for(u64 i=0;i<n;i++)((u8*)d)[i]=0;}
template<class T> static T& at(void*p,u64 o=0){return *(T*)((u8*)p+o);}
static u8* a(u64 o){return active->arena+o;}
static u8* body(u32 n){return a(n?0x4000:0x2000);}
static int body_index(void*p){return p==body(0)?0:p==body(1)?1:-1;}
static void* result_body(void*){return body(0);}
static void* attack_body(void*,void*,void*){return body(1);}
static void* physics(void*){return a(0x8000);}
static u64 zero(){return 0;}
static u64 disabled(){return active->disabled;}
static void disable(void*,void*,void*,u32 flag){active->disabled=flag;}
static void* argument(){return a(0xB000);}
static void* blackboard(){return a(0xB400);}
static u32 absent(){return ~0u;}
static bool equal(const char*x,const char*y){for(u32 i=0;i<96;i++){if(x[i]!=y[i])return false;if(!x[i])return true;}return false;}
static u32 parameter(void*,const char**name){
    const char* keys[]={"ShootableArrow_TerminalSpeed","ShootableArrow_Range","ShootableArrow_Acceleration","ShootableArrow_FallAcceleration","ShootableArrow_FallSpeed","ShootableArrow_GravitationalAcceleration"};
    for(u32 i=0;i<6;i++)if(equal(*name,keys[i]))return i;
    native_failed=1;return ~0u;
}
struct Ref {u64 pointer=0,owner=0;~Ref(){}};
static Ref empty_ref(){return {};}
struct Group {u32 id;u8 enabled,pad[3];~Group(){}};
static Group group(){return {active->api.group,1,{0,0,0}};}
static void empty_iterator(void*p){clear(p,32);}
static void ref_copy(void*d,const void*s){
    if(s!=a(0x14400)){native_failed=1;return;}copy(d,s,24);
}
static void velocity(void*b,const NativeVec*v){
    if(body_index(b)<0){native_failed=1;return;}copy((u8*)b+0xF8,v,12);
    auto p=at<u8*>(b,0x60);if(p)at<u32>(p,0xD4)&=~128u;
}
static void angular(void*b,const NativeVec*v){
    if(body_index(b)<0){native_failed=1;return;}copy((u8*)b+0x104,v,12);
    auto p=at<u8*>(b,0x60);if(p)at<u32>(p,0xD4)&=~256u;
}
static void position(void*b,const NativeVec*v){
    int n=body_index(b);if(n<0){native_failed=1;return;}
    auto p=at<u8*>(b,0x60);if(p&&(at<u32>(p,0xD4)&64)){copy((u8*)b+0x98,p+8,48);at<u32>(p,0xD4)&=~64u;}
    at<float>(b,0xA4)=v->x;at<float>(b,0xB4)=v->y;at<float>(b,0xC4)=v->z;active->teleports[n]=1;
}
static void both(void*b,const NativeVec*v,const NativeVec*w){velocity(b,v);angular(b,w);}
static void* pending_request(void*,void*b,void*){
    if(body_index(b)<0){native_failed=1;return nullptr;}
    if(at<u64>(b,0x60))return at<void*>(b,0x60);
    auto p=(u8*)b+0x200;clear(p,0xE0);at<u64>(b,0x60)=(u64)p;at<u64>(p,0xC8)=(u64)b;
    at<u32>(p,0xBC)=~0u;for(u32 off=0x68;off<=0x80;off+=4)at<float>(p,off)=1;
    at<u64>(p,0x88)=0x1000000000ull;return p;
}
static void* copy_adapter(void*d,const void*s,u64 n){copy(d,s,n);return d;}
static void* fill_adapter(void*d,u32 value,u64 n){for(u64 i=0;i<n;i++)((u8*)d)[i]=(u8)value;return d;}
static float sine(float x){return ((float(*)(float))(active->api.base+0x2BB7440))(x);}
static float cosine(float x){return ((float(*)(float))(active->api.base+0x2BB7410))(x);}
static float arccos(float x){return ((float(*)(float))(active->api.base+0x2BB7450))(x);}
static double arccos_double(double x){return ((double(*)(double))(active->api.base+0x2BB7A10))(x);}
static u32 query_body_valid(void*q){
    // Original read-side body validity query; it takes/releases the physics
    // world read lock and does not invoke actor gameplay callbacks.
    return ((u32(*)(void*))(active->api.base+0x978FD0))(q);
}
extern "C" bool engine_sensor_valid(void*q){
    if(!at<u8>(q,0x20)||!at<u64>(q,0x168))return false;
    if((at<u8>(q,0x118)&8u)&&!query_body_valid(q))return false;
    float fraction=at<float>(q,0x3C);
    if(!(fraction>=0&&fraction<=1))return false;
    for(u32 i=0;i<3;i++){
        float value=at<float>(q,0x24+i*4);
        if(!(value>-1e7f&&value<1e7f))return false;
    }
    return true;
}
static u64 scene(void*q,u32){
    if(!active->api.scene){at<u8>(q,0x20)=0;return 0;}
    u64 result=((u64(*)(void*,void*))(active->api.scene))(q,(void*)active->api.scene_context);
    if(result&&at<u8>(q,0x20)){
        NativeHit&h=active->recent[active->hit_count++%8];copy(&h.point,(u8*)q+0x24,12);copy(&h.normal,(u8*)q+0x30,12);h.body=at<u64>(q,0x120);
    }
    return result;
}
static void* global(u64);
#include "runtime_bindings.inc"
static void clock(float dt){
    float values[]={dt,1/dt,dt/30,30/dt,dt,1/dt,dt/30,30/dt};copy(a(0x9000),values,sizeof(values));
    at<float>(a(0xA224))=dt/30;at<float>(a(0xA228))=30/dt;
}
static float world_step(const NativeEngine*e,float actor_step){
    float rate=*(const float*)(e->arena+0x1280);
    if(!__builtin_isfinite(rate)||rate<0)return 0;
    // Native arrow update uses 1 when the actor rate is zero (1D23C70..78).
    if(rate==0)rate=1;
    return actor_step/rate;
}
static void* global(u64 rva){
    if(rva>=0x395D000&&rva<0x395E000)return a(0x15000+rva-0x395D000);
    if(rva>=0x395F000&&rva<0x3960000)return a(0x16000+rva-0x395F000);
    if(rva>=0x3988000&&rva<0x3989000)return a(0x17000+rva-0x3988000);
    if(rva>=0x3AD2000&&rva<0x3AD3000)return a(0x18000+rva-0x3AD2000);
    if(rva>=0x3961000&&rva<0x3962000)return a(0x19000+rva-0x3961000);
    return (void*)(active->api.base+rva);
}
static void ptr(u64 rva,void*p){at<u64>(global(rva))=(u64)p;}
static void initialize_private_globals(){
    copy(a(0x15000),(void*)(active->api.base+0x395D000),0x1000);
    copy(a(0x16000),(void*)(active->api.base+0x395F000),0x1000);
    copy(a(0x17000),(void*)(active->api.base+0x3988000),0x1000);
    copy(a(0x18000),(void*)(active->api.base+0x3AD2000),0x1000);
    copy(a(0x19000),(void*)(active->api.base+0x3961000),0x1000);
    ptr(0x395DA70,a(0x9300));at<u64>(a(0x9300))=(u64)a(0x9000);
    ptr(0x395DAC0,a(0xA000));at<u64>(a(0xA000))=(u64)a(0xA100);at<u64>(a(0xA1C8))=(u64)a(0xA200);
    ptr(0x395DAB0,a(0xA300));ptr(0x395D988,a(0xE000));ptr(0x395F7A0,a(0x14400));
    ptr(0x3AD27C8,a(0xA100));ptr(0x3961BA8,(void*)(active->api.base+0x2CF6AE0));
    at<u32>(a(0x14410))=~0u;
    at<u64>(a(0xA118))=(u64)a(0x14000);
    for(u32 i=0;i<4;i++){at<u64>(a(0x14000),0x48+i*16)=(u64)a(0x14800+i*0x100);copy(a(0x14800+i*0x100),active->api.masks[i],0x100);}
    at<u64>(a(0x14448))=(u64)a(0x14500);
    const float identity[]={1,0,0,0,1,0,0,0,1};copy(a(0xE000),identity,sizeof(identity));
    bind_imports();
}
static bool enter(NativeEngine*e){if(__atomic_exchange_n(&busy,1,__ATOMIC_ACQUIRE))return false;active=e;native_failed=0;return true;}
static bool leave(){active->error=native_failed;active=nullptr;__atomic_store_n(&busy,0,__ATOMIC_RELEASE);return !native_failed;}
extern "C" void engine_record_contact(void*controller,const NativeVec*){
    if(controller!=a(0)){native_failed=1;return;}
    copy(&active->contact,a(0x120),12);
    for(u32 i=0;i<active->hit_count&&i<8;i++){
        auto&h=active->recent[(active->hit_count-1-i)%8];bool same=true;
        for(u32 j=0;j<3;j++){float delta=at<float>(&h.point,j*4)-at<float>(&active->contact,j*4);same=same&&delta<.001f&&delta>-.001f;}
        if(same){active->normal=h.normal;active->contact_body=h.body;active->hit=1;return;}
    }
    native_failed=1;
}
extern "C" bool arrow_native_initialize_impl(NativeEngine*e,const NativeApi*api,const NativeSeed*seed,const NativeLaunch*launch){
    if(!e||!api||!seed||!launch||!(launch->dt>0&&launch->dt<=2)||!enter(e))return false;
    clear(e,sizeof(*e));copy(&e->api,api,sizeof(*api));copy(a(0),seed->controller,0x240);copy(a(0x1000),seed->actor,0x340);
    at<u64>(a(0x18))=(u64)a(0x1000);at<u64>(a(0x50))=(u64)a(0xB100);
    for(u32 i=0;i<2;i++){
        auto b=body(i);copy(b,seed->bodies[i].raw,0x180);copy(b+0x200,seed->bodies[i].pending,0xE0);
        at<u64>(b,0x60)=seed->bodies[i].has_pending?(u64)(b+0x200):0;
        at<u64>(b,0x70)=(u64)(b+0x400);at<u64>(b,0x400)=(u64)(b+0x500);at<u64>(b,0x410)=(u64)(b+0x600);
        at<u64>(b,0x528)=(u64)&native_20D170;copy(b+0x620,&seed->bodies[i].local_center,12);
        at<u64>(b,0x148)=(u64)a(0x14200+i*0x40);copy(a(0x14200+i*0x40),api->body_filters[i],0x20);
    }
    float step=world_step(e,launch->dt);
    if(!(step>0&&step<=2)){native_failed=1;return leave();}
    initialize_private_globals();clock(step);
    copy(a(0xB07C),&launch->velocity,12);copy(a(0xB0A0),&launch->origin,12);at<u8>(a(0xB0F4))=9;
    at<u32>(a(0xB520))=6;at<u64>(a(0xB528))=(u64)a(0xBC00);copy(a(0xBC00),&launch->terminal,24);
    native_invoke((u64)&native_1D21FBC,(u64)a(0));if(!native_failed)e->magic=0x81343008;return leave();
}
static void advance_body(u32 n,float dt){
    auto b=body(n);auto p=a(0xC100+n*0x1000);auto motion=p+0x100;auto hbody=p+0x200;auto world=a(0x10000);
    // Commit queued game-body requests only to the private prediction copy.
    // Explicit native setters clear their own bits, so a queued older request
    // cannot overwrite a new velocity computed by this simulated update.
    auto pending=at<u8*>(b,0x60);
    if(pending){
        u32 flags=at<u32>(pending,0xD4);
        if(flags&64u){copy(b+0x98,pending+8,48);active->teleports[n]=1;}
        if(flags&128u)copy(b+0xF8,pending+0x44,12);
        if(flags&256u)copy(b+0x104,pending+0x50,12);
        at<u64>(b,0x60)=0;
    }
    if(!active->initialized[n]||active->teleports[n]){
        native_invoke((u64)&native_7C249C,(u64)motion,(u64)(b+0x98));
        native_invoke((u64)&native_29A8EAC,(u64)b,(u64)(p+0xA0));
        for(u32 j=0;j<3;j++)at<double>(motion,0x10+j*8)=at<float>(p,0xA0+j*4);
        active->initialized[n]=1;active->teleports[n]=0;
    }
    clear(hbody,0xC0);at<u64>(hbody,0x78)=1;at<float>(hbody,0xAC)=1;
    for(u32 col=0;col<3;col++){
        for(u32 row=0;row<3;row++)at<float>(hbody,col*16+row*4)=at<float>(b,0x98+row*16+col*4);
        at<float>(hbody,col*16+12)=at<float>(b,0x620+col*4);
    }
    clear(world,0xA00);at<u64>(world,0x38)=(u64)(hbody-0xC0);at<u64>(world,0x150)=(u64)(motion-0x80);
    at<u32>(hbody,0x54)=2;at<u32>(hbody,0x80)=1;clear(motion+0x70,16);at<float>(motion,0x7C)=1000;
    native_invoke((u64)&native_11C9A04,(u64)world,0x8000000000000001ull,(u64)(b+0x104),1);
    copy(p+0x80,b+0xF8,12);at<float>(p,0x8C)=0;at<float>(p,0x90)=dt/30;at<float>(p,0x94)=dt/30;
    native_invoke((u64)&native_11CDFC4,(u64)(p+0x80),(u64)(motion+0x70),(u64)(p+0x90),(u64)motion);
    native_invoke((u64)&native_11CE1E4,(u64)hbody,(u64)motion);
    for(u32 row=0;row<3;row++){
        for(u32 col=0;col<3;col++)at<float>(b,0x98+row*16+col*4)=at<float>(hbody,col*16+row*4);
        at<float>(b,0xA4+row*16)=at<float>(hbody,0x30+row*4);
    }
    at<u64>(b,0x60)=0;
}
extern "C" bool arrow_native_advance(NativeEngine*e,float dt,NativePoint*out){
    if(!e||e->magic!=0x81343008||!out||!(dt>0&&dt<=2)||e->hit||!enter(e))return false;
    clear(out,sizeof(*out));
    float step=world_step(e,dt);
    if(!(step>0&&step<=2)){native_failed=1;return leave();}
    // dt is the actor update interval. The native update divides it by the
    // actor's rate itself; world clocks and rigid-body integration use step.
    clock(step);at<float>(a(0xC080))=dt;
    native_invoke((u64)&native_1D23A4C,(u64)a(0),(u64)a(0xC080));
    if(native_failed)return leave();
    if(active->hit){out->origin=active->contact;out->attack_origin=active->contact;out->normal=active->normal;out->hit=1;out->body=active->contact_body;
        copy(&out->direction,a(0x114),12);out->speed=at<float>(a(0x108));out->mode=at<u32>(a(0x170));return leave();}
    advance_body(0,step);
    if(active->disabled){copy(body(1)+0x98,body(0)+0x98,48);at<u64>(body(1),0x60)=0;}
    else advance_body(1,step);
    for(u32 row=0;row<3;row++){
        at<float>(a(0x12B4),row*4)=at<float>(body(0),0xA4+row*16);
        for(u32 col=0;col<3;col++)at<float>(a(0x12C0),row*12+col*4)=at<float>(body(0),0x98+row*16+col*4);
    }
    copy(a(0x1320),body(0)+0xF8,12);copy(&out->origin,a(0x12B4),12);
    for(u32 row=0;row<3;row++)at<float>(&out->attack_origin,row*4)=at<float>(body(1),0xA4+row*16);
    copy(&out->direction,a(0x114),12);out->speed=at<float>(a(0x108));out->mode=at<u32>(a(0x170));out->error=native_failed;
    return leave();
}
