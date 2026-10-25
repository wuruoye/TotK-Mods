#include "trajectory.hpp"
#include "native_guards.hpp"
#include "native_component.hpp"

static float absf(float x) { return x<0?-x:x; }
static bool finite(float x) { union {float f;u32 u;} v{x};return (v.u&0x7f800000u)!=0x7f800000u; }
static bool finite3(Vec3 v) {return finite(v.x)&&finite(v.y)&&finite(v.z);}
static Vec3 add(Vec3 a,Vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
static Vec3 sub(Vec3 a,Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
static Vec3 mul(Vec3 a,float s){return {a.x*s,a.y*s,a.z*s};}
static float dot(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
static float length(Vec3 v){return __builtin_sqrtf(dot(v,v));}
static Vec3 unit(Vec3 v){float l=length(v);return l>1e-7f?mul(v,1.0f/l):Vec3{};}
static Vec3 cross(Vec3 a,Vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
static float clamp(float x,float a,float b){return x<a?a:x>b?b:x;}
static void copy(void* dst,const void* src,u32 bytes){auto d=(u8*)dst;auto s=(const u8*)src;for(u32 i=0;i<bytes;i++)d[i]=s[i];}
extern "C" void* memcpy(void* d,const void* s,uptr n){copy(d,s,(u32)n);return d;}
extern "C" void* memset(void* d,int value,uptr n){for(uptr i=0;i<n;i++)((u8*)d)[i]=(u8)value;return d;}
template<class T> static T read(uptr p,uptr off=0){T out;copy(&out,(void*)(p+off),sizeof(T));return out;}
template<class F> static F call(Native*n,uptr rva){return reinterpret_cast<F>(n->base+rva);}
static bool pointer(uptr p){return p>=0x10000 && p<(1ull<<39) && !(p&7);}
static u32 get(const u32* p){return __atomic_load_n(p,__ATOMIC_ACQUIRE);}
static void put(u32*p,u32 x){__atomic_store_n(p,x,__ATOMIC_RELEASE);}
static bool lock(State*s){u32 expected=0;return __atomic_compare_exchange_n(&s->lock,&expected,1,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED);}
static void unlock(State*s){put(&s->lock,0);}
static bool equal(const char* a,const char*b){if(!a)return false;for(u32 i=0;i<64;i++){if(a[i]!=b[i])return false;if(!b[i])return true;}return false;}
static bool bow_name(const char*a){constexpr char prefix[]="Weapon_Bow_";if(!a)return false;for(u32 i=0;i<sizeof(prefix)-1;i++)if(a[i]!=prefix[i])return false;return true;}
static u32 invalidate(State*s,Reason reason){u32 e=__atomic_add_fetch(&s->epoch,1,__ATOMIC_ACQ_REL);put(&s->reason,reason);return e;}
static bool native_guard(Native*n,const Guard&g){
    u32 first=read<u32>(n->base+g.rva);
    if(first!=g.words[0]){
        if(g.rva!=0x1D23A4C)return false;
        uptr module=(uptr)n-0xE100,entry=n->base+g.rva,stub=module+0x2800;
        u32 expected=0x14000000u|(u32)(((stub-entry)>>2)&0x3FFFFFF);
        if(first!=expected||read<u32>(stub+0x300)!=g.words[0])return false;
    }
    for(u32 i=1;i<4;i++)if(read<u32>(n->base+g.rva,i*4)!=g.words[i])return false;
    return true;
}
static void initialize(Native*n,State*s){if(get(&s->magic)==0x31525441u)return;bool ok=true;
    for(const auto&g:kNativeGuards)if(!native_guard(n,g))ok=false;
    put(&s->enabled,ok?1:0);put(&s->magic,0x31525441u);}

extern "C" bool trajectory_valid(const Params*p){
    const float* f=&p->initial;for(u32 i=0;i<9;i++)if(!finite(f[i]))return false;
    return p->initial>0.1f&&p->initial<=12&&p->terminal>0&&p->terminal<=12&&p->acceleration>=-1&&p->acceleration<=0&&
        p->fall_acceleration>=-2&&p->fall_acceleration<=0&&p->fall_speed>=0&&p->fall_speed<=8&&p->gravity<=0&&p->gravity>=-30&&
        p->range>0&&p->range<=2000&&p->step>=0.01f&&p->step<=1.01f&&p->defer_scale>0&&p->defer_scale<=1.01f;
}
extern "C" void trajectory_motion_init(Motion*m,const Request*r){
    // Native player arrows (shootable type 9) retain a separate launch reference.
    // Their bow-to-reference offset converges at |offset| / 7 per 30 Hz unit,
    // while forward motion is active. The remaining offset freezes on falling.
    *m={r->launch_origin,r->origin,unit(r->velocity),length(r->velocity),r->params.acceleration,r->params.terminal,0,0,(r->flags&IgnoreGravity)?500:r->params.range,
        mul(sub(r->launch_origin,r->origin),1.0f/7.0f),7.0f,{},0};
    if(absf(m->acceleration)<=1.1920929e-7f)m->terminal=m->speed;
}
extern "C" void trajectory_fall_start(Motion*m,const Params*p){
    m->falling=1;
    m->terminal=clamp(p->fall_speed+p->range/20.0f-1.0f,0.0f,p->fall_speed);
    m->acceleration=absf(p->fall_acceleration);
    if(m->acceleration<=1.1920929e-7f)m->terminal=m->speed;
    if(absf(m->terminal)<=1.1920929e-7f)m->terminal=absf(p->gravity/900.0f);
}
extern "C" void trajectory_motion_step(Motion*m,const Params*p){
    bool was_falling=m->falling!=0;
    m->speed=__builtin_fmaf(m->acceleration,p->step,m->speed);
    if(m->acceleration<0&&m->speed<m->terminal)m->speed=m->terminal;
    if(m->acceleration>0&&m->speed>m->terminal)m->speed=m->terminal;
    Vec3 velocity=mul(m->direction,m->speed);
    if(m->falling){velocity.y+=p->gravity/900.0f*p->step;m->direction=unit(velocity);}
    // The native range test reads the attack body's reference position;
    // the visible/main body still carries the converging bow offset.
    else if(length(sub(add(m->position,mul(m->convergence_velocity,m->convergence_time)),m->origin))>=(m->flight_range>0?m->flight_range:p->range)){
        if(!m->delayed&&p->defer_scale<=0.5f)m->delayed=1;
        else trajectory_fall_start(m,p);
    }
    // Forward motion writes the attack body; falling writes the main body.
    // On the transition frame the native main-body correction is skipped,
    // so that body retains the preceding frame's velocity for one update.
    if(!was_falling&&m->falling&&m->has_previous)velocity=m->previous_velocity;
    if(!m->falling&&m->convergence_time>0){
        float dt=p->step<m->convergence_time?p->step:m->convergence_time;
        velocity=add(velocity,mul(m->convergence_velocity,dt/p->step));m->convergence_time-=dt;
    }
    m->position=add(m->position,mul(velocity,p->step));m->previous_velocity=velocity;m->has_previous=1;
}
extern "C" u32 trajectory_predict(const Request*r,Curve*out){
    *out={};out->epoch=r->epoch;out->frame=r->frame;
    if(!trajectory_valid(&r->params)||!finite3(r->origin)||!finite3(r->launch_origin)||!finite3(r->velocity)||length(r->velocity)<0.1f)return 0;
    Motion m{};trajectory_motion_init(&m,r);out->points[0]=r->origin;out->count=1;
    out->direction=m.direction;out->flags=r->flags;
    // Homing/floating futures depend on state not present in the aim snapshot.
    // Only show a short launch-direction guide, without a claimed impact.
    const bool initial_only=(r->flags&(Homing|SpecialMotion))!=0;
    float elapsed=0;Vec3 last_dir=m.direction;
    for(u32 i=0;i<kMaxSteps;i++){
        trajectory_motion_step(&m,&r->params);elapsed+=r->params.step;
        if(!finite3(m.position)||!finite3(m.direction))return 0;
        Vec3 span=sub(m.position,out->points[out->count-1]);
        const float distance=length(span);
        // Short straight segments and tighter corners bound geometric error without increasing physics dt.
        if(initial_only&&length(sub(m.position,r->origin))>=5.0f){
            out->points[out->count++]=add(r->origin,mul(unit(r->velocity),5.0f));break;
        }
        if(distance>=clamp(r->params.range/48,1.5f,5.0f)||dot(last_dir,m.direction)<0.9998f||elapsed>=240.0f){
            out->points[out->count++]=m.position;last_dir=m.direction;
            if(out->count==kSegments+1||elapsed>=240.0f)break;
        }
    }
    return out->count;
}
// Native 1.4.3 uses the world-Y axis and alternating side indices; the
// prepared center arrow corresponds to index -1, additional arrows to 0..N-2.
extern "C" Vec3 trajectory_spread(Vec3 v,float degrees,u32 count,u32 shot){
    if(!shot||count<2)return v;
    float a=degrees*0.0174532925199433f/(float)(count/2+1)*(float)((shot+1)/2);
    if(shot&1)a=-a;
    // Bounded to 45 degrees by capture. Taylor error is below float precision here.
    float aa=a*a,si=a*(1-aa/6+aa*aa/120-aa*aa*aa/5040);
    float co=1-aa/2+aa*aa/24-aa*aa*aa/720+aa*aa*aa*aa/40320;
    return {co*v.x+si*v.z,v.y,co*v.z-si*v.x};
}
extern "C" void trajectory_stabilize(Curve*c,Stable*s,float dt){
    if(!c->complete||!c->hit||c->count<2||c->count>kSegments+1){s->valid=0;return;}
    Vec3 hit=c->points[c->count-1],origin=c->points[0],normal=c->normal;
    bool same=s->valid&&s->epoch==c->epoch&&s->body==c->body&&c->body!=~0ull&&
        c->frame-s->frame<=3&&s->flags==c->flags&&dot(s->normal,normal)>.995f&&
        dot(s->direction,c->direction)>.99998f&&length(sub(origin,s->origin))<.08f;
    Vec3 delta=sub(s->point,hit);
    // Never drag the marker between surfaces, across a corner, or after a fast turn.
    if(same&&length(delta)<.25f&&absf(dot(delta,normal))<.02f){
        delta=sub(delta,mul(normal,dot(delta,normal)));
        float alpha=c->frame==s->frame?0:clamp(dt/(.075f+dt),.08f,1.0f);
        if(length(delta)<.008f)alpha=0;
        Vec3 correction=mul(delta,1-alpha);
        for(u32 i=1;i<c->count;i++){
            float t=(float)i/(float)(c->count-1);
            c->points[i]=add(c->points[i],mul(correction,t*t));
        }
    }
    *s={c->epoch,c->frame,1,c->flags,c->body,c->points[c->count-1],normal,origin,c->direction};
}
extern "C" bool trajectory_segment_hit(const Vec3*a,const Vec3*b,const Vec3*h){
    if(!finite3(*h))return false;Vec3 d=sub(*b,*a);float ds=dot(d,d);if(ds<1e-10f)return false;
    float t=dot(sub(*h,*a),d)/ds;if(t<-.001f||t>1.001f)return false;
    return length(sub(*h,add(*a,mul(d,t))))<0.05f;
}
extern "C" bool trajectory_project(const float*v,const float*p,const Vec3*in,Vec3*out){
    Vec3 a{v[0]*in->x+v[1]*in->y+v[2]*in->z+v[3],v[4]*in->x+v[5]*in->y+v[6]*in->z+v[7],v[8]*in->x+v[9]*in->y+v[10]*in->z+v[11]};
    float w=p[12]*a.x+p[13]*a.y+p[14]*a.z+p[15];if(!finite(w)||w<=0.001f)return false;
    *out={(p[0]*a.x+p[1]*a.y+p[2]*a.z+p[3])/w,(p[4]*a.x+p[5]*a.y+p[6]*a.z+p[7])/w,-a.z};return finite3(*out)&&out->z>0;
}

// Non-trivial return types intentionally use the engine's X8 result ABI.
struct Reference { uptr actor=0;u8 owns=0;u8 pad[7]{};~Reference(){} };
struct NativeGroup:Group {~NativeGroup(){} };
struct Quaternion {float x,y,z,w;};
static_assert(sizeof(Reference)==16&&sizeof(NativeGroup)==8);
static void release_ref(Native*n,Reference*r){call<void(*)(Reference*)>(n,0xC7B9EC)(r);r->owns=0;}
static bool player_group(Native*n,uptr actor,Group&out){
    constexpr uptr offsets[]={0x228ull,0x50ull,0x20ull,0x10ull,8ull,0x18ull};
    uptr p=actor;for(uptr off:offsets){p=read<uptr>(p,off);if(!pointer(p))return false;}
    NativeGroup g=call<NativeGroup(*)(uptr)>(n,0x1CFB0D0)(actor);out=g;return out.enabled!=0;
}
static bool attachment_params(Native*n,uptr link,Request&r){
    if(!pointer(link))return false;
    if(read<u32>(link,0x10)==0xFFFFFFFFu)return true;
    Reference ar=call<Reference(*)(uptr)>(n,0x1AFBE4)(link);
    bool ok=false;
    if(pointer(ar.actor)){
        r.attachment=ar.actor;
        uptr table=read<uptr>(ar.actor,0x228),component=pointer(table)?read<uptr>(table,0x120):0;
        if(pointer(component)&&read<uptr>(component,0x18)==ar.actor){
            auto tag=[&](u32 id){return call<bool(*)(uptr,u32)>(n,0x1B2428C)(component,id);};
            float scale=call<float(*)(uptr)>(n,0x1B25C90)(component);
            ok=finite(scale)&&scale>0&&scale<=5;
            r.params.range*=scale;
            if(tag(0x27))r.flags|=Homing;
            if(tag(0x2B))r.flags|=IgnoreGravity;
            if(tag(0x16)||tag(0x42))r.flags|=SpecialMotion;
        }
    }
    release_ref(n,&ar);return ok;
}
static u64 native_scene(void*query,void*context){
    State*s=(State*)context;__atomic_add_fetch(&s->queries,1,__ATOMIC_RELAXED);
    return ((u64(*)(void*,u32))(s->native_api.base+0x1952A4))(query,0);
}
static bool capture_native(Native*n,const Request&r,NativeSeed&seed,NativeApi&api){
    if(r.shots!=1||r.attachment||r.flags)return false;
    for(const auto&g:kCopiedNativeGuards)if(!native_guard(n,g))return false;
    // An installed UltraCam gate may zero angular velocity. Reject that mode.
    u32 first=read<u32>(n->base,0x11C9A04);
    if(first!=0xD102C3FFu){
        if((first&0xFC000000u)!=0x14000000u)return false;
        int displacement=(int)(first<<6)>>4;uptr wrapper=n->base+0x11C9A04+displacement;
        for(u32 i=0;i<6;i++)if(read<u32>(wrapper,i*4)!=kAngularWrapper[i])return false;
        u32 branch=read<u32>(wrapper,0x18);if((branch&0xFC000000u)!=0x94000000u)return false;
        displacement=(int)(branch<<6)>>4;uptr predicate=wrapper+0x18+displacement;
        for(u32 i=0;i<6;i++)if(read<u32>(predicate,i*4)!=kAngularGate[i])return false;
        if(((bool(*)())predicate)())return false;
    }
    uptr table=read<uptr>(r.arrow,0x228);if(!pointer(table))return false;
    uptr shootable=read<uptr>(table,0x428);if(!pointer(shootable)||read<uptr>(shootable,0x18)!=r.arrow)return false;
    uptr controller=read<uptr>(shootable,0xD0);if(!pointer(controller)||read<uptr>(controller,0x18)!=r.arrow||read<uptr>(controller)!=n->base+0x3693970)return false;
    const char* attack_name=(const char*)(n->base+0x2C779DB);const char* attack_group=(const char*)(n->base+0x2C4BDC2);
    uptr bodies[]={call<uptr(*)(uptr)>(n,0x98B050)(r.arrow),call<uptr(*)(uptr,const char**,const char**)>(n,0x2B490A4)(r.arrow,&attack_name,&attack_group)};
    if(!pointer(bodies[0])||!pointer(bodies[1])||bodies[0]==bodies[1])return false;
    copy(seed.controller,(void*)controller,sizeof(seed.controller));copy(seed.actor,(void*)r.arrow,sizeof(seed.actor));
    for(u32 i=0;i<2;i++){
        uptr b=bodies[i],shape=read<uptr>(b,0x70);if(!pointer(shape))return false;
        uptr vt=read<uptr>(shape);if(!pointer(vt)||read<uptr>(vt,0x28)!=n->base+0x20D170)return false;
        uptr prop=read<uptr>(shape,0x10);if(prop&&!pointer(prop))return false;
        uptr pending=read<uptr>(b,0x60);if(pending&&!pointer(pending))return false;
        copy(seed.bodies[i].raw,(void*)b,sizeof(seed.bodies[i].raw));seed.bodies[i].has_pending=pending?1:0;
        if(pending)copy(seed.bodies[i].pending,(void*)pending,sizeof(seed.bodies[i].pending));
        seed.bodies[i].local_center=prop?read<NativeVec>(prop,0x20):NativeVec{};
        uptr filter=read<uptr>(b,0x148);if(!pointer(filter))return false;
        copy(api.body_filters[i],(void*)filter,0x20);
    }
    NativeGroup g=call<NativeGroup(*)(uptr)>(n,0xB694D0)(bodies[0]);if(!g.enabled)return false;
    api.group=g.id;api.base=n->base;api.scene=(uptr)&native_scene;
    uptr singleton=read<uptr>(n->base,0x395DAC0);if(!pointer(singleton))return false;
    uptr world=read<uptr>(singleton);if(!pointer(world))return false;
    uptr filters=read<uptr>(world,0x18);if(!pointer(filters))return false;
    for(u32 i=0;i<4;i++){uptr at=read<uptr>(filters,0x48+i*0x10);if(!pointer(at))return false;copy(api.masks[i],(void*)at,0x100);}
    return true;
}
static bool predict_native(Native*n,State*s,const Request&r,Curve&curve){
    const uptr module=(uptr)n-0xE100,component=module+0x10000;
    if(!s->native_bound){
        if(!((bool(*)(uptr))(component+kNativeBind))((uptr)s-0x20000+0x10000))return false;
        s->native_bound=1;
    }
    NativeLaunch input{{r.launch_origin.x,r.launch_origin.y,r.launch_origin.z},{r.velocity.x,r.velocity.y,r.velocity.z},
        r.params.terminal,r.params.range,r.params.acceleration,r.params.fall_acceleration,r.params.fall_speed,r.params.gravity,r.params.step};
    s->native_api.scene_context=(uptr)s;
    if(!((bool(*)(NativeEngine*,const NativeApi*,const NativeSeed*,const NativeLaunch*))(component+kNativeInitialize))(&s->native_engine,&s->native_api,&s->native_seed,&input))return false;
    curve={};curve.epoch=r.epoch;curve.frame=r.frame;curve.points[0]=r.origin;curve.count=1;curve.direction=unit(r.velocity);
    NativePoint point{};float elapsed=0,spacing=1.5f;Vec3 last_direction=curve.direction;
    for(u32 step=0;step<1024&&elapsed<240;step++){
        if(r.epoch!=get(&s->epoch))return false;
        if(!((bool(*)(NativeEngine*,float,NativePoint*))(component+kNativeAdvance))(&s->native_engine,r.params.step,&point))return false;
        Vec3 pos{point.origin.x,point.origin.y,point.origin.z},direction{point.direction.x,point.direction.y,point.direction.z};
        if(!finite3(pos)||!finite3(direction))return false;elapsed+=r.params.step;
        if(point.hit||length(sub(pos,curve.points[curve.count-1]))>=spacing||dot(direction,last_direction)<.9998f||elapsed>=240){
            curve.points[curve.count++]=pos;last_direction=direction;
            if(point.hit){curve.hit=1;curve.body=point.body;curve.normal={point.normal.x,point.normal.y,point.normal.z};
                if(!finite3(curve.normal)||length(curve.normal)<.5f)return false;curve.normal=unit(curve.normal);break;}
            if(curve.count==kSegments+1){
                // Drawing storage must not stop the original collision simulation.
                for(u32 i=1;i<=kSegments/2;i++)curve.points[i]=curve.points[i*2];
                curve.count=kSegments/2+1;spacing*=2;
            }
        }
    }
    curve.complete=1;return curve.count>=2;
}
static bool capture(Native*n,State*s,uptr action,Request&r){
    s->last_action=action;s->capture_stage=1;
    if(!pointer(action)){put(&s->reason,NotAiming);return false;}
    s->action_f0=read<u32>(action,0xF0);s->action_f4=read<u32>(action,0xF4);s->action_f8=read<u32>(action,0xF8);
    if(s->action_f0!=0||s->action_f4>1||s->action_f8!=2){put(&s->reason,NotAiming);return false;}
    s->capture_stage=2;
    uptr actor=call<uptr(*)(uptr)>(n,0x6BC918)(action);
    s->last_actor=actor;
    if(!pointer(actor)||!equal(read<const char*>(actor,0x218),"Player"))return false;
    s->capture_stage=3;
    Vec3 movement=read<Vec3>(actor,0x320);float rate=read<float>(actor,0x280);
    // Bullet time slows the world while Player's relative rate can be 20.
    // The native launch getter already accounts for the shooter; this rate is
    // only validated here and must not be capped at normal-time 1.0.
    if(!finite3(movement)||length(movement)>100||!finite(rate)||rate<0){put(&s->reason,MotionUnsupported);return false;}
    uptr table=read<uptr>(actor,0x228);if(!pointer(table))return false;
    uptr equip=read<uptr>(table,0x230);if(!pointer(equip))return false;
    if(read<uptr>(equip,0x18)!=actor)return false;
    s->capture_stage=4;
    Reference bowref=call<Reference(*)(uptr)>(n,0x1AFBE4)(equip+0x68);
    bool valid=false;
    if(pointer(bowref.actor)){
        s->last_bow=bowref.actor;s->capture_stage=5;
        uptr bt=read<uptr>(bowref.actor,0x228),bow=pointer(bt)?read<uptr>(bt,0x150):0;
        if(pointer(bow)&&read<uptr>(bow,0x18)==bowref.actor&&bow_name(read<const char*>(bowref.actor,0x218))&&read<u32>(bow,0x650)<=kShots){
            u32 shots=read<u32>(bow,0x650);u8 flags=read<u8>(bow,0x654);
            r.shots=(flags&1)&&shots>1?shots:1;
            if(flags&2)r.flags|=SpecialMotion;
            r.shot_angle=r.shots>1?call<float(*)(uptr)>(n,0x1B07EF4)(bow):0;
            if(!finite(r.shot_angle)||r.shot_angle<0||r.shot_angle>45){put(&s->reason,UnsupportedBow);release_ref(n,&bowref);return false;}
            uptr attachment=call<uptr(*)(uptr)>(n,0x4DAA94)(equip);
            s->capture_stage=6;
            if(!pointer(attachment))put(&s->reason,Attachment);
            else{
                uptr link=call<uptr(*)(uptr)>(n,0x4DAE80)(equip);
                if(pointer(link)){
                    s->capture_stage=7;
                    Reference ar=call<Reference(*)(uptr)>(n,0x1AFBE4)(link);
                    s->last_arrow=ar.actor;
                    if(pointer(ar.actor)&&equal(read<const char*>(ar.actor,0x218),"NormalArrow")){
                        uptr clockslot=read<uptr>(n->base,0x395DA70),clock=pointer(clockslot)?read<uptr>(clockslot):0;
                        if(pointer(clock)){
                            s->capture_stage=8;
                            auto f=[&](uptr at){return call<float(*)(uptr)>(n,at)(bow);};
                            r.params={f(0x1B2CE2C),f(0x1B2D204),f(0x1B2D240),f(0x1B2D27C),f(0x1B2D2B8),f(0x1B2D9A0),f(0x1B07D84)*f(0x1B2D2FC),read<float>(clock),read<float>(clock,0x10)};
                            r.elapsed_step=r.params.step;
                            bool live_clock_valid=finite(r.elapsed_step)&&r.elapsed_step>0&&r.elapsed_step<=2&&
                                finite(r.params.defer_scale)&&r.params.defer_scale>0&&r.params.defer_scale<=2;
                            r.params.step=.5f;r.params.defer_scale=.5f;
                            r.player=read<Vec3>(actor,0x2B4);r.action=action;r.bow=bowref.actor;r.arrow=ar.actor;
                            s->capture_stage=9;
                            r.origin=read<Vec3>(ar.actor,0x2B4);
                            r.launch_origin={__builtin_nanf(""),0,0};
                            call<void(*)(uptr,Vec3*,Vec3*)>(n,0x1B2CBD0)(bow,&r.velocity,&r.launch_origin);
                            // Undo the native current side-shot rotation without changing the player's shot index.
                            if(flags&1){
                                Quaternion q=call<Quaternion(*)(uptr)>(n,0x1B2CE68)(bow);
                                Vec3 axis{-q.x,-q.y,-q.z},t=mul(cross(axis,r.velocity),2);
                                r.velocity=add(r.velocity,add(mul(t,q.w),cross(axis,t)));
                            }
                            bool material_ok=attachment_params(n,attachment,r);
                            // A selected material can precede creation of its attached actor.
                            if(!r.attachment&&read<u32>(equip,0x15D0)!=0xFFFFFFFFu)material_ok=false;
                            valid=live_clock_valid&&trajectory_valid(&r.params)&&finite3(r.origin)&&finite3(r.velocity)&&finite3(r.launch_origin)&&
                                length(sub(r.launch_origin,r.player))<5&&length(sub(r.launch_origin,r.origin))<5&&length(sub(r.origin,r.player))<5&&
                                material_ok&&absf(length(r.velocity)-r.params.initial)<0.01f&&player_group(n,actor,r.group);
                            s->capture_stage=valid?10:9;
                        }
                    }else put(&s->reason,NoArrow);
                    release_ref(n,&ar);
                }
            }
        }else put(&s->reason,UnsupportedBow);
    }
    release_ref(n,&bowref);return valid;
}
extern "C" u64 trajectory_bow_enter(Registers*f,Native*n,State*s,uptr original){initialize(n,s);invalidate(s,NotAiming);return ((u64(*)(uptr))original)(f->x[0]);}
extern "C" u64 trajectory_bow_leave(Registers*f,Native*,State*s,uptr original){invalidate(s,NotAiming);return ((u64(*)(uptr))original)(f->x[0]);}
static void trace_arm(Native*,State*s){
    if(!get(&s->enabled)||!lock(s))return;
    Request request=s->request;Curve prediction=s->curve;
    bool valid=request.shots==1&&!request.attachment&&!request.flags&&request.epoch==get(&s->epoch)&&
        get(&s->frame)-request.frame<=3&&s->seed_valid&&s->seed_epoch==request.epoch;
    unlock(s);if(!valid)return;
    ShotTrace*t=&s->trace;u32 expected=0;
    if(!__atomic_compare_exchange_n(&t->lock,&expected,1,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))return;
    put(&t->status,TraceIdle);t->count=0;t->lost=0;t->sequence++;t->start_frame=t->last_frame=get(&s->frame);
    t->arrow=request.arrow;t->controller=0;t->main_body=0;t->owner_table=read<uptr>(request.arrow,0x228);
    t->request=request;t->prediction=prediction;t->impact={};t->contact={};
    if(!pointer(t->owner_table))put(&t->status,TraceInvalid);
    else put(&t->status,TraceArmed);
    put(&t->lock,0);
}
extern "C" u64 trajectory_release(Registers*f,Native*n,State*s,uptr original){trace_arm(n,s);invalidate(s,NotAiming);return ((u64(*)(uptr))original)(f->x[0]);}
extern "C" u64 trajectory_contact(Registers*f,Native*,State*s,uptr){
    const u64 result=f->x[0];ShotTrace*t=&s->trace;
    // The inline gate runs after the native query accepted a hit and before
    // native contact response moves the arrow's placement coordinate backward.
    if(!(result&1)||get(&t->status)!=TraceRecording||get(&t->lock)!=1||
       f->x[19]!=t->controller||t->count>=kTraceCapacity||t->contact.captured)return result;
    uptr controller=f->x[19],descriptor=f->x[20],frame=f->x[29];
    if(!pointer(controller)||!pointer(descriptor)||!pointer(frame)||frame<0x10100||
       read<uptr>(controller,0x18)!=t->arrow||read<uptr>(t->arrow,0x228)!=t->owner_table)return result;
    NativeContact c{};c.step=t->count;c.mode=read<u32>(controller,0x170);
    c.point=read<Vec3>(controller,0x120);c.direction=read<Vec3>(controller,0x114);c.length=read<float>(descriptor,0x38);
    if(!finite3(c.point)||!finite3(c.direction)||!finite(c.length)||c.length<0||c.length>100)return result;
    constexpr u32 offsets[]={0x34,0x38,0x3C,0x40,0x44,0x48};
    for(u32 i=0;i<6;i++)if(read<u8>(frame-offsets[i]))c.flags|=1u<<i;
    t->contact=c;put(&t->contact.captured,1);return result;
}
extern "C" u64 trajectory_arrow_update(Registers*f,Native*n,State*s,uptr original){
    auto orig=(u64(*)(uptr,uptr))original;uptr controller=f->x[0],delta=f->x[1];ShotTrace*t=&s->trace;
    u32 status=get(&t->status),expected=0;
    if((status!=TraceArmed&&status!=TraceRecording)||!pointer(controller)||
       read<uptr>(controller)!=n->base+0x3693970||read<uptr>(controller,0x18)!=t->arrow)
        return orig(controller,delta);
    if(!__atomic_compare_exchange_n(&t->lock,&expected,1,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)){
        __atomic_add_fetch(&t->lost,1,__ATOMIC_RELAXED);return orig(controller,delta);
    }
    bool valid=t->count<kTraceCapacity&&get(&s->frame)-t->start_frame<7200&&
        read<uptr>(t->arrow,0x228)==t->owner_table&&(!t->controller||t->controller==controller)&&
        delta>=0x10000&&delta<(1ull<<39)&&!(delta&3);
    if(valid&&status==TraceArmed){
        valid=capture_native(n,t->request,t->initial,t->api);
        if(valid){t->controller=controller;t->main_body=call<uptr(*)(uptr)>(n,0x98B050)(t->arrow);valid=pointer(t->main_body);}
        if(valid)put(&t->status,TraceRecording);
    }
    TraceEntry entry{};
    if(valid){
        uptr clockslot=read<uptr>(n->base,0x395DA70),clock=pointer(clockslot)?read<uptr>(clockslot):0;
        uptr worldslot=read<uptr>(n->base,0x395DAC0),world=pointer(worldslot)?read<uptr>(worldslot):0;
        uptr physical=pointer(world)?read<uptr>(world,0xC8):0;
        valid=pointer(clock)&&pointer(physical);
        if(valid){
            entry.frame=get(&s->frame);entry.before_mode=read<u32>(controller,0x170);entry.before_speed=read<float>(controller,0x108);
            entry.flags=(u32)read<u8>(controller,0x1F1)|((u32)read<u8>(controller,0x1F0)<<16);
            entry.input_step=read<float>(delta);entry.scaled_step=read<float>(clock);entry.base_step=read<float>(clock,0x10);
            entry.physics_seconds=read<float>(physical,0x24);entry.actor_scale=read<float>(t->arrow,0x280);entry.position=read<Vec3>(t->arrow,0x2B4);
            valid=finite(entry.input_step)&&entry.input_step>0&&entry.input_step<=2&&finite3(entry.position);
        }
    }
    u64 result=orig(controller,delta);
    if(valid&&read<uptr>(controller,0x18)==t->arrow&&read<uptr>(t->arrow,0x228)==t->owner_table){
        entry.after_mode=read<u32>(controller,0x170);entry.after_speed=read<float>(controller,0x108);entry.direction=read<Vec3>(controller,0x114);
        entry.flags|=(u32)read<u8>(controller,0x1F1)<<8;entry.flags|=(u32)read<u8>(controller,0x1F0)<<24;
        uptr pending=read<uptr>(t->main_body,0x60);entry.linear_velocity=read<Vec3>(t->main_body,0xF8);
        if(pointer(pending)&&(read<u32>(pending,0xD4)&128))entry.linear_velocity=read<Vec3>(pending,0x44);
        entry.original_return=result;t->entries[t->count]=entry;t->last_frame=entry.frame;
        put(&t->count,t->count+1);
        if((entry.flags>>8)&1){t->impact=read<Vec3>(controller,0x120);put(&t->status,TraceHit);}
        else if(t->count==kTraceCapacity)put(&t->status,TraceFull);
    }else put(&t->status,TraceInvalid);
    put(&t->lock,0);return result;
}
extern "C" u64 trajectory_bow_update(Registers*f,Native*n,State*s,uptr original){
    u64 result=((u64(*)(uptr))original)(f->x[0]);initialize(n,s);__atomic_add_fetch(&s->aim_callbacks,1,__ATOMIC_RELAXED);
    if(!get(&s->enabled)){invalidate(s,InvalidNative);return result;}
    Request r{};r.epoch=get(&s->epoch);r.frame=get(&s->frame);put(&s->reason,InvalidNative);
    if(!capture(n,s,f->x[0],r)){invalidate(s,(Reason)get(&s->reason));__atomic_add_fetch(&s->rejected,1,__ATOMIC_RELAXED);return result;}
    NativeSeed seed{};NativeApi api{};bool native_ok=capture_native(n,r,seed,api);
    if(lock(s)){
        if(get(&s->native_busy)){unlock(s);return result;}
        if(s->request.action!=r.action||s->request.bow!=r.bow||s->request.arrow!=r.arrow||
           s->request.attachment!=r.attachment||s->request.shots!=r.shots||s->request.flags!=r.flags)r.epoch=invalidate(s,Ready);
        // Forecast on a stable 60 Hz clock. Repeating one live frame's duration
        // changes the native discrete range transition on every aim callback.
        // Keep elapsed_step for display timing; do not change the live game clock.
        s->step_average=.5f;r.params.step=.5f;r.params.defer_scale=.5f;
        // Snapshot publication and prediction share this lock; no live body pointers are used by the copied engine.
        s->native_seed=seed;s->native_api=api;s->seed_valid=native_ok;s->seed_epoch=r.epoch;
        s->request=r;s->pending=1;unlock(s);put(&s->reason,Ready);
    }else put(&s->reason,QueryBusy);
    return result;
}

using RayFn=u64(*)(const Vec3*,const Vec3*,void*,const Group*,u32,u32);
extern "C" u64 trajectory_raycast(Registers*f,Native*n,State*s,uptr original){
    auto orig=(RayFn)original;
    u64 result=orig((Vec3*)f->x[0],(Vec3*)f->x[1],(void*)f->x[2],(Group*)f->x[3],(u32)f->x[4],(u32)f->x[5]);
    __atomic_add_fetch(&s->ray_callbacks,1,__ATOMIC_RELAXED);
    if(!get(&s->enabled)||f->x[0]<0x10000||f->x[0]>=(1ull<<39)||(f->x[0]&3)||!pointer(f->x[2])||!lock(s))return result;
    if(!s->pending){unlock(s);return result;}
    Request r=s->request;Vec3 donor=read<Vec3>(f->x[0]);
    if(!finite3(donor)||length(sub(donor,r.player))>=8){unlock(s);return result;}
    bool use_native=r.shots==1&&!r.attachment&&!r.flags;
    if(use_native){
        if(!s->seed_valid||s->seed_epoch!=r.epoch||get(&s->native_busy)){unlock(s);return result;}
        put(&s->native_busy,1);
    }
    s->pending=0;unlock(s);
    if(use_native){
        Curve curve{};bool ok=predict_native(n,s,r,curve);
        if(lock(s)){
            if(ok&&r.epoch==get(&s->epoch)&&get(&s->frame)-r.frame<=2){trajectory_stabilize(&curve,&s->stable[0],r.elapsed_step/30);s->curve=curve;s->curve_shots=1;__atomic_add_fetch(&s->native_predictions,1,__ATOMIC_RELAXED);}
            else __atomic_add_fetch(&s->native_failures,1,__ATOMIC_RELAXED);
            put(&s->native_busy,0);unlock(s);
        }else put(&s->native_busy,0);
        return result;
    }
    if(r.epoch!=get(&s->epoch)||get(&s->frame)-r.frame>2){__atomic_add_fetch(&s->discarded,1,__ATOMIC_RELAXED);return result;}
    alignas(16) u8 template_query[0x200],query[0x200];copy(template_query,(void*)f->x[2],sizeof(query));
    if(read<uptr>((uptr)template_query,0x60)!=f->x[2]+0x128){put(&s->reason,FilterUnknown);return result;}
    Curve curves[kShots]{};
    for(u32 shot=0;shot<r.shots;shot++){
    Request sample=r;sample.velocity=trajectory_spread(r.velocity,r.shot_angle,r.shots,shot);
    Curve&curve=curves[shot];if(trajectory_predict(&sample,&curve)<2)return result;
    for(u32 i=1;i<curve.count;i++){
        if(r.epoch!=get(&s->epoch)){__atomic_add_fetch(&s->discarded,1,__ATOMIC_RELAXED);return result;}
        copy(query,template_query,sizeof(query));*(uptr*)(query+0x60)=(uptr)query+0x128;
        query[0x20]=0;*(u64*)(query+0x120)=~0ull;*(uptr*)(query+0x170)=0;
        orig(&curve.points[i-1],&curve.points[i],query,&r.group,0x20,0);
        __atomic_add_fetch(&s->queries,1,__ATOMIC_RELAXED);
        if(query[0x20]){
            Vec3 hit=read<Vec3>((uptr)query,0x24),normal=read<Vec3>((uptr)query,0x30);
            if(!trajectory_segment_hit(&curve.points[i-1],&curve.points[i],&hit)||!finite3(normal)||length(normal)<.5f)return result;
            curve.points[i]=hit;curve.count=i+1;curve.normal=unit(normal);
            curve.hit=(r.flags&(Homing|SpecialMotion))?0:1;curve.body=read<u64>((uptr)query,0x120);
            __atomic_add_fetch(&s->hits,1,__ATOMIC_RELAXED);break;
        }
    }
    curve.complete=1;
    }
    if(lock(s)){if(r.epoch==get(&s->epoch)&&get(&s->frame)-r.frame<=2&&
        (s->curve.epoch!=r.epoch||r.frame>=s->curve.frame)){
        for(u32 i=0;i<r.shots;i++){
            trajectory_stabilize(&curves[i],&s->stable[i],r.elapsed_step/30);
            if(!i)s->curve=curves[i];else s->side[i-1]=curves[i];
        }
        s->curve_shots=r.shots;put(&s->reason,curves[0].hit?Ready:NoHit);
    }unlock(s);}
    return result;
}

using LineFn=void(*)(uptr,const Vec3*,const Vec3*,const float*,float);
static Vec3 lerp(Vec3 a,Vec3 b,float t){return add(a,mul(sub(b,a),t));}
static float perspective_t(float t,float za,float zb){return t*za/(zb*(1-t)+t*za);}
static bool clip_axis(float start,float delta,float&lo,float&hi){
    if(absf(delta)<1e-7f)return absf(start)<=1.05f;
    float a=(-1.05f-start)/delta,b=(1.05f-start)/delta;
    if(a>b){float tmp=a;a=b;b=tmp;}lo=lo>a?lo:a;hi=hi<b?hi:b;return lo<=hi;
}
static u32 dashed(LineFn line,uptr dc,const Curve&c,const float*view,const float*proj,float aspect,float width,const float*color){
    float phase=0;u32 drawn=0;
    for(u32 i=1;i<c.count&&drawn<512;i++){
        Vec3 a=c.points[i-1],b=c.points[i];
        float za=-(view[8]*a.x+view[9]*a.y+view[10]*a.z+view[11]);
        float zb=-(view[8]*b.x+view[9]*b.y+view[10]*b.z+view[11]);
        if(za<.05f&&zb<.05f)continue;
        if(za<.05f){a=lerp(a,b,(.05f-za)/(zb-za));za=.05f;}
        if(zb<.05f){b=lerp(a,b,(.05f-za)/(zb-za));zb=.05f;}
        Vec3 pa,pb;if(!trajectory_project(view,proj,&a,&pa)||!trajectory_project(view,proj,&b,&pb))continue;
        float dx=pb.x-pa.x,dy=pb.y-pa.y;
        float pixels=__builtin_sqrtf(dx*dx*aspect*aspect+dy*dy)*540;
        if(!finite(pixels)||pixels<.001f)continue;
        float lo=0,hi=1;
        if(clip_axis(pa.x,dx,lo,hi)&&clip_axis(pa.y,dy,lo,hi)){
            float cursor=lo*pixels,end=hi*pixels;
            while(cursor<end&&drawn<512){
                float current=phase+cursor;
                float mod=current-__builtin_floorf(current/16)*16;
                float next=cursor+(mod<6?6-mod:16-mod);
                if(next<=cursor+.001f)next=cursor+.001f;
                float stop=next<end?next:end;
                if(mod<6&&stop-cursor>.001f){
                    Vec3 x=lerp(a,b,perspective_t(cursor/pixels,za,zb));
                    Vec3 y=lerp(a,b,perspective_t(stop/pixels,za,zb));
                    line(dc,&x,&y,color,width);drawn++;
                }
                cursor=stop;
            }
        }
        phase+=pixels;phase-=__builtin_floorf(phase/16)*16;
    }
    return drawn;
}
static void render(Native*n,State*s,uptr args){
    if(!pointer(args)||read<u32>(args,0x18)!=1||read<u32>(args,0x1C)!=0)return;
    uptr context=read<uptr>(args,0x10);if(!pointer(context)||read<u8>(context,8)!=0)return;
    u32 frame=__atomic_add_fetch(&s->frame,1,__ATOMIC_ACQ_REL);
    if(!get(&s->enabled)||!lock(s))return;
    Curve curves[kShots];curves[0]=s->curve;
    u32 shots=s->curve_shots;Request latest=s->request;
    for(u32 i=1;i<shots&&i<kShots;i++)curves[i]=s->side[i-1];
    unlock(s);
    if(!shots||shots>kShots)return;
    const Curve&first=curves[0];
    if(!first.complete||first.epoch!=get(&s->epoch)||frame-first.frame>2||first.count<2||first.count>kSegments+1)return;
    if(latest.epoch!=first.epoch||dot(unit(latest.velocity),first.direction)<.999f)return;
    uptr dc=read<uptr>(args),camera=read<uptr>(context,0x18);
    uptr target=read<uptr>(context,0x538),viewport=read<uptr>(context,0x540);
    if(!pointer(dc)||!pointer(camera)||!pointer(target)||!pointer(viewport)||!pointer(read<uptr>(dc,0xB8)))return;
    uptr shader_slot=read<uptr>(n->base,0x3968DE8),holder=pointer(shader_slot)?read<uptr>(shader_slot):0;
    if(!pointer(holder)||read<u32>(holder,8)<63)return;
    uptr shaders=read<uptr>(holder,0x10);if(!pointer(shaders)||!pointer(read<uptr>(shaders,0x1F0))){put(&s->reason,RendererUnavailable);return;}
    float view[12],proj[16];copy(view,(void*)(camera+0x108),sizeof(view));copy(proj,(void*)(camera+0x1A8),sizeof(proj));
    for(float x:view)if(!finite(x))return;for(float x:proj)if(!finite(x))return;
    // Native viewport stores its bounding box at +8..+0x14.
    float vw=read<float>(viewport,0x10)-read<float>(viewport,8);
    float vh=read<float>(viewport,0x14)-read<float>(viewport,0xC);
    float physical_h=read<float>(target,0x1C)-read<float>(target,0x14);
    float logical_h=read<float>(target,0xC);
    if(!finite(vw)||!finite(vh)||vw<64||vh<64||vw>16384||vh>16384)return;
    float scale=vh/1080;
    if(finite(physical_h)&&finite(logical_h)&&physical_h>=64&&logical_h>=64)scale*=physical_h/logical_h;
    scale=clamp(scale,.5f,8);
    alignas(16) u8 graphics[128]{};
    auto ctor=call<void(*)(void*)>(n,0x155C);auto apply=call<void(*)(void*,uptr)>(n,0xAD40);
    ctor(graphics);graphics[0]=1;graphics[1]=0;apply(graphics,dc);
    call<void(*)(uptr,uptr)>(n,0x9370C)(context,dc);
    call<void(*)(uptr,const float*,const float*)>(n,0x280D724)(dc,view,proj);
    auto line=call<LineFn>(n,0x280D644);
    for(u32 shot=0;shot<shots;shot++){
    Curve&c=curves[shot];if(!c.complete||c.count<2||c.count>kSegments+1)continue;
    // Follow the currently nocked arrow at the near end, with no lag from landing smoothing.
    Vec3 shift=sub(latest.origin,c.points[0]);
    if(length(shift)>.30f)continue;
    for(u32 i=0;i<c.count;i++){
        float t=(float)i/(float)(c.count-1),weight=1-clamp(t*8,0,1);
        c.points[i]=add(c.points[i],mul(shift,weight));
    }
    // Short ivory dashes with a restrained olive edge remain readable on bright ground.
    const float edge[4]={.10f,.105f,.075f,shot?.28f:.40f};
    const float color[4]={.949f,.941f,.898f,shot?.36f:.56f};
    dashed(line,dc,c,view,proj,vw/vh,2.4f*scale,edge);
    __atomic_add_fetch(&s->drawn_segments,dashed(line,dc,c,view,proj,vw/vh,1.2f*scale,color),__ATOMIC_RELAXED);
    if(c.hit){
        Vec3 projected;if(!trajectory_project(view,proj,&c.points[c.count-1],&projected))continue;
        if(absf(proj[5])<.01f||absf(proj[0])<.01f)continue;
        // Preserve the surface's ellipse, with a minimum visible minor axis.
        // The marker stays centered on the hit; no world-space size cap makes it vanish at range.
        Vec3 camera_right{view[0],view[1],view[2]},camera_up{view[4],view[5],view[6]};
        Vec3 normal_camera{dot(c.normal,camera_right),dot(c.normal,camera_up),dot(c.normal,Vec3{view[8],view[9],view[10]})};
        float planar=__builtin_sqrtf(normal_camera.x*normal_camera.x+normal_camera.y*normal_camera.y);
        float nx=planar>.001f?normal_camera.x/planar:0,ny=planar>.001f?normal_camera.y/planar:1;
        float minor=clamp(absf(normal_camera.z),.45f,1.0f);
        Vec3 right=mul(camera_right,projected.z/(540*(vw/vh)*absf(proj[0]))),up=mul(camera_up,projected.z/(540*absf(proj[5])));
        Vec3 u=mul(add(mul(right,ny),mul(up,-nx)),5.5f),v=mul(add(mul(right,nx),mul(up,ny)),5.5f*minor),center=c.points[c.count-1];
        const float marker_edge[4]={.10f,.105f,.075f,shot?.42f:.54f};
        const float marker[4]={.949f,.941f,.898f,shot?.60f:.78f};
        constexpr float xy[33][2]={{1,0},{.9807853f,.1950903f},{.9238795f,.3826834f},{.8314696f,.5555702f},{.7071068f,.7071068f},{.5555702f,.8314696f},{.3826834f,.9238795f},{.1950903f,.9807853f},{0,1},{-.1950903f,.9807853f},{-.3826834f,.9238795f},{-.5555702f,.8314696f},{-.7071068f,.7071068f},{-.8314696f,.5555702f},{-.9238795f,.3826834f},{-.9807853f,.1950903f},{-1,0},{-.9807853f,-.1950903f},{-.9238795f,-.3826834f},{-.8314696f,-.5555702f},{-.7071068f,-.7071068f},{-.5555702f,-.8314696f},{-.3826834f,-.9238795f},{-.1950903f,-.9807853f},{0,-1},{.1950903f,-.9807853f},{.3826834f,-.9238795f},{.5555702f,-.8314696f},{.7071068f,-.7071068f},{.8314696f,-.5555702f},{.9238795f,-.3826834f},{.9807853f,-.1950903f},{1,0}};
        // The impact is an aiming indicator at its true projected position, visible over the held material.
        graphics[0]=0;apply(graphics,dc);
        for(u32 pass=0;pass<2;pass++){
            for(u32 i=0;i<32;i++){if(i%8>=6)continue;Vec3 a=add(center,add(mul(u,xy[i][0]),mul(v,xy[i][1]))),b=add(center,add(mul(u,xy[i+1][0]),mul(v,xy[i+1][1])));line(dc,&a,&b,pass?marker:marker_edge,(pass?1.25f:2.6f)*scale);}
            Vec3 a=sub(center,mul(right,.3f)),b=add(center,mul(right,.3f));line(dc,&a,&b,pass?marker:marker_edge,(pass?1.25f:2.6f)*scale);
        }
        graphics[0]=1;apply(graphics,dc);
    }
    }
    call<void(*)(uptr,uptr)>(n,0x284A590)(context,dc);
    u64 flags=2;call<void(*)(uptr,uptr,u64*)>(n,0x27F24D8)(target,dc,&flags);
    ctor(graphics);apply(graphics,dc);__atomic_add_fetch(&s->draw_callbacks,1,__ATOMIC_RELAXED);
}
extern "C" u64 trajectory_draw(Registers*f,Native*n,State*s,uptr original){u64 result=((u64(*)(uptr,uptr))original)(f->x[0],f->x[1]);render(n,s,f->x[1]);return result;}
