"""Tests for the independent SMZ reference comparator (no firmware)."""
import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from smz_approve import verify, verify_selection


def example(target):
    data=bytearray(104)
    def put(off,n):data[off:off+4]=n.to_bytes(4,'big')
    put(52,16);put(56,1);put(72,52);put(76,0);put(80,0);put(84,target)
    data[88:92]=b'A\0B\0';data[92:96]=b'A\0B\0'
    return bytes(data)


class ComparatorTests(unittest.TestCase):
    def test_equal_strings_at_different_targets(self):
        self.assertEqual(verify(example(4),example(8)),1)

    def test_different_target_or_code_is_rejected(self):
        with self.assertRaises(ValueError):verify(example(4),example(6))
        bad=bytearray(example(4));bad[98]=1
        with self.assertRaises(ValueError):verify(example(4),bytes(bad))

    def test_empty_string_only_on_a_terminator(self):
        # "" folded into the end of "A" in each link: equivalent
        self.assertEqual(verify(example(5),example(9)),1)
        # zero-filled data, such as two weak copies of the subtype selector's registry in
        # .bss, is not an empty string: the browser bank used the wrong copy
        with self.assertRaises(ValueError):verify(example(12),example(13))
        with self.assertRaises(ValueError):verify(example(5),example(12))


class SelectionTests(unittest.TestCase):
    def test_two_machines_share_one_registered_selector(self):
        artifacts=[{'modules':['sampler_input'], 'machines':[66,67,68,69]},
                   {'modules':['st_subtype_module'], 'machines':[71]},
                   {'modules':['st_subtype_module'], 'machines':[70]},
                   {'modules':['comp_module'], 'machines':[]}]
        report={'modules':['sampler_input','st_subtype_module','comp_module'],
                'machines':[66,67,68,69,71,70]}
        verify_selection(artifacts,report)
        for modules in [report['modules'][:-1], list(reversed(report['modules'])),
                        [*report['modules'],'st_subtype_module']]:
            with self.assertRaises(ValueError):
                verify_selection(artifacts,dict(report,modules=modules))
        with self.assertRaises(ValueError):
            verify_selection(artifacts,dict(report,machines=report['machines'][:-1]))


if __name__=='__main__':unittest.main()
