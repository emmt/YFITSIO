/*
 * yfitsio.c --
 *
 * Implements Yorick interface to CFITSIO library.
 *
 *-----------------------------------------------------------------------------
 *
 * Copyright (C) 2015-2018 Éric Thiébaut (https://github.com/emmt/YFITSIO).
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see http://www.gnu.org/licenses/.
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <float.h>
#include <limits.h>

#include <fitsio2.h>

#include <pstdlib.h>
#include <play.h>
#include <yapi.h>

/* Define some macros to get rid of some GNU extensions when not compiling
   with GCC. */
#if ! (defined(__GNUC__) && __GNUC__ > 1)
#   define __attribute__(x)
#   define __inline__
#   define __FUNCTION__        ""
#   define __PRETTY_FUNCTION__ ""
#endif

#define TRUE  1
#define FALSE 0

#define MAXDIMS 99

PLUG_API void y_error(const char *) __attribute__ ((noreturn));

/* A type to store any scalar value. */
typedef struct {
  union {
    char c;
    short s;
    int i;
    long l;
    float f;
    double d;
    char* q;
  } value;
  int type;
} scalar_t;

static void push_string(const char* str);
static void push_complex(double re, double im);
static void push_scalar(const scalar_t* s);

static void define_string(long index, const char* str);


static char* fetch_path(int iarg);
static int   fetch_int(int iarg);
#define      fetch_fitsfile(iarg, flags) yfits_fetch((iarg), (flags))->fptr

static void critical(int clear_errmsg);

/* space: ' ' (32)
   white: '\t' (9), '\n' (10), '\v' (11), '\f' (12), '\r' (13)
*/
#define IS_WHITE(c) ('\t' <= (c) && (c) <= '\r')
#define IS_SPACE(c) ((c) == ' ' || IS_WHITE(c))

/* Trim leading and trailing spaces and return length.   SRC and DST
   can be the same.   NULL forbidden. */
static int trim_string(char* dst, const char* src);

/* Retrieve dimension list from arguments of the stack.  IARG_FIRST is the
   first stack element to consider, IARG_LAST is the last one (inclusive and
   such that IARG_FIRST >= IARG_LAST).  MAXDIMS is the maximum number of
   dimensions, DIMS is an array of at least MAXDIMS+1 elements. */
static void get_dimlist(int iarg_first, int iarg_last,
                        long* dims, int maxdims);

/* Define a Yorick global symbol with an int value. */
static void define_int_const(const char* name, int value);

static int yfits_debug = FALSE;
static void yfits_error(int status);

/* Operations implementing the behavior of a FITS object. */
static void yfits_free(void* ptr);
static void yfits_print(void* ptr);
static void yfits_eval(void* ptr, int argc);
static void yfits_extract(void* ptr, char* name);

/* FITS instance functions. */
typedef struct _yfits_object yfits_object;
static yfits_object* yfits_push(void);
static yfits_object* yfits_fetch(int iarg, unsigned int flags);
#define MAY_BE_CLOSED  0
#define NOT_CLOSED 1
#define CRITICAL       2

static const char* hdu_type_name(int type);

/* Get image parameters.  Similar to fits_get_img_param but return also
   number of axes and computes number of elements. */
static int
get_image_param(fitsfile* fptr, int maxdims, int* bitpix, int* naxis,
                long dims[], long* number, int* status);

/* Fast indexes to common keywords. */
static long index_of_ascii = -1L;
static long index_of_basic = -1L;
static long index_of_case = -1L;
static long index_of_extname = -1L;
static long index_of_first = -1L;
static long index_of_incr = -1L;
static long index_of_last = -1L;
static long index_of_null = -1L;
static long index_of_number = -1L;
static long index_of_tunit = -1L;
static long index_of_def = -1L;

/* A static buffer for error messages, file names, etc. */
static char buffer[32*1024];

static void
yfits_error(int status)
{
  if (yfits_debug) {
    fits_report_error(stderr, status);
  } else {
    fits_clear_errmsg();
  }
  fits_get_errstatus(status, buffer);
  y_error(buffer);
}

/* FITS handle instance. */
struct _yfits_object {
  fitsfile *fptr;
};

/* FITS handle type. */
static struct y_userobj_t yfits_type = {
  "FITS handle", yfits_free, yfits_print, yfits_eval, yfits_extract, NULL
};

static void
yfits_free(void* ptr)
{
  yfits_object* obj = (yfits_object*)ptr;
  fitsfile *fptr = obj->fptr;
  if (fptr != NULL) {
    /* Close the FITS file.  In case of failure, just print the error messages
       (do not throw an error). */
    int status = 0;
    obj->fptr = NULL;
    if (fits_close_file(fptr, &status) != 0) {
      fits_report_error(stderr, status);
    }
  }
}

static void
yfits_print(void* ptr)
{
  yfits_object* obj = (yfits_object*)ptr;
  fitsfile *fptr = obj->fptr;
  int number, status = 0;

  critical(TRUE);
  if (fptr != NULL) {
    fits_get_num_hdus(fptr, &number, &status);
  } else {
    number = 0;
  }
  sprintf(buffer, "%s with %d HDU", yfits_type.type_name, number);
  y_print(buffer, TRUE);
  if (number >= 1) {
    int hdu0, hdu, type;
    fits_get_hdu_num(fptr, &hdu0);
    for (hdu = 1; hdu <= number; ++hdu) {
      if (fits_movabs_hdu(fptr, hdu, &type, &status) != 0) {
        fits_report_error(stderr, status);
        status = 0;
        break;
      }
      sprintf(buffer, "  HDU[%d] = %s", hdu, hdu_type_name(type));
      y_print(buffer, TRUE);
    }
    if (fits_movabs_hdu(fptr, hdu0, &type, &status) != 0) {
      fits_report_error(stderr, status);
      status = 0;
    }
  }
}

static void
yfits_eval(void* ptr, int argc)
{
  ypush_nil();
}

static void
yfits_extract(void* ptr, char* name)
{
  ypush_nil();
}

/* Open an existing data file. */
static void
open_file(int argc, int which)
{
  yfits_object* obj;
  char* path = NULL;
  char* mode = NULL;
  int basic = FALSE; /* use basic file name syntax? */
  int status = 0;
  int iomode = 0;
  int iarg;

  /* First parse template keyword. */
  for (iarg = argc - 1; iarg >= 0; --iarg) {
    long index = yarg_key(iarg);
    if (index < 0) {
      /* Positional argument. */
      if (path == NULL) {
        path = fetch_path(iarg);
      } else if (mode == NULL) {
        mode = ygets_q(iarg);
      } else {
        y_error("too many arguments");
      }
    } else {
      /* Keyword argument. */
      --iarg;
      if (which == 0 && index == index_of_basic) {
        basic = yarg_true(iarg);
      } else {
        y_error("unsupported keyword");
      }
    }
  }
  if (path == NULL) y_error("too few arguments");
  if (mode == NULL || strcmp(mode, "r") == 0) {
    iomode = READONLY;
  } else if (strcmp(mode, "rw") == 0) {
    iomode = READWRITE;
  } else {
    y_error("invalid mode");
  }

  obj = yfits_push();
  critical(TRUE);
  if (which == 1) {
    fits_open_data(&obj->fptr, path, iomode, &status);
  } else if (which == 2) {
    fits_open_table(&obj->fptr, path, iomode, &status);
  } else if (which == 3) {
    fits_open_image(&obj->fptr, path, iomode, &status);
  } else if (basic) {
    fits_open_diskfile(&obj->fptr, path, iomode, &status);
  } else {
    fits_open_file(&obj->fptr, path, iomode, &status);
  }

  if (status) yfits_error(status);
}

void
Y_fitsio_open_file(int argc)
{
  open_file(argc, 0);
}

void
Y_fitsio_open_data(int argc)
{
  open_file(argc, 1);
}

void
Y_fitsio_open_table(int argc)
{
  open_file(argc, 2);
}

void
Y_fitsio_open_image(int argc)
{
  open_file(argc, 3);
}

/* Create and open a new empty output FITS file. */
void
Y_fitsio_create_file(int argc)
{
  yfits_object* obj;
  char* path = NULL;
  int basic = FALSE; /* use basic file name syntax? */
  int status = 0;
  int iarg;

  /* First parse template keyword. */
  for (iarg = argc - 1; iarg >= 0; --iarg) {
    long index = yarg_key(iarg);
    if (index < 0) {
      /* Positional argument. */
      if (path == NULL) {
        path = fetch_path(iarg);
      } else {
        y_error("too many arguments");
      }
    } else {
      /* Keyword argument. */
      --iarg;
      if (index == index_of_basic) {
        basic = yarg_true(iarg);
      } else {
        y_error("unsupported keyword");
      }
    }
  }
  if (path == NULL) y_error("too few arguments");

  obj = yfits_push();
  critical(TRUE);
  if (basic) {
    fits_create_diskfile(&obj->fptr, path, &status);
  } else {
    fits_create_file(&obj->fptr, path, &status);
  }
  if (status != 0) yfits_error(status);
}

/* Close a FITS file. */
void
Y_fitsio_close_file(int argc)
{
  yfits_object* obj;
  fitsfile *fptr;

  if (argc != 1) y_error("expecting exactly one argument");
  obj = yfits_fetch(0, MAY_BE_CLOSED|CRITICAL);
  fptr = obj->fptr;
  if (fptr != NULL) {
    int status = 0;
    obj->fptr = NULL;
    if (fits_close_file(fptr, &status) != 0) {
      yfits_error(status);
    }
  }
}

/* Delete a FITS file. */
void
Y_fitsio_delete_file(int argc)
{
  yfits_object* obj;
  fitsfile *fptr;

  if (argc != 1) y_error("expecting exactly one argument");
  obj = yfits_fetch(0, NOT_CLOSED|CRITICAL);
  fptr = obj->fptr;
  if (fptr != NULL) {
    int status = 0;
    obj->fptr = NULL;
    if (fits_delete_file(fptr, &status) != 0) {
      yfits_error(status);
    }
  }
}

void
Y_fitsio_is_open(int argc)
{
  if (argc != 1) y_error("expecting exactly one argument");
  ypush_int(fetch_fitsfile(0, MAY_BE_CLOSED) != NULL ? TRUE : FALSE);
}

void
Y_fitsio_is_handle(int argc)
{
  if (argc != 1) y_error("expecting exactly one argument");
  ypush_int((char*)yget_obj(0, NULL) == yfits_type.type_name ? TRUE : FALSE);
}

