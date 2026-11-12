#ifndef KORIN_PC_PS2_H
#define KORIN_PC_PS2_H

int ps2_init(void);
const char *ps2_layout(const char *cmdline); /* « keyb=BE » des options du noyau */

#endif
