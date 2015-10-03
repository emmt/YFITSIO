/*
 * fitsio.i --
 *
 * Yorick interface to CFITSIO library.
 *
 *-----------------------------------------------------------------------------
 *
 * Copyright (C) 2015: Éric Thiébaut <eric.thiebaut@univ-lyon1.fr>
 *
 * See LICENSE.md for details.
 *
 */

if (is_func(plug_in)) plug_in, "yfitsio";

extern fitsio_open_file;
extern fitsio_open_data;
extern fitsio_open_table;
extern fitsio_open_image;
/* DOCUMENT fh = fitsio_open_file(path[, mode][, basic=0/1]);
         or fh = fitsio_open_data(path[, mode]);
         or fh = fitsio_open_table(path[, mode]);
         or fh = fitsio_open_image(path[, mode]);

      Open an  existing FITS  file.  Argument  PATH is the  name of  the file.
      Optional argument  MODE can be "r"  for reading or "rw"  for reading and
      writing.  The returned value FH is a handle to access the file contents.

      The first routine opens the file.   The other routines open the file and
      moves to the first HDU containing  significant data, a table or an image
      (respectively).  The extend file  syntax (see CFITSIO documentation) can
      be  exploited  to  move  to  a  specific  HDU  or  extension.   For  the
      fitsio_open_file routine, keyword  BASIC can be set true to  not use the
      extended file name syntax to interpret PATH.

      The opened file is automatically closed  when FH is no longer referenced
      (thus there is no needs to use fitsio_close_file).


   SEE ALSO: fitsio_close_file, fitsio_create_file.
 */

extern fitsio_create_file;
/* DOCUMENT fh = fitsio_create_file(path);

      Create and open a new empty output FITS file.  Argument PATH is the name
      of the  file.  An error will  be returned if the  specified file already
      exists, unless the file name is  prefixed with an exclamation point (!).
      The returned value FH is a handle to access the file contents.

      The opened file is automatically closed  when FH is no longer referenced
      (thus there is no needs to use fitsio_close_file).

      Keyword BASIC can be  set true to not use the  extended file name syntax
      to interpret PATH (see CFITSIO documentation).

   SEE ALSO: fitsio_close_file, fitsio_open_file.
 */

extern fitsio_close_file;
extern fitsio_delete_file;
/* DOCUMENT fitsio_close_file, fh;
         or fitsio_delete_file, fh;

     Close a previously opened FITS file.  The first routine simply closes the
     file, whereas the  second one also deletes the file,  which can be useful
     in cases where a FITS file has  been partially created, but then an error
     occurs which prevents it from being completed.

   SEE ALSO: fitsio_open_file, fitsio_create_file.
 */

extern fitsio_is_open;
extern fitsio_is_handle;
/* DOCUMENT fitsio_is_open(fh);
         or fitsio_is_handle(obj);

     The first  function checks whether FITS  handle FH is associated  with an
     open  file.  The  second function  checks whether  object OBJ  is a  FITS
     handle.

   SEE ALSO: fitsio_open_file, fitsio_create_file, fitsio_close_file.
 */

extern fitsio_file_name;
extern fitsio_file_mode;
extern fitsio_url_type;
/* DOCUMENT fitsio_file_name(fh);
         or fitsio_file_mode(fh);
         or fitsio_url_type(fh);

     The `fitsio_file_name` function returns the file name for FITS handle FH.

     The `fitsio_file_mode` function  returns the file mode of  FITS handle FH,
     which is "r" for read-only and "rw" for read-write.

     The `fitsio_url_type`  function returns the  URL type for FITS  handle FH.
     For instance, "file://" or "ftp://".

     All these functions return a null string, string(0), if the file has been
     closed.

   SEE ALSO: fitsio_open_file, fitsio_is_open.
 */