void
Y_fitsio_file_name(int argc)
{
  fitsfile* fptr;
  char* name;
  int status = 0;

  if (argc != 1) y_error("expecting exactly one argument");
  fptr = fetch_fitsfile(0, MAY_BE_CLOSED|CRITICAL);
  if (fptr == NULL) {
    name = NULL;
  } else {
    name = buffer;
    if (fits_file_name(fptr, name, &status) != 0) {
      yfits_error(status);
    }
  }
  push_string(name);
}

void
Y_fitsio_file_mode(int argc)
{
  fitsfile* fptr;
  const char* mode = NULL;
  if (argc != 1) y_error("expecting exactly one argument");
  fptr = fetch_fitsfile(0, MAY_BE_CLOSED|CRITICAL);
  if (fptr != NULL) {
    int iomode, status = 0;
    if (fits_file_mode(fptr, &iomode, &status) != 0) {
      yfits_error(status);
    }
    if (iomode == READONLY) {
      mode = "r";
    } else if (iomode == READWRITE) {
      mode = "rw";
    }
  }
  push_string(mode);
}

void
Y_fitsio_url_type(int argc)
{
  fitsfile* fptr;
  char* url;
  int status = 0;

  if (argc != 1) y_error("expecting exactly one argument");
  fptr = fetch_fitsfile(0, MAY_BE_CLOSED|CRITICAL);
  if (fptr == NULL) {
    url = NULL;
  } else {
    url = buffer;
    if (fits_url_type(fptr, url, &status) != 0) {
      yfits_error(status);
    }
  }
  push_string(url);
}

void
Y_fitsio_movabs_hdu(int argc)
{
  fitsfile* fptr;
  int number, type;
  int status = 0;

  if (argc != 2) y_error("expecting exactly two arguments");
  fptr = fetch_fitsfile(1, NOT_CLOSED|CRITICAL);
  number = fetch_int(0);
  if (number <= 0) y_error("invalid HDU number");
  if (fits_movabs_hdu(fptr, number, &type, &status) != 0) {
    if (status != BAD_HDU_NUM || yarg_subroutine()) {
      yfits_error(status);
    }
    type = -1;
  }
  ypush_int(type);
}

void
Y_fitsio_movrel_hdu(int argc)
{
  fitsfile* fptr;
  int offset, type;
  int status = 0;

  if (argc != 2) y_error("expecting exactly two arguments");
  fptr = fetch_fitsfile(1, NOT_CLOSED|CRITICAL);
  offset = fetch_int(0);
  if (fits_movrel_hdu(fptr, offset, &type, &status) != 0) {
    if (status != BAD_HDU_NUM || yarg_subroutine()) {
      yfits_error(status);
    }
    type = -1;
  }
  ypush_int(type);
}

void
Y_fitsio_movnam_hdu(int argc)
{
  fitsfile* fptr;
  char* extname;
  int type, extver;
  int status = 0;

  if (argc < 3 || argc > 4) y_error("expecting 3 or 4 arguments");
  fptr = fetch_fitsfile(argc - 1, NOT_CLOSED|CRITICAL);
  type = fetch_int(argc - 2);
  extname = ygets_q(argc - 3);
  extver = (argc >= 4 ? fetch_int(argc - 4) : 0);
  if (type != IMAGE_HDU && type != BINARY_TBL &&
      type != ASCII_TBL && type != ANY_HDU) {
    y_error("bad HDUTYPE");
  }
  if (fits_movnam_hdu(fptr, type, extname, extver, &status) != 0) {
    if (status != BAD_HDU_NUM || yarg_subroutine()) {
      yfits_error(status);
    }
    type = -1;
  } else if (fits_get_hdu_type(fptr, &type, &status) != 0) {
    yfits_error(status);
  }
  ypush_int(type);
}

void
Y_fitsio_get_num_hdus(int argc)
{
  fitsfile* fptr;
  int number;
  int status = 0;

  if (argc != 1) y_error("expecting exactly one argument");
  fptr = fetch_fitsfile(0, MAY_BE_CLOSED|CRITICAL);
  if (fptr == NULL) {
    number = 0;
  } else if (fits_get_num_hdus(fptr, &number, &status) != 0) {
    yfits_error(status);
  }
  ypush_long(number); /* Yorick number of elements is a long */
}

void
Y_fitsio_get_hdu_num(int argc)
{
  fitsfile* fptr;
  int number;

  if (argc != 1) y_error("expecting exactly one argument");
  fptr = fetch_fitsfile(0, MAY_BE_CLOSED|CRITICAL);
  if (fptr == NULL) {
    number = 0;
  } else {
    fits_get_hdu_num(fptr, &number);
  }
  ypush_long(number); /* Yorick number of elements is a long */
}

void
Y_fitsio_get_hdu_type(int argc)
{
  int type;
  int status = 0;
  if (argc != 1) y_error("expecting exactly one argument");
  fits_get_hdu_type(fetch_fitsfile(0, NOT_CLOSED|CRITICAL), &type, &status);
  if (status != 0) yfits_error(status);
  ypush_int(type);
}

void
Y_fitsio_copy_file(int argc)
{
  fitsfile* inp;
  fitsfile* out;
  int previous, current, following;
  int status = 0;
  if (argc != 5) y_error("expecting exactly 5 arguments");
  inp = fetch_fitsfile(argc - 1, NOT_CLOSED|CRITICAL);
  out = fetch_fitsfile(argc - 2, NOT_CLOSED);
  previous = yarg_true(argc - 3);
  current = yarg_true(argc - 4);
  following = yarg_true(argc - 5);
  fits_copy_file(inp, out, previous, current, following, &status);
  if (status != 0) yfits_error(status);
  yarg_drop(3); /* left output object on top of stack */
}

void
Y_fitsio_copy_hdu(int argc)
{
  fitsfile* inp;
  fitsfile* out;
  int morekeys, status = 0;
  if (argc < 2 || argc > 3) y_error("expecting 2 or 3 arguments");
  inp = fetch_fitsfile(argc - 1, NOT_CLOSED|CRITICAL);
  out = fetch_fitsfile(argc - 2, NOT_CLOSED);
  morekeys = (argc >= 3 ? fetch_int(argc - 3) : 0);
  fits_copy_hdu(inp, out, morekeys, &status);
  if (status != 0) yfits_error(status);
  if (argc > 2) yarg_drop(argc - 2); /* left output object on top of stack */
}

/* missing: fits_write_hdu */

void
Y_fitsio_copy_header(int argc)
{
  int status = 0;
  if (argc != 2) y_error("expecting exactly 2 arguments");
  fits_copy_header(fetch_fitsfile(argc - 1, NOT_CLOSED|CRITICAL),
                   fetch_fitsfile(argc - 2, NOT_CLOSED), &status);
  if (status != 0) yfits_error(status);
}

void
Y_fitsio_delete_hdu(int argc)
{
  int type, status = 0;
  if (argc != 1) y_error("expecting exactly one argument");
  fits_delete_hdu(fetch_fitsfile(0, NOT_CLOSED|CRITICAL), &type, &status);
  if (status != 0) yfits_error(status);
  ypush_int(type);
}

/*---------------------------------------------------------------------------*/
/* HEADER KEYWORDS */

void
Y_fitsio_get_num_keys(int argc)
{
  int numkeys, status = 0;
  if (argc != 1) y_error("expecting exactly one argument");
  fits_get_hdrspace(fetch_fitsfile(0, NOT_CLOSED|CRITICAL),
                    &numkeys, NULL, &status);
  if (status != 0) yfits_error(status);
  ypush_long(numkeys);
}

static int
get_key(int iarg, int* keynum, char** keystr)
{
  if (yarg_rank(iarg) == 0) {
    int type = yarg_typeid(iarg);
    if (type == Y_STRING) {
      *keynum = 0;
      *keystr = ygets_q(iarg);
      return Y_STRING;
    } else if (type <= Y_LONG) {
      long lval = ygets_l(iarg);
      int ival = (int)lval;
      if (ival < 0 || ival != lval) y_error("invalid keyword number");
      *keynum = ival;
      *keystr = NULL;
      return Y_INT;
    }
  }
  y_error("expecting a card number or a keyword name");
  return -1;
}

void
Y_fitsio_read_card(int argc)
{
  fitsfile* fptr;
  char* keystr;
  int status = 0, keynum, keytype;
  if (argc != 2) y_error("expecting exactly 2 arguments");
  fptr = fetch_fitsfile(argc - 1, NOT_CLOSED|CRITICAL);
  keytype = get_key(argc - 2, &keynum, &keystr);
  if (keytype == Y_STRING) {
    if (keystr == NULL || keystr[0] == '\0') {
      status = KEY_NO_EXIST;
    } else {
      fits_read_card(fptr, keystr, buffer, &status);
    }
  } else {
    fits_read_record(fptr, keynum, buffer, &status);
    if (keynum == 0 && status == 0) {
      status = KEY_NO_EXIST;
    }
  }
  if (status == 0) {
    push_string(buffer);
  } else if (status == KEY_NO_EXIST) {
    ypush_nil();
  } else {
    yfits_error(status);
  }
}

void
Y_fitsio_split_card(int argc)
{
  long dims[] = {1, 3};
  char** out;
  char* card;
  char keyword[FLEN_KEYWORD];
  char value[FLEN_VALUE];
  char comment[FLEN_COMMENT];
  int len, status = 0;
  if (argc != 1) y_error("expecting exactly 1 argument");
  card = ygets_q(0);
  if (card == NULL || card[0] == '\0') {
    out = ypush_q(dims);
  } else {
    status = 0;
    /* FIXME: fits_test_record(card, &status); */
    fits_get_keyname(card, keyword, &len, &status);
    /* FIXME: fits_test_keyword(keyword, &status); */
    fits_parse_value(card, value, comment, &status);
    if (status != 0) yfits_error(status);
    out = ypush_q(dims);
    out[0] = p_strcpy(keyword);
    out[1] = (value[0] == '\0' ? NULL : p_strcpy(value));
    out[2] = p_strcpy(comment);
  }
}

