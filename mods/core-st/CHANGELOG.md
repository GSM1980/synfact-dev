# Changelog: core

Generated from git history by `tools/changelog.py`. Latest version: 2.6.1.

## 2.6.1 (2026-11-23)

- SP playhead, Playhead keys, MD/MnM limits, MD voice knobs, COMP 1.1.0 (`70f4868`, 2026-11-24)
- Core 2.6.1: 32 modules; LFO3 provider, quick save, SP p-lock list, ARP fixes (`a7a19a5`, 2026-11-23)
- Changelogs: core 2.6.1, ARP 1.10.8, LFO3 1.3.4, SP 1.6.1, MD KIT 0.2, SY MnM 0.8.1, System info 1.1.3 (`ebab6b4`, 2026-11-23)

## 2.6.0 (2026-11-22)

- Changelogs: core 2.6.0 and SY MnM 0.8.0 (`43276e6`, 2026-11-22)

## 2.5.0 (2026-11-22)

- Lighter stereo tracks on both CPUs (core 2.5.0, samplers 1.6.0) (`b6d39b8`, 2026-11-22)
- Changelogs: core 2.5.0 and samplers 1.6.0 (`2eed3fc`, 2026-11-22)
- Changelogs: COMP 1.0.0 (`c148d93`, 2026-11-22)
- Changelogs: COMP 1.0.0 ATK/REL in ms (`3ddf7da`, 2026-11-22)

## 2.4.0 (2026-11-20)

- Changelogs: COMP 0.3.0 (`a81bb0b`, 2026-11-21)
- Add FX-bus COMP with shared core services and SRAM optimization (`e1e51bf`, 2026-11-20)
- Changelogs: COMP 0.2.0 and core 2.4.0 FX services (`307482a`, 2026-11-20)

## 2.3.2 (2026-11-19)

- Rename Digi Mono to SY MnM and preserve TYPE on SYN reload (`e9a6dd4`, 2026-11-19)
- Update core 2.3.2 and SY MnM 0.5.1 changelogs (`a171405`, 2026-11-19)

## 2.3.1 (2026-11-19)

- Add Digi Mono subtype picker and keep TYPE outside modulation (`b570b0e`, 2026-11-19)
- Update core 2.3.1 and Digi Mono 0.5.0 changelogs (`1e78098`, 2026-11-19)

## 2.3.0 (2026-11-19)

- Improve Sample Manager sizes, project memory and atomic capacity checks (`ce91a3b`, 2026-11-19)
- Document Digi Mono 0.4.0 OS 1.42 and API 16 integration (`ca7e95d`, 2026-11-19)
- Refresh merged core and Digi Mono release histories (`f7b1014`, 2026-11-19)

## 2.2.0 (2026-11-19)

- Core 2.2.0: 4 MiB bank window, pool above AUDIO's page (contract 06) (`53ac728`, 2026-11-19)
- Changelogs: core 2.2.0 and samplers 1.5.2 (`121e965`, 2026-11-19)

## 2.1.2 (2026-11-19)

- Core 2.1.2: initialize parameter services for module-only banks (`e39f627`, 2026-11-19)
- Changelogs: core 2.1.2 (`84d9065`, 2026-11-19)

## 2.1.1 (2026-11-19)

- Core 2.1.1: free the bank's AUDIO copy in MAIN, cold code at -Os (`523dcc2`, 2026-11-19)
- Changelogs: core 2.1.1 (`2b37c65`, 2026-11-19)

## 2.1.0 (2026-11-18)

- Core 2.1: reduce MAIN audio during project operations (`e222ce8`, 2026-11-18)
- Changelogs: core 2.1 project operation CPU headroom (`4402653`, 2026-11-18)

## 2.0.3 (2026-11-18)

- Fix ARP encoder edits beneath mute and Chorus popup labels (`6bcf23b`, 2026-11-18)

## 2.0.2 (2026-11-18)

- Core 2.0.2: retain native interest filters with the full Playhead bank (`e07cef9`, 2026-11-18)
- Changelogs: core 2.0.2 native interest capacity (`53ad639`, 2026-11-18)
- Record hardware validation of core 2.0.2 MAIN CPU recovery (`dff9f54`, 2026-11-18)
- Clarify core 2.0.2 validation with the latest ARP and POLY releases (`c5bbe1a`, 2026-11-18)
- Update core 2.0.2 changelog with hardware validation (`9c235f3`, 2026-11-18)

## 2.0.1 (2026-11-17)

- Changelogs: POLY 1.7 (`c0e44f9`, 2026-11-18)
- Core 2.0.1: stream stock PCM safely and defer waveforms (`b6406a5`, 2026-11-17)
- Changelogs: core 2.0.1 (`eae3159`, 2026-11-17)
- Changelogs: ARP 1.10.6 (`3d50e09`, 2026-11-17)

## 2.0.0 (2026-11-17)

