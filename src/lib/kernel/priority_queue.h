#ifndef __LIB_KERNEL_PRIORITY_QUEUE_H
#define __LIB_KERNEL_PRIORITY_QUEUE_H

/* Priority Queue

   This is a standard heap-based priority queue.  If the queue needs to grow, it
   performs so by doubling the size of the allocated array, resulting in an
   amortized time complexity of O(log n) for all operations, and size complexity
   of O(n) where n is the number of entries.
*/

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Compares the value of two priority queue elements A and B, given
   auxiliary data AUX.  Returns true if A is less than B, or
   false if A is greater than or equal to B. */
typedef bool pqueue_less_func (const void *a,
                              const void *b,
                              void *aux);

/* Initial storage size of the priority queue. */
#define PQUEUE_INIT_SIZE 4

/* Priority queue */
struct pqueue
  {
    size_t data_size;            /* The actual size of the allocated data */
    size_t size;                 /* The number of the elements in the queue */
    void **data;                 /* The array of pointers to the items stored */
    pqueue_less_func *less;      /* Comparison function */
    void *aux;                   /* Auxilary data for `less' */
  };

/* Basic life cycle */
bool pqueue_init (struct pqueue *, pqueue_less_func *, void *);
void pqueue_destroy (struct pqueue *);

/* Reserving data sizes, to allow for some optimisations by users. */
bool pqueue_reserve (struct pqueue *, size_t);

/* Basic operations */
size_t pqueue_size (struct pqueue *);
bool pqueue_push (struct pqueue *, void *);
void *pqueue_top (struct pqueue *);
void *pqueue_pop (struct pqueue *);

#endif /* lib/kernel/priority_queue.h */