static void
push_key_value(const char* value, char* buffer)
{
  double re, im, dval;
  long lval;
  char* end;
  char dummy;
  int i, c, len, real, status;

  if (value == NULL) {
    push_string(NULL);
    return;
  }

  /* Trim leading and trailing spaces. */
  len = trim_string(buffer, value);

  /* Guess value type (see fits_get_keytype/ffdtyp in fitscore.c). */
  switch (buffer[0]) {
  case '\0':
    push_string(NULL);
    return;

  case 'T':
  case 't':
    if (len != 1) break;
    ypush_int(TRUE);
    return;

  case 'F':
  case 'f':
    if (len != 1) break;
    ypush_int(FALSE);
    return;

  case '\'':
    /* String value. */
    if (len < 2 || buffer[len-1] != '\'') break;
    status = 0;
    ffc2s(buffer, buffer, &status);
    if (status != 0) yfits_error(status);
    /* FIXME: do we need to trim trailing spaces? */
    push_string(buffer);
    return;

  case '(':
    /* Complex value. */
    if (sscanf(buffer + 1, "%lf ,%lf )%1c", &re, &im, &dummy) == 2) {
      push_complex(re, im);
      return;
    }
    break;

  default:
    real = FALSE;
    for (i = 0; i < len; ++i) {
      c = buffer[i];
      if (c == '.' || c == 'E' || c == 'e') {
        real = TRUE;
      } else if (c == 'D' || c == 'd') {
        buffer[i] = 'E';
        real = TRUE;
      }
    }
    if (real) {
      /* Try to read a single real. */
      dval = strtod(buffer, &end);
      if (*end == '\0') {
        ypush_double(dval);
        return;
      }
    } else {
      /* Try to read a single integer. */
      lval = strtol(buffer, &end, 10);
      if (*end == '\0') {
        ypush_long(lval);
        return;
      }
    }
  }
  y_error("invalid keyword value");
}

/* Extract the units from the comment.
   The units part is comment[i:j] (if 1 <= i <= j);
   in any case, the comment part is comment[k:].
 */
static void
parse_unit(const char* comment, int* iptr, int* jptr, int* kptr)
{
  int c, i = -1, j = -2, k = 0;
  do { c = comment[++i]; } while (IS_SPACE(c));
  if (c == '[') {
    do { c = comment[++i]; } while (IS_SPACE(c));
    j = i;
    for (;;) {
      if (c == '\0') {
        j = -2;
        break;
      }
      if (c == ']') {
        k = j;
        do { c = comment[--j]; } while (IS_SPACE(c));
        do { c = comment[++k]; } while (IS_SPACE(c));
        break;
      }
      c = comment[++j];
    }
  }
  *iptr = i;
  *jptr = j;
  *kptr = k;
}

void
Y_fitsio_read_key(int argc)
{
  fitsfile* fptr;
  char* keystr;
  char card[FLEN_CARD];
  char keyword[FLEN_KEYWORD];
  char value[FLEN_VALUE];
  char comment[FLEN_COMMENT];
  long comm_index, unit_index;
  int iarg, def_iarg, pos;
  int len, status = 0, keynum, keytype;

  /* Parse arguments. */
  comm_index = -1;
  unit_index = -1;
  def_iarg = -1;
  fptr = NULL;
  pos = 0;
  keytype = -1;
  keystr = NULL;
  keynum = -1;
  for (iarg = argc - 1; iarg >= 0; --iarg) {
    long index = yarg_key(iarg);
    if (index < 0) {
      /* Positional argument. */
      ++pos;
      if (pos == 1) {
        fptr = fetch_fitsfile(iarg, NOT_CLOSED|CRITICAL);
      } else if (pos == 2) {
        keytype = get_key(iarg, &keynum, &keystr);
      } else if (pos == 3) {
        comm_index = yget_ref(iarg);
        if (comm_index < 0 && ! yarg_nil(iarg)) {
          y_error("3rd argument must be a simple variable");
        }
      } else if (pos == 4) {
        unit_index = comm_index;
        comm_index = yget_ref(iarg);
        if (unit_index < 0 && ! yarg_nil(iarg)) {
          y_error("4th argument must be a simple variable");
        }
      } else {
        y_error("too many arguments");
      }
    } else {
      /* Keyword argument. */
      --iarg;
      if (index == index_of_def) {
        def_iarg = iarg;
      } else {
        y_error("unsupported keyword");
      }
    }
  }
  if (pos < 2) {
    y_error("too few arguments");
  }

  if (keytype == Y_STRING) {
    if (keystr == NULL || keystr[0] == '\0' /* FIXME: empty key possible? */) {
      status = KEY_NO_EXIST;
    } else {
      fits_read_card(fptr, keystr, card, &status);
      fits_get_keyname(card, keyword, &len, &status);
      fits_parse_value(card, value, comment, &status);
    }
  } else {
    if (keynum < 1) y_error("invalid card number");
    fits_read_keyn(fptr, keynum, keyword, value, comment, &status);
  }
  if (value[0] == '\0' && status == 0) {
    status = VALUE_UNDEFINED;
  }
  if (status == 0) {
    int i, j, k = 0;
    if (unit_index != -1) {
      parse_unit(comment, &i, &j, &k);
      if (i >= 1 && j >= i) {
        comment[j+1] = '\0';
        define_string(unit_index, &comment[i]);
      } else {
        define_string(unit_index, NULL);
      }
    }
    if (comm_index != -1) {
      define_string(comm_index, &comment[k]);
    }
    push_key_value(value, value);
  } else if (status == VALUE_UNDEFINED) {
    if (unit_index != -1) {
      define_string(unit_index, NULL);
    }
    if (comm_index != -1) {
      define_string(comm_index, comment);
    }
    push_string(NULL);
  } else if (status == KEY_NO_EXIST) {
    if (unit_index != -1) {
      define_string(unit_index, NULL);
    }
    if (comm_index != -1) {
      define_string(comm_index, NULL);
    }
    if (def_iarg > 0) {
      yarg_drop(def_iarg);
    } else if (def_iarg < 0) {
      ypush_nil();
    }
  } else {
    yfits_error(status);
  }
}

static char**
fetch_card(int iarg, int* n)
{
  if (yarg_typeid(iarg) == Y_STRING) {
    long dims[Y_DIMSIZE], ntot;
    char** card = ygeta_q(iarg, &ntot, dims);
    int rank = dims[0];
    if (rank == 0) {
      if (card[0] != NULL && strlen(card[0]) > 80) {
        y_error("FITS cards have at most 80 characters");
      }
      *n = 1;
      return card;
    }
    if (rank == 1 && ntot == 3) {
      if (card[0] != NULL && strlen(card[0]) > 71) {
        y_error("FITS keywords have at most 71 characters");
      }
      if (card[1] != NULL && strlen(card[1]) > 70) {
        y_error("FITS card values have at most 70 characters");
      }
      if (card[2] != NULL && strlen(card[2]) > 72) {
        y_error("FITS card comments have at most 72 characters");
      }
      *n = 3;
      return card;
    }
  }
  y_error("expecting a FITS card argument");
  return NULL;
}

void
Y_fitsio_get_keyword(int argc)
{
  char keyword[FLEN_KEYWORD];
  char** card;
  int status, n, len;

  if (argc != 1) y_error("expecting exactly 1 argument");
  card = fetch_card(0, &n);
  if (n == 1) {
    if (card[0] == NULL) {
      push_string(NULL);
    } else {
      status = 0;
      fits_get_keyname(card[0], keyword, &len, &status);
      if (status != 0) yfits_error(status);
      push_string(keyword);
    }
  } else {
    push_string(card[0]);
  }
}

void
Y_fitsio_get_value(int argc)
{
  char value[FLEN_VALUE];
  char comment[FLEN_COMMENT];
  char** card;
  int status, n;

  if (argc != 1) y_error("expecting exactly 1 argument");
  card = fetch_card(0, &n);
  if (n == 1) {
    if (card[0] == NULL) {
      push_string(NULL);
    } else {
      status = 0;
      fits_parse_value(card[0], value, comment, &status);
      if (status != 0) yfits_error(status);
      push_key_value(value, value);
    }
  } else {
    push_key_value(card[1], value);
  }
}

void
Y_fitsio_get_comment(int argc)
{
  char comment[FLEN_COMMENT];
  char value[FLEN_VALUE];
  char** card;
  long unit_index = -1;
  int status, n, i, j, k, iarg;

  if (argc != 1 && argc != 2) y_error("expecting 1 or 2 arguments");
  card = fetch_card(argc - 1, &n);
  if (argc < 2) {
    unit_index = -1;
  } else {
    iarg = argc - 2;
    unit_index = yget_ref(iarg);
    if (unit_index < 0 && ! yarg_nil(iarg)) {
      y_error("optional argument must be a simple variable");
    }
  }

  if (n == 1) {
    if (card[0] == NULL) goto undefined;
    status = 0;
    fits_parse_value(card[0], value, comment, &status);
    if (status != 0) yfits_error(status);
  } else {
    if (card[2] == NULL)  goto undefined;
    strncpy(comment, card[2], FLEN_COMMENT);
    comment[FLEN_COMMENT-1] = '\0';
  }

  if (unit_index < 0) {
    k = 0;
  } else {
    parse_unit(comment, &i, &j, &k);
    if (i >= 1 && j >= i) {
      comment[j+1] = '\0';
      define_string(unit_index, &comment[i]);
    } else {
      define_string(unit_index, NULL);
    }
  }
  push_string(&comment[k]);
  return;

 undefined:
  if (unit_index >= 0) {
    define_string(unit_index, NULL);
  }
  push_string(NULL);
}

