"""Build SCF_KeywordApiTest.esp - a test plugin for the SCF keyword API (README "Keyword API").

    python tools/make_keyword_test_esp.py <Skyrim.esm> <output.esp>

The plugin (Skyrim SE, form version 44, ESL-flagged .esp, master Skyrim.esm) holds:
  * KYWD SCF_PassThroughFriends / SCF_PassThroughAll / SCF_Untargetable;
  * per keyword, a non-hostile Script-archetype effect carrying it, as
      - a fire-and-forget "target actor" spell, 120 s ("SCF Test: <name>"), cast at a summon/NPC;
      - a constant self ability ("SCF Test Ability: <name>"), for `<ref>.addspell` / `removespell`.
The MGEF DATA blocks are copied from vanilla records of Skyrim.esm (PerkRestoreStaminaFFActor
for the spell, AbFamiliarVisuals for the ability) and then edited, so the 152-byte layout is the
game's own; nothing from Skyrim.esm is redistributed except those numbers inside our records.
"""
import struct
import sys
import zlib

TEMPLATE_TARGET = 0x0004E93F  # PerkRestoreStaminaFFActor: non-hostile, F&F, target actor
TEMPLATE_SELF = 0x000ED5F2    # AbFamiliarVisuals: constant, self, Script archetype
EITHER_HAND = 0x00013F44      # EquipType EitherHand (Skyrim.esm)

# MGEF DATA offsets (152 bytes, SSE)
OFF_FLAGS, OFF_ASSOC, OFF_SKILL, OFF_RESIST = 0x00, 0x08, 0x0C, 0x10
OFF_COUNTER = 0x14
OFF_ARCHETYPE, OFF_AV, OFF_CAST, OFF_DELIVERY, OFF_AV2 = 0x40, 0x44, 0x50, 0x54, 0x58
OFF_PERK = 0x84
FORM_FIELDS_SELF = [0x18, 0x20, 0x24, 0x48, 0x4C, 0x5C, 0x60, 0x64, 0x6C, 0x74, 0x78, 0x7C, 0x80, 0x84]

ARCHETYPE_SCRIPT = 1
FLAG_NO_HIT_EVENT, FLAG_NO_DURATION, FLAG_NO_MAGNITUDE, FLAG_NO_AREA = 0x20, 0x200, 0x400, 0x800
FLAG_FX_PERSIST, FLAG_PAINLESS = 0x1000, 0x04000000

KEYWORDS = [
    ("SCF_PassThroughFriends", "Pass Through Friends"),
    ("SCF_PassThroughAll", "Pass Through All"),
    ("SCF_Untargetable", "Untargetable"),
]


def read_records(path):
    buf = open(path, "rb").read()
    p = 0
    while p < len(buf):
        typ = buf[p:p + 4]
        size = struct.unpack_from("<I", buf, p + 4)[0]
        if typ == b"GRUP":
            p += 24
            continue
        flags, fid = struct.unpack_from("<II", buf, p + 8)
        data = buf[p + 24:p + 24 + size]
        if flags & 0x40000:
            data = zlib.decompress(data[4:])
        yield typ, fid, data
        p += 24 + size


def subrecords(data):
    p = 0
    while p < len(data):
        t = data[p:p + 4]
        n = struct.unpack_from("<H", data, p + 4)[0]
        yield t, data[p + 6:p + 6 + n]
        p += 6 + n


def sub(sig, payload):
    return sig + struct.pack("<H", len(payload)) + payload


def zstr(text):
    return text.encode("cp1252") + b"\0"


def record(sig, fid, payload, flags=0):
    return sig + struct.pack("<IIIHHHH", len(payload), flags, fid, 0, 0, 44, 0) + payload


def group(label, records):
    body = b"".join(records)
    return b"GRUP" + struct.pack("<I", 24 + len(body)) + label + struct.pack("<IHHHH", 0, 0, 0, 0, 0) + body


def mgef_data(template, flags, cast, delivery, clear_forms):
    data = bytearray(template)
    struct.pack_into("<I", data, OFF_FLAGS, flags)
    struct.pack_into("<I", data, OFF_ASSOC, 0)
    struct.pack_into("<i", data, OFF_RESIST, -1)
    struct.pack_into("<H", data, OFF_COUNTER, 0)
    struct.pack_into("<I", data, OFF_ARCHETYPE, ARCHETYPE_SCRIPT)
    struct.pack_into("<i", data, OFF_AV, -1)
    struct.pack_into("<I", data, OFF_CAST, cast)
    struct.pack_into("<I", data, OFF_DELIVERY, delivery)
    struct.pack_into("<i", data, OFF_AV2, -1)
    struct.pack_into("<I", data, OFF_PERK, 0)
    for offset in clear_forms:
        struct.pack_into("<I", data, offset, 0)
    return bytes(data)


