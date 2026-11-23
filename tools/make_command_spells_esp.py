"""Build SummonControlFramework.esp — ESL-flagged plugin with the 3 quick-cast summon command spells.

    python tools/make_command_spells_esp.py <Skyrim.esm> <output.esp>

The plugin (Skyrim SE, form version 44, ESL-flagged .esp, master Skyrim.esm) defines:
  * CSTY override 0x000B729C csFamiliar — zero circle and fallback weights;
  * PROJ 0x800 SCF_CmdAttackTargetProj — fast zero-gravity aimed impulse (speed 4500, range 10000);
  * MGEF 0x801 SCF_CmdHoldPosition_Effect — self, Fire & Forget, Script archetype, 0.1s cast;
  * MGEF 0x802 SCF_CmdFollow_Effect       — self, Fire & Forget, Script archetype, 0.1s cast;
  * MGEF 0x803 SCF_CmdAttackTarget_Effect — aimed, Fire & Forget, Script archetype, 0.1s cast;
  * SPEL 0x811 SCF_CmdHoldPosition        — "Приказ: Держать позицию" (Either Hand, 0 magicka, 0.1s);
  * SPEL 0x812 SCF_CmdFollow              — "Приказ: Следовать" (Either Hand, 0 magicka, 0.1s);
  * SPEL 0x813 SCF_CmdAttackTarget        — "Приказ: В атаку" (Either Hand, 0 magicka, 0.1s).

Record templates (PROJ 0x0004BE35 TurnUndeadProjectile, MGEF 0x0004DBA3 SoulTrapFFActor) are read
from Skyrim.esm so binary layouts match SSE exactly.
"""
import os
import struct
import sys
import zlib

TEMPLATE_MGEF = 0x0004DBA3  # SoulTrapFFActor (Conjuration, Script archetype, purple hand glow + SNDD)
TEMPLATE_PROJ = 0x0004BE35  # TurnUndeadProjectile (clean fast glowing missile)
EITHER_HAND = 0x00013F44    # EquipType EitherHand (Skyrim.esm)

# Local FormIDs (master index 01 = this plugin, ESL range 0x800..0xFFF)
PROJ_ATTACK_TARGET = 0x01000800
MGEF_HOLD_POSITION = 0x01000801
MGEF_FOLLOW        = 0x01000802
MGEF_ATTACK_TARGET = 0x01000803
SPEL_HOLD_POSITION = 0x01000811
SPEL_FOLLOW        = 0x01000812
SPEL_ATTACK_TARGET = 0x01000813
NEXT_OBJECT_ID     = 0x000814

# MGEF DATA offsets (152 bytes, SSE)
OFF_FLAGS      = 0x00
OFF_BASE_COST  = 0x04
OFF_ASSOC      = 0x08
OFF_SKILL      = 0x0C
OFF_RESIST     = 0x10
OFF_COUNTER    = 0x14
OFF_HIT_SHADER = 0x20
OFF_MIN_SKILL  = 0x28
OFF_CAST_TIME  = 0x30
OFF_ARCHETYPE  = 0x40
OFF_AV         = 0x44
OFF_PROJ       = 0x48
OFF_EXPLOSION  = 0x4C
OFF_CAST_TYPE  = 0x50
OFF_DELIVERY   = 0x54
OFF_AV2        = 0x58
OFF_HIT_ART    = 0x60
OFF_IMPACT     = 0x64
OFF_SKILL_MULT = 0x68
OFF_PERK       = 0x84

# MGEF EffectSettingData::Flag
FLAG_NO_HIT_EVENT  = 1 << 4   # 0x00000010
FLAG_NO_DURATION   = 1 << 9   # 0x00000200
FLAG_NO_MAGNITUDE  = 1 << 10  # 0x00000400
FLAG_NO_AREA       = 1 << 11  # 0x00000800
FLAG_HIDE_IN_UI    = 1 << 15  # 0x00008000
FLAG_PAINLESS      = 1 << 26  # 0x04000000

# SPEL SpellItem::SpellFlag
SPEL_COST_OVERRIDE     = 1 << 0   # 0x00000001
SPEL_IGNORE_RESISTANCE = 1 << 20  # 0x00100000
SPEL_NO_ABSORB         = 1 << 21  # 0x00200000
SPEL_NO_DUAL_CAST_MODS = 1 << 23  # 0x00800000
SPEL_FLAGS = SPEL_COST_OVERRIDE | SPEL_IGNORE_RESISTANCE | SPEL_NO_ABSORB | SPEL_NO_DUAL_CAST_MODS

ARCHETYPE_SCRIPT = 1
CAST_FIRE_AND_FORGET = 1
DELIVERY_SELF = 0
DELIVERY_AIMED = 2
SKILL_CONJURATION = 19
QUICK_CAST_TIME = 0.1


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


def zstr_ascii(text):
    return text.encode("ascii") + b"\0"


def zstr_utf8(text):
    return text.encode("utf-8") + b"\0"


def record(sig, fid, payload, flags=0):
    return sig + struct.pack("<IIIHHHH", len(payload), flags, fid, 0, 0, 44, 0) + payload


