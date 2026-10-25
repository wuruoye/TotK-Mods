.text
.global arrow_native_initialize
.type arrow_native_initialize,%function
arrow_native_initialize:
    b arrow_native_initialize_impl
.global arrow_native_bind
.type arrow_native_bind,%function
arrow_native_bind:
    adr x9,arrow_native_bind
    ldr x10,.Lbind_offset
    sub x9,x9,x10
    adrp x11,bound_base
    add x11,x11,:lo12:bound_base
    sub x11,x11,x9
    add x11,x11,x0
    ldr x12,[x11]
    cbnz x12,.Lalready_bound
    adrp x13,__got_start
    add x13,x13,:lo12:__got_start
    adrp x14,__got_end
    add x14,x14,:lo12:__got_end
    sub x13,x13,x9
    sub x14,x14,x9
    add x13,x13,x0
    add x14,x14,x0
    ldr x15,.Ldata_offset
.Lbind_loop:
    cmp x13,x14
    b.hs .Lbind_done
    ldr x16,[x13]
    cmp x16,x15
    csel x17,x0,x9,hs
    add x16,x16,x17
    str x16,[x13],#8
    b .Lbind_loop
.Lbind_done:
    adr x12,native_checkpoint_sp
    sub x12,x12,x9
    add x12,x12,x0
    adr x13,.Lcheckpoint_address
    sub x13,x13,x9
    add x13,x13,x0
    str x12,[x13]
    adr x12,native_failed
    sub x12,x12,x9
    add x12,x12,x0
    adr x13,.Lfailed_address
    sub x13,x13,x9
    add x13,x13,x0
    str x12,[x13]
    str x9,[x11]
    mov x0,#1
    ret
.Lalready_bound:
    cmp x12,x9
    cset x0,eq
    ret
.balign 8
.Lbind_offset: .quad arrow_native_bind
.Ldata_offset: .quad __data_start
.global native_invoke
.type native_invoke,%function
native_invoke:
    stp x29,x30,[sp,#-0xb0]!
    stp x19,x20,[sp,#0x10]
    stp x21,x22,[sp,#0x20]
    stp x23,x24,[sp,#0x30]
    stp x25,x26,[sp,#0x40]
    stp x27,x28,[sp,#0x50]
    stp d8,d9,[sp,#0x60]
    stp d10,d11,[sp,#0x70]
    stp d12,d13,[sp,#0x80]
    stp d14,d15,[sp,#0x90]
    ldr x8,.Lcheckpoint_address
    mov x9,sp
    str x9,[x8]
    mov x16,x0
    mov x0,x1
    mov x1,x2
    mov x2,x3
    mov x3,x4
    blr x16
.Lrestore:
    ldp d8,d9,[sp,#0x60]
    ldp d10,d11,[sp,#0x70]
    ldp d12,d13,[sp,#0x80]
    ldp d14,d15,[sp,#0x90]
    ldp x19,x20,[sp,#0x10]
    ldp x21,x22,[sp,#0x20]
    ldp x23,x24,[sp,#0x30]
    ldp x25,x26,[sp,#0x40]
    ldp x27,x28,[sp,#0x50]
    ldp x29,x30,[sp],#0xb0
    ret
.global engine_abort
.type engine_abort,%function
engine_abort:
    ldr x16,.Lfailed_address
    mov w17,#1
    str w17,[x16]
    ldr x16,.Lcheckpoint_address
    ldr x16,[x16]
    mov sp,x16
    mov x0,#0
    b .Lrestore
.global engine_contact
.type engine_contact,%function
engine_contact:
    bl engine_record_contact
    ldr x16,.Lcheckpoint_address
    ldr x16,[x16]
    mov sp,x16
    mov x0,#1
    b .Lrestore
.data
.balign 8
.global native_checkpoint_sp
native_checkpoint_sp: .quad 0
.global native_failed
native_failed: .word 0
.global busy
busy: .word 0
.global active
active: .quad 0
bound_base: .quad 0
.Lcheckpoint_address: .quad 0
.Lfailed_address: .quad 0
