"""Tests for the independent SMZ reference comparator (no firmware)."""
import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from smz_approve import verify


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


if __name__=='__main__':unittest.main()
