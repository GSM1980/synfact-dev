"""Unicorn model of the Syntakt's UI and lock state, shared by the emulator tests.

Virtual model access and notification are explicit constant-time doubles, so the
tests compare instruction counts of algorithmic work, not cycles or CPU percentages.
Run the tests with the Unicorn interpreter from docs/SYNTAKT-BUILD.md.
"""
import hashlib
import json
import struct
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from elekloader import syx
from elekloader.sdk import elf
from unicorn import Uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, UC_HOOK_CODE, UC_HOOK_MEM_READ
from unicorn.m68k_const import *

BASE = 0x40000400
STACK, MODEL, VTABLE, DATA, COMPRESSED = 0x48010000, 0x49000000, 0x49001000, 0x49010000, 0x49040000
RETURN, ACCESS, NOTIFY = 0x4f000000, 0x4a000000, 0x4a000010
STRIDE = 0x2447


class Machine:
    def __init__(self, image):
        self.uc = u = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
        u.ctl_set_cpu_model(UC_CPU_M68K_CFV4E)
        u.mem_map(0x40000000, (len(image) + 0x103ff) & ~0xffff)
        u.mem_write(BASE, image)
        for at, size in ((STACK - 0x10000, 0x20000), (MODEL, 0x50000), (ACCESS, 0x10000), (RETURN, 0x10000)):
            u.mem_map(at, size)
        u.mem_write(MODEL, struct.pack('>I', VTABLE))
        u.mem_write(VTABLE + 0x28, struct.pack('>I', ACCESS))
        u.mem_write(VTABLE + 0x10, struct.pack('>I', NOTIFY))
        u.hook_add(UC_HOOK_CODE, self.code)
        u.hook_add(UC_HOOK_MEM_READ, self.read)
        self.draw_stubs = {}
        self.extra_stubs = {}

    def read(self, u, access, address, size, value, user):
        off = address-DATA
        if 0 <= off < 13*STRIDE and 0x2400 <= off % STRIDE < 0x2447:
            self.flag_reads += 1

    def code(self, u, at, size, user):
        self.count += 1
        self.hits[at] += 1
        if at in self.extra_stubs:
            name, value = self.extra_stubs[at]
            self.stubs[name] += 1
            sp = u.reg_read(UC_M68K_REG_A7)
            target = struct.unpack('>I', bytes(u.mem_read(sp, 4)))[0]
            u.reg_write(UC_M68K_REG_D0, value)
            u.reg_write(UC_M68K_REG_A7, sp + 4)
            u.reg_write(UC_M68K_REG_PC, target)
            return
        if at in self.draw_stubs:
            name = self.draw_stubs[at]
            self.stubs[name] += 1
            sp = u.reg_read(UC_M68K_REG_A7)
            words = struct.unpack('>4I', bytes(u.mem_read(sp, 16)))
            value = 0
            if name == 'resolve':
                value = self.draw_layout[words[2]]
            elif name == 'dynamic':
                value = 1
            elif name == 'GET':
                value = 0x1200
                u.mem_write(words[3], bytes([self.draw_locked]))
            u.reg_write(UC_M68K_REG_D0, value)
            u.reg_write(UC_M68K_REG_A7, sp + 4)
            u.reg_write(UC_M68K_REG_PC, words[0])
            return
        if at in (ACCESS, NOTIFY):
            self.stubs[hex(at)] += 1
            sp = u.reg_read(UC_M68K_REG_A7)
            target = struct.unpack('>I', bytes(u.mem_read(sp, 4)))[0]
            u.reg_write(UC_M68K_REG_D0, DATA if at == ACCESS else 0)
            u.reg_write(UC_M68K_REG_A7, sp + 4)
            u.reg_write(UC_M68K_REG_PC, target)

    def run(self, entry, args):
        u = self.uc
        self.count, self.hits, self.stubs, self.flag_reads = 0, Counter(), Counter(), 0
        sp = STACK - 4 * (len(args) + 1)
        u.mem_write(sp, struct.pack('>%dI' % (len(args) + 1), RETURN, *args))
        u.reg_write(UC_M68K_REG_A7, sp)
        u.emu_start(entry, RETURN, count=2000000)
        assert u.reg_read(UC_M68K_REG_PC) == RETURN
        assert u.reg_read(UC_M68K_REG_A7) == sp + 4
        return {'instructions_including_stub_entries': self.count, 'model_accesses': self.stubs[hex(ACCESS)],
                'notifications': self.stubs[hex(NOTIFY)], 'result': u.reg_read(UC_M68K_REG_D0),
                'lane_flag_reads': self.flag_reads,
                'full_rebuild_calls': self.hits[0x400a811a], 'scan_lane_iterations': self.hits[0x400a82d0],
                'copied_step_words': self.hits[0x400a8374]}

    def pattern(self, lanes, steps=64):
        data = bytearray(b'\xff' * (13 * STRIDE))
        for track in range(13):
            for lane in range(71):
                data[track * STRIDE + 0x2400 + lane] = 0
            for step in range(64):
                struct.pack_into('>H', data, track * STRIDE + 144 * step + 142, 0)
        for n in range(lanes):
            track, lane = n % 13, n // 13
            assert lane < 71
            data[track * STRIDE + 0x2400 + lane] = 1
            for step in range(steps):
                struct.pack_into('>h', data, track * STRIDE + 144 * step + 2 * lane, 0x1200 + n)
        self.uc.mem_write(DATA, bytes(data))
        self.uc.mem_write(COMPRESSED, b'\xff' * 10400)
