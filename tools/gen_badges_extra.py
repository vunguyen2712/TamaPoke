#!/usr/bin/env python3
"""Original badge art -> badges_extra.h

    python3 tools/gen_badges_extra.py                 # writes badges_extra.h
    python3 tools/gen_badges_extra.py --preview a.png # also a 2x contact sheet

Two sets, both ORIGINAL art drawn for this project (no upstream sheet exists
for either, so nothing here is traced from anything):

  * ALOLA's eight ladder slots. Alola has no gyms -- the slots are six trial
    captains and two kahunas (trainers.h) -- so these are not canon badges but a
    family of sun medallions: one shared frame (gold sun rays, a coral ring) with
    the slot's type as the field colour and a hand-drawn motif in the middle.
  * The ELITE FOUR and CHAMPION of EVERY region (ladder slots 8..12). Four E4
    shapes (shield, diamond, hexagon, star) and one champion crest (crown over a
    laurelled medallion). The frame metal is tinted per REGION and the jewel in
    the middle per the TRAINER'S TYPE, so the same shape reads differently from
    one ladder to the next and every one of the 7 x 5 is generated, none restated.

Output is the same BadgeArt format as badges.h: 32x32, 8bpp indices into a
15-colour RGB565 palette, 0xFF transparent. The E4/champion shapes are stored
ONCE (5 index maps) with a palette per region x slot, which is what keeps the
35 emblems to ~6 KB of flash.

Everything is derived from the repo: the regions and every trainer's type come
from trainers.h, the type names from dex.h, and BADGE_REGIONS from badges.h --
Alola's index must be the first one past the upstream sheets, and the script
refuses to run if it is not.
"""
import math
import os
import re
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')
OUT = os.path.join(ROOT, 'badges_extra.h')
S = 32

# Regions whose eight ladder badges are drawn HERE rather than fetched by
# gen_badges.py. gen_badges.py imports this so it never tries to fetch them.
ORIGINAL_REGIONS = ['ALOLA']


# ---------------------------------------------------------------- repo data
def _read(name):
    return open(os.path.join(ROOT, name)).read()


def type_names():
    m = re.search(r'enum PkType : uint8_t \{([^}]*)\}', _read('dex.h'))
    return [t.strip() for t in m.group(1).split(',') if t.strip().startswith('T_')
            and t.strip() != 'T_NONE']


def ladders():
    """[(REGION, [(name, T_TYPE) x 13])] in TRAINER_SETS order."""
    th = _read('trainers.h')
    sets = re.findall(r'\{\s*(TRAINERS_\w+),\s*"(\w+)"\s*\}', th)
    out = []
    for arr, region in sets:
        body = re.search(r'static const Trainer %s\[TRAINER_COUNT\] = \{(.*?)\n\};' % arr,
                         th, re.S).group(1)
        rows = re.findall(r'\{\s*"([^"]+)",\s*"[^"]*",\s*(T_\w+)', body)
        if len(rows) != 13:
            raise SystemExit('%s: %d trainers, expected 13' % (arr, len(rows)))
        out.append((region, rows))
    return out


def badge_regions():
    return int(re.search(r'#define BADGE_REGIONS (\d+)', _read('badges.h')).group(1))


