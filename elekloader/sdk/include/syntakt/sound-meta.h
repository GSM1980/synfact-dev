/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ELEKLOADER_ST_SOUND_META_H
#define ELEKLOADER_ST_SOUND_META_H
#include "native.h"
/* API 10 task-side lifecycle notifications on the existing native bus.
 * args={kind,a,b,n}, observers return zero, ownership never transfers.
 * COPY/RELOCATE: destination,source,bytes (after native copy).
 * NEW/FREE: object/begin,zero,object/allocation bytes.
 * CODEC: destination,source,known full project container or zero.
 * S1_SAVE: native record,512-byte output scratch,known source container or zero.
 * S1_LOAD: native record,512-byte input scratch,success.
 * PROJECT_NEW/READY: known native project container,zero,zero.
 * KIT_RECORD_COPY/RECORD_COPY: destination,source,known source container or
 * zero; after native copies of 1600h / 107h bytes respectively.
 * RECORD_BIND: destination,copied byte count,known destination container;
 * publishes the copied metadata into that container's Sound carrier.
 * PATTERN_ENCODE (LFO3 1.3.4): destination record, source pattern, known
 * container or zero; after a successful native pattern encode (400a8264).
 * The native quick save (FUNC+YES, 4001a8d2) encodes the pattern into its
 * save buffer and writes it to disk before it encodes the Kit: a module
 * whose data lives in the pattern record's tail fills it here.
 * Firmware sites remain shared with the established object/codec adapter.
 * Observers ignore kinds they do not know.
 */
enum st_sound_meta_kind {
 ST_SOUND_COPY=1,ST_SOUND_NEW,ST_SOUND_FREE,ST_SOUND_RELOCATE,
 ST_SOUND_KIT_ENCODE,ST_SOUND_KIT_DECODE,ST_SOUND_ENCODE,ST_SOUND_DECODE,
 ST_SOUND_S1_SAVE,ST_SOUND_S1_LOAD,ST_SOUND_PROJECT_NEW,ST_SOUND_PROJECT_READY,
 ST_SOUND_KIT_RECORD_COPY,ST_SOUND_RECORD_COPY,ST_SOUND_RECORD_BIND,
 ST_SOUND_PATTERN_ENCODE,
 /* Task query 34 {kind, Sound record, TrackModel, bytes, 1}. */
 ST_SOUND_SETUP,
 /* Task query 34 {kind, track, 0, 0, 1}, after the native load commits. */
 ST_SOUND_TRACK_LOAD
};
/* OS 1.42 native quick save/reload buffers (4001a36a/4001a458 save,
 * 40019d78/4001b070 reload). The pattern buffers are 5c00h bytes. */
#define ST_SOUND_KIT_SAVE_BUFFER 0x40600554u
#define ST_SOUND_KIT_RELOAD_BUFFER 0x405fe354u
#define ST_SOUND_PATTERN_SAVE_BUFFER 0x40607754u
#define ST_SOUND_PATTERN_RELOAD_BUFFER 0x40601b54u
#define ST_SOUND_S1_EXTENSION 0x11fu
#define ST_SOUND_S1_EXTENSION_BYTES 32u
#define ST_SOUND_S1_BYTES (ST_SOUND_S1_EXTENSION+ST_SOUND_S1_EXTENSION_BYTES)
#endif
