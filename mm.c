/*
 * mm.c — Implicit free list allocator with headers, footers, 
 *        block splitting, coalescing, and heap extension.
 *
 * This allocator maintains an implicit free list over a heap of blocks.
 * Each block contains:
 *      - a 4-byte header  (size | alloc)
 *      - payload
 *      - a 4-byte footer  (size | alloc)
 *
 * Blocks are aligned to 8 bytes. The heap begins with an 8-byte 
 * allocated prologue block and ends with a 0-size allocated 
 * epilogue header.
 *
 * Allocation:
 *      - rounds the requested size up to meet alignment and include
 *        header/footer overhead
 *      - performs a first-fit search through the implicit list
 *      - splits a free block if it is larger than required
 *      - extends the heap if no suitable block is found
 *
 * Free:
 *      - marks the block free
 *      - coalesces with adjacent free blocks (if any)
 *
 * Realloc:
 * - #TODO: not yet implemented
 *
 * This allocator is simple but fully functional for the Malloc Lab.
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)

// simplify pointer operations
#define PACK(size, alloc) (size | alloc)
#define GET(p) (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = val)

// get block size and free flag
#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 1)

// get header and footer pointer
#define HDRP(bp) ((char *)(bp) - 4)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - 8)

// move to next and prev payload
#define NEXT_BLKP(bp) ((char *)bp + GET_SIZE(HDRP(bp)))
#define PREV_BLKP(bp) ((char *)bp - GET_SIZE((char *)bp - 8))

static char *heap_startp = NULL;

/* Helper functions*/
static void split(void *bp, int newsize);
static void* extend_heap(int newsize);
static void* coalesce(void *bp);

/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    // Allocate 4 words:
    // [0] padding (4 bytes, just to ensure alignment)
    // [1] prologue header (4 bytes)
    // [2] prologue footer (4 bytes)
    // [3] epilogue header (4 bytes)
    if (mem_sbrk(4 * 4) == (void *)-1)
        return -1;

    char *heap_start = mem_heap_lo();

    // Padding word (unused, just makes sure prologue is aligned)
    PUT(heap_start, 0);

    // Prologue block: size = 8, allocated
    PUT(heap_start + 4, PACK(8, 1));    // prologue header
    PUT(heap_start + 8, PACK(8, 1));    // prologue footer

    // Epilogue header: size = 0, allocated
    PUT(heap_start + 12, PACK(0, 1));

    // bp points to prologue *payload*
    heap_startp = heap_start + 8;

    return 0;
}


/* 
 * mm_malloc – Allocate a block of at least 'size' bytes.
 *
 * Rounds the size for alignment, searches the implicit free list for
 * a suitable free block, splits it if necessary, and returns it.
 * If none is found, extends the heap and allocates from the new space.
 */
void *mm_malloc(size_t size)
{
    int newsize = ALIGN(size + 8);

    // search for free block
    char* current = heap_startp;
    while(GET_SIZE(HDRP(current)) != 0){
        if(!GET_ALLOC(HDRP(current)) && GET_SIZE(HDRP(current)) >= newsize){
            if(GET_SIZE(HDRP(current)) != newsize) split(current, newsize);
            PUT(HDRP(current), PACK(newsize, 1));
            PUT(FTRP(current), PACK(newsize, 1));
            return current;
        }
        current = NEXT_BLKP(current);
    }
    // extend heap if no suitable free block is found
    void* bp = extend_heap(newsize);
    if (bp == NULL) return NULL;

    PUT(HDRP(bp), PACK(newsize, 1));
    PUT(FTRP(bp), PACK(newsize, 1));

    return bp;
}

/*
 * mm_free – Marks a block as free and coalesces with neighbors.
 */
void mm_free(void *ptr)
{
    int size = GET_SIZE(HDRP(ptr));
    PUT(HDRP(ptr), PACK(size, 0));
    PUT(FTRP(ptr), PACK(size, 0));
    coalesce(ptr);
}

/*
 * mm_realloc - #TODO
 */
void *mm_realloc(void *ptr, size_t size){
}

/* Helper functions implementations */
static void split(void* bp, int newsize){
    int free_size = GET_SIZE(HDRP(bp));
    int remaining = free_size - newsize;

    PUT(HDRP(bp), PACK(newsize, 1));
    PUT(FTRP(bp), PACK(newsize, 1));
    
    PUT(HDRP(NEXT_BLKP(bp)), PACK(remaining, 0));
    PUT(FTRP(NEXT_BLKP(bp)), PACK(remaining, 0));
}

static void* extend_heap(int newsize){
    void* epilogue_bp = mem_heap_hi() - 3;
    void* last = PREV_BLKP(epilogue_bp);
    if(!GET_ALLOC(HDRP(last))){
        int size_last = GET_SIZE(HDRP(last));
        assert(size_last < newsize);
        newsize -= size_last;
    }
    void* bp;
    if((bp = mem_sbrk(newsize)) == (void *)-1)
        return NULL;

    // init new free block
    PUT(HDRP(bp), PACK(newsize, 0));
    PUT(FTRP(bp), PACK(newsize, 0));
    PUT(NEXT_BLKP(bp), PACK(0, 1)); // new epilogue header

    // coalesce with previous block if possible
    return coalesce(bp);
}

static void* coalesce(void* bp){
    void* prev = PREV_BLKP(bp);
    if(GET_ALLOC(HDRP(prev))) return bp;
    printf("Should not get here");
    int newsize = GET_SIZE(HDRP(bp)) + GET_SIZE(HDRP(prev));
    PUT(HDRP(prev), PACK(newsize, 0));
    PUT(FTRP(bp), PACK(newsize, 0));
    return prev;
}

/* Useful functions for debugging */
static void print_block(void *bp) {
    size_t hsize = GET_SIZE(HDRP(bp));
    size_t halloc = GET_ALLOC(HDRP(bp));
    size_t fsize = GET_SIZE(FTRP(bp));
    size_t falloc = GET_ALLOC(FTRP(bp));

    printf("Block %p:\n", bp);
    printf("   Header: [%zu | %zu]\n", hsize, halloc);
    printf("   Footer: [%zu | %zu]\n", fsize, falloc);
    printf("   Next:   %p\n", NEXT_BLKP(bp));
    printf("   Prev:   %p\n", PREV_BLKP(bp));
}

static void print_heap() {
    printf("\n===== HEAP DUMP =====\n\n");

    void *bp = heap_startp;

    while (GET_SIZE(HDRP(bp)) != 0) {   // stop at epilogue
        print_block(bp);
        bp = NEXT_BLKP(bp);
    }

    printf("Epilogue header at %p\n", HDRP(bp));
    printf("=====================\n\n");
}

int main(){
    mem_init();
    mm_init();
    void * p = mm_malloc(16);
    print_heap();
    mm_free(p);
    print_heap();
    mm_malloc(8); // should take the free spot
    print_heap();
    mm_malloc(8); // should extend the last spot
    print_heap();
}