# ---------------------------------------------------------------- colour
def rgb565(c):
    r, g, b = c
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def mix(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


def ramp(base):
    """dark, mid, light, highlight around one base colour"""
    return [mix(base, (12, 8, 24), 0.55), base, mix(base, (255, 255, 255), 0.4),
            mix(base, (255, 255, 255), 0.75)]


# The jewel / field colour per type. Not TYPE_COL (types.h): those are UI
# accents tuned for text on a pastel background; a gem needs saturation.
TYPE_BASE = {
    'T_NORMAL': (196, 186, 150), 'T_FIRE': (236, 90, 40), 'T_WATER': (52, 124, 230),
    'T_ELECTRIC': (246, 200, 30), 'T_GRASS': (70, 176, 70), 'T_ICE': (120, 210, 230),
    'T_FIGHTING': (196, 50, 40), 'T_POISON': (160, 64, 170), 'T_GROUND': (210, 160, 80),
    'T_FLYING': (150, 160, 240), 'T_PSYCHIC': (240, 80, 140), 'T_BUG': (160, 190, 30),
    'T_ROCK': (176, 150, 70), 'T_GHOST': (110, 80, 160), 'T_DRAGON': (100, 60, 230),
    'T_DARK': (96, 72, 64), 'T_STEEL': (170, 176, 200), 'T_FAIRY': (240, 140, 200),
}

# The E4 frame metal per region, and the champion laurel. Order follows
# TRAINER_SETS; a region added there without a row here fails loudly below.
REGION_METAL = {
    'KANTO': (200, 120, 120), 'JOHTO': (196, 150, 90), 'HOENN': (90, 180, 170),
    'SINNOH': (170, 150, 210), 'UNOVA': (120, 126, 140), 'KALOS': (110, 150, 220),
    'ALOLA': (240, 130, 100),
}
REGION_LAUREL = {
    'KANTO': (220, 60, 50), 'JOHTO': (210, 170, 60), 'HOENN': (40, 160, 120),
    'SINNOH': (130, 100, 200), 'UNOVA': (70, 70, 80), 'KALOS': (60, 110, 210),
    'ALOLA': (60, 170, 90),
}

OUTLINE = (30, 18, 26)
GOLD = [(150, 96, 20), (226, 168, 40), (252, 214, 90), (255, 244, 190)]
CORAL = [(150, 54, 44), (226, 104, 76), (250, 160, 120), (255, 222, 196)]
SUN = [(196, 120, 20), (246, 182, 40), (255, 226, 120)]
PEARL = [(170, 170, 186), (232, 232, 240), (255, 255, 255)]


# ---------------------------------------------------------------- canvas
class Canvas:
    def __init__(self):
        self.px = [[None] * S for _ in range(S)]

    def fill(self, inside, colour):
        """colour(x, y) -> index; inside(fx, fy) tested at the pixel centre,
        with (0, 0) the badge centre. No antialiasing: this is pixel art."""
        for y in range(S):
            for x in range(S):
                fx, fy = x - 15.5, y - 15.5
                if inside(fx, fy):
                    self.px[y][x] = colour(fx, fy) if callable(colour) else colour

    def mask(self, inside):
        return [[inside(x - 15.5, y - 15.5) for x in range(S)] for y in range(S)]

    def outline(self, m, idx=0):
        """Ink every pixel of mask m that touches the outside of it."""
        for y in range(S):
            for x in range(S):
                if not m[y][x]:
                    continue
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    nx, ny = x + dx, y + dy
                    if not (0 <= nx < S and 0 <= ny < S) or not m[ny][nx]:
                        self.px[y][x] = idx
                        break

    def stamp(self, art, keymap, ox=0, oy=0):
        """Paste an ASCII drawing centred on the badge, '.' = leave alone."""
        h = len(art)
        w = max(len(r) for r in art)
        x0 = 16 - (w + 1) // 2 + ox
        y0 = 16 - (h + 1) // 2 + oy
        for j, row in enumerate(art):
            for i, ch in enumerate(row):
                if ch in keymap:
                    self.px[y0 + j][x0 + i] = keymap[ch]

    def indices(self):
        return [0xFF if p is None else p for row in self.px for p in row]


def light(fx, fy, bands):
    """A level 0..bands-1 for a surface lit from the upper left."""
    d = math.hypot(fx, fy) or 1.0
    t = (-(fx + fy) / (d * math.sqrt(2)) + 1) / 2      # 0 (lower right) .. 1
    return max(0, min(bands - 1, int(t * bands)))


# ---------------------------------------------------------------- Alola
# Palette slots for the sun medallion:
#   0 outline | 1-4 ring coral ramp | 5-7 field (type) dark/mid/light
#   8 motif ink | 9-11 motif pearl ramp | 12-14 sun ray ramp
A_FIELD = (5, 6, 7)
MOTIF_KEYS = {'#': 8, 'o': 9, '+': 10, '*': 11, 'f': 7, 'd': 5, 'y': 13, 'Y': 14}

# Hand-drawn 13x13 motifs, one per Alola ladder slot, keyed by the slot's type.
# '#' ink, '*' white, '+' pearl, 'o' grey (the shaded side), 'f'/'d' field
# light/dark for cut-outs.
MOTIFS = {
    'T_NORMAL': [          # a four-point sparkle
        "......#......",
        ".....#*#.....",
        ".....#*#.....",
        "....#***#....",
        "...#**+**#...",
        ".##**+++o*##.",
        "#****+++oo**#",
        ".##*oooooo##.",
        "...#**oo*#...",
        "....#*oo#....",
        ".....#o#.....",
        ".....#o#.....",
        "......#......",
    ],
    'T_WATER': [           # a droplet
        "......#......",
        ".....#*#.....",
        ".....#*#.....",
        "....#***#....",
        "....#***#....",
        "...#**+++#...",
        "..#**++++o#..",
        "..#*f++++o#..",
        "..#*f+++oo#..",
        "..#*++++oo#..",
        "...#++ooo#...",
        "....##oo##...",
        "......##.....",
    ],
    'T_FIRE': [            # a flame
        ".....#.......",
        ".....##......",
        "....#*#...#..",
        "....#**#.##..",
        "...#***+##*#.",
        "...#**++#*+#.",
        "..#**+++o*+#.",
        "..#*++YY++o#.",
        "..#*+YyyY+o#.",
        "..#*+YyyY+o#.",
        "...#+YyyY+#..",
        "....#+yy+#...",
        ".....####....",
    ],
    'T_GRASS': [           # a leaf with its vein
        ".........###.",
        ".......##**#.",
        ".....##****#.",
        "....#****+#..",
        "...#***++d#..",
        "..#**++d++#..",
        "..#*++d+++#..",
        ".#*++d+++o#..",
        ".#*+d+++o#...",
        ".#+d+++oo#...",
        ".#d#oooo#....",
        "#d#.####.....",
        "##...........",
    ],
    'T_ELECTRIC': [        # a lightning bolt
        ".....######..",
        ".....#****#..",
        "....#****#...",
        "....#***#....",
        "...#***#.....",
        "...#**######.",
        "..#*******#..",
        "..#####+o#...",
        "....#+oo#....",
        "....#+o#.....",
        "...#+o#......",
        "...#o#.......",
        "...##........",
    ],
    'T_FAIRY': [           # a heart with a glint
        ".............",
        "..###...###..",
        ".#***#.#**+#.",
        "#**f**#***+o#",
        "#*fff*****+o#",
        "#**f*****++o#",
        "#*******+++o#",
        ".#*****+++o#.",
        "..#***+++o#..",
        "...#*++oo#...",
        "....#+oo#....",
        ".....#o#.....",
        "......#......",
    ],
    'T_DARK': None,        # a crescent moon: drawn by crescent() below
    'T_GROUND': [          # twin peaks over a plain
        ".............",
        "....#........",
        "...#*#.......",
        "...#**#..#...",
        "..#*f+o##*#..",
        "..#ff+oo#*+#.",
        ".#**++oo#*+o#",
        ".#*++ooo#++o#",
        "#**+oooo#+oo#",
        "#############",
        "#ddddddddddd#",
        ".###########.",
        ".............",
    ],
}


def crescent():
    """13x13 crescent opening to the upper right, with a small star."""
    on = lambda x, y: (math.hypot(x - 6, y - 6.5) <= 6.2 and
                       math.hypot(x - 8.6, y - 4.4) > 5.0)
    rows = []
    for y in range(13):
        r = ''
        for x in range(13):
            if not on(x, y):
                r += '.'
                continue
            edge = any(not on(x + dx, y + dy) for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))
            r += '#' if edge else ('*' if x + y < 11 else ('+' if x + y < 15 else 'o'))
        rows.append(r)
    for (x, y, ch) in ((10, 1, '#'), (9, 2, '#'), (10, 2, '*'), (11, 2, '#'), (10, 3, '#')):
        rows[y] = rows[y][:x] + ch + rows[y][x + 1:]
    return rows


