/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <syntakt/machines.h>
#define P(n) ST_MACHINE_PARAM("P" #n,"PARAMETER " #n,0,0x7f00,0x4000)
static const struct core_machine_param controls[]={
    P(1),P(2),P(3),P(4),P(5),P(6),P(7),P(8),P(9),P(10),P(11),P(12),P(13),P(14),
    P(15),P(16),P(17),P(18),P(19),P(20),P(21),P(22),P(23),P(24),P(25),P(26),P(27),P(28)
};
static unsigned loaded;
static int on_load(const struct core_machine_def *m){loaded=m->type;return loaded==64?0:-1;}
const struct core_machine_def machine_test_st={
    64,28,"LOADER TEST","LDTEST","SYN",controls,on_load,44,5,9
};