extern fitsio_movabs_hdu;
extern fitsio_movrel_hdu;
extern fitsio_movnam_hdu;
/* DOCUMENT hdutype = fitsio_movabs_hdu(fh, num);
         or hdutype = fitsio_movrel_hdu(fh, off);
         or hdutype = fitsio_movnam_hdu(fh, hdutype, extname);
         or hdutype = fitsio_movnam_hdu(fh, hdutype, extname, extver);

     Move  to a  different HDU  in the  file.  The  first routine  moves to  a
     specified absolute HDU number (starting with  1 for the primary array) in
     the  FITS file,  and  the second  routine moves  a  relative number  HDUs
     forward or backward from the current HDU.  The third routine moves to the
     (first) HDU which has the specified extension type and EXTNAME and EXTVER
     keyword values (or  HDUNAME and HDUVER keywords).   The HDUTYPE parameter
     may have a value of FITSIO_IMAGE, FITSIO_ASCII_TBL, FITSIO_BINARY_TBL, or
     FITSIO_ANY_HDU  where  FITSIO_ANY_HDU means  that  only  the EXTNAME  and
     EXTVER  values will  be used  to locate  the correct  extension.  If  the
     argument EXTVER  is omitted or 0  then the EXTVER keyword  is ignored and
     the first HDU with a matching EXTNAME (or HDUNAME) keyword will be found.

     Upon  success, the  returned value  is the  type of  the new  current HDU
     (FITSIO_IMAGE, FITSIO_ASCII_TBL,  or FITSIO_BINARY_TBL).  If  no matching
     HDU is found in  the file then the current HDU  will remain unchanged and
     -1  is returned  or an  error  is thrown  if  the function  is called  as
     a subroutine.

   SEE ALSO: fitsio_open_file, fitsio_get_hdu_type.
 */

extern fitsio_get_num_hdus;
/* DOCUMENT fitsio_get_num_hdus(fh);

     Return  the total  number of  HDUs in  the FITS  file.  This  returns the
     number of  completely defined HDUs  in the file.  If  a new HDU  has just
     been added to the  FITS file, then that last HDU will  only be counted if
     it has been closed, or if data  has been written to the HDU.  The current
     HDU remains unchanged by this routine.

   SEE ALSO: fitsio_open_file, fitsio_get_hdu_num, fitsio_get_hdu_type.
 */

extern fitsio_get_hdu_num;
/* DOCUMENT fitsio_get_hdu_nums(fh);

     Return the number of  the current HDU (CHDU) in the  FITS file (where the
     primary array = 1).

   SEE ALSO: fitsio_open_file, fitsio_get_num_hdus, fitsio_get_hdu_type.
 */

local FITSIO_IMAGE_HDU, FITSIO_ASCII_TBL, FITSIO_BINARY_TBL, FITSIO_ANY_HDU;
extern fitsio_get_hdu_type;
/* DOCUMENT fitsio_get_hdu_type(fh);

     Return the type of the current HDU  in the FITS file. The possible values
     for   the  returned   value  are:   FITSIO_IMAGE,  FITSIO_ASCII_TBL,   or
     FITSIO_BINARY_TBL.

   SEE ALSO:fitsio_open_file, fitsio_get_hdu_num, fitsio_get_num_hdus.
 */

extern fitsio_copy_file;
extern fitsio_copy_hdu;
/* FIXME: extern fits_write_hdu; */
extern fitsio_copy_header;
extern fitsio_delete_hdu;
/* DOCUMENT fitsio_copy_file(inp, out, previous, current, following);
         or fitsio_copy_hdu(inp, out);
         or fitsio_copy_hdu(inp, out, morekeys);
         or fitsio_copy_header(inp, out);
         or fitsio_delete_hdu(fh);

      The function  `fitsio_copy_file` copies all or  part of the HDUs  in the
      FITS file  associated with INP  and append them to  the end of  the FITS
      file associated with OUT.  If PREVIOUS  is true, then any HDUs preceding
      the current  HDU in the  input file will be  copied to the  output file.
      Similarly,  CURRENT and  FOLLOWING  determine whether  the current  HDU,
      and/or any following HDUs in the input file will be copied to the output
      file.  Thus,  if all 3 parameters  are true, then the  entire input file
      will be  copied.  On  exit, the current  HDU in the  input file  will be
      unchanged, and the last HDU in the output file will be the current HDU.

      The  function `fitsio_hdu`  copies the  current HDU  from the  FITS file
      associated with INP and append it to the end of the FITS file associated
      with  OUT.   Space may  be  reserved  for MOREKEYS  additional  keywords
      (default value is 0 if not specified) in the output header.

      The function `fitsio_copy_header`  copies the header (and  not the data)
      from the current  HDU of INP to  the current HDU of OUT.  If the current
      output HDU is not completely empty,  then the cuurent HDU will be closed
      and a new HDU will be appended to the output file.  An empty output data
      unit will be created with all values initially = 0.

      The 3 first functions return the output object OUT.

      The function `fitsio_delete_hdu` deletes the  current HDU of FITS handle
      FH and returns the type of the new current HDU.  Any following HDUs will
      be  shifted forward  in the  file, to  fill in  the gap  created by  the
      deleted HDU.  In  the case of deleting the primary  array (the first HDU
      in the file) then  the current primary array will be  replaced by a null
      primary array  containing the  minimum set of  required keywords  and no
      data.  If there  are more extensions in the file  following the one that
      is  deleted, then  the current  HDU will  be redefined  to point  to the
      following  extension.  If  there are  no following  extensions then  the
      current HDU will be redefined to point to the previous HDU.

   SEE ALSO: fitsio_open_file.
 */