MOTIFS['T_DARK'] = crescent()


def alola_badge(tp):
    c = Canvas()
    # sun rays: twelve points between radius 11 and 15.5
    def ray(fx, fy):
        r = math.hypot(fx, fy)
        a = math.atan2(fy, fx)
        k = abs(((a / (2 * math.pi) * 12) % 1.0) - 0.5) * 2   # 1 at a ray's tip
        return r <= 11.5 + 4.2 * k
    c.fill(ray, lambda fx, fy: 12 + light(fx, fy, 3))
    c.outline(c.mask(ray))
    # coral ring
    ring = lambda fx, fy: math.hypot(fx, fy) <= 11.6
    c.fill(ring, lambda fx, fy: 1 + light(fx, fy, 4))
    c.outline(c.mask(ring))
    # the field, in the slot's type colour, shaded the other way so it reads as
    # a recess inside the ring
    field = lambda fx, fy: math.hypot(fx, fy) <= 9.6
    def recess(fx, fy):
        # flat in the middle; the rim of the recess is shadowed on the upper
        # left and catches the light on the lower right
        if math.hypot(fx, fy) < 7.4:
            return A_FIELD[1]
        return A_FIELD[0] if fx + fy < 0 else A_FIELD[2]
    c.fill(field, recess)
    c.outline(c.mask(field))
    c.stamp(MOTIFS[tp], MOTIF_KEYS)
    pal = [OUTLINE] + CORAL + ramp(TYPE_BASE[tp])[:3]
    pal += [mix(ramp(TYPE_BASE[tp])[0], OUTLINE, 0.6)] + PEARL + SUN
    return pal, c.indices()