static void
write_key(int argc, int update)
{
  fitsfile* fptr;
  long lval; /* integer */
  int ival; /* logical */
  double dval; /* real */
  void* valptr;
  char *key;
  char *comment;
  int iarg, status, valtype, type, valok;

  if (argc != 3 && argc != 4) y_error("expecting 3 or 4 arguments");
  fptr = fetch_fitsfile(argc - 1, NOT_CLOSED|CRITICAL);

  /* Get the key name. */
  iarg = argc - 2;
  if (yarg_typeid(iarg) != Y_STRING || yarg_rank(iarg) != 0) {
    y_error("illegal keyword name");
  }
  key = ygets_q(iarg);
  if (key == NULL) key = "";

  /* Get the value. */
  iarg = argc - 3;
  valtype = 0;
  valptr = NULL;
  valok = TRUE;
  type = yarg_typeid(iarg);
  if (type != Y_VOID) {
    if (yarg_rank(iarg) != 0) {
      valok = FALSE;
    } else if (type <= Y_LONG) {
      lval = ygets_l(iarg);
      if (type == Y_CHAR) {
        if (lval == 'T' || lval == 't') {
          ival = 'T';
        } else if (lval == 'F' || lval == 'f') {
          ival = 'F';
        } else {
          y_error("logical value must be 'T' or 'F'");
        }
        valtype = TLOGICAL;
        valptr = &ival;
      } else {
        valtype = TLONG;
        valptr = &lval;
      }
    } else if (type == Y_FLOAT || type == Y_DOUBLE) {
      dval = ygets_d(iarg);
      valtype = TDOUBLE;
      valptr = &dval;
    } else if (type == Y_COMPLEX) {
      valtype = TDBLCOMPLEX;
      valptr = ygeta_z(iarg, NULL, NULL);
    } else if (type == Y_STRING) {
      valtype = TSTRING;
      valptr = ygets_q(iarg);
      if (valptr == NULL) valptr = "";

    } else {
      valok = FALSE;
    }
  }
  if (! valok) {
    y_error("illegal keyword value");
  }

  /* Get the comment argument. */
  comment = NULL;
  if (argc > 4) {
    iarg = argc - 4;
    type = yarg_typeid(iarg);
    if (type == Y_STRING && yarg_rank(iarg) == 0) {
      comment = ygets_q(iarg);
      if (comment == NULL) comment = "";
    } else if (type != Y_VOID) {
      y_error("illegal comment");
    }
  }

  /* Write/update the card. */
  status = 0;
  if (update) {
    if (valptr != NULL) {
      fits_update_key(fptr, valtype, key, valptr, comment, &status);
    } else {
      fits_update_key_null(fptr, key, comment, &status);
    }
  } else {
    if (valptr != NULL) {
      fits_write_key(fptr, valtype, key, valptr, comment, &status);
    } else {
      fits_write_key_null(fptr, key, comment, &status);
    }
  }
  if (status != 0) {
    yfits_error(status);
  }
  ypush_nil();
}

void
Y_fitsio_write_key(int argc)
{
  write_key(argc, 0);
}

void
Y_fitsio_update_key(int argc)
{
  write_key(argc, 1);
}

void
Y_fitsio_write_comment(int argc)
{
  fitsfile* fptr;
  char *comment;
  int status = 0;
  if (argc != 1 && argc != 2) y_error("expecting 1 or 2 arguments");
  fptr = fetch_fitsfile(argc - 1, NOT_CLOSED|CRITICAL);
  comment = (argc >= 2 ? ygets_q(argc - 2) : NULL);
  if (comment == NULL) comment = "";
  fits_write_comment(fptr, comment, &status);
  if (status != 0) yfits_error(status);
  ypush_nil();
}

void
Y_fitsio_write_history(int argc)
{
  fitsfile* fptr;
  char *history;
  int status = 0;
  if (argc != 1 && argc != 2) y_error("expecting 1 or 2 arguments");
  fptr = fetch_fitsfile(argc - 1, NOT_CLOSED|CRITICAL);
  history = (argc >= 2 ? ygets_q(argc - 2) : NULL);
  if (history == NULL) history = "";
  fits_write_history(fptr, history, &status);
  if (status != 0) yfits_error(status);
  ypush_nil();
}

void
Y_fitsio_delete_key(int argc)
{
  fitsfile* fptr;
  char* keystr;
  int status = 0, keynum, keytype;
  if (argc != 2) y_error("expecting exactly 2 arguments");
  fptr = fetch_fitsfile(argc - 1, NOT_CLOSED|CRITICAL);
  keytype = get_key(argc - 2, &keynum, &keystr);
  if (keytype == Y_STRING) {
    if ( /* FIXME: */ keystr == NULL || keystr[0] == '\0') {
      status = KEY_NO_EXIST;
    } else {
      fits_delete_key(fptr, keystr, &status);
    }
  } else {
    fits_delete_record(fptr, keynum, &status);
  }
  if (status != 0) {
    yfits_error(status);
  }
  ypush_nil();
}

/*---------------------------------------------------------------------------*/
/* PRIMARY HDU OR IMAGE EXTENSION */

void
Y_fitsio_get_img_type(int argc)
{
  int bitpix, status = 0;
  if (argc != 1) y_error("expecting exactly one argument");
  fits_get_img_type(fetch_fitsfile(0, NOT_CLOSED|CRITICAL), &bitpix, &status);
  if (status != 0) yfits_error(status);
  ypush_int(bitpix);
}

void
Y_fitsio_get_img_equivtype(int argc)
{
  int bitpix, status = 0;
  if (argc != 1) y_error("expecting exactly one argument");
  fits_get_img_equivtype(fetch_fitsfile(0, NOT_CLOSED|CRITICAL), &bitpix,
                         &status);
  if (status != 0) yfits_error(status);
  ypush_int(bitpix);
}

void
Y_fitsio_get_img_dim(int argc)
{
  int naxis, status = 0;
  if (argc != 1) y_error("expecting exactly one argument");
  fits_get_img_dim(fetch_fitsfile(0, NOT_CLOSED|CRITICAL), &naxis, &status);
  if (status != 0) yfits_error(status);
  ypush_int(naxis);
}

void
Y_fitsio_get_img_size(int argc)
{
  fitsfile* fptr;
  long dims[2];
  int naxis, status = 0;
  if (argc != 1) y_error("expecting exactly one argument");
  fptr = fetch_fitsfile(0, NOT_CLOSED|CRITICAL);
  fits_get_img_dim(fptr, &naxis, &status);
  if (status != 0) yfits_error(status);
  if (naxis <= 0) {
    ypush_nil();
  } else {
    dims[0] = 1;
    dims[1] = naxis;
    fits_get_img_size(fptr, naxis, ypush_l(dims), &status);
    if (status != 0) yfits_error(status);
  }
}

void
Y_fitsio_read_img(int argc)
{
  scalar_t null;
  fitsfile* fptr;
  long ntot, first, number;
  long dims[Y_DIMSIZE];
  long c[Y_DIMSIZE - 1];
  long* fpix = NULL;
  long* lpix = NULL;
  long* ipix = NULL;
  void* arr;
  long null_index;
  int naxis, bitpix, status, mode, datatype, anynull;
  int iarg, first_iarg, last_iarg, incr_iarg, number_iarg;
  int eltype;
  size_t elsize;

  /* Parse arguments. */
  null_index = -1;
  first_iarg = -1;
  last_iarg = -1;
  incr_iarg = -1;
  number_iarg = -1;
  mode = 0;
  fptr = NULL;
  for (iarg = argc - 1; iarg >= 0; --iarg) {
    long index = yarg_key(iarg);
    if (index < 0) {
      /* Positional argument. */
      if (fptr == NULL) {
        fptr = fetch_fitsfile(iarg, NOT_CLOSED|CRITICAL);
      } else if (null_index < 0) {
        null_index = yget_ref(iarg);
        if (null_index < 0) {
          y_error("argument NULL must be set with a simple variable");
        }
      } else {
        y_error("too many arguments");
      }
    } else {
      /* Keyword argument. */
      --iarg;
      if (index == index_of_first) {
        first_iarg = iarg;
        mode |= 1;
      } else if (index == index_of_last) {
        last_iarg = iarg;
        mode |= 2;
      } else if (index == index_of_incr) {
        incr_iarg = iarg;
        mode |= 4;
     } else if (index == index_of_number) {
        number_iarg = iarg;
        mode |= 8;
      } else {
        y_error("unsupported keyword");
      }
    }
  }
  if (fptr == NULL) {
    y_error("too few arguments");
  }

  /* Get array dimensions and type. */
  status = 0;
  get_image_param(fptr, Y_DIMSIZE - 1, NULL, &naxis, &dims[1], &ntot, &status);
  if (status != 0) yfits_error(status);
  if (naxis <= 0) {
    ypush_nil();
    return;
  }
  if (fits_get_img_equivtype(fptr, &bitpix, &status)) {
    yfits_error(status);
  }

  /* Parse sub-array options. */
  fpix = NULL;
  lpix = NULL;
  ipix = NULL;
  if (mode == 0) {
    /* Read complete array. */
    dims[0] = naxis;
    first = 1;
    number = ntot;
  } else if (mode == 3 || mode == 7) {
    /* Read rectangular sub-array. */
    long d[Y_DIMSIZE], n;
    int k;
    fpix = ygeta_l(first_iarg, &n, d);
    if ((d[0] != 0 && d[0] != 1) || n != naxis) {
      y_error("bad number of coordinates for keyword FIRST");
    }
    lpix = ygeta_l(last_iarg, &n, d);
    if ((d[0] != 0 && d[0] != 1) || n != naxis) {
      y_error("bad number of coordinates for keyword LAST");
    }
    if (incr_iarg == -1) {
      ipix = c;
      for (k = 0; k < naxis; ++k) {
        ipix[k] = 1;
      }
    } else {
      ipix = ygeta_l(incr_iarg, &n, d);
      if ((d[0] != 0 && d[0] != 1) || n != naxis) {
        y_error("bad number of coordinates for keyword INCR");
      }
    }
    for (k = 0; k < naxis; ++k) {
      if (fpix[k] < 1 || fpix[k] > lpix[k] || lpix[k] > dims[k+1] ||
          ipix[k] < 1 || (lpix[k] - fpix[k] + 1)%ipix[k] != 0) {
        y_error("bad sub-array parameters (FIRST, LAST, INCR)");
      }
      dims[k+1] = (lpix[k] - fpix[k] + 1)/ipix[k];
    }
    dims[0] = naxis;
    first = number = 0; /* avoid warnings about uninitialized variables */
  } else if (mode == 9) {
    /* Read flat sub-array. */
    first = ygets_l(first_iarg);
    number = ygets_l(number_iarg);
    if (first < 1 || number < 0 || first - 1 + number > ntot) {
      y_error("bad range of array elements");
    }
    if (number == 0) {
      ypush_nil();
      return;
    }
    dims[0] = 1;
    dims[1] = number;
  } else {
    y_error("bad combination of keywords FIRST, LAST, INCR, or NUMBER");
  }

  /* Get the type of the elements and create the array. */
  if (fits_get_img_equivtype(fptr, &bitpix, &status)) {
    yfits_error(status);
  }
  if (bitpix == BYTE_IMG) {
    elsize = 1;
    eltype = 0;
  } else if (bitpix == SBYTE_IMG || bitpix == SHORT_IMG) {
    elsize = 2;
    eltype = 1;
  } else if (bitpix == USHORT_IMG || bitpix == LONG_IMG) {
    elsize = 4;
    eltype = 1;
  } else if (bitpix == ULONG_IMG || bitpix == LONGLONG_IMG) {
    elsize = 8;
    eltype = 1;
  } else if (bitpix == FLOAT_IMG) {
    elsize = sizeof(float);
    eltype = 2;
  } else if (bitpix == DOUBLE_IMG) {
    elsize = sizeof(double);
    eltype = 2;
  } else {
    eltype = 0;
    elsize = 0;
  }
  if (elsize <= sizeof(char) && eltype == 0) {
    datatype = TBYTE;
    null.type = Y_CHAR;
    arr = ypush_c(dims);
  } else if (elsize <= sizeof(short) && eltype == 1) {
    datatype = TSHORT;
    null.type = Y_SHORT;
    arr = ypush_s(dims);
  } else if (elsize <= sizeof(int) && eltype == 1) {
    datatype = TINT;
    null.type = Y_INT;
    arr = ypush_i(dims);
  } else if (elsize <= sizeof(long) && eltype == 1) {
    datatype = TLONG;
    null.type = Y_LONG;
    arr = ypush_l(dims);
  } else if (elsize <= sizeof(float) && eltype == 2) {
    datatype = TFLOAT;
    null.type = Y_FLOAT;
    arr = ypush_f(dims);
  } else if (elsize <= sizeof(double) && eltype == 2) {
    datatype = TDOUBLE;
    null.type = Y_DOUBLE;
    arr = ypush_d(dims);
  } else {
    datatype = -1;
    arr = NULL;
    y_error("unsupported data type");
  }

  /* Read the data. */
  if (mode == 0 || mode == 9) {
    fits_read_img(fptr, datatype, first, number,
                  &null.value, arr, &anynull, &status);
  } else {
    fits_read_subset(fptr, datatype, fpix, lpix, ipix,
                     &null.value, arr, &anynull, &status);
  }
  if (status != 0) {
    yfits_error(status);
  }

  /* Save the 'null' value. */
  if (null_index >= 0) {
    if (anynull == 0) {
      ypush_nil();
    } else {
      push_scalar(&null);
    }
    yput_global(null_index, 0);
    yarg_drop(1);
  }
}

