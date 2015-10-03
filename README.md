YFITSIO
=======

YFITSIO is a Yorick plug-in for reading/writing FITS files thanks to the
[CFITSIO library](http://heasarc.gsfc.nasa.gov/fitsio/fitsio.html).  FITS
(for Flexible Image Transport System) is a file format mostly used in
astronomy.  When complete, this plug-in will supersede the functionalities
implented by `"fits.i"` in pure Yorick, in the mean time the prefix
`fitsio_` is used to avoid confusion with the `fits_` suffix used by the
routines defined in by `"fits.i"`.


Installation
------------

In short, building and installing the plug-in can be as quick as:
````
$ cd $BUILD_DIR
$ $SRC_DIR/configure
$ make
$ make install
````
where `$BUILD_DIR` is the build directory (at your convenience) and
`$SRC\_DIR` is the source directory of the plug-in code.  The build and
source directories can be the same in which case, call `./configure` to
configure for building.

Then, to use the plug-in in Yorick:
````
$ yorick
> include "fitsio.i"
````
More detailled explanations are given below.

1. You must have Yorick and the CFITSIO library installed on your machine.

2. Unpack the plug-in code somewhere.

3. Configure for compilation.  The are two possibilities:

   For an **in-place build**, go to the source directory of the plug-in
   code and run the configuration script:
   ````
   $ cd SRC_DIR
   $ ./configure
   ````
   To see the configuration options, call:
   ````
   $ ./configure --help
   ````

   To compile in a **different build directory**, say BUILD_DIR, create the
   build directory, go to the build directory, and run the configuration
   script:
   ````
   $ mkdir -p $BUILD_DIR
   $ cd $BUILD_DIR
   $ $SRC_DIR/configure
   ````
   where `$SRC_DIR` is the path to the source directory of the plug-in
   code. To see the configuration options, call:
   ````
   $ $SRC_DIR/configure --help
   ````

4. Compile the code:
   ````
   $ make
   ````

4. Install the plug-in in Yorick directories:
   ````
   $ make install
   ````

