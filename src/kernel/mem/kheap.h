#ifndef KHEAP_H
#define KHEAP_H

#include "../util/types.h"

void* kmalloc(u32 size);
void kfree(void* ptr);

inline void* operator new(u32 size) { return kmalloc(size); }
inline void* operator new[](u32 size) { return kmalloc(size); }
inline void operator delete(void* p) { kfree(p); }
inline void operator delete[](void* p) { kfree(p); }

#endif
