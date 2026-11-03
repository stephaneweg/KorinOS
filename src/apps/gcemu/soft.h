// soft.h -- le repli sans GPU (soft.cpp)
#ifndef GCEMU_SOFT_H
#define GCEMU_SOFT_H
#include "gc/gc.h"
void softFrame (gc::Machine &m, const gc::GFrame &F, gc::u32 *px, int w, int h, long pitch);
#endif
