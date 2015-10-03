/*
 * yfitsio.c --
 *
 * Implements Yorick interface to CFITSIO library.
 *
 *-----------------------------------------------------------------------------
 *
 * Copyright (C) 2015: Éric Thiébaut <eric.thiebaut@univ-lyon1.fr>
 *
 * See LICENSE.md for details.
 *
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <float.h>
#include <limits.h>

#include <fitsio.h>

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
  } value;
  int type;
} scalar_t;

static void  push_string(const char* str);
static void  push_scalar(const scalar_t* s);
static char* fetch_path(int iarg);
static int   fetch_int(int iarg);
static void  critical(int clear_errmsg);

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
static yfits_object* yfits_fetch(int iarg, int assert_open);

static const char* hdu_type_name(int type);

/* Get image parameters.  Similar to fits_get_img_param but return also
   number of axes and computes number of elements. */
static int
get_image_param(yfits_object* fh, int maxdims, int* bitpix, int* naxis,
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
  obj = yfits_fetch(0, FALSE);
  critical(TRUE);
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
  obj = yfits_fetch(0, TRUE);
  critical(TRUE);
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
  ypush_int(yfits_fetch(0, FALSE)->fptr != NULL ? TRUE : FALSE);
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
  yfits_object* obj;
  char* name;
  int status = 0;

  if (argc != 1) y_error("expecting exactly one argument");
  obj = yfits_fetch(0, FALSE);
  if (obj->fptr == NULL) {
    name = NULL;
  } else {
    name = buffer;
    critical(TRUE);
    if (fits_file_name(obj->fptr, name, &status) != 0) {
      yfits_error(status);
    }
  }
  push_string(name);
}

