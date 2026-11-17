/* SPDX-License-Identifier: GPL-2.0-or-later
 * Sequencer step order per track (seq-direction.s). Task side only. */
#ifndef PLAYHEAD_SEQ_DIRECTION_H
#define PLAYHEAD_SEQ_DIRECTION_H
#define PLAYHEAD_TRACKS 13u              /* 12 audio tracks and the FX track */
enum {PLAYHEAD_UNCHECKED,PLAYHEAD_READY,PLAYHEAD_REFUSED};
/* The order a track plays its steps in. FORWARDS is stock. */
enum {PLAYHEAD_FORWARDS,PLAYHEAD_BACKWARDS,PLAYHEAD_PENDULUM,PLAYHEAD_RANDOM,PLAYHEAD_WALK,PLAYHEAD_MODES};
/* The live kits, one per pattern. */
#define PLAYHEAD_KITS 128u
#define PLAYHEAD_KIT_BASE 0x41ad6134u
#define PLAYHEAD_KIT_BYTES 0x1618u
/* Each kit's track modes; 0 forwards. Read by the sequencer interrupt. */
extern volatile unsigned char playhead_modes[PLAYHEAD_KITS][16];
/* A live kit's index, PLAYHEAD_KITS for any other address; the playing one. */
unsigned int playhead_kit_index(unsigned int kit);
unsigned int playhead_active_kit(void);
/* Sets kit k's track t mode. Installs the step sites while some kit has a
 * mode and takes them out when none has. 0 done; 1 the stock bytes are not
 * the expected ones: nothing is written, the mode stays FORWARD. */
unsigned int playhead_seq_set(unsigned int k,unsigned int t,unsigned int mode);
/* Kit k's thirteen modes at once (persist.c); null: all FORWARD. */
void playhead_seq_load(unsigned int k,const unsigned char *modes);
/* The step track t plays for the stock forwards count j (seq-direction.c).
 * compute 0: only a step already chosen (the UI task). */
unsigned int playhead_step(unsigned int t,unsigned int j,const unsigned char *pattern,unsigned int compute);
extern unsigned int playhead_seq_state;  /* PLAYHEAD_UNCHECKED, READY or REFUSED */
extern unsigned int playhead_seq_installed;
#endif
