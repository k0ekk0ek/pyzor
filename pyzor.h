#ifndef PYZOR_H_INCLUDED
#define PYZOR_H_INCLUDED

#include <sys/types.h>

typedef struct pyzor_digest pyzor_digest_t;

pyzor_digest_t *pyzor_digest_create (pyzor_digest_t **);
void pyzor_digest_destroy (pyzor_digest_t *);
int pyzor_digest_update (pyzor_digest_t *, const unsigned char *, size_t, int);
int pyzor_digest_final (unsigned char *, size_t, pyzor_digest_t *);

#endif

