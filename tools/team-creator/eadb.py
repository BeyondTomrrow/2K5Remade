"""Read EA Sports "DB" roster databases (Madden, NCAA Football).

The format (shared by Madden and NCAA rosters on PS3/360/PC, 2005-2021+):

  header   "DB" u16 8 | u8 big-endian flag, 3 pad | u32 size | u32 0 | u32 tables | u32 crc
  list     tables x { char name[4], u32 offset }  (offset from the end of the list + 4)
  table    u32 crc, u32 ?, u32 record bytes, u32 record bits, u32 0,
           u16 capacity, u16 records, u32 ?, u8 fields, u8 indexes, u16 0, u32 0, u32 crc
           fields x { u32 type, u32 bit offset, char name[4], u32 bits }
           records x <record bytes>  (bit-packed; strings type 0, unsigned 3/4/..)

Big-endian files store four-character names reversed ("YALP" is PLAY).
Madden 20+ files wrap the database in an "FBCHUNKS" container; the database
starts at the "DB" magic.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass, field


@dataclass
class Field:
    name: str
    type: int
    offset: int      # bits
    bits: int


@dataclass
class Table:
    name: str
    record_bytes: int
    capacity: int
    count: int
    fields: list[Field]
    data: bytes
    big: bool
    rows: list[dict] = field(default_factory=list)

    def value(self, rec: bytes, f: Field):
        if f.type == 0:                                    # string
            raw = rec[f.offset // 8:(f.offset + f.bits) // 8]
            return raw.split(b"\0", 1)[0].decode("latin-1")
        if self.big:
            n = int.from_bytes(rec, "big")
            total = len(rec) * 8
            return (n >> (total - f.offset - f.bits)) & ((1 << f.bits) - 1)
        # little-endian files: the record is one little-endian integer, bit
        # offsets from its lowest bit (Madden 21: Geno Smith QB #7 75" 221 lb)
        n = int.from_bytes(rec, "little")
        return (n >> f.offset) & ((1 << f.bits) - 1)


def find_db(data: bytes) -> int:
    i = data.find(b"DB\x00\x08")
    if i < 0:
        raise ValueError("no EA DB database in this file")
    return i


def read(data: bytes) -> dict[str, Table]:
    base = find_db(data)
    big = data[base + 4] == 1
    E = ">" if big else "<"
    ntables = struct.unpack_from(E + "I", data, base + 0x10)[0]
    lst = base + 0x18
    start = lst + ntables * 8
    tables: dict[str, Table] = {}
    for t in range(ntables):
        raw_name = data[lst + t * 8:lst + t * 8 + 4]
        name = (raw_name[::-1] if big else raw_name).decode("latin-1")
        off = struct.unpack_from(E + "I", data, lst + t * 8 + 4)[0]
        h = start + off
        rec_bytes = struct.unpack_from(E + "I", data, h + 8)[0]
        capacity, count = struct.unpack_from(E + "HH", data, h + 20)
        nfields = data[h + 28]
        fields = []
        for k in range(nfields):
            ftype, foff = struct.unpack_from(E + "II", data, h + 40 + k * 16)
            fname = data[h + 48 + k * 16:h + 52 + k * 16]
            fname = (fname[::-1] if big else fname).decode("latin-1")
            fbits = struct.unpack_from(E + "I", data, h + 52 + k * 16)[0]
            fields.append(Field(fname, ftype, foff, fbits))
        rec0 = h + 40 + nfields * 16
        tb = Table(name, rec_bytes, capacity, count, fields, data[rec0:rec0 + capacity * rec_bytes], big)
        tables[name] = tb
    return tables


def rows(t: Table) -> list[dict]:
    out = []
    for r in range(t.count):
        rec = t.data[r * t.record_bytes:(r + 1) * t.record_bytes]
        out.append({f.name: t.value(rec, f) for f in t.fields})
    return out


if __name__ == "__main__":
    import sys
    d = open(sys.argv[1], "rb").read()
    tabs = read(d)
    for n, t in tabs.items():
        print(n, t.count, "/", t.capacity, "records,", len(t.fields), "fields,", t.record_bytes, "bytes")
    if len(sys.argv) > 2:
        t = tabs[sys.argv[2]]
        for r in rows(t)[: int(sys.argv[3]) if len(sys.argv) > 3 else 3]:
            print(r)
