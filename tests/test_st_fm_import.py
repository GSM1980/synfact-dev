"""Host-only Digitone import contract against the current authored FM/core.

No device or firmware fixtures. Transfer's enclosing object/CRC tests live
with that app; this suite checks the canonical record converter in Elekloader.
"""
import importlib.util
from pathlib import Path
import re
import struct
import unittest

ROOT = Path(__file__).resolve().parents[1]
FM = ROOT / 'mods/fm-tone-st'
spec = importlib.util.spec_from_file_location('fm_digitone_import', FM / 'tools/digitone.py')
DN = importlib.util.module_from_spec(spec)
spec.loader.exec_module(DN)


def record(**values):
    data = bytearray(302)
    struct.pack_into('>III', data, 0, 0xbeefbace, 6, 0x201)
    data[12:28] = b'INIT IMPORT'.ljust(16, b'\0')
    defaults = {18: 0x300, 19: 0x300, 20: 120, 21: 0x3f00, 25: 0x100,
                28: 0x2000, 29: 0x7f00, 32: 0x2000, 33: 0x7f00,
                36: 0x100, 37: 0x100, 39: 0x100, 40: 0x100,
                50: 0x4000, 51: 0x4000, 52: 0x4000, 53: 0x4000,
                66: 0x800, 67: 0x2000, 68: 0x6000, 69: 0x1800, 76: 0x100}
    defaults.update({int(k): v for k, v in values.items()})
    for pos, slot in enumerate(DN.DN_ORDER):
        struct.pack_into('>H', data, 28 + 2 * pos, defaults.get(slot, 0))
    data[-4:] = bytes.fromhex('bacef00c')
    return data


def converted(**values):
    data, notes = DN.convert_record(record(**values))
    words = struct.unpack_from('>71H', data, 28)
    return dict(zip(DN.ST_ORDER, words)), data, notes


class FmImportTests(unittest.TestCase):
    def test_all_fm_slots_match_authored_control_order_and_core_storage(self):
        source = (FM / 'main.c').read_text()
        controls = source.split('controls[]={', 1)[1].split('};', 1)[0]
        names = re.findall(r'(?:P\(|\{)"([^"]+)"', controls)
        aliases = {'C': 'COFF', 'A': 'AOFF', 'B1': 'B1OF', 'B2': 'B2OF', '---': 'UNUSED'}
        core = (ROOT / 'mods/core-st/machines.c').read_text()
        slots = re.search(r'slots\[28\]=\{([^}]+)\}', core).group(1)
        actual = [int(s.strip(), 0) for s in slots.split(',')]
        self.assertEqual(len(names), 28)
        self.assertEqual([DN.FM_SLOTS[aliases.get(name, name)] for name in names], actual)
        ratio = (FM / 'ratio.h').read_text()
        self.assertRegex(ratio, r'#define FM_RATIO_STEP 88u\b')
        self.assertRegex(ratio, r'#define FM_RATIO_MAX 360u\b')

    def test_every_syn1_word_reaches_its_current_destination(self):
        # Native DN SYN slots 17..24; FEEDBACK/MIX are deliberately not swapped.
        values = {'17': 0x600, '18': 0x1100, '19': 0x2200, '20': 2 * 123,
                  '21': 0x4abc, '22': 0x1234, '23': 0x5678, '24': 0x3210}
        slots, _, _ = converted(**values)
        self.assertEqual(tuple(slots[i] for i in (18, 19, 20, 21, 22, 23, 26, 25)),
                         (0x600, 0x1100, 0x2200, 123 * 88, 0x4abc, 0x1234, 0x5678, 0x3210))

    def test_ratio_b_reencodes_all_361_pairs_without_offset_c_corruption(self):
        for raw in range(722):
            slots, _, _ = converted(**{'20': raw})
            self.assertEqual(slots[21] // 88 * 2, raw & ~1)
            self.assertEqual(slots[69], 0x4000)
        slots, _, _ = converted(**{'20': 0xffff})
        self.assertEqual(slots[21], 360 * 88)

    def test_native_amp_adsr_and_attack_floor(self):
        for attack in (0, 0x7ff, 0x800, 0x801, 0x7f00):
            slots, _, notes = converted(**{'66': attack})
            self.assertEqual(tuple(slots[i] for i in (39, 40, 41, 42, 43, 44, 50, 51)),
                             (0, max(0x800, attack), 0, 0x2000, 0x6000, 0x1800, 0x100, 0x100))
            self.assertEqual((slots[54], slots[58]), (0, 0))  # operator attacks stay intact
            self.assertEqual(any('AMP ATK raised' in n for n in notes), attack < 0x800)

    def test_offsets_last_word_and_sound_geometry(self):
        slots, data, notes = converted(**{'50': 0x1234, '51': 0x2346, '52': 0x3456, '53': 0x4568})
        self.assertEqual(tuple(slots[i] for i in (69, 17, 24, 70)),
                         (0x1234, 0x2346, 0x3456, 0x4568))
        self.assertEqual(len(data), 263)
        self.assertEqual(struct.unpack_from('>H', data, 0xa8)[0], 0x4568)
        self.assertEqual(data[172], 65)
        self.assertEqual(slots[0], 0)
        self.assertEqual(data[-4:], bytes.fromhex('bacef00c'))
        codec = (ROOT / 'mods/core-st/machine-pages.c').read_text()
        self.assertIn('r[0xa8]=s[0xa0];r[0xa9]=s[0xa1];', codec)
        self.assertIn('s[0xa0]=r[0xa8];s[0xa1]=r[0xa9];', codec)
        self.assertFalse(any('OFFSET' in note for note in notes))

    def test_lfo_targets_all_syn_and_offsets_and_unsupported_to_none(self):
        targets = {17: 18, 18: 19, 19: 20, 20: 21, 21: 22, 22: 23, 23: 26,
                   24: 25, 25: 65, 50: 69, 51: 17, 52: 24, 53: 70}
        targets.update({s: s + 27 for s in range(27, 38)})
        targets.update({38: 66, 39: 67, 40: 68, 70: 0})
        for source, target in targets.items():
            slots, _, notes = converted(**{'4': DN.DN_ORDER.index(source) << 8,
                                           '12': DN.DN_ORDER.index(source) << 8})
            self.assertEqual((slots[4], slots[12]), (DN.ST_ORDER.index(target) << 8,) * 2)
            self.assertEqual(any('destination remapped' in n for n in notes), target == 0)


if __name__ == '__main__':
    unittest.main()
