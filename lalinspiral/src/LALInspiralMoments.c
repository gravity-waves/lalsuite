/*
*  Copyright (C) 2007 David Churches, Duncan Brown, Jolien Creighton, Benjamin Owen, B.S. Sathyaprakash, Thomas Cokelaer
*
*  This program is free software; you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation; either version 2 of the License, or
*  (at your option) any later version.
*
*  This program is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*  GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with with program; see the file COPYING. If not, write to the
*  Free Software Foundation, Inc., 59 Temple Place, Suite 330, Boston,
*  MA  02111-1307  USA
*/

/**
Authors: Brown, D. A., Cokelaer, T. and  Sathyaprakash, B. S.
\file
\ingroup LALInspiralBank_h

\brief Module to calculate the moment of the noise power spectral density.

\heading{Prototypes}

<tt>LALInspiralMoments()</tt>:
<ul>
   <li> <tt>moment,</tt> Output, the value of the moment
   </li><li> <tt>pars,</tt> Input</li>
</ul>

\heading{Description}

The moments of the noise curve are defined as
\f{equation}{
I(q)  \equiv S_{h}(f_{0}) \int^{f_{c}/f_{0}}_{f_{s}/f_{0}}
\frac{x^{-q}}{S_{h}(x)} \, dx \,.
\f}
Because in practice we will always divide one of these moments by another, we
do not need to include the \f$S_{h}(f_{0})\f$ term, which always cancels.
This function calculates the integral
\f{equation}{
I = \int^{f_{c}/f_{0}}_{f_{s}/f_{0}} \frac{x^{-q}}{S_{h}(x)} \, dx \,.
\f}
It then divides this quantity by a normalisation constant which has been
passed to the function. In the case of calculating the components of the
metric for the signal manifold for the purpose of generating a template bank,
this constant is given by \f$I(7)\f$, because of the definition of the quantity
\f{equation}{
J(q) \equiv \frac{I(q)}{I(7/3)} \,.
\f}

\heading{Algorithm}
Given the exponent <tt>pars.ndx</tt> and limits of integration
<tt>pars.xmin</tt> and <tt>pars.xmax</tt> this function returns the moment of
the power spectral density specified by the frequency series
<tt>pars.shf</tt> according to
\f{equation}{
\mathtt{moment} = \int_{\mathtt{xmin}}^{\mathtt{xmax}}
\frac{x^{-\mathtt{ndx}}}{S_h(x)}\, dx \, .
\f}

\heading{Notes}

*/

#include <lal/LALInspiralBank.h>
#include <lal/Integrate.h>

/* Deprecation Warning */

void
LALGetInspiralMoments (
    LALStatus            *status,
    InspiralMomentsEtc   *moments,
    REAL8FrequencySeries *psd,
    InspiralTemplate     *params
    )

{
  INITSTATUS(status);
  ATTATCHSTATUSPTR( status );
  XLALPrintDeprecationWarning("LALGetInspiralMoments", "XLALGetInspiralMoments");

  if (XLALGetInspiralMoments(moments, params->fLower, params->fCutoff, psd)!= XLAL_SUCCESS){
     ABORTXLAL( status );
  }
  DETATCHSTATUSPTR( status );
  RETURN( status );
}

int
XLALGetInspiralMoments (
    InspiralMomentsEtc   *moments,
    REAL8 fLower,
    REAL8 fCutoff,
    REAL8FrequencySeries *psd
    )
{
  UINT4 k;
  REAL8 xmin;
  REAL8 xmax;
  REAL8 ndx;
  REAL8 norm;

  /* Check inputs */
  if (!moments){
    XLALPrintError("Moments is NULL\n");
    XLAL_ERROR(XLAL_EFAULT);
  }
  if (!psd){
    XLALPrintError("PSD is NULL\n");
    XLAL_ERROR(XLAL_EFAULT);
  }

  if (fLower <= 0 || fCutoff <= fLower){
    XLALPrintError("fLower must be between 0 and fCutoff\n");
    XLAL_ERROR(XLAL_EDOM);
  };

  /* Constants needed in computing the moments */
  moments->a01 = 3.L/5.L;
  moments->a21 = 11.L * LAL_PI/12.L;
  moments->a22 = 743.L/2016.L * cbrt(25.L/(2.L*LAL_PI*LAL_PI));
  moments->a31 = -3.L/2.L;
  moments->a41 = 617.L * LAL_PI * LAL_PI / 384.L;
  moments->a42 = 5429.L/5376.L * cbrt(25.L*LAL_PI/2.L);
  moments->a43 = 1.5293365L/1.0838016L * cbrt(5.L/(4.L*LAL_PI*LAL_PI*LAL_PI*LAL_PI));

  /* Divide all frequencies by fLower, a scaling that is used in solving */
  /* the moments integral                                                */
  psd->f0 /= fLower;
  psd->deltaF /= fLower;
  xmin = fLower / fLower;
  xmax = fCutoff / fLower;

  /* First compute the norm and print if requested */
  norm = 1.L;
  ndx = 7.L/3.L;
  moments->j[7]=XLALInspiralMoments(xmin, xmax, ndx, norm, psd);
  if (XLAL_IS_REAL8_FAIL_NAN(moments->j[7])){
    XLAL_ERROR(XLAL_EFUNC);
  }
  norm = moments->j[7];

  /* Then compute the normalised moments of the noise PSD from 1/3 to 17/3. */
  for ( k = 1; k <= 17; ++k )
  {
    ndx = (REAL8) k / 3.L;
    moments->j[k]=XLALInspiralMoments(xmin, xmax, ndx, norm, psd);
  }

  /* Moments are done: Rescale deltaF and f0 back to their original values */
  psd->deltaF *= fLower;
  psd->f0 *= fLower;

  return XLAL_SUCCESS;
}


