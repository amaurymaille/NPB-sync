#pragma once

/* When puhsing towards the refine queue */
void update_refine_insert_step(unsigned int* step, unsigned int it);
/* When pulling from the refine queue */
void update_refine_extract_step(unsigned int* step, unsigned int it);
/* When pushing towards the deduplicate queue */
void update_dedup_insert_step(unsigned int* step, unsigned int it);
/* When pulling from the deduplicate queue */
void update_dedup_extract_step(unsigned int* step, unsigned int it);
/* When pushing towards the compress queue */
void update_compress_insert_step(unsigned int* step, unsigned int it);
/* When pulling from the compress que */
void update_compress_extract_step(unsigned int* step, unsigned int it);
/* When pushing towards the reorder queue */
void update_reorder_insert_step(unsigned int* step, unsigned int it);
/* When pulling from the reorder queue */
void update_reorder_extract_step(unsigned int* step, unsigned int it);

inline unsigned int refine_initial_insert_step() { return 1; }
inline unsigned int refine_initial_extract_step() { return 1; }

inline unsigned int dedup_initial_insert_step() { return 10; }
inline unsigned int dedup_initial_extract_step() { return 10; }

inline unsigned int compress_initial_insert_step() { return 20; }
inline unsigned int compress_initial_extract_step() { return 20; }

inline unsigned int reorder_initial_insert_step() { return 30; }
inline unsigned int reorder_initial_extract_step() { return 30; }

