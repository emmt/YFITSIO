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


static void
yfits_error(int status)
{
  char reason[31];
  fits_report_error(stderr, status);
  fits_get_errstatus(status, reason);
  y_error(reason);
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

static void
yfits_print(void* ptr)
{
  yfits_object* obj = (yfits_object*)ptr;
  fitsfile *fptr = obj->fptr;
  int number, status = 0;
  char buf[100];

  critical(TRUE);
  if (fptr != NULL) {
    fits_get_num_hdus(fptr, &number, &status);
  } else {
    number = 0;
  }
  sprintf(buf, "%s with %d HDU", yfits_type.type_name, number);
  y_print(buf, TRUE);
  if (number >= 1) {
    int hdu0, hdu, type;
    fits_get_hdu_num(fptr, &hdu0);
    for (hdu = 1; hdu <= number; ++hdu) {
      if (fits_movabs_hdu(fptr, hdu, &type, &status) != 0) {
        fits_report_error(stderr, status);
        status = 0;
        break;
      }
      sprintf(buf, "  HDU[%d] = %s", hdu, hdu_type_name(type));
      y_print(buf, TRUE);
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
  ypush_int(yfits_fetch(0, FALSE)->fptr != NULL ? 1 : 0);
}

void
Y_fitsio_is_handle(int argc)
{
  if (argc != 1) y_error("expecting exactly one argument");
  ypush_int((char*)yget_obj(0, NULL) == yfits_type.type_name ? 1 : 0);
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
    yfits_error(status);
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
    yfits_error(status);
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
  critical(TRUE);
  if (fits_movnam_hdu(obj->fptr, type, extname, extver, &status) != 0) {
    yfits_error(status);
  }
  yarg_drop(argc - 1);
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
