#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <openssl/sha.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>

#include "pyzor.h"

/* minimum line length for it to be included in the message digest */
#define PYZOR_LINE_MIN (8)

/* minimum number of non white space characters to remove portion */
#define PYZOR_STRING_MIN (10)

/* message digested as a whole if number of lines is less than or equal to
   this amount of lines */
#define PYZOR_LINES_ATOMIC (4)

#define PYZOR_DELIM_LEN (sizeof (unsigned char) + sizeof (size_t))

#define PYZOR_SIZE_MAX (SIZE_MAX - PYZOR_DELIM_LEN)

      // FIXME: we shouldn't rely on g_ascii_isspace... just copy it from
      //        what I have in ixhash right now...
      //        we'll add a compile time option later so that we can overwrite
      //        it when
      //if (pos == len || g_ascii_isspace (str[pos])) {

typedef enum pyzor_phase pyzor_phase_t;

enum pyzor_phase {
  pyzor_phase_none = 0,
  pyzor_phase_space,
  pyzor_phase_non_space,
  pyzor_phase_alpha,
  pyzor_phase_delim,
  pyzor_phase_discard
};

struct pyzor_digest {
  pyzor_phase_t phase;
  /* line buffer */
  unsigned char *buf;
  size_t len;
  size_t cnt;
  /* line counters */
  size_t tot; /* number of lines in buffer */
  size_t nth; /* number of first line in buffer */
  /* offset counters */
  size_t delim; /* offset of current line delimiter in bytes */
  size_t off; /* part lower bound */
  size_t lim; /* part upper bound */
  size_t lt; /* first HTML tag open */
  size_t gt; /* first HTML tag close */
};

static int pyzor_digest_grow (pyzor_digest_t *, size_t);
static int pyzor_digest_scrub (pyzor_digest_t *);
static int pyzor_digest_part_update (pyzor_digest_t *);
static int pyzor_digest_part_final (pyzor_digest_t *);
static int pyzor_digest_part_term (pyzor_digest_t *);
static void pyzor_digest_part_strip (pyzor_digest_t *); /* strip HTML tags */
static int pyzor_digest_part_forget (pyzor_digest_t *);
static int pyzor_digest_pre_update (pyzor_digest_t *, const void *, size_t,
  int, size_t *);

int
pyzor_digest_create (pyzor_digest_t **digest)
{
  int err;
  pyzor_digest_t *ptr;

  assert (digest);

  if (! (ptr = calloc (1, sizeof (pyzor_digest_t))) ||
      ! (err = pyzor_digest_grow (ptr, 0)))
  {
    pyzor_digest_destroy (ptr);
    return (ENOMEM);
  }

  ptr->cnt = PYZOR_DELIM_LEN;
  ptr->lim = PYZOR_DELIM_LEN;
  *digest = ptr;

  return (0);
}

void
pyzor_digest_destroy (pyzor_digest_t *digest)
{
  assert (digest);

  if (digest) {
    if (digest->buf)
      free (digest->buf);
    memset (digest, 0, sizeof (pyzor_digest_t));
    free (digest);
  }
}

static int
pyzor_digest_grow (pyzor_digest_t *digest, size_t len)
{
  unsigned char *a_buf;
  size_t a_len;

  assert (digest);

  if (len + PYZOR_DIGEST_LEN < digest->len - digest->cnt)
    return (0);
  pyzor_digest_scrub (digest);
  if (len + PYZOR_DIGEST_LEN < digest->len - digest->cnt)
    return (0);

  /* decide how much the line buffer should grow */
  a_len = digest->len > len ? digest->len : len;
  if (a_len >= PYZOR_SIZE_MAX)
    return (EOVERFLOW);
  else if (a_len > PYZOR_SIZE_MAX - a_len)
    a_len = PYZOR_SIZE_MAX;
  else
    a_len = (a_len * 2) + PYZOR_DELIM_LEN;

  /* grow the line buffer */
  if (! (a_buf = realloc (digest->buf, a_len)))
    return (errno);
  memset (a_buf + digest->len, 0, a_len - digest->len);

  digest->buf = a_buf;
  digest->len = a_len;

  return (0);
}