extern fitsio_get_hdrspace;
/* DOCUMENT [numkeys, morekeys] = fitsio_get_hdrspace(fh);

     Return  the number  NUMKEYS of  existing keywords  (not counting  the END
     keyword) and  the amount MOREKEYS  of space currently available  for more
     keywords.   It returns  MOREKEYS =  -1  if the  header has  not yet  been
     closed.  Note  that CFITSIO will  dynamically add space if  required when
     writing new keywords to a header so  in practice there is no limit to the
     number of keywords that can be added to a header.

   SEE ALSO: fitsio_open_file.
 */

extern fitsio_read_keyword;
extern fitsio_read_value;
extern fitsio_read_comment;
extern fitsio_read_card;
extern fitsio_read_str;
extern fitsio_read_record;
extern fitsio_read_keyn;
extern fitsio_read_key_unit;
/* DOCUMENT fitsio_read_keyword(fh, key);
         or fitsio_read_value(fh, key);
         or fitsio_read_value(fh, key, def);
         or fitsio_read_comment(fh, key);
         or fitsio_read_card(fh, key);
         or fitsio_read_str(fh, key);
         or fitsio_read_record(fh, keynum);
         or fitsio_read_keyn(fh, keynum);
         or fitsio_read_key_unit(fh, key);

     These functions read or write keywords  in the Current Header Unit (CHU).
     Wild card characters  ('*', '?', or '#') may be  used when specifying the
     name of the keyword to be read:  a '?' will match any single character at
     that  position in  the  keyword name  and  a '*'  will  match any  length
     (including zero) string of characters.   The '#' character will match any
     consecutive string of decimal digits (0-9).  When a wild card is used the
     routine will only search for a  match from the current header position to
     the end of the header and will not  resume the search from the top of the
     header back to the original header  position as is done when no wildcards
     are included in the keyword  name.  The `fits_read_record` routine may be
     used to set the starting position when doing wild card searches.

     The function `fitsio_read_keyword` returns the  textual value of the FITS
     keyword KEY in  current header unit of  FITS handle FH.  If  the value is
     undefined, a  null string is returned.   If KEY is not  found, nothing is
     returned.  Leading and trailing spaces are stripped from the result.

     The function  `fitsio_read_value` returns the  value of the  FITS keyword
     KEY in current header unit of FITS handle FH.  If the value is undefined,
     nothing  is returned.   If KEY  is not  found, the  default value  DEF is
     returned if  it is provided; otherwise,  a null string is  returned.  The
     type of the returned value depends on that of the FITS card:

        ---------------------------------------------------------
        Result        Type of FITS card
        ---------------------------------------------------------
        int (0 or 1)  logical value
        long          integer value
        double        real value
        complex       complex value
        string        string value -- never string(0)
        string(0)     undefined value
        [] (nothing)  keyword not found -- unless DEF is provided
        ---------------------------------------------------------

     Remarks: (i)  String values  are returned  with trailing  spaces stripped
     (according to  FITS standard they  are unsignificant).  (ii) There  is no
     distinction between integer and real  complex values (if that matters for
     you, you  can use  `fitsio_read_keyword` and  parse the  value yourself).
     (iii) There is  no checking nor conversion done on  the default value DEF
     if it is provided.

     The  functions  `fitsio_read_record`  and `fitsio_read_keyn`  return  the
     KEYNUM-th header record  in the CHU.  The first keyword  in the header is
     at  KEYNUM =  1; if  KEYNUM =  0, then  these routines  simply reset  the
     internal  CFITSIO  pointer  to  the  beginning  of  the  header  so  that
     subsequent keyword operations will start at  the top of the header (e.g.,
     prior to  searching for keywords using  wild cards in the  keyword name).
     If KEYNUM  = 0,  then nothing  is returned;  otherwise the  first routine
     returns  the  entire 80-character  header  record  (with trailing  blanks
     truncated),  while the  second routine  parses the  record and  returns a
     vector  of 3  strings: the  keyword, the  value, and  the comment  fields
     (blank truncated).

     The  function `fitsio_read_key_unit`  returns the  physical units  string
     from an existing keyword.  This routine uses a local convention, in which
     the keyword units are enclosed in square brackets in the beginning of the
     keyword comment field.  A null string is returned if no units are defined
     for the keyword.

   SEE ALSO: fitsio_open_file.
 */