# ---------------------------------------------------------------- E4 + champion
# Palette slots for the emblems:
#   0 outline | 1-4 frame metal ramp (region) | 5-8 jewel ramp (trainer type)
#   9-12 gold ramp (champion crown) | 13-14 laurel dark/light (region)
def shield(fx, fy):
    if fy < -12.5 or abs(fx) > 12.5:
        return False
    if fy <= 2:
        # rounded top corners
        if fy < -10.5 and abs(fx) > 10.5:
            return math.hypot(abs(fx) - 10.5, fy + 10.5) <= 2
        return True
    return abs(fx) <= 12.5 * (1 - ((fy - 2) / 13.3) ** 1.6)


def diamond(fx, fy):
    return abs(fx) / 13.2 + abs(fy) / 15.2 <= 1


def hexagon(fx, fy):
    # pointy-topped regular hexagon, circumradius 15
    r = 15.0
    ax, ay = abs(fx), abs(fy)
    return ax <= r * math.sqrt(3) / 2 and ay <= r - ax / math.sqrt(3)


def star(fx, fy):
    # eight points: a square and the same square turned 45 degrees
    a = max(abs(fx), abs(fy)) <= 11.2
    b = (abs(fx) + abs(fy)) <= 15.6
    return a or b


def scaled(shape, k, dy=0.0):
    return lambda fx, fy: shape(fx / k, (fy - dy) / k)


def facets(fx, fy, base):
    """Jewel shading: six angular facets around a bright table."""
    r = math.hypot(fx, fy)
    if r < 2.2:
        return base + 2
    a = (math.atan2(fy, fx) + math.pi) / (2 * math.pi)   # 0..1
    f = int(a * 6) % 6
    return base + (2, 1, 0, 0, 1, 2)[f]


def bevel(c, m, hi, lo, raised=True):
    """Light the upper-left inside edge of mask m and shade the lower-right
    (or the other way round for a recess). Outline pixels are left alone."""
    out = lambda x, y: not (0 <= x < S and 0 <= y < S) or not m[y][x] or c.px[y][x] == 0
    put = []
    for y in range(S):
        for x in range(S):
            if not m[y][x] or c.px[y][x] == 0:
                continue
            ul = out(x - 1, y) or out(x, y - 1)
            dr = out(x + 1, y) or out(x, y + 1)
            if ul and not dr:
                put.append((x, y, hi if raised else lo))
            elif dr and not ul:
                put.append((x, y, lo if raised else hi))
    for x, y, v in put:
        c.px[y][x] = v


def emblem_e4(shape):
    c = Canvas()
    c.fill(shape, 2)
    c.outline(c.mask(shape))
    bevel(c, c.mask(shape), 4, 1)
    # an engraved groove inside the rim
    inner = scaled(shape, 0.80)
    c.fill(inner, 2)
    c.outline(c.mask(inner), 1)
    bevel(c, c.mask(inner), 3, 1, raised=False)
    gem = scaled(shape, 0.58)
    c.fill(gem, lambda fx, fy: facets(fx, fy, 5))
    c.outline(c.mask(gem))
    # the jewel's glint
    for (x, y) in ((13, 12), (14, 12), (13, 13)):
        if c.px[y][x] not in (None, 0):
            c.px[y][x] = 8
    # four rivets on the frame
    for (x, y) in ((15, 4), (16, 4), (15, 27), (16, 27)):
        if c.px[y][x] not in (None, 0):
            c.px[y][x] = 4
    return c.indices()


