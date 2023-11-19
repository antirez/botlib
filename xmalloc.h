#ifndef XMALLOC_H
#define XMALLOC_H
void *xmalloc(size_t size);
void *xrealloc(void *ptr, size_t size);
void xfree(void *ptr);
#endif
