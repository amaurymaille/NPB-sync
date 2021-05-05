#pragma once

/* When puhsing towards the refine queue / when pulling from the refine queue */
void update_refine_step(unsigned int* step, unsigned int it);
/* When pushing towards the deduplicate queue / pulling from the dedup queue */
void update_dedup_step(unsigned int* step, unsigned int it);
/* When pushing towards the compress queue / pulling from the compress queue */
void update_compress_step(unsigned int* step, unsigned int it);
/* When pushing towards the reorder queue / pulling from the reorder queue */
void update_reorder_step(unsigned int* step, unsigned int it);

inline unsigned int refine_initial_step() { return 1; }
inline unsigned int dedup_initial_step() { return 10; }
inline unsigned int compress_initial_step() { return 20; }
inline unsigned int reorder_initial_step() { return 30; }