static void
pyzor_digest_scrub (pyzor_digest_t *digest)
{
  size_t cnt, len, nth, off;

  assert (digest);

  /* do not remove lines unless we have at least this many lines */
  if (digest->tot < PYZOR_LINE_MIN) {
    /* spec is hard coded, three lines at twenty percent and three at
       sixty percent */
    nth = ((size_t)((float) digest->tot / 100.0) * 20);

    if (nth > digest->nth) {
      off = 0;

      for (cnt = nth - digest->nth; cnt > 0; cnt--) {
        off += sizeof (unsigned char);
        memcpy (&len, digest->buf + off, sizeof (size_t));
        /* bail if len is zero */
        assert (len);
        off += sizeof (size_t) + len;
      }

      memmove (digest->buf, digest->buf + off, digest->len - off);
      digest->cnt   -= off;
      digest->delim -= off;
      if (digest->off)
        digest->off -= off;
      if (digest->lim)
        digest->lim -= off;
      if (digest->lt)
        digest->lt  -= off;
      if (digest->gt)
        digest->gt  -= off;

      digest->nth = nth;
    }
  }
}

static int
pyzor_digest_part_update (pyzor_digest_t *digest,
                          pyzor_digest_phase_t phase,
                          const void *str,
                          size_t len,
                          ssize_t lt, /* HTML tag open */
                          ssize_t gt) /* HTML tag close */
{
  int err;

  assert (digest);
  assert (str);
  assert (lt == -1 || (lt >= 0 && lt < len));
  assert (gt == -1 || (gt >= 0 && gt < len));

  if ((err = pyzor_digest_grow (digest, len)) != 0)
    return (err);

  memcpy (digest->buf + digest->lim, str, len);

  digest->cnt += len;
  /* do not update offset if previous update was incomplete */
  if (digest->phase == pyzor_phase_none ||
      digest->phase == pyzor_phase_space)
    digest->off = digest->lim;
  if (! digest->lt && lt >= 0)
    digest->lt = digest->off + lt;
  if (! digest->gt && gt >= 0 && digest->lt && gt >= lt)
    digest->gt = digest->off + gt;
  digest->lim += len;
  digest->phase = phase;

  pyzor_digest_part_strip (digest);
  pyzor_digest_part_term (digest);

  return (0);
}

static int
pyzor_digest_part_final (pyzor_digest_t *digest)
{
  int err;
  size_t len, off;

  assert (digest);

  if ((digest->delim + PYZOR_DELIM_LEN) >= digest->lim) {
    len = 0;
  } else {
    len = digest->lim - (digest->delim + PYZOR_DELIM_LEN);
  }

  if (len >= PYZOR_LINE_MIN) {
    off = digest->delim + sizeof (unsigned char);
    memcpy (digest->buf + off, &len, sizeof (size_t));
    /* update line counter only on first invocation */
    if (! digest->tot)
      digest->cnt = 1;
    digest->tot++;
    /* no rewind */
    digest->delim = digest->lim;
  } else {
    /* rewind */
    digest->lim = digest->delim;
  }

  if ((err = pyzor_digest_part_term (digest)) != 0)
    return (err);

  digest->lim = digest->delim + PYZOR_DIGEST_LEN;
  digest->off = digest->lim;
  digest->lt  = 0;
  digest->gt  = 0;
  digest->phase = pyzor_phase_none;

  return (err);
}

static int
pyzor_digest_part_term (pyzor_digest_t *digest)
{
  assert (digest);

  if (digest->len - digest->cnt < PYZOR_DELIM_LEN)
    return (ENOBUFS);

  /* used space counter not updated here on purpose */
  memset (digest->buf + digest->lim, '\0', PYZOR_DELIM_LEN);
  return (0);
}