def group(label, records):
    body = b"".join(records)
    return b"GRUP" + struct.pack("<I", 24 + len(body)) + label + struct.pack("<IHHHH", 0, 0, 0, 0, 0) + body


def build_projectile(template_subs):
    data = bytearray(template_subs[b"DATA"])
    if len(data) != 92:
        sys.exit(f"unexpected PROJ DATA size: {len(data)}")
    # DATA: flags(H), type(H), gravity(f), speed(f), range(f)
    struct.pack_into("<f", data, 0x04, 0.0)      # gravity = 0 (straight laser-like trajectory)
    struct.pack_into("<f", data, 0x08, 4500.0)   # speed = 4500 (snappy impulse)
    struct.pack_into("<f", data, 0x0C, 10000.0)  # range = 10000

    parts = [sub(b"EDID", zstr_ascii("SCF_CmdAttackTargetProj"))]
    # Do NOT copy MODT from Skyrim.esm (form_version 39 has legacy raw texture hashes that crash v44 loader)
    for tag in (b"OBND", b"MODL"):
        if tag in template_subs:
            parts.append(sub(tag, template_subs[tag]))
    parts.append(sub(b"DATA", bytes(data)))
    for tag in (b"NAM1", b"VNAM"):
        if tag in template_subs:
            parts.append(sub(tag, template_subs[tag]))
    return record(b"PROJ", PROJ_ATTACK_TARGET, b"".join(parts))


def build_mgef_data(template_data, delivery, proj_id, keep_hit_vfx):
    data = bytearray(template_data)
    flags = FLAG_NO_HIT_EVENT | FLAG_NO_MAGNITUDE | FLAG_NO_AREA | FLAG_HIDE_IN_UI | FLAG_PAINLESS
    if delivery == DELIVERY_SELF:
        flags |= FLAG_NO_DURATION

    struct.pack_into("<I", data, OFF_FLAGS, flags)
    struct.pack_into("<f", data, OFF_BASE_COST, 0.0)
    struct.pack_into("<I", data, OFF_ASSOC, 0)
    struct.pack_into("<i", data, OFF_SKILL, SKILL_CONJURATION)
    struct.pack_into("<i", data, OFF_RESIST, -1)
    struct.pack_into("<H", data, OFF_COUNTER, 0)
    struct.pack_into("<I", data, OFF_HIT_SHADER, 0)
    struct.pack_into("<I", data, OFF_MIN_SKILL, 0)
    struct.pack_into("<f", data, OFF_CAST_TIME, QUICK_CAST_TIME)
    struct.pack_into("<I", data, OFF_ARCHETYPE, ARCHETYPE_SCRIPT)
    struct.pack_into("<i", data, OFF_AV, -1)
    struct.pack_into("<I", data, OFF_PROJ, proj_id)
    struct.pack_into("<I", data, OFF_EXPLOSION, 0)
    struct.pack_into("<I", data, OFF_CAST_TYPE, CAST_FIRE_AND_FORGET)
    struct.pack_into("<I", data, OFF_DELIVERY, delivery)
    struct.pack_into("<i", data, OFF_AV2, -1)
    if not keep_hit_vfx:
        struct.pack_into("<I", data, OFF_HIT_ART, 0)
        struct.pack_into("<I", data, OFF_IMPACT, 0)
    struct.pack_into("<f", data, OFF_SKILL_MULT, 0.0)
    struct.pack_into("<I", data, OFF_PERK, 0)
    return bytes(data)


def build_mgef(fid, edid, full_name, dnam, mgef_data, sndd_payload):
    parts = [
        sub(b"EDID", zstr_ascii(edid)),
        sub(b"FULL", zstr_utf8(full_name)),
        sub(b"DATA", mgef_data),
    ]
    if sndd_payload is not None:
        parts.append(sub(b"SNDD", sndd_payload))
    parts.append(sub(b"DNAM", zstr_utf8(dnam)))
    return record(b"MGEF", fid, b"".join(parts))


def build_spell(fid, edid, full_name, delivery, mgef_id, duration):
    spit = struct.pack(
        "<IIIfIIffI",
        0,                     # costOverride = 0 magicka
        SPEL_FLAGS,            # kCostOverride | kIgnoreResistance | kNoAbsorb | kNoDualCastMods
        0,                     # spellType = kSpell
        QUICK_CAST_TIME,       # chargeTime = 0.1s
        CAST_FIRE_AND_FORGET,  # castType = kFireAndForget
        delivery,              # delivery = kSelf or kAimed
        0.0,                   # castDuration
        0.0,                   # range (uses projectile range for aimed)
        0,                     # halfCostPerk
    )
    parts = [
        sub(b"EDID", zstr_ascii(edid)),
        sub(b"OBND", b"\0" * 12),
        sub(b"FULL", zstr_utf8(full_name)),
        sub(b"ETYP", struct.pack("<I", EITHER_HAND)),
        sub(b"DESC", zstr_utf8("")),
        sub(b"SPIT", spit),
        sub(b"EFID", struct.pack("<I", mgef_id)),
        sub(b"EFIT", struct.pack("<fII", 0.0, 0, duration)),
    ]
    return record(b"SPEL", fid, b"".join(parts))