void
Y_fitsio_create_img(int argc)
{
  fitsfile* fptr;
  long dims[MAXDIMS + 1];
  int bitpix, status = 0;
  if (argc < 2) y_error("not enough arguments");
  fptr = fetch_fitsfile(argc - 1, NOT_CLOSED|CRITICAL);
  bitpix = fetch_int(argc - 2);
  get_dimlist(argc - 3, 0, dims, MAXDIMS);
  if (fits_create_img(fptr, bitpix, dims[0], &dims[1], &status) != 0) {
    yfits_error(status);
  }
  yarg_drop(argc - 1);
}

void
Y_fitsio_copy_cell2image(int argc)
{
  fitsfile* inp;
  fitsfile* out;
  char* colname;
  long rownum;
  int status = 0;

  if (argc != 5) y_error("expecting exactly 4 arguments");
  inp = fetch_fitsfile(argc - 1, NOT_CLOSED|CRITICAL);
  out = fetch_fitsfile(argc - 2, NOT_CLOSED);
  colname = ygets_q(argc - 3);
  rownum = ygets_l(argc - 4);
  if (colname == NULL || colname[0] == '\0') {
    y_error("invalid column name");
  }
  fits_copy_cell2image(inp, out, colname, rownum, &status);
  if (status != 0) yfits_error(status);
  yarg_drop(2); /* left output object on top of stack */
}

void
Y_fitsio_write_img(int argc)
{
  fitsfile* fptr;
  long src_number, dst_number, first, flen;
  long src_dims[Y_DIMSIZE];
  long dst_dims[Y_DIMSIZE];
  long* fpix;
  long  lpix[Y_DIMSIZE - 1];
  void* src;
  void* null;
  int k, type, naxis, status;
  int iarg, first_iarg, null_iarg, first_case;

  /* Parse arguments. */
  null_iarg = -1;
  first_iarg = -1;
  fptr = NULL;
  src = NULL;
  for (iarg = argc - 1; iarg >= 0; --iarg) {
    long index = yarg_key(iarg);
    if (index < 0) {
      /* Positional argument. */
      if (fptr == NULL) {
        fptr = fetch_fitsfile(iarg, NOT_CLOSED|CRITICAL);
      } else if (src == NULL) {
        src = ygeta_any(iarg, &src_number, src_dims, &type);
      } else {
        y_error("too many arguments");
      }
    } else {
      /* Keyword argument. */
      --iarg;
      if (index == index_of_first) {
        first_iarg = iarg;
      } else if (index == index_of_null) {
        null_iarg = iarg;
      } else {
        y_error("unsupported keyword");
      }
    }
  }
  if (src == NULL) {
    y_error("too few arguments");
  }
  if (null_iarg == -1) {
    null = NULL;
  } else {
    int id = yarg_typeid(null_iarg);
    if (id == Y_VOID) {
      null = NULL;
    } else {
      if (yarg_rank(null_iarg) != 0) {
        y_error("null value must be a scalar");
      }
      if (id != type) {
        y_error("null value must be of same type as the data");
      }
      null = ygeta_any(null_iarg, NULL, NULL, NULL);
    }
  }

  /* Convert Yorick type to CFITSIO "pixel" type. */
  switch (type) {
  case Y_CHAR:
    type = TBYTE;
    break;
  case Y_SHORT:
    type = TSHORT;
    break;
  case Y_INT:
    type = TINT;
    break;
  case Y_LONG:
    type = (sizeof(long) == 8 ? TLONGLONG : TLONG);
    break;
  case Y_FLOAT:
    type = TFLOAT;
    break;
  case Y_DOUBLE:
    type = TDOUBLE;
    break;
  case Y_COMPLEX:
    type = TDBLCOMPLEX;
    break;
  case Y_STRING:
    type = TSTRING;
    break;
  default:
    y_error("unsupported array type");
    type = -1;
  }

  /* Get FITS array dimensions and type. */
  status = 0;
  get_image_param(fptr, Y_DIMSIZE - 1, NULL, &naxis, &dst_dims[1],
                  &dst_number, &status);
  if (status != 0) yfits_error(status);
  dst_dims[0] = naxis; /* to mimic Yorick dimension list */
  if (naxis < 0) y_error("bad number of dimensions");

  /* Parse FIRST keyword to decide how to write. */
  if (first_iarg == -1) {
    first_case = 0;
  } else {
    int id = yarg_typeid(first_iarg);
    first_case = -1;
    if (id <= Y_LONG) {
      int rank = yarg_rank(first_iarg);
      if (rank == 0) {
        first_case = 1;
      } else if (rank == 1) {
        first_case = 2;
      }
    } else if (id == Y_VOID) {
      first_case = 0;
    }
  }
  first = 1;
  fpix = NULL;
  if (first_case == 0) {
    /* Keyword FIRST unspecified or void, write the whole source array whose
       dimensions must match those of the FITS array. */
    for (k = 0; k <= naxis; ++k) {
      if (src_dims[k] != dst_dims[k]) {
        y_error("not same dimensions");
      }
    }
  } else if (first_case == 1) {
    /* Will write an interval of elements. */
    first = ygets_l(first_iarg);
    if (first < 1 || src_number + first - 1 > dst_number) {
      y_error("out of range interval");
    }
  } else if (first_case == 2) {
    /* Will write a subarray. */
    fpix = ygeta_l(first_iarg, &flen, NULL);
    if (flen != naxis) {
      y_error("bad number of values in keyword FIRST");
    }
    if (src_dims[0] > dst_dims[0]) {
      y_error("source array has too many dimensions");
    }
    for (k = 0; k < naxis; ++k) {
      lpix[k] = fpix[k] + (k < src_dims[0] ? src_dims[k+1] - 1 : 0);
      if (fpix[k] < 1 || lpix[k] > dst_dims[k+1]) {
        y_error("out of range subarray");
      }
    }
    if (null != NULL) {
      y_error("NULL keyword forbidden when writing a rectangular subarray");
    }
  } else {
    y_error("invalid type/rank for keyword FIRST");
  }

  /* Write the values. */
  if (fpix != NULL) {
    fits_write_subset(fptr, type, fpix, lpix, src, &status);
  } else if (null != NULL) {
    fits_write_imgnull(fptr, type, first, src_number, src, null, &status);
  } else {
    fits_write_img(fptr, type, first, src_number, src, &status);
  }
  if (status != 0) {
    yfits_error(status);
  }
  ypush_nil();
}

void
Y_fitsio_copy_image2cell(int argc)
{
  fitsfile* inp;
  fitsfile* out;
  char* colname;
  long rownum, longval;
  int flag, status = 0;

  if (argc != 5) y_error("expecting exactly 5 arguments");
  inp = fetch_fitsfile(argc - 1, NOT_CLOSED|CRITICAL);
  out = fetch_fitsfile(argc - 2, NOT_CLOSED);
  colname = ygets_q(argc - 3);
  rownum  = ygets_l(argc - 4);
  longval = ygets_l(argc - 5);
  if (longval < 0 || longval > 2) y_error("bad value for COPYKEYFLAG");
  flag = (int)longval;
  if (colname == NULL || colname[0] == '\0') {
    y_error("invalid column name");
  }
  fits_copy_image2cell(inp, out, colname, rownum, flag, &status);
  if (status != 0) yfits_error(status);
  yarg_drop(3); /* left output object on top of stack */
}

void
Y_fitsio_copy_image_section(int argc)
{
  fitsfile* inp;
  fitsfile* out;
  char* section;
  int status = 0;
  if (argc != 3) y_error("expecting exactly 3 arguments");
  inp = fetch_fitsfile(2, NOT_CLOSED|CRITICAL);
  out = fetch_fitsfile(1, NOT_CLOSED);
  section = ygets_q(0);
  if (section == NULL || section[0] == '\0') {
    y_error("invalid section string");
  }
  fits_copy_image_section(inp, out, section, &status);
  if (status != 0) yfits_error(status);
  yarg_drop(1); /* left output on top of stack */
}

static void
check_ncols(int* tfields, long ntot)
{
  if (*tfields > 0) {
    if (*tfields != ntot) {
      y_error("number of columns mismatch");
    }
  } else {
    if ((*tfields = ntot) != ntot) {
      y_error("too many columns (integer overflow)");
    }
  }
}

/*---------------------------------------------------------------------------*/
/* TABLES */