local FITSIO_BYTE_IMG, FITSIO_SHORT_IMG, FITSIO_LONG_IMG;
local FITSIO_LONGLONG_IMG, FITSIO_FLOAT_IMG, FITSIO_DOUBLE_IMG;
local FITSIO_SBYTE_IMG, FITSIO_USHORT_IMG, FITSIO_ULONG_IMG;
extern fitsio_get_img_type;
extern fitsio_get_img_equivtype;
/* DOCUMENT bitpix = fitsio_get_img_type(fh);
         or bitpix = fitsio_get_img_equivtype(fh);

     Get  the data  type or  equivalent  data type  of the  image.  The  first
     routine returns the physical data type of the FITS image, as given by the
     BITPIX keyword, with allowed values of 8, 16, 32, 64, -32, -64 (see table
     below).  The  second routine is similar,  except that if the  image pixel
     values  are scaled,  with non-default  values  for the  BZERO and  BSCALE
     keywords, then the routine will return the ’equivalent’ data type that is
     needed  to store  the scaled  values.  For  example, if  BITPIX =  16 and
     BSCALE  =  0.1  then  the   equivalent  data  type  is  FITSIO_FLOAT_IMG.
     Similarly if  BITPIX = 16, BSCALE  = 1, and  BZERO = 32768, then  the the
     pixel values span the range of an unsigned short integer and the returned
     data type will be CFITSIO_USHORT_IMG.

     ---------------------------------------------------------
     Constant            Value      Description
     ---------------------------------------------------------
     FITSIO_BYTE_IMG        8      8-bit unsigned integer
     FITSIO_SHORT_IMG      16     16-bit signed integer
     FITSIO_LONG_IMG       32     32-bit signed integer
     FITSIO_LONGLONG_IMG   64     64-bit signed integer
     FITSIO_FLOAT_IMG     -32     32-bit floating point
     FITSIO_DOUBLE_IMG    -64     64-bit floating point
     FITSIO_SBYTE_IMG      10      8-bit signed integer (*)
     FITSIO_USHORT_IMG     20     16-bit unsigned integer (*)
     FITSIO_ULONG_IMG      40     32-bit unsigned integer (*)
     ---------------------------------------------------------
     (*) non standard values


   SEE ALSO: fitsio_open_file, fitsio_get_img_size.
 */

extern fitsio_get_img_dim;
extern fitsio_get_img_size;
/* DOCUMENT naxis = fitsio_get_img_dim(fh);
         or size = fitsio_get_img_size(fh);

     Get the  number of dimensions, and/or  the size of each  dimension in the
     image.  The number  of axes in the  image is given by  the NAXIS keyword,
     and the size of each dimension is given by the NAXISn keywords.

   SEE ALSO: fitsio_open_file, fitsio_get_img_type.
 */