static void
pyzor_digest_part_strip (pyzor_digest_t *digest)
{
  size_t lim, len, off, pos;

  assert (digest);

  for (; digest->lt && digest->gt > digest->lt; ) {
    if (digest->gt < digest->lim) {
      /* remove portion */
      off = digest->lt;
      lim = digest->gt + 1;
      len = digest->lim - lim;

      memmove (digest->buf + off, digest->buf + lim, len);
      /* update counters */
      digest->lim -= (lim - off);
      if (digest->off > digest->lt)
        digest->off = digest->lim;
      /* find open or close tags located in tail */
      pos = digest->lt;
      digest->lt = 0;
      digest->gt = 0;

      for (; pos < digest->lim && ! digest->lt && ! digest->gt; pos++) {
        if (! digest->lt && digest->buf[pos] == '<')
          digest->lt = pos;
        else if (! digest->gt && digest->buf[pos] == '>' && digest->lt)
          digest->gt = pos;
      }
    } else {
      digest->lim = digest->lt;
      /* no need to look any further */
      digest->lt = 0;
      digest->gt = 0;
    }
  }
}

static int
pyzor_digest_part_forget (pyzor_digest_t *digest, pyzor_digest_phase_t phase)
{
  int err;

  assert (digest);

  if (digest->off <= digest->delim ||
      digest->off >= digest->lim   ||
      digest->lim <= digest->delim)
    return (0);

  digest->cnt -= digest->lim - digest->off;
  digest->lim  = digest->off;
  if (digest->lt >= digest->off)
    digest->lt = 0;
  if (digest->gt >= digest->off)
    digest->gt = 0;
  digest->phase = phase;

  err = pyzor_digest_part_term (digest)
  return (err);
}

static int
pyzor_digest_pre_update (pyzor_digest_t *digest,
                         const void *str,
                         size_t len,
                         int eom,
                         size_t *num)
{
  int err;
  size_t cnt, off, pos;
  ssize_t lt, gt;

  assert (digest);
  assert (str);

  lt = -1;
  gt = -1;
  pos = 0;
  phase = digest->phase;

  if (phase == pyzor_phase_non_space ||
      phase == pyzor_phase_alpha     ||
      phase == pyzor_phase_delim)
  {
    /* unlikely, but you can never be to sure */
    if (digest->off <= digest->delim || digest->off >= digest->lim)
      return (EINVAL);

    cnt = digest->lim - digest->off;

    for (; cnt; pos++) {
      if (pos == len || pyzor_isspace (str[pos])) {
        if ((pos == len && eom) || str[pos] == '\n')
          phase = pyzor_phase_none;
        else if (! eom)
          phase = pyzor_phase_space;

        if ((err = pyzor_digest_part_update (digest, phase, str, pos, lg, gt)))
          return (err);

        if (phase == pyzor_phase_none) {
          if ((err = pyzor_digest_part_final (digest)))
            return (err);
        }

        break;

      } else {
        ++cnt;

        if (cnt >= PYZOR_STRING_MIN || phase == pyzor_phase_delim) {
          if ((err = pyzor_digest_part_forget (digest, pyzor_phase_discard)))
            return (err);
          break;
        } else if (str[pos] == '<') {
          lt = pos;
        } else if (str[pos] == '>') {
          lt = pos;
        } else if (str[pos] == '@') {
          phase = pyzor_phase_delim;
        } else if (str[pos] == ':' && phase == pyzor_phase_alpha) {
          phase = pyzor_phase_delim;
        }
      }
    }
  }

  *num = pos;

  return (0);
}

