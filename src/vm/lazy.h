#ifndef VM_LAZY_H
#define VM_LAZY_H

#include <stdint.h>
#include "filesys/file.h"
#include "filesys/off_t.h"

struct lazy_load;

struct lazy_load *create_lazy_load(struct file *file, off_t offset,
																	 uint16_t length);
void lazy_load_lazy(void *kpage, struct lazy_load *lazy);
void lazy_free(struct lazy_load *lazy);

#endif