void
Y_fitsio_create_tbl(int argc)
{
  fitsfile* fptr;
  long nrows, ntot;
  long dims[Y_DIMSIZE];
  char** ttype;
  char** tform;
  char** tunit;
  char* extname;
  int status, iarg, tbltype, tfields;

  tbltype = BINARY_TBL;
  fptr = NULL;
  extname = NULL;
  ttype = NULL;
  tform = NULL;
  tunit = NULL;
  tfields = -1;
  nrows = 0;
  for (iarg = argc - 1; iarg >= 0; --iarg) {
    long index = yarg_key(iarg);
    if (index < 0) {
      /* Positional argument. */
      if (fptr == NULL) {
        fptr = fetch_fitsfile(iarg, NOT_CLOSED|CRITICAL);
      } else if (ttype == NULL) {
        ttype = ygeta_q(iarg, &ntot, dims);
        if (dims[0] > 1) y_error("too many dimensions for argument TTYPE");
        check_ncols(&tfields, ntot);
      } else if (tform == NULL) {
        tform = ygeta_q(iarg, &ntot, dims);
        if (dims[0] > 1) y_error("too many dimensions for argument TFORM");
        check_ncols(&tfields, ntot);
      } else {
        y_error("too many arguments");
      }
    } else {
      /* Keyword argument. */
      --iarg;
      if (index == index_of_extname) {
        extname = ygets_q(iarg);
      } else if (index == index_of_tunit) {
        tunit = ygeta_q(iarg, &ntot, dims);
        if (dims[0] > 1) y_error("too many dimensions for argument TUNIT");
        check_ncols(&tfields, ntot);
      } else if (index == index_of_ascii) {
        tbltype = (yarg_true(iarg) ? ASCII_TBL : BINARY_TBL);
        check_ncols(&tfields, ntot);
      } else {
        y_error("unsupported keyword");
      }
    }
  }
  if (tform == NULL) y_error("too few arguments");
  status = 0;
  if (fits_create_tbl(fptr, tbltype, nrows, tfields, ttype,
                      tform,  tunit, extname, &status) != 0) {
    yfits_error(status);
  }
  ypush_nil();
}

void
Y_fitsio_get_num_rows(int argc)
{
  long nrows;
  int status = 0;
  if (argc != 1) y_error("expecting exactly one argument");
  fits_get_num_rows(fetch_fitsfile(0, NOT_CLOSED|CRITICAL), &nrows, &status);
  if (status != 0) yfits_error(status);
  ypush_long(nrows);
}

void
Y_fitsio_get_num_cols(int argc)
{
  int ncols;
  int status = 0;
  if (argc != 1) y_error("expecting exactly one argument");
  fits_get_num_cols(fetch_fitsfile(0, NOT_CLOSED|CRITICAL), &ncols, &status);
  if (status != 0) yfits_error(status);
  ypush_long(ncols);
}

void
Y_fitsio_get_colnum(int argc)
{
  fitsfile* fptr;
  long  dims[2];
  char* template;
  long* result;
  int   iarg, status, casesen, colnum, ncols, col;

  fptr = NULL;
  template = NULL;
  casesen = CASEINSEN;
  for (iarg = argc - 1; iarg >= 0; --iarg) {
    long index = yarg_key(iarg);
    if (index < 0) {
      /* Positional argument. */
      if (fptr == NULL) {
        fptr = fetch_fitsfile(iarg, NOT_CLOSED|CRITICAL);
      } else if (template == NULL) {
        template = ygets_q(iarg);
        if (template == NULL || template[0] == 0) {
          y_error("invalid TEMPLATE string");
        }
      } else {
        y_error("too many arguments");
      }
    } else {
      /* Keyword argument. */
      --iarg;
      if (index == index_of_case) {
        casesen = (yarg_true(iarg) ? CASESEN : CASEINSEN);
      } else {
        y_error("unsupported keyword");
      }
    }
  }
  if (template == NULL) y_error("too few arguments");

  status = 0;
  fits_get_colnum(fptr, casesen, template, &colnum, &status);
  if (status == 0) {
    ypush_long(colnum);
    return;
  }
  if (status == COL_NOT_FOUND) {
    ypush_nil();
    return;
  }
  ncols = 0;
  while (status == COL_NOT_UNIQUE) {
    ++ncols;
    fits_get_colnum(fptr, casesen, template, &colnum, &status);
  }
  if (status != COL_NOT_FOUND) {
    yfits_error(status);
  }
  dims[0] = 1;
  dims[1] = ncols;
  result = ypush_l(dims);
  status = 0;
  for (col = 0; col < ncols; ++col) {
    fits_get_colnum(fptr, casesen, template, &colnum, &status);
    if (status != COL_NOT_UNIQUE) {
      yfits_error(status);
    }
    result[col] = colnum;
  }
}

void
Y_fitsio_get_colname(int argc)
{
  fitsfile* fptr;
  long   dims[2];
  char   colname[80];
  char*  template;
  char** result;
  int    iarg, status, casesen, colnum, ncols, col;

  fptr = NULL;
  template = NULL;
  casesen = CASEINSEN;
  for (iarg = argc - 1; iarg >= 0; --iarg) {
    long index = yarg_key(iarg);
    if (index < 0) {
      /* Positional argument. */
      if (fptr == NULL) {
        fptr = fetch_fitsfile(iarg, NOT_CLOSED|CRITICAL);
      } else if (template == NULL) {
        template = ygets_q(iarg);
        if (template == NULL || template[0] == 0) {
          y_error("invalid TEMPLATE string");
        }
      } else {
        y_error("too many arguments");
      }
    } else {
      /* Keyword argument. */
      --iarg;
      if (index == index_of_case) {
        casesen = (yarg_true(iarg) ? CASESEN : CASEINSEN);
      } else {
        y_error("unsupported keyword");
      }
    }
  }
  if (template == NULL) y_error("too few arguments");

  status = 0;
  fits_get_colname(fptr, casesen, template, colname, &colnum, &status);
  if (status == 0) {
    push_string(colname);
    return;
  }
  if (status == COL_NOT_FOUND) {
    ypush_nil();
    return;
  }
  ncols = 0;
  while (status == COL_NOT_UNIQUE) {
    ++ncols;
    fits_get_colname(fptr, casesen, template, colname, &colnum, &status);
  }
  if (status != COL_NOT_FOUND) {
    yfits_error(status);
  }
  dims[0] = 1;
  dims[1] = ncols;
  result = ypush_q(dims);
  status = 0;
  for (col = 0; col < ncols; ++col) {
    fits_get_colname(fptr, casesen, template, colname, &colnum, &status);
    if (status != COL_NOT_UNIQUE) {
      yfits_error(status);
    }
    result[col] = p_strcpy(colname);
  }
}

static int
get_colnum(int iarg, fitsfile *fptr)
{
  char* colname;
  int type, rank, status, ncols, colnum;

  type = yarg_typeid(iarg);
  rank = yarg_rank(iarg);
  if (type <= Y_LONG && rank == 0) {
    status = 0;
    if (fits_get_num_cols(fptr, &ncols, &status) != 0) {
      yfits_error(status);
    }
    colnum = ygets_i(iarg);
    if (colnum < 1 || colnum > ncols) {
      y_error("out of range column number");
    }
    return colnum;
  }
  if (type == Y_STRING && rank == 0) {
    colname = ygets_q(iarg);
    if (colname == NULL || colname[0] == '\0') {
      status = COL_NOT_FOUND;
    } else {
      status = 0;
      fits_get_colnum(fptr, CASEINSEN, colname, &colnum, &status);
    }
    if (status != 0) {
      if (status == COL_NOT_FOUND) {
        y_error("column name not found");
      }
      if (status != COL_NOT_UNIQUE) {
        y_error("column name not unique");
      }
      yfits_error(status);
    }
    return colnum;
  }
  y_error("expecting column number or name");
  return -1;
}

static void
get_coltype(int argc, int eqcoltype)
{
  fitsfile* fptr;
  long  dims[2];
  long* result;
  long  repeat, width;
  int   status = 0, type, colnum;

  if (argc != 2) y_error("expecting exactly 2 arguments");
  fptr = fetch_fitsfile(1, NOT_CLOSED|CRITICAL);
  colnum = get_colnum(0, fptr);
  if (eqcoltype) {
    fits_get_eqcoltype(fptr, colnum, &type, &repeat, &width, &status);
  } else {
    fits_get_coltype(fptr, colnum, &type, &repeat, &width, &status);
  }
  if (status != 0) {
    yfits_error(status);
  }
  dims[0] = 1;
  dims[1] = 3;
  result = ypush_l(dims);
  result[0] = type;
  result[1] = repeat;
  result[2] = width;
}

void
Y_fitsio_get_coltype(int argc)
{
  get_coltype(argc, 0);
}

void
Y_fitsio_get_eqcoltype(int argc)
{
  get_coltype(argc, 1);
}

static void
push_tdim(int naxis, long naxes[])
{
  long  dims[2];
  long* result;
  int   k;

  if (naxis == 1 && naxes[0] == 1) {
    naxis = 0;
  }
  dims[0] = 1;
  dims[1] = naxis + 1;
  result = ypush_l(dims);
  result[0] = naxis;
  for (k = 0; k < naxis; ++k) {
    result[k+1] = naxes[k];
  }
}

void
Y_fitsio_read_tdim(int argc)
{
  fitsfile* fptr;
  long  naxes[Y_DIMSIZE - 1];
  int   status = 0, colnum, naxis;

  if (argc != 2) y_error("expecting exactly 2 arguments");
  fptr = fetch_fitsfile(1, NOT_CLOSED|CRITICAL);
  colnum = get_colnum(0, fptr);
  if (fits_read_tdim(fptr, colnum,
                     Y_DIMSIZE - 1, &naxis, naxes, &status) != 0) {
    yfits_error(status);
  }
  push_tdim(naxis, naxes);
}

void
Y_fitsio_decode_tdim(int argc)
{
  fitsfile* fptr;
  long  naxes[Y_DIMSIZE - 1];
  char* tdimstr;
  int   status = 0, colnum, naxis;

  if (argc != 3) y_error("expecting exactly 3 arguments");
  fptr = fetch_fitsfile(2, NOT_CLOSED|CRITICAL);
  tdimstr = ygets_q(1);
  colnum = get_colnum(0, fptr);
  if (fits_decode_tdim(fptr, tdimstr, colnum,
                       Y_DIMSIZE - 1, &naxis, naxes, &status) != 0) {
    yfits_error(status);
  }
  push_tdim(naxis, naxes);
}

