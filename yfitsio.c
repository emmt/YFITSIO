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

PLUG_API void y_error(const char *) __attribute__ ((noreturn));

static void push_string(const char* str);
static char* fetch_path(int iarg);
static int fetch_int(int iarg);
static void critical(int clear_errmsg);

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

static long index_extended = -1L;

static void
initialize_indexes()
{
#define INIT(s) if (index_##s == -1L) index_##s = yget_global(#s, 0)
  INIT(extended);
#undef INIT
}

/* Open an existing data file. */
void
Y_fitsio_open(int argc)
{
  yfits_object* obj;
  char* path = NULL;
  char* mode = NULL;
  int extended = FALSE; /* use extended file name syntax? */
  int status = 0;
  int iomode = 0;
  int iarg;

  if (index_extended == -1L) initialize_indexes();

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
      if (index == index_extended) {
        extended = yarg_true(iarg);
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
  if (extended) {
    fits_open_file(&obj->fptr, path, iomode, &status);
  } else {
    fits_open_diskfile(&obj->fptr, path, iomode, &status);
  }
  if (status) yfits_error(status);
}

/* Create and open a new empty output FITS file. */
void
Y_fitsio_create(int argc)
{
  yfits_object* obj;
  char* path = NULL;
  int extended = FALSE; /* use extended file name syntax? */
  int status = 0;
  int iarg;

  if (index_extended == -1L) initialize_indexes();

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
      if (index == index_extended) {
        extended = yarg_true(iarg);
      } else {
        y_error("unsupported keyword");
      }
    }
  }
  if (path == NULL) y_error("too few arguments");

  obj = yfits_push();
  critical(TRUE);
  if (extended) {
    fits_create_file(&obj->fptr, path, &status);
  } else {
    fits_create_diskfile(&obj->fptr, path, &status);
  }
  if (status != 0) yfits_error(status);
}

/* Close a FITS file. */
void
Y_fitsio_close(int argc)
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
Y_fitsio_delete(int argc)
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
  if (fits_read_keyword(obj->fptr, key, buffer, NULL, &status) != 0) {
    if (status == KEY_NO_EXIST) {
      ypush_nil();
      return;
    }
    if (status == VALUE_UNDEFINED) {
      fprintf(stderr, "undefined value for \"%s\"\n", key);
      push_string(NULL);
      return;
    }
    yfits_error(status);
  }

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
      return;
    }
    if (status == VALUE_UNDEFINED) {
      fprintf(stderr, "undefined value for \"%s\"\n", key);
      push_string(NULL);
      return;
    }
    yfits_error(status);
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

void
Y_fitsio_read_record(int argc)
{
  yfits_object* obj;
  int num, status = 0;
  if (argc != 2) y_error("expecting exactly 2 arguments");
  obj = yfits_fetch(argc - 1, TRUE);
  num = fetch_int(argc - 2);
  critical(TRUE);
  if (fits_read_record(obj->fptr, num, buffer, &status) != 0) {
    if (status == KEY_NO_EXIST || status == VALUE_UNDEFINED) {
      ypush_nil();
      return;
    }
    yfits_error(status);
  }
  push_string(buffer);
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
Y_fitsio_init(int argc)
{
  /* Define constants. */
#define DEFINE_INT_CONST(c)    define_int_const("FITSIO_"#c, c)
  DEFINE_INT_CONST(IMAGE_HDU);
  DEFINE_INT_CONST(ASCII_TBL);
  DEFINE_INT_CONST(BINARY_TBL);
  DEFINE_INT_CONST(ANY_HDU);
#undef DEFINE_INT_CONST
  ypush_nil();
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