- move to OS 1.42 (`276667a`, 2026-11-17)
- Changelogs: move to OS 1.42 (`3c0cdcb`, 2026-11-17)

## 1.9.0 (2026-11-16)

- Recorder 1.4 and core 1.9: reduced post-take audio and faster MAIN capture (`085868a`, 2026-11-16)
- Changelogs: core 1.9.0 and Recorder 1.4 (`f48f3ab`, 2026-11-16)

## 1.8.2 (2026-11-11)

- Fix TRIG/ARP mute navigation and reentrant native view cleanup (`5ff821a`, 2026-11-16)
- Generate ARP 1.10.4 and core 1.8.2 changelogs (`84ec2db`, 2026-11-16)
- Filter Digi Mono destinations by Sound TYPE (`7fe932b`, 2026-11-11)
- Document destination filtering releases (`1094bde`, 2026-11-11)

## 1.8.1 (2026-11-11)

- Playhead 1.0: step order per track (reverse, pendulum, random, walk) (`9dd0e4d`, 2026-11-16)
- Changelogs: Playhead 1.0 (`c60f324`, 2026-11-16)
- Changelogs: Chorus 1.2 (`d322360`, 2026-11-15)
- Optimize MAIN paths across core and optional mods (`b747754`, 2026-11-14)
- Changelogs: consolidated MAIN optimization releases (`fcbc288`, 2026-11-14)
- Refresh SYN layouts when Digi Mono TYPE changes (`fe671aa`, 2026-11-11)
- Document live SYN layout fix releases (`83ff21d`, 2026-11-11)

## 1.8.0 (2026-11-10)

- Changelogs: samplers 1.4 (`9c5ba25`, 2026-11-11)
- Changelogs: ARP 1.9, POLY 1.5 (`ccd74b7`, 2026-11-11)
- Stereo tracks, stereo samples and the stereo Recorder (core 1.8.0) (`222a29d`, 2026-11-10)
- Changelogs: core 1.8.0 (`40e397f`, 2026-11-10)
- Sample Manager 1.2: FUNC+UP/DOWN steps one row after an audition (`a4bd154`, 2026-11-10)
- Changelogs: Sample Manager 1.2 (`6bc6bb7`, 2026-11-10)

## 1.7.2 (2026-11-09)

- Core 1.7.2: the base-width filter reads stock poles (`1378804`, 2026-11-09)
- Changelogs: core 1.7.2 (`f9192b9`, 2026-11-09)

## 1.7.1 (2026-11-09)

- Core 1.7.1: write the trial record at boot; safe-boot guards (`8529889`, 2026-11-09)
- Syntakt docs: setup guide, machine guides, and environment variables (`c8071dd`, 2026-11-09)
- Changelogs for the Syntakt core and mods, generated from git history (`5382765`, 2026-11-09)

## 1.7 (2026-11-07)

- Automatically keep staged bank after healthy startup (`42c9d25`, 2026-11-07)

## 1.6 (2026-11-07)

- Gate stock hook preimages before safe bank startup (`6032516`, 2026-11-07)

## 1.5 (2026-11-07)

- Stage SMZ4 banks with core contract and rollback trial (`677b598`, 2026-11-07)

## 1.4 (2026-11-06)

- Core 1.4: page randomize on the new pages (list item 22) (`d4ad3ae`, 2026-11-06)

## 1.3 (2026-11-06)

- Bump integrated sampler core to 1.3 (`23420f9`, 2026-11-06)

## 1.2 (2026-11-06)

- Chorus 1.0 and core 1.2: mixer FX-send events, chained providers (`30b08c6`, 2026-11-06)
- Fix sampler waveform cache, OVER routing and BR response (`60985cb`, 2026-11-06)

## 1.1 (2026-11-03)

- Core: FX track SYN keeps its encoder (DRIVE) (`7e80835`, 2026-11-04)
- Core 1.1: mod list in System info, bank mod-info directory (`1827c4b`, 2026-11-03)

## 0.13.1 (2026-11-01)

- Keep only the Syntakt: remove other devices' mods, examples and apps (`804b4bb`, 2026-11-01)

## 0.13.0 (2026-10-29)

- Add persistent sample folders and move Sample Manager UI to SMZ (`56ba161`, 2026-10-29)

## 0.12.22 (2026-10-30)

- Add Sound-owned Digitone-style arpeggiator on all Syntakt audio tracks (`b69c039`, 2026-10-30)
- Preserve POLY host ABI and compact Sound codecs for ARP integration (`8bae41d`, 2026-10-30)

## 0.12.19 (2026-10-29)

- Add analog POLY with bounded publication and cached guards (`ee0e455`, 2026-10-29)
- Add per-voice POLY LFO modes and native mono controls (`4cd9c23`, 2026-10-29)

## 0.12.18 (2026-10-29)

- Syntakt OS 1.41: core-st, SMZ banks, machines, modules and the modulation skip (`2c41add`, 2026-10-29)
