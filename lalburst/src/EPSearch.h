/*
 *
 * Copyright (C) 2007  Kipp Cannon and Flanagan, E
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */


#ifndef _EPSEARCH_H
#define _EPSEARCH_H


#include <lal/LALDatatypes.h>
#include <lal/LIGOMetadataTables.h>
#include <lal/TimeSeries.h>
#include <lal/Window.h>


#ifdef  __cplusplus   /* C++ protection. */
extern "C" {
#endif

/**
 * \defgroup EPSearch_h Header EPSearch.h
 * \ingroup lalburst_burstsearch
 *
 * \brief A set of functions to implement the excess power search
 * technique which was suggested in Ref.\ \cite fh1998 and later
 * independently invented in Ref.\ \cite acdhp1999 .  The implementation
 * here is described in detail in Ref.\ \cite ABCF2001 .
 */
  /*@{*/


/**
 * liblal.so can't resolve symbols from liblalsupport.so, so to call
 * diagnostics dump functions from lal, they have to be passed in as
 * pointers.  The prototypes here must match those in LIGOLwXMLArray.h or
 * memory problems will occur.  Since this is only used for diagnostics,
 * not production runs, there isn't any need to do this more robustly.
 * Note, also that the LIGOLwXMLStream structure is defined in a header
 * from liblalsupport, so here it has to be refered to as a void *.
 */
struct XLALEPSearchDiagnostics {
	void *LIGOLwXMLStream;
	int (*XLALWriteLIGOLwXMLArrayREAL8FrequencySeries)(void *, const char *, const REAL8FrequencySeries *);
	int (*XLALWriteLIGOLwXMLArrayREAL8TimeSeries)(void *, const char *, const REAL8TimeSeries *);
	int (*XLALWriteLIGOLwXMLArrayCOMPLEX16FrequencySeries)(void *, const char *, const COMPLEX16FrequencySeries *);
};


SnglBurst *XLALEPSearch(
	struct XLALEPSearchDiagnostics *diagnostics,
	const REAL8TimeSeries  *tseries,
	REAL8Window *window,
	double flow,
	double bandwidth,
	double confidence_threshold,
	/* t.f. plane tiling parameters */
	double fractional_stride,
	double maxTileBandwidth,
	double maxTileDuration
);

  /*@}*/

#ifdef  __cplusplus
}
#endif  /* C++ protection. */

#endif
