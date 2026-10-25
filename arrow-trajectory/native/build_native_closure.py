"""Relocate original ARM64 instruction blocks without reimplementing their arithmetic.

Builds a private offline development component, not an installable Mod. Native
global pages and explicit adapters are import slots. Unresolved native calls trap
and are reported. Runtime binding must supply isolated objects and side effects.
"""
from pathlib import Path
import importlib.util,json,os,struct,subprocess,sys,zlib
HERE=Path(__file__).resolve().parent;ROOT=HERE.parent
sys.path.insert(0,str(ROOT/'tooling'))
from capstone import Cs,CS_ARCH_ARM64,CS_MODE_ARM
from capstone.arm64_const import ARM64_OP_IMM

def build(runtime=False):
    text=(ROOT/'inputs/text.bin').read_bytes()
    assert zlib.crc32(text)==0x945314EA
    spec=json.loads((HERE/'closure-input.json').read_text());roots=set(spec['roots']);adapters=set(spec['adapters'])
    if runtime:adapters.update((0x25A650,0x978FD0))
    md=Cs(CS_ARCH_ARM64,CS_MODE_ARM);md.detail=True
    instructions={};pending=list(roots);imports=set();indirect=[]
    while pending:
        pc=pending.pop()
        if pc in instructions:continue
        assert 0<=pc<len(text) and len(instructions)<40000,('closure_bound',hex(pc),len(instructions))
        ins=next(md.disasm(text[pc:pc+4],pc));instructions[pc]=ins
        name=ins.mnemonic
        if name in ('ret','eret','brk'):continue
        if name in ('br','blr'):
            indirect.append(pc)
            if name=='br':continue
        elif name=='bl':
            target=ins.operands[-1].imm
            if target in roots:pending.append(target)
            else:imports.add(target)
        elif name=='b' or name.startswith('b.') or name in ('cbz','cbnz','tbz','tbnz'):
            target=ins.operands[-1].imm
            if name=='b' and target not in roots and abs(target-pc)>0x4000:imports.add(target)
            else:pending.append(target)
            if name=='b':continue
        pending.append(pc+4)
    # Keep original instruction bytes except PC-relative instructions.
    rows=['// Generated from CRC32-bound private game bytes. Do not edit or format.','.text','.balign 16']
    globals_={};literals={};branch_count=0
    for pc,ins in sorted(instructions.items()):
        rows += [f'.global native_{pc:X}',f'native_{pc:X}:']
        name=ins.mnemonic
        if runtime and pc==0x1D251B0:
            # Original query has returned an accepted hit. Stop before game-side
            # contact responses, effects, actor references or damage are applied.
            assert bytes(ins.bytes)==text[0x1D251B0:0x1D251B4] and name=='tbnz'
            rows += ['tbz w0,#0,native_1D251B4','mov x0,x19','sub x1,x29,#0x58','b engine_contact']
        elif runtime and pc==0x1D29434:
            # Sensor query uses the game's arrow collision filters. Preserve the
            # native nearest-contact comparison, without resolving actor refs,
            # damage properties or contact-response velocities in a preview.
            assert name=='tbz' and ins.operands[-1].imm==0x1D29CEC
            rows += ['tbz w0,#0,native_1D29CEC','add x0,sp,#0x100','bl engine_sensor_valid',
                     'tbz w0,#0,native_1D29CEC','ldr s4,[sp,#0x60]','mov w20,wzr','mov w28,wzr',
                     'ldr x26,[sp,#0x28]','b native_1D29BA0']
        elif runtime and pc==0x1D29C90:
            # The selected geometry is already in controller+0x120. Do not read
            # the target actor or calculate its response velocity.
            assert name=='ldr' and ins.op_str=='x8, [x25, #0x148]'
            rows += ['b native_1D29CE0']
        elif name in ('adr','adrp'):
            target=ins.operands[-1].imm;key=f'address_{target:X}';globals_[target]=key
            rows.append(f'ldr {ins.reg_name(ins.operands[0].reg)}, {key}')
        elif name.startswith('ldr') and len(ins.operands)==2 and ins.operands[1].type==ARM64_OP_IMM:
            target=ins.operands[-1].imm;reg=ins.reg_name(ins.operands[0].reg)
            size=16 if reg.startswith('q') else 8 if reg.startswith(('x','d')) else 4
            key=f'literal_{target:X}_{size}';literals[key]=text[target:target+size]
            rows.append(f'{name} {reg}, {key}')
        elif name=='bl' or name=='b' or name.startswith('b.') or name in ('cbz','cbnz','tbz','tbnz'):
            target=ins.operands[-1].imm
            label=f'native_{target:X}' if target in instructions else f'import_{target:X}'
            args=ins.op_str.rsplit('#',1)[0]+label
            rows.append(f'{name} {args}');branch_count+=1
        else:rows.append(f'.inst 0x{int.from_bytes(ins.bytes,"little"):08X}')
        # Sparse blocks need explicit fallthrough when intervening bytes were omitted.
        if name not in ('ret','eret','br','brk','b') and pc+4 not in instructions:
            raise AssertionError(('missing_fallthrough',hex(pc)))
    rows += ['.balign 16']
    for target in sorted(imports):
        rows += [f'.global import_{target:X}',f'import_{target:X}:',f'ldr x16, slot_{target:X}','br x16']
    rows += ['.global native_unbound','native_unbound:','b engine_abort' if runtime else 'brk #0x143','.section .data','.balign 16']
    for target in sorted(imports):rows += [f'.global slot_{target:X}',f'slot_{target:X}:','.quad 0']
    for target,key in sorted(globals_.items()):rows += [f'.global {key}',f'{key}:','.quad 0']
    for key,data in literals.items():rows += ['.balign 16',f'{key}:','.byte '+','.join(str(x) for x in data)]
    out=HERE/('runtime-build' if runtime else 'relocated');out.mkdir(exist_ok=True)
    (out/'native.S').write_text('\n'.join(rows)+'\n')
    (out/'native.ld').write_text('SECTIONS { . = 0; .text : { *(.text*) } .rodata : { *(.rodata*) } . = ALIGN(16); __data_start = .; .data : { *(.data*) } .got : { __got_start = .; *(.got*) __got_end = .; } .bss : { *(.bss*) } /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) } }\n')
    zig=Path(os.environ['ARROW_ZIG'])
    cmd=[str(zig),'cc','-target','aarch64-freestanding-none','-nostdlib','-Wl,-T,'+str(out/'native.ld'),'-Wl,-e,native_1D21FBC',str(out/'native.S'),'-o',str(out/'native.elf')]
    if runtime:
        adapter_map={0x2B3CF58:'result_body',0x2B490A4:'attack_body',0xC3C734:'physics',0x1D2AC4C:'disabled',0x1BF5A18:'disable',0x1BF6488:'argument',
                     0x567750:'velocity',0x25A1F0:'angular',0x4ECCA0:'position',0x40E490:'both',0xD66B24:'parameter',0x9F7A78:'blackboard',0xB9F3DC:'absent',
                     0x1AFBE4:'empty_ref',0x476C9C:'empty_iterator',0x2BB7BD0:'copy_adapter',0x2BB7B80:'fill_adapter',0x98B050:'result_body',0xB694D0:'group',
                     0x1B0614:'ref_copy',0x1952A4:'scene',0x25A650:'pending_request',0x978FD0:'query_body_valid',0x2BB7440:'sine',0x2BB7410:'cosine',0x2BB7450:'arccos',0x2BB7A10:'arccos_double'}
        for target in (0x1B2F20,0x2BB7A60,0x2BB7A70,0x11C6820,0x11C6E20,0x1AFF9C,0x194F8C4,0xB2B7EC,0x1979070,0xC7B9EC,0xB1FDC0,0xAE4300,0x29ADF9C,
                       0x2AF5338,0xD0B840,0x4F8D60,0xC3C758,0x1D246F4,0x1D2486C,0x1D24CAC):adapter_map[target]='zero'
        assert not (imports&adapters)-adapter_map.keys(),[hex(x) for x in (imports&adapters)-adapter_map.keys()]
        binding=['// Generated binding declarations. Do not edit or format.','extern "C" {']
        binding += [f'void native_{rva:X}();' for rva in sorted(roots)]
        binding += [f'extern u64 slot_{rva:X};' for rva in sorted(imports)]
        binding += [f'extern u64 address_{rva:X};' for rva in sorted(globals_)]
        binding += ['}','static void bind_imports(){']
        binding += [f'slot_{rva:X}=(u64)&'+(adapter_map[rva] if rva in adapters else 'native_unbound')+';' for rva in sorted(imports)]
        binding += [f'address_{rva:X}=(u64)global(0x{rva:X});' for rva in sorted(globals_)]
        binding += ['}'];(HERE/'runtime_bindings.inc').write_text('\n'.join(binding)+'\n')
        cmd=[str(zig),'c++','-target','aarch64-freestanding-none','-std=c++17','-O2','-ffreestanding','-fPIC','-mno-outline-atomics','-fno-jump-tables','-fno-builtin','-fno-exceptions','-fno-rtti','-fno-stack-protector','-fno-unwind-tables','-fno-asynchronous-unwind-tables','-nostdlib','-Wall','-Wextra','-Werror',
             str(HERE/'runtime_engine.cpp'),str(HERE/'runtime_invoke.S'),str(out/'native.S'),'-Wl,-T,'+str(out/'native.ld'),'-Wl,-e,arrow_native_initialize','-Wl,--no-undefined','-o',str(out/'native.elf')]
    env=dict(os.environ,ZIG_LOCAL_CACHE_DIR=str(out/'zig-local'),ZIG_GLOBAL_CACHE_DIR=str(out/'zig-global'))
    result=subprocess.run(cmd,capture_output=True,text=True,env=env);(out/'compile.log').write_text(result.stdout+result.stderr)
    if result.returncode:raise RuntimeError(result.stderr)
    helper=ROOT/'tooling/elf.py'
    s=importlib.util.spec_from_file_location('closure_elf',helper);module=importlib.util.module_from_spec(s);s.loader.exec_module(module)
    blob,_=module.elf_flat(out/'native.elf');(out/'native.bin').write_bytes(blob)
    raw=(out/'native.elf').read_bytes();shoff=struct.unpack_from('<Q',raw,40)[0];shsize,shnum=struct.unpack_from('<HH',raw,58)
    sections=[struct.unpack_from('<IIQQQQIIQQ',raw,shoff+i*shsize) for i in range(shnum)];symbols={}
    for section in sections:
        if section[1]!=2:continue
        names=sections[section[6]];strings=raw[names[4]:names[4]+names[5]]
        for at in range(section[4],section[4]+section[5],section[9]):
            name,info,other,index,value,size=struct.unpack_from('<IBBHQQ',raw,at)
            label=strings[name:strings.find(b'\0',name)].decode() if name else ''
            if label and index and not label.startswith('$'):symbols[label]=value
    report={'status':'relocated_native_component_built','game_text_crc32':'945314EA','component_crc32':f'{zlib.crc32(blob):08X}',
            'bytes':len(blob),'instructions':len(instructions),'relocated_branches':branch_count,'roots':sorted(roots),'symbols':symbols,
            'adapter_imports':sorted(imports&adapters),'unbound_imports':sorted(imports-adapters),'address_imports':sorted(globals_),
            'indirect_sites':sorted(indirect),'motion_arithmetic_reimplemented':False,'installable':False,'installed':False}
    (out/'build.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:x for k,x in report.items() if k not in ('symbols','roots','adapter_imports','unbound_imports','address_imports','indirect_sites')}))
    print('Unbound imports',len(report['unbound_imports']))
    return report
if __name__=='__main__':build('--runtime' in sys.argv)