CROWN = [
    "...#.....##.....#...",
    "..#*#...#**#...#*#..",
    "...#....#**#....#...",
    "..###....##....###..",
    "..#*+#..#**#..#*+#..",
    "..#*++#.#*+#.#*++#..",
    "..#**++##*+o##*+o#..",
    "..#*+++++++++++oo#..",
    "..#*+o+o+o+o+o+oo#..",
    "..################..",
]
CROWN_KEYS = {'#': 0, '*': 12, '+': 11, 'o': 10}


def emblem_champion():
    c = Canvas()
    # laurel: on each side four leaves climbing from the bottom of the
    # medallion, each an ellipse laid along the arc, light on its outer half
    leaves = []
    for side in (-1, 1):
        for deg in (28, 62, 96, 130):
            a = math.radians(deg)
            leaves.append((side * 12.6 * math.sin(a), 3 + 12.6 * math.cos(a), a, side))

    # drawn bottom-up and outlined one at a time, so each leaf overlaps the
    # one below it with an inked edge instead of merging into a ring
    for lx, ly, a, side in leaves:
        tx, ty = side * math.cos(a), -math.sin(a)

        def one(fx, fy, lx=lx, ly=ly, tx=tx, ty=ty):
            dx, dy = fx - lx, fy - ly
            u = dx * tx + dy * ty
            v = -dx * ty + dy * tx
            return (u / 4.0) ** 2 + (v / 2.2) ** 2 <= 1
        c.fill(one, lambda fx, fy: 14 if math.hypot(fx, fy - 3) > 12.6 else 13)
        c.outline(c.mask(one))
    # gold medallion
    med = lambda fx, fy: math.hypot(fx, fy - 3) <= 9.8
    c.fill(med, lambda fx, fy: 9 + light(fx, fy - 3, 4))
    c.outline(c.mask(med))
    # metal ring in the region's colour, then the jewel
    ring = lambda fx, fy: math.hypot(fx, fy - 3) <= 7.6
    c.fill(ring, lambda fx, fy: 4 - light(fx, fy - 3, 4))
    c.outline(c.mask(ring))
    gem = lambda fx, fy: math.hypot(fx, fy - 3) <= 5.7
    c.fill(gem, lambda fx, fy: facets(fx, fy - 3, 5))
    c.outline(c.mask(gem))
    for (x, y) in ((13, 16), (14, 16), (13, 17)):
        c.px[y][x] = 8
    c.stamp(CROWN, CROWN_KEYS, oy=-11)
    return c.indices()


E4_SHAPES = [shield, diamond, hexagon, star]


def emblem_palette(region, tp):
    jewel = ramp(TYPE_BASE[tp])
    pal = [OUTLINE] + ramp(REGION_METAL[region]) + jewel + GOLD
    lau = REGION_LAUREL[region]
    pal += [mix(lau, OUTLINE, 0.35), mix(lau, (255, 255, 255), 0.25)]
    return pal


# ---------------------------------------------------------------- output
def c_idx(name, idx):
    lines = ['static const uint8_t %s[%d] = {' % (name, len(idx))]
    for r in range(0, len(idx), 32):
        lines.append(','.join(str(v) for v in idx[r:r + 32]) + ',')
    lines.append('};')
    return lines


def c_pal(pal):
    assert len(pal) == 15, len(pal)
    return '{' + ','.join('0x%04X' % rgb565(c) for c in pal) + '}'


def build():
    regions = ladders()
    names = [r for r, _ in regions]
    br = badge_regions()
    for reg in ORIGINAL_REGIONS:
        if names.index(reg) != br:
            raise SystemExit('%s is ladder %d but badges.h stops at %d: the badge '
                             'arrays would not line up' % (reg, names.index(reg), br))
    missing = [r for r in names if r not in REGION_METAL or r not in REGION_LAUREL]
    if missing:
        raise SystemExit('no emblem colours for %s -- add them to REGION_METAL/LAUREL'
                         % ', '.join(missing))
    types = type_names()

    alola = []
    for reg in ORIGINAL_REGIONS:
        rows = dict(regions)[reg]
        for i in range(8):
            tp = rows[i][1]
            if tp not in MOTIFS:
                raise SystemExit('%s slot %d (%s) is %s: no motif drawn for it'
                                 % (reg, i, rows[i][0], tp))
            alola.append((reg, i, rows[i][0], tp) + alola_badge(tp))
    shapes = [emblem_e4(s) for s in E4_SHAPES] + [emblem_champion()]
    pals = [[emblem_palette(reg, rows[8 + k][1]) for k in range(5)] for reg, rows in regions]
    return names, types, alola, shapes, pals, regions