int
pyzor_digest_update (pyzor_digest_t *digest,
                     const unsigned char *str,
                     size_t len, /* number of bytes in str */
                     int eom) /* indicates end of mime part */
{
  pyzor_phase_t phase, phase_ii;
  size_t off, pos;
  ssize_t lt, gt;

  assert (digest);
  assert (str);

  if ((err = pyzor_digest_pre_update (digest, str, len, eom, &pos)))
    return (err);

  phase = digest->phase;
  lt = -1;
  gt = -1;

  for (;; pos++) {
    if (pos == len || pyzor_isspace (str[pos])) {
      if ((pos == len && eom) || str[pos] == '\n')
        phase_ii = pyzor_phase_none;
      else
        phase_ii = pyzor_phase_space;

      /* update intermediate buffer */
      if (phase == pyzor_phase_non_space ||
          phase == pyzor_phase_alpha     ||
          phase == pyzor_phase_delim)
      {
        if ((err = pyzor_digest_part_update (digest, phase_ii, str+off, pos-off, lt-off, gt-off)))
          return (err);
        if ((err = pyzor_digest_part_final (digest)))
          return (err);
      }

      off = 0;
      lt = -1;
      gt = -1;
      phase = phase_ii;

      if (pos == len)
        break;

    } else if (phase != pyzor_phase_discard) {
      if (off && ((pos - off) + 1) >= PYZOR_STRING_MIN || (phase == pyzor_phase_delim)) {
        phase = pyzor_phase_discard;
      } else if (str[pos] == ':' && (phase == pyzor_phase_alpha)) {
        phase = pyzor_phase_delim;
      } else if (str[pos] == '@' && (phase == pyzor_phase_alpha || phase == pyzor_phase_non_space)) {
        phase = pyzor_phase_delim;
      } else {
        if (phase == pyzor_phase_none || phase == pyzor_phase_space) {
          off = pos;
          if (pyzor_isalpha (str[pos]))
            phase = pyzor_phase_aplha;
          else
            phase = pyzor_phase_non_space;
        } else if (phase == pyzor_phase_alpha) {
          if (! pyzor_isalpha (str[pos]))
            phase = pyzor_phase_non_space;
        }

        if (str[pos] == '<' && lt == -1)
          lt = pos;
        else if (str[pos] == '>' && gt == -1)
          lt = pos;
      }
    }
  }

  return (0);
}

int
pyzor_digest_final (unsigned char *str, size_t len, pyzor_digest_t *digest)
{
  /* Pyzor's DataDigestSpec is hard-coded. if the number of lines after
     normalization is equal to or more than four, the algorithm evaluates
     three lines at twenty percent and three lines at sixty percent. */

  GChecksum *sum;
  size_t cnt, num, pos;
  size_t off, offs[2][2];
  unsigned int inc;

  assert (digest);

  inc = sizeof (size_t);
  sum = g_checksum_new (G_CHECKSUM_SHA1);
  /* FIXME: implement error handling */

  if (digest->tot > PYZOR_LINES_ATOMIC) {
    off = (20.0 * digest->tot) / 100.0;
    offs[0][0] = off;
    offs[0][1] = off + 2;
    off = (60.0 * digest->tot) / 100.0;
    offs[1][0] = off;
    offs[1][1] = off + 2;
  } else {
    offs[0][0] = 0;
    offs[0][1] = digest->tot;
    offs[1][0] = digest->tot;
    offs[1][1] = digest->tot;
  }
//fprintf (stderr, "%d > %d, %d > %d\n", offs[0][0], offs[0][1], offs[1][0], offs[1][1]);
cnt = digest->nth;
//fprintf (stderr, "cnt: %d\n", cnt);
  for (pos = 0; cnt <= offs[1][1]; cnt++) {
//fprintf (stderr, "cnt: %d\n", cnt);
    //num = *(size_t *)digest->buf[pos];
    memcpy (&num, digest->buf + pos, sizeof (size_t));
    pos += inc;

    if ((cnt >= offs[0][0] && cnt <= offs[0][1]) ||
        (cnt >= offs[1][0] && cnt <= offs[1][1]))
    {
fprintf (stderr, "%s:%u: line: %.*s\n", __FILE__, __LINE__, num, digest->buf + pos);
      g_checksum_update (sum, digest->buf + pos, num);
    }

    pos += num;
  }

  strncpy (str, g_checksum_get_string (sum), len);

  return (0);
  /* FIXME: implement destroy buffer etc etc */
}







































