void
Y_fitsio_write_tdim(int argc)
{
  fitsfile* fptr;
  long dims[Y_DIMSIZE];
  int  status = 0, colnum;

  if (argc < 2) y_error("expecting at least 2 arguments");
  fptr = fetch_fitsfile(argc - 1, NOT_CLOSED|CRITICAL);
  colnum = get_colnum(argc - 2, fptr);
  get_dimlist(argc - 3, 0, dims, Y_DIMSIZE - 1);
  if (dims[0] == 0) {
    dims[0] = 1;
    dims[1] = 1;
  }
  if (fits_write_tdim(fptr, colnum, dims[0], &dims[1], &status) != 0) {
    yfits_error(status);
  }
  ypush_nil();
}

void
Y_fitsio_write_col(int argc)
{
  fitsfile* fptr;
  long number, firstrow, repeat, width;
  long dims[Y_DIMSIZE];
  long naxes[Y_DIMSIZE - 1];
  void* arr;
  void* null;
  int k, type, naxis, status, colnum, coltype;
  int iarg, null_iarg, pos;

  /* Parse arguments. */
  null_iarg = -1;
  firstrow = 1;
  colnum = -1;
  fptr = NULL;
  arr = NULL;
  pos = 0;
  for (iarg = argc - 1; iarg >= 0; --iarg) {
    long index = yarg_key(iarg);
    if (index < 0) {
      /* Positional argument. */
      ++pos;
      if (pos == 1) {
        fptr = fetch_fitsfile(iarg, NOT_CLOSED|CRITICAL);
      } else if (pos == 2) {
        colnum = get_colnum(iarg, fptr);
      } else if (pos == 3) {
        arr = ygeta_any(iarg, &number, dims, &type);
      } else if (pos == 4) {
        firstrow = ygets_l(iarg);
      } else {
        y_error("too many arguments");
      }
    } else {
      /* Keyword argument. */
      --iarg;
      if (index == index_of_null) {
        null_iarg = iarg;
      } else {
        y_error("unsupported keyword");
      }
    }
  }
  if (pos < 3) {
    y_error("too few arguments");
  }
  if (null_iarg == -1) {
    null = NULL;
  } else {
    int id = yarg_typeid(null_iarg);
    if (id == Y_VOID) {
      null = NULL;
    } else {
      if (yarg_rank(null_iarg) != 0) {
        y_error("null value must be a scalar");
      }
      if (id != type) {
        y_error("null value must be of same type as the data");
      }
      null = ygeta_any(null_iarg, NULL, NULL, NULL);
    }
  }

  /* Get column dimensions and type, then check that types are compatible and
     that dimensions (but the last one) are matching. */
  status = 0;
  fits_get_eqcoltype(fptr, colnum, &coltype, &repeat, &width, &status);
  fits_read_tdim(fptr, colnum, Y_DIMSIZE - 1, &naxis, naxes, &status);
  if (status != 0) {
    yfits_error(status);
  }
  if (coltype < 0) {
    y_error("writing variable size arrays not yet implemented");
  }
  if (coltype == TSTRING) {
    /* Column of strings is special. */
    if (type != Y_STRING) {
      y_error("expecting array of strings for this column");
    } else {
      type = TSTRING;
    }
    if (naxis < 1 || naxes[0] != width) {
      y_error("assumption failed!");
    }
    --naxis;
    for (k = 0; k < naxis; ++k) {
      naxes[k] = naxes[k+1];
    }
  } else {
    /* Non-string column. */
    if (naxis == 1 && naxes[0] == 1) {
      naxis = 0;
    }
    switch (type) {
    case Y_CHAR:
      type = (coltype == TBIT || coltype == TLOGICAL ? coltype : TBYTE);
      break;
    case Y_SHORT:
      type = TSHORT;
      break;
    case Y_INT:
      type = TINT;
      break;
    case Y_LONG:
      type = (sizeof(long) == 8 ? TLONGLONG : TLONG);
      break;
    case Y_FLOAT:
      type = TFLOAT;
      break;
    case Y_DOUBLE:
      type = TDOUBLE;
      break;
    case Y_COMPLEX:
      type = TDBLCOMPLEX;
      break;
    case Y_STRING:
      type = TSTRING;
      break;
    default:
      y_error("unsupported array type");
    }
  }
  if (dims[0] != naxis + 1 && dims[0] != naxis) {
    y_error("incompatible number of dimensions");
  }
  for (k = 0; k < naxis; ++k) {
    if (dims[k+1] != naxes[k]) {
      y_error("non matching dimension(s)");
    }
  }

  /* Write the values. */
  if (null == NULL) {
    fits_write_col(fptr, type, colnum, firstrow, 1,
                   number, arr, &status);
  } else {
    fits_write_colnull(fptr, type, colnum, firstrow, 1,
                       number, arr, null, &status);
  }
  if (status != 0) {
    yfits_error(status);
  }
  ypush_nil();
}

void
Y_fitsio_read_col(int argc)
{
  scalar_t null;
  fitsfile* fptr;
  long number, firstrow, lastrow, nrows, null_index, repeat, width;
  long dims[Y_DIMSIZE];
  void* arr;
  int k, coltype, type, naxis, status, colnum, anynull;
  int iarg, pos;

  /* Parse arguments. */
  null_index = -1;
  firstrow = -1;
  lastrow = -1;
  colnum = -1;
  fptr = NULL;
  arr = NULL;
  pos = 0;
  for (iarg = argc - 1; iarg >= 0; --iarg) {
    long index = yarg_key(iarg);
    if (index < 0) {
      /* Positional argument. */
      ++pos;
      if (pos == 1) {
        fptr = fetch_fitsfile(iarg, NOT_CLOSED|CRITICAL);
      } else if (pos == 2) {
        colnum = get_colnum(iarg, fptr);
      } else if (pos == 3) {
        firstrow = ygets_l(iarg);
      } else if (pos == 4) {
        lastrow = ygets_l(iarg);
      } else {
        y_error("too many arguments");
      }
    } else {
      /* Keyword argument. */
      --iarg;
      if (index == index_of_null) {
        null_index = yget_ref(iarg);
      } else {
        y_error("unsupported keyword");
      }
    }
  }
  if (pos < 2) {
    y_error("too few arguments");
  }

  /* Get FITS column dimensions and type. */
  status = 0;
  fits_get_num_rows(fptr, &nrows, &status);
  fits_get_eqcoltype(fptr, colnum, &coltype, &repeat, &width, &status);
  fits_read_tdim(fptr, colnum, Y_DIMSIZE - 1, &naxis, &dims[1], &status);
  if (coltype < 0) {
    y_error("variable size arrays not yet supported");
  }
  if (status != 0) {
    yfits_error(status);
  }
  if (pos < 3) {
    firstrow = 1;
  }
  if (pos < 4) {
    lastrow = nrows;
  }
  if (firstrow < 1 || firstrow > lastrow || lastrow > nrows) {
    y_error("invalid range of rows");
  }

  /* Figure out the dimensions of the result. */
  if (coltype == TSTRING) {
    /* Discard leading dimension for array of strings. */
    if (naxis < 1 || dims[1] != width) {
      y_error("assumption failed!");
    }
    --naxis;
    for (k = 1; k <= naxis; ++k) {
      dims[k] = dims[k+1];
    }
  } else if (naxis == 1 && dims[1] == 1) {
    naxis = 0;
  }
  dims[0] = naxis;
  if (firstrow < lastrow) {
    /* Append a trailing dimension whose length is equal to the number of rows
       to read. */
    if (naxis >= Y_DIMSIZE - 1) {
      y_error("too many dimensions");
    }
    dims[0] = ++naxis;
    dims[naxis] = lastrow - firstrow + 1;
  }
  number = 1;
  for (k = 1; k <= naxis; ++k) {
    number *= dims[k];
  }

  /* Create the destination array. */
  switch (coltype) {
  case TBIT:
    /* Reading/writing bits with datatype = TBIT results in considering that
       each bit is stored in a single char with value 0/1. */
    type = TBIT;
    arr = ypush_c(dims);
    null.type = Y_CHAR;
    break;

  case TSTRING:
    type = TSTRING;
    {
      char** str = ypush_q(dims);
      size_t size = width + 1; /* enough bytes for the longuest string */
      long i;
      for (i = 0; i < number; ++i) {
        str[i] = p_malloc(size);
      }
      arr = str;
    }
    null.type = Y_CHAR; /* FIXME: */
    break;

  case TBYTE:
  case TLOGICAL:
    type = TBYTE;
    arr = ypush_c(dims);
    null.type = Y_CHAR;
    break;

  case TSBYTE:
  case TSHORT:
    type = TSHORT;
    arr = ypush_s(dims);
    null.type = Y_SHORT;
    break;

  case TUSHORT:
  case TINT:
    type = TINT;
    arr = ypush_i(dims);
    null.type = Y_INT;
    break;

#if TINT32BIT != TLONG
  case TINT32BIT:
    if (sizeof(int) >= 4) {
      type = TINT;
      arr = ypush_i(dims);
      null.type = Y_INT;
    } else {
      type = TLONG;
      arr = ypush_l(dims);
      null.type = Y_LONG;
    }
    break;
#endif

  case TUINT:
  case TULONG:
  case TLONG:
  case TLONGLONG:
    type = TLONG;
    arr = ypush_l(dims);
    null.type = Y_LONG;
    break;

  case TFLOAT:
    type = TFLOAT;
    arr = ypush_f(dims);
    null.type = Y_FLOAT;
    break;

  case TDOUBLE:
    type = TDOUBLE;
    arr = ypush_d(dims);
    null.type = Y_DOUBLE;
    break;

  case TCOMPLEX:
  case TDBLCOMPLEX:
    type = TDBLCOMPLEX;
    arr = ypush_z(dims);
    null.type = Y_COMPLEX;
    break;

  default:
    arr = NULL;
    y_error("unsupported array type");
  }

  /* Read the values. */
  fits_read_col(fptr, type, colnum, firstrow, 1, number,
                &null.value, arr, &anynull, &status);
  if (status != 0) {
    yfits_error(status);
  }

  /* Save the 'null' value. */
  if (null_index != -1) {
    if (anynull == 0) {
      ypush_nil();
    } else {
      push_scalar(&null);
    }
    yput_global(null_index, 0);
    yarg_drop(1);
  }
}

