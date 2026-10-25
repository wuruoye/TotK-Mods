"""Build the local, origin-checked 1.4.3 WiiXLaunch module; no game writes."""
import sys
sys.dont_write_bytecode = True
from pathlib import Path
import json, struct, zlib
HERE = Path(__file__).resolve().parent
from keystone import Ks, KS_ARCH_ARM64, KS_MODE_LITTLE_ENDIAN
STATE_SIZE = 0x10040

TEXT = None
KS = Ks(KS_ARCH_ARM64, KS_MODE_LITTLE_ENDIAN)
PAYLOAD_SIZE = 0x10000
STATE = PAYLOAD_SIZE
TABLE = 0xc000
POOL = 0xe000
IMPORTS = [ ('wiixl.core','Log'), ('wiixl.call','ResolveTarget'),
            ('wiixl.patch','Write'), ('wiixl.version','Fingerprint') ]
NATIVE = {'hash':0x007d4310, 'rng':0x00c8b920, 'rng_global':0x03accea8,
          'weapon_roll':0x01c94880,'shield_roll':0x01c94db0,'bow_roll':0x01c950d0,
          'get_int':0x0019f9d4,'set_int':0x0016fdcc,
          'get_record':0x01b65190,'main_base':0}
SLOTS = {n:POOL+0x100+i*8 for i,n in enumerate(NATIVE)}
STATE_POINTER = POOL + 0x180
HOST = {'CoreLog':0x267d0,'write_delta':0x1a8690,'flush':0x2b4a0}
HOST_IMAGE = None
payload = bytearray(PAYLOAD_SIZE)
asm_records = {}
patches = []
fixups = []

def asm(source, at):
    raw,_ = KS.asm(source, addr=at, as_bytes=True)
    payload[at:at+len(raw)] = raw
    asm_records[f'{at:06x}'] = source
    return bytes(raw)

def adr(reg, at):
    return f'adrp {reg}, {at & ~4095:#x}; add {reg}, {reg}, #{at & 4095:#x}'

def loadptr(reg, at):
    return f'{adr(reg,at)}; ldr {reg}, [{reg}]'

def importcall(name):
    slot = POOL + next(i for i,(_,s) in enumerate(IMPORTS) if s==name)*8
    return f'{loadptr("x16",slot)}; blr x16'

def patch(rva, replacement=None, stub=None, size=4, label=''):
    origin=TEXT[rva:rva+size]
    assert len(origin)==size
    patches.append({'rva':rva,'origin':origin.hex(), 'replacement':replacement,
                    'stub':stub,'size':size,'label':label})

def return_fix(at, dest_rva):
    fixups.append((at,dest_rva))

def pickup(at, hook, roll):
    frame=0x2c0
    rows=[f'sub sp,sp,#{frame:#x}']
    rows += [f'stp x{i},x{i+1},[sp,#{i*8:#x}]' for i in range(0,18,2)]
    rows += ['stp x18,x30,[sp,#0x90]','mrs x8,nzcv','str x8,[sp,#0xa0]']
    rows += [f'stp q{i},q{i+1},[sp,#{0xc0+i*16:#x}]' for i in range(0,32,2)]
    rows += ['ldr x0,[x0,#0x18]','cbz x0,restore','ldr x0,[x0,#0x218]',
             'cbz x0,restore','mov w1,wzr',loadptr('x16',SLOTS['hash']),'blr x16',
             'str w0,[sp,#0xa8]',loadptr('x0',SLOTS['rng_global']),
             'ldr x0,[x0]','cbz x0,restore',loadptr('x16',SLOTS['rng']),'blr x16',
             'and w2,w0,#1','str w2,[sp,#0xac]','add x0,sp,#0xb0',
             'str wzr,[x0]','ldr w1,[sp,#0xa8]',loadptr('x16',SLOTS[roll]),'blr x16',
             'cbnz w0,apply','ldr w2,[sp,#0xac]','eor w2,w2,#1',
             'add x0,sp,#0xb0','ldr w1,[sp,#0xa8]',loadptr('x16',SLOTS[roll]),'blr x16',
             'apply:','cmp w0,#1','b.lo restore','cmp w0,#11','b.hi restore',
             'ldr x8,[sp]','ldr w9,[sp,#0xb0]','str w0,[x8,#0x4f8]','str w9,[x8,#0x4fc]',
             'restore:']
    rows += [f'ldp q{i},q{i+1},[sp,#{0xc0+i*16:#x}]' for i in range(0,32,2)]
    rows += ['ldr x8,[sp,#0xa0]','msr nzcv,x8']
    rows += [f'ldp x{i},x{i+1},[sp,#{i*8:#x}]' for i in range(0,18,2)]
    rows += ['ldp x18,x30,[sp,#0x90]',f'add sp,sp,#{frame:#x}',
             'ldr w8,[x0,#0x4f8]','b #0']
    raw=asm('\n'.join(rows),at)
    assert len(raw)<0x400
    return_fix(at+len(raw)-4,hook+4)
    assert TEXT[hook:hook+4].hex()=='08f844b9'
    patch(hook,stub=at,label='pickup_'+roll)