void
LALGetInspiralMomentsBCV (
    LALStatus               *status,
    InspiralMomentsEtcBCV   *moments,
    REAL8FrequencySeries    *psd,
    InspiralTemplate        *params
    )
{
  UINT4 k;
  InspiralMomentsIn in;

  INITSTATUS(status);
  ATTATCHSTATUSPTR( status );

  /* doesn't seem to be needed. thomas, janvier 2004. I prefer to remove it for the moment.
   *  The factor is not important in the case of SPA approximation but is important in BCV
   *  case. Indeed on one hand we use quantity which are a ratio between two moments and
   *  consequently a factor 1 or 2 is not important. Howver in the case of BCV, we might
   *  use a moment alone. Thus a factor in the computation has an effect. */

  /*  for (i=0; i< psd->data->length ; i++)
  {
    psd->data->data[i] = psd->data->data[i] * 1e45;
  }
   */
  in.shf = psd;
  in.xmin = params->fLower;
  in.xmax = params->fCutoff;

  /* First compute the norm */
  in.norm = 1.L;
  for ( k = 0; k <= 22; ++k )
  {
    if (k <= 17)
    {
      /* positive value*/
      in.ndx = (REAL8)k / 3.L;
    }
    else
    {
      /* negative -1,-2 ...-6 */
      in.ndx = (17.- (REAL8)k) /3.L;
    }

    LALInspiralMoments( status->statusPtr, &moments->i[k], in );
    CHECKSTATUSPTR(status);
  }

  in.norm = moments->i[7] -2.*moments->alpha * moments->i[5] +
    moments->alpha * moments->alpha*moments->i[3];


  /* 17 */
  moments->M1[0][0] = (moments->i[17] -2.*moments->alpha * moments->i[15] +
      moments->alpha * moments->alpha*moments->i[13]) / in.norm;
  /* 14 */
  moments->M1[0][1] = (moments->i[14] -2.*moments->alpha * moments->i[12] +
      moments->alpha * moments->alpha*moments->i[10]) / in.norm;
  /* 11 */
  moments->M1[1][1] = (moments->i[11] -2.*moments->alpha * moments->i[9] +
      moments->alpha * moments->alpha*moments->i[7]) / in.norm;

  moments->M1[1][0]=moments->M1[0][1] ;

  /*  12 */
  moments->M2[0][0] = (moments->i[12] -2.*moments->alpha * moments->i[10] +
      moments->alpha * moments->alpha*moments->i[8]) / in.norm;
  /* 9 */

  moments->M2[0][1] = (moments->i[9] -2.*moments->alpha * moments->i[7] +
      moments->alpha * moments->alpha*moments->i[5]) / in.norm;
  /*  9 */

  moments->M2[1][0] = (moments->i[9] -2.*moments->alpha * moments->i[7] +
      moments->alpha * moments->alpha*moments->i[5]) / in.norm;
  /*  6 */
  moments->M2[1][1] = (moments->i[6] -2.*moments->alpha * moments->i[4] +
      moments->alpha * moments->alpha*moments->i[2]) / in.norm;

  /* 7 */
  moments->M3[0][0] = (moments->i[7] -2.*moments->alpha * moments->i[5] +
      moments->alpha * moments->alpha*moments->i[3]) / in.norm;
  /* 4 */
  moments->M3[0][1] = (moments->i[4] -2.*moments->alpha * moments->i[2] +
      moments->alpha * moments->alpha*moments->i[0]) / in.norm;
  /* 1 */
  moments->M3[1][1] = (moments->i[1] -2.*moments->alpha * moments->i[18] +
      moments->alpha * moments->alpha * moments->i[20]) / in.norm;

  moments->M3[1][0]=moments->M3[0][1] ;

  if ( lalDebugLevel & LALINFO )
  {
    LALPrintError( "#M1=\n");
    LALPrintError( "#%15.12lf %15.12lf \n# %15.12lf %15.12lf\n",
        moments->M1[0][0],
        moments->M1[0][1],
        moments->M1[1][0],
        moments->M1[1][1] );

    LALPrintError( "#M2=\n" );
    LALPrintError( "#%15.12lf %15.12lf \n# %15.12lf %15.12lf\n",
        moments->M2[0][0],
        moments->M2[0][1],

        moments->M2[1][0],
        moments->M2[1][1] );

    LALPrintError( "#M3=\n" );
    LALPrintError( "#%15.12lf %15.12lf \n# %15.12lf %15.12lf\n",
        moments->M3[0][0],
        moments->M3[0][1],
        moments->M3[1][0],
        moments->M3[1][1] );
  }

  DETATCHSTATUSPTR( status );
  RETURN( status );
}


