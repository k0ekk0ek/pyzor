#include <assert.h>
#include <glib.h>
#include <errno.h>
#include <limits.h>
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

static int pyzor_digest_grow (pyzor_digest_t *, size_t);
static int pyzor_digest_scrub (pyzor_digest_t *);

int
pyzor_digest_init (pyzor_digest_t *digest)
{
  int err;

  assert (digest);

  if (digest) {
    memset (digest, 0, sizeof (pyzor_digest_t));
    err = 0;
  } else {
    err = EINVAL;
  }

  return (err);
}

static int
pyzor_digest_grow (pyzor_digest_t *digest, size_t min)
{
  unsigned char *buf;
  size_t len, max;

  assert (digest);
//fprintf (stderr, "%s:%u: growing\n", __FILE__, __LINE__);
  /* decide how much the line buffer should grow */
  max = digest->len > min ? digest->len : min;
  if (max == SIZE_MAX)
    return (EOVERFLOW);
  else if (max > (SIZE_MAX - max))
    len = SIZE_MAX;
  else
    len = (max * 2) + (sizeof (size_t) * 2);

//fprintf (stderr, "%s:%u: len: %lu, new len: %lu\n", __FILE__, __LINE__, digest->len, len);

  if (min > (len - min))
    return (EOVERFLOW);

  /* grow the line buffer */
  if (! (buf = realloc (digest->buf, len)))
    return (errno);
  memset (buf + digest->len, 0, len - digest->len);

  digest->buf = buf;
  digest->len = len;

  return (0);
}

static int
pyzor_digest_scrub (pyzor_digest_t *digest)
{
  size_t cnt, inc, min, num;
  size_t len;
  size_t off;

  /* do not remove lines unless we have at least this many lines */
  if (digest->tot < PYZOR_LINE_MIN)
    return (0);

  /* spec is hard coded for now, we get three lines at twenty percent and
     three at sixty percent */
  min = (size_t)((digest->tot * 20.0) / 100.0);

  if (min > digest->nth) {
    off = 0;

    for (cnt = 0, num = min - digest->nth; cnt < num; cnt++) {
      memcpy (&inc, digest->buf + off, sizeof (size_t));
      inc += sizeof (size_t);
      off += inc;
    }

    memmove (digest->buf, digest->buf + off, digest->len - off);
fprintf (stderr, "nth: %d\n", digest->nth);
    digest->nth += cnt;
fprintf (stderr, "nth: %d\n", digest->nth);
    digest->delim -= off;
    if (digest->off)
      digest->off -= off;
    if (digest->lim)
      digest->lim -= off;
    if (digest->lt)
      digest->lt -= off;
    if (digest->gt)
      digest->gt -= off;
  }

  // for

  return (0);
}

static int
pyzor_digest_part_put (pyzor_digest_t *digest,
                       const void *str,
                       size_t len,
                       ssize_t lt, /* html tag open */
                       ssize_t gt, /* html tag close */
                       int eop) /* end of part */
{
  int err;
  size_t lim, off, pos;
  unsigned int inc;

  assert (digest);
  assert (str);

  if (digest->lim > digest->delim) {
    inc = 0;
    off = digest->lim;
  } else {
    inc = sizeof (size_t);
    off = digest->delim + inc;
  }

  /* verify line buffer is large enough, scrub and grow if necessary */
  if ((digest->len - digest->cnt) < (len + inc)) {
    //pyzor_digest_scrub (digest);
    if ((digest->len - digest->cnt) < (len + inc) && (err = pyzor_digest_grow (digest, len + inc)))
      return (err);
  }
//fprintf (stderr, "put: '%.*s'\n", len, str);
  if (digest->lt == 0 && lt >= 0)
    digest->lt = off + lt;
  if (digest->gt == 0 && gt >= 0 && digest->lt && gt >= lt)
    digest->gt = off + gt;

  memcpy (digest->buf + off, str, len);
  digest->cnt += len + inc;
  digest->off  = off;
  digest->lim  = off + len;

  /* FIXME: place removal of html tags in a seperate function */
  if (eop) {
    for (; digest->lt > 0 && digest->gt >= digest->lt; ) {
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
        
        for (; pos < digest->lim; pos++) {
          if (digest->buf[pos] == '<' && digest->lt == 0) {
            digest->lt = pos;
          } else if (digest->buf[pos] == '>' && digest->lt != 0) {
            digest->gt = pos;
            break;
          }
        }
      } else {
        digest->lim = digest->lt;
        /* no need to look any further */
        digest->lt = 0;
        digest->gt = 0;
      }
    }
  }

  return (0);
}

static int
pyzor_digest_part_pop (pyzor_digest_t *digest)
{
  assert (digest);

  /* discard operation allowed only once */
  if (digest->lim <= digest->delim || digest->lim <= digest->off)
    return (EINVAL);

  if (digest->off > digest->delim)
    digest->lim = --digest->off;
  else
    digest->lim = digest->off;

  return (0);
}