def map_state(at,hook,stack_offset,from_state,label):
    raw=asm(f'''ldr w8,[sp,#{stack_offset:#x}]
sub sp,sp,#0x20
stp x9,x10,[sp]
mrs x9,nzcv
str x9,[sp,#0x10]
mov w10,#{from_state & 65535:#x}
movk w10,#{from_state>>16:#x},lsl #16
cmp w8,w10
b.ne restore
mov w8,#0xec02
movk w8,#0x1818,lsl #16
restore:
ldr x9,[sp,#0x10]
msr nzcv,x9
ldp x9,x10,[sp]
add sp,sp,#0x20
b #0''',at)
    return_fix(at+len(raw)-4,hook+4)
    assert TEXT[hook:hook+4]==bytes(KS.asm(f'ldr w8,[sp,#{stack_offset:#x}]',as_bytes=True)[0])
    patch(hook,stub=at,label=label)

def misko(at):
    rows=['sub sp,sp,#0x20','stp x9,x10,[sp]','mrs x9,nzcv','str x9,[sp,#0x10]',
          'cbnz w8,allow','ldr w10,[x19,#0x100]']
    for hash_ in [0x5bb7a11d,0x31c2d76d,0x2b53d1e7]:
        rows += [f'mov w9,#{hash_&65535:#x}',f'movk w9,#{hash_>>16:#x},lsl #16','cmp w10,w9','b.eq allow']
    restore=['ldr x9,[sp,#0x10]','msr nzcv,x9','ldp x9,x10,[sp]','add sp,sp,#0x20']
    rows += restore+['b #0','allow:']+restore+['b #0']
    raw=asm('\n'.join(rows),at)
    return_fix(at+len(raw)-24,0x020fe230)
    return_fix(at+len(raw)-4,0x020fe2c0)
    assert TEXT[0x020fe2bc:0x020fe2c0].hex()=='a8fbff34'
    patch(0x020fe2bc,stub=at,label='misko_visibility_only')

