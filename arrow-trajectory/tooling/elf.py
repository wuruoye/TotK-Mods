import struct

def elf_flat(path):
    raw = path.read_bytes()
    assert raw[:6] == b'\x7fELF\x02\x01'
    phoff = struct.unpack_from('<Q', raw, 32)[0]
    phsize, phnum = struct.unpack_from('<HH', raw, 54)
    ph = [struct.unpack_from('<IIQQQQQQ', raw, phoff+i*phsize) for i in range(phnum)]
    loads = [x for x in ph if x[0] == 1 and x[5]]
    assert min(x[3] for x in loads) == 0
    blob = bytearray(max(x[3]+x[5] for x in loads))
    for x in loads: blob[x[3]:x[3]+x[5]] = raw[x[2]:x[2]+x[5]]
    shoff = struct.unpack_from('<Q', raw, 40)[0]
    shsize, shnum = struct.unpack_from('<HH', raw, 58)
    sections = [struct.unpack_from('<IIQQQQIIQQ', raw, shoff+i*shsize) for i in range(shnum)]
    symbols = {}
    for section in sections:
        assert section[1] not in (4, 9) or section[5] == 0, 'unresolved relocations'
        if section[1] != 2: continue
        names = sections[section[6]]
        strings = raw[names[4]:names[4]+names[5]]
        for at in range(section[4], section[4]+section[5], section[9]):
            name, info, other, index, value, size = struct.unpack_from('<IBBHQQ', raw, at)
            label = strings[name:strings.find(b'\0', name)].decode('utf-8') if name else ''
            if label and index and info & 15 == 2: symbols[label] = value
    return bytes(blob), symbols