def write(names, types, alola, shapes, pals, regions):
    o = ['// GENERADO por tools/gen_badges_extra.py - no editar',
         '//',
         '// ORIGINAL art for this project (see CREDITS.md): the Alola ladder',
         "// badges, and the Elite Four / Champion emblems of every region. Same",
         '// BadgeArt format as badges.h; 0xFF is transparent.',
         '#pragma once', '#include "badges.h"', '',
         '#define BADGE_ALOLA_REGION %d' % names.index(ORIGINAL_REGIONS[0]),
         '#define EMBLEM_REGIONS %d' % len(names),
         '#define EMBLEM_SLOTS 5', '']
    for reg, i, who, tp, pal, idx in alola:
        o.append('// %s %d: %s, %s' % (reg, i, who, tp[2:]))
        o.append('static const uint16_t BADGE_%s_%d_PAL[15] = %s;' % (reg, i, c_pal(pal)))
        o += c_idx('BADGE_%s_%d_IDX' % (reg, i), idx)
    for k, idx in enumerate(shapes):
        o.append('// emblem %d: %s' % (k, ['E4 shield', 'E4 diamond', 'E4 hexagon',
                                            'E4 star', 'champion crest'][k]))
        o += c_idx('EMBLEM_%d_IDX' % k, idx)
    o.append('// [region][slot]: region metal + the trainer\'s type for the jewel')
    o.append('static const uint16_t EMBLEM_PAL[EMBLEM_REGIONS][EMBLEM_SLOTS][15] = {')
    for (reg, rows), row in zip(regions, pals):
        o.append('  { // %s: %s' % (reg, ' '.join('%s/%s' % (rows[8 + k][0], rows[8 + k][1][2:])
                                                for k in range(5))))
        for p in row:
            o.append('    %s,' % c_pal(p))
        o.append('  },')
    o.append('};')
    o.append('static const BadgeArt BADGES_ALOLA_ART[8] = {')
    o.append('  ' + ' '.join('{ BADGE_%s_%d_PAL, BADGE_%s_%d_IDX },' % (r, i, r, i)
                             for r, i, *_ in alola))
    o.append('};')
    o.append('static const BadgeArt EMBLEMS_ART[EMBLEM_REGIONS][EMBLEM_SLOTS] = {')
    for ri in range(len(names)):
        o.append('  { ' + ' '.join('{ EMBLEM_PAL[%d][%d], EMBLEM_%d_IDX },' % (ri, k, k)
                                   for k in range(5)) + ' },')
    o.append('};')
    open(OUT, 'w').write('\n'.join(o) + '\n')
    print('wrote %s (%d Alola badges, %d emblem shapes x %d regions)'
          % (os.path.normpath(OUT), len(alola), len(shapes), len(names)))


def png(path, w, h, rgb):
    raw = b''.join(b'\x00' + bytes(rgb[y * w * 3:(y + 1) * w * 3]) for y in range(h))
    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
                           + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def preview(path, names, alola, shapes, pals, scale=3):
    """Alola row, then one row of the five emblems per region."""
    rows = [[(p, idx) for *_, p, idx in alola]]
    for ri in range(len(names)):
        rows.append([(pals[ri][k], shapes[k]) for k in range(5)])
    cell = S * scale + 8
    W, H = cell * 8 + 8, cell * len(rows) + 8
    img = bytearray([236, 232, 220] * W * H)
    for j, row in enumerate(rows):
        for i, (pal, idx) in enumerate(row):
            for y in range(S):
                for x in range(S):
                    v = idx[y * S + x]
                    if v == 0xFF:
                        continue
                    c = pal[v] if isinstance(pal[v], tuple) else pal[v]
                    for dy in range(scale):
                        for dx in range(scale):
                            X = 8 + i * cell + x * scale + dx
                            Y = 8 + j * cell + y * scale + dy
                            img[(Y * W + X) * 3:(Y * W + X) * 3 + 3] = bytes(c)
    png(path, W, H, img)
    print('preview: %s' % path)


def main():
    names, types, alola, shapes, pals, regions = build()
    write(names, types, alola, shapes, pals, regions)
    if '--preview' in sys.argv:
        preview(sys.argv[sys.argv.index('--preview') + 1], names, alola, shapes, pals)


if __name__ == '__main__':
    main()