extern fitsio_read_array;
/* DOCUMENT fitsio_read_array(fh);
         or fitsio_read_array(fh, first=..., last=..., incr=...);
         or fitsio_read_array(fh, first=..., number=...);

     Read array values  from FITS "IMAGE" extension in the  current HDU of the
     handle FH.

     The first call returns the complete multi-dimesional array.

     The  second call  returns a  rectangular sub-array  whose first  and last
     elements  are defined  by  the values  of the  keywords  FIRST and  LAST.
     Optionally,  an increment  can  be  specified via  the  INCR keyword;  by
     default the increment  is equal to one for every  dimensions.  The values
     of  these  keywords  must  be multi-dimensional  "coordinates",  that  is
     integer vectors  of same length  as the rank of  the array (given  by the
     value of the  "NAXIS" keyword).  Integer scalars  are however acceptable,
     if NAXIS is equal to one.  Coordinates start at one.

     The third call returns a flat array of values.  The first element to read
     and the  number of elements to  read are specified by  the keywords FIRST
     and NUMBER.

     Keyword NULL can be  used to specify a variable to  store the value taken
     by undefined elements of the array.   If there are no undefined elements,
     the variable will be set to [] on return.  Beware that undefined elements
     may take the special NaN (not a number) value which is difficult to check
     (a simple comparison is not sufficient  but the ieee_test function can be
     used).  The example below takes care of that:

       local null;
       arr = fitsio_read_array(fh, null=null);
       if (! is_void(null)) {
          if (null == null) {
             // undefined values are not marked by NaN's
	     undefined = (arr == null);
          } else {
             // undefined values are marked by NaN's
	     undefined = (ieee_test(arr) == ieee_test(null));
          }
       }

     This  function  implements  most  of  the  capabilities  of  the  CFITSIO
     functions fits_read_img, fits_read_subset and fits_read_pix.


   SEE ALSO: fitsio_open_file, fitsio_write_array, ieee_test.
 */

extern fitsio_read_array;
/* DOCUMENT fitsio_read_array(fh);
         or fitsio_read_array(fh, first=..., last=..., incr=...);
         or fitsio_read_array(fh, first=..., number=...);

     Read array values from the current HDU  of handle FH.  The current HDU of
     FH must be a FITS "IMAGE" extension.

     The first call returns the complete multi-dimesional array.

     The  second call  returns a  rectangular sub-array  whose first  and last
     elements  are defined  by  the values  of the  keywords  FIRST and  LAST.
     Optionally,  an increment  can  be  specified via  the  INCR keyword;  by
     default the increment  is equal to one for every  dimensions.  The values
     of  these  keywords  must  be multi-dimensional  "coordinates",  that  is
     integer vectors  of same length  as the rank of  the array (given  by the
     value of the  "NAXIS" keyword).  Integer scalars  are however acceptable,
     if NAXIS is equal to one.  Coordinates start at one.

     The third call returns a flat array of values.  The first element to read
     and the  number of elements to  read are specified by  the keywords FIRST
     and NUMBER.

     Keyword NULL can be  used to specify a variable to  store the value taken
     by undefined elements of the array.   If there are no undefined elements,
     the variable will be set to [] on return.  Beware that undefined elements
     may take the special NaN (not a number) value which is difficult to check
     (a simple comparison is not sufficient  but the ieee_test function can be
     used).  The example below takes care of that:

       local null;
       arr = fitsio_read_array(fh, null=null);
       if (! is_void(null)) {
          if (null == null) {
             // undefined values are not marked by NaN's
	     undefined = (arr == null);
          } else {
             // undefined values are marked by NaN's
	     undefined = (ieee_test(arr) == ieee_test(null));
          }
       }

     This  function  implements  most  of  the  capabilities  of  the  CFITSIO
     functions fits_read_img, fits_read_subset and fits_read_pix.


   SEE ALSO: fitsio_open_file, fitsio_write_array, ieee_test.
 */
extern fitsio_create_img;
/* DOCUMENT fitsio_create_img, fh, bitpix, dims, ...;

     Create a new primary array or  IMAGE extension with a specified data type
     and size.  If the  FITS file is currently empty, then  a primary array is
     created,  otherwise  a new  IMAGE  extension  is  appended to  the  file.
     Remaining arguments  DIMS, ... form the  dimension list of the  image and
     can take any of the form accepted by Yorick array() function.

     When called as a function, FH is returned.

   SEE ALSO: fitsio_create_file, dimsof, array.
 */

extern fitsio_copy_cell2image;
extern fitsio_copy_image2cell;
/* DOCUMENT fitsio_copy_cell2image, inp, out, colnam, rownum;
         or fitsio_copy_image2cell, inp, out, colnam, rownum, copykeyflag;

     Copy an  n-dimensional image in a  particular row and column  of a binary
     table (in a vector column) to or from a primary array or image extension.

     The  cell2image routine  will append  a new  image extension  (or primary
     array) to  the output file.  Any  WCS keywords associated with  the input
     column image  will be translated into  the appropriate form for  an image
     extension.   Any  other  keywords  in  the  table  header  that  are  not
     specifically related to  defining the binary table structure  or to other
     columns in  the table  will also be  copied to the  header of  the output
     image.

     The image2cell routine  will copy the input image into  the specified row
     and column  of the current binary  table in the output  file.  The binary
     table HDU  must exist before calling  this routine, but it  may be empty,
     with no rows or columns of data.   The specified column (and row) will be
     created if it does not already exist.  The COPYKEYFLAG parameter controls
     which  keywords are  copied from  the input  image to  the header  of the
     output table: 0  = no keywords will  be copied, 1 = all  keywords will be
     copied (except those keywords that would be invalid in the table header),
     and 2 = copy only the WCS keywords.

     When called as a function, argument OUT is returned.

   SEE ALSO: fitsio_open_file.
 */

