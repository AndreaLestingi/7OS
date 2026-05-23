#include "kheap.h"

static u8 heap[1024 * 64];

struct BlockHeader {
    u32 size;
    bool free;
    BlockHeader* next;
    BlockHeader* prev;
};

static BlockHeader* heap_head = 0;

static inline u32 align8(u32 size) {
    return (size + 7u) & ~7u;
}

static void heap_init() {
    if (heap_head) return;
    heap_head = (BlockHeader*)heap;
    heap_head->size = sizeof(heap) - sizeof(BlockHeader);
    heap_head->free = true;
    heap_head->next = 0;
    heap_head->prev = 0;
}

void* kmalloc(u32 size) {
    if(size == 0) return 0;
    heap_init();
    size = align8(size);

    BlockHeader* cur = heap_head;
    while(cur) {
        if(cur->free && cur->size >= size) {
            if(cur->size >= size + sizeof(BlockHeader) + 8) {
                BlockHeader* split = (BlockHeader*)((u8*)(cur+1)+size);
                split->size = cur->size - size - sizeof(BlockHeader);
                split->free = true;
                split->next = cur->next;
                split->prev = cur;
                if(split->next) split->next->prev = split;
                cur->size = size;
                cur->next = split;
            }
            cur->free = false;
            return (void*)(cur + 1);
        }
        cur = cur->next;
    }
    return 0;
}

static void coalesce(BlockHeader* block) {
    if (block->next && block->next->free) {
        BlockHeader* next = block->next;
        block->size += sizeof(BlockHeader) + next->size;
        block->next = next->next;
        if (block->next) block->next->prev = block;
    }
    if (block->prev && block->prev->free) {
        BlockHeader* prev = block->prev;
        prev->size += sizeof(BlockHeader) + block->size;
        prev->next = block->next;
        if (prev->next) prev->next->prev = prev;
    }
}

void kfree(void* ptr) {
    if (!ptr) return;
    BlockHeader* block = ((BlockHeader*)ptr) - 1;
    block->free = true;
    coalesce(block);
}