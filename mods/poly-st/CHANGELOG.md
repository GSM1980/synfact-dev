# Changelog: POLY

Generated from git history by `tools/changelog.py`. Latest version: 1.4.

## 1.4 (2026-11-09)

- ARP 1.8, FM TONE 1.1, POLY 1.4: TRIG pages, SCALE, falling RANGE, FUNC snaps (`f89c72b`, 2026-11-09)
- POLY: build the native kernels from the stock OS alone; add mod licences (`1fd3823`, 2026-11-09)

## 1.2 (2026-11-06)

- Gate stock hook preimages before safe bank startup (`6032516`, 2026-11-07)
- Core 1.4: page randomize on the new pages (list item 22) (`d4ad3ae`, 2026-11-06)

## 1.1 (2026-11-06)

- POLY 1.1: sequencer steps in a row overlap across the group's voices (`28a7392`, 2026-11-06)

## 1.0 (2026-11-02)

- Set the mod versions: 1.0, and ARP 0.1 (`347ef8e`, 2026-11-02)

## 0.8 (2026-11-02)

- Let FUNC, TRK and trigs work on POLY slave tracks (`5cdc014`, 2026-11-02)
- Drop references to the retired diagnostic mods (`db8317a`, 2026-11-02)

## 0.7 (2026-10-29)

- perf(poly): compact shared modulation publication and cache track bounds (`8827a33`, 2026-10-29)

## 0.6 (2026-10-29)

- Copy POLY Sounds on setup and edits; use native track engines (`7682e91`, 2026-10-29)

## 0.4 (2026-10-29)

- Keep per-voice LFO parameters in SRAM and use native unguarded paths (`7f23657`, 2026-10-29)

## 0.3 (2026-10-29)

- Keep only POLY and shared-LFO playback modes (`c32b24c`, 2026-10-29)

## 0.2.1 (2026-10-29)

- Keep the master Sound on analog POLY slave note events (`e4b1dd6`, 2026-10-29)
- Record physical verification of analog POLY keyboard fix (`ea8a2a0`, 2026-10-29)

## 0.2 (2026-10-29)

- Add analog POLY with bounded publication and cached guards (`ee0e455`, 2026-10-29)
- Add per-voice POLY LFO modes and native mono controls (`4cd9c23`, 2026-10-29)

## 0.1 (2026-10-29)

- Syntakt OS 1.41: core-st, SMZ banks, machines, modules and the modulation skip (`2c41add`, 2026-10-29)