def build(feature_module=None, module_name='qol143', artifact_directory=None, output_root=None):
    if TEXT is None or HOST_IMAGE is None:
        raise ValueError('Explicit game-text and host-image inputs are required')
    artifact_directory=Path(artifact_directory) if artifact_directory else HERE
    payload[:]=bytes(PAYLOAD_SIZE)
    asm_records.clear();patches.clear();fixups.clear()
    if feature_module is None:
        import features_v2
        feature_module=features_v2
    feature_module.emit(sys.modules[__name__])

    # Table entry: target RVA, byte size, local stub offset or zero, zero,
    # expected original bytes (16), replacement bytes (16).
    for i,p in enumerate(patches):
        raw=struct.pack('<4I',p['rva'],p['size'],p['stub'] or 0,0)
        raw+=bytes.fromhex(p['origin']).ljust(16,b'\0')
        raw+=bytes.fromhex(p['replacement'] or '').ljust(16,b'\0')
        payload[TABLE+i*48:TABLE+(i+1)*48]=raw
    FIXTABLE=0xd000
    for i,(site,dest) in enumerate(fixups):
        original_word=struct.unpack_from('<I',payload,site)[0]
        struct.pack_into('<III',payload,FIXTABLE+i*12,site,dest,original_word)
    NATIVETABLE=0xd800
    for i,(name,rva) in enumerate(NATIVE.items()):
        struct.pack_into('<II',payload,NATIVETABLE+i*8,SLOTS[name],rva)
    HOSTTABLE=0xdc00
    host_anchors=[(0x267d0,24),(0x2ff70,44),(0x2b4a0,16)]
    for i,(rva,size) in enumerate(host_anchors):
        assert size<=48
        struct.pack_into('<II',payload,HOSTTABLE+i*56,rva,size)
        payload[HOSTTABLE+i*56+8:HOSTTABLE+i*56+8+size]=HOST_IMAGE[rva:rva+size]
    msgs={'badversion':'qol143: refused: expected TOTK 1.4.3 fingerprint.',
          'badhost':'qol143: refused: host text or writable arena alias mismatch.',
          'badbytes':'qol143: refused before install: original bytes mismatch.',
          'badrange':'qol143: refused: branch range/alignment check failed.',
          'badwrite':'qol143: stopped: host refused a patch; see preceding log.',
          'ok':f'qol143 v{v2_metadata["version"]}: installed {len(patches)} origin-checked patches; pickup durability x2 once; shield/bow zero wear; runtime trigger logging enabled.'}
    if module_name!='qol143':
        msgs={key:value.replace('qol143',module_name) for key,value in msgs.items()}
        msgs['ok']=f'{module_name}: installed {len(patches)} origin-checked patches; Korok, Cave, Well, Chasm map markers.'
    msgloc={}
    off=POOL+0x200
    for name,msg in msgs.items():
        msgloc[name]=off
        raw=msg.encode()+b'\0';payload[off:off+len(raw)]=raw;off+=len(raw)
    init=f'''
stp x29,x30,[sp,#-0x80]!
stp x19,x20,[sp,#0x10]
stp x21,x22,[sp,#0x20]
stp x23,x24,[sp,#0x30]
stp x25,x26,[sp,#0x40]
stp x27,x28,[sp,#0x50]
mov x29,sp
{importcall('Fingerprint')}
mov w8,#0x378f
movk w8,#0x3773,lsl #16
cmp w0,w8
b.ne badversion
mov x0,#0x100
mov x1,xzr
{importcall('ResolveTarget')}
cbz x0,badversion
sub x19,x0,#0x100
{adr('x20',0)}
{loadptr('x24',POOL)}
mov w8,#{HOST['CoreLog'] & 65535:#x}
movk w8,#{HOST['CoreLog']>>16:#x},lsl #16
sub x24,x24,x8
{adr('x21',HOSTTABLE)}
mov w22,#{len(host_anchors)}
verify_host_next:
ldp w8,w10,[x21]
add x9,x24,x8
add x11,x21,#8
verify_host_byte:
ldrb w12,[x9],#1
ldrb w13,[x11],#1
cmp w12,w13
b.ne badhost
subs w10,w10,#1
b.ne verify_host_byte
add x21,x21,#56
subs w22,w22,#1
b.ne verify_host_next
mov w8,#{HOST['write_delta'] & 65535:#x}
movk w8,#{HOST['write_delta']>>16:#x},lsl #16
ldr x23,[x24,x8]
cbz x23,badhost
add x23,x20,x23
{adr('x21',TABLE)}
mov w22,#{len(patches)}
verify_next:
ldr w8,[x21]
add x9,x19,x8
ldr w10,[x21,#4]
add x11,x21,#16
verify_byte:
ldrb w12,[x9],#1
ldrb w13,[x11],#1
cmp w12,w13
b.ne badbytes
subs w10,w10,#1
b.ne verify_byte
ldr w8,[x21,#8]
cbz w8,verify_continue
add x9,x20,x8
ldr w8,[x21]
add x10,x19,x8
sub x11,x9,x10
bl validate_branch
cbz w0,badrange
verify_continue:
add x21,x21,#48
subs w22,w22,#1
b.ne verify_next
{adr('x21',FIXTABLE)}
mov w22,#{len(fixups)}
fix_verify:
ldp w8,w9,[x21]
add x10,x20,x8
add x9,x19,x9
sub x11,x9,x10
bl validate_branch
cbz w0,badrange
add x21,x21,#12
subs w22,w22,#1
b.ne fix_verify
{adr('x21',NATIVETABLE)}
mov w22,#{len(NATIVE)}
native_next:
ldp w8,w9,[x21],#8
add x9,x19,x9
str x9,[x23,x8]
subs w22,w22,#1
b.ne native_next
add x9,x23,#{STATE:#x}
mov w8,#{STATE_POINTER:#x}
str x9,[x23,x8]
{adr('x21',FIXTABLE)}
mov w22,#{len(fixups)}
fix_next:
ldp w8,w9,[x21]
add x21,x21,#12
add x0,x20,x8
add x9,x19,x9
sub x11,x9,x0
bl make_branch
sub x9,x0,x20
str w8,[x23,x9]
subs w22,w22,#1
b.ne fix_next
mov w16,#{HOST['flush'] & 65535:#x}
movk w16,#{HOST['flush']>>16:#x},lsl #16
add x16,x24,x16
blr x16
{adr('x21',TABLE)}
mov w22,#{len(patches)}
install_next:
ldr w8,[x21]
add x0,x19,x8
ldr w8,[x21,#8]
cbz w8,install_simple
add x9,x20,x8
sub x11,x9,x0
bl make_branch
str w8,[sp,#0x60]
mov w8,#0x201f
movk w8,#0xd503,lsl #16
str w8,[sp,#0x64]
add x1,sp,#0x60
b install_write
install_simple:
add x1,x21,#32
install_write:
ldr w2,[x21,#4]
add x3,x21,#16
mov w4,w2
{importcall('Write')}
cbz w0,badwrite
add x21,x21,#48
subs w22,w22,#1
b.ne install_next
{adr('x0',msgloc['ok'])}
b log_exit
badversion:
{adr('x0',msgloc['badversion'])}
b log_exit
badhost:
{adr('x0',msgloc['badhost'])}
b log_exit
badbytes:
{adr('x0',msgloc['badbytes'])}
b log_exit
badrange:
{adr('x0',msgloc['badrange'])}
b log_exit
badwrite:
{adr('x0',msgloc['badwrite'])}
log_exit:
{importcall('Log')}
ldp x19,x20,[sp,#0x10]
ldp x21,x22,[sp,#0x20]
ldp x23,x24,[sp,#0x30]
ldp x25,x26,[sp,#0x40]
ldp x27,x28,[sp,#0x50]
ldp x29,x30,[sp],#0x80
ret
validate_branch:
tst x11,#3
b.ne invalid_branch
asr x8,x11,#27
cmp x8,#0
b.eq valid_branch
cmn x8,#1
b.ne invalid_branch
valid_branch:
mov w0,#1
ret
invalid_branch:
mov w0,wzr
ret
make_branch:
asr x8,x11,#2
and w8,w8,#0x3ffffff
mov w12,#0x14000000
orr w8,w8,w12
ret
'''
    # validate_branch only clobbers x0/x8, not its x11 argument or table registers.
    raw=asm(init,0)
    assert len(raw)<0x1000
    relocs=b''.join(struct.pack('<II',0x04000000| (POOL+i*8),i) for i in range(len(IMPORTS)))
    stringblob=bytearray();stringindex={}
    def string(s):
        if s not in stringindex:
            stringindex[s]=len(stringblob);stringblob.extend(s.encode()+b'\0')
        return stringindex[s]
    def fnv(s):
        h=0x811c9dc5
        for b in s.encode():h=((h^b)*0x1000193)&0xffffffff
        return h
    imports=b''.join(struct.pack('<IIIHH',string(surf),string(name),fnv(name),1,0) for surf,name in IMPORTS)
    required=b''.join(struct.pack('<IHH',string(s),1,0) for s in dict.fromkeys(x[0] for x in IMPORTS))
    po=144;ro=po+len(payload);io=ro+len(relocs);eo=io+len(imports);reqo=eo;so=reqo+len(required)
    content=payload+relocs+imports+required+stringblob
    vals=[144+len(content),zlib.crc32(content),po,len(payload),ro,len(IMPORTS),io,len(IMPORTS),eo,0,reqo,len(required)//8,so,len(stringblob),0,0,0,STATE_SIZE,0,0,0,0,0,0,0,0,0]
    version=tuple(int(x) for x in v2_metadata['version'].split('.'))+(0,)
    header=struct.pack('<IHHBBH16s4H27I',0x57584c4d,1,2,0,0,1,module_name.encode(),*version,*vals)
    assert len(header)==144
    output_root=Path(output_root) if output_root else HERE/'staged/Quality of Life 1.4.3'
    out=output_root/'romfs/WiiXLaunch/mods'/f'{module_name}.wxlm'
    out.parent.mkdir(parents=True,exist_ok=True);out.write_bytes(header+content)
    artifact_directory.mkdir(parents=True,exist_ok=True)
    (artifact_directory/f'{module_name}.payload').write_bytes(payload)
    manifest={'build_id':'277178B7DBA1B6D4949A84778D4ABC58B31B34F5','payload_size':len(payload),'bss_size':STATE_SIZE,'patches':patches,'return_fixups':fixups,'native':NATIVE,'host':HOST,'host_anchors':[{'rva':a,'size':n,'expected_hex':HOST_IMAGE[a:a+n].hex()} for a,n in host_anchors],'output':str(out),'features_v2':v2_metadata}
    (artifact_directory/'module-manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    (artifact_directory/'module-assembly.json').write_text(json.dumps(asm_records,indent=2),encoding='utf-8')
    print(json.dumps(manifest,indent=2))

if __name__=='__main__':build()