def main():
    esm, out = sys.argv[1], sys.argv[2]
    templates = {}
    for typ, fid, data in read_records(esm):
        if typ == b"MGEF" and fid in (TEMPLATE_TARGET, TEMPLATE_SELF):
            templates[fid] = dict(subrecords(data))[b"DATA"]
    if len(templates) != 2 or any(len(d) != 152 for d in templates.values()):
        sys.exit("vanilla MGEF templates not found / unexpected DATA size")

    next_id = 0x01000800  # master index 01 = this plugin (one master); ESL range 0x800..0xFFF
    keywords, effects, spells = [], [], []
    for editor_id, label in KEYWORDS:
        kywd_id = next_id; next_id += 1
        keywords.append(record(b"KYWD", kywd_id, sub(b"EDID", zstr(editor_id))))

        for kind in ("target", "self"):
            mgef_id = next_id; next_id += 1
            spel_id = next_id; next_id += 1
            if kind == "target":
                data = mgef_data(templates[TEMPLATE_TARGET],
                                 FLAG_NO_HIT_EVENT | FLAG_NO_MAGNITUDE | FLAG_NO_AREA | FLAG_FX_PERSIST | FLAG_PAINLESS,
                                 cast=1, delivery=3, clear_forms=[])
                name = f"SCF Test: {label}"
                edid = f"SCFTest_{editor_id}_Target"
            else:
                data = mgef_data(templates[TEMPLATE_SELF],
                                 FLAG_NO_DURATION | FLAG_NO_MAGNITUDE | FLAG_NO_AREA,
                                 cast=0, delivery=0, clear_forms=FORM_FIELDS_SELF)
                name = f"SCF Test Ability: {label}"
                edid = f"SCFTest_{editor_id}_Ability"

            effects.append(record(b"MGEF", mgef_id, b"".join([
                sub(b"EDID", zstr(edid + "_Effect")),
                sub(b"FULL", zstr(name)),
                sub(b"KSIZ", struct.pack("<I", 1)),
                sub(b"KWDA", struct.pack("<I", kywd_id)),
                sub(b"DATA", data),
                sub(b"DNAM", zstr(f"Carries {editor_id} (Summon Control Framework keyword API test).")),
            ])))

            if kind == "target":
                spit = struct.pack("<IIIfIIffI", 0, 0x1, 0, 0.0, 1, 3, 0.0, 0.0, 0)  # manual cost 0, spell, F&F, target actor
                parts = [sub(b"EDID", zstr(edid)), sub(b"OBND", b"\0" * 12), sub(b"FULL", zstr(name)),
                         sub(b"ETYP", struct.pack("<I", EITHER_HAND)), sub(b"DESC", zstr("")),
                         sub(b"SPIT", spit),
                         sub(b"EFID", struct.pack("<I", mgef_id)), sub(b"EFIT", struct.pack("<fII", 0.0, 0, 120))]
            else:
                spit = struct.pack("<IIIfIIffI", 0, 0x1, 4, 0.0, 0, 0, 0.0, 0.0, 0)  # ability, constant, self
                parts = [sub(b"EDID", zstr(edid)), sub(b"OBND", b"\0" * 12), sub(b"FULL", zstr(name)),
                         sub(b"DESC", zstr("")), sub(b"SPIT", spit),
                         sub(b"EFID", struct.pack("<I", mgef_id)), sub(b"EFIT", struct.pack("<fII", 0.0, 0, 0))]
            spells.append(record(b"SPEL", spel_id, b"".join(parts)))

    count = len(keywords) + len(effects) + len(spells)
    header = b"".join([
        sub(b"HEDR", struct.pack("<fII", 1.71, count, next_id & 0xFFFFFF)),
        sub(b"CNAM", zstr("Summon Control Framework")),
        sub(b"SNAM", zstr("Test plugin for the SCF keyword API (SCF_PassThroughFriends / SCF_PassThroughAll / SCF_Untargetable).")),
        sub(b"MAST", zstr("Skyrim.esm")),
        sub(b"DATA", b"\0" * 8),
    ])
    plugin = record(b"TES4", 0, header, flags=0x200)  # ESL flag
    plugin += group(b"KYWD", keywords) + group(b"MGEF", effects) + group(b"SPEL", spells)
    open(out, "wb").write(plugin)
    print(f"wrote {out}: {count} records")


if __name__ == "__main__":
    main()