#if 0
void
Y_fitsio_insert_rows(int argc)
{
  fitsfile* fptr;
  long firstrow, nrows;
  int status = 0;
}
#endif

/*---------------------------------------------------------------------------*/
/* UTILITY ROUTINES */

void
Y_fitsio_write_chksum(int argc)
{
  int status = 0;
  if (argc != 1) y_error("expecting exactly one argument");
  fits_write_chksum(yfits_fetch(0, NOT_CLOSED|CRITICAL)->fptr, &status);
  if (status != 0) yfits_error(status);
  ypush_nil();
}

void
Y_fitsio_update_chksum(int argc)
{
  int status = 0;
  if (argc != 1) y_error("expecting exactly one argument");
  fits_update_chksum(yfits_fetch(0, NOT_CLOSED|CRITICAL)->fptr, &status);
  if (status != 0) yfits_error(status);
  ypush_nil();
}

void
Y_fitsio_verify_chksum(int argc)
{
  long dims[] = {1, 2};
  int* result;
  int status = 0;
  if (argc != 1) y_error("expecting exactly one argument");
  result = ypush_i(dims);
  fits_verify_chksum(yfits_fetch(1, NOT_CLOSED|CRITICAL)->fptr,
                     &result[0], &result[1], &status);
  if (status != 0) yfits_error(status);
}

void
Y_fitsio_get_chksum(int argc)
{
  long dims[] = {1, 2};
  unsigned long* result;
  int status = 0;
  if (argc != 1) y_error("expecting exactly one argument");
  result = (unsigned long*)ypush_l(dims);
  fits_get_chksum(yfits_fetch(1, NOT_CLOSED|CRITICAL)->fptr,
                  &result[0], &result[1], &status);
  if (status != 0) yfits_error(status);
}

void
Y_fitsio_encode_chksum(int argc)
{
  char ascii[17];
  unsigned long sum;
  int compl;

  if (argc == 1) {
    sum = ygets_l(0);
    compl = FALSE;
  } else if (argc == 2) {
    sum = ygets_l(1);
    compl = yarg_true(0);
  } else {
    y_error("expecting 1 or 2 argument");
  }
  fits_encode_chksum(sum, compl, ascii);
  push_string(ascii);
}

void
Y_fitsio_decode_chksum(int argc)
{
  char* ascii;
  unsigned long sum;
  int compl;

  if (argc == 1) {
    ascii = ygets_q(0);
    compl = FALSE;
  } else if (argc == 2) {
    ascii = ygets_q(1);
    compl = yarg_true(0);
  } else {
    y_error("expecting 1 or 2 argument");
  }
  if (ascii == NULL || strlen(ascii) != 16) {
    y_error("length of checksum string should be exactly 16 characters");
  }
  ypush_long(fits_decode_chksum(ascii, compl, &sum));
}

/*---------------------------------------------------------------------------*/
/* MISCELLANEOUS */

void
Y_fitsio_get_version(int argc)
{
  float version;
  fits_get_version(&version);
  ypush_double((double)version);
}

void
Y_fitsio_debug(int argc)
{
  int new_value, old_value;
  if (argc != 1) y_error("expecting exactly one argument");
  new_value = yarg_true(0);
  old_value = yfits_debug;
  yfits_debug = new_value;
  ypush_int(old_value);
}

void
Y_fitsio_setup(int argc)
{
  /* Define constants. */
#define DEFINE_INT_CONST(c)    define_int_const("FITSIO_"#c, c)
  DEFINE_INT_CONST(IMAGE_HDU);
  DEFINE_INT_CONST(ASCII_TBL);
  DEFINE_INT_CONST(BINARY_TBL);
  DEFINE_INT_CONST(ANY_HDU);
  DEFINE_INT_CONST(BYTE_IMG);
  DEFINE_INT_CONST(SHORT_IMG);
  DEFINE_INT_CONST(LONG_IMG);
  DEFINE_INT_CONST(LONGLONG_IMG);
  DEFINE_INT_CONST(FLOAT_IMG);
  DEFINE_INT_CONST(DOUBLE_IMG);
  DEFINE_INT_CONST(SBYTE_IMG);
  DEFINE_INT_CONST(USHORT_IMG);
  DEFINE_INT_CONST(ULONG_IMG);
#undef DEFINE_INT_CONST
  ypush_nil();

  /* Define fast keyword/member indexes. */
#define INIT(s) if (index_of_##s == -1L) index_of_##s = yget_global(#s, 0)
  INIT(ascii);
  INIT(basic);
  INIT(case);
  INIT(extname);
  INIT(first);
  INIT(incr);
  INIT(last);
  INIT(null);
  INIT(number);
  INIT(tunit);
  INIT(def);
#undef INIT
}

/*---------------------------------------------------------------------------*/
/* METHODS */

static yfits_object*
yfits_push(void)
{
  return (yfits_object*)ypush_obj(&yfits_type, sizeof(yfits_object));
}

static yfits_object*
yfits_fetch(int iarg, unsigned int flags)
{
  yfits_object* obj = (yfits_object*)yget_obj(iarg, &yfits_type);
  if ((flags & (NOT_CLOSED|MAY_BE_CLOSED)) == NOT_CLOSED
      && obj->fptr == NULL) {
    y_error("FITS file has been closed");
  }
  if ((flags & CRITICAL) == CRITICAL) {
    critical(TRUE);
  }
  return obj;
}

static const char*
hdu_type_name(int type)
{
  switch (type) {
  case IMAGE_HDU:  return "image";
  case BINARY_TBL: return "binary table";
  case ASCII_TBL:  return "ascii table";
  default:         return "unknown HDU type";
  }
}


/*---------------------------------------------------------------------------*/
/* UTILITIES */

static int
trim_string(char* dst, const char* src)
{
  int c, i, copy = FALSE;
  int j = 0; /* index to write in dst */
  int k = 0; /* length, last non space in dst + 1 */

  for (i = 0; (c = src[i]) != '\0'; ++i) {
    if (c == ' ' || IS_WHITE(c)) {
      if (copy) {
        dst[j++] = c;
      }
    } else {
      copy = TRUE;
      dst[j++] = c;
      k = j;
    }
  }
  dst[k] = '\0';
  return k;
}

static void
critical(int clear_errmsg)
{
  if (p_signalling) {
    p_abort();
  }
  if (clear_errmsg) {
    fits_clear_errmsg();
  }
}

static void
define_int_const(const char* name, int value)
{
  ypush_int(value);
  yput_global(yget_global(name, 0), 0);
  yarg_drop(1);
}

static int
fetch_int(int iarg)
{
  long lval = ygets_l(iarg);
  int ival = (int)lval;
  if (ival != lval) y_error("integer overflow");
  return ival;
}

static void
push_string(const char* str)
{
  ypush_q(NULL)[0] = p_strcpy(str);
}

static void
define_string(long index, const char* str)
{
  push_string(str);
  yput_global(index, 0);
  yarg_drop(1);
}

static void
push_complex(double re, double im)
{
  double* z = ypush_z(NULL);
  z[0] = re;
  z[1] = im;
}

static void
push_scalar(const scalar_t* s)
{
  switch (s->type) {
  case Y_CHAR:
    *ypush_c(NULL) = s->value.c;
    break;
  case Y_SHORT:
    *ypush_s(NULL) = s->value.s;
    break;
  case Y_INT:
     ypush_int(s->value.i);
    break;
  case Y_LONG:
    ypush_long(s->value.l);
    break;
  case Y_FLOAT:
    *ypush_f(NULL) = s->value.f;
    break;
  case Y_DOUBLE:
     ypush_double(s->value.d);
    break;
  case Y_VOID:
    ypush_nil();
    break;
  default:
    y_error("unknown scalar type");
  }
}

static char*
fetch_path(int iarg)
{
  char** arr = ypush_q(NULL);
  char* arg = ygets_q(iarg + 1);
  if (arg != NULL) arr[0] = p_native(arg);
  yarg_swap(iarg + 1, 0);
  yarg_drop(1);
  return arr[0];
}

static void
get_dimlist(int iarg_first, int iarg_last,
            long* dims, int maxdims)
{
  int iarg, rank, type, ndims = 0, k;
  for (iarg = iarg_first; iarg >= iarg_last; --iarg) {
    type = yarg_typeid(iarg);
    if (type == Y_VOID) {
      continue;
    }
    if (type > Y_LONG) {
      y_error("bad type for dimension");
    }
    rank = yarg_rank(iarg);
    if (rank == 0) {
      /* scalar */
      if (ndims >= maxdims) {
         y_error("too many dimensions");
      }
      dims[++ndims] = ygets_l(iarg);
    } else if (rank == 1) {
      /* vector */
      long i, ntot;
      long* dimlist = ygeta_l(iarg, &ntot, NULL);
      if (ntot < 1 || dimlist[0] != ntot - 1) {
        y_error("bad dimension list");
      }
      if (ndims +  ntot - 1 > maxdims) {
         y_error("too many dimensions");
      }
      for (i = 1; i < ntot; ++i) {
        dims[++ndims] = dimlist[i];
      }
    } else {
      y_error("bad dimension list");
    }
  }
  for (k = 1; k <= ndims; ++k) {
    if (dims[k] < 1) {
      y_error("bad dimension");
    }
  }
  dims[0] = ndims;
}

static int
get_image_param(fitsfile* fptr, int maxdims, int* bitpix_ptr,
                int* naxis_ptr, long dims[], long* number_ptr,
                int* status)
{
  int k, naxis, bitpix;

  if (*status == 0) {
    if (fits_get_img_param(fptr, maxdims, &bitpix, &naxis,
                           dims, status) != 0) {
      /* An error occured, set values to avoid warnings about uninitialized
         varaibles. */
      if (bitpix_ptr != NULL) {
        *bitpix_ptr = 0;
      }
      if (naxis_ptr != NULL) {
        *naxis_ptr = 0;
      }
      if (number_ptr != NULL) {
        *number_ptr = 0;
      }
    } else {
      if (naxis > maxdims) {
        y_error("too many dimensions");
      }
      if (bitpix_ptr != NULL) {
        *bitpix_ptr = bitpix;
      }
      if (naxis_ptr != NULL) {
        *naxis_ptr = naxis;
      }
      if (number_ptr != NULL) {
        long number = 1;
        for (k = 0; k < naxis; ++k) {
          number *= dims[k];
        }
        *number_ptr = number;
      }
    }
  }
  return *status;
}
