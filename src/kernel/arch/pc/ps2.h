#ifndef KORIN_PC_PS2_H
#define KORIN_PC_PS2_H

int ps2_init(void);
int ps2_owned(void); /* 1 : le noyau tient l'i8042 (moniteur) ; sinon il est à ps2kbd */
const char *ps2_layout(const char *cmdline); /* « keyb=BE » des options du noyau */

#endif
