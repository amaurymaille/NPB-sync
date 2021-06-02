#include "dedupdef.h"
#include "step.h"

void update_refine_insert_step(unsigned int* step, unsigned int it) {
     
}

void update_refine_extract_step(unsigned int* step, unsigned int it) {

}

void update_dedup_insert_step(unsigned int* step, unsigned int it) {
    
}

void update_dedup_extract_step(unsigned int* step, unsigned int it) {

}

void update_compress_insert_step(unsigned int* step, unsigned int it) {

}

void update_compress_extract_step(unsigned int* step, unsigned int it) {

}

void update_reorder_insert_step(unsigned int* step, unsigned int it) {
    
}

void update_reorder_extract_step(unsigned int* step, unsigned int it) {
    
}

unsigned int refine_initial_insert_step() { return ANCHOR_DATA_PER_INSERT; }
unsigned int refine_initial_extract_step() { return MAX_PER_FETCH; }

unsigned int dedup_initial_insert_step() { return CHUNK_ANCHOR_PER_INSERT; }
unsigned int dedup_initial_extract_step() { return CHUNK_ANCHOR_PER_FETCH; }

unsigned int compress_initial_insert_step() { return ITEM_PER_INSERT; }
unsigned int compress_initial_extract_step() { return ITEM_PER_FETCH; }

unsigned int reorder_initial_insert_step() { return ITEM_PER_INSERT; } 
unsigned int reorder_initial_extract_step() { return ITEM_PER_FETCH; } 

