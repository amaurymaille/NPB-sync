#ifndef ENCODE_COMMON_H
#define ENCODE_COMMON_H

unsigned int hash_from_key_fn(void* k);
int keys_equal_fn(void* key1, void* key2);

int write_file(int fd, u_char type, u_long len, u_char * content);
int create_output_file(const char *outfile); 
void write_chunk_to_file(int fd, chunk_t *chunk);

void sub_Compress(chunk_t *chunk);
int sub_Deduplicate(chunk_t *chunk);

#endif /* ENCODE_COMMON_H */
