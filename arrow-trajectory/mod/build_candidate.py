"""Build a private, origin-checked candidate without writing installed Mod folders."""
from pathlib import Path
import contextlib,importlib.util,io,json,os,struct,subprocess,sys,zlib
sys.dont_write_bytecode=True
HERE=Path(__file__).resolve().parent;ROOT=HERE.parent
sys.path.insert(0,str(ROOT/'tooling'))
from keystone import Ks,KS_ARCH_ARM64,KS_MODE_LITTLE_ENDIAN
from capstone import Cs,CS_ARCH_ARM64,CS_MODE_ARM
CORE=0x4000;PAYLOAD=0x20000;STATE_BYTES=0x36000
HOOKS=[('bow_enter',0x23BB4B8),('bow_update',0x23BB680),('bow_leave',0x23BC074),('release',0x1B78BB0),('raycast',0x2B6900),('draw',0x7A910C),('arrow_update',0x1D23A4C),('contact',0x1D251B0)]
FUNCTIONS={
 'action_actor':0x6BC918,'actor_reference':0x1AFBE4,'release_reference':0xC7B9EC,
 'arrow_link':0x4DAE80,'attachment_link':0x4DAA94,'player_collision_group':0x1CFB0D0,
 'bow_initial':0x1B2CE2C,'bow_acceleration':0x1B2D204,'bow_terminal':0x1B2D240,
 'bow_fall_acceleration':0x1B2D27C,'bow_fall_speed':0x1B2D2B8,'bow_gravity':0x1B2D9A0,
 'bow_range':0x1B07D84,'bow_range_multiplier':0x1B2D2FC,'bow_launch':0x1B2CBD0,
 'graphics_construct':0x155C,'graphics_apply':0xAD40,'target_bind':0x9370C,'target_unbind':0x284A590,
 'target_barrier':0x27F24D8,'draw_begin':0x280D724,'draw_line':0x280D644,
 'motion_forward':0x1D25BE4,'motion_falling':0x1D26140,'fall_begin':0x1D26058,'fall_test':0x1D25F6C,
 'shot_angle':0x1B07EF4,'shot_rotation':0x1B2CE68,'attachment_property':0x1B2428C,'attachment_range':0x1B25C90,
 'arrow_origin_selection':0x1D22084,'arrow_attachment_flags':0x1D22A54,
 'bow_aim_target':0x1C93CC0,'bow_launch_direction':0x1B2CF44,'shooter_launch_origin':0x1BFB834,
 'arrow_motion_dispatch':0x1D24D98,'arrow_motion_reference':0x1D23A4C,'origin_convergence':0x18AD188,
}
def crc(b):return f'{zlib.crc32(b):08X}'
def record(p):b=p.read_bytes();return {'path':str(p),'bytes':len(b),'crc32':crc(b)}
def load(name,path):s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);sys.modules[name]=m;s.loader.exec_module(m);return m
horse=load('arrow_elf_helpers',ROOT/'tooling/elf.py')
def main():
    out=HERE/'build';out.mkdir(exist_ok=True)
    textpath=ROOT/'inputs/text.bin';text=textpath.read_bytes();assert crc(text)=='945314EA'
    native_dir=HERE.parent/'native';native_build=json.loads((native_dir/'runtime-build/build.json').read_text())
    native_blob=(native_dir/'runtime-build/native.bin').read_bytes();assert crc(native_blob)==native_build['component_crc32'] and 0x10000+len(native_blob)<PAYLOAD
    component=['// Generated component bindings. Do not edit or format.','#pragma once']
    for key,name in [('Bind','arrow_native_bind'),('Initialize','arrow_native_initialize'),('Advance','arrow_native_advance')]:component.append(f'constexpr uptr kNative{key}=0x{native_build["symbols"][name]:X};')
    component.append('constexpr Guard kCopiedNativeGuards[]={')
    for at in sorted(set(native_build['roots'])|{0x1952A4,0x98B050,0xB694D0,0x978FD0}):
        if at==0x11C9A04:continue
        component.append('{0x%X,{%s}},'%(at,','.join('0x%08Xu'%x for x in struct.unpack_from('<4I',text,at))))
    component.append('};')
    sys.path.insert(0,str(ROOT/'tooling'));from inspect_binary import lz4_block
    uc_path=ROOT/'inputs/ultracam/subsdk3'
    uc=uc_path.read_bytes();assert crc(uc)=='3989F712';off,location,size,_=struct.unpack_from('<4I',uc,0x10);packed=struct.unpack_from('<I',uc,0x60)[0]
    uc_text=lz4_block(uc[off:off+packed],size);assert crc(uc_text)=='030D066C'
    for label,at in [('Wrapper',0x22520),('Gate',0x22410)]:component.append('constexpr u32 kAngular%s[]={%s};'%(label,','.join('0x%08Xu'%x for x in struct.unpack_from('<6I',uc_text,at))))
    (HERE/'native_component.hpp').write_text('\n'.join(component)+'\n')
    guard_source=['// Generated native byte guards. Do not edit or format.','#pragma once','struct Guard { uptr rva; u32 words[4]; };','constexpr Guard kNativeGuards[]={']
    guards=[]
    for name,at in FUNCTIONS.items():
        words=struct.unpack_from('<4I',text,at);guard_source.append('{0x%X,{%s}},'%(at,','.join('0x%08Xu'%x for x in words)))
        guards.append(dict(name=name,rva=at,first16=text[at:at+16].hex()))
    guard_source.append('};');(HERE/'native_guards.hpp').write_text('\n'.join(guard_source)+'\n',encoding='utf-8')
    zig=Path(os.environ['ARROW_ZIG'])
    cmd=[str(zig),'c++','-target','aarch64-freestanding-none','-std=c++17','-O2','-g','-ffreestanding','-fPIC','-mno-outline-atomics','-fno-jump-tables','-fno-builtin','-fno-exceptions','-fno-rtti','-fno-stack-protector','-fno-unwind-tables','-fno-asynchronous-unwind-tables','-nostdlib','-Wall','-Wextra','-Werror',str(HERE/'trajectory.cpp'),'-Wl,-T,'+str(HERE/'trajectory.ld'),'-Wl,--no-undefined','-o',str(out/'trajectory.elf')]
    cmd.insert(-2,'-Wl,-e,trajectory_predict')
    for name in ['trajectory_predict','trajectory_motion_init','trajectory_motion_step','trajectory_fall_start','trajectory_valid','trajectory_project','trajectory_segment_hit']+['trajectory_'+x[0] for x in HOOKS]:
        cmd.insert(-2,'-Wl,-u,'+name)
    env=dict(os.environ,ZIG_LOCAL_CACHE_DIR=str(out/'zig-local'),ZIG_GLOBAL_CACHE_DIR=str(out/'zig-global'))
    r=subprocess.run(cmd,env=env,capture_output=True,text=True,encoding='utf-8');(out/'compile.log').write_text(r.stdout+r.stderr,encoding='utf-8')
    if r.returncode:raise RuntimeError(r.stdout+r.stderr)
    blob,syms=horse.elf_flat(out/'trajectory.elf');assert CORE+len(blob)<0xC000
    (out/'trajectory.bin').write_bytes(blob)
    source=(ROOT/'tooling/builder_core.py').read_text(encoding='utf-8')
    source=source.replace('Korok, Cave, Well, Chasm map markers.','bow-anchored dashed arrow previews; gameplay unverified.')
    local=out/'builder_module.py';local.write_text(source,encoding='utf-8');b=load('arrow_builder',local)
    b.TEXT=text;b.HOST_IMAGE=(ROOT/'inputs/host.image').read_bytes()
    b.PAYLOAD_SIZE=b.STATE=PAYLOAD;b.STATE_SIZE=STATE_BYTES;b.TABLE=0xC000;b.POOL=0xE000
    b.NATIVE={'main_base':0};b.SLOTS={'main_base':0xE100};b.STATE_POINTER=0xE180
    stubs=[];md=Cs(CS_ARCH_ARM64,CS_MODE_ARM)
    class Feature:
        @staticmethod
        def emit(m):
            m.v2_metadata={'version':'0.0.13','stage':'development','feature':'allow native bullet-time shooter rates and positive slowed clock; retains v012 collision and UI','actual_gameplay_verified':False,'save_writes':False,'native_guards':guards,'state_bytes':STATE_BYTES,'native_component_crc32':native_build['component_crc32'],'complete_impact_accuracy_verified':False,'prediction_step':0.5,'changes_live_game_clock':False,'trace_offset':120464,'trace_capacity':1024,'trace_entry_bytes':88,'changes_real_arrow_motion':False}
            m.payload[CORE:CORE+len(blob)]=blob
            m.payload[0x10000:0x10000+len(native_blob)]=native_blob
            for i,(label,rva) in enumerate(HOOKS):
                at=0x1000+i*0x400;tramp=at+0x300;frame=0x310
                ins=next(md.disasm(text[rva:rva+4],rva));inline=label=='contact'
                assert (ins.mnemonic=='tbnz' and ins.op_str=='w0, #0, #0x1d2529c') if inline else ins.mnemonic in ('sub','stp')
                m.payload[tramp:tramp+4]=text[rva:rva+4]
                if inline:m.asm('brk #0x143',tramp+4)
                else:m.asm('b #0',tramp+4);m.return_fix(tramp+4,rva+4)
                rows=[f'tbz w0,#0,#{at+0x2E0}'] if inline else []
                rows += [f'sub sp,sp,#{frame:#x}']
                rows += [f'stp x{j},x{j+1},[sp,#{j*8}]' for j in range(0,30,2)]
                rows += ['str x30,[sp,#240]','mrs x8,nzcv','str x8,[sp,#248]']
                rows += [f'stp q{j},q{j+1},[sp,#{256+j*16}]' for j in range(0,32,2)]
                rows += ['mov x0,sp',m.adr('x1',0xE100),m.loadptr('x2',m.STATE_POINTER),m.adr('x3',tramp),f'bl #{CORE+syms["trajectory_"+label]}','str x0,[sp]']
                rows += [f'ldp q{j},q{j+1},[sp,#{256+j*16}]' for j in range(0,32,2)]
                rows += ['ldr x8,[sp,#248]','msr nzcv,x8']
                rows += [f'ldp x{j},x{j+1},[sp,#{j*8}]' for j in range(0,30,2)]
                rows += ['ldr x30,[sp,#240]',f'add sp,sp,#{frame:#x}','b #0' if inline else 'ret']
                code=m.asm('\n'.join(rows),at);assert len(code)<0x300
                if inline:
                    assert len(code)<0x2E0
                    m.return_fix(at+len(code)-4,0x1D2529C)
                    m.asm('b #0',at+0x2E0);m.return_fix(at+0x2E0,rva+4)
                m.patch(rva,stub=at,label=label);stubs.append(dict(label=label,rva=rva,stub=at,stub_bytes=len(code),trampoline=tramp,original=ins.mnemonic+' '+ins.op_str))
    log=io.StringIO()
    with contextlib.redirect_stdout(log):b.build(Feature,'arrow143',out,out/'candidate/Arrow Trajectory 1.4.3')
    (out/'build.log').write_text(log.getvalue(),encoding='utf-8')
    module=out/'candidate/Arrow Trajectory 1.4.3/romfs/WiiXLaunch/mods/arrow143.wxlm'
    result={'status':'development_built_not_accepted','module':record(module),'core':record(out/'trajectory.bin'),'core_offset':CORE,'symbols':syms,'hooks':stubs,'native_guards':guards,'compile_command':cmd,'allocation_bytes':PAYLOAD+STATE_BYTES,'text':record(textpath),'source':[record(HERE/x) for x in ['trajectory.hpp','trajectory.cpp','trajectory.ld','native_guards.hpp','native_component.hpp','build_candidate.py']],'native_component':native_build['component_crc32'],'installed':False,'gameplay_verified':False}
    (out/'build.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps({'module_crc32':result['module']['crc32'],'core_bytes':len(blob),'allocation_bytes':PAYLOAD+STATE_BYTES,'hooks':len(stubs)}))
if __name__=='__main__':main()