void
Y_fitsio_file_mode(int argc)
{
  yfits_object* obj;
  const char* mode = NULL;
  if (argc != 1) y_error("expecting exactly one argument");
  obj = yfits_fetch(0, FALSE);
  if (obj->fptr != NULL) {
    int iomode, status = 0;
    critical(TRUE);
    if (fits_file_mode(obj->fptr, &iomode, &status) != 0) {
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
  yfits_object* obj;
  char* url;
  int status = 0;

  if (argc != 1) y_error("expecting exactly one argument");
  obj = yfits_fetch(0, FALSE);
  if (obj->fptr == NULL) {
    url = NULL;
  } else {
    url = buffer;
    critical(TRUE);
    if (fits_url_type(obj->fptr, url, &status) != 0) {
      yfits_error(status);
    }
  }
  push_string(url);
}

void
Y_fitsio_movabs_hdu(int argc)
{
  yfits_object* obj;
  int number, type;
  int status = 0;

  if (argc != 2) y_error("expecting exactly two arguments");
  obj = yfits_fetch(1, TRUE);
  number = fetch_int(0);
  if (number <= 0) y_error("invalid HDU number");
  critical(TRUE);
  if (fits_movabs_hdu(obj->fptr, number, &type, &status) != 0) {
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
  yfits_object* obj;
  int offset, type;
  int status = 0;

  if (argc != 2) y_error("expecting exactly two arguments");
  obj = yfits_fetch(1, TRUE);
  offset = fetch_int(0);
  critical(TRUE);
  if (fits_movrel_hdu(obj->fptr, offset, &type, &status) != 0) {
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
  yfits_object* obj;
  char* extname;
  int type, extver;
  int status = 0;

  if (argc < 3 || argc > 4) y_error("expecting 3 or 4 arguments");
  obj = yfits_fetch(argc - 1, TRUE);
  type = fetch_int(argc - 2);
  extname = ygets_q(argc - 3);
  extver = (argc >= 4 ? fetch_int(argc - 4) : 0);
  if (type != IMAGE_HDU && type != BINARY_TBL &&
      type != ASCII_TBL && type != ANY_HDU) {
    y_error("bad HDUTYPE");
  }
  critical(TRUE);
  if (fits_movnam_hdu(obj->fptr, type, extname, extver, &status) != 0) {
    if (status != BAD_HDU_NUM || yarg_subroutine()) {
      yfits_error(status);
    }
    type = -1;
  } else if (fits_get_hdu_type(obj->fptr, &type, &status) != 0) {
    yfits_error(status);
  }
  ypush_int(type);
}

void
Y_fitsio_get_num_hdus(int argc)
{
  yfits_object* obj;
  int number;
  int status = 0;

  if (argc != 1) y_error("expecting exactly one argument");
  obj = yfits_fetch(0, FALSE);
  if (obj->fptr == NULL) {
    number = 0;
  } else {
    critical(TRUE);
    if (fits_get_num_hdus(obj->fptr, &number, &status) != 0) {
      yfits_error(status);
    }
  }
  ypush_long(number); /* Yorick number of elements is a long */
}

void
Y_fitsio_get_hdu_num(int argc)
{
  yfits_object* obj;
  int number;

  if (argc != 1) y_error("expecting exactly one argument");
  obj = yfits_fetch(0, FALSE);
  if (obj->fptr == NULL) {
    number = 0;
  } else {
    critical(FALSE);
    fits_get_hdu_num(obj->fptr, &number);
  }
  ypush_long(number); /* Yorick number of elements is a long */
}

void
Y_fitsio_get_hdu_type(int argc)
{
  yfits_object* obj;
  int type;
  int status = 0;

  if (argc != 1) y_error("expecting exactly one argument");
  obj = yfits_fetch(0, TRUE);
  critical(TRUE);
  if (fits_get_hdu_type(obj->fptr, &type, &status) != 0) {
    yfits_error(status);
  }
  ypush_int(type);
}

void
Y_fitsio_copy_file(int argc)
{
  yfits_object* inp;
  yfits_object* out;
  int previous, current, following;
  int status = 0;

  if (argc != 5) y_error("expecting exactly 5 arguments");
  inp = yfits_fetch(argc - 1, TRUE);
  out = yfits_fetch(argc - 2, TRUE);
  previous = yarg_true(argc - 3);
  current = yarg_true(argc - 4);
  following = yarg_true(argc - 5);
  critical(TRUE);
  if (fits_copy_file(inp->fptr, out->fptr, previous, current,
                     following, &status) != 0) {
    yfits_error(status);
  }
  yarg_drop(3); /* left output object on top of stack */
}

void
Y_fitsio_copy_hdu(int argc)
{
  yfits_object* inp;
  yfits_object* out;
  int morekeys, status = 0;
  if (argc < 2 || argc > 3) y_error("expecting 2 or 3 arguments");
  inp = yfits_fetch(argc - 1, TRUE);
  out = yfits_fetch(argc - 2, TRUE);
  morekeys = (argc >= 3 ? fetch_int(argc - 3) : 0);
  critical(TRUE);
  if (fits_copy_hdu(inp->fptr, out->fptr, morekeys, &status) != 0) {
    yfits_error(status);
  }
  if (argc > 2) yarg_drop(argc - 2); /* left output object on top of stack */
}

/* missing: fits_write_hdu */

void
Y_fitsio_copy_header(int argc)
{
  yfits_object* inp;
  yfits_object* out;
  int status = 0;
  if (argc != 2) y_error("expecting exactly 2 arguments");
  inp = yfits_fetch(argc - 1, TRUE);
  out = yfits_fetch(argc - 2, TRUE);
  critical(TRUE);
  if (fits_copy_header(inp->fptr, out->fptr, &status) != 0) {
    yfits_error(status);
  }
}

void
Y_fitsio_delete_hdu(int argc)
{
  yfits_object* obj;
  int type, status = 0;
  if (argc != 1) y_error("expecting exactly one argument");
  obj = yfits_fetch(0, TRUE);
  critical(TRUE);
  if (fits_delete_hdu(obj->fptr, &type, &status) != 0) {
    yfits_error(status);
  }
  ypush_int(type);
}

void
Y_fitsio_get_hdrspace(int argc)
{
  yfits_object* obj;
  int numkeys, morekeys, status = 0;
  long dims[2];
  long* out;
  if (argc != 1) y_error("expecting exactly one argument");
  obj = yfits_fetch(0, TRUE);
  critical(TRUE);
  if (fits_get_hdrspace(obj->fptr, &numkeys, &morekeys, &status) != 0) {
    yfits_error(status);
  }
  dims[0] = 1;
  dims[1] = 2;
  out = ypush_l(dims);
  out[0] = numkeys;
  out[1] = morekeys;
}

void
Y_fitsio_read_keyword(int argc)
{
  yfits_object* obj;
  char* key;
  char* value;
  int len, status = 0;

  /* Fetch textual value. */
  if (argc != 2) y_error("expecting exactly 2 arguments");
  obj = yfits_fetch(argc - 1, TRUE);
  key = ygets_q(argc - 2);
  critical(TRUE);
  if (fits_read_keyword(obj->fptr, key, buffer, NULL, &status) == 0) {
    /* Trim leading and trailing spaces and push it on top of the stack. */
    value = buffer;
    while (value[0] == ' ') {
      ++value;
    }
    len = strlen(value);
    while (len > 1 && value[len-1] == ' ') {
      --len;
    }
    value[len] = '\0';
    push_string(value);
  } else if (status == KEY_NO_EXIST) {
    ypush_nil();
  } else if (status == VALUE_UNDEFINED) {
    push_string(NULL);
  } else {
    yfits_error(status);
  }
}

void
Y_fitsio_read_value(int argc)
{
  yfits_object* obj;
  char* key;
  char* value;
  char* end;
  int len, status = 0;
  double dval;
  long lval;
  double* z;

  if (argc < 2 || argc > 3) y_error("expecting 2 or 3 arguments");
  obj = yfits_fetch(argc - 1, TRUE);
  key = ygets_q(argc - 2);
  critical(TRUE);
  if (fits_read_keyword(obj->fptr, key, buffer, NULL, &status) != 0) {
    if (status == KEY_NO_EXIST) {
      /* Keyword not found, return default value if any. */
      if (argc < 3) ypush_nil();
    } else if (status == VALUE_UNDEFINED) {
      push_string(NULL);
    } else {
      yfits_error(status);
    }
    return;
  }

  /* Trim leading and trailing spaces. */
  value = buffer;
  while (value[0] == ' ') {
    ++value;
  }
  len = strlen(value);
  while (len > 1 && value[len-1] == ' ') {
    --len;
  }
  value[len] = '\0';

  /* Guess value type. */
  switch (value[0]) {
  case '\0':
    ypush_nil();
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
    /* String value (trim trailing spaces). */
    if (len < 2 || value[len-1] != '\'') break;
    if (fits_read_key(obj->fptr, TSTRING, key, value, NULL, &status) != 0) {
      yfits_error(status);
    }
    len = strlen(value);
    while (len > 1 && value[len-1] == ' ') {
      --len;
    }
    value[len] = '\0';
    push_string(value);
    return;
  default:
    /* Try to read a single integer. */
    lval = strtol(value, &end, 10);
    if (*end == '\0') {
      ypush_long(lval);
      return;
    }
    if (end != value) {
      /* Try to read a single real. */
      dval = strtod(value, &end);
      if (*end == '\0') {
        ypush_double(dval);
        return;
      }
      if (end != value) {
        /* Must be a complex (or an error). */
        z = ypush_z(NULL);
        z[0] = dval;
        value = end;
        z[1] = strtod(value, &end);
        if (*end == '\0') return;
      }
    }
  }
  y_error("invalid keyword value");
}

void
Y_fitsio_read_comment(int argc)
{
  yfits_object* obj;
  char* key;
  char *comment;
  char value[81];
  int len, status = 0;
  if (argc != 2) y_error("expecting exactly 2 arguments");
  obj = yfits_fetch(argc - 1, TRUE);
  key = ygets_q(argc - 2);
  critical(TRUE);
  if (fits_read_keyword(obj->fptr, key, value, buffer, &status) != 0) {
    if (status == KEY_NO_EXIST || status == VALUE_UNDEFINED) {
      /* Keyword not found or value undefined, return nothing. */
      ypush_nil();
      return;
    }
    yfits_error(status);
  }
  /* Trim leading and trailing spaces. */
  comment = buffer;
  while (comment[0] == ' ') {
    ++comment;
  }
  len = strlen(comment);
  while (len > 1 && comment[len-1] == ' ') {
    --len;
  }
  comment[len] = '\0';
  push_string(comment);
}

void
Y_fitsio_read_card(int argc)
{
  yfits_object* obj;
  char *key;
  int status = 0;
  if (argc != 2) y_error("expecting exactly 2 arguments");
  obj = yfits_fetch(argc - 1, TRUE);
  key = ygets_q(argc - 2);
  critical(TRUE);
  if (fits_read_card(obj->fptr, key, buffer, &status) != 0) {
    if (status == KEY_NO_EXIST || status == VALUE_UNDEFINED) {
      ypush_nil();
      return;
    }
    yfits_error(status);
  }
  push_string(buffer);
}

void
Y_fitsio_read_str(int argc)
{
  yfits_object* obj;
  char *str;
  int status = 0;
  if (argc != 2) y_error("expecting exactly 2 arguments");
  obj = yfits_fetch(argc - 1, TRUE);
  str = ygets_q(argc - 2);
  critical(TRUE);
  if (fits_read_str(obj->fptr, str, buffer, &status) != 0) {
    if (status == KEY_NO_EXIST || status == VALUE_UNDEFINED) {
      ypush_nil();
      return;
    }
    yfits_error(status);
  }
  push_string(buffer);
}

static void
read_record(int argc, int split)
{
  yfits_object* obj;
  int keynum, status;

  if (argc != 2) y_error("expecting exactly 2 arguments");
  obj = yfits_fetch(argc - 1, TRUE);
  keynum = fetch_int(argc - 2);
  if (keynum < 0) y_error("invalid keyword umber");

  status = 0;
  critical(TRUE);
  if (split) {
    char** out;
    char keyword[FLEN_KEYWORD+1];
    char value[FLEN_VALUE+1];
    char comment[FLEN_COMMENT+1];
    long dims[2];
    if (fits_read_keyn(obj->fptr, keynum, keyword, value, comment,
                     &status) == 0) {
      if (keynum == 0) {
        ypush_nil();
      } else {
        dims[0] = 1;
        dims[1] = 3;
        out = ypush_q(dims);
        out[0] = p_strcpy(keyword);
        out[1] = p_strcpy(value);
        out[2] = p_strcpy(comment);
      }
    }
  } else {
    if (fits_read_record(obj->fptr, keynum, buffer, &status) == 0) {
      if (keynum == 0) {
        ypush_nil();
      } else {
        push_string(buffer);
      }
    }
  }
  if (status != 0) {
    if (status != KEY_NO_EXIST && status != VALUE_UNDEFINED) {
      yfits_error(status);
    }
    ypush_nil();
  }
}

void
Y_fitsio_read_record(int argc)
{
  read_record(argc, 0);
}

void
Y_fitsio_read_keyn(int argc)
{
  read_record(argc, 1);
}

void
Y_fitsio_read_key_unit(int argc)
{
  yfits_object* obj;
  char *key;
  int status = 0;
  if (argc != 2) y_error("expecting exactly 2 arguments");
  obj = yfits_fetch(argc - 1, TRUE);
  key = ygets_q(argc - 2);
  critical(TRUE);
  if (fits_read_key_unit(obj->fptr, key, buffer, &status) == 0) {
    push_string(buffer[0] == '\0' ? NULL : buffer);
  } else if (status == KEY_NO_EXIST) {
    ypush_nil();
  } else if (status == VALUE_UNDEFINED) {
    push_string(NULL);
  } else {
    yfits_error(status);
  }
}

void
Y_fitsio_get_img_type(int argc)
{
  yfits_object* obj;
  int bitpix, status = 0;
  if (argc != 1) y_error("expecting exactly one argument");
  obj = yfits_fetch(0, TRUE);
  critical(TRUE);
  if (fits_get_img_type(obj->fptr, &bitpix, &status) != 0) {
    yfits_error(status);
  }
  ypush_int(bitpix);
}

void
Y_fitsio_get_img_equivtype(int argc)
{
  yfits_object* obj;
  int bitpix, status = 0;
  if (argc != 1) y_error("expecting exactly one argument");
  obj = yfits_fetch(0, TRUE);
  critical(TRUE);
  if (fits_get_img_equivtype(obj->fptr, &bitpix, &status) != 0) {
    yfits_error(status);
  }
  ypush_int(bitpix);
}

void
Y_fitsio_get_img_dim(int argc)
{
  yfits_object* obj;
  int naxis, status = 0;
  if (argc != 1) y_error("expecting exactly one argument");
  obj = yfits_fetch(0, TRUE);
  critical(TRUE);
  if (fits_get_img_dim(obj->fptr, &naxis, &status) != 0) {
    yfits_error(status);
  }
  ypush_int(naxis);
}

void
Y_fitsio_get_img_size(int argc)
{
  yfits_object* obj;
  long dims[2];
  int naxis, status = 0;
  if (argc != 1) y_error("expecting exactly one argument");
  obj = yfits_fetch(0, TRUE);
  critical(TRUE);
  if (fits_get_img_dim(obj->fptr, &naxis, &status) != 0) {
    yfits_error(status);
  }
  dims[0] = 1;
  dims[1] = naxis;
  if (fits_get_img_size(obj->fptr, naxis, ypush_l(dims), &status) != 0) {
    yfits_error(status);
  }
}

void
Y_fitsio_read_array(int argc)
{
  scalar_t null;
  yfits_object* fh;
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

  /* Parse arguments. */
  null_index = -1;
  first_iarg = -1;
  last_iarg = -1;
  incr_iarg = -1;
  number_iarg = -1;
  mode = 0;
  fh = NULL;
  for (iarg = argc - 1; iarg >= 0; --iarg) {
    long index = yarg_key(iarg);
    if (index < 0) {
      /* Positional argument. */
      if (fh == NULL) {
        fh = yfits_fetch(iarg, TRUE);
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
      } else if (index == index_of_null) {
        null_index = yget_ref(iarg);
        fprintf(stderr, "null_index = %ld (iarg = %d)\n", null_index, iarg);
        if (null_index < 0) {
          y_error("keyword NULL must be set with a simple variable");
        }
      } else {
        y_error("unsupported keyword");
      }
    }
  }
  if (fh == NULL) {
    y_error("too few arguments");
  }

  /* Get array dimensions and type. */
  critical(TRUE);
  status = 0;
  if (get_image_param(fh, Y_DIMSIZE - 1, NULL,
                      &naxis, &dims[1], &ntot, &status) != 0) {
    yfits_error(status);
  }
  if (naxis < 0) y_error("bad number of dimensions");
  if (fits_get_img_equivtype(fh->fptr, &bitpix, &status)) {
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
    first = number = 0; /* avoid warnings aboutn uninitialized variables */
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
  if (fits_get_img_equivtype(fh->fptr, &bitpix, &status)) {
    yfits_error(status);
  }
  arr = NULL;
  datatype = -1;
  if (bitpix > 0) {
    if (bitpix == 8*sizeof(unsigned char)) {
      datatype = TBYTE;
      null.type = Y_CHAR;
      arr = ypush_c(dims);
    } else if (bitpix == 8*sizeof(short)) {
      datatype = TSHORT;
      null.type = Y_SHORT;
      arr = ypush_s(dims);
    } else if (bitpix == 8*sizeof(int)) {
      datatype = TINT;
      null.type = Y_INT;
      arr = ypush_i(dims);
    } else if (bitpix == 8*sizeof(long)) {
      datatype = (sizeof(long long) == sizeof(long) ? TLONGLONG : TLONG);
      null.type = Y_LONG;
      arr = ypush_l(dims);
    }
  } else {
    if (bitpix == -8*sizeof(float)) {
      datatype = TFLOAT;
      null.type = Y_FLOAT;
      arr = ypush_f(dims);
    } else if (bitpix == -8*sizeof(double)) {
      datatype = TDOUBLE;
      null.type = Y_DOUBLE;
      arr = ypush_d(dims);
    }
  }
  if (arr == NULL) y_error("unsupported data type");

  /* Read the data. */
  if (mode == 0 || mode == 9) {
    fits_read_img(fh->fptr, datatype, first, number,
                  &null.value, arr, &anynull, &status);
  } else {
    fits_read_subset(fh->fptr, datatype, fpix, lpix, ipix,
                     &null.value, arr, &anynull, &status);
  }
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

void
Y_fitsio_create_img(int argc)
{
  yfits_object* obj;
  long dims[MAXDIMS + 1];
  int bitpix, status = 0;
  if (argc < 2) y_error("not enough arguments");
  obj = yfits_fetch(argc - 1, TRUE);
  bitpix = fetch_int(argc - 2);
  get_dimlist(argc - 3, 0, dims, MAXDIMS);
  critical(TRUE);
  if (fits_create_img(obj->fptr, bitpix, dims[0], &dims[1], &status) != 0) {
    yfits_error(status);
  }
  yarg_drop(argc - 1);
}

void
Y_fitsio_copy_cell2image(int argc)
{
  yfits_object* inp;
  yfits_object* out;
  char* colname;
  long rownum;
  int status = 0;

  if (argc != 5) y_error("expecting exactly 4 arguments");
  inp = yfits_fetch(argc - 1, TRUE);
  out = yfits_fetch(argc - 2, TRUE);
  colname = ygets_q(argc - 3);
  rownum = ygets_l(argc - 4);
  if (colname == NULL || colname[0] == '\0') {
    y_error("invalid column name");
  }
  critical(TRUE);
  if (fits_copy_cell2image(inp->fptr, out->fptr, colname,
                           rownum, &status) != 0) {
    yfits_error(status);
  }
  yarg_drop(2); /* left output object on top of stack */
}

void
Y_fitsio_write_array(int argc)
{
  yfits_object* fh;
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
  fh = NULL;
  src = NULL;
  for (iarg = argc - 1; iarg >= 0; --iarg) {
    long index = yarg_key(iarg);
    if (index < 0) {
      /* Positional argument. */
      if (fh == NULL) {
        fh = yfits_fetch(iarg, TRUE);
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
    if (yarg_rank(null_iarg) != 0) {
      y_error("null value must be a scalar");
    }
    if (yarg_typeid(null_iarg) != type) {
      y_error("null value must be of same type as the data");
    }
    null = ygeta_any(null_iarg, NULL, NULL, NULL);
  }

  /* Convert Yorick type to CFITSIO type. */
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
  default:
    y_error("unsupported array type");
  }

  /* Get FITS array dimensions and type. */
  critical(TRUE);
  status = 0;
  if (get_image_param(fh, Y_DIMSIZE - 1, NULL,
                      &naxis, &dst_dims[1], &dst_number, &status) != 0) {
    yfits_error(status);
  }
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
    fits_write_subset(fh->fptr, type, fpix, lpix, src, &status);
  } else if (null != NULL) {
    fits_write_imgnull(fh->fptr, type, first, src_number, src, null, &status);
  } else {
    fits_write_img(fh->fptr, type, first, src_number, src, &status);
  }
  if (status != 0) {
    yfits_error(status);
  }
  ypush_nil();
}

void
Y_fitsio_copy_image2cell(int argc)
{
  yfits_object* inp;
  yfits_object* out;
  char* colname;
  long rownum, longval;
  int flag, status = 0;

  if (argc != 5) y_error("expecting exactly 5 arguments");
  inp = yfits_fetch(argc - 1, TRUE);
  out = yfits_fetch(argc - 2, TRUE);
  colname = ygets_q(argc - 3);
  rownum  = ygets_l(argc - 4);
  longval = ygets_l(argc - 5);
  if (longval < 0 || longval > 2) y_error("bad value for COPYKEYFLAG");
  flag = (int)longval;
  if (colname == NULL || colname[0] == '\0') {
    y_error("invalid column name");
  }
  critical(TRUE);
  if (fits_copy_image2cell(inp->fptr, out->fptr, colname,
                           rownum, flag, &status) != 0) {
    yfits_error(status);
  }
  yarg_drop(3); /* left output object on top of stack */
}

void
Y_fitsio_copy_image_section(int argc)
{
  yfits_object* inp;
  yfits_object* out;
  char* section;
  int status = 0;
  if (argc != 3) y_error("expecting exactly 3 arguments");
  inp = yfits_fetch(2, TRUE);
  out = yfits_fetch(1, TRUE);
  section = ygets_q(0);
  critical(TRUE);
  if (fits_copy_image_section(inp->fptr, out->fptr, section, &status) != 0) {
    yfits_error(status);
  }
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
  yfits_object* fh;
  long nrows, ntot;
  long dims[Y_DIMSIZE];
  char** ttype;
  char** tform;
  char** tunit;
  char* extname;
  int status, iarg, tbltype, tfields;

  tbltype = BINARY_TBL;
  fh = NULL;
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
      if (fh == NULL) {
        fh = yfits_fetch(iarg, TRUE);
      } else if (ttype == NULL) {
        ttype = ygeta_q(iarg, &ntot, dims);
        if (dims[0] < 1) y_error("too many dimensions for argument TTYPE");
        check_ncols(&tfields, ntot);
      } else if (tform == NULL) {
        tform = ygeta_q(iarg, &ntot, dims);
        if (dims[0] < 1) y_error("too many dimensions for argument TFORM");
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
        if (dims[0] < 1) y_error("too many dimensions for argument TUNIT");
        check_ncols(&tfields, ntot);
      } else if (index == index_of_ascii) {
        tbltype = (yarg_true(iarg) ? ASCII_TBL : BINARY_TBL);
        check_ncols(&tfields, ntot);
      } else {
        y_error("unsupported keyword");
      }
    }
  }
  if (tform == NULL) y_error("too fex arguments");

  status = 0;
  critical(TRUE);
  if (fits_create_tbl(fh->fptr, tbltype, nrows, tfields, ttype,
                      tform,  tunit, extname, &status) != 0) {
    yfits_error(status);
  }
  ypush_nil();
}

void
Y_fitsio_get_num_rows(int argc)
{
  long nrows;
  int status;

  if (argc != 1) y_error("expecting exactly one argument");
  status = 0;
  critical(TRUE);
  if (fits_get_num_rows(yfits_fetch(0, TRUE)->fptr, &nrows, &status) != 0) {
    yfits_error(status);
  }
  ypush_long(nrows);
}

void
Y_fitsio_get_num_cols(int argc)
{
  int ncols;
  int status;

  if (argc != 1) y_error("expecting exactly one argument");
  status = 0;
  critical(TRUE);
  if (fits_get_num_cols(yfits_fetch(0, TRUE)->fptr, &ncols, &status) != 0) {
    yfits_error(status);
  }
  ypush_long(ncols);
}

void
Y_fitsio_get_colnum(int argc)
{
  yfits_object* fh;
  long  dims[2];
  char* template;
  long* result;
  int   iarg, status, casesen, colnum, ncols, col;

  fh = NULL;
  template = NULL;
  casesen = CASEINSEN;
  for (iarg = argc - 1; iarg >= 0; --iarg) {
    long index = yarg_key(iarg);
    if (index < 0) {
      /* Positional argument. */
      if (fh == NULL) {
        fh = yfits_fetch(iarg, TRUE);
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

  critical(TRUE);
  status = 0;
  fits_get_colnum(fh->fptr, casesen, template, &colnum, &status);
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
    fits_get_colnum(fh->fptr, casesen, template, &colnum, &status);
  }
  if (status != COL_NOT_FOUND) {
    yfits_error(status);
  }
  dims[0] = 1;
  dims[1] = ncols;
  result = ypush_l(dims);
  status = 0;
  for (col = 0; col < ncols; ++col) {
    fits_get_colnum(fh->fptr, casesen, template, &colnum, &status);
    if (status != COL_NOT_UNIQUE) {
      yfits_error(status);
    }
    result[col] = colnum;
  }
}

void
Y_fitsio_get_colname(int argc)
{
  yfits_object* fh;
  long   dims[2];
  char   colname[80];
  char*  template;
  char** result;
  int    iarg, status, casesen, colnum, ncols, col;

  fh = NULL;
  template = NULL;
  casesen = CASEINSEN;
  for (iarg = argc - 1; iarg >= 0; --iarg) {
    long index = yarg_key(iarg);
    if (index < 0) {
      /* Positional argument. */
      if (fh == NULL) {
        fh = yfits_fetch(iarg, TRUE);
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

  critical(TRUE);
  status = 0;
  fits_get_colname(fh->fptr, casesen, template, colname, &colnum, &status);
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
    fits_get_colname(fh->fptr, casesen, template, colname, &colnum, &status);
  }
  if (status != COL_NOT_FOUND) {
    yfits_error(status);
  }
  dims[0] = 1;
  dims[1] = ncols;
  result = ypush_q(dims);
  status = 0;
  for (col = 0; col < ncols; ++col) {
    fits_get_colname(fh->fptr, casesen, template, colname, &colnum, &status);
    if (status != COL_NOT_UNIQUE) {
      yfits_error(status);
    }
    result[col] = p_strcpy(colname);
  }
}

static void
get_coltype(int argc, int eqcoltype)
{
  yfits_object* fh;
  long  dims[2];
  long* result;
  long  repeat, width;
  int   status, type, colnum;

  if (argc != 2) y_error("expecting exactly 2 arguments");
  fh = yfits_fetch(1, TRUE);
  colnum = ygets_i(0);

  critical(TRUE);
  status = 0;
  if (eqcoltype) {
    fits_get_eqcoltype(fh->fptr, colnum, &type, &repeat, &width, &status);
  } else {
    fits_get_coltype(fh->fptr, colnum, &type, &repeat, &width, &status);
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

void
Y_fitsio_read_tdim(int argc)
{
  yfits_object* fh;
  long  dims[2];
  long  naxes[Y_DIMSIZE - 1];
  long* result;
  int   status, colnum, k, naxis;

  if (argc != 2) y_error("expecting exactly 2 arguments");
  fh = yfits_fetch(1, TRUE);
  colnum = ygets_i(0);

  critical(TRUE);
  status = 0;
  if (fits_read_tdim(fh->fptr, colnum,
                     Y_DIMSIZE - 1, &naxis, naxes, &status) != 0) {
    yfits_error(status);
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
Y_fitsio_decode_tdim(int argc)
{
  yfits_object* fh;
  long  dims[2];
  long  naxes[Y_DIMSIZE - 1];
  long* result;
  char* tdimstr;
  int   status, colnum, k, naxis;

  if (argc != 3) y_error("expecting exactly 3 arguments");
  fh = yfits_fetch(2, TRUE);
  tdimstr = ygets_q(1);
  colnum = ygets_i(0);

  critical(TRUE);
  status = 0;
  if (fits_decode_tdim(fh->fptr, tdimstr, colnum,
                       Y_DIMSIZE - 1, &naxis, naxes, &status) != 0) {
    yfits_error(status);
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
Y_fitsio_write_tdim(int argc)
{
  yfits_object* fh;
  long dims[Y_DIMSIZE];
  int  status, colnum;

  if (argc < 2) y_error("expecting at least 2 arguments");
  fh = yfits_fetch(argc - 1, TRUE);
  colnum = ygets_i(argc - 2);
  get_dimlist(argc - 3, 0, dims, Y_DIMSIZE - 1);

  critical(TRUE);
  status = 0;
  if (fits_write_tdim(fh->fptr, colnum, dims[0], &dims[1], &status) != 0) {
    yfits_error(status);
  }
  ypush_nil();
}

/*---------------------------------------------------------------------------*/
/* MISCELLANEOUS */

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
Y_fitsio_init(int argc)
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
yfits_fetch(int iarg, int assert_open)
{
  yfits_object* obj = (yfits_object*)yget_obj(iarg, &yfits_type);
  if (assert_open && obj->fptr == NULL) y_error("FITS file has been closed");
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
get_image_param(yfits_object* fh, int maxdims, int* bitpix_ptr,
                int* naxis_ptr, long dims[], long* number_ptr,
                int* status)
{
  int k, naxis, bitpix;

  if (fits_get_img_param(fh->fptr, maxdims, &bitpix, &naxis,
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
  return *status;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 2
 * tab-width: 8
 * indent-tabs-mode: nil
 * fill-column: 79
 * coding: utf-8
 * ispell-local-dictionary: "american"
 * End:
 */
