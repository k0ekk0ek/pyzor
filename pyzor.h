#ifndef PYZOR_H_INCLUDED
#define PYZOR_H_INCLUDED

#include <sys/types.h>

typedef enum pyzor_phase pyzor_phase_t;

enum pyzor_phase {
  pyzor_phase_none = 0,
  pyzor_phase_space,
  pyzor_phase_non_space,
  pyzor_phase_alpha,
  pyzor_phase_delim,
  pyzor_phase_discard
};


typedef struct pyzor_digest pyzor_digest_t;

struct pyzor_digest {
  pyzor_phase_t phase;

  /* line buffer */
  unsigned char *buf;
  size_t len;
  size_t cnt;

  size_t tot; /* total number of lines in buffer */
  size_t nth; /* first line in buffer */

  /* instead of using an intermediate buffer, we write directly to the line
     buffer, as a result we must keep track of more variables */
  size_t delim; /* offset of current line delimiter in bytes */
  size_t off; /* lower bound of portion of string we're currently normalizing */
  size_t lim; // upper bound of portion
  size_t lt; /* smaller than sign, html tag open */
  size_t gt; /* greater than sign, html tag close */
};

pyzor_digest_t *pyzor_digest_create (void);
int pyzor_digest_update (pyzor_digest_t *, const unsigned char *, size_t, int);
int pyzor_digest_finish (unsigned char *, size_t, pyzor_digest_t *);

#endif

