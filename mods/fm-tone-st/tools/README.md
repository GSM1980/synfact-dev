# Digitone Sound import for Elekloader FM TONE

`digitone.py` is the canonical host-only converter. Synfact Transfer calls an
unchanged vendored copy of this module; its local `digitone.py` handles the
outer Transfer object and CRC. No firmware, emulator or MIDI is needed to
convert a record. 

The destination is Elekloader's current FM TONE, type 65, with one stored
RATIO B control and four ratio offsets. Older Synfact layouts are not a
supported destination. Existing Syntakt `.stsnd` files pass through Transfer
unchanged; this does not migrate saved legacy Sounds or patterns.

The converter reads Digitone Sound versions 1–6 and emits Sound v7. It keeps
the supported v2 padded-buffer handling and the fixed record/footer checks.
The caller validates the enclosing data object's product, version and CRC.

```python
record_v7, notes = convert_record(digitone_record)
```

The AMP envelope uses ADSR, DELAY/HOLD 0, the source DEC/SUS/REL and RESET,
and `ATK = max(source ATK, 8)` (raw minimum `0x0800`). Larger attacks,
including their fractional part, are preserved. A raised attack is logged.
This is an import-time value floor, not a runtime limit on parameter edits
or modulation.

These are Sound storage slots, not engine/UI IDs:

| Control | Digitone slot | Elekloader slot | Conversion |
| --- | ---: | ---: | --- |
| ALGO | 17 | 18 | unchanged |
| RATIO C | 18 | 19 | unchanged |
| RATIO A | 19 | 20 | unchanged |
| RATIO B | 20 | 21 | `min(raw // 2, 360) * 88` |
| HARM | 21 | 22 | unchanged |
| DTUN | 22 | 23 | unchanged |
| FDBK | 23 | 26 | unchanged |
| MIX | 24 | 25 | unchanged |
| RATIO OFFSET C | 50 | 69 | unchanged |
| RATIO OFFSET A | 51 | 17 | unchanged |
| RATIO OFFSET B1 | 52 | 24 | unchanged |
| RATIO OFFSET B2 | 53 | 70 | unchanged; record word at `0xa8` |

LFO destinations follow the same mapping, including RATIO B and the offsets.
The former separate B2 value must not overwrite offset C. Unsupported LFO
destinations become None. Chorus, arpeggiator, AMP DRIVE and controller
assignments remain unsupported. The existing digital filter mapping remains
OFF/LP2/HP/LP4 → OFF/LP/HP/LP; filters and effects can sound different.

Run from the repository root:

```sh
python tests/test_st_fm_import.py
python tests/test_units.py
```

The contract tests check all 28 slots against `main.c` and core-st's slot
table, all 361 RATIO B pairs, AMP attack boundaries, offsets, record geometry
and LFO destinations. Transfer additionally tests complete object CRCs,
package detection, old record versions and mocked upload/readback. Device
and complete audio-output validation remain separate.

When updating the converter, refresh Transfer's
`synfact-transfer/vendor/elekloader_digitone.py` byte for byte and its
`PROVENANCE.json` hashes; run both repositories' tests.
