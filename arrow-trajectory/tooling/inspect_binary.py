import struct

def lz4_block(src,size):
    out=bytearray();p=0
    def length(initial):
        nonlocal p
        n=initial
        if n==15:
            while True:
                b=src[p];p+=1;n+=b
                if b!=255:break
        return n
    while p<len(src):
        token=src[p];p+=1;n=length(token>>4)
        if p+n>len(src):raise ValueError('LZ4 literal exceeds input')
        out.extend(src[p:p+n]);p+=n
        if p==len(src):break
        if p+2>len(src):raise ValueError('LZ4 missing match offset')
        distance=src[p]|src[p+1]<<8;p+=2
        if not distance or distance>len(out):raise ValueError('LZ4 invalid match offset')
        n=length(token&15)+4
        pattern=out[-distance:]
        out.extend((pattern*((n+distance-1)//distance))[:n])
        if len(out)>size:raise ValueError('LZ4 exceeds declared size')
    if len(out)!=size:raise ValueError(('LZ4 size mismatch',len(out),size))
    return bytes(out)