extern fitsio_write_array;
/* DOCUMENT fitsio_write_array, fh, arr, first=..., null=...;

     Write array  values ARR into the  current HDU of handle  FH.  The current
     HDU of FH must be a FITS "IMAGE" extension.

     There are 3  possibilities depending on whether and how  keyword FIRST is
     specified; in  any case, the  number of written values  is numberof(ARR).
     If keyword FIRST is not specified,  the array dimensions must be the same
     as  that of  the FITS  array.   If keyword  FIRST  is set  with a  scalar
     integer, it indicates  the position (starting at 1) of  the first element
     to  write  in  the  FITS  array,   the  writing  is  sequential  and  the
     dimensionality of ARR and of the FITS array are not considered.  Finally,
     if keyword FIRST is  set with a vector of NAXIS  integers (where NAXIS is
     the value  of the "NAXIS"  FITS key), it  indicates indices of  the first
     element of a rectangular sub-array to  write, the number of dimensions of
     ARR may be less than NAXIS.  When writing a sub-array the NULL keyword is
     not supported.

     Keyword NULL can be used to specify  the value of the invalid (or "null")
     elements in ARR.   The routine will substitute the  appropriate FITS null
     value for any elements  which are equal to the value  of keyword NULL (if
     specified this value must be a scalar of the same data type as ARR).  For
     integer FITS arrays, the FITS null  value is defined by the BLANK keyword
     (an error is returned if the  BLANK keyword doesn't exist).  For floating
     point  FITS arrays  the special  IEEE  NaN (Not-a-Number)  value will  be
     written into the FITS file.  Keyword NULL cannot be specified for writing
     a rectangular sub-array (i.e., when keyword FIRST is a list of indices).

     This   function   implements   writing    via   the   CFITSIO   functions
     fits_write_subset, fits_write_img and fits_write_imgnull.


   SEE ALSO: fitsio_open_file, fitsio_read_array, ieee_test.
 */

extern fitsio_copy_image_section;
/* DOCUMENT fitsio_copy_image_section, inp, out, section;

     Copy a rectangular section of an image and write it to a new FITS primary
     image or image  extension.  The new image  HDU is appended to  the end of
     the output file; all the keywords in the input image (current HDU of FITS
     handle  INP) will  be copied  to the  output image  (current HDU  of FITS
     handle OUT).   The common WCS  keywords will  be updated if  necessary to
     correspond to the coordinates of the  section.  The format of the section
     expression is same as specifying an image section using the extended file
     name  syntax   (see  CFISIO  documentation).    Examples:  "1:100,1:200",
     "1:100:2, 1:*:2", "*, -*".

   SEE ALSO: fitsio_open_file.
 */

/*---------------------------------------------------------------------------*/
/* TABLES */

extern fitsio_create_tbl;
/* DOCUMENT fitsio_create_tbl, fh, ttype, tform;

     Create a new ASCII or bintable table extension. If the FITS file is
     currently empty then a dummy primary array will be created before
     appending the table extension to it. The tbltype parameter defines the
     type of table and can have values of ASCII TBL or BINARY TBL.  The naxis2
     parameter gives the initial number of rows to be created in the table,
     and should normally be set = 0. CFITSIO will automatically increase the
     size of the table as additional rows are written. A non-zero number of
     rows may be specified to reserve space for that many rows, even if a
     fewer number of rows will be written. The tunit and extname parameters
     are optional and a null pointer may be given if they are not defined. The
     FITS Standard recommends that only letters, digits, and the underscore
     character be used in column names (the ttype parameter) with no embedded
     spaces. Trailing blank characters are not significant.


     Keyword TUNITS

     Keyword EXTNAME

     Keyword ASCII

   SEE ALSO:
 */

