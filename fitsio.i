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

extern fitsio_open;
extern fitsio_create;
extern fitsio_close;
extern fitsio_delete;
extern fitsio_is_open;
extern fitsio_is_handle;
extern fitsio_movabs_hdu;
extern fitsio_movrel_hdu;
extern fitsio_movnam_hdu;
extern fitsio_get_num_hdus;
extern fitsio_get_hdu_num;
extern fitsio_get_hdu_type;
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