/* Deprecation Warning */

void
LALInspiralMoments(
    LALStatus         *status,
    REAL8             *moment,
    InspiralMomentsIn  pars
    )

{
  INITSTATUS(status);
  XLALPrintDeprecationWarning("LALInspiralMoments", "XLALInspiralMoments");

  *moment = XLALInspiralMoments(pars.xmin, pars.xmax, pars.ndx, pars.norm, pars.shf);
  if (XLAL_IS_REAL8_FAIL_NAN(*moment)){
    ABORTXLAL( status );
  };
  RETURN (status);
}

REAL8
XLALInspiralMoments(
    REAL8 xmin,
    REAL8 xmax,
    REAL8 ndx,
    REAL8 norm,
    REAL8FrequencySeries *shf
    )

{
  REAL8 f;
  REAL8 moment;
  REAL8 momentTmp;
  REAL8 fMin;
  REAL8 fMax;
  REAL8 deltaF;
  UINT4 k;
  UINT4 kMin;
  UINT4 kMax;

  /* Check inputs */
  if (!shf || !(shf->data) || !(shf->data->data)){
    XLALPrintError("PSD or its data are NULL\n");
    XLAL_ERROR_REAL8(XLAL_EFAULT);
  }

  if (xmin <= 0 || xmax <= 0 || xmax <= xmin || norm <= 0){
    XLALPrintError("xmin, xmax, and norm must be positive and xmax must be greater than xmin\n");
    XLAL_ERROR_REAL8(XLAL_EDOM);
  }

  /* make sure that the minimum and maximum of the integral are within */
  /* the frequency series                                              */
  fMax = shf->f0 + shf->data->length * shf->deltaF;
  if ( xmin < shf->f0 || xmax > fMax+LAL_REAL4_EPS )
  {
    XLALPrintError("PSD does not cover domain of integration\n");
    XLAL_ERROR_REAL8(XLAL_EDOM);
  }

  /* the minimum and maximum frequency where we have four points */
  deltaF = shf->deltaF;
  fMin = shf->f0 + deltaF;
  fMax = shf->f0 + ((REAL8) shf->data->length - 2 ) * deltaF;

  if ( xmin <= fMin )
  {
    kMin = 1;
  }
  else
  {
    kMin = (UINT8) floor( (xmin - shf->f0) / deltaF );
  }

  if ( xmax >= fMax )
  {
    kMax = shf->data->length - 1;
  }
  else
  {
    kMax = (UINT8) floor( (xmax - shf->f0) / deltaF );
  }

  /* the first and last points of the integral */
  momentTmp = 0.;
  f = shf->f0 + (REAL8) kMin * deltaF;
  if( shf->data->data[kMin] )
  {
    momentTmp = pow( f, -(ndx) ) / ( 2.0 * shf->data->data[kMin] );
  }
  moment = momentTmp;

  momentTmp = 0.;
  f = shf->f0 + (REAL8) kMax * deltaF;
  if( shf->data->data[kMin] )
  {
    momentTmp = pow( f, -(ndx) ) / ( 2.0 * shf->data->data[kMin] );
  }
  moment += momentTmp;
#if 0
  In the following line we should have kMax
  Changed by Sathya on June 30, 2002
  moment += pow( f, -(ndx) ) / ( 2.0 * shf->data->data[kMin] );
#endif
  momentTmp = 0.;
  if ( shf->data->data[kMax] )
  {
    momentTmp = pow( f, -(ndx) ) / ( 2.0 * shf->data->data[kMax] );
  }
  moment += momentTmp;
  kMin++;
  kMax--;

  if (kMax<=kMin){
    XLALPrintError("kMin must be less than kMax\n");
    XLAL_ERROR_REAL8(XLAL_EDOM);
  };

  for ( k = kMin; k < kMax; ++k )
  {
    momentTmp = 0.;
    f = shf->f0 + (REAL8) k * deltaF;
    if ( shf->data->data[k] )
    {
      momentTmp = pow( f, -(ndx) ) / shf->data->data[k];
    }
    moment += momentTmp;
  }

  moment *= deltaF;

  /* now divide the moment by the specified norm */
  moment /= norm;

  return moment;
}