extern fitsio_get_num_rows;
extern fitsio_get_num_cols;
extern fitsio_get_colnum;
extern fitsio_get_colname;
/* DOCUMENT fitsio_get_colnum(fh, template, case=);
         or fitsio_get_colname(fh, template, case=);

     fitsio_get_colname(fh, "*");

     returns all column names (in order).

   SEE ALSO:
 */

extern fitsio_get_coltype;
extern fitsio_get_eqcoltype;
/* DOCUMENT [type,repeat,width] = fitsio_get_coltype(fh, colnum);
         or [type,repeat,width] = fitsio_get_eqcoltype(fh, colnum);

     Return the data  type, vector repeat value,  and the width in  bytes of a
     column in an ASCII or binary table.   Allowed values for the data type in
     ASCII tables  are: TSTRING, TSHORT,  TLONG, TFLOAT, and  TDOUBLE.  Binary
     tables  also support  these types:  TLOGICAL, TBIT,  TBYTE, TCOMPLEX  and
     TDBLCOMPLEX.  The negative of the data  type code value is returned if it
     is a variable length array column.  Note that in the case of a 'J' 32-bit
     integer  binary  table  column,  these routines  will  return  data  type
     TINT32BIT (which  in fact is equivalent  to TLONG).  With most  current C
     compilers,  a value  in  a 'J'  column  has  the same  size  as an  'int'
     variable,  and may  not  be equivalent  to a  'long'  variable, which  is
     64-bits long on an increasing number of compilers.

     The returned  `repeat` value  is the  vector repeat  count on  the binary
     table TFORMn keyword value. (ASCII table columns always have repeat = 1).
     The  returned `width`  value is  the width  in bytes  of a  single column
     element (e.g., a '10D' binary table column  will have width = 8, an ASCII
     table  'F12.2' column  will have  width =  12, and  a binary  table '60A'
     character  string  column will  have  width  =  60).  Note  that  CFITSIO
     supports  the local  convention  for specifying  arrays  of fixed  length
     strings within a  binary table character column using the  syntax TFORM =
     "rAw" where  'r' is the  total number of characters  (= the width  of the
     column) and 'w' is the width of a unit string within the column.  Thus if
     the column has TFORM = '60A12' then this means that each row of the table
     contains  5 12-character  substrings within  the 60-character  field, and
     thus in this  case this routine will return typecode  = TSTRING, repeat =
     60, and width =  12.  (The TDIMn keyword may also be  used to specify the
     unit  string length;  The pair  of keywords  TFORMn =  '60A' and  TDIMn =
     '(12,5)' would have the same effect  as TFORMn = '60A12').  The number of
     substrings in any  binary table character string field  can be calculated
     by (repeat/width).

     The second  routine, fitsio_get_eqcoltype is  similar except that  in the
     case of scaled integer columns it returns the 'equivalent' data type that
     is needed  to store the scaled  values, and not necessarily  the physical
     data  type of  the unscaled  values  as stored  in the  FITS table.   For
     example if a  '1I' column in a binary  table has TSCALn = 1  and TZEROn =
     32768,  then  this column  effectively  contains  unsigned short  integer
     values, and  thus the  returned value  of typecode  will be  TUSHORT, not
     TSHORT.  Similarly, if a column has TTYPEn = '1I' and TSCALn = 0.12, then
     the returned typecode will be TFLOAT.

  SEE ALSO:
*/

extern fitsio_read_tdim;
extern fitsio_decode_tdim;
extern fitsio_write_tdim;
/* DOCUMENT dims = fitsio_read_tdim(fh, colnum);
         or dims = fitsio_decode_tdim(fh, str, colnum);
         or fitsio_write_tdim, fh, colnum, dims, ...;

     The returned dimension list is similar to the value returned by the
     `dimsof()` function: [NDIMS, DIM1, DIM2, ...].  The `fitsio_write_tdim`
     accept a dimension list specified with a variable number of arguments as
     the `array()` function.


   SEE ALSO: dimsof.
 */

/*---------------------------------------------------------------------------*/
/* MISCELLANEOUS */

extern fitsio_debug;
/* DOCUMENT oldval = fitsio_debug(newval);
     Set the verbosity of error messages and return the former setting.
   SEE ALSO: error.
 */
extern fitsio_init;
fitsio_init;

/*
 * Local Variables:
 * mode: Yorick
 * tab-width: 8
 * c-basic-offset: 2
 * indent-tabs-mode: nil
 * fill-column: 78
 * coding: utf-8
 * End:
 */
