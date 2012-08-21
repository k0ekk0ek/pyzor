/* */
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <glib.h>
#include <gmime/gmime.h>

#include "pyzor.h"


static GMimeMessage *
parse_message (int fd)
{
	GMimeMessage *message;
	GMimeParser *parser;
	GMimeStream *stream;
	
	/* create a stream to read from the file descriptor */
	stream = g_mime_stream_fs_new (fd);
	
	/* create a new parser object to parse the stream */
	parser = g_mime_parser_new_with_stream (stream);

	/* unref the stream (parser owns a ref, so this object does not actually get free'd until we destroy the parser) */
	g_object_unref (stream);
	
	/* parse the message from the stream */
	message = g_mime_parser_construct_message (parser);
	
	/* free the parser (and the stream) */
	g_object_unref (parser);
	
	return message;
}

// look at rfc822BodyCleaner in client.py line 698
// it does some decoding... check if we should do so as well or if thats done
// by gmime automatically

#define BUFLEN 4096

static void
pyzor_foreach_callback (GMimeObject *parent, GMimeObject *part, gpointer user_data)
{
  pyzor_digest_t *digest = user_data;

  GMimeFilter *filter;
  GMimeStream *stream, *filtered_stream;
  //GMimeDataWrapper *wrapper;

  ssize_t cnt;
  char buf[BUFLEN];
  memset (buf, '\0', BUFLEN);

// it can automatically decode stuff... there's an example for that look it
// up.
// the easiest thing to do is look at write_part in imap-example.c which comes
// with the gmime distribution
// the content member of GMimePart is a GMimeDataWrapper
// does g_mime_data_wrapper_get_stream do automatic decoding?!?! thats what i
// want to know!

  if (GMIME_IS_MULTIPART (part)) {
    //multipart = (GMimeMultipart *) part;
    // invoke foreach stuff
    //} else if (GMIME_IS_MESSAGE_PARTIAL

  } else if (GMIME_IS_PART (part)) {
    // right this is what we want...
    // now just call pyzor_digest_update (...)
    //g_mime_data_wrapper_get_stream (GMIME_PART(part)->content);
    stream = g_mime_data_wrapper_get_stream (GMIME_PART (part)->content);
    // returns stream member of wrapper...
    // we can then invoke g_mime_data_wrapper_get_encoding
    // GMimeContentEncoding  g_mime_data_wrapper_get_encoding  (GMimeDataWrapper *wrapper);
    // to get the content encoding...
    // then we need to create a filter... because the stream doesn't do decoding
    // by default... see write_to_stream() inn gmime/gmime-data-wrapper.c
    filter = g_mime_filter_basic_new (g_mime_data_wrapper_get_encoding (GMIME_PART (part)->content), FALSE);
		filtered_stream = g_mime_stream_filter_new (stream);
		g_mime_stream_filter_add (GMIME_STREAM_FILTER (filtered_stream), filter);
		g_object_unref (filter);
		//g_mime_stream_write_to_stream (istream, ostream); // forget this
		// we now have a filtered stream... which can be used to read data if i'm
		// not mistaken
		// yep... just use use g_mime_stream_read
		//g_object_unref (ostream);
		// as far as i can see no attempt is made by pyzor to do character set
		// conversion
		/* now read... */
		for (;;) {
		  cnt = g_mime_stream_read (filtered_stream, buf, BUFLEN);
		  if (cnt == -1) {
		    fprintf (stderr, "error\n");
		    return;
		  } else if (cnt == 0) {
		    printf ("stream empty\n");
		    break;
		  } else {
//fprintf (stderr, "MIME: %.*s\n", cnt, buf);
		    int err = pyzor_digest_update (digest, buf, (size_t)cnt, (cnt < BUFLEN ? 1 : 0));
		    if (err != 0) {
		      fprintf (stderr, "error: %s\n", strerror (err));
		      return;
		    }
		    //printf ("%.*s", cnt, buf);
		    //if (cnt < BUFLEN)
		    //  break;
		  }
		}
  }

  return;
}

int
main (int argc, char *argv[])
{
	GMimeMessage *message;
	int fd;
	
	if (argc < 2) {
		printf ("Usage: a.out <message file>\n");
		return 0;
	} else {
		if ((fd = open (argv[1], O_RDONLY, 0)) == -1) {
			fprintf (stderr, "Cannot open message `%s': %s\n", argv[1], g_strerror (errno));
			return 0;
		}
	}
	
	/* init the gmime library */
	g_mime_init (0);
	
	/* parse the message */
	message = parse_message (fd);

  pyzor_digest_t *digest;
  int err;
//  pyzor_digest_init (&digest);
  if ((err = pyzor_digest_create (&digest)) != 0) {
    fprintf (stderr, "error: %s\n", strerror (err));
    return (1);
  }

  g_mime_message_foreach (message, pyzor_foreach_callback, (void *)digest);

  char buf[1024];
  pyzor_digest_final (buf, 1024, digest);

  printf ("digest: %s\n", buf);

  return (0);
}