static int
pyzor_digest_part_end (pyzor_digest_t *digest)
{
  size_t len;

  assert (digest);

  // implement support for handling of html tags?!?!

  if (digest->lim <= digest->delim)
    len = 0;
  else
    len = (digest->lim - digest->delim) - sizeof (size_t);

  if (len >= PYZOR_LINE_MIN) {
    //(size_t)*(digest->buf + digest->delim) = len;
fprintf (stderr, "%.*s\n", (digest->lim - digest->delim) - sizeof (size_t) ,digest->buf + (digest->delim + sizeof (size_t)));
    memcpy (digest->buf + digest->delim, &len, sizeof (size_t));
    digest->tot++;
    digest->delim = digest->lim;
  }

  digest->off = 0;
  digest->lim = 0;
  digest->lt = 0;
  digest->gt = 0;

  return (0);
}

int
pyzor_digest_update (pyzor_digest_t *digest,
                     const unsigned char *str,
                     size_t len, /* number of bytes in str */
                     int eom) /* indicates end of mime part */
{
  int loop;
  pyzor_phase_t phase;
  size_t cnt, off, pos;
  ssize_t lt, gt;

  assert (digest);
  assert (str);

  /* assert current state */
  lt = -1;
  gt = -1;
  off = 0;
  pos = 0;
  phase = digest->phase;

  // inproper handling of html tags... we must handle the situation where we
  // update the digest part but we're not done... ad an extra attribute?!?!
  // we cannot use the state if we return

//fprintf (stderr, "%.*s", len, str);

  if (phase == pyzor_phase_non_space
   || phase == pyzor_phase_alpha
   || phase == pyzor_phase_delim)
  {
    cnt = digest->lim - digest->off;
//fprintf (stderr, "cur phase: %d, cnt: %lu\n", phase, cnt);
    /* we only write these states if we're not exactly sure about the
       outcome */
    for (; ; pos++) {
      if (pos == len || g_ascii_isspace (str[pos])) {
        pyzor_digest_part_put (digest, str, pos, lt, gt, (pos == len ? 1 : 0));
        if (eom || str[pos] == '\n') {
          phase = pyzor_phase_none;
          pyzor_digest_part_end (digest);
          break;
        } else {
          phase = pyzor_phase_space;
        }
        break;
      } else {
        cnt++;
        if (cnt >= PYZOR_STRING_MIN || phase == pyzor_phase_delim) {
          phase = pyzor_phase_discard;
          pyzor_digest_part_pop (digest);
          break;
        } else if (str[pos] == '<') {
          lt = pos;
        } else if (str[pos] == '>') {
          lt = pos;
        } else if (str[pos] == '@' || (str[pos] == ':' && phase == pyzor_phase_alpha)) {
          phase = pyzor_phase_delim;
        }
      }
    }
  }

  for (;; pos++) {
    if (g_ascii_isspace (str[pos]) || pos == len) {
      /* update intermediate buffer */
      if (phase == pyzor_phase_non_space
       || phase == pyzor_phase_alpha
       || phase == pyzor_phase_delim)
      {
        pyzor_digest_part_put (digest, str + off, pos - off, lt - off, gt - off, 1);
//        fprintf (stderr, "lt: %ld, gt: %ld, update: '%.*s'\n", lt, gt, pos - off, str + off);
//#ifdef PYZOR_DEBUG
      } else if (off) {
//        fprintf (stderr, "discard: '%.*s'\n", pos - off, str + off);
//#endif
      }

      lt = -1;
      gt = -1;
      off = 0;
      phase = pyzor_phase_space;

      if (str[pos] == '\n' || (pos == len && eom)) {
        pyzor_digest_part_end (digest);
        //off = 0;
        phase = pyzor_phase_none;
        if (pos == len)
          break;
      }
    } else if (g_ascii_isalpha (str[pos])) {
      if (phase == pyzor_phase_none
       || phase == pyzor_phase_space)
      {
        off = pos;
        phase = pyzor_phase_alpha;
      } else {
//if (pos - off >= 9)
//  fprintf (stderr, "pos - off: %d\n", pos - off);
        if ((pos - off) + 1 >= PYZOR_STRING_MIN) {
          phase = pyzor_phase_discard;
        }
      }
    } else if (phase != pyzor_phase_discard) {
      if (str[pos] == '<') {
        if (lt < 0)
          lt = pos;
      } else if (str[pos] == '>') {
        if (gt < 0)
          gt = pos;
      }

      if (phase == pyzor_phase_none || phase == pyzor_phase_space) {
        off = pos;
        phase = pyzor_phase_non_space;
      } else if ((pos - off) + 1 >= PYZOR_STRING_MIN || phase == pyzor_phase_delim) {
        phase = pyzor_phase_discard;
      } else if ((str[pos] == ':' && (phase == pyzor_phase_alpha))
              || (str[pos] == '@' && (phase == pyzor_phase_alpha || phase == pyzor_phase_non_space)))
      {
        phase = pyzor_phase_delim;
// THIS IS NOT CORRECT... FIX IT... BUT FIRST FIX THE BUFFER OVERFLOW
//      } else if (str[pos] == '<') {
//        if (lt < 0)
//          lt = pos;
//        phase = pyzor_phase_non_space;
//      } else if (str[pos] == '>') {
//        if (gt < 0 && lt >= 0)
//          gt = pos;
//        phase = pyzor_phase_non_space;
      } else {
        phase = pyzor_phase_non_space;
      }
    }
  }

  digest->phase = phase;

  return (0);
}

int
pyzor_digest_finish (unsigned char *str, size_t len, pyzor_digest_t *digest)
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







































































