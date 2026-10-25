#pragma once
#include "../native/runtime_engine.hpp"
using u8 = unsigned char;
using u32 = unsigned int;
using u64 = unsigned long long;
using uptr = unsigned long long;
struct Vec3 { float x, y, z; };
struct Group { u32 id; u8 enabled, pad[3]; };
struct Params { float initial, acceleration, terminal, fall_acceleration, fall_speed, gravity, range, step, defer_scale; };
struct Motion { Vec3 origin, position, direction; float speed, acceleration, terminal; u32 falling, delayed; float flight_range;
    Vec3 convergence_velocity; float convergence_time; Vec3 previous_velocity; u32 has_previous; };
constexpr u32 kSegments = 128;
constexpr u32 kMaxSteps = 2048;
constexpr u32 kShots = 5;
struct Request { u32 epoch, frame; uptr action, bow, arrow; Group group; Vec3 player, origin, velocity; Params params;
    uptr attachment; u32 shots, flags; float shot_angle, elapsed_step; Vec3 launch_origin; };
enum PreviewFlags : u32 { Homing=1, IgnoreGravity=2, SpecialMotion=4 };
struct Curve { u32 epoch, frame, count, hit, complete; Vec3 points[kSegments+1]; Vec3 normal;
    u64 body; Vec3 direction; u32 flags; };
struct Stable { u32 epoch, frame, valid, flags; u64 body; Vec3 point, normal, origin, direction; };
constexpr u32 kTraceCapacity=1024;
enum TraceStatus:u32 {TraceIdle=0,TraceArmed=1,TraceRecording=2,TraceHit=3,TraceFull=4,TraceInvalid=5};
struct TraceEntry {
    u32 frame,before_mode,after_mode,flags;
    float input_step,scaled_step,base_step,physics_seconds,actor_scale,before_speed,after_speed;
    Vec3 position,direction,linear_velocity;
    u64 original_return;
};
static_assert(sizeof(TraceEntry)==88);
struct NativeContact {
    u32 captured,step,flags,mode;
    Vec3 point,direction;
    float length;
    u32 reserved;
};
static_assert(sizeof(NativeContact)==48);
struct ShotTrace {
    u32 lock,status,count,sequence,lost,start_frame,last_frame,reserved;
    uptr arrow,controller,main_body,owner_table;
    Request request;Curve prediction;NativeSeed initial;NativeApi api;Vec3 impact;
    TraceEntry entries[kTraceCapacity];
    NativeContact contact;
};
static_assert(__builtin_offsetof(ShotTrace,contact)==95728);
struct State {
    u32 magic, enabled, epoch, frame, lock, pending, reason, reserved;
    u64 aim_callbacks, ray_callbacks, draw_callbacks, queries, drawn_segments, rejected, discarded, hits;
    Request request;
    Curve curve;
    uptr last_action, last_actor, last_bow, last_arrow;
    u32 action_f0, action_f4, action_f8, capture_stage;
    Curve side[kShots-1];
    Stable stable[kShots];
    float step_average;
    u32 curve_shots;
    u32 native_busy,native_bound,seed_valid,seed_epoch;
    u64 native_predictions,native_failures;
    NativeSeed native_seed;
    NativeApi native_api;
    NativeEngine native_engine;
    ShotTrace trace;
};
static_assert(__builtin_offsetof(State,trace)==120464);
static_assert(sizeof(State)<=0x36000);
struct Native { uptr base; };
struct Registers { uptr x[31]; uptr nzcv; u8 q[512]; };
static_assert(sizeof(Registers)==768);
enum Reason : u32 { Ready=0, NotAiming=1, UnsupportedBow=2, Attachment=3, NoArrow=4, MotionUnsupported=5,
    InvalidNative=6, QueryBusy=7, FilterUnknown=8, Stale=9, NoHit=10, Limit=11, RendererUnavailable=12 };
extern "C" {
bool trajectory_valid(const Params* p);
void trajectory_motion_init(Motion* m, const Request* r);
void trajectory_motion_step(Motion* m, const Params* p);
void trajectory_fall_start(Motion* m, const Params* p);
u32 trajectory_predict(const Request* r, Curve* out);
bool trajectory_project(const float* view, const float* proj, const Vec3* in, Vec3* out);
bool trajectory_segment_hit(const Vec3* a,const Vec3* b,const Vec3* hit);
void trajectory_stabilize(Curve*,Stable*,float);
Vec3 trajectory_spread(Vec3,float,u32,u32);
u64 trajectory_bow_update(Registers*,Native*,State*,uptr original);
u64 trajectory_bow_enter(Registers*,Native*,State*,uptr original);
u64 trajectory_bow_leave(Registers*,Native*,State*,uptr original);
u64 trajectory_release(Registers*,Native*,State*,uptr original);
u64 trajectory_raycast(Registers*,Native*,State*,uptr original);
u64 trajectory_draw(Registers*,Native*,State*,uptr original);
u64 trajectory_arrow_update(Registers*,Native*,State*,uptr original);
u64 trajectory_contact(Registers*,Native*,State*,uptr unused);
}