# Skyrim.esm uses this style for EncSummonFamiliar and HowlSummonWolf.
FAMILIAR_STYLE = 0x000B729C


def build_familiar_style(template):
    parts = dict(subrecords(template))
    close_range = parts.get(b"CSCR", b"")
    if len(close_range) != 16:
        raise ValueError("unexpected csFamiliar CSCR size")
    # Circle and fall back are the first two floats. Keep flank distance, stalk
    # time and all attack weights from the template.
    parts[b"CSCR"] = struct.pack("<ff", 0.0, 0.0) + close_range[8:]
    return record(b"CSTY", FAMILIAR_STYLE, b"".join(sub(tag, value) for tag, value in parts.items()))


def main():
    if len(sys.argv) != 3:
        sys.exit("Usage: python tools/make_command_spells_esp.py <Skyrim.esm> <output.esp>")

    esm, out = sys.argv[1], sys.argv[2]
    mgef_subs = None
    proj_subs = None
    style = None
    for typ, fid, data in read_records(esm):
        if typ == b"MGEF" and fid == TEMPLATE_MGEF:
            mgef_subs = dict(subrecords(data))
        elif typ == b"PROJ" and fid == TEMPLATE_PROJ:
            proj_subs = dict(subrecords(data))
        elif typ == b"CSTY" and fid == FAMILIAR_STYLE:
            style = build_familiar_style(data)
        if mgef_subs and proj_subs and style:
            break

    if not mgef_subs or b"DATA" not in mgef_subs or len(mgef_subs[b"DATA"]) != 152:
        sys.exit("vanilla MGEF template 0x0004DBA3 not found or unexpected DATA size")
    if not proj_subs or b"DATA" not in proj_subs or len(proj_subs[b"DATA"]) != 92:
        sys.exit("vanilla PROJ template 0x0004BE35 not found or unexpected DATA size")

    if style is None:
        sys.exit("vanilla csFamiliar not found")

    sndd = mgef_subs.get(b"SNDD")

    projectiles = [build_projectile(proj_subs)]

    effects = [
        build_mgef(
            MGEF_HOLD_POSITION,
            "SCF_CmdHoldPosition_Effect",
            "Приказ: Держать позицию",
            "Приказывает призванным существам прервать бой и удерживать текущую позицию.",
            build_mgef_data(mgef_subs[b"DATA"], DELIVERY_SELF, 0, keep_hit_vfx=False),
            sndd,
        ),
        build_mgef(
            MGEF_FOLLOW,
            "SCF_CmdFollow_Effect",
            "Приказ: Следовать",
            "Приказывает призванным существам прервать бой и следовать за вами.",
            build_mgef_data(mgef_subs[b"DATA"], DELIVERY_SELF, 0, keep_hit_vfx=False),
            sndd,
        ),
        build_mgef(
            MGEF_ATTACK_TARGET,
            "SCF_CmdAttackTarget_Effect",
            "Приказ: В атаку",
            "Выпускает быстрый импульс, приказывая призванным существам атаковать поражённую цель.",
            build_mgef_data(mgef_subs[b"DATA"], DELIVERY_AIMED, PROJ_ATTACK_TARGET, keep_hit_vfx=True),
            sndd,
        ),
    ]

    spells = [
        build_spell(
            SPEL_HOLD_POSITION,
            "SCF_CmdHoldPosition",
            "Приказ: Держать позицию",
            DELIVERY_SELF,
            MGEF_HOLD_POSITION,
            duration=0,
        ),
        build_spell(
            SPEL_FOLLOW,
            "SCF_CmdFollow",
            "Приказ: Следовать",
            DELIVERY_SELF,
            MGEF_FOLLOW,
            duration=0,
        ),
        build_spell(
            SPEL_ATTACK_TARGET,
            "SCF_CmdAttackTarget",
            "Приказ: В атаку",
            DELIVERY_AIMED,
            MGEF_ATTACK_TARGET,
            duration=1,
        ),
    ]

    count = len(projectiles) + len(effects) + len(spells) + 1
    header = b"".join([
        sub(b"HEDR", struct.pack("<fII", 1.70, count + 4, NEXT_OBJECT_ID)),
        sub(b"CNAM", zstr_ascii("Sequoia")),
        sub(b"SNAM", zstr_ascii("Summon Control Framework quick-cast command spells (Hold Position, Follow, Attack Target).")),
        sub(b"MAST", zstr_ascii("Skyrim.esm")),
        sub(b"DATA", b"\0" * 8),
    ])

    plugin = record(b"TES4", 0, header, flags=0x200)  # ESL flag
    plugin += group(b"PROJ", projectiles) + group(b"MGEF", effects) + group(b"SPEL", spells) + group(b"CSTY", [style])

    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    with open(out, "wb") as f:
        f.write(plugin)
    print(f"wrote {out}: {count} records ({len(plugin)} bytes)")


if __name__ == "__main__":
    main()
