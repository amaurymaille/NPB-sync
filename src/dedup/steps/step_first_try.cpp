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
    if (*step >= 12500) {
        *step = 12500;
        return;
    }

    if (it > 0 && it % 10 == 0) {
        *step *= 1.75;
    }
}

void update_reorder_extract_step(unsigned int* step, unsigned int it) {
    if (*step >= 12500) {
        *step = 12500;
        return;
    }

    if (it > 0 && it % 4 == 0) {
        *step *= 1.25;
    }
}

unsigned int refine_initial_insert_step() { return 1; }
unsigned int refine_initial_extract_step() { return 1; }

unsigned int dedup_initial_insert_step() { return 10; }
unsigned int dedup_initial_extract_step() { return 10; }

unsigned int compress_initial_insert_step() { return 20; }
unsigned int compress_initial_extract_step() { return 20; }

unsigned int reorder_initial_insert_step() { return 10; } // Start ASAP
unsigned int reorder_initial_extract_step() { return 10; } // Start ASAP

