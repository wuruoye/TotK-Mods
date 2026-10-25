#pragma once
using u8=unsigned char;using u32=unsigned int;using u64=unsigned long long;
struct NativeVec {float x,y,z;};
struct NativeBodySeed {u8 raw[0x180],pending[0xE0];NativeVec local_center;u32 has_pending;};
struct NativeSeed {u8 controller[0x240],actor[0x340];NativeBodySeed bodies[2];};
struct NativeLaunch {NativeVec origin,velocity;float terminal,range,acceleration,fall_acceleration,fall_speed,gravity,dt;};
struct NativeApi {u64 base;u64 scene;u64 scene_context;u32 group;u32 reserved;u8 masks[4][0x100];u8 body_filters[2][0x20];};
struct NativePoint {NativeVec origin,attack_origin,direction;float speed;u32 mode,error,hit;NativeVec normal;u64 body;};
struct NativeHit {NativeVec point,normal;u64 body;};
struct alignas(16) NativeEngine {u8 arena[0x1A000];NativeApi api;u32 disabled,initialized[2],teleports[2],error,magic,hit;NativeVec contact,normal;
    NativeHit recent[8];u32 hit_count;u64 contact_body;};
extern "C" bool arrow_native_initialize(NativeEngine*,const NativeApi*,const NativeSeed*,const NativeLaunch*);
// Call once with the writable alias of the component before initialize/advance.
extern "C" bool arrow_native_bind(u64 writable_alias);
extern "C" bool arrow_native_advance(NativeEngine*,float,NativePoint*);
