#ifndef __DYNALLOC_H__
#define __DYNALLOC_H__

#define DA_NUM_POOLS 10
#define DA_MAX_CHUNK_SIZE 2048

void dynalloc_init(void);
void *dynalloc_alloc(unsigned int size);
void dynalloc_free(void *ptr);
unsigned long dynalloc_metadata_bytes(unsigned long total_pages); // page_descriptors 陣列在 total_pages 下所需 bytes


#endif /* __DYNALLOC_H__ */