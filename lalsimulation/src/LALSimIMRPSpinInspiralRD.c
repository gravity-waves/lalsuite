/*
 * Copyright (C) 2011 Riccardo Sturani, John Veitch, Drew Keppel
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

#include <stdlib.h>
#include <math.h>
#include <gsl/gsl_linalg.h>
#include <gsl/gsl_interp.h>
#include <gsl/gsl_spline.h>
#include <lal/LALStdlib.h>
#include <lal/AVFactories.h>
#include <lal/SeqFactories.h>
#include <lal/Units.h>
#include <lal/TimeSeries.h>
#include <lal/LALConstants.h>
#include <lal/SeqFactories.h>
#include <lal/RealFFT.h>
#include <lal/SphericalHarmonics.h>
#include <lal/LALAdaptiveRungeKutta4.h>
#include <lal/LALSimInspiral.h>
#include <lal/Date.h>
#include <lal/LALSimIMRPSpinInspiralRD.h>

#include "LALSimInspiralPNCoefficients.c"

/* Minimum integration length */
#define minIntLen    16
/* For turning on debugging messages*/
#define DEBUG_LEVEL  1

#define nModes 8
#define RD_EFOLDS 10

static REAL8 OmMatch(REAL8 LNhS1, REAL8 LNhS2, REAL8 S1S1, REAL8 S1S2, REAL8 S2S2) {

  const REAL8 omM       = 0.0555;
  const REAL8 omMsz12   =    9.97e-4;
  const REAL8 omMs1d2   =  -2.032e-3;
  const REAL8 omMssq    =   5.629e-3;
  const REAL8 omMsz1d2  =   8.646e-3;
  const REAL8 omMszsq   =  -5.909e-3;
  const REAL8 omM3s1d2  =   1.801e-3;
  const REAL8 omM3ssq   = -1.4059e-2;
  const REAL8 omM3sz1d2 =  1.5483e-2;
  const REAL8 omM3szsq  =   8.922e-3;

  return omM + /*6.05e-3 * sqrtOneMinus4Eta +*/
    omMsz12   * (LNhS1 + LNhS2) +
    omMs1d2   * (S1S2) +
    omMssq    * (S1S1 + S2S2) +
    omMsz1d2  * (LNhS1 * LNhS2) +
    omMszsq   * (LNhS1 * LNhS1 + LNhS2 * LNhS2) +
    omM3s1d2  * (LNhS1 + LNhS2) * (S1S2) +
    omM3ssq   * (LNhS1 + LNhS2) * (S1S1+S2S2) +
    omM3sz1d2 * (LNhS1 + LNhS2) * (LNhS1*LNhS2) +
    omM3szsq  * (LNhS1 + LNhS2) * (LNhS1*LNhS1+LNhS2*LNhS2);
} /* End of OmMatch */

static REAL8 fracRD(REAL8 LNhS1, REAL8 LNhS2, REAL8 S1S1, REAL8 S1S2, REAL8 S2S2) {

  const double frac0      = 0.840;
  const double fracsz12   = -2.145e-2;
  const double fracs1d2   = -4.421e-2;
  const double fracssq    = -2.643e-2;
  const double fracsz1d2  = -5.876e-2;
  const double fracszsq   = -2.215e-2;

  return frac0 +
    fracsz12   * (LNhS1 + LNhS2) +
    fracs1d2   * (S1S2) +
    fracssq    * (S1S1 + S2S2) +
    fracsz1d2  * (LNhS1 * LNhS2) +
    fracszsq   * (LNhS1 * LNhS1 + LNhS2 * LNhS2);
} /* End of fracRD */

/**
 * Convenience function to set up XLALSimInspiralSpinTaylotT4Coeffs struct
 */

static int XLALSimIMRPhenSpinParamsSetup(LALSimInspiralSpinTaylorT4Coeffs  *params,  /** PN params [returned] */
					 REAL8 dt,                                   /** Sampling in secs */
					 REAL8 fStart,                               /** Starting frequency of integration*/
					 REAL8 fEnd,                                 /** Ending frequency of integration*/
					 REAL8 mass1,                                /** Mass 1 in solar mass units */
					 REAL8 mass2,                                /** Mass 2 in solar mass units */
					 LALSimInspiralInteraction interFlags,       /** Spin interaction */
					 LALSimInspiralTestGRParam *testGR,          /** Test GR param */
					 UINT4 order                                 /** twice PN Order in Phase */
					 )
{
  /* Zero the coefficients */
  memset(params, 0, sizeof(LALSimInspiralSpinTaylorT4Coeffs));
  params->M      = (mass1+mass2);
  params->eta    = mass1*mass2/(params->M*params->M);
  params->m1ByM  = mass1 / params->M;
  params->m2ByM  = mass2 / params->M;
  params->dmByM  = (mass1 - mass2) / params->M;
  params->m1Bym2 = mass1/mass2;
  params->M     *= LAL_MTSUN_SI;
  REAL8 unitHz   = params->M *((REAL8) LAL_PI);

  params->fEnd   = fEnd*unitHz;
  params->fStart = fStart*unitHz;
  params->dt     = dt;

  REAL8 phi1 = XLALSimInspiralTestGRParamExists(testGR,"phi1") ? XLALSimInspiralGetTestGRParam(testGR,"phi1") : 0.;
  REAL8 phi2 = XLALSimInspiralTestGRParamExists(testGR,"phi2") ? XLALSimInspiralGetTestGRParam(testGR,"phi2") : 0.;
  REAL8 phi3 = XLALSimInspiralTestGRParamExists(testGR,"phi3") ? XLALSimInspiralGetTestGRParam(testGR,"phi3") : 0.;
  REAL8 phi4 = XLALSimInspiralTestGRParamExists(testGR,"phi4") ? XLALSimInspiralGetTestGRParam(testGR,"phi4") : 0.;

  params->wdotnewt = XLALSimInspiralTaylorT4Phasing_0PNCoeff(params->eta);
  params->Enewt    = XLALSimInspiralEnergy_0PNCoeff(params->eta);

  switch (order) {

    case -1: // Use the highest PN order available.
    case 7:
      params->wdotcoeff[7]  = XLALSimInspiralTaylorT4Phasing_7PNCoeff(params->eta);

    case 6:
      params->Ecoeff[6]     = XLALSimInspiralEnergy_6PNCoeff(params->eta);
      params->wdotcoeff[6]  = XLALSimInspiralTaylorT4Phasing_6PNCoeff(params->eta);
      params->wdotlogcoeff  = XLALSimInspiralTaylorT4Phasing_6PNLogCoeff(params->eta);
      if( (interFlags & LAL_SIM_INSPIRAL_INTERACTION_SPIN_ORBIT_3PN) == LAL_SIM_INSPIRAL_INTERACTION_SPIN_ORBIT_3PN ) {
	params->wdotSO3s1   = XLALSimInspiralTaylorT4Phasing_6PNSLCoeff(params->m1ByM);
	params->wdotSO3s2   = XLALSimInspiralTaylorT4Phasing_6PNSLCoeff(params->m1ByM);
      }

    case 5:
      params->Ecoeff[5]     = 0.;
      params->wdotcoeff[5]  = XLALSimInspiralTaylorT4Phasing_5PNCoeff(params->eta);
      if( (interFlags & LAL_SIM_INSPIRAL_INTERACTION_SPIN_ORBIT_25PN) == LAL_SIM_INSPIRAL_INTERACTION_SPIN_ORBIT_25PN ) {
	params->ESO25s1     = XLALSimInspiralEnergy_5PNSOCoeffs1(params->eta, params->m1Bym2);
	params->ESO25s2     = XLALSimInspiralEnergy_5PNSOCoeffs1(params->eta, 1./params->m1Bym2);
	params->wdotSO25s1  = XLALSimInspiralTaylorT4Phasing_5PNSLCoeff(params->eta, params->m1Bym2);
	params->wdotSO25s2  = XLALSimInspiralTaylorT4Phasing_5PNSLCoeff(params->eta, 1./params->m1Bym2);
	params->S1dot25     = XLALSimInspiralSpinDot_5PNCoeff(params->eta,params->dmByM);
	params->S2dot25     = XLALSimInspiralSpinDot_5PNCoeff(params->eta,-params->dmByM);
      }

    case 4:
      params->wdotcoeff[4]  = XLALSimInspiralTaylorT4Phasing_4PNCoeff(params->eta)+phi4;
      params->Ecoeff[4]   = XLALSimInspiralEnergy_4PNCoeff(params->eta);
      if( (interFlags & LAL_SIM_INSPIRAL_INTERACTION_SPIN_SPIN_2PN) ==  LAL_SIM_INSPIRAL_INTERACTION_SPIN_SPIN_2PN ) {
	params->wdotSS2     = XLALSimInspiralTaylorT4Phasing_4PNSSCoeff(params->eta);
	params->wdotSSO2    = XLALSimInspiralTaylorT4Phasing_4PNSSOCoeff(params->eta);
	params->ESS2        = XLALSimInspiralEnergy_4PNSSCoeff(params->eta);
	params->ESSO2       = XLALSimInspiralEnergy_4PNSSOCoeff(params->eta);
      }
      if( (interFlags & LAL_SIM_INSPIRAL_INTERACTION_SPIN_SPIN_SELF_2PN) == LAL_SIM_INSPIRAL_INTERACTION_SPIN_SPIN_SELF_2PN ) {
	params->wdotSelfSS2 = XLALSimInspiralTaylorT4Phasing_4PNSelfSSCoeff(params->eta);
	params->wdotSelfSSO2= XLALSimInspiralTaylorT4Phasing_4PNSelfSSOCoeff(params->eta);
	params->ESelfSS2s1  = XLALSimInspiralEnergy_4PNSelfSSCoeff(params->m1Bym2);
	params->ESelfSS2s2  = XLALSimInspiralEnergy_4PNSelfSSCoeff(1./params->m1Bym2);
	params->ESelfSSO2s1 = XLALSimInspiralEnergy_4PNSelfSSOCoeff(params->m1Bym2);
	params->ESelfSSO2s2 = XLALSimInspiralEnergy_4PNSelfSSOCoeff(1./params->m1Bym2);
      }

    case 3:
      params->Ecoeff[3]      = 0.;
      params->wdotcoeff[3]   = XLALSimInspiralTaylorT4Phasing_3PNCoeff(params->eta)+phi3;
      if( (interFlags & LAL_SIM_INSPIRAL_INTERACTION_SPIN_ORBIT_15PN) == LAL_SIM_INSPIRAL_INTERACTION_SPIN_ORBIT_15PN ) {
	  params->wdotSO15s1 = XLALSimInspiralTaylorT4Phasing_3PNSOCoeff(params->m1Bym2);
	  params->wdotSO15s2 = XLALSimInspiralTaylorT4Phasing_3PNSOCoeff(1./params->m1Bym2);
	  params->ESO15s1    = XLALSimInspiralEnergy_3PNSOCoeff(params->m1Bym2);
	  params->ESO15s2    = XLALSimInspiralEnergy_3PNSOCoeff(1./params->m1Bym2);
	  params->S1dot15    = XLALSimInspiralSpinDot_3PNCoeff(params->eta,params->m1Bym2);
	  params->S2dot15    = XLALSimInspiralSpinDot_3PNCoeff(params->eta,1./params->m1Bym2);
      }

    case 2:
      params->Ecoeff[2]  = XLALSimInspiralEnergy_2PNCoeff(params->eta);
      params->wdotcoeff[2] = XLALSimInspiralTaylorT4Phasing_2PNCoeff(params->eta)+phi2;

    case 1:
      params->Ecoeff[1]  = 0.;
      params->wdotcoeff[1] = phi1;

    case 0:
      params->Ecoeff[0]  = 1.;
      params->wdotcoeff[0] = 1.;
      break;

    case 8:
      XLALPrintError("*** LALSimIMRPhenSpinInspiralRD ERROR: PhenSpin approximant not available at pseudo4PN order\n");
			XLAL_ERROR(XLAL_EDOM);
      break;

    default:
      XLALPrintError("*** LALSimIMRPhenSpinInspiralRD ERROR: Impossible to create waveform with %d order\n",order);
			XLAL_ERROR(XLAL_EFAILED);
      break;
  }

  return XLAL_SUCCESS;
} /* End of XLALSimIMRPhenSpinParamsSetup */

static int XLALSpinInspiralDerivatives(UNUSED double t,
				       const double values[],
				       double dvalues[],
				       void *mparams)
{
  REAL8 omega;                // time-derivative of the orbital phase
  REAL8 LNhx, LNhy, LNhz;     // orbital angular momentum unit vector
  REAL8 S1x, S1y, S1z;        // dimension-less spin variable S/M^2
  REAL8 S2x, S2y, S2z;
  REAL8 LNhS1, LNhS2;         // scalar products
  REAL8 domega;               // derivative of omega
  REAL8 dLNhx, dLNhy, dLNhz;  // derivatives of \f$\hat L_N\f$ components
  REAL8 dS1x, dS1y, dS1z;     // derivative of \f$S_i\f$
  REAL8 dS2x, dS2y, dS2z;
  REAL8 energy,energyold;

  /* auxiliary variables*/
  REAL8 S1S2, S1S1, S2S2;     // Scalar products
  REAL8 alphadotcosi;         // alpha is the right ascension of L, i(iota) the angle between L and J
  REAL8 v, v2, v4, v5, v6, v7;
  REAL8 tmpx, tmpy, tmpz, cross1x, cross1y, cross1z, cross2x, cross2y, cross2z, LNhxy;

  LALSimInspiralSpinTaylorT4Coeffs *params = (LALSimInspiralSpinTaylorT4Coeffs *) mparams;

  /* --- computation start here --- */
  omega = values[1];

  LNhx = values[2];
  LNhy = values[3];
  LNhz = values[4];

  S1x = values[5];
  S1y = values[6];
  S1z = values[7];

  S2x = values[8];
  S2y = values[9];
  S2z = values[10];

  energyold = values[11];

  /*  int i;
      for (i=0;i<12;i++) printf(" val %d %12.6e\n",i,values[i]);*/

  v = cbrt(omega);
  v2 = v * v;
  v4 = omega * v;
  v5 = omega * v2;
  v6 = omega * omega;
  v7 = omega * omega * v;

  // Omega derivative without spin effects up to 3.5 PN
  // this formula does not include the 1.5PN shift mentioned in arXiv:0810.5336, five lines below (3.11)
  domega = params->wdotcoeff[0]
          + v * (params->wdotcoeff[1]
                 + v * (params->wdotcoeff[2]
                        + v * (params->wdotcoeff[3]
                               + v * (params->wdotcoeff[4]
                                      + v * (params->wdotcoeff[5]
                                             + v * (params->wdotcoeff[6] + params->wdotlogcoeff * log(omega)
                                                    + v * params->wdotcoeff[7]))))));

  /*  printf(" domega %12.4e v %12.6e\n",domega,v);
  printf("params->wdotcoeff0 %12.6e\n",params->wdotcoeff[0]);
  printf("params->wdotcoeff1 %12.6e\n",params->wdotcoeff[1]);
  printf("params->wdotcoeff2 %12.6e\n",params->wdotcoeff[2]);
  printf("params->wdotcoeff3 %12.6e\n",params->wdotcoeff[3]);
  printf("params->wdotcoeff4 %12.6e\n",params->wdotcoeff[4]);
  printf("params->wdotcoeff5 %12.6e\n",params->wdotcoeff[5]);
  printf("params->wdotcoeff6 %12.6e\n",params->wdotcoeff[6]);
  printf("params->wdotcoeff7 %12.6e\n",params->wdotcoeff[7]);
  printf("params->wdotcoeffL %12.6e\n",params->wdotlogcoeff);*/

  energy = (params->Ecoeff[0] + v2 * (params->Ecoeff[2] +
				      v2 * (params->Ecoeff[4] +
					    v2 * params->Ecoeff[6])));

  /*printf("params->ecoeff2 %12.6e\n",params->Ecoeff[2]);
  printf("params->ecoeff4 %12.6e\n",params->Ecoeff[4]);
  printf("params->ecoeff6 %12.6e\n",params->Ecoeff[6]);*/

  // Adding spin effects
  // L dot S1,2
  LNhS1 = (LNhx * S1x + LNhy * S1y + LNhz * S1z);
  LNhS2 = (LNhx * S2x + LNhy * S2y + LNhz * S2z);

  // wdotSO15si = -1/12 (...)
  domega += omega * (params->wdotSO15s1 * LNhS1 + params->wdotSO15s2 * LNhS2); // see e.g. Buonanno et al. gr-qc/0211087

  //printf("params->wdotSO15 %12.6e %12.6e\n",params->wdotSO15s1,params->wdotSO15s2);

  energy += omega * (params->ESO15s1 * LNhS1 + params->ESO15s2 * LNhS2);  // see e.g. Blanchet et al. gr-qc/0605140

  // wdotSS2 = -1/48 eta ...
  S1S1 = (S1x * S1x + S1y * S1y + S1z * S1z);
  S2S2 = (S2x * S2x + S2y * S2y + S2z * S2z);
  S1S2 = (S1x * S2x + S1y * S2y + S1z * S2z);
  domega += v4 * ( params->wdotSS2 * S1S2 + params->wdotSSO2 * LNhS1 * LNhS2);	// see e.g. Buonanno et al. arXiv:0810.5336
  domega += v4 * ( params->wdotSelfSSO2 * (LNhS1 * LNhS1 + LNhS2 * LNhS2) + params->wdotSelfSS2 * (S1S1 + S2S2));
  // see Racine et al. arXiv:0812.4413
  //printf("params->wdotSS2 %12.6e %12.6e %12.6e %12.6e\n",params->wdotSS2,params->wdotSSO2,params->wdotSelfSSO2,params->wdotSelfSS2);

  energy += v4 * (params->ESS2 * S1S2 + params->ESSO2 * LNhS1 * LNhS2);    // see e.g. Buonanno et al. as above
  energy += v4 * (params->ESelfSS2s1 * S1S1 + params->ESelfSS2s2 * S2S2 + params->ESelfSSO2s1 * LNhS1 * LNhS1 + params->ESelfSSO2s2 * LNhS2 * LNhS2);	// see Racine et al. as above

  // wdotspin25SiLNh = see below
  domega += v5 * (params->wdotSO25s1 * LNhS1 + params->wdotSO25s2 * LNhS2);	//see (8.3) of Blanchet et al.
  energy += v5 * (params->ESO25s1 * LNhS1 + params->ESO25s2 * LNhS2);    //see (7.9) of Blanchet et al.
  //printf("params->ESO %12.6e %12.6e\n",params->ESO25s1,params->ESO25s2);

  domega += omega*omega * (params->wdotSO3s1 * LNhS1 + params->wdotSO3s2 * LNhS2); // see (6.5) of arXiv:1104.5659
  //printf("params->wdotSO3 %12.6e %12.6e\n",params->wdotSO3s1,params->wdotSO3s2);

  // Setting the right pre-factor
  domega *= params->wdotnewt * v5 * v6;
  //printf("params->wdotnewt %12.6e\n",params->wdotnewt);

  energy *= params->Enewt * v2;
  //printf("params->Enewt %12.6e\n",params->Enewt);

  /*Derivative of the angular momentum and spins */

  /* dS1, 1.5PN */
  /* S1dot15= (4+3m2/m1)/2 * eta */
  cross1x = (LNhy * S1z - LNhz * S1y);
  cross1y = (LNhz * S1x - LNhx * S1z);
  cross1z = (LNhx * S1y - LNhy * S1x);

  dS1x = params->S1dot15 * v5 * cross1x;
  dS1y = params->S1dot15 * v5 * cross1y;
  dS1z = params->S1dot15 * v5 * cross1z;

  //printf("params->S1dot15 %12.6e\n",params->S1dot15);

  /* dS1, 2PN */
  /* Sdot20= 0.5 */
  tmpx = S1z * S2y - S1y * S2z;
  tmpy = S1x * S2z - S1z * S2x;
  tmpz = S1y * S2x - S1x * S2y;

  // S1S2 contribution see. eq. 2.23 of arXiv:0812.4413
  dS1x += 0.5 * v6 * (tmpx - 3. * LNhS2 * cross1x);
  dS1y += 0.5 * v6 * (tmpy - 3. * LNhS2 * cross1y);
  dS1z += 0.5 * v6 * (tmpz - 3. * LNhS2 * cross1z);
  // S1S1 contribution
  dS1x -= 1.5 * v6 * LNhS1 * cross1x * (1. + 1./params->m1Bym2) * params->m1ByM;
  dS1y -= 1.5 * v6 * LNhS1 * cross1y * (1. + 1./params->m1Bym2) * params->m1ByM;
  dS1z -= 1.5 * v6 * LNhS1 * cross1z * (1. + 1./params->m1Bym2) * params->m1ByM;
  //printf("params->m1/m2 %12.6e\n",params->m1Bym2);

  // dS1, 2.5PN, eq. 7.8 of Blanchet et al. gr-qc/0605140
  // S1dot25= 9/8-eta/2.+eta+mparams->eta*29./24.+mparams->m1m2*(-9./8.+5./4.*mparams->eta)
  dS1x += params->S1dot25 * v7 * cross1x;
  dS1y += params->S1dot25 * v7 * cross1y;
  dS1z += params->S1dot25 * v7 * cross1z;

  /* dS2, 1.5PN */
  cross2x = (LNhy * S2z - LNhz * S2y);
  cross2y = (LNhz * S2x - LNhx * S2z);
  cross2z = (LNhx * S2y - LNhy * S2x);

  dS2x = params->S2dot15 * v5 * cross2x;
  dS2y = params->S2dot15 * v5 * cross2y;
  dS2z = params->S2dot15 * v5 * cross2z;

  //printf("params->S2dot15 %12.6e\n",params->S2dot15);

  /* dS2, 2PN */
  dS2x += 0.5 * v6 * (-tmpx - 3.0 * LNhS1 * cross2x);
  dS2y += 0.5 * v6 * (-tmpy - 3.0 * LNhS1 * cross2y);
  dS2z += 0.5 * v6 * (-tmpz - 3.0 * LNhS1 * cross2z);
  // S2S2 contribution
  dS2x -= 1.5 * v6 * LNhS2 * cross2x * (1. + params->m1Bym2) * params->m2ByM;
  dS2y -= 1.5 * v6 * LNhS2 * cross2y * (1. + params->m1Bym2) * params->m2ByM;
  dS2z -= 1.5 * v6 * LNhS2 * cross2z * (1. + params->m1Bym2) * params->m2ByM;

  // dS2, 2.5PN, eq. 7.8 of Blanchet et al. gr-qc/0605140
  dS2x += params->S2dot25 * v7 * cross2x;
  dS2y += params->S2dot25 * v7 * cross2y;
  dS2z += params->S2dot25 * v7 * cross2z;

  //printf("params->S2dot25 %12.6e\n",params->S2dot25);

  dLNhx = -(dS1x + dS2x) * v / params->eta;
  dLNhy = -(dS1y + dS2y) * v / params->eta;
  dLNhz = -(dS1z + dS2z) * v / params->eta;

  /* dphi */
  LNhxy = LNhx * LNhx + LNhy * LNhy;

  if (LNhxy > 0.0)
    alphadotcosi = LNhz * (LNhx * dLNhy - LNhy * dLNhx) / LNhxy;
  else
  {
    //XLALPrintWarning("*** LALPSpinInspiralRD WARNING ***: alphadot set to 0, LNh:(%12.4e %12.4e %12.4e)\n",LNhx,LNhy,LNhz);
    alphadotcosi = 0.;
  }

  /* dvalues->data[0] is the phase derivative */
  /* omega is the derivative of the orbital phase omega \neq dvalues->data[0] */
  dvalues[0] = omega - alphadotcosi;
  dvalues[1] = domega;

  dvalues[2] = dLNhx;
  dvalues[3] = dLNhy;
  dvalues[4] = dLNhz;

  dvalues[5] = dS1x;
  dvalues[6] = dS1y;
  dvalues[7] = dS1z;

  dvalues[8] = dS2x;
  dvalues[9] = dS2y;
  dvalues[10] = dS2z;

  dvalues[11] = (energy-energyold)/params->dt*params->M;

  //printf(" e %12.5e  eO %12.5e  dt %12.5e  M %12.5e\n",energy,energyold,params->dt,params->M);

  return GSL_SUCCESS;
} /* end of XLALSpinInspiralDerivatives */

int XLALGenerateWaveDerivative (REAL8Vector *dwave,
				REAL8Vector *wave,
				REAL8 dt
				)
{
  /* XLAL error handling */
  int errcode = XLAL_SUCCESS;

  /* For checking GSL return codes */
  INT4 gslStatus;

  UINT4 j;
  double *x, *y;
  double dy;
  gsl_interp_accel *acc;
  gsl_spline *spline;

  if (wave->length!=dwave->length)
    XLAL_ERROR( XLAL_EFUNC );

  /* Getting interpolation and derivatives of the waveform using gsl spline routine */
  /* Initialize arrays and supporting variables for gsl */

  x = (double *) LALMalloc(wave->length * sizeof(double));
  y = (double *) LALMalloc(wave->length * sizeof(double));

  if ( !x || !y )
  {
    if ( x ) LALFree (x);
    if ( y ) LALFree (y);
    XLAL_ERROR( XLAL_ENOMEM );
  }

  for (j = 0; j < wave->length; ++j)
  {
		x[j] = j;
		y[j] = wave->data[j];
  }

  XLAL_CALLGSL( acc = (gsl_interp_accel*) gsl_interp_accel_alloc() );
  XLAL_CALLGSL( spline = (gsl_spline*) gsl_spline_alloc(gsl_interp_cspline, wave->length) );
  if ( !acc || !spline )
  {
    if ( acc )    gsl_interp_accel_free(acc);
    if ( spline ) gsl_spline_free(spline);
    LALFree( x );
    LALFree( y );
    XLAL_ERROR( XLAL_ENOMEM );
  }

  /* Gall gsl spline interpolation */
  XLAL_CALLGSL( gslStatus = gsl_spline_init(spline, x, y, wave->length) );
  if ( gslStatus != GSL_SUCCESS )
  {
    gsl_spline_free(spline);
    gsl_interp_accel_free(acc);
    LALFree( x );
    LALFree( y );
    XLAL_ERROR( XLAL_EFUNC );
  }

  /* Getting first and second order time derivatives from gsl interpolations */
  for (j = 0; j < wave->length; ++j)
  {
    XLAL_CALLGSL(gslStatus = gsl_spline_eval_deriv_e( spline, j, acc, &dy ) );
    if (gslStatus != GSL_SUCCESS )
    {
      gsl_spline_free(spline);
      gsl_interp_accel_free(acc);
      LALFree( x );
      LALFree( y );
      XLAL_ERROR( XLAL_EFUNC );
    }
    dwave->data[j]  = (REAL8)(dy / dt);
  }

  /* Free gsl variables */
  gsl_spline_free(spline);
  gsl_interp_accel_free(acc);
  LALFree(x);
  LALFree(y);
	
  return errcode;
} /* End of XLALGenerateWaveDerivative */

static int XLALSimSpinInspiralTest(UNUSED double t, const double values[], double dvalues[], void *mparams) {

  LALSimInspiralSpinTaylorT4Coeffs *params = (LALSimInspiralSpinTaylorT4Coeffs *) mparams;

  REAL8 omega   =   values[1];
  REAL8 energy  =  values[11];
  REAL8 denergy = dvalues[11];

  if ( (energy > 0.0) || (( denergy > - 0.01*energy/params->dt*params->M )&&(energy<0.) ) ) {
    /*energy increase by more than 1%*/
#if DEEBUG_LEVEL
    fprintf(stderr,"** LALSimIMRPSpinInspiralRD WARNING **: Energy increases dE %12.6e E %12.6e  M: %12.4e, eta: %12.4e  om %12.6e \n",denergy, energy, params->M, params->eta, omega);
#endif
    XLALPrintWarning("** LALSimIMRPSpinInspiralRD WARNING **: Energy increases dE %12.6e E %12.6e  M: %12.4e, eta: %12.4e  om %12.6e \n",denergy, energy, params->M, params->eta, omega);
    return LALSIMINSPIRAL_PHENSPIN_TEST_ENERGY;
  }
  else if (omega < 0.0) {
    XLALPrintWarning("** LALSimIMRPSpinInspiralRD WARNING **: omega < 0  M: %12.4e, eta: %12.4e  om %12.6e\n",params->M, params->eta, omega);
    return LALSIMINSPIRAL_PHENSPIN_TEST_OMEGANONPOS;
  }
  else if (dvalues[1] < 0.0) {
    /* omegadot < 0 */
    return LALSIMINSPIRAL_PHENSPIN_TEST_OMEGADOT;
  }
  else if (isnan(omega)) {
    /* omega is nan */
    return LALSIMINSPIRAL_PHENSPIN_TEST_OMEGANAN;
  } 
  else if ( params->fEnd > 0. && params->fStart > params->fEnd && omega < params->fEnd) {
    /* freq. below bound in backward integration */
    return LALSIMINSPIRAL_PHENSPIN_TEST_FREQBOUND;
  }
  else if ( params->fEnd > params->fStart && omega > params->fEnd) {
    /* freq. above bound in forward integration */
    return LALSIMINSPIRAL_PHENSPIN_TEST_FREQBOUND;
  }
  else
    return GSL_SUCCESS;
} /* End of XLALSimSpinInspiralTest */


static int XLALSimIMRPhenSpinTest(UNUSED double t, const double values[], double dvalues[], void *mparams) {

  LALSimInspiralSpinTaylorT4Coeffs *params = (LALSimInspiralSpinTaylorT4Coeffs *) mparams;

  REAL8 omega   =   values[1];
  REAL8 energy  =  values[11];
  REAL8 denergy = dvalues[11];

  REAL8 LNhS1=(values[2]*values[5]+values[3]*values[6]+values[4]*values[7])/params->m1ByM/params->m1ByM;
  REAL8 LNhS2=(values[2]*values[8]+values[3]*values[9]+values[4]*values[10])/params->m2ByM/params->m2ByM;
  REAL8 S1sq =(values[5]*values[5]+values[6]*values[6]+values[7]*values[7])/pow(params->m1ByM,4);
  REAL8 S2sq =(values[8]*values[8]+values[9]*values[9]+values[10]*values[10])/pow(params->m2ByM,4);
  REAL8 S1S2 =(values[5]*values[8]+values[6]*values[9]+values[7]*values[10])/pow(params->m1ByM*params->m2ByM,2);
	
  REAL8 omegaMatch=OmMatch(LNhS1,LNhS2,S1sq,S1S2,S2sq)+0.006;

  if ( (energy > 0.0) || (( denergy > - 0.01*energy/params->dt*params->M )&&(energy<0.) ) ) {
    /*energy increase by more than 1%*/
    XLALPrintWarning("** LALSimIMRPSpinInspiralRD WARNING **: Energy increases dE %12.6e E %12.6e  M: %12.4e, eta: %12.4e  om %12.6e \n",denergy, energy, params->M, params->eta, omega);
    return LALSIMINSPIRAL_PHENSPIN_TEST_ENERGY;
  }
  else if (omega < 0.0) {
    XLALPrintWarning("** LALSimIMRPSpinInspiralRD WARNING **: omega < 0  M: %12.4e, eta: %12.4e  om %12.6e\n",params->M, params->eta, omega);
    return LALSIMINSPIRAL_PHENSPIN_TEST_OMEGANONPOS;
  }
  else if (dvalues[1] < 0.0) {
    /* omegadot < 0 */
    return LALSIMINSPIRAL_PHENSPIN_TEST_OMEGADOT;
  }
  else if (isnan(omega)) {
    /* omega is nan */
    return LALSIMINSPIRAL_PHENSPIN_TEST_OMEGANAN;
  } 
  else if ( params->fEnd > 0. && params->fStart > params->fEnd && omega < params->fEnd) {
    /* freq. below bound in backward integration */
    return LALSIMINSPIRAL_PHENSPIN_TEST_FREQBOUND;
  }
  else if ( params->fEnd > params->fStart && omega > params->fEnd) {
    /* freq. above bound in forward integration */
    return LALSIMINSPIRAL_PHENSPIN_TEST_FREQBOUND;
  }
  else if (omega>omegaMatch) {
    return LALSIMINSPIRAL_PHENSPIN_TEST_OMEGAMATCH;
  }
  else
    return GSL_SUCCESS;
} /* End of XLALSimIMRPhenSpinTest */

int XLALSimSpinInspiralFillL2Modes(COMPLEX16Vector *hL2,
				   REAL8 v,
				   REAL8 eta,
				   REAL8 dm,
				   REAL8 Psi,
				   REAL8 alpha,
				   LALSimInspiralInclAngle *an
				   )
{
  const int os=2;
  REAL8 amp20 = sqrt(1.5);
  REAL8 v2    = v*v;
  REAL8 damp  = 1.;

  hL2->data[2+os] = ( ( 1. - damp * v2 / 42. * (107. - 55. * eta) ) * 
		      ( cos(2.*(Psi+alpha)) * an->cHi4 + cos(2.*(Psi-alpha)) * an->sHi4 ) + 
		      v * dm/3.*an->si * ( cos(Psi-2.*alpha) * an->sHi2 + cos(Psi + 2.*alpha) * an->cHi2 ) );

  hL2->data[2+os]+=I*( ( 1. - damp * v2 / 42. * (107. - 55. * eta) ) * 
		       (-sin(2.*(Psi+alpha)) * an->cHi4 + sin(2.*(Psi-alpha)) * an->sHi4 ) +  
		       v * dm/3.*an->si * ( sin(Psi-2.*alpha) * an->sHi2 - sin(Psi + 2. * alpha) * an->cHi2 ) );

  hL2->data[-2+os] = ( ( 1. - damp * v2 / 42. * (107. - 55. * eta) ) *
		       ( cos(2. * (Psi + alpha)) * an->cHi4 + cos(2. * (Psi - alpha)) * an->sHi4 ) -
		       v * dm / 3. * an->si * ( cos(Psi - 2. * alpha) * an->sHi2 + cos(Psi + 2. * alpha) * an->cHi2 ) );

  hL2->data[-2+os]+=I*( ( 1. - damp * v2 / 42. * (107. - 55. * eta) ) *
			( sin(2.*(Psi + alpha))*an->cHi4 - sin(2.*(Psi-alpha)) * an->sHi4 ) +
			v*dm/3.*an->si * ( sin(Psi-2.*alpha) * an->sHi2 - sin(Psi+2.*alpha) * an->cHi2 ) );

  hL2->data[1+os] = an->si * ( ( 1. - damp * v2 / 42. * (107. - 55. * eta) ) *
			       ( -cos(2. * Psi - alpha) * an->sHi2 + cos(2. * Psi + alpha) * an->cHi2 ) +
			       v * dm / 3. * ( -cos(Psi + alpha) * (an->ci + an->cDi)/2. - cos(Psi - alpha) * an->sHi2 * (1. + 2. * an->ci) ) );

  hL2->data[1+os]+= an->si *I*( ( 1. - damp * v2 / 42. * (107. - 55. * eta) ) *
				( -sin(2.*Psi-alpha ) * an->sHi2 - sin(2.*Psi + alpha) * an->cHi2 ) +
				v * dm / 3. * (sin(Psi + alpha) * (an->ci + an->cDi)/2. - sin(Psi - alpha) * an->sHi2 * (1.+2.*an->ci) ) );

  hL2->data[-1+os] = an->si * ( ( 1. - damp * v2 / 42. * (107. - 55. * eta) ) * 
				( cos(2.*Psi-alpha) * an->sHi2 - cos(2.*Psi+alpha)*an->cHi2) +
				v * dm / 3. * ( -cos(Psi + alpha) * (an->ci + an->cDi)/2. - cos(Psi - alpha) * an->sHi2 * (1. + 2. * an->ci) ) );

  hL2->data[-1+os]+= an->si *I*( ( 1. - damp * v2 / 42. * (107. - 55. * eta) ) * 
				 ( -sin(2. * Psi - alpha) * an->sHi2 - sin(2. * Psi + alpha) * an->cHi2 ) -
				 v * dm / 3. * ( sin(Psi + alpha) * (an->ci + an->cDi)/2. - sin(Psi - alpha) * an->sHi2 * (1. + 2. * an->ci) ) );

  hL2->data[os] = amp20 * ( an->si2*( 1.- damp*v2/42.*(107.-55.*eta) )*cos(2.*Psi) + I*v*dm/3.*an->sDi*sin(Psi) );

  return XLAL_SUCCESS;
} /* End of XLALSimSpinInspiralFillL2Modes*/

int XLALSimSpinInspiralFillL3Modes(COMPLEX16Vector *hL3,
				   REAL8 v,
				   REAL8 eta,
				   REAL8 dm,
				   REAL8 Psi,
				   REAL8 alpha,
				   LALSimInspiralInclAngle *an)
{
  const int os=3;
  REAL8 amp32 = sqrt(1.5);
  REAL8 amp31 = sqrt(0.15);
  REAL8 amp30 = 1. / sqrt(5)/2.;
  REAL8 v2    = v*v;

  hL3->data[3+os] = (v * dm * (-9.*cos(3.*(Psi-alpha))*an->sHi6 - cos(Psi-3.*alpha)*an->sHi4*an->cHi2 + cos(Psi+3.*alpha)*an->sHi2*an->cHi4 + 9.*cos(3.*(Psi+alpha))*an->cHi6) +
		     v2 * 4. * an->si *(1.-3.*eta)* ( -cos(2.*Psi-3.*alpha)*an->sHi4 + cos(2.*Psi+3.*alpha)*an->cHi4) );

  hL3->data[3+os]+= I*(v * dm * (-9.*sin(3.*(Psi-alpha))*an->sHi6 - sin(Psi-3.*alpha)*an->sHi4*an->cHi2 - sin(Psi+3.*alpha)*an->sHi2*an->cHi4 - 9.*sin(3.*(Psi+alpha))* an->cHi6) +
		       v2 * 4. * an->si *(1.-3.*eta)* ( -sin(2.*Psi-3.*alpha)*an->sHi4 -sin(2.*Psi+3.*alpha)*an->cHi4) );
  
  hL3->data[-3+os] = (-v * dm * (-9.*cos(3.*(Psi-alpha))*an->sHi6 - cos(Psi-3.*alpha)*an->sHi4*an->cHi2 + cos(Psi+3.*alpha)*an->sHi2*an->cHi4 + 9.*cos(3.*(Psi+alpha))*an->cHi6) +
		      v2 * 4. * an->si *(1.-3.*eta)*( -cos(2.*Psi-3.*alpha)*an->sHi4 + cos(2.*Psi+3.*alpha)*an->cHi4) );

  hL3->data[-3+os]+=I*(v * dm *(-9.*sin(3.*(Psi-alpha))*an->sHi6 - sin(Psi-3.*alpha)*an->sHi4*an->cHi2 - sin(Psi+3.*alpha)*an->sHi2*an->cHi4 - 9.*sin(3.*(Psi+alpha))* an->cHi6) -
		       v2 * 4. * an->si * (1.-3.*eta)*( -sin(2.*Psi-3.*alpha)*an->sHi4 - sin(2.*Psi+3.*alpha)*an->cHi4 ) );

  hL3->data[2+os] = amp32 * ( v * dm/3. * (27.*cos(3.*Psi-2.*alpha)*an->si*an->sHi4 + 27.*cos(3.*Psi+2.*alpha)*an->si*an->cHi4 + cos(Psi+2.*alpha)*an->cHi3*(5.*an->sHi-3.*an->si*an->cHi-3.*an->ci*an->sHi) /2. + cos(Psi-2.*alpha)*an->sHi3*(5.*an->cHi+3.*an->ci*an->cHi-3.*an->si*an->sHi) /2. ) +
			      v2*(1./3.-eta) * (-8.*an->cHi4*(3.*an->ci-2.)*cos(2.*(Psi+alpha)) + 8.*an->sHi4*(3.*an->ci+2.)*cos(2.*(Psi-alpha)) ) );

  hL3->data[2+os]+= amp32*I*( v * dm/3. * ( 27.*sin(3.*Psi-2.*alpha)*an->si*an->sHi4 - 27.*cos(3.*Psi+2.*alpha)*an->si*an->cHi4 - sin(Psi+2.*alpha)*an->cHi3*(5.*an->sHi-3.*an->si*an->cHi-3.*an->ci*an->sHi) /2. + sin(Psi-2.*alpha)*an->sHi3*(5.*an->cHi+3.*an->ci*an->cHi-3.*an->si*an->sHi)/2. ) +
			      v2*(1./3.-eta) * ( 8.*an->cHi4*(3.*an->ci-2.)*sin(2.*(Psi+alpha)) + 8.*an->sHi4*(3.*an->ci+2.)*sin(2.*(Psi-alpha)) ) );

  hL3->data[-2+os] = amp32 * ( v * dm/3. * (27.*cos(3.*Psi-2.*alpha)*an->si*an->sHi4 + 27.*cos(3.*Psi+2.*alpha)*an->si*an->cHi4 + cos(Psi+2.*alpha)*an->cHi3*(5.*an->sHi-3.*an->si*an->cHi-3.*an->ci*an->sHi) /2. + cos(Psi-2.*alpha)*an->sHi3*(5.*an->cHi+3.*an->ci*an->cHi-3.*an->si*an->sHi) /2. ) - 
			       v2*(1./3.-eta) * ( 8.*an->cHi4*(3.*an->ci-2.)*cos(2.*(Psi+alpha)) - 8.*an->sHi4*(3.*an->ci+2.)*cos(2.*(Psi-alpha)) ) );

  hL3->data[-2+os]+= amp32*I*(-v * dm/3. * (27.*sin(3.*Psi-2.*alpha)*an->si*an->sHi4 - 27.*cos(3.*Psi+2.*alpha)*an->si*an->cHi4 - sin(Psi+2.*alpha)*an->cHi3*(5.*an->sHi-3.*an->si*an->cHi-3.*an->ci*an->sHi) /2.+ sin(Psi-2.*alpha)*an->sHi3*(5.*an->cHi+3.*an->ci*an->cHi-3.*an->si*an->sHi) /2.) +
			      v2*(1./3.-eta) * (8.*an->cHi4*(3.*an->ci-2.)*sin(2.*(Psi+alpha)) + 8.*an->sHi4*(3.*an->ci+2.)*sin(2.*(Psi-alpha)) ) );

  hL3->data[1+os] = amp31 * ( v * dm/6. * ( -135.*cos(3.*Psi-alpha)*an->sHi*an->sHi2 + 135.*cos(3.*Psi+alpha)*an->sHi*an->cHi2 + cos(Psi+alpha)*an->cHi2*(15.*an->cDi-20.*an->ci+13.)/2. - cos(Psi-alpha)*an->sHi2*(15.*an->cDi+20.*an->ci+13.)/2.)
			      + v2*(1./3.-eta) * ( 20.*an->cHi3*cos(2.*Psi+alpha)*(3.*(an->sHi*an->ci+an->cHi*an->si)-5.*an->sHi) + 20.*an->sHi3*cos(2.*Psi-alpha)*(3.*(an->cHi2*an->ci-an->sHi*an->si)+5.*an->cHi) ) );

  hL3->data[1+os]+= amp31*I*(-v * dm/6. * ( -135.*cos(3.*Psi-alpha)*an->si2*an->sHi2 + 135.*cos(3.*Psi+alpha)*an->si2*an->cHi2 + cos(Psi+alpha)*an->cHi2*(15.*an->cDi-20.*an->ci+13.)/2. - cos(Psi-alpha)*an->sHi2*(15.*an->cDi+20.*an->ci+13.)/2. )
			     - v2*(1./3.-eta) * ( 20.*an->cHi3*cos(2.*Psi+alpha)*(3.*(an->sHi*an->ci+an->cHi*an->si)-5.*an->sHi) + 20.*an->sHi3*cos(2.*Psi-alpha)*(3.*(an->cHi*an->ci-an->sHi*an->si)+5.*an->cHi) ) );

  hL3->data[-1+os] = amp31 * (-v * dm/6. * ( -135.*cos(3.*Psi-alpha)*an->si2*an->sHi2 + 135.*cos(3.*Psi+alpha)*an->si2*an->cHi2 + cos(Psi+alpha)*an->cHi2*(15.*an->cDi-20.*an->ci+13.)/2.- cos(Psi-alpha) * an->sHi2*(15.*an->cDi+20.*an->ci+13.)/2. ) -
			      v2 * (1./3.-eta)* ( 20.*an->cHi3*cos(2.*Psi+alpha)*(3.*(an->sHi*an->ci+an->cHi*an->si)-5.*an->sHi) + 20.*an->sHi3*cos(2.*Psi-alpha)*(3.*(an->cHi*an->ci-an->sHi*an->si)+5.*an->cHi) ) );

  hL3->data[-1+os]+= amp31*I*(v * dm/6. * ( -135.*sin(3.*Psi-alpha)*an->si2*an->sHi2 - 135.*sin(3.*Psi+alpha)*an->si2*an->cHi2 - sin(Psi+alpha)*an->cHi2*(15.*an->cDi-20.*an->ci+13.)/2. - sin(Psi-alpha)*an->sHi2*(15.*an->cDi+20.*an->ci+13.)/2.) 
			      -v2 * (1./3.-eta)* ( 20.*an->cHi3*sin(2.*Psi+alpha)*(3.*(an->sHi*an->ci+an->ci2*an->si)-5.*an->si2) - 20.*an->sHi3*sin(2.*Psi-alpha)*(3.*(an->ci2*an->ci-an->si2*an->si)+5.*an->ci2) ) );

  hL3->data[os] = amp30 * I * ( v * dm * ( cos(Psi)*an->si*(cos(2.*Psi)*(45.*an->si2)-(25.*an->cDi-21.) ) ) +
				v2*(1.-3.*eta) * (80.*an->si2*an->cHi*sin(2.*Psi) ) );

  return XLAL_SUCCESS;

} /*End of XLALSimSpinInspiralFillL3Modes*/


int XLALSimSpinInspiralFillL4Modes(COMPLEX16Vector *hL4,
				   UNUSED REAL8 v,
				   REAL8 eta,
				   UNUSED REAL8 dm,
				   REAL8 Psi,
				   REAL8 alpha,
				   LALSimInspiralInclAngle *an
				   )
{
  const int os=4;
  REAL8 amp43 = - sqrt(2.);
  REAL8 amp42 = sqrt(7.)/2.;
  REAL8 amp41 = sqrt(3.5)/4.;
  REAL8 amp40 = sqrt(17.5)/16.;
	
  hL4->data[4+os] = (1. - 3.*eta) * ( 4.*an->sHi8*cos(4.*(Psi-alpha)) + cos(2.*Psi-4.*alpha)*an->sHi6*an->cHi2 + an->sHi2*an->cHi6*cos(2.*Psi+4.*alpha) + 4.*an->cHi8*cos(4.*(Psi+alpha)) );

  hL4->data[4+os]+= (1. - 3.*eta)*I*( 4.*an->sHi8*sin(4.*(Psi-alpha)) + sin(2.*Psi-4.*alpha)*an->sHi6*an->cHi2 - an->sHi2*an->cHi6*sin(2.*Psi+4.*alpha) - 4.*an->cHi8*sin(4.*(Psi+alpha)) );

  hL4->data[-4+os] = (1. - 3.*eta) * (4.*an->sHi8*cos(4.*(Psi-alpha)) + cos(2.*Psi-4.*alpha)*an->sHi6*an->cHi2 + an->sHi2*an->cHi6*cos(2.*Psi+4.*alpha) + 4.*an->cHi8*cos(4.*(Psi+alpha) ) );

  hL4->data[-4+os]+=-(1. - 3.*eta) *I*(4.*an->sHi8*sin(4.*(Psi-alpha)) + sin(2*Psi-4.*alpha)*an->sHi6*an->cHi2 - an->sHi2*an->cHi6*sin(2.*Psi+4.*alpha) - 4.*an->cHi8*sin(4.*(Psi+alpha)) );

  hL4->data[3+os] = amp43 * (1. - 3.*eta) * an->si * ( 4.*an->sHi6*cos(4.*Psi-3.*alpha) - 4.*an->cHi6*cos(4.*Psi+3.*alpha) - an->sHi4*(an->ci+0.5)/2.*cos(2.*Psi-3.*alpha) + an->cHi4*(an->ci-0.5)*cos(2.*Psi+3.*alpha) ); /****/

  hL4->data[3+os]+= amp43*I*(1. - 3.*eta) * an->si * ( 4.*an->sHi6*sin(4.*Psi-3.*alpha) + 4.*an->cHi6*sin(4.*Psi+3.*alpha) - an->sHi4*(an->ci+0.5)/2.*sin(2.*Psi-3.*alpha) + an->cHi4*(an->ci-0.5)*sin(2.*Psi+3.*alpha) ); /****/

  hL4->data[-3+os] = -amp43 * (1. - 3.*eta) * an->si * ( 4.*an->sHi6*cos(4.*Psi-3.*alpha) - 4.*an->cHi6*cos(4.*Psi+3.*alpha) - an->sHi4*(an->ci+0.5)/2.*cos(2.*Psi-3.*alpha) + an->cHi4*(an->ci-0.5)*cos(2.*Psi+3.*alpha) ); /****/

  hL4->data[-3+os]+= amp43*I*(1. - 3.*eta) * an->si * ( 4.*an->sHi6*sin(4.*Psi-3.*alpha) + 4.*an->cHi6*sin(4.*Psi+3.*alpha) - an->sHi4*(an->ci+0.5)/2.*sin(2.*Psi-3.*alpha) + an->cHi4*(an->ci-0.5)*sin(2.*Psi+3.*alpha) ); /****/

  hL4->data[2+os] = amp42 * (1. - 3.*eta) * ( 16.*an->sHi6*an->cHi2*cos(4.*Psi-2.*alpha) + 16.*an->cHi6*an->sHi2*cos(4.*Psi+2.*alpha) - an->cHi4*cos(2.*(Psi+alpha))*(an->cDi-2.*an->ci+9./7.)/2. - an->sHi4*cos(2.*(Psi-alpha))*(an->cDi+2.*an->ci+9./7.)/2. );

  hL4->data[2+os]+= amp42 *I*(1. - 3.*eta) * ( 16.*an->sHi6*an->cHi2 * sin(4.*Psi-2.*alpha) - 16.*an->cHi6*an->sHi2*sin(4.*Psi+2.*alpha) + an->cHi4*sin(2.*(Psi+alpha))*(an->cDi-2.*an->ci+9./7.)/2. - an->sHi4*sin(2.*(Psi-alpha))*(an->cDi+2.*an->ci+9./7.)/2. );

  hL4->data[-2+os] = amp42 * (1. - 3.*eta) * ( 16.*an->sHi6*an->cHi2*cos(4.*Psi-2.*alpha) + 16.*an->cHi6*an->sHi2*cos(4.*Psi+2.*alpha) - an->cHi4*cos(2.*(Psi+alpha))*(an->cDi-2.*an->ci+9./7.)/2. - an->sHi4*cos(2.*(Psi-alpha))*(an->cDi+2.*an->ci+9./7.)/2. );

  hL4->data[-2+os]+=-amp42 *I*(1. - 3.*eta) * ( 16.*an->sHi6*an->cHi2*sin(4.*Psi-2.*alpha) - 16.*an->cHi6*an->sHi2*sin(4.*Psi+2.*alpha) + an->cHi4*sin(2.*(Psi+alpha))*(an->cDi-2.*an->ci+9./7.)/2. - an->sHi4*sin(2.*(Psi-alpha))*(an->cDi+2.*an->ci+9./7.)/2. );

  hL4->data[1+os] = amp41 * (1. - 3.*eta) * ( -64.*an->sHi5*an->cHi3*cos(4.*Psi-alpha) + 64.*an->sHi3*an->cHi5*cos(4.*Psi+alpha) - an->sHi3*cos(2.*Psi-alpha)*((an->cDi*an->cHi-an->sDi*an->sHi) + 2.*(an->cHi*an->ci-an->sHi*an->si) + 19./7.*an->cHi) + an->cHi3*cos(2.*Psi+alpha)*((an->cDi*an->sHi+an->sDi*an->cHi) - 2.*(an->si*an->cHi+an->ci*an->si2) +19./7.*an->cHi) );

  hL4->data[1+os]+= amp41*I*(1. - 3.*eta) * ( -64.*an->sHi5*an->cHi3 * sin(4.*Psi-alpha) - 64.*an->sHi3*an->cHi5 * sin(4.*Psi+alpha) - an->sHi3*sin(2.*Psi-alpha)*((an->cDi*an->cHi-an->sDi*an->sHi) + 2.*(an->cHi*an->ci-an->sHi*an->si) + 19./7.*an->cHi) - an->cHi3*sin(2.*Psi+alpha)*((an->cDi*an->sHi+an->sDi*an->cHi) - 2.*(an->si*an->cHi+an->ci*an->sHi) + 19./7.*an->cHi) );

  hL4->data[-1+os] = -amp41 * (1. - 3.*eta) * ( -64*an->sHi5*an->cHi3 * cos(4.*Psi-alpha) + 64.*an->sHi3*an->cHi5*cos(4.*Psi+alpha) - an->sHi3*cos(2.*Psi-alpha)*((an->cDi*an->cHi-an->sDi*an->sHi) + 2.*(an->cHi*an->ci-an->sHi*an->si) + 19./7.*an->ci2) + an->cHi3*cos(2.*Psi+alpha)*((an->cDi*an->sHi+an->sDi*an->cHi) - 2.*(an->si*an->cHi+an->ci*an->sHi) + 19./7.*an->ci2) );

  hL4->data[-1+os]+= amp41 *I*(1. - 3.*eta) *I*( -64.*an->sHi5*an->cHi3 * sin(4.*Psi-alpha) - 64.*an->sHi3*an->cHi5 * sin(4.*Psi+alpha) - an->sHi3*sin(2.*Psi-alpha)*((an->cDi*an->cHi-an->sDi*an->sHi) + 2.*(an->cHi*an->ci-an->sHi*an->si) + 19./7.*an->ci2) - an->cHi3*sin(2.*Psi+alpha)*((an->cDi*an->sHi+an->sDi*an->cHi) - 2.*(an->si*an->cHi+an->ci*an->sHi) + 19./7.*an->cHi) );

  hL4->data[os] = amp40 * (1.-3.*eta) * an->si2 * (8.*an->si2*cos(4.*Psi) + cos(2.*Psi)*(an->cDi+5./7.) );
	
  return XLAL_SUCCESS;
} /* End of XLALSimSpinInspiralFillL4Modes*/

static int XLALSimInspiralSpinTaylorT4Engine(REAL8TimeSeries **omega,      /**< post-Newtonian parameter [returned]*/
					     REAL8TimeSeries **Phi,        /**< orbital phase            [returned]*/
					     REAL8TimeSeries **LNhatx,	   /**< LNhat vector x component [returned]*/
					     REAL8TimeSeries **LNhaty,	   /**< "    "    "  y component [returned]*/
					     REAL8TimeSeries **LNhatz,	   /**< "    "    "  z component [returned]*/
					     REAL8TimeSeries **S1x,	   /**< Spin1 vector x component [returned]*/
					     REAL8TimeSeries **S1y,	   /**< "    "    "  y component [returned]*/
					     REAL8TimeSeries **S1z,	   /**< "    "    "  z component [returned]*/
					     REAL8TimeSeries **S2x,	   /**< Spin2 vector x component [returned]*/
					     REAL8TimeSeries **S2y,	   /**< "    "    "  y component [returned]*/
					     REAL8TimeSeries **S2z,	   /**< "    "    "  z component [returned]*/
					     REAL8TimeSeries **Energy,     /**< Energy                   [returned]*/
					     const INT4  sign,             /** sign >(<)0 for forward (backward) integration */
					     const UINT4 lengthH,
					     const UINT4 offset,
					     const REAL8 yinit[],
					     const Approximant approx,     /** Allow to choose w/o ringdown */
					     LALSimInspiralSpinTaylorT4Coeffs *params
					     )
{
  UINT4 idx;
  int jdx;
  UINT4 intLen;
  int intReturn;

  REAL8 S1x0,S1y0,S1z0,S2x0,S2y0,S2z0;  /** Used to store initial spin values */
  REAL8Array *yout;	                /** Used to store integration output */

  ark4GSLIntegrator *integrator;

  /* allocate the integrator */
  if (approx == PhenSpinTaylor)
    integrator = XLALAdaptiveRungeKutta4Init(LAL_NUM_PST4_VARIABLES,XLALSpinInspiralDerivatives,XLALSimSpinInspiralTest,LAL_PST4_ABSOLUTE_TOLERANCE,LAL_PST4_RELATIVE_TOLERANCE);
  else
    integrator = XLALAdaptiveRungeKutta4Init(LAL_NUM_PST4_VARIABLES,XLALSpinInspiralDerivatives,XLALSimIMRPhenSpinTest,LAL_PST4_ABSOLUTE_TOLERANCE,LAL_PST4_RELATIVE_TOLERANCE);

  if (!integrator) {
#if DEBUG_LEVEL
    fprintf(stderr,"**** LALSimIMRPSpinInspiralRD ERROR ****: Cannot allocate adaptive integrator.\n");
#endif
    XLALPrintError("XLAL Error - %s: Cannot allocate integrator\n", __func__);
    XLAL_ERROR(XLAL_EFUNC);
  }

  /* stop the integration only when the test is true */
  integrator->stopontestonly = 1;

  REAL8 *yin = (REAL8 *) LALMalloc(sizeof(REAL8) * LAL_NUM_PST4_VARIABLES);
  for (idx=0; idx<LAL_NUM_PST4_VARIABLES; idx++) yin[idx]=yinit[idx];
  S1x0=yinit[5];
  S1y0=yinit[6];
  S1z0=yinit[7];
  S2x0=yinit[8];
  S2y0=yinit[9];
  S2z0=yinit[10];

  REAL8 length=((REAL8)lengthH)*params->dt/params->M;

  intLen    = XLALAdaptiveRungeKutta4Hermite(integrator,(void *)params,yin,0.0,length,params->dt/params->M,&yout);
  intReturn = integrator->returncode;
  XLALAdaptiveRungeKutta4Free(integrator);

  if (intReturn == XLAL_FAILURE) {
#if DEBUG_LEVEL
    fprintf(stderr,"** LALSimIMRPSpinInspiralRD Error **: Adaptive Integrator\n");
#endif
    XLALPrintError("** LALSimIMRPSpinInspiralRD Error **: Adaptive Integrator\n");
    XLAL_ERROR(XLAL_EFUNC);
  }
  /* End integration*/

  /* Start of the integration checks*/
  if (intLen<minIntLen) {
#if DEBUG_LEVEL
    fprintf(stderr,"** LALSimIMRPSpinInspiralRD ERROR **: integration too short! intReturnCode %d, integration length %d\n",intReturn,intLen);
#endif
    XLALPrintError("** LALSimIMRPSpinInspiralRD ERROR **: integration too short! intReturnCode %d, integration length %d\n",intReturn,intLen);
    if (XLALClearErrno() == XLAL_ENOMEM) {
      XLAL_ERROR(  XLAL_ENOMEM);
    } else {
      XLAL_ERROR( XLAL_EFAILED);
    }
  }

  /* if we have enough space, store variables; otherwise abort */
  UINT4 totLen=intLen+offset;
  if ( totLen >= lengthH ) {
#if DEBUG_LEVEL
    fprintf(stderr,"**** LALPSpinInspiralRD ERROR ****: no space to write in waveforms: %d vs. %d\n",totLen,lengthH);
    fprintf(stderr,"                     m:           : %12.5f  %12.5f\n",params->m1ByM*params->M,params->m2ByM*params->M);
    fprintf(stderr,"              S1:                 : %12.5f  %12.5f  %12.5f\n",S1x0,S1y0,S1z0);
    fprintf(stderr,"              S2:                 : %12.5f  %12.5f  %12.5f\n",S2x0,S2y0,S2z0);
#endif
    XLALPrintError("**** LALPSpinInspiralRD ERROR ****: no space to write in waveforms: %d vs. %d\n",totLen,lengthH);
    XLALPrintError("                     m:           : %12.5f  %12.5f\n",params->m1ByM*params->M,params->m2ByM*params->M);
    XLALPrintError("              S1:                 : %12.5f  %12.5f  %12.5f\n",S1x0,S1y0,S1z0);
    XLALPrintError("              S2:                 : %12.5f  %12.5f  %12.5f\n",S2x0,S2y0,S2z0);
    XLAL_ERROR(XLAL_ESIZE);
  }
  /* End of integration checks*/

  const LIGOTimeGPS tStart=LIGOTIMEGPSZERO;
  *omega  = XLALCreateREAL8TimeSeries( "OMEGA", &tStart, 0., params->dt, &lalDimensionlessUnit, intLen);
  *Phi    = XLALCreateREAL8TimeSeries( "ORBITAL_PHASE", &tStart, 0., params->dt, &lalDimensionlessUnit, intLen); 
  *LNhatx = XLALCreateREAL8TimeSeries( "LNHAT_X_COMPONENT", &tStart, 0., params->dt, &lalDimensionlessUnit, intLen); 
  *LNhaty = XLALCreateREAL8TimeSeries( "LNHAT_Y_COMPONENT", &tStart, 0., params->dt, &lalDimensionlessUnit, intLen);
  *LNhatz = XLALCreateREAL8TimeSeries( "LNHAT_Z_COMPONENT", &tStart, 0., params->dt, &lalDimensionlessUnit, intLen);
  *S1x    = XLALCreateREAL8TimeSeries( "SPIN1_X_COMPONENT", &tStart, 0., params->dt, &lalDimensionlessUnit, intLen);
  *S1y    = XLALCreateREAL8TimeSeries( "SPIN1_Y_COMPONENT", &tStart, 0., params->dt, &lalDimensionlessUnit, intLen);
  *S1z    = XLALCreateREAL8TimeSeries( "SPIN1_Z_COMPONENT", &tStart, 0., params->dt, &lalDimensionlessUnit, intLen);
  *S2x    = XLALCreateREAL8TimeSeries( "SPIN2_X_COMPONENT", &tStart, 0., params->dt, &lalDimensionlessUnit, intLen);
  *S2y    = XLALCreateREAL8TimeSeries( "SPIN2_Y_COMPONENT", &tStart, 0., params->dt, &lalDimensionlessUnit, intLen); 
  *S2z    = XLALCreateREAL8TimeSeries( "SPIN2_Z_COMPONENT", &tStart, 0., params->dt, &lalDimensionlessUnit, intLen);
  *Energy = XLALCreateREAL8TimeSeries( "LNHAT_Z_COMPONENT", &tStart, 0., params->dt, &lalDimensionlessUnit, intLen);
  if ( !omega || !Phi || !S1x || !S1y || !S1z || !S2x || !S2y || !S2z || !LNhatx || !LNhaty || !LNhatz || !Energy ) {
#if DEBUG_LEVEL
    fprintf(stderr,"*** LALSimIMRPSpinInspiralRD ***: Error in allocating memory for dynamical variables\n");
#endif
    XLALDestroyREAL8Array(yout);
    XLAL_ERROR(XLAL_EFUNC);
  }

  /* Copy dynamical variables from yout array to output time series.
   * Note the first 'len' members of yout are the time steps. 
   */

  jdx = (intLen-1)*(-sign+1)/2;
  int jEnd = intLen*(sign+1)/2;

  do {
    (*Phi)->data->data[jdx+offset]  = yout->data[intLen+jdx];
    (*omega)->data->data[jdx+offset]  = yout->data[2*intLen+jdx];
    (*LNhatx)->data->data[jdx+offset] = yout->data[3*intLen+jdx];
    (*LNhaty)->data->data[jdx+offset] = yout->data[4*intLen+jdx];
    (*LNhatz)->data->data[jdx+offset] = yout->data[5*intLen+jdx];
    (*S1x)->data->data[jdx+offset]    = yout->data[6*intLen+jdx];
    (*S1y)->data->data[jdx+offset]    = yout->data[7*intLen+jdx];
    (*S1z)->data->data[jdx+offset]    = yout->data[8*intLen+jdx];
    (*S2x)->data->data[jdx+offset]    = yout->data[9*intLen+jdx];
    (*S2y)->data->data[jdx+offset]    = yout->data[10*intLen+jdx];
    (*S2z)->data->data[jdx+offset]    = yout->data[11*intLen+jdx];
    (*Energy)->data->data[jdx+offset] = yout->data[12*intLen+jdx];
    jdx+=sign;
  }
  while (jdx!=jEnd);

  XLALDestroyREAL8Array(yout);
  return intReturn;
} /* End of XLALSimInspiralSpinTaylorT4Engine */

int XLALSimInspiralComputeInclAngle(REAL8 ciota, LALSimInspiralInclAngle *angle){
  angle->ci=ciota;
  angle->si=sqrt(1.-ciota*ciota);
  angle->ci2=angle->ci*angle->ci;
  angle->si2=angle->si*angle->si;
  angle->cDi=angle->ci*angle->ci-angle->si*angle->si;
  angle->sDi=2.*angle->ci*angle->si;
  angle->cHi=sqrt((1.+angle->ci)/2.);
  angle->sHi=sqrt((1.-angle->ci)/2.);
  angle->cHi2=(1.+angle->ci)/2.;
  angle->sHi2=(1.-angle->ci)/2.;
  angle->cHi3=angle->cHi*angle->cHi2;
  angle->sHi3=angle->sHi*angle->sHi2;
  angle->cHi4=angle->cHi2*angle->cHi2;
  angle->sHi4=angle->sHi2*angle->sHi2;
  angle->cHi6=angle->cHi2*angle->cHi4;
  angle->sHi6=angle->sHi2*angle->sHi4;
  angle->cHi8=angle->cHi4*angle->cHi4;
  angle->sHi8=angle->sHi4*angle->sHi4;

  return XLAL_SUCCESS;

} /* End of XLALSimInspiralComputeInclAngle*/

/** The following lines are necessary in the case L is initially parallel to 
 * N so that alpha is undefined at the beginning but different from zero at the first 
 * step (this happens if the spins are not aligned with L). 
 * Such a discontinuity of alpha would induce 
 * a discontinuity of the waveform between its initial value and its value after the
 * first integration step. This does not happen during the integration as in that 
 * case alpha can be safely set to the previous value, just before L becomes parallel 
 * to N. In the case L stays all the time parallel to N than alpha can be 
 * safely set to zero, as it is.
 **/
 
static int XLALSimInspiralComputeAlpha(REAL8 mass1, REAL8 mass2, REAL8 LNhx, REAL8 LNhy, REAL8 S1x, REAL8 S1y, REAL8 S2x, REAL8 S2y,REAL8 *alpha){
  if ((LNhy*LNhy+LNhx*LNhx)==0.) {
    REAL8 S1xy=S1x*S1x+S1y*S1y;
    REAL8 S2xy=S2x*S2x+S2y*S2y;
    if ((S1xy+S2xy)==0.) {
      *alpha=0.;
    }
    else {
      REAL8 Mass=mass1+mass2;
      REAL8 c1=(2.5*mass1*mass2+1.5*mass2*mass2)/Mass/Mass;
      REAL8 c2=(2.5*mass1*mass2+1.5*mass1*mass1)/Mass/Mass; /* Dq qui*/
      *alpha=atan2(-c1*S1x-c2*S2x,c1*S1y+c2*S2y);
    }
  }
  return XLAL_SUCCESS;
} /*End of XLALSimInspiralComputeAlpha*/

/** Here we use the following convention:
 *  the coordinates of the spin vectors spin1,2 and the inclination 
 *  variable refers to different physical parameters according to the value of 
 *  axisChoice:
 *  * LAL_SIM_INSPIRAL_FRAME_AXIS_ORBITAL_L: inclination denotes the angle 
 *            between the view direction N and the initial L 
 *            (initial L//z, N in the x-z plane) and the spin 
 * 	      coordinates are given with respect to initial L.
 *  * LAL_SIM_INSPIRAL_FRAME_AXIS_TOTAL_J:   inclination denotes the angle 
 *            between the view direction and J (J is constant during the 
 *            evolution, J//z, both N and initial L are in the x-z plane) 
 *            and the spin coordinates are given wrt initial L.
 *  * LAL_SIM_INSPIRAL_FRAME_AXIS_VIEW:     inclination denotes the angle 
 *            between the initial L and N (N//z, initial L in the x-z plane)
 *            and the spin coordinates are given with respect to N.
 *
 *   In order to reproduce the results of the SpinTaylor code View must be chosen.
 *   The spin magnitude are normalized to the individual mass^2, i.e.
 *   they are dimension-less.
 *   The modulus of the initial angular momentum is fixed by m1,m2 and
 *   initial frequency.
 *   The polarization angle is not used here, it enters the pattern
 *   functions along with the angles marking the sky position of the
 *   source.
 **/

/*static void rotateX(REAL8 phi,REAL8 *vx, REAL8 *vy, REAL8 *vz){
  REAL8 tmp[3]={*vx,*vy,*vz};
  *vx=*vy=*vz=0.;
  REAL8 rotX[3][3]={{1.,0.,0.},{0,cos(phi),-sin(phi)},{0,sin(phi),cos(phi)}};
  int idx;
  for (idx=0;idx<3;idx++) {
    *vx+=rotX[0][idx]*tmp[idx];
    *vy+=rotX[1][idx]*tmp[idx];
    *vz+=rotX[2][idx]*tmp[idx];
  }
  }*/
static void rotateY(REAL8 phi,REAL8 *vx, REAL8 *vy, REAL8 *vz){
  REAL8 rotY[3][3]={{cos(phi),0.,sin(phi)},{0.,1.,0.},{-sin(phi),0.,cos(phi)}};
  REAL8 tmp[3]={*vx,*vy,*vz};
  *vx=*vy=*vz=0.;
  int idx;
  for (idx=0;idx<3;idx++) {
    *vx+=rotY[0][idx]*tmp[idx];
    *vy+=rotY[1][idx]*tmp[idx];
    *vz+=rotY[2][idx]*tmp[idx];
  }
}
static void rotateZ(REAL8 phi,REAL8 *vx, REAL8 *vy, REAL8 *vz){
  REAL8 tmp[3]={*vx,*vy,*vz};
  REAL8 rotZ[3][3]={{cos(phi),-sin(phi),0.},{sin(phi),cos(phi),0.},{0.,0.,1.}};
  *vx=*vy=*vz=0.;
  int idx;
  for (idx=0;idx<3;idx++) {
    *vx+=rotZ[0][idx]*tmp[idx];
    *vy+=rotZ[1][idx]*tmp[idx];
    *vz+=rotZ[2][idx]*tmp[idx];
  }
}

static int XLALSimIMRPhenSpinInspiralSetAxis(REAL8 mass1, /* in MSun units */
					     REAL8 mass2, /* in MSun units */
					     REAL8 *iota, /* input/output */
					     REAL8 *yinit,/* RETURNED */
					     LALSimInspiralFrameAxis axisChoice)
{
  // Magnitude of the Newtonian orbital angular momentum
  REAL8 omega=yinit[1];
  REAL8 Mass=mass1+mass2;
  REAL8 Lmag = mass1*mass2 / cbrt(omega);
  REAL8 Jmag;
  REAL8 S1[3],S2[3],J[3],LNh[3],N[3];
  REAL8 inc;
  REAL8 phiJ,thetaJ,phiN;

  // Physical values of the spins
  inc=*iota;
  S1[0] =  yinit[5] * mass1 * mass1;
  S1[1] =  yinit[6] * mass1 * mass1;
  S1[2] =  yinit[7] * mass1 * mass1;
  S2[0] =  yinit[8] * mass2 * mass2;
  S2[1] =  yinit[9] * mass2 * mass2;
  S2[2] = yinit[10] * mass2 * mass2;

  switch (axisChoice) {

  case LAL_SIM_INSPIRAL_FRAME_AXIS_ORBITAL_L:
#if DEBUG_LEVEL
    fprintf(stdout,"  *** OrbitalL, cos(inc) %8.4f\n",cos(inc));
#endif
    J[0]=S1[0]+S2[0];
    J[1]=S1[1]+S2[1];
    J[2]=S1[2]+S2[2]+Lmag;
    N[0]=sin(inc);
    N[1]=0.;
    N[2]=cos(inc);
    LNh[0]=0.;
    LNh[1]=0.;
    LNh[2]=1.;
    Jmag=sqrt(J[0]*J[0]+J[1]*J[1]+J[2]*J[2]);
    if (Jmag>0.) phiJ=atan2(J[1],J[0]);
    else phiJ=0.;
    thetaJ=acos(J[2]/Jmag);
    rotateZ(-phiJ,&N[0],&N[1],&N[2]);
    rotateY(-thetaJ,&N[0],&N[1],&N[2]);
    break;

  case LAL_SIM_INSPIRAL_FRAME_AXIS_TOTAL_J:
#if DEBUG_LEVEL
    fprintf(stdout,"  *** TotalJ, cos(inc) %8.4f\n",cos(inc));
#endif
    J[0]=S1[0]+S2[0];
    J[1]=S1[1]+S2[1];
    J[2]=S1[2]+S2[2]+Lmag;
    LNh[0]=0.;
    LNh[1]=0.;
    LNh[2]=1.;
    N[0]=sin(inc);
    N[1]=0.;
    N[2]=cos(inc);
    Jmag=sqrt(J[0]*J[0]+J[1]*J[1]+J[2]*J[2]);
    if (Jmag>0.) phiJ=atan2(J[1],J[0]);
    else phiJ=0.;
    thetaJ=acos(J[2]/Jmag);
    break;

  case LAL_SIM_INSPIRAL_FRAME_AXIS_VIEW:
  default:
#if DEBUG_LEVEL
    fprintf(stdout,"  *** View, cos(inc) %8.4f\n",cos(inc));
#endif
    LNh[0] = sin(inc);
    LNh[1] = 0.;
    LNh[2] = cos(inc);
    J[0]=S1[0]+S2[0]+LNh[0]*Lmag;
    J[1]=S1[1]+S2[1]+LNh[1]*Lmag;
    J[2]=S1[2]+S2[2]+LNh[2]*Lmag;
    N[0]=0.;
    N[1]=0.;
    N[2]=1.;
    Jmag=sqrt(J[0]*J[0]+J[1]*J[1]+J[2]*J[2]);
    if (Jmag>0.) phiJ=atan2(J[1],J[0]);
    else phiJ=0.;
    thetaJ=acos(J[2]/Jmag);
    rotateZ(-phiJ,&N[0],&N[1],&N[2]);
    rotateY(-thetaJ,&N[0],&N[1],&N[2]);
    break;
  }

#if DEBUG_LEVEL
  printf("  Start:  S1  %9.4f %9.4f %9.4f\n",S1[0],S1[1],S1[2]);
  printf("          S2  %9.4f %9.4f %9.4f\n",S2[0],S2[1],S2[2]);
  printf("          LNh %9.4f %9.4f %9.4f\n",LNh[0],LNh[1],LNh[2]);
  printf("          J   %9.4f %9.4f %9.4f\n",J[0],J[1],J[2]);
#endif
  rotateZ(-phiJ,&S1[0],&S1[1],&S1[2]);
  rotateZ(-phiJ,&S2[0],&S2[1],&S2[2]);
  rotateZ(-phiJ,&LNh[0],&LNh[1],&LNh[2]);
  rotateY(-thetaJ,&S1[0],&S1[1],&S1[2]);
  rotateY(-thetaJ,&S2[0],&S2[1],&S2[2]);
  rotateY(-thetaJ,&LNh[0],&LNh[1],&LNh[2]);
  phiN=atan2(N[1],N[0]);
  rotateZ(-phiN,&S1[0],&S1[1],&S1[2]);
  rotateZ(-phiN,&S2[0],&S2[1],&S2[2]);
  rotateZ(-phiN,&LNh[0],&LNh[1],&LNh[2]);
  rotateZ(-phiN,&N[0],&N[1],&N[2]);
  inc = acos(N[2]);
  *iota=inc;
  yinit[2] = LNh[0];
  yinit[3] = LNh[1];
  yinit[4] = LNh[2];
  yinit[5] = S1[0]/Mass/Mass;
  yinit[6] = S1[1]/Mass/Mass;
  yinit[7] = S1[2]/Mass/Mass;
  yinit[8] = S2[0]/Mass/Mass;
  yinit[9] = S2[1]/Mass/Mass;
  yinit[10]= S2[2]/Mass/Mass;

#if DEBUG_LEVEL
  J[0]=S1[0]+S2[0]+LNh[0]*Lmag;
  J[1]=S1[1]+S2[1]+LNh[1]*Lmag;
  J[2]=S1[2]+S2[2]+LNh[2]*Lmag;
  printf("   After setaxis: S1  %9.4f %9.4f %9.4f\n",S1[0],S1[1],S1[2]);
  printf("                  S2  %9.4f %9.4f %9.4f\n",S2[0],S2[1],S2[2]);
  printf("                  LNh %9.4f %9.4f %9.4f\n",LNh[0],LNh[1],LNh[2]);
  printf("                  N   %9.4f %9.4f %9.4f\n",N[0],N[1],N[2]);
  printf("                  J   %9.4f %9.4f %9.4f\n",J[0],J[1],J[2]);
#endif

  return XLAL_SUCCESS;

} /* End of XLALSimIMRPhenSpinInspiralSetAxis*/

/**
 * PhenSpin Initialization
 **/

static int XLALSimIMRPhenSpinInitialize(REAL8TimeSeries **hPlus,
					REAL8TimeSeries **hCross,
					REAL8 mass1,                              /* in Msun units */
					REAL8 mass2,                              /* in Msun units */
					REAL8 *yinit,
					REAL8 fMin,
					REAL8 fRef,
					REAL8 deltaT,
					REAL8 iota,
					int phaseO,
					LALSimInspiralSpinTaylorT4Coeffs *params,
					LALSimInspiralWaveformFlags      *waveFlags,
					LALSimInspiralTestGRParam        *testGRparams,
					UINT4 *lengthH)
{
  if (fMin<=0.) {
#if DEBUG_LEVEL
    fprintf(stderr,"** LALSimIMRPSpinInspiralRD error *** non >ve value of fMin %12.4e\n",fMin);
#endif
    XLALPrintError("** LALSimIMRPSpinInspiralRD error *** non >ve value of fMin %12.4e\n",fMin);
    XLAL_ERROR(XLAL_EDOM);
  }

  REAL8 S1x=yinit[5];
  REAL8 S1y=yinit[6];
  REAL8 S1z=yinit[7];
  REAL8 S2x=yinit[8];
  REAL8 S2y=yinit[9];
  REAL8 S2z=yinit[10];

  REAL8 LNhS1 = S1z;
  REAL8 LNhS2 = S2z;
  REAL8 S1S1  = S1x*S1x + S1y*S1y + S1z*S1z;
  REAL8 S1S2  = S1x*S2x + S1y*S2y + S1z*S2z;
  REAL8 S2S2  = S2x*S2x + S2y*S2y + S2z*S2z;
  REAL8 unitHz     = (mass1+mass2)*LAL_MTSUN_SI; /* convert m from msun to seconds */
  REAL8 initOmega  = fMin*unitHz * (REAL8) LAL_PI;
  REAL8 omegaMatch = OmMatch(LNhS1,LNhS2,S1S1,S1S2,S2S2);

  if ( initOmega > omegaMatch ) {
    if ((S1x==S1y)&&(S1x==0)&&(S2x==S2y)&&(S2y==0.)) {
      initOmega = 0.95*omegaMatch;
      yinit[1]=initOmega;
#if DEBUG_LEVEL
      fprintf(stdout,"*** LALPSpinInspiralRD WARNING ***: Initial frequency reset from %12.6e to %12.6e Hz, m:(%12.4e,%12.4e)\n",fMin,initOmega/unitHz/LAL_PI,mass1,mass2);
#endif
      XLALPrintWarning("*** LALPSpinInspiralRD WARNING ***: Initial frequency reset from %12.6e to %12.6e Hz, m:(%12.4e,%12.4e)\n",fMin,initOmega/unitHz/LAL_PI,mass1,mass2);
    }
    else {
#if DEBUG_LEVEL
      fprintf(stdout,"*** LALPSpinInspiralRD ERROR ***: Initial frequency %12.6e Hz too high, as fMatch estimated %12.6e Hz, m:(%12.4e,%12.4e)\n",fMin,omegaMatch/unitHz/LAL_PI,mass1,mass2);
#endif
      XLALPrintError("*** LALPSpinInspiralRD ERROR ***: Initial frequency %12.6e Hz too high, as fMatch estimated %12.6e Hz, m:(%12.4e,%12.4e)\n",fMin,omegaMatch/unitHz/LAL_PI,mass1,mass2);
      XLAL_ERROR(XLAL_EFAILED);
    }
  }
  yinit[1]=initOmega;

  if ((*hPlus != NULL) && (*hCross != NULL)) {
    int lenPlus =(*hPlus)->data->length;
    int lenCross=(*hCross)->data->length;
    if ((lenPlus!=lenCross)||(lenPlus==0) ) {
#if DEBUG_LEVEL
      fprintf(stderr,"*** LALSimIMRPSpinInspiralRD ERROR: hPlus and hCross passed with different lengths %d %d\n",lenPlus,lenCross);
#endif      
      XLALPrintError("*** LALSimIMRPSpinInspiralRD ERROR: hPlus and hCross passed with different lengths %d %d\n",lenPlus,lenCross);
      XLAL_ERROR(XLAL_ENOMEM);
    }
    else
      *lengthH=lenPlus;
  }

  /* setup coefficients for PN equations */
  if(XLALSimIMRPhenSpinParamsSetup(params,deltaT,fMin,fRef,mass1,mass2,XLALSimInspiralGetInteraction(waveFlags),testGRparams,phaseO)) {
    XLAL_ERROR(XLAL_ENOMEM);
  }

  if(XLALSimIMRPhenSpinInspiralSetAxis(mass1,mass2,&iota,yinit,XLALSimInspiralGetFrameAxis(waveFlags))) {
    XLAL_ERROR(XLAL_ENOMEM);
  }

  return XLAL_SUCCESS;

} /* End of XLALSimIMRPhenSpinInitialize*/

/**
 * Driver routine to compute the PhenSpin Inspiral waveform
 * without ring-down
 *
 * All units are SI units.
 */
int XLALSimSpinInspiralGenerator(REAL8TimeSeries **hPlus,	        /**< +-polarization waveform [returned] */
				 REAL8TimeSeries **hCross,	        /**< x-polarization waveform [returned] */
				 REAL8 phi_start,                       /**< start phase */
				 REAL8 deltaT,                          /**< sampling interval */
				 REAL8 m1,                              /**< mass of companion 1 */
				 REAL8 m2,                              /**< mass of companion 2 */
				 REAL8 f_min,                           /**< start frequency */
				 REAL8 f_ref,                           /**< reference frequency */
				 REAL8 r,                               /**< distance of source */
				 REAL8 iota,                            /**< incination of source (rad) */
				 REAL8 s1x,                             /**< x-component of dimensionless spin for object 1 */
				 REAL8 s1y,                             /**< y-component of dimensionless spin for object 1 */
				 REAL8 s1z,                             /**< z-component of dimensionless spin for object 1 */
				 REAL8 s2x,                             /**< x-component of dimensionless spin for object 2 */
				 REAL8 s2y,                             /**< y-component of dimensionless spin for object 2 */
				 REAL8 s2z,                             /**< z-component of dimensionless spin for object 2 */
				 int phaseO,                            /**< twice post-Newtonian phase order */
				 int UNUSED ampO,                       /**< twice post-Newtonian amplitude order */
				 LALSimInspiralWaveformFlags *waveFlags,/**< Choice of axis for input spin params */
				 LALSimInspiralTestGRParam *testGRparams/**< Non-GR params */
				 )
{

  int errcode=0;
  int errcodeInt=0;
  uint lengthH=0;     /* Length of hPlus and hCross passed, 0 if NULL*/
  int intLen;         /* Length of arrays after integration*/
  int idx,kdx;
  LALSimInspiralSpinTaylorT4Coeffs *params=NULL;
  REAL8 mass1=m1/LAL_MSUN_SI;
  REAL8 mass2=m2/LAL_MSUN_SI;

  REAL8 yinit[LAL_NUM_PST4_VARIABLES];
  yinit[0] = phi_start;
  yinit[1] = 0.;
  yinit[2] = 0.;
  yinit[3] = 0.;
  yinit[4] = cos(iota);
  yinit[5] = s1x;
  yinit[6] = s1y;
  yinit[7] = s1z;
  yinit[8] = s2x;
  yinit[9] = s2y;
  yinit[10]= s2z;
  yinit[11]= 0.;

  REAL8TimeSeries **omega =NULL;
  REAL8TimeSeries **Phi   =NULL;
  REAL8TimeSeries **LNhatx=NULL;
  REAL8TimeSeries **LNhaty=NULL;
  REAL8TimeSeries **LNhatz=NULL;
  REAL8TimeSeries **S1x   =NULL;
  REAL8TimeSeries **S1y   =NULL;
  REAL8TimeSeries **S1z   =NULL;
  REAL8TimeSeries **S2x   =NULL;
  REAL8TimeSeries **S2y   =NULL;
  REAL8TimeSeries **S2z   =NULL;
  REAL8TimeSeries **Energy=NULL;
  if (f_ref<=f_min) {
    errcode=XLALSimIMRPhenSpinInitialize(hPlus,hCross,mass1,mass2,yinit,f_min,-1.,deltaT,iota,phaseO,params,waveFlags,testGRparams,&lengthH);
    if(errcode) XLAL_ERROR(XLAL_EFUNC);
    if (lengthH==0) {
      REAL8 tn = XLALSimInspiralTaylorLength(deltaT, m1, m2, f_ref, phaseO);
      REAL8 x  = 1.1 * (tn + 1. ) / deltaT;
      int length = ceil(log10(x)/log10(2.));
      lengthH    = pow(2, length);
    }
    errcodeInt=XLALSimInspiralSpinTaylorT4Engine(omega,Phi,LNhatx,LNhaty,LNhatz,S1x,S1y,S1z,S2x,S2y,S2z,Energy,1,lengthH,0,yinit,PhenSpinTaylor,params);
    intLen=(*Phi)->data->length;
  }
  else {
    REAL8TimeSeries **Phi1=NULL,**omega1=NULL,**LNhatx1=NULL,**LNhaty1=NULL,**LNhatz1=NULL,**S1x1=NULL,**S1y1=NULL,**S1z1=NULL,**S2x1=NULL,**S2y1=NULL,**S2z1=NULL,**Energy1=NULL;
    errcode=XLALSimIMRPhenSpinInitialize(hPlus,hCross,m1,m2,yinit,f_min,f_ref,deltaT,iota,phaseO,params,waveFlags,testGRparams,&lengthH);
    if(errcode) XLAL_ERROR(XLAL_EFUNC);
    if (lengthH==0) {
      REAL8 tn = XLALSimInspiralTaylorLength(deltaT, m1, m2, f_min, phaseO);
      REAL8 x  = 1.1 * (tn + 1. ) / deltaT;
      int length = ceil(log10(x)/log10(2.));
      lengthH    = pow(2, length);
    }
    errcodeInt=XLALSimInspiralSpinTaylorT4Engine(omega1,Phi1,LNhatx1,LNhaty1,LNhatz1,S1x1,S1y1,S1z1,S2x1,S2y1,S2z1,Energy1,-1,lengthH,0,yinit,PhenSpinTaylor,params);
    /* report on abnormal termination*/
    if ( (errcodeInt != LALSIMINSPIRAL_PHENSPIN_TEST_FREQBOUND) ) {
#if DEBUG_LEVEL
      fprintf(stderr,"** LALPSpinInspiralRD WARNING **: integration terminated with code %d.\n",errcode);
      fprintf(stderr,"   1025: Energy increases\n  1026: Omegadot -ve\n  1027: Freqbound\n 1028: Omega NAN\n  1031: Omega -ve\n");
      fprintf(stderr,"   Waveform parameters were m1 = %14.6e, m2 = %14.6e, inc = %10.6f,  fRef %10.4f Hz\n", m1, m2, iota, f_ref);
      fprintf(stderr,"                           S1 = (%10.6f,%10.6f,%10.6f)\n", s1x, s1y, s1z);
      fprintf(stderr,"                           S2 = (%10.6f,%10.6f,%10.6f)\n", s2x, s2y, s2z);
#endif
      XLALPrintError("** LALPSpinInspiralRD WARNING **: integration terminated with code %d.\n",errcode);
      XLALPrintError("   1025: Energy increases\n  1026: Omegadot -ve\n  1027: Freqbound\n 1028: Omega NAN\n  1031: Omega -ve\n");
      XLALPrintError("  Waveform parameters were m1 = %14.6e, m2 = %14.6e, inc = %10.6f,  fref %10.4f Hz\n", m1, m2, iota, f_ref);
      XLALPrintError("                           S1 = (%10.6f,%10.6f,%10.6f)\n", s1x, s1y, s1z);
      XLALPrintError("                           S2 = (%10.6f,%10.6f,%10.6f)\n", s2x, s2y, s2z);
    }

    REAL8TimeSeries **omega2=NULL,**Phi2=NULL,**LNhatx2=NULL,**LNhaty2=NULL,**LNhatz2=NULL,**S1x2=NULL,**S1y2=NULL,**S1z2=NULL,**S2x2=NULL,**S2y2=NULL,**S2z2=NULL,**Energy2=NULL;
    errcode=XLALSimIMRPhenSpinInitialize(hPlus,hCross,m1,m2,yinit,f_ref,-1.,deltaT,iota,phaseO,params,waveFlags,testGRparams,&lengthH);
    if(errcode) XLAL_ERROR(XLAL_EFUNC);

    errcodeInt=XLALSimInspiralSpinTaylorT4Engine(omega2,Phi2,LNhatx2,LNhaty2,LNhatz2,S1x2,S1y2,S1z2,S2x2,S2y2,S2z2,Energy2,LAL_NUM_PST4_VARIABLES,phaseO,f_ref,yinit,PhenSpinTaylor,params);

    REAL8 phiRef=(*Phi1)->data->data[(*omega1)->data->length-1];

    errcode =XLALAppendTSandFree(*omega1,*omega2,*omega);
    errcode+=XLALAppendTSandFree(*Phi1,*Phi2,*Phi);
    errcode+=XLALAppendTSandFree(*LNhatx1,*LNhatx2,*LNhatx);
    errcode+=XLALAppendTSandFree(*LNhaty1,*LNhaty2,*LNhaty);
    errcode+=XLALAppendTSandFree(*LNhatz1,*LNhatz2,*LNhatz);
    errcode+=XLALAppendTSandFree(*S1x1,*S1x2,*S1x);
    errcode+=XLALAppendTSandFree(*S1y1,*S1y2,*S1y);
    errcode+=XLALAppendTSandFree(*S1z1,*S1z2,*S1z);
    errcode+=XLALAppendTSandFree(*S2x1,*S2x2,*S2x);
    errcode+=XLALAppendTSandFree(*S2y1,*S2y2,*S2y);
    errcode+=XLALAppendTSandFree(*S2z1,*S2z2,*S2z);

    intLen=(*Phi)->data->length;
    for (idx=0;idx<intLen;idx++) (*Phi)->data->data[idx]-=phiRef;

  }

  /* report on abnormal termination*/
  if ( (errcodeInt !=  LALSIMINSPIRAL_PHENSPIN_TEST_ENERGY) ) {
#if DEBUG_LEVEL
    fprintf(stderr,"** LALPSpinInspiralRD WARNING **: integration terminated with code %d.\n",errcode);
    fprintf(stderr,"   1025: Energy increases\n  1026: Omegadot -ve\n  1028: Omega NAN\n  1031: Omega -ve\n");
    fprintf(stderr,"   Waveform parameters were m1 = %14.6e, m2 = %14.6e, inc = %10.6f,\n", m1, m2, iota);
    fprintf(stderr,"                           S1 = (%10.6f,%10.6f,%10.6f)\n", s1x, s1y, s1z);
    fprintf(stderr,"                           S2 = (%10.6f,%10.6f,%10.6f)\n", s2x, s2y, s2z);
#endif
    XLALPrintWarning("** LALPSpinInspiralRD WARNING **: integration terminated with code %d.\n",errcode);
    XLALPrintWarning("  Waveform parameters were m1 = %14.6e, m2 = %14.6e, inc = %10.6f,\n", m1, m2, iota);
    XLALPrintWarning("                           S1 = (%10.6f,%10.6f,%10.6f)\n", s1x, s1y, s1z);
    XLALPrintWarning("                           S2 = (%10.6f,%10.6f,%10.6f)\n", s2x, s2y, s2z);
  }

  const LIGOTimeGPS tStart=LIGOTIMEGPSZERO;
  COMPLEX16TimeSeries* hL2=XLALCreateCOMPLEX16TimeSeries( "hL2", &tStart, 0., deltaT, &lalDimensionlessUnit, 5*intLen);
  COMPLEX16TimeSeries* hL3=XLALCreateCOMPLEX16TimeSeries( "hL3", &tStart, 0., deltaT, &lalDimensionlessUnit, 7*intLen);
  COMPLEX16TimeSeries* hL4=XLALCreateCOMPLEX16TimeSeries( "hL4", &tStart, 0., deltaT, &lalDimensionlessUnit, 9*intLen);
  COMPLEX16Vector* hL2tmp=XLALCreateCOMPLEX16Vector(5);
  COMPLEX16Vector* hL3tmp=XLALCreateCOMPLEX16Vector(7);
  COMPLEX16Vector* hL4tmp=XLALCreateCOMPLEX16Vector(9);
  LALSimInspiralInclAngle trigAngle;

  REAL8 amp22ini = -2.0 * m1*m2/(m1+m2) * LAL_G_SI/pow(LAL_C_SI,3.) / r * sqrt(16. * LAL_PI / 5.);
  REAL8 amp33ini = -amp22ini * sqrt(5./42.)/4.;
  REAL8 amp44ini = amp22ini * sqrt(5./7.) * 2./9.;
  REAL8 alpha,v,v2,Psi,om;
  REAL8 eta=mass1*mass2/(mass1+mass2)/(mass1+mass2);
  REAL8 dm=(mass1-mass2)/(mass1+mass2);

  for (idx=0;idx<intLen;intLen++) {
    om=(*omega)->data->data[idx];
    v=cbrt(om);
    v2=v*v;
    Psi=(*Phi)->data->data[idx] -2.*om*(1.-eta*v2)*log(om);
    errcode =XLALSimInspiralComputeAlpha(mass1,mass2,(*LNhatx)->data->data[idx],(*LNhaty)->data->data[idx],(*S1x)->data->data[idx],(*S1y)->data->data[idx],(*S2x)->data->data[idx],(*S2y)->data->data[idx],&alpha);
    errcode+=XLALSimInspiralComputeInclAngle((*LNhatz)->data->data[idx],&trigAngle);
    errcode+=XLALSimSpinInspiralFillL2Modes(hL2tmp,v,eta,dm,Psi,alpha,&trigAngle);
    for (kdx=0;kdx<5;kdx++) hL2->data->data[5*idx+kdx]=hL2tmp->data[kdx]*amp22ini*v2;
    errcode+=XLALSimSpinInspiralFillL3Modes(hL3tmp,v,eta,dm,Psi,alpha,&trigAngle);
    for (kdx=0;kdx<7;kdx++) hL3->data->data[7*idx+kdx]=hL3tmp->data[kdx]*amp33ini*v2;
    errcode+=XLALSimSpinInspiralFillL4Modes(hL4tmp,v,eta,dm,Psi,alpha,&trigAngle);
    for (kdx=0;kdx<9;kdx++) hL4->data->data[9*idx+kdx]=hL4tmp->data[kdx]*amp44ini*v2*v2;
  }
  XLALDestroyCOMPLEX16Vector(hL2tmp);
  XLALDestroyCOMPLEX16Vector(hL3tmp);
  XLALDestroyCOMPLEX16Vector(hL4tmp);
  int m;
  REAL8TimeSeries *hPtmp=XLALCreateREAL8TimeSeries( "hPtmp", &tStart, 0., deltaT, &lalDimensionlessUnit, intLen);
  REAL8TimeSeries *hCtmp=XLALCreateREAL8TimeSeries( "hCtmp", &tStart, 0., deltaT, &lalDimensionlessUnit, intLen);
  COMPLEX16TimeSeries* hLMtmp=XLALCreateCOMPLEX16TimeSeries( "hLMtmp", &tStart, 0., deltaT, &lalDimensionlessUnit, intLen);
  int l=2;
  for (m=-l;m<=l;m++) {
    for (idx=0;idx<intLen;idx++) hLMtmp->data->data[idx]=hL2->data->data[(m+l)+idx*(2*l+1)];
    XLALSimAddMode(hPtmp,hCtmp,hLMtmp,iota,0.,l,m,0);
  }
  l=3;
  for (m=-l;m<=l;m++) {
    for (idx=0;idx<intLen;idx++)
      hLMtmp->data->data[idx]=hL3->data->data[(m+l)+idx*(2*l+1)];
    XLALSimAddMode(hPtmp,hCtmp,hLMtmp,iota,0.,l,m,0);
  }
  l=4;
  for (m=-l;m<=l;m++) {
    for (idx=0;idx<intLen;idx++)
      hLMtmp->data->data[idx]=hL4->data->data[(m+l)+idx*(2*l+1)];
    XLALSimAddMode(hPtmp,hCtmp,hLMtmp,iota,0.,l,m,0);
  }
  XLALDestroyCOMPLEX16TimeSeries(hL2);
  XLALDestroyCOMPLEX16TimeSeries(hL3);
  XLALDestroyCOMPLEX16TimeSeries(hL4);
  XLALDestroyCOMPLEX16TimeSeries(hLMtmp);

  if ((hPlus) && (hCross)) {
    if ((*hPlus)->data->length>(uint)intLen) {
#if DEBUG_LEVEL
      fprintf(stderr,"*** LALSimIMRPSpinInspiralRD ERROR: h+ and hx too short: %d vs. %d\n",(*hPlus)->data->length,intLen);
#endif
      XLALPrintError("*** LALSimIMRPSpinInspiralRD ERROR: h+ and hx too short: %d vs. %d\n",(*hPlus)->data->length,intLen);
      XLAL_ERROR(XLAL_EFAILED);
    }
    else { 
      for (idx=0;idx<(int)intLen;idx++) {
	(*hPlus)->data->data[idx] =hPtmp->data->data[idx];
	(*hCross)->data->data[idx]=hCtmp->data->data[idx];
      }
      for (idx=intLen;idx<(int)(*hPlus)->data->length;idx++) {
	(*hPlus)->data->data[idx] = (*hCross)->data->data[idx]= 0.;
      }
      XLALDestroyREAL8TimeSeries(hCtmp);
      XLALDestroyREAL8TimeSeries(hPtmp);
    }
  }
  else {
    (*hPlus)=hPtmp;
    (*hCross)=hCtmp;
  }

  return errcode;

} /* End of XLALSimSpinInspiralGenerator*/

int XLALSimIMRPhenSpinFinalMassSpin(REAL8 *finalMass,
				    REAL8 *finalSpin,
				    REAL8 mass1,
				    REAL8 mass2,
				    REAL8 s1s1,
				    REAL8 s2s2,
				    REAL8 s1L,
				    REAL8 s2L,
				    REAL8 s1s2,
				    REAL8 energy)
{
  /* XLAL error handling */
  INT4 errcode = XLAL_SUCCESS;
  REAL8 qq,ll,eta;
	
  /* See eq.(6) in arXiv:0904.2577 */
  REAL8 ma1,ma2,a12,a12l;
  REAL8 cosa1=0.;
  REAL8 cosa2=0.;
  REAL8 cosa12=0.;
	
  REAL8 t0=-2.9;
  REAL8 t3=2.6;
  REAL8 s4=-0.123;
  REAL8 s5=0.45;
  REAL8 t2=16.*(0.6865-t3/64.-sqrt(3.)/2.);
	
  /* get a local copy of the intrinstic parameters */
  qq = mass2/mass1;
  eta = mass1*mass2/((mass1+mass2)*(mass1+mass2));
  /* done */
  ma1 = sqrt( s1s1 );
  ma2 = sqrt( s2s2 );
	
  if (ma1>0.) cosa1 = s1L/ma1;
  else cosa1=0.;
  if (ma2>0.) cosa2 = s2L/ma2;
  else cosa2=0.;
  if ((ma1>0.)&&(ma2>0.)) {
    cosa12  = s1s2/ma1/ma2;
  }
  else cosa12=0.;
	
  a12  = ma1*ma1 + ma2*ma2*qq*qq*qq*qq + 2.*ma1*ma2*qq*qq*cosa12 ;
  a12l = ma1*cosa1 + ma2*cosa2*qq*qq ;
  ll = 2.*sqrt(3.)+ t2*eta + t3*eta*eta + s4*a12/(1.+qq*qq)/(1.+qq*qq) + (s5*eta+t0+2.)/(1.+qq*qq)*a12l;
	
  /* Estimate final mass by adding the negative binding energy to the rest mass*/
  *finalMass = 1. + energy;

  /* Estimate final spin */
  *finalSpin = sqrt( a12 + 2.*ll*qq*a12l + ll*ll*qq*qq)/(1.+qq)/(1.+qq);

  /* Check value of finalMass */
  if (*finalMass < 0.) {
#if DEBUG_LEVEL
    fprintf(stderr,"*** LALSimIMRPSpinInspiralRD ERROR: Estimated final mass <0 : %12.6f\n ",*finalMass);
#endif
    XLAL_ERROR( XLAL_ERANGE);
  }
	
  /* Check value of finalSpin */
  if ((*finalSpin > 1.)||(*finalSpin < 0.)) {
    if ((*finalSpin>=1.)&&(*finalSpin<1.01)) {
#if DEBUG_LEVEL
      fprintf(stderr,"*** LALSimIMRPSpinInspiralRD WARNING: Estimated final Spin slightly >1 : %11.3e\n ",*finalSpin);
      fprintf(stderr,"      (m1=%8.3f  m2=%8.3f  s1sq=%8.3f  s2sq=%8.3f  s1L=%8.3f  s2L=%8.3f  s1s2=%8.3f ) final spin set to 1 and code goes on\n",mass1,mass2,s1s1,s2s2,s1L,s2L,s1s2);
#endif
      XLALPrintWarning("*** LALSimIMRPSpinInspiralRD WARNING: Estimated final Spin slightly >1 : %11.3e\n ",*finalSpin);
      XLALPrintWarning("    (m1=%8.3f  m2=%8.3f  s1sq=%8.3f  s2sq=%8.3f  s1L=%8.3f  s2L=%8.3f  s1s2=%8.3f ) final spin set to 1 and code goes on\n",mass1,mass2,s1s1,s2s2,s1L,s2L,s1s2);
      *finalSpin = .99999;
    }
    else {
#if DEBUG_LEVEL
      fprintf(stderr,"*** LALSimIMRPSpinInspiralRD ERROR: Unphysical estimation of final Spin : %11.3e\n ",*finalSpin);
      fprintf(stderr,"      (m1=%8.3f  m2=%8.3f  s1sq=%8.3f  s2sq=%8.3f  s1L=%8.3f  s2L=%8.3f  s1s2=%8.3f )\n",mass1,mass2,s1s1,s2s2,s1L,s2L,s1s2);
      fprintf(stderr,"***                                    Code aborts\n");
#endif
      XLALPrintError("*** LALSimIMRPSpinInspiralRD ERROR: Unphysical estimation of final Spin : %11.3e\n ",*finalSpin);
      XLALPrintWarning("    (m1=%8.3f  m2=%8.3f  s1sq=%8.3f  s2sq=%8.3f  s1L=%8.3f  s2L=%8.3f  s1s2=%8.3f )\n",mass1,mass2,s1s1,s2s2,s1L,s2L,s1s2);
      XLALPrintError("***                                    Code aborts\n");
      XLAL_ERROR( XLAL_ERANGE);
    }
  }
	
  return errcode;
} /* End of XLALSimIMRPhenSpinFinalMassSpin*/

static INT4 XLALSimIMRHybridRingdownWave(
    REAL8Vector			*rdwave1,   /**<< Real part of ringdown */
    REAL8Vector			*rdwave2,   /**<< Imaginary part of ringdown */
    const REAL8                 dt,         /**<< Sampling interval */
    const REAL8                 mass1,      /**<< First component mass (in Solar masses) */
    const REAL8                 mass2,      /**<< Second component mass (in Solar masses) */
    REAL8VectorSequence		*inspwave1, /**<< Values and derivatives of real part of inspiral waveform */
    REAL8VectorSequence		*inspwave2, /**<< Values and derivatives of Imaginary part of inspiral waveform */
    COMPLEX16Vector		*modefreqs, /**<< Complex frequencies of ringdown (scaled by total mass) */
    REAL8Vector			*matchrange /**<< Times which determine the comb size for ringdown attachment */
	)
{

  /* XLAL error handling */
  INT4 errcode = XLAL_SUCCESS;

  /* For checking GSL return codes */
  INT4 gslStatus;

  UINT4 i, j, k, nmodes = 8;

  /* Sampling rate from input */
  REAL8 t1, t2, t3, t4, t5, rt;
  gsl_matrix *coef;
  gsl_vector *hderivs;
  gsl_vector *x;
  gsl_permutation *p;
  REAL8Vector *modeamps;
  int s;
  REAL8 tj;
  REAL8 m;

  /* mass in geometric units */
  m  = (mass1 + mass2) * LAL_MTSUN_SI;
  t5 = (matchrange->data[0] - matchrange->data[1]) * m;
  rt = -t5 / 5.;

  t4 = t5 + rt;
  t3 = t4 + rt;
  t2 = t3 + rt;
  t1 = t2 + rt;
  
  if ( inspwave1->length != 3 || inspwave2->length != 3 ||
		modefreqs->length != nmodes )
  {
    XLAL_ERROR( XLAL_EBADLEN );
  }

  /* Solving the linear system for QNMs amplitude coefficients using gsl routine */
  /* Initiate matrices and supporting variables */
  XLAL_CALLGSL( coef = (gsl_matrix *) gsl_matrix_alloc(2 * nmodes, 2 * nmodes) );
  XLAL_CALLGSL( hderivs = (gsl_vector *) gsl_vector_alloc(2 * nmodes) );
  XLAL_CALLGSL( x = (gsl_vector *) gsl_vector_alloc(2 * nmodes) );
  XLAL_CALLGSL( p = (gsl_permutation *) gsl_permutation_alloc(2 * nmodes) );

  /* Check all matrices and variables were allocated */
  if ( !coef || !hderivs || !x || !p )
  {
    if (coef)    gsl_matrix_free(coef);
    if (hderivs) gsl_vector_free(hderivs);
    if (x)       gsl_vector_free(x);
    if (p)       gsl_permutation_free(p);

    XLAL_ERROR( XLAL_ENOMEM );
  }

  /* Define the linear system Ax=y */
  /* Matrix A (2*n by 2*n) has block symmetry. Define half of A here as "coef" */
  /* Define y here as "hderivs" */
  for (i = 0; i < nmodes; ++i)
  {
	gsl_matrix_set(coef, 0, i, 1);
	gsl_matrix_set(coef, 1, i, - cimag(modefreqs->data[i]));
	gsl_matrix_set(coef, 2, i, exp(-cimag(modefreqs->data[i])*t1) * cos(creal(modefreqs->data[i])*t1));
	gsl_matrix_set(coef, 3, i, exp(-cimag(modefreqs->data[i])*t2) * cos(creal(modefreqs->data[i])*t2));
	gsl_matrix_set(coef, 4, i, exp(-cimag(modefreqs->data[i])*t3) * cos(creal(modefreqs->data[i])*t3));
	gsl_matrix_set(coef, 5, i, exp(-cimag(modefreqs->data[i])*t4) * cos(creal(modefreqs->data[i])*t4));
	gsl_matrix_set(coef, 6, i, exp(-cimag(modefreqs->data[i])*t5) * cos(creal(modefreqs->data[i])*t5));
	gsl_matrix_set(coef, 7, i, exp(-cimag(modefreqs->data[i])*t5) * 
		       (-cimag(modefreqs->data[i]) * cos(creal(modefreqs->data[i])*t5)
			-creal(modefreqs->data[i]) * sin(creal(modefreqs->data[i])*t5)));
	gsl_matrix_set(coef, 8, i, 0);
	gsl_matrix_set(coef, 9, i, - creal(modefreqs->data[i]));
	gsl_matrix_set(coef, 10, i, -exp(-cimag(modefreqs->data[i])*t1) * sin(creal(modefreqs->data[i])*t1));
	gsl_matrix_set(coef, 11, i, -exp(-cimag(modefreqs->data[i])*t2) * sin(creal(modefreqs->data[i])*t2));
	gsl_matrix_set(coef, 12, i, -exp(-cimag(modefreqs->data[i])*t3) * sin(creal(modefreqs->data[i])*t3));
	gsl_matrix_set(coef, 13, i, -exp(-cimag(modefreqs->data[i])*t4) * sin(creal(modefreqs->data[i])*t4));
	gsl_matrix_set(coef, 14, i, -exp(-cimag(modefreqs->data[i])*t5) * sin(creal(modefreqs->data[i])*t5));
	gsl_matrix_set(coef, 15, i, exp(-cimag(modefreqs->data[i])*t5) * 
		       ( cimag(modefreqs->data[i]) * sin(creal(modefreqs->data[i])*t5)
			 -creal(modefreqs->data[i]) * cos(creal(modefreqs->data[i])*t5)));
  }
  for (i = 0; i < 2; ++i)
  {
	gsl_vector_set(hderivs, i, inspwave1->data[(i + 1) * inspwave1->vectorLength - 1]);
	gsl_vector_set(hderivs, i + nmodes, inspwave2->data[(i + 1) * inspwave2->vectorLength - 1]);
	gsl_vector_set(hderivs, i + 6, inspwave1->data[i * inspwave1->vectorLength]);
	gsl_vector_set(hderivs, i + 6 + nmodes, inspwave2->data[i * inspwave2->vectorLength]);
  }
  gsl_vector_set(hderivs, 2, inspwave1->data[4]);
  gsl_vector_set(hderivs, 2 + nmodes, inspwave2->data[4]);
  gsl_vector_set(hderivs, 3, inspwave1->data[3]);
  gsl_vector_set(hderivs, 3 + nmodes, inspwave2->data[3]);
  gsl_vector_set(hderivs, 4, inspwave1->data[2]);
  gsl_vector_set(hderivs, 4 + nmodes, inspwave2->data[2]);
  gsl_vector_set(hderivs, 5, inspwave1->data[1]);
  gsl_vector_set(hderivs, 5 + nmodes, inspwave2->data[1]);
  
  /* Complete the definition for the rest half of A */
  for (i = 0; i < nmodes; ++i)
  {
	for (k = 0; k < nmodes; ++k)
	{
	  gsl_matrix_set(coef, i, k + nmodes, - gsl_matrix_get(coef, i + nmodes, k));
	  gsl_matrix_set(coef, i + nmodes, k + nmodes, gsl_matrix_get(coef, i, k));
	}
  }

  #if 0
  /* print ringdown-matching linear system: coefficient matrix and RHS vector */
  printf("\nRingdown matching matrix:\n");
  for (i = 0; i < 16; ++i)
  {
    for (j = 0; j < 16; ++j)
    {
      printf("%.12e ",gsl_matrix_get(coef,i,j));
    }
    printf("\n");
  }
  printf("RHS:  ");
  for (i = 0; i < 16; ++i)
  {
    printf("%.12e   ",gsl_vector_get(hderivs,i));
  }
  printf("\n");
  #endif

  /* Call gsl LU decomposition to solve the linear system */
  XLAL_CALLGSL( gslStatus = gsl_linalg_LU_decomp(coef, p, &s) );
  if ( gslStatus == GSL_SUCCESS )
  {
    XLAL_CALLGSL( gslStatus = gsl_linalg_LU_solve(coef, p, hderivs, x) );
  }
  if ( gslStatus != GSL_SUCCESS )
  {
    gsl_matrix_free(coef);
    gsl_vector_free(hderivs);
    gsl_vector_free(x);
    gsl_permutation_free(p);
    XLAL_ERROR( XLAL_EFUNC );
  }

  /* Putting solution to an XLAL vector */
  modeamps = XLALCreateREAL8Vector(2 * nmodes);

  if ( !modeamps )
  {
    gsl_matrix_free(coef);
    gsl_vector_free(hderivs);
    gsl_vector_free(x);
    gsl_permutation_free(p);
    XLAL_ERROR( XLAL_ENOMEM );
  }

  for (i = 0; i < nmodes; ++i)
  {
	modeamps->data[i] = gsl_vector_get(x, i);
	modeamps->data[i + nmodes] = gsl_vector_get(x, i + nmodes);
  }

  /* Free all gsl linear algebra objects */
  gsl_matrix_free(coef);
  gsl_vector_free(hderivs);
  gsl_vector_free(x);
  gsl_permutation_free(p);

  /* Build ring-down waveforms */

  REAL8 timeOffset = fmod( matchrange->data[1], dt/m) * dt;

  for (j = 0; j < rdwave1->length; ++j)
  {
	tj = j * dt - timeOffset;
	rdwave1->data[j] = 0;
	rdwave2->data[j] = 0;
	for (i = 0; i < nmodes; ++i)
	{
	  rdwave1->data[j] += exp(- tj * cimag(modefreqs->data[i]))
	    * ( modeamps->data[i] * cos(tj * creal(modefreqs->data[i]))
		+   modeamps->data[i + nmodes] * sin(tj * creal(modefreqs->data[i])) );
	  rdwave2->data[j] += exp(- tj * cimag(modefreqs->data[i]))
	    * (- modeamps->data[i] * sin(tj * creal(modefreqs->data[i]))
			   +   modeamps->data[i + nmodes] * cos(tj * creal(modefreqs->data[i])) );
	}
  }

  XLALDestroyREAL8Vector(modeamps);
  return errcode;
}

/**
 * Driver routine for generating PhenSpinRD waveforms
 **/

int XLALSimIMRPhenSpinInspiralRDGenerator(REAL8TimeSeries **hPlus,	         /**< +-polarization waveform [returned] */
					  REAL8TimeSeries **hCross,	         /**< x-polarization waveform [returned] */
					  REAL8 phi_start,                       /**< start phase */
					  REAL8 deltaT,                          /**< sampling interval */
					  REAL8 m1,                              /**< mass of companion 1 in SI units */
					  REAL8 m2,                              /**< mass of companion 2 in SI units */
					  REAL8 f_min,                           /**< start frequency */
					  REAL8 f_ref,                           /**< reference frequency */
					  REAL8 r,                               /**< distance of source */
					  REAL8 iota,                            /**< inclination of source (rad) */
					  REAL8 s1x,                             /**< x-component of dimensionless spin for object 1 */
					  REAL8 s1y,                             /**< y-component of dimensionless spin for object 1 */
					  REAL8 s1z,                             /**< z-component of dimensionless spin for object 1 */
					  REAL8 s2x,                             /**< x-component of dimensionless spin for object 2 */
					  REAL8 s2y,                             /**< y-component of dimensionless spin for object 2 */
					  REAL8 s2z,                             /**< z-component of dimensionless spin for object 2 */
					  int phaseO,                            /**< twice post-Newtonian phase order */
					  int UNUSED ampO,                       /**< twice post-Newtonian amplitude order */
					  LALSimInspiralWaveformFlags *waveFlags,/**< Choice of axis for input spin params */
					  LALSimInspiralTestGRParam *testGRparams/**< Non-GR params */
					  )
{

  int errcode=0;
  int errcodeInt=0;
  uint lengthH=0;     /* Length of hPlus and hCross passed, 0 if NULL*/
  uint intLen;        /* Length of arrays after integration*/
  int idx,jdx,kdx;
  LALSimInspiralSpinTaylorT4Coeffs params;
  REAL8 S1S1=s1x*s1x+s1y*s1y+s1z*s1z;
  REAL8 S2S2=s1x*s1x+s1y*s1y+s1z*s1z;
  REAL8 mass1=m1/LAL_MSUN_SI;
  REAL8 mass2=m2/LAL_MSUN_SI;
  REAL8 Mass=mass1+mass2;

  REAL8 yinit[LAL_NUM_PST4_VARIABLES];
  yinit[0] = phi_start;
  yinit[1] = 0.;
  yinit[2] = 0.;
  yinit[3] = 0.;
  yinit[4] = cos(iota);
  yinit[5] = s1x;
  yinit[6] = s1y;
  yinit[7] = s1z;
  yinit[8] = s2x;
  yinit[9] = s2y;
  yinit[10]= s2z;
  yinit[11]= 0.;

  REAL8TimeSeries *omega, *Phi, *LNhatx, *LNhaty, *LNhatz;
  REAL8TimeSeries *S1x, *S1y, *S1z, *S2x, *S2y, *S2z, *Energy;
  if (f_ref<=f_min) {
    errcode=XLALSimIMRPhenSpinInitialize(hPlus,hCross,mass1,mass2,yinit,f_min,-1.,deltaT,iota,phaseO,&params,waveFlags,testGRparams,&lengthH);
    if(errcode) XLAL_ERROR(XLAL_EFUNC);
    if (lengthH==0) {
      REAL8 tn = XLALSimInspiralTaylorLength(deltaT, m1, m2, f_min, phaseO);
      REAL8 x  = 1.1 * (tn + 1. ) / deltaT;
      int length = ceil(log10(x)/log10(2.));
      lengthH    = pow(2, length);
    }
#if DEBUG_LEVEL
    printf("  Estimated Length %d\n",lengthH);
#endif
    errcodeInt=XLALSimInspiralSpinTaylorT4Engine(&omega,&Phi,&LNhatx,&LNhaty,&LNhatz,&S1x,&S1y,&S1z,&S2x,&S2y,&S2z,&Energy,1,lengthH,0,yinit,PhenSpinTaylorRD,&params);
    intLen=Phi->data->length;
  }
  else {
    REAL8TimeSeries *Phi1, *omega1, *LNhatx1, *LNhaty1, *LNhatz1;
    REAL8TimeSeries *S1x1, *S1y1, *S1z1, *S2x1, *S2y1, *S2z1, *Energy1;
    errcode=XLALSimIMRPhenSpinInitialize(hPlus,hCross,m1,m2,yinit,f_min,f_ref,deltaT,iota,phaseO,&params,waveFlags,testGRparams,&lengthH);
    if(errcode) XLAL_ERROR(XLAL_EFUNC);
    if (lengthH==0) {
      REAL8 tn = XLALSimInspiralTaylorLength(deltaT, m1, m2, f_min, phaseO);
      REAL8 x  = 1.1 * (tn + 1. ) / deltaT;
      int length = ceil(log10(x)/log10(2.));
      lengthH    = pow(2, length);
    }
#if DEBUG_LEVEL
    printf("  Estimated Length %d\n",lengthH);
#endif
    errcode=XLALSimInspiralSpinTaylorT4Engine(&omega1,&Phi1,&LNhatx1,&LNhaty1,&LNhatz1,&S1x1,&S1y1,&S1z1,&S2x1,&S2y1,&S2z1,&Energy1,-1,lengthH,0,yinit,PhenSpinTaylorRD,&params);
    /* report on abnormal termination*/
    if ( (errcode != LALSIMINSPIRAL_PHENSPIN_TEST_FREQBOUND) ) {
#if DEBUG_LEVEL
      fprintf(stderr,"** LALPSpinInspiralRD WARNING **: integration terminated with code %d.\n",errcode);
      fprintf(stderr,"   1025: Energy increases\n  1026: Omegadot -ve\n 1028: Omega NAN\n 1029: Omega > OmegaMatch\n 1031: Omega -ve\n");
      fprintf(stderr,"   Waveform parameters were m1 = %14.6e, m2 = %14.6e, inc = %10.6f,  fRef %10.4f Hz\n", m1, m2, iota, f_ref);
      fprintf(stderr,"                           S1 = (%10.6f,%10.6f,%10.6f)\n", s1x, s1y, s1z);
      fprintf(stderr,"                           S2 = (%10.6f,%10.6f,%10.6f)\n", s2x, s2y, s2z);
#endif
      XLALPrintError("** LALPSpinInspiralRD WARNING **: integration terminated with code %d.\n",errcode);
      XLALPrintError("   1025: Energy increases\n  1026: Omegadot -ve\n 1028: Omega NAN\n 1029: OMega > OmegaMatch\n 1031: Omega -ve\n");
      XLALPrintError("  Waveform parameters were m1 = %14.6e, m2 = %14.6e, inc = %10.6f,  fref %10.4f Hz\n", m1, m2, iota, f_ref);
      XLALPrintError("                           S1 = (%10.6f,%10.6f,%10.6f)\n", s1x, s1y, s1z);
      XLALPrintError("                           S2 = (%10.6f,%10.6f,%10.6f)\n", s2x, s2y, s2z);
    }

    REAL8TimeSeries *omega2, *Phi2, *LNhatx2, *LNhaty2, *LNhatz2;
    REAL8TimeSeries *S1x2, *S1y2, *S1z2, *S2x2, *S2y2, *S2z2, *Energy2;
    errcode=XLALSimIMRPhenSpinInitialize(hPlus,hCross,m1,m2,yinit,f_ref,-1.,deltaT,iota,phaseO,&params,waveFlags,testGRparams,&lengthH);
    if(errcode) XLAL_ERROR(XLAL_EFUNC);
    errcodeInt=XLALSimInspiralSpinTaylorT4Engine(&omega2,&Phi2,&LNhatx2,&LNhaty2,&LNhatz2,&S1x2,&S1y2,&S1z2,&S2x2,&S2y2,&S2z2,&Energy2,1,lengthH,0,yinit,PhenSpinTaylorRD,&params);

    REAL8 phiRef=Phi1->data->data[omega1->data->length-1];

    errcode =XLALAppendTSandFree(omega1,omega2,omega);
    errcode+=XLALAppendTSandFree(Phi1,Phi2,Phi);
    errcode+=XLALAppendTSandFree(LNhatx1,LNhatx2,LNhatx);
    errcode+=XLALAppendTSandFree(LNhaty1,LNhaty2,LNhaty);
    errcode+=XLALAppendTSandFree(LNhatz1,LNhatz2,LNhatz);
    errcode+=XLALAppendTSandFree(S1x1,S1x2,S1x);
    errcode+=XLALAppendTSandFree(S1y1,S1y2,S1y);
    errcode+=XLALAppendTSandFree(S1z1,S1z2,S1z);
    errcode+=XLALAppendTSandFree(S2x1,S2x2,S2x);
    errcode+=XLALAppendTSandFree(S2y1,S2y2,S2y);
    errcode+=XLALAppendTSandFree(S2z1,S2z2,S2z);

    intLen=Phi->data->length;
    for (idx=0;idx<(int)intLen;idx++) Phi->data->data[idx]-=phiRef;

  }

  /* report on abnormal termination*/
  if ( (errcodeInt != LALSIMINSPIRAL_PHENSPIN_TEST_OMEGAMATCH) ) {
#if DEBUG_LEVEL
      fprintf(stderr,"** LALPSpinInspiralRD WARNING **: integration terminated with code %d.\n",errcode);
      fprintf(stderr,"   1025: Energy increases\n  1026: Omegadot -ve\n  1027: Freqbound\n 1028: Omega NAN\n  1031: Omega -ve\n");
      fprintf(stderr,"   Waveform parameters were m1 = %14.6e, m2 = %14.6e, inc = %10.6f,\n", m1, m2, iota);
      fprintf(stderr,"                           S1 = (%10.6f,%10.6f,%10.6f)\n", s1x, s1y, s1z);
      fprintf(stderr,"                           S2 = (%10.6f,%10.6f,%10.6f)\n", s2x, s2y, s2z);
#endif
      XLALPrintError("** LALPSpinInspiralRD WARNING **: integration terminated with code %d.\n",errcode);
      XLALPrintError("   1025: Energy increases\n  1026: Omegadot -ve\n  1027: Freqbound\n 1028: Omega NAN\n  1031: Omega -ve\n");
      XLALPrintError("  Waveform parameters were m1 = %14.6e, m2 = %14.6e, inc = %10.6f,\n", m1, m2, iota);
      XLALPrintError("                           S1 = (%10.6f,%10.6f,%10.6f)\n", s1x, s1y, s1z);
      XLALPrintError("                           S2 = (%10.6f,%10.6f,%10.6f)\n", s2x, s2y, s2z);
  }

  int count=intLen;
  double tPeak=0.,tm=0.;
  LIGOTimeGPS tStart=LIGOTIMEGPSZERO;
  COMPLEX16TimeSeries* hL2=XLALCreateCOMPLEX16TimeSeries( "hL2", &tStart, 0., deltaT, &lalDimensionlessUnit, 5*intLen);
  COMPLEX16TimeSeries* hL3=XLALCreateCOMPLEX16TimeSeries( "hL3", &tStart, 0., deltaT, &lalDimensionlessUnit, 7*intLen);
  COMPLEX16TimeSeries* hL4=XLALCreateCOMPLEX16TimeSeries( "hL4", &tStart, 0., deltaT, &lalDimensionlessUnit, 9*intLen);
  COMPLEX16Vector* hL2tmp=XLALCreateCOMPLEX16Vector(5);
  COMPLEX16Vector* hL3tmp=XLALCreateCOMPLEX16Vector(7);
  COMPLEX16Vector* hL4tmp=XLALCreateCOMPLEX16Vector(9);
  LALSimInspiralInclAngle trigAngle;

  REAL8 amp22ini = -2.0 * m1*m2/(m1+m2) * LAL_G_SI/pow(LAL_C_SI,3.) / r * sqrt(16. * LAL_PI / 5.);
  REAL8 amp33ini = -amp22ini * sqrt(5./42.)/4.;
  REAL8 amp44ini = amp22ini * sqrt(5./7.) * 2./9.;
  REAL8 alpha,v,v2,Psi,om;
  REAL8 eta=m1*m2/(m1+m2)/(m1+m2);
  REAL8 dm=(m1-m2)/(m1+m2);
  LALSimInspiralModesChoice modesChoice=XLALSimInspiralGetModesChoice(waveFlags);

  for (idx=0;idx<(int)intLen;idx++) {
    om=omega->data->data[idx];
    v=cbrt(om);
    v2=v*v;
    Psi=Phi->data->data[idx] -2.*om*(1.-eta*v2)*log(om);
    errcode =XLALSimInspiralComputeAlpha(mass1,mass2,LNhatx->data->data[idx],LNhaty->data->data[idx],S1x->data->data[idx],S1y->data->data[idx],S2x->data->data[idx],S2y->data->data[idx],&alpha);
    errcode+=XLALSimInspiralComputeInclAngle(LNhatz->data->data[idx],&trigAngle);
    errcode+=XLALSimSpinInspiralFillL2Modes(hL2tmp,v,eta,dm,Psi,alpha,&trigAngle);
    for (kdx=0;kdx<5;kdx++) hL2->data->data[5*idx+kdx]=hL2tmp->data[kdx]*amp22ini*v2;
    errcode+=XLALSimSpinInspiralFillL3Modes(hL3tmp,v,eta,dm,Psi,alpha,&trigAngle);
    for (kdx=0;kdx<7;kdx++) hL3->data->data[7*idx+kdx]=hL3tmp->data[kdx]*amp33ini*v2;
    errcode+=XLALSimSpinInspiralFillL4Modes(hL4tmp,v,eta,dm,Psi,alpha,&trigAngle);
    for (kdx=0;kdx<9;kdx++) hL4->data->data[9*idx+kdx]=hL4tmp->data[kdx]*amp44ini*v2*v2;
  }

  REAL8TimeSeries *hPtmp=XLALCreateREAL8TimeSeries( "hPtmp", &tStart, 0., deltaT, &lalDimensionlessUnit, intLen);
  REAL8TimeSeries *hCtmp=XLALCreateREAL8TimeSeries( "hCtmp", &tStart, 0., deltaT, &lalDimensionlessUnit, intLen);
  COMPLEX16TimeSeries *hLMtmp=XLALCreateCOMPLEX16TimeSeries( "hLMtmp", &tStart, 0., deltaT, &lalDimensionlessUnit, intLen);
  int m;
  int l=2;
  for (m=-l;m<=l;m++) {
    for (idx=0;idx<(int)intLen;idx++)
      hLMtmp->data->data[idx]=hL2->data->data[(m+l)+idx*(2*l+1)];
    XLALSimAddMode(hPtmp,hCtmp,hLMtmp,iota,0.,l,m,0);
  }
  l=3;
  for (m=-l;m<=l;m++) {
    for (idx=0;idx<(int)intLen;idx++)
      hLMtmp->data->data[idx]=hL3->data->data[(m+l)+idx*(2*l+1)];
    XLALSimAddMode(hPtmp,hCtmp,hLMtmp,iota,0.,l,m,0);
  }
  l=4;
  for (m=-l;m<=l;m++) {
    for (idx=0;idx<(int)intLen;idx++)
      hLMtmp->data->data[idx]=hL4->data->data[(m+l)+idx*(2*l+1)];
    XLALSimAddMode(hPtmp,hCtmp,hLMtmp,iota,0.,l,m,0);
  }

  if (errcodeInt==LALSIMINSPIRAL_PHENSPIN_TEST_OMEGAMATCH) {
    REAL8 LNhS1,LNhS2,S1S2;
    REAL8 omegaMatch;
    REAL8 m1ByMsq=pow(params.m1ByM,2.);    
    REAL8 m2ByMsq=pow(params.m2ByM,2.);
    INT4 iMatch=0;
    INT4 nPts=minIntLen;
    idx=intLen;
    do {
      idx--;
      LNhS1=(LNhatx->data->data[idx]*S1x->data->data[idx]+LNhaty->data->data[idx]*S1y->data->data[idx]+LNhatz->data->data[idx]*S1z->data->data[idx])/m1ByMsq/m1ByMsq;
      LNhS2=(LNhatx->data->data[idx]*S2x->data->data[idx]+LNhaty->data->data[idx]*S2y->data->data[idx]+LNhatz->data->data[idx]*S2z->data->data[idx])/m2ByMsq/m2ByMsq;
      S1S2=(S1x->data->data[idx]*S2x->data->data[idx]+S1y->data->data[idx]*S2y->data->data[idx]+S1z->data->data[idx]*S2z->data->data[idx])/m1ByMsq/m2ByMsq;
      omegaMatch=OmMatch(LNhS1,LNhS2,S1S1,S1S2,S2S2);
      if ((omegaMatch>omega->data->data[idx])&&(omega->data->data[idx]<0.1)) {
	if (omega->data->data[idx-1]<omega->data->data[idx]) iMatch=idx;
	// The numerical integrator sometimes stops and stores twice the last
	// omega value, this 'if' instruction avoids keeping two identical 
	// values of omega at the end of the integration.
      }
    } while ((idx>0)&&(iMatch==0));

    INT4 iCpy=0;
    while ( ((iMatch+iCpy)<(int)intLen)&&(omega->data->data[iMatch+iCpy+1]>omega->data->data[iMatch+iCpy])&&(omega->data->data[iMatch+iCpy+1]<0.1)&&(iCpy<5) ) {
      iCpy++;
    }

    //We keep until the point where omega > omegaMatch for better derivative
    //computation, but do the matching at the last point at which
    // omega < omegaMatch

    REAL8Vector *omega_s   = XLALCreateREAL8Vector(nPts);
    REAL8Vector *LNhx_s    = XLALCreateREAL8Vector(nPts);
    REAL8Vector *LNhy_s    = XLALCreateREAL8Vector(nPts);
    REAL8Vector *LNhz_s    = XLALCreateREAL8Vector(nPts);
		
    REAL8Vector *domega    = XLALCreateREAL8Vector(nPts);
    REAL8Vector *dLNhx     = XLALCreateREAL8Vector(nPts);
    REAL8Vector *dLNhy     = XLALCreateREAL8Vector(nPts);
    REAL8Vector *dLNhz     = XLALCreateREAL8Vector(nPts);
    REAL8Vector *diota     = XLALCreateREAL8Vector(nPts);
    REAL8Vector *dalpha    = XLALCreateREAL8Vector(nPts);
		
    REAL8Vector *ddomega   = XLALCreateREAL8Vector(nPts);
    REAL8Vector *ddiota    = XLALCreateREAL8Vector(nPts);
    REAL8Vector *ddalpha   = XLALCreateREAL8Vector(nPts);

    int jMatch=nPts-iCpy-1;		
    idx=iMatch-nPts+iCpy+1;
    for (jdx=0;jdx<nPts;jdx++) {
      omega_s->data[jdx]  = omega->data->data[idx];
      LNhx_s->data[jdx]   = LNhatx->data->data[idx];
      LNhy_s->data[jdx]   = LNhaty->data->data[idx];
      LNhz_s->data[jdx]   = LNhatz->data->data[idx];
      idx++;
    }

    errcode  = XLALGenerateWaveDerivative(domega,omega_s,deltaT);
    errcode += XLALGenerateWaveDerivative(dLNhx,LNhx_s,deltaT);
    errcode += XLALGenerateWaveDerivative(dLNhy,LNhy_s,deltaT);
    errcode += XLALGenerateWaveDerivative(dLNhz,LNhz_s,deltaT);
    errcode += XLALGenerateWaveDerivative(ddomega,domega,deltaT);
    errcode += XLALGenerateWaveDerivative(ddiota,diota,deltaT);
    errcode += XLALGenerateWaveDerivative(ddalpha,dalpha,deltaT);
    for (idx=0;idx<(int)domega->length;idx++)
    if ( (errcode != 0) || (domega->data[jMatch]<0.) || (ddomega->data[jMatch]<0.) ) {
#if DEBUG_LEVEL
      fprintf(stderr,"**** LALSimIMRPhenSpinInspiralRD ERROR ****: error generating derivatives");
      fprintf(stderr,"                     m:           : %12.5f  %12.5f\n",m1,m2);
      fprintf(stderr,"              S1:                 : %12.5f  %12.5f  %12.5f\n",s1x,s1y,s1z);
      fprintf(stderr,"              S2:                 : %12.5f  %12.5f  %12.5f\n",s2x,s2y,s2z);
      fprintf(stderr,"     omM %12.5f   om[%d] %12.5f\n",omegaMatch,iMatch,omega->data->data[iMatch]);
#if DEBUG_LEVEL
#endif
      XLALPrintError("**** LALSimIMRPhenSpinInspiralRD ERROR ****: error generating derivatives");
      XLALPrintError("                     m:           : %12.5f  %12.5f\n",m1,m2);
      XLALPrintError("              S1:                 : %12.5f  %12.5f  %12.5f\n",s1x,s1y,s1z);
      XLALPrintError("              S2:                 : %12.5f  %12.5f  %12.5f\n",s2x,s2y,s2z);
      XLALPrintError("     omM %12.5f   om[%d] %12.5f\n",omegaMatch,jMatch,omega);

#endif
      XLALPrintError("**** LALSimIMRPhenSpinInspiralRD ERROR ****: error generating derivatives");
      XLAL_ERROR(XLAL_EFAILED);
    }
    else {
      REAL8 LNhxy;
      for (idx=0;idx<nPts;idx++) {
	LNhxy = sqrt(LNhx_s->data[idx] * LNhx_s->data[idx] + LNhy_s->data[idx] * LNhy_s->data[idx]);
	if (LNhxy > 0.) {
	  diota->data[idx]  = -dLNhz->data[idx] / LNhxy;
	  dalpha->data[idx] = (LNhx_s->data[idx] * dLNhy->data[idx] - LNhy_s->data[idx] * dLNhx->data[idx]) / LNhxy;
	} else {
	  diota->data[idx]  = 0.;
	  dalpha->data[idx] = 0.;
	}
      }
    }

    REAL8 t0;
    tm = t0 = ((REAL8) intLen )*deltaT;
    REAL8 tAs  = t0 + 2. * domega->data[jMatch] / ddomega->data[jMatch];
    REAL8 om1  = domega->data[jMatch] * tAs * (1. - t0 / tAs) * (1. - t0 / tAs);
    REAL8 om0  = omega_s->data[jMatch] - om1 / (1. - t0 / tAs);

    REAL8 dalpha1 = ddalpha->data[jMatch] * tAs * (1. - t0 / tAs) * (1. - t0 / tAs);
    REAL8 dalpha0 = dalpha->data[jMatch] - dalpha1 / (1. - t0 / tAs);

    REAL8 Psi0;
    REAL8 alpha0,energy;

    if ((tAs < t0) || (om1 < 0.)) {
#if DEBUG_LEVEL
      fprintf(stderr,"**** LALSimIMRPhenSpinInspiralRD ERROR ****: Could not attach phen part for:\n");
      fprintf(stderr," tAs %12.6e  dom %12.6e  ddom %12.6e\n",tAs,domega->data[jMatch],ddomega->data[jMatch]);
      fprintf(stderr,"   m1 = %14.6e, m2 = %14.6e, inc = %10.6f,\n", mass1, mass2, iota);
      fprintf(stderr,"   S1 = (%10.6f,%10.6f,%10.6f)\n", s1x, s1y, s1z);
      fprintf(stderr,"   S2 = (%10.6f,%10.6f,%10.6f)\n", s2x, s2y, s2z);
#endif
      XLALPrintError("**** LALSimIMRPhenSpinInspiralRD ERROR ****: Could not attach phen part for:\n");
      XLALPrintError(" tAs %12.6e  t0 %12.6e  om1 %12.6e\n",tAs,t0,om1);
      XLALPrintError("   m1 = %14.6e, m2 = %14.6e, inc = %10.6f,\n", mass1, mass2, iota);
      XLALPrintError("   S1 = (%10.6f,%10.6f,%10.6f)\n", s1x, s1y, s1z);
      XLALPrintError("   S2 = (%10.6f,%10.6f,%10.6f)\n", s2x, s2y, s2z);
      XLAL_ERROR(XLAL_EFAILED);
    }
    else /*pippo*/ {
      XLALSimInspiralComputeInclAngle(LNhatz->data->data[iMatch],&trigAngle);
      om     = omega->data->data[iMatch];
      Psi    = Phi->data->data[iMatch] - 2. * om * log(om);
      Psi0   = Psi + tAs * (om1/(m1+m2) -dalpha1*trigAngle.ci) * log(1. - t0 / tAs);
      errcode =XLALSimInspiralComputeAlpha(m1,m2,LNhatx->data->data[iMatch],LNhaty->data->data[iMatch],S1x->data->data[iMatch],S1y->data->data[iMatch],S2x->data->data[iMatch],S2y->data->data[iMatch],&alpha);
      alpha0 = alpha + tAs * dalpha1 * log(1. - t0 / tAs);
      energy = Energy->data->data[iMatch];
      count  = intLen-1;

      /* Estimate final mass and spin*/
      REAL8 finalMass,finalSpin;
      errcode=XLALSimIMRPhenSpinFinalMassSpin(&finalMass,&finalSpin,m1,m2,S1S1,S2S2,LNhS1,LNhS2,S1S2,energy);

      /* Get QNM frequencies */
      COMPLEX16Vector *modefreqs=XLALCreateCOMPLEX16Vector(1);
      errcode+=XLALSimIMRPhenSpinGenerateQNMFreq(modefreqs, 2, 2, finalMass, finalSpin, Mass);
      if (errcode) {
#if DEBUG_LEVEL
        fprintf(stderr,"**** LALSimIMRPhenSpinInspiralRD ERROR ****: impossible to generate RingDown frequency\n");
        fprintf(stderr,"   m  (%11.4e  %11.4e)  f0 %11.4e\n",mass1, mass2, f_min);
        fprintf(stderr,"   S1 (%8.4f  %8.4f  %8.4f)\n", s1x,s1y,s1z);
	fprintf(stderr,"   S2 (%8.4f  %8.4f  %8.4f)\n", s2x,s2y,s2z);
#endif
        XLALPrintError("**** LALSimIMRPhenSpinInspiralRD ERROR ****: impossible to generate RingDown frequency\n");
        XLALPrintError( "   m  (%11.4e  %11.4e)  f0 %11.4e\n",mass1, mass2, f_min);
        XLALPrintError( "   S1 (%8.4f  %8.4f  %8.4f)\n", s1x,s1y,s1z);
        XLALPrintError( "   S2 (%8.4f  %8.4f  %8.4f)\n", s2x,s2y,s2z);
        XLALDestroyCOMPLEX16Vector(modefreqs);
        XLAL_ERROR(XLAL_EFAILED);
      }

      REAL8 omegaRD = creal(modefreqs->data[0])*Mass*LAL_MTSUN_SI/LAL_PI/2.;
      REAL8 frOmRD  = fracRD(LNhS1,LNhS2,S1S1,S1S2,S2S2)*omegaRD;

      v     = cbrt(om);
      v2    = v*v;
      REAL8 amp22 = amp22ini*v2;
      REAL8 amp33,amp44;
      REAL8 v2old;

      do {
	count++;
        tm += deltaT;
        v2old = v2;
        om    = om1 / (1. - tm / tAs) + om0;
        Psi   = Psi0 + (- tAs * (om1/Mass-dalpha1*trigAngle.ci) * log(1. - tm / tAs) + (om0/Mass-dalpha0*trigAngle.ci) * (tm - t0) );
        alpha = alpha0 + ( dalpha0 * (tm - t0) - dalpha1 * tAs * log(1. - tm / tAs) );
        v     = cbrt(om);
        v2    = v*v;
        amp22*= v2 / v2old;
        amp33 = -amp22 / 4. * sqrt(5. / 42.);
        amp44 = amp22 * sqrt(5./7.) * 2./9.   * v2;

	errcode=XLALSimSpinInspiralFillL2Modes(hL2tmp,v,eta,dm,Psi,alpha,&trigAngle);
	for (kdx=0;kdx<5;kdx++) hL2->data->data[5*count+kdx]=hL2tmp->data[kdx]*amp22ini*v2;
	errcode+=XLALSimSpinInspiralFillL3Modes(hL3tmp,v,eta,dm,Psi,alpha,&trigAngle);
	for (kdx=0;kdx<7;kdx++) hL3->data->data[7*count+kdx]=hL3tmp->data[kdx]*amp33ini*v2;
	errcode+=XLALSimSpinInspiralFillL4Modes(hL4tmp,v,eta,dm,Psi,alpha,&trigAngle);
	for (kdx=0;kdx<9;kdx++) hL4->data->data[9*count+kdx]=hL4tmp->data[kdx]*amp44ini*v2*v2;

      } while ( (om < frOmRD) && (tm < tAs) );

      tPeak=tm;

      /*--------------------------------------------------------------
       * Attach the ringdown waveform to the end of inspiral
       -------------------------------------------------------------*/

      static const int nPtsComb=6;
      REAL8Vector *waveR   = XLALCreateREAL8Vector( nPtsComb+2 );
      REAL8Vector *dwaveR  = XLALCreateREAL8Vector( nPtsComb+2 );
      REAL8Vector *ddwaveR = XLALCreateREAL8Vector( nPtsComb+2 );
      REAL8Vector *waveI   = XLALCreateREAL8Vector( nPtsComb+2 );
      REAL8Vector *dwaveI  = XLALCreateREAL8Vector( nPtsComb+2 );
      REAL8Vector *ddwaveI = XLALCreateREAL8Vector( nPtsComb+2 );
      REAL8VectorSequence *inspWaveR = XLALCreateREAL8VectorSequence( 3, nPtsComb );
      REAL8VectorSequence *inspWaveI = XLALCreateREAL8VectorSequence( 3, nPtsComb );

      int nRDWave = (INT4) (RD_EFOLDS / fabs(cimag(modefreqs->data[0])) / deltaT);
      REAL8Vector *matchrange=XLALCreateREAL8Vector(3);
      matchrange->data[0]=count*deltaT;
      matchrange->data[1]=(count-nPtsComb+1)*deltaT;
      matchrange->data[2]=0.;

     /* Check memory was allocated */
      if ( !waveR || !dwaveR || !ddwaveR || !waveI || !dwaveI || !ddwaveI || !inspWaveR || !inspWaveI ) {
        XLALDestroyCOMPLEX16Vector( modefreqs );
        if (waveR)   XLALDestroyREAL8Vector( waveR );
        if (dwaveR)  XLALDestroyREAL8Vector( dwaveR );
        if (ddwaveR) XLALDestroyREAL8Vector( ddwaveR );
        if (waveI)   XLALDestroyREAL8Vector( waveI );
        if (dwaveI)  XLALDestroyREAL8Vector( dwaveI );
        if (ddwaveI) XLALDestroyREAL8Vector( ddwaveI );
        if (inspWaveR) XLALDestroyREAL8VectorSequence( inspWaveR );
        if (inspWaveI) XLALDestroyREAL8VectorSequence( inspWaveI );
        XLAL_ERROR( XLAL_ENOMEM );
      }

      double fSpin;
      int mm;
      int startComb=count-nPtsComb-1;
      hLMtmp = XLALResizeCOMPLEX16TimeSeries(hLMtmp, intLen, count-intLen+nRDWave);
      hPtmp  = XLALResizeREAL8TimeSeries(hPtmp,  intLen, count+nRDWave);
      hCtmp  = XLALResizeREAL8TimeSeries(hCtmp,  intLen, count+nRDWave);

      if ( ( modesChoice &  LAL_SIM_INSPIRAL_MODES_CHOICE_RESTRICTED) ==  LAL_SIM_INSPIRAL_MODES_CHOICE_RESTRICTED ) {
	REAL8Vector *rdwave1l2 = XLALCreateREAL8Vector( nRDWave );
	REAL8Vector *rdwave2l2 = XLALCreateREAL8Vector( nRDWave );
	memset( rdwave1l2->data, 0, rdwave1l2->length * sizeof( REAL8 ) );
	memset( rdwave2l2->data, 0, rdwave2l2->length * sizeof( REAL8 ) );
	l=2;
	for (m=-l;m<=l;m++) {
	  for (idx=0;idx<nPtsComb+2;idx++) {
	    waveR->data[idx]=creal(hL2->data->data[5*(startComb+idx)+(m+l)]);
	    waveI->data[idx]=cimag(hL2->data->data[5*(startComb+idx)+(m+l)]);
	  }
	  errcode =XLALGenerateWaveDerivative(waveR,dwaveR,deltaT);
	  errcode+=XLALGenerateWaveDerivative(dwaveR,ddwaveR,deltaT);
	  errcode+=XLALGenerateWaveDerivative(waveI,dwaveI,deltaT);
	  errcode+=XLALGenerateWaveDerivative(dwaveI,ddwaveI,deltaT);
	  for (idx=0;idx<nPtsComb;idx++) {
	    inspWaveR->data[idx]            =waveR->data[idx+1];
	    inspWaveR->data[idx+  nPtsComb] =dwaveR->data[idx+1];
	    inspWaveR->data[idx+2*nPtsComb] =ddwaveR->data[idx+1];
	    inspWaveI->data[idx]            =waveI->data[idx+1];
	    inspWaveI->data[idx+  nPtsComb] =dwaveI->data[idx+1];
	    inspWaveI->data[idx+2*nPtsComb] =ddwaveI->data[idx+1];
	  }

	  XLALDestroyCOMPLEX16Vector(modefreqs);
	  modefreqs=XLALCreateCOMPLEX16Vector(nModes);
	  if (m<0) {
	    mm=-m;
	    fSpin=-finalSpin;
	  }
	  else {
	    mm=m;
	    fSpin=finalSpin;
	  }
	  errcode+=XLALSimIMRPhenSpinGenerateQNMFreq(modefreqs, l, mm, finalMass, fSpin, Mass);
	  errcode+=XLALSimIMRHybridRingdownWave(rdwave1l2,rdwave2l2,deltaT,mass1,mass2,inspWaveR,inspWaveI,modefreqs,matchrange);
	  for (idx=intLen;idx<count;idx++) hLMtmp->data->data[idx-intLen]=hL2->data->data[5*idx+(l+m)];
	  for (idx=0;idx<nRDWave;idx++)    hLMtmp->data->data[count-intLen+idx]=rdwave1l2->data[idx]+I*rdwave2l2->data[idx];
	  XLALSimAddMode(hPtmp,hCtmp,hLMtmp,iota,0.,l,m,0);
	}
	XLALDestroyREAL8Vector(rdwave1l2);
	XLALDestroyREAL8Vector(rdwave2l2);
      }
      if ( ( modesChoice &  LAL_SIM_INSPIRAL_MODES_CHOICE_3L) ==  LAL_SIM_INSPIRAL_MODES_CHOICE_3L ) {
	REAL8Vector *rdwave1l3 = XLALCreateREAL8Vector( nRDWave );
	REAL8Vector *rdwave2l3 = XLALCreateREAL8Vector( nRDWave );
	l=3;
	for (m=-l;m<=l;m++) {
	  for (idx=0;idx<nPtsComb+2;idx++) {
	    waveR->data[idx]=creal(hL3->data->data[7*(startComb+idx)+(m+l)]);
	    waveI->data[idx]=cimag(hL3->data->data[7*(startComb+idx)+(m+l)]);
	  }
	  errcode =XLALGenerateWaveDerivative(waveR,dwaveR,deltaT);
	  errcode+=XLALGenerateWaveDerivative(dwaveR,ddwaveR,deltaT);
	  errcode+=XLALGenerateWaveDerivative(waveI,dwaveI,deltaT);
	  errcode+=XLALGenerateWaveDerivative(dwaveI,ddwaveI,deltaT);
	  for (idx=0;idx<nPtsComb;idx++) {
	    inspWaveR->data[idx]            = waveR->data[idx+1];
	    inspWaveR->data[idx+  nPtsComb] = dwaveR->data[idx+1];
	    inspWaveR->data[idx+2*nPtsComb] = ddwaveR->data[idx+1];
	    inspWaveI->data[idx]            = waveI->data[idx+1];
	    inspWaveI->data[idx+  nPtsComb] = dwaveI->data[idx+1];
	    inspWaveI->data[idx+2*nPtsComb] = ddwaveI->data[idx+1];
	  }
	  if (m<0) {
	    mm=-m;
	    fSpin=-finalSpin;
	  }
	  else {
	    mm=m;
	    fSpin=finalSpin;
	  }
	  errcode+=XLALSimIMRPhenSpinGenerateQNMFreq(modefreqs, l, m, finalMass, finalSpin, Mass);
	  errcode+=XLALSimIMRHybridRingdownWave(rdwave1l3,rdwave2l3,deltaT,mass1,mass2,inspWaveR,inspWaveI,modefreqs,matchrange);
	  for (idx=intLen;idx<count;idx++) hLMtmp->data->data[idx-intLen]=hL2->data->data[5*idx+(l+m)];
	  for (idx=0;idx<nRDWave;idx++)    hLMtmp->data->data[count-intLen+idx]=rdwave1l3->data[idx]+I*rdwave2l3->data[idx];
	  XLALSimAddMode(hPtmp,hCtmp,hLMtmp,iota,0.,l,m,0);
	}
	XLALDestroyREAL8Vector(rdwave1l3);
	XLALDestroyREAL8Vector(rdwave2l3);
      }
      if ( ( modesChoice &  LAL_SIM_INSPIRAL_MODES_CHOICE_4L) ==  LAL_SIM_INSPIRAL_MODES_CHOICE_4L ) {
	REAL8Vector *rdwave1l4 = XLALCreateREAL8Vector( nRDWave );
	REAL8Vector *rdwave2l4 = XLALCreateREAL8Vector( nRDWave );
	l=4;
	for (m=-l;m<=l;m++) {
	  for (idx=0;idx<nPtsComb+2;idx++) {
	    waveR->data[idx]=creal(hL2->data->data[9*(startComb+idx)+(m+l)]);
	    waveI->data[idx]=cimag(hL2->data->data[9*(startComb+idx)+(m+l)]);
	  }
	  errcode =XLALGenerateWaveDerivative(waveR,dwaveR,deltaT);
	  errcode+=XLALGenerateWaveDerivative(dwaveR,ddwaveR,deltaT);
	  errcode+=XLALGenerateWaveDerivative(waveI,dwaveI,deltaT);
	  errcode+=XLALGenerateWaveDerivative(dwaveI,ddwaveI,deltaT);
	  for (idx=0;idx<nPtsComb;idx++) {
	    inspWaveR->data[idx]            = waveR->data[idx+1];
	    inspWaveR->data[idx+  nPtsComb] = dwaveR->data[idx+1];
	    inspWaveR->data[idx+2*nPtsComb] = ddwaveR->data[idx+1];
	    inspWaveI->data[idx]            = waveI->data[idx+1];
	    inspWaveI->data[idx+  nPtsComb] = dwaveI->data[idx+1];
	    inspWaveI->data[idx+2*nPtsComb] = ddwaveI->data[idx+1];
	  }
	  if (m<0) {
	    mm=-m;
	    fSpin=-finalSpin;
	  }
	  else {
	    mm=m;
	    fSpin=finalSpin;
	  }
	  errcode+= XLALSimIMRPhenSpinGenerateQNMFreq(modefreqs,l,m, finalMass, finalSpin, Mass);
	  errcode+= XLALSimIMRHybridRingdownWave(rdwave1l4,rdwave2l4,deltaT,mass1,mass2,inspWaveR,
	                      inspWaveI,modefreqs,matchrange);
	  for (idx=intLen;idx<count;idx++) hLMtmp->data->data[idx-intLen]=hL2->data->data[5*idx+(l+m)];
	  for (idx=0;idx<nRDWave;idx++)    hLMtmp->data->data[count-intLen+idx]=rdwave1l4->data[idx]+I*rdwave2l4->data[idx];
	  XLALSimAddMode(hPtmp,hCtmp,hLMtmp,iota,0.,l,m,0);
	}
	XLALDestroyREAL8Vector(rdwave1l4);
	XLALDestroyREAL8Vector(rdwave2l4);
      }

      XLALDestroyCOMPLEX16Vector( modefreqs );
      XLALDestroyREAL8Vector( waveR );
      XLALDestroyREAL8Vector( dwaveR );
      XLALDestroyREAL8Vector( ddwaveR );
      XLALDestroyREAL8Vector( waveI );
      XLALDestroyREAL8Vector( dwaveI );
      XLALDestroyREAL8Vector( ddwaveI );
      XLALDestroyREAL8VectorSequence( inspWaveR );
      XLALDestroyREAL8VectorSequence( inspWaveI );
      if (errcode) XLAL_ERROR( XLAL_EFUNC );

      count+=nRDWave;

    } /* End of if phen part not sane*/

  } /*End of if intreturn==LALPSIRDPN_TEST_OMEGAMATCH*/

  if ((*hPlus) && (*hCross)) {
    if ((*hPlus)->data->length!=(*hCross)->data->length) {
#if DEBUG_LEVEL
      fprintf(stderr,"*** LALSimIMRPSpinInspiralRD ERROR: h+ and hx differ in length: %d vs. %d\n",(*hPlus)->data->length,(*hCross)->data->length);
#endif
      XLALPrintError("*** LALSimIMRPSpinInspiralRD ERROR: h+ and hx differ in length: %d vs. %d\n",(*hPlus)->data->length,(*hCross)->data->length);
      XLAL_ERROR(XLAL_EFAILED);
    }
    else {
      if ((int)(*hPlus)->data->length<count) {
#if DEBUG_LEVEL
	fprintf(stderr,"*** LALSimIMRPSpinInspiralRD ERROR: h+ and hx too short: %d vs. %d\n",(*hPlus)->data->length,count);
#endif
	XLALPrintError("*** LALSimIMRPSpinInspiralRD ERROR: h+ and hx too short: %d vs. %d\n",(*hPlus)->data->length,count);
	XLAL_ERROR(XLAL_EFAILED);
      }
      else {
	XLALGPSAdd(&((*hPlus)->epoch),-tPeak);
	XLALGPSAdd(&((*hCross)->epoch),-tPeak);
      }
    }
  }
  else {
    XLALGPSAdd(&tStart,-tPeak);
    *hPlus  = XLALCreateREAL8TimeSeries("H+", &tStart, 0.0, deltaT, &lalDimensionlessUnit, count);
    *hCross = XLALCreateREAL8TimeSeries("Hx", &tStart, 0.0, deltaT, &lalDimensionlessUnit, count);
    if(*hPlus == NULL || *hCross == NULL)
      XLAL_ERROR(XLAL_ENOMEM);
  }
    
  for (idx=0;idx<(int)hPtmp->data->length;idx++) {
    (*hPlus)->data->data[idx] =hPtmp->data->data[idx];
    (*hCross)->data->data[idx]=hCtmp->data->data[idx];
  }

  for (idx=hPtmp->data->length;idx<(int)(*hPlus)->data->length;idx++) {
    (*hPlus)->data->data[idx] =0.;
    (*hCross)->data->data[idx]=0.;
  }

  return count;
} /* End of XLALSimIMRPhenSpinInspiralRDGenerator*/

int XLALSimIMRPhenSpinGenerateQNMFreq(COMPLEX16Vector *modefreqs,
				      UINT4 l,
				      INT4  m,
				      REAL8 finalMass,
				      REAL8 finalSpin,
				      REAL8 totalMass)
{
  static const int UNUSED nSpin=107;
  static const int UNUSED nMode=8;
  if (modefreqs->length>8) {
    XLALPrintError("*** LALSimIMRPhenSpinGenerateQNMFreq ERROR: number of modes limited to %d, %d requested\n",nMode,modefreqs->length);
    XLAL_ERROR(XLAL_EFUNC);
  }
  int idx;

  static const double afinallist[107] = {-0.9996, -0.9995, -0.9994, -0.9992, -0.999, -0.9989, -0.9988, 
  -0.9987, -0.9986, -0.9985, -0.998, -0.9975, -0.997, -0.996, -0.995, -0.994, -0.992, -0.99, -0.988, 
  -0.986, -0.984, -0.982, -0.98, -0.975, -0.97, -0.96, -0.95, -0.94, -0.92, -0.9, -0.88, -0.86, -0.84, 
  -0.82, -0.8, -0.78, -0.76, -0.74, -0.72, -0.7, -0.65, -0.6, -0.55, -0.5, -0.45, -0.4, -0.35, -0.3, 
  -0.25, -0.2, -0.15, -0.1, -0.05, 0, 0.05, 0.1, 0.15, 0.2, 0.25, 0.3, 0.35, 0.4, 0.45, 0.5, 0.55, 0.6, 
  0.65, 0.7, 0.72, 0.74, 0.76, 0.78, 0.8, 0.82, 0.84, 0.86, 0.88, 0.9, 0.92, 0.94, 0.95, 0.96, 0.97, 
  0.975, 0.98, 0.982, 0.984, 0.986, 0.988, 0.99, 0.992, 0.994, 0.995, 0.996, 0.997, 0.9975, 0.998, 
  0.9985, 0.9986, 0.9987, 0.9988, 0.9989, 0.999, 0.9992, 0.9994, 0.9995, 0.9996};

  const double reomegaqnm22[8][107] = {{ 0.270228, 0.276562, 0.280636, 0.285234, 0.287548, 0.288282, 0.288845, 0.289287, 0.289639, 0.289924, 0.290781, 0.291189, 0.291418, 0.291658, 0.291785, 0.291870, 0.291998, 0.292111, 0.292221, 0.292331, 0.292441, 0.292552, 0.292664, 0.292943, 0.293223, 0.293787, 0.294354, 0.294925, 0.296077, 0.297244, 0.298426, 0.299624, 0.300837, 0.302067, 0.303313, 0.304577, 0.305857, 0.307156, 0.308473, 0.309808, 0.313232, 0.316784, 0.320473, 0.324307, 0.328299, 0.332458, 0.336798, 0.341333, 0.346079, 0.351053, 0.356275, 0.361768, 0.367557, 0.373672, 0.380146, 0.387018, 0.394333, 0.402145, 0.410518, 0.419527, 0.429264, 0.439842, 0.451402, 0.464123, 0.478235, 0.494045, 0.511969, 0.532600, 0.541794, 0.551630, 0.562201, 0.573616, 0.586017, 0.599580, 0.614539, 0.631206, 0.650018, 0.671614, 0.696995, 0.727875, 0.746320, 0.767674, 0.793208, 0.808235, 0.825429, 0.833100, 0.841343, 0.850272, 0.860046, 0.870893, 0.883162, 0.897446, 0.905664, 0.914902, 0.925581, 0.931689, 0.938524, 0.946385, 0.948123, 0.949929, 0.951813, 0.953784, 0.955854, 0.960358, 0.965514, 0.968438, 0.971690},
{ 0.243689, 0.247076, 0.248937, 0.250577, 0.251041, 0.251088, 0.251073, 0.251023, 0.250952, 0.250874, 0.250520, 0.250339, 0.250292, 0.250359, 0.250463, 0.250549, 0.250686, 0.250814, 0.250945, 0.251079, 0.251214, 0.251349, 0.251484, 0.251820, 0.252158, 0.252836, 0.253519, 0.254205, 0.255591, 0.256994, 0.258413, 0.259851, 0.261306, 0.262780, 0.264273, 0.265785, 0.267317, 0.268869, 0.270442, 0.272036, 0.276117, 0.280342, 0.284721, 0.289262, 0.293978, 0.298880, 0.303981, 0.309295, 0.314838, 0.320629, 0.326687, 0.333036, 0.339701, 0.346711, 0.354101, 0.361910, 0.370183, 0.378976, 0.388353, 0.398390, 0.409183, 0.420847, 0.433527, 0.447407, 0.462728, 0.479807, 0.499079, 0.521161, 0.530970, 0.541447, 0.552684, 0.564795, 0.577922, 0.592247, 0.608001, 0.625499, 0.645173, 0.667658, 0.693938, 0.725708, 0.744582, 0.766349, 0.792272, 0.807482, 0.824852, 0.832591, 0.840901, 0.849896, 0.859735, 0.870645, 0.882977, 0.897322, 0.905568, 0.914834, 0.925538, 0.931657, 0.938502, 0.946371, 0.948110, 0.949919, 0.951804, 0.953776, 0.955847, 0.960353, 0.965510, 0.968435, 0.971688},
{ 0.191626, 0.188311, 0.185222, 0.182107, 0.182532, 0.183173, 0.183784, 0.184279, 0.184634, 0.184860, 0.184811, 0.184353, 0.184263, 0.184581, 0.184713, 0.184720, 0.184862, 0.185055, 0.185205, 0.185357, 0.185521, 0.185687, 0.185850, 0.186250, 0.186658, 0.187473, 0.188294, 0.189121, 0.190789, 0.192478, 0.194189, 0.195923, 0.197680, 0.199460, 0.201263, 0.203092, 0.204945, 0.206823, 0.208728, 0.210659, 0.215608, 0.220737, 0.226056, 0.231577, 0.237312, 0.243272, 0.249472, 0.255926, 0.262651, 0.269665, 0.276988, 0.284642, 0.292654, 0.301053, 0.309873, 0.319153, 0.328939, 0.339285, 0.350255, 0.361927, 0.374396, 0.387779, 0.402225, 0.417925, 0.435130, 0.454179, 0.475545, 0.499906, 0.510701, 0.522217, 0.534557, 0.547847, 0.562240, 0.577926, 0.595148, 0.614222, 0.635576, 0.659827, 0.687923, 0.721496, 0.741239, 0.763831, 0.790516, 0.806079, 0.823779, 0.831643, 0.840075, 0.849189, 0.859144, 0.870168, 0.882612, 0.897067, 0.905368, 0.914687, 0.925444, 0.931587, 0.938454, 0.946343, 0.948085, 0.949897, 0.951785, 0.953760, 0.955833, 0.960343, 0.965503, 0.968430, 0.971683},
{ 0.127766, 0.134925, 0.137314, 0.135026, 0.132545, 0.132613, 0.133213, 0.133927, 0.134502, 0.134846, 0.134197, 0.133839, 0.134383, 0.134311, 0.134305, 0.134548, 0.134538, 0.134764, 0.134855, 0.134990, 0.135153, 0.135276, 0.135401, 0.135760, 0.136094, 0.136791, 0.137494, 0.138199, 0.139634, 0.141097, 0.142587, 0.144106, 0.145656, 0.147236, 0.148849, 0.150493, 0.152171, 0.153884, 0.155632, 0.157416, 0.162043, 0.166920, 0.172065, 0.177495, 0.183228, 0.189280, 0.195670, 0.202416, 0.209537, 0.217053, 0.224984, 0.233352, 0.242183, 0.251505, 0.261348, 0.271749, 0.282749, 0.294397, 0.306753, 0.319886, 0.333884, 0.348856, 0.364938, 0.382309, 0.401210, 0.421977, 0.445099, 0.471336, 0.482957, 0.495375, 0.508729, 0.523187, 0.538956, 0.556285, 0.575464, 0.596830, 0.620788, 0.647869, 0.678894, 0.715333, 0.736426, 0.760279, 0.788101, 0.804173, 0.822338, 0.830374, 0.838970, 0.848242, 0.858347, 0.869515, 0.882100, 0.896696, 0.905068, 0.914460, 0.925289, 0.931468, 0.938371, 0.946292, 0.948041, 0.949858, 0.951752, 0.953732, 0.955811, 0.960329, 0.965496, 0.968424, 0.971679},
{ 0.111845, 0.106167, 0.107777, 0.111206, 0.108936, 0.108364, 0.108639, 0.109304, 0.109887, 0.110167, 0.109102, 0.109714, 0.109696, 0.109638, 0.109676, 0.109676, 0.109779, 0.109961, 0.109986, 0.110136, 0.110210, 0.110304, 0.110427, 0.110658, 0.110906, 0.111408, 0.111916, 0.112428, 0.113458, 0.114508, 0.115576, 0.116663, 0.117771, 0.118902, 0.120054, 0.121230, 0.122431, 0.123659, 0.124914, 0.126199, 0.129550, 0.133123, 0.136951, 0.141068, 0.145514, 0.150331, 0.155561, 0.161252, 0.167448, 0.174197, 0.181540, 0.189518, 0.198166, 0.207515, 0.217594, 0.228433, 0.240060, 0.252511, 0.265825, 0.280057, 0.295269, 0.311546, 0.328992, 0.347741, 0.367967, 0.389902, 0.413872, 0.440385, 0.451887, 0.464034, 0.476972, 0.490925, 0.506263, 0.523592, 0.543861, 0.568205, 0.597077, 0.629836, 0.666171, 0.707148, 0.730203, 0.755806, 0.785154, 0.801892, 0.820650, 0.828901, 0.837698, 0.847157, 0.857436, 0.868766, 0.881504, 0.896248, 0.904696, 0.914167, 0.925079, 0.931302, 0.938248, 0.946214, 0.947971, 0.949797, 0.951699, 0.953687, 0.955773, 0.960306, 0.965484, 0.968417, 0.971675},
{ 0.097352, 0.098752, 0.095213, 0.097231, 0.096805, 0.095906, 0.095838, 0.096399, 0.096960, 0.097159, 0.096291, 0.096883, 0.096445, 0.096717, 0.096813, 0.096745, 0.096896, 0.097015, 0.097036, 0.097175, 0.097226, 0.097351, 0.097408, 0.097640, 0.097875, 0.098312, 0.098759, 0.099209, 0.100108, 0.101005, 0.101904, 0.102805, 0.103707, 0.104611, 0.105520, 0.106431, 0.107346, 0.108266, 0.109192, 0.110125, 0.112493, 0.114931, 0.117463, 0.120121, 0.122943, 0.125975, 0.129278, 0.132931, 0.137032, 0.141710, 0.147116, 0.153415, 0.160767, 0.169299, 0.179085, 0.190140, 0.202441, 0.215947, 0.230623, 0.246457, 0.263464, 0.281691, 0.301218, 0.322157, 0.344655, 0.368896, 0.395090, 0.423450, 0.435430, 0.447764, 0.460418, 0.473311, 0.486283, 0.499037, 0.511101, 0.522095, 0.532461, 0.536917, 0.527548, 0.521465, 0.518438, 0.515417, 0.512410, 0.510911, 0.509415, 0.508818, 0.508221, 0.507624, 0.507028, 0.506432, 0.505837, 0.505242, 0.504944, 0.504647, 0.504350, 0.504202, 0.504054, 0.503904, 0.503874, 0.503845, 0.503817, 0.503789, 0.503760, 0.503692, 0.503638, 0.503632, 0.503585},
{ 0.084098, 0.086874, 0.087915, 0.085882, 0.087177, 0.086217, 0.085878, 0.086378, 0.086944, 0.087051, 0.086602, 0.086479, 0.086776, 0.086675, 0.086661, 0.086887, 0.086965, 0.087057, 0.087108, 0.087225, 0.087354, 0.087429, 0.087563, 0.087827, 0.088086, 0.088604, 0.089131, 0.089658, 0.090698, 0.091730, 0.092745, 0.093744, 0.094727, 0.095694, 0.096641, 0.097572, 0.098485, 0.099379, 0.100255, 0.101113, 0.103185, 0.105155, 0.107036, 0.108838, 0.110571, 0.112244, 0.113866, 0.115452, 0.117034, 0.118689, 0.120596, 0.123138, 0.127029, 0.133252, 0.142539, 0.154841, 0.169536, 0.185971, 0.203721, 0.222585, 0.242517, 0.263571, 0.285865, 0.309569, 0.334899, 0.362133, 0.391629, 0.423854, 0.437641, 0.452010, 0.467014, 0.482713, 0.499165, 0.516428, 0.534543, 0.553525, 0.573596, 0.602227, 0.649295, 0.697231, 0.722841, 0.750611, 0.781797, 0.799333, 0.818797, 0.827303, 0.836336, 0.846012, 0.856487, 0.867993, 0.880887, 0.895775, 0.904293, 0.913837, 0.924830, 0.931097, 0.938091, 0.946107, 0.947875, 0.949711, 0.951623, 0.953621, 0.955717, 0.960269, 0.965464, 0.968404, 0.971669},
{ 0.078336, 0.073380, 0.076620, 0.074432, 0.076411, 0.075479, 0.074940, 0.075503, 0.076100, 0.076029, 0.075987, 0.075511, 0.075776, 0.075832, 0.075971, 0.075923, 0.076114, 0.076229, 0.076321, 0.076418, 0.076597, 0.076721, 0.076841, 0.077174, 0.077519, 0.078210, 0.078912, 0.079614, 0.081016, 0.082412, 0.083796, 0.085153, 0.086479, 0.087770, 0.089021, 0.090230, 0.091394, 0.092513, 0.093588, 0.094617, 0.096993, 0.099091, 0.100921, 0.102489, 0.103795, 0.104821, 0.105530, 0.105854, 0.105679, 0.104817, 0.102968, 0.099711, 0.094996, 0.092822, 0.102996, 0.122468, 0.144233, 0.166117, 0.187881, 0.209753, 0.232042, 0.255048, 0.279057, 0.304354, 0.331244, 0.360084, 0.391319, 0.425537, 0.440234, 0.455604, 0.471729, 0.488705, 0.506652, 0.525720, 0.546107, 0.568081, 0.592057, 0.618799, 0.650217, 0.691189, 0.716737, 0.745623, 0.778341, 0.796657, 0.816858, 0.825638, 0.834931, 0.844846, 0.855539, 0.867237, 0.880294, 0.895318, 0.903898, 0.913504, 0.924565, 0.930871, 0.937910, 0.945976, 0.947755, 0.949602, 0.951525, 0.953535, 0.955643, 0.960217, 0.965434, 0.968355, 0.971639}};

  const double reomegaqnm21[8][107] = {{ 0.336609, 0.339386, 0.340852, 0.342219, 0.342818, 0.343002, 0.343143, 0.343254, 0.343344, 0.343417, 0.343641, 0.343748, 0.343804, 0.343853, 0.343871, 0.343879, 0.343886, 0.343891, 0.343896, 0.343902, 0.343909, 0.343915, 0.343922, 0.343941, 0.343960, 0.344002, 0.344049, 0.344101, 0.344220, 0.344359, 0.344517, 0.344696, 0.344896, 0.345115, 0.345356, 0.345617, 0.345899, 0.346201, 0.346525, 0.346870, 0.347824, 0.348911, 0.350132, 0.351491, 0.352990, 0.354633, 0.356423, 0.358366, 0.360469, 0.362738, 0.365183, 0.367812, 0.370637, 0.373672, 0.376931, 0.380432, 0.384197, 0.388248, 0.392615, 0.397330, 0.402436, 0.407979, 0.414020, 0.420632, 0.427909, 0.435968, 0.444968, 0.455121, 0.459569, 0.464271, 0.469259, 0.474564, 0.480231, 0.486308, 0.492859, 0.499965, 0.507729, 0.516291, 0.525845, 0.536673, 0.542693, 0.549213, 0.556329, 0.560146, 0.564155, 0.565814, 0.567505, 0.569227, 0.570976, 0.572749, 0.574535, 0.576322, 0.577208, 0.578084, 0.578948, 0.579374, 0.579795, 0.580212, 0.580295, 0.580377, 0.580460, 0.580541, 0.580623, 0.580784, 0.580942, 0.581018, 0.581093},
{ 0.313486, 0.314984, 0.315748, 0.316360, 0.316504, 0.316512, 0.316502, 0.316483, 0.316460, 0.316436, 0.316347, 0.316311, 0.316305, 0.316315, 0.316325, 0.316330, 0.316338, 0.316345, 0.316352, 0.316360, 0.316368, 0.316375, 0.316383, 0.316401, 0.316419, 0.316455, 0.316491, 0.316527, 0.316602, 0.316682, 0.316772, 0.316874, 0.316989, 0.317122, 0.317273, 0.317444, 0.317636, 0.317851, 0.318090, 0.318353, 0.319124, 0.320064, 0.321179, 0.322476, 0.323960, 0.325636, 0.327508, 0.329582, 0.331865, 0.334364, 0.337088, 0.340045, 0.343249, 0.346711, 0.350448, 0.354478, 0.358823, 0.363506, 0.368558, 0.374014, 0.379916, 0.386313, 0.393268, 0.400856, 0.409172, 0.418337, 0.428509, 0.439897, 0.444855, 0.450076, 0.455587, 0.461420, 0.467612, 0.474208, 0.481261, 0.488834, 0.497004, 0.505862, 0.515506, 0.526011, 0.531572, 0.537263, 0.542874, 0.545479, 0.547723, 0.548438, 0.548984, 0.549290, 0.549241, 0.548642, 0.547140, 0.544185, 0.542606, 0.542325, 0.541626, 0.540962, 0.540617, 0.540156, 0.540066, 0.539986, 0.539906, 0.539818, 0.539726, 0.539553, 0.539377, 0.539288, 0.539198},
{ 0.270281, 0.269419, 0.268655, 0.267935, 0.267923, 0.268009, 0.268102, 0.268185, 0.268251, 0.268297, 0.268334, 0.268285, 0.268273, 0.268312, 0.268342, 0.268359, 0.268398, 0.268442, 0.268483, 0.268523, 0.268562, 0.268601, 0.268640, 0.268733, 0.268822, 0.268989, 0.269141, 0.269280, 0.269520, 0.269718, 0.269881, 0.270016, 0.270132, 0.270235, 0.270333, 0.270431, 0.270536, 0.270653, 0.270785, 0.270937, 0.271426, 0.272100, 0.272992, 0.274121, 0.275504, 0.277153, 0.279078, 0.281288, 0.283792, 0.286598, 0.289716, 0.293156, 0.296931, 0.301053, 0.305540, 0.310410, 0.315684, 0.321389, 0.327554, 0.334218, 0.341421, 0.349217, 0.357668, 0.366852, 0.376864, 0.387825, 0.399890, 0.413261, 0.419032, 0.425075, 0.431414, 0.438075, 0.445088, 0.452488, 0.460310, 0.468593, 0.477369, 0.486650, 0.496390, 0.506348, 0.511184, 0.515607, 0.519030, 0.519992, 0.520100, 0.519830, 0.519361, 0.518710, 0.517943, 0.517204, 0.516728, 0.516696, 0.516098, 0.513376, 0.509813, 0.508083, 0.506412, 0.504789, 0.504468, 0.504148, 0.503830, 0.503511, 0.503194, 0.502559, 0.501923, 0.501605, 0.501286},
{ 0.212690, 0.214089, 0.215208, 0.215364, 0.214740, 0.214584, 0.214560, 0.214625, 0.214730, 0.214837, 0.214992, 0.214885, 0.214922, 0.215045, 0.215071, 0.215139, 0.215267, 0.215383, 0.215507, 0.215624, 0.215741, 0.215858, 0.215974, 0.216256, 0.216533, 0.217064, 0.217566, 0.218040, 0.218903, 0.219653, 0.220295, 0.220836, 0.221285, 0.221650, 0.221945, 0.222180, 0.222368, 0.222520, 0.222647, 0.222759, 0.223032, 0.223383, 0.223900, 0.224647, 0.225672, 0.227010, 0.228689, 0.230730, 0.233151, 0.235969, 0.239199, 0.242854, 0.246951, 0.251505, 0.256534, 0.262058, 0.268101, 0.274689, 0.281852, 0.289628, 0.298059, 0.307197, 0.317104, 0.327855, 0.339542, 0.352278, 0.366207, 0.381510, 0.388065, 0.394895, 0.402017, 0.409454, 0.417227, 0.425361, 0.433878, 0.442797, 0.452127, 0.461855, 0.471918, 0.482195, 0.487390, 0.492710, 0.498495, 0.501813, 0.505600, 0.507251, 0.508939, 0.510585, 0.512031, 0.513028, 0.513289, 0.512758, 0.512367, 0.512091, 0.511921, 0.511350, 0.509600, 0.507150, 0.506650, 0.506154, 0.505662, 0.505175, 0.504694, 0.503744, 0.502809, 0.502344, 0.501880},
{ 0.171239, 0.169224, 0.168634, 0.170053, 0.169938, 0.169613, 0.169418, 0.169408, 0.169531, 0.169696, 0.169825, 0.169724, 0.169920, 0.169916, 0.170075, 0.170140, 0.170344, 0.170511, 0.170710, 0.170886, 0.171073, 0.171258, 0.171437, 0.171892, 0.172337, 0.173213, 0.174064, 0.174889, 0.176453, 0.177895, 0.179203, 0.180372, 0.181395, 0.182273, 0.183007, 0.183606, 0.184081, 0.184444, 0.184710, 0.184896, 0.185105, 0.185120, 0.185113, 0.185222, 0.185550, 0.186176, 0.187164, 0.188562, 0.190413, 0.192751, 0.195604, 0.199000, 0.202962, 0.207515, 0.212679, 0.218480, 0.224939, 0.232083, 0.239940, 0.248543, 0.257928, 0.268139, 0.279228, 0.291258, 0.304303, 0.318456, 0.333830, 0.350563, 0.357675, 0.365045, 0.372687, 0.380617, 0.388855, 0.397422, 0.406346, 0.415670, 0.425462, 0.435857, 0.447149, 0.459990, 0.467349, 0.475546, 0.484594, 0.489367, 0.494295, 0.496339, 0.498459, 0.500692, 0.503080, 0.505635, 0.508214, 0.510197, 0.510572, 0.510387, 0.509864, 0.509635, 0.509456, 0.508790, 0.508443, 0.507999, 0.507470, 0.506881, 0.506257, 0.504974, 0.503704, 0.503082, 0.502466},
{ 0.136159, 0.137914, 0.137016, 0.136626, 0.137329, 0.137055, 0.136755, 0.136667, 0.136793, 0.136997, 0.136954, 0.137117, 0.137154, 0.137279, 0.137300, 0.137470, 0.137649, 0.137842, 0.138069, 0.138258, 0.138476, 0.138671, 0.138881, 0.139389, 0.139898, 0.140910, 0.141910, 0.142898, 0.144829, 0.146684, 0.148447, 0.150099, 0.151622, 0.153001, 0.154221, 0.155270, 0.156142, 0.156839, 0.157368, 0.157740, 0.158094, 0.157845, 0.157250, 0.156526, 0.155847, 0.155354, 0.155162, 0.155369, 0.156061, 0.157316, 0.159206, 0.161795, 0.165143, 0.169299, 0.174306, 0.180192, 0.186977, 0.194671, 0.203282, 0.212811, 0.223263, 0.234649, 0.246985, 0.260297, 0.274622, 0.290009, 0.306518, 0.324226, 0.331666, 0.339322, 0.347205, 0.355332, 0.363727, 0.372432, 0.381518, 0.391111, 0.401435, 0.412868, 0.425995, 0.441563, 0.450520, 0.460463, 0.471717, 0.477974, 0.484665, 0.487438, 0.490248, 0.493085, 0.495960, 0.498923, 0.502084, 0.505527, 0.507222, 0.508526, 0.508875, 0.508627, 0.508251, 0.507926, 0.507863, 0.507785, 0.507670, 0.507484, 0.507181, 0.506113, 0.504624, 0.503839, 0.503060},
{ 0.114342, 0.113963, 0.115141, 0.114074, 0.114860, 0.114789, 0.114485, 0.114338, 0.114452, 0.114665, 0.114512, 0.114807, 0.114670, 0.114821, 0.114995, 0.115025, 0.115231, 0.115438, 0.115661, 0.115846, 0.116050, 0.116249, 0.116452, 0.116951, 0.117454, 0.118454, 0.119460, 0.120464, 0.122464, 0.124443, 0.126383, 0.128266, 0.130073, 0.131781, 0.133368, 0.134813, 0.136090, 0.137179, 0.138065, 0.138742, 0.139567, 0.139335, 0.138333, 0.136834, 0.135064, 0.133208, 0.131418, 0.129826, 0.128564, 0.127775, 0.127629, 0.128336, 0.130133, 0.133252, 0.137863, 0.144021, 0.151672, 0.160684, 0.170902, 0.182186, 0.194429, 0.207556, 0.221528, 0.236327, 0.251955, 0.268421, 0.285744, 0.303950, 0.311492, 0.319195, 0.327076, 0.335168, 0.343522, 0.352232, 0.361443, 0.371382, 0.382361, 0.394771, 0.409089, 0.426092, 0.436065, 0.447400, 0.460415, 0.467675, 0.475563, 0.478922, 0.482402, 0.485993, 0.489665, 0.493377, 0.497113, 0.500995, 0.503086, 0.505273, 0.507186, 0.507677, 0.507628, 0.507177, 0.507077, 0.506981, 0.506892, 0.506806, 0.506714, 0.506383, 0.505391, 0.504576, 0.503665},
{ 0.099636, 0.098809, 0.098823, 0.098961, 0.099181, 0.099322, 0.099091, 0.098913, 0.099007, 0.099206, 0.099084, 0.099201, 0.099305, 0.099370, 0.099413, 0.099541, 0.099710, 0.099906, 0.100109, 0.100292, 0.100464, 0.100668, 0.100843, 0.101320, 0.101793, 0.102742, 0.103705, 0.104671, 0.106617, 0.108576, 0.110540, 0.112492, 0.114417, 0.116296, 0.118106, 0.119822, 0.121422, 0.122876, 0.124150, 0.125214, 0.126860, 0.127085, 0.126123, 0.124264, 0.121765, 0.118824, 0.115576, 0.112102, 0.108440, 0.104598, 0.100602, 0.096644, 0.093455, 0.092822, 0.096761, 0.105267, 0.116748, 0.129817, 0.143719, 0.158124, 0.172920, 0.188094, 0.203673, 0.219697, 0.236201, 0.253208, 0.270727, 0.288784, 0.296181, 0.303707, 0.311404, 0.319342, 0.327628, 0.336416, 0.345907, 0.356330, 0.367923, 0.380968, 0.395965, 0.413866, 0.424377, 0.436366, 0.450442, 0.458506, 0.467375, 0.471171, 0.475126, 0.479254, 0.483568, 0.488052, 0.492639, 0.497232, 0.499557, 0.501993, 0.504598, 0.505838, 0.506697, 0.506742, 0.506649, 0.506536, 0.506410, 0.506281, 0.506156, 0.505924, 0.505541, 0.505047, 0.504219}};
  const double reomegaqnm20[8][107] = {{ 0.099636, 0.098809, 0.098823, 0.098961, 0.099181, 0.099322, 0.099091, 0.098913, 0.099007, 0.099206, 0.099084, 0.099201, 0.099305, 0.099370, 0.099413, 0.099541, 0.099710, 0.099906, 0.100109, 0.100292, 0.100464, 0.100668, 0.100843, 0.101320, 0.101793, 0.102742, 0.103705, 0.104671, 0.106617, 0.108576, 0.110540, 0.112492, 0.114417, 0.116296, 0.118106, 0.119822, 0.121422, 0.122876, 0.124150, 0.125214, 0.126860, 0.127085, 0.126123, 0.124264, 0.121765, 0.118824, 0.115576, 0.112102, 0.108440, 0.104598, 0.100602, 0.096644, 0.093455, 0.092822, 0.373762, 0.374032, 0.374485, 0.375124, 0.375955, 0.376985, 0.378223, 0.379682, 0.381374, 0.383318, 0.385536, 0.388054, 0.390905, 0.394129, 0.395535, 0.397012, 0.398565, 0.400198, 0.401917, 0.403727, 0.405634, 0.407645, 0.409766, 0.412004, 0.414368, 0.416862, 0.418159, 0.419491, 0.420856, 0.421551, 0.422254, 0.422538, 0.422823, 0.423109, 0.423396, 0.423685, 0.423975, 0.424266, 0.424412, 0.424558, 0.424701, 0.424768, 0.424827, 0.424866, 0.424870, 0.424871, 0.424870, 0.424866, 0.424858, 0.424833, 0.424799, 0.424786, 0.424772},
{ 0.099636, 0.098809, 0.098823, 0.098961, 0.099181, 0.099322, 0.099091, 0.098913, 0.099007, 0.099206, 0.099084, 0.099201, 0.099305, 0.099370, 0.099413, 0.099541, 0.099710, 0.099906, 0.100109, 0.100292, 0.100464, 0.100668, 0.100843, 0.101320, 0.101793, 0.102742, 0.103705, 0.104671, 0.106617, 0.108576, 0.110540, 0.112492, 0.114417, 0.116296, 0.118106, 0.119822, 0.121422, 0.122876, 0.124150, 0.125214, 0.126860, 0.127085, 0.126123, 0.124264, 0.121765, 0.118824, 0.115576, 0.112102, 0.108440, 0.104598, 0.100602, 0.096644, 0.093455, 0.092822, 0.346831, 0.347191, 0.347795, 0.348646, 0.349750, 0.351116, 0.352753, 0.354673, 0.356893, 0.359428, 0.362301, 0.365534, 0.369153, 0.373185, 0.374919, 0.376725, 0.378602, 0.380550, 0.382569, 0.384655, 0.386801, 0.388999, 0.391231, 0.393473, 0.395684, 0.397807, 0.398810, 0.399759, 0.400640, 0.401051, 0.401440, 0.401589, 0.401734, 0.401876, 0.402013, 0.402145, 0.402274, 0.402398, 0.402459, 0.402518, 0.402575, 0.402603, 0.402634, 0.402671, 0.402680, 0.402690, 0.402699, 0.402708, 0.402716, 0.402715, 0.402639, 0.402518, 0.402260},
{ 0.099636, 0.098809, 0.098823, 0.098961, 0.099181, 0.099322, 0.099091, 0.098913, 0.099007, 0.099206, 0.099084, 0.099201, 0.099305, 0.099370, 0.099413, 0.099541, 0.099710, 0.099906, 0.100109, 0.100292, 0.100464, 0.100668, 0.100843, 0.101320, 0.101793, 0.102742, 0.103705, 0.104671, 0.106617, 0.108576, 0.110540, 0.112492, 0.114417, 0.116296, 0.118106, 0.119822, 0.121422, 0.122876, 0.124150, 0.125214, 0.126860, 0.127085, 0.126123, 0.124264, 0.121765, 0.118824, 0.115576, 0.112102, 0.108440, 0.104598, 0.100602, 0.096644, 0.093455, 0.092822, 0.301226, 0.301745, 0.302612, 0.303832, 0.305410, 0.307356, 0.309677, 0.312384, 0.315491, 0.319010, 0.322953, 0.327329, 0.332140, 0.337367, 0.339564, 0.341813, 0.344104, 0.346423, 0.348751, 0.351062, 0.353316, 0.355458, 0.357410, 0.359064, 0.360277, 0.360891, 0.360932, 0.360787, 0.360457, 0.360224, 0.359947, 0.359824, 0.359695, 0.359558, 0.359416, 0.359266, 0.359111, 0.358949, 0.358866, 0.358781, 0.358695, 0.358653, 0.358612, 0.358564, 0.358551, 0.358536, 0.358520, 0.358503, 0.358487, 0.358477, 0.358557, 0.358656, 0.358763},
{ 0.099636, 0.098809, 0.098823, 0.098961, 0.099181, 0.099322, 0.099091, 0.098913, 0.099007, 0.099206, 0.099084, 0.099201, 0.099305, 0.099370, 0.099413, 0.099541, 0.099710, 0.099906, 0.100109, 0.100292, 0.100464, 0.100668, 0.100843, 0.101320, 0.101793, 0.102742, 0.103705, 0.104671, 0.106617, 0.108576, 0.110540, 0.112492, 0.114417, 0.116296, 0.118106, 0.119822, 0.121422, 0.122876, 0.124150, 0.125214, 0.126860, 0.127085, 0.126123, 0.124264, 0.121765, 0.118824, 0.115576, 0.112102, 0.108440, 0.104598, 0.100602, 0.096644, 0.093455, 0.092822, 0.251735, 0.252425, 0.253576, 0.255192, 0.257276, 0.259831, 0.262861, 0.266370, 0.270359, 0.274824, 0.279754, 0.285121, 0.290866, 0.296871, 0.299299, 0.301709, 0.304069, 0.306337, 0.308452, 0.310333, 0.311866, 0.312892, 0.313197, 0.312525, 0.310673, 0.307762, 0.306042, 0.304179, 0.302199, 0.301174, 0.300132, 0.299710, 0.299286, 0.298859, 0.298431, 0.298001, 0.297569, 0.297135, 0.296918, 0.296700, 0.296479, 0.296377, 0.296272, 0.296133, 0.296106, 0.296084, 0.296072, 0.296071, 0.296081, 0.296090, 0.295901, 0.295691, 0.295593},
{ 0.099636, 0.098809, 0.098823, 0.098961, 0.099181, 0.099322, 0.099091, 0.098913, 0.099007, 0.099206, 0.099084, 0.099201, 0.099305, 0.099370, 0.099413, 0.099541, 0.099710, 0.099906, 0.100109, 0.100292, 0.100464, 0.100668, 0.100843, 0.101320, 0.101793, 0.102742, 0.103705, 0.104671, 0.106617, 0.108576, 0.110540, 0.112492, 0.114417, 0.116296, 0.118106, 0.119822, 0.121422, 0.122876, 0.124150, 0.125214, 0.126860, 0.127085, 0.126123, 0.124264, 0.121765, 0.118824, 0.115576, 0.112102, 0.108440, 0.104598, 0.100602, 0.096644, 0.093455, 0.092822, 0.207801, 0.208660, 0.210090, 0.212089, 0.214652, 0.217773, 0.221444, 0.225649, 0.230365, 0.235556, 0.241164, 0.247088, 0.253161, 0.259075, 0.261282, 0.263320, 0.265113, 0.266556, 0.267505, 0.267758, 0.267026, 0.264918, 0.260982, 0.255164, 0.249003, 0.243206, 0.240152, 0.237243, 0.234329, 0.232892, 0.231471, 0.230906, 0.230343, 0.229782, 0.229223, 0.228668, 0.228112, 0.227563, 0.227282, 0.227020, 0.226723, 0.226605, 0.226491, 0.226268, 0.226253, 0.226265, 0.226297, 0.226324, 0.226309, 0.226033, 0.225920, 0.226313, 0.226576},
{ 0.099636, 0.098809, 0.098823, 0.098961, 0.099181, 0.099322, 0.099091, 0.098913, 0.099007, 0.099206, 0.099084, 0.099201, 0.099305, 0.099370, 0.099413, 0.099541, 0.099710, 0.099906, 0.100109, 0.100292, 0.100464, 0.100668, 0.100843, 0.101320, 0.101793, 0.102742, 0.103705, 0.104671, 0.106617, 0.108576, 0.110540, 0.112492, 0.114417, 0.116296, 0.118106, 0.119822, 0.121422, 0.122876, 0.124150, 0.125214, 0.126860, 0.127085, 0.126123, 0.124264, 0.121765, 0.118824, 0.115576, 0.112102, 0.108440, 0.104598, 0.100602, 0.096644, 0.093455, 0.092822, 0.169657, 0.170725, 0.172497, 0.174959, 0.178090, 0.181864, 0.186246, 0.191189, 0.196629, 0.202475, 0.208593, 0.214773, 0.220670, 0.225672, 0.227190, 0.228272, 0.228756, 0.228412, 0.226904, 0.223732, 0.218155, 0.209323, 0.198745, 0.193577, 0.186305, 0.180463, 0.176827, 0.174111, 0.171249, 0.169885, 0.168502, 0.167948, 0.167413, 0.166876, 0.166340, 0.165810, 0.165288, 0.164746, 0.164515, 0.164209, 0.163984, 0.163792, 0.163805, 0.163455, 0.163488, 0.163585, 0.163680, 0.163676, 0.163501, 0.163097, 0.163828, 0.163493, 0.162468},
{ 0.099636, 0.098809, 0.098823, 0.098961, 0.099181, 0.099322, 0.099091, 0.098913, 0.099007, 0.099206, 0.099084, 0.099201, 0.099305, 0.099370, 0.099413, 0.099541, 0.099710, 0.099906, 0.100109, 0.100292, 0.100464, 0.100668, 0.100843, 0.101320, 0.101793, 0.102742, 0.103705, 0.104671, 0.106617, 0.108576, 0.110540, 0.112492, 0.114417, 0.116296, 0.118106, 0.119822, 0.121422, 0.122876, 0.124150, 0.125214, 0.126860, 0.127085, 0.126123, 0.124264, 0.121765, 0.118824, 0.115576, 0.112102, 0.108440, 0.104598, 0.100602, 0.096644, 0.093455, 0.092822, 0.133728, 0.135148, 0.137483, 0.140692, 0.144717, 0.149486, 0.154913, 0.160894, 0.167302, 0.173965, 0.180643, 0.186972, 0.192350, 0.195693, 0.196032, 0.195488, 0.193718, 0.190195, 0.184057, 0.173800, 0.157606, 0.149904, 0.150294, 0.136574, 0.136371, 0.130053, 0.125270, 0.122908, 0.121266, 0.119961, 0.118878, 0.118309, 0.117833, 0.117412, 0.116952, 0.116509, 0.116094, 0.115667, 0.115352, 0.115216, 0.115053, 0.114762, 0.114876, 0.114415, 0.114510, 0.114721, 0.114853, 0.114708, 0.114322, 0.114511, 0.114432, 0.113700, 0.115095},
{ 0.099636, 0.098809, 0.098823, 0.098961, 0.099181, 0.099322, 0.099091, 0.098913, 0.099007, 0.099206, 0.099084, 0.099201, 0.099305, 0.099370, 0.099413, 0.099541, 0.099710, 0.099906, 0.100109, 0.100292, 0.100464, 0.100668, 0.100843, 0.101320, 0.101793, 0.102742, 0.103705, 0.104671, 0.106617, 0.108576, 0.110540, 0.112492, 0.114417, 0.116296, 0.118106, 0.119822, 0.121422, 0.122876, 0.124150, 0.125214, 0.126860, 0.127085, 0.126123, 0.124264, 0.121765, 0.118824, 0.115576, 0.112102, 0.108440, 0.104598, 0.100602, 0.096644, 0.093455, 0.092822, 0.093595, 0.095873, 0.099540, 0.104431, 0.110354, 0.117108, 0.124493, 0.132307, 0.140327, 0.148283, 0.155807, 0.162340, 0.166934, 0.167780, 0.166293, 0.163133, 0.157503, 0.147921, 0.131060, 0.107176, 0.119461, 0.116330, 0.094108, 0.104492, 0.097083, 0.093018, 0.087420, 0.086369, 0.083105, 0.081981, 0.082460, 0.081711, 0.081131, 0.080986, 0.080498, 0.080153, 0.079710, 0.079360, 0.079312, 0.079042, 0.078766, 0.078878, 0.078724, 0.078322, 0.078468, 0.078779, 0.078873, 0.078526, 0.078115, 0.078949, 0.077817, 0.078978, 0.078246}};
  const double reomegaqnm33[8][107] = {{ 0.445768, 0.452799, 0.456948, 0.460943, 0.462462, 0.462842, 0.463095, 0.463269, 0.463394, 0.463488, 0.463746, 0.463886, 0.463989, 0.464144, 0.464267, 0.464374, 0.464572, 0.464763, 0.464952, 0.465140, 0.465329, 0.465518, 0.465707, 0.466182, 0.466657, 0.467612, 0.468573, 0.469540, 0.471491, 0.473465, 0.475464, 0.477487, 0.479535, 0.481609, 0.483709, 0.485837, 0.487991, 0.490174, 0.492386, 0.494627, 0.500363, 0.506300, 0.512449, 0.518826, 0.525445, 0.532323, 0.539479, 0.546934, 0.554710, 0.562834, 0.571335, 0.580244, 0.589600, 0.599443, 0.609823, 0.620796, 0.632425, 0.644787, 0.657972, 0.672086, 0.687260, 0.703650, 0.721455, 0.740921, 0.762369, 0.786223, 0.813057, 0.843687, 0.857254, 0.871717, 0.887201, 0.903860, 0.921885, 0.941521, 0.963088, 0.987016, 1.013906, 1.044637, 1.080583, 1.124097, 1.149983, 1.179862, 1.215469, 1.236367, 1.260229, 1.270858, 1.282268, 1.294616, 1.308117, 1.323083, 1.339991, 1.359648, 1.370944, 1.383632, 1.398286, 1.406660, 1.416026, 1.426791, 1.429170, 1.431642, 1.434220, 1.436917, 1.439748, 1.445906, 1.452953, 1.456948, 1.461390},
{ 0.428357, 0.432238, 0.434351, 0.436362, 0.437184, 0.437404, 0.437552, 0.437654, 0.437725, 0.437773, 0.437868, 0.437898, 0.437929, 0.438015, 0.438115, 0.438217, 0.438421, 0.438623, 0.438826, 0.439030, 0.439233, 0.439437, 0.439641, 0.440152, 0.440665, 0.441694, 0.442729, 0.443771, 0.445872, 0.447999, 0.450150, 0.452328, 0.454532, 0.456764, 0.459023, 0.461311, 0.463627, 0.465974, 0.468350, 0.470758, 0.476918, 0.483289, 0.489884, 0.496717, 0.503805, 0.511164, 0.518814, 0.526776, 0.535074, 0.543734, 0.552785, 0.562261, 0.572200, 0.582644, 0.593642, 0.605250, 0.617535, 0.630573, 0.644453, 0.659285, 0.675198, 0.692352, 0.710943, 0.731221, 0.753507, 0.778225, 0.805952, 0.837504, 0.851449, 0.866295, 0.882167, 0.899220, 0.917645, 0.937687, 0.959667, 0.984015, 1.011333, 1.042500, 1.078890, 1.122855, 1.148968, 1.179073, 1.214906, 1.235913, 1.259884, 1.270555, 1.282006, 1.294395, 1.307936, 1.322940, 1.339885, 1.359577, 1.370888, 1.383591, 1.398259, 1.406639, 1.416011, 1.426781, 1.429161, 1.431634, 1.434213, 1.436910, 1.439742, 1.445902, 1.452950, 1.456946, 1.461388},
{ 0.390162, 0.390933, 0.390855, 0.390130, 0.389593, 0.389452, 0.389377, 0.389349, 0.389352, 0.389374, 0.389533, 0.389627, 0.389680, 0.389780, 0.389896, 0.390013, 0.390243, 0.390473, 0.390704, 0.390935, 0.391167, 0.391398, 0.391630, 0.392211, 0.392793, 0.393963, 0.395139, 0.396321, 0.398706, 0.401118, 0.403557, 0.406025, 0.408522, 0.411048, 0.413604, 0.416191, 0.418810, 0.421460, 0.424144, 0.426861, 0.433807, 0.440980, 0.448397, 0.456071, 0.464019, 0.472260, 0.480815, 0.489704, 0.498954, 0.508590, 0.518645, 0.529153, 0.540151, 0.551685, 0.563804, 0.576567, 0.590041, 0.604302, 0.619445, 0.635578, 0.652833, 0.671371, 0.691391, 0.713145, 0.736956, 0.763249, 0.792606, 0.825846, 0.840483, 0.856032, 0.872619, 0.890397, 0.909560, 0.930355, 0.953102, 0.978234, 1.006355, 1.038346, 1.075582, 1.120416, 1.146970, 1.177518, 1.213791, 1.235015, 1.259198, 1.269953, 1.281487, 1.293956, 1.307576, 1.322655, 1.339673, 1.359433, 1.370776, 1.383509, 1.398204, 1.406597, 1.415981, 1.426761, 1.429142, 1.431618, 1.434198, 1.436897, 1.439731, 1.445894, 1.452945, 1.456942, 1.461385},
{ 0.332321, 0.329612, 0.329447, 0.330998, 0.331533, 0.331485, 0.331367, 0.331244, 0.331151, 0.331101, 0.331226, 0.331354, 0.331387, 0.331504, 0.331643, 0.331766, 0.332024, 0.332280, 0.332537, 0.332795, 0.333053, 0.333311, 0.333569, 0.334216, 0.334865, 0.336168, 0.337478, 0.338796, 0.341454, 0.344143, 0.346863, 0.349614, 0.352398, 0.355215, 0.358066, 0.360951, 0.363872, 0.366829, 0.369823, 0.372855, 0.380605, 0.388612, 0.396890, 0.405455, 0.414327, 0.423525, 0.433070, 0.442986, 0.453300, 0.464039, 0.475237, 0.486928, 0.499155, 0.511962, 0.525402, 0.539534, 0.554428, 0.570163, 0.586836, 0.604556, 0.623460, 0.643711, 0.665511, 0.689115, 0.714850, 0.743145, 0.774586, 0.809998, 0.825530, 0.841991, 0.859506, 0.878231, 0.898362, 0.920146, 0.943908, 0.970086, 0.999286, 1.032398, 1.070804, 1.116860, 1.144045, 1.175233, 1.212147, 1.233689, 1.258185, 1.269061, 1.280716, 1.293303, 1.307039, 1.322230, 1.339355, 1.359216, 1.370608, 1.383385, 1.398122, 1.406534, 1.415935, 1.426730, 1.429115, 1.431593, 1.434176, 1.436878, 1.439714, 1.445882, 1.452937, 1.456936, 1.461381},
{ 0.279748, 0.281425, 0.280003, 0.278496, 0.279479, 0.279701, 0.279679, 0.279534, 0.279380, 0.279284, 0.279511, 0.279556, 0.279569, 0.279745, 0.279852, 0.279994, 0.280246, 0.280507, 0.280766, 0.281026, 0.281285, 0.281546, 0.281806, 0.282459, 0.283115, 0.284431, 0.285757, 0.287091, 0.289787, 0.292519, 0.295288, 0.298095, 0.300940, 0.303825, 0.306751, 0.309717, 0.312725, 0.315776, 0.318871, 0.322011, 0.330062, 0.338415, 0.347087, 0.356096, 0.365463, 0.375210, 0.385359, 0.395936, 0.406968, 0.418488, 0.430528, 0.443126, 0.456326, 0.470174, 0.484725, 0.500042, 0.516195, 0.533269, 0.551359, 0.570583, 0.591077, 0.613010, 0.636588, 0.662069, 0.689786, 0.720173, 0.753822, 0.791564, 0.808064, 0.825515, 0.844044, 0.863808, 0.885005, 0.907885, 0.932779, 0.960131, 0.990559, 1.024965, 1.064751, 1.112293, 1.140263, 1.172260, 1.210000, 1.231954, 1.256856, 1.267892, 1.279704, 1.292446, 1.306332, 1.321670, 1.338934, 1.358928, 1.370382, 1.383219, 1.398011, 1.406448, 1.415873, 1.426690, 1.429078, 1.431560, 1.434147, 1.436852, 1.439692, 1.445866, 1.452926, 1.456927, 1.461375},
{ 0.239992, 0.240995, 0.242946, 0.241560, 0.241854, 0.242213, 0.242269, 0.242117, 0.241939, 0.241855, 0.242174, 0.242080, 0.242226, 0.242298, 0.242438, 0.242546, 0.242783, 0.243024, 0.243261, 0.243498, 0.243736, 0.243974, 0.244213, 0.244811, 0.245412, 0.246621, 0.247840, 0.249069, 0.251558, 0.254089, 0.256662, 0.259279, 0.261940, 0.264647, 0.267400, 0.270200, 0.273049, 0.275948, 0.278897, 0.281899, 0.289636, 0.297725, 0.306186, 0.315041, 0.324312, 0.334027, 0.344210, 0.354891, 0.366101, 0.377873, 0.390245, 0.403257, 0.416954, 0.431386, 0.446612, 0.462696, 0.479713, 0.497750, 0.516906, 0.537303, 0.559082, 0.582416, 0.607517, 0.634649, 0.664150, 0.696462, 0.732182, 0.772150, 0.789586, 0.807999, 0.827516, 0.848298, 0.870543, 0.894505, 0.920521, 0.949042, 0.980704, 1.016433, 1.057669, 1.106837, 1.135700, 1.168643, 1.207370, 1.229826, 1.255224, 1.266456, 1.278461, 1.291392, 1.305463, 1.320979, 1.338415, 1.358571, 1.370101, 1.383011, 1.397871, 1.406340, 1.415794, 1.426639, 1.429032, 1.431519, 1.434110, 1.436820, 1.439664, 1.445846, 1.452913, 1.456917, 1.461368},
{ 0.218840, 0.216298, 0.216633, 0.217244, 0.216888, 0.217233, 0.217329, 0.217189, 0.217034, 0.217006, 0.217217, 0.217255, 0.217293, 0.217414, 0.217505, 0.217599, 0.217807, 0.218016, 0.218223, 0.218427, 0.218635, 0.218841, 0.219050, 0.219570, 0.220094, 0.221148, 0.222212, 0.223288, 0.225470, 0.227696, 0.229966, 0.232282, 0.234646, 0.237057, 0.239517, 0.242028, 0.244591, 0.247207, 0.249877, 0.252604, 0.259674, 0.267128, 0.274991, 0.283291, 0.292057, 0.301319, 0.311110, 0.321465, 0.332419, 0.344012, 0.356285, 0.369284, 0.383057, 0.397660, 0.413152, 0.429604, 0.447094, 0.465712, 0.485563, 0.506772, 0.529486, 0.553883, 0.580183, 0.608655, 0.639647, 0.673609, 0.711146, 0.753105, 0.771386, 0.790674, 0.811095, 0.832808, 0.856013, 0.880965, 0.908002, 0.937583, 0.970359, 1.007288, 1.049872, 1.100643, 1.130440, 1.164419, 1.204272, 1.227311, 1.253296, 1.264760, 1.276993, 1.290148, 1.304437, 1.320164, 1.337802, 1.358147, 1.369768, 1.382763, 1.397703, 1.406210, 1.415699, 1.426576, 1.428976, 1.431468, 1.434066, 1.436781, 1.439630, 1.445822, 1.452897, 1.456905, 1.461349},
{ 0.199814, 0.201353, 0.200132, 0.200956, 0.200467, 0.200722, 0.200829, 0.200716, 0.200604, 0.200626, 0.200705, 0.200826, 0.200822, 0.200914, 0.201004, 0.201103, 0.201273, 0.201448, 0.201625, 0.201800, 0.201975, 0.202153, 0.202329, 0.202773, 0.203220, 0.204121, 0.205032, 0.205953, 0.207826, 0.209742, 0.211702, 0.213706, 0.215757, 0.217854, 0.220001, 0.222198, 0.224447, 0.226749, 0.229105, 0.231518, 0.237809, 0.244492, 0.251597, 0.259160, 0.267216, 0.275804, 0.284965, 0.294742, 0.305178, 0.316322, 0.328221, 0.340928, 0.354499, 0.368992, 0.384475, 0.401019, 0.418708, 0.437635, 0.457910, 0.479661, 0.503040, 0.528232, 0.555461, 0.585008, 0.617226, 0.652577, 0.691676, 0.735379, 0.754411, 0.774477, 0.795706, 0.818253, 0.842317, 0.868149, 0.896082, 0.926573, 0.960272, 0.998162, 1.041815, 1.093935, 1.124603, 1.159629, 1.200706, 1.224407, 1.251066, 1.262799, 1.275299, 1.288715, 1.303258, 1.319229, 1.337099, 1.357662, 1.369384, 1.382477, 1.397508, 1.406057, 1.415588, 1.426502, 1.428909, 1.431409, 1.434013, 1.436734, 1.439590, 1.445793, 1.452879, 1.456891, 1.461338}};
  const double reomegaqnm32[8][107] = {{ 0.506437, 0.510521, 0.512354, 0.513460, 0.513606, 0.513606, 0.513597, 0.513588, 0.513582, 0.513579, 0.513605, 0.513652, 0.513698, 0.513775, 0.513839, 0.513896, 0.514004, 0.514110, 0.514215, 0.514321, 0.514427, 0.514533, 0.514639, 0.514905, 0.515173, 0.515712, 0.516256, 0.516805, 0.517918, 0.519052, 0.520208, 0.521384, 0.522583, 0.523803, 0.525046, 0.526312, 0.527601, 0.528913, 0.530250, 0.531610, 0.535122, 0.538796, 0.542640, 0.546663, 0.550875, 0.555285, 0.559905, 0.564749, 0.569831, 0.575167, 0.580774, 0.586673, 0.592888, 0.599443, 0.606369, 0.613700, 0.621475, 0.629738, 0.638545, 0.647958, 0.658052, 0.668919, 0.680668, 0.693440, 0.707409, 0.722802, 0.739919, 0.759175, 0.767600, 0.776509, 0.785959, 0.796021, 0.806783, 0.818351, 0.830862, 0.844496, 0.859491, 0.876183, 0.895066, 0.916929, 0.929375, 0.943179, 0.958779, 0.967466, 0.976924, 0.980968, 0.985186, 0.989601, 0.994241, 0.999140, 1.004343, 1.009907, 1.012845, 1.015898, 1.019070, 1.020697, 1.022344, 1.023996, 1.024325, 1.024652, 1.024978, 1.025301, 1.025621, 1.026251, 1.026862, 1.027158, 1.027448},
{ 0.488275, 0.490233, 0.491246, 0.492172, 0.492528, 0.492618, 0.492677, 0.492716, 0.492741, 0.492758, 0.492791, 0.492808, 0.492829, 0.492880, 0.492934, 0.492989, 0.493099, 0.493208, 0.493318, 0.493428, 0.493538, 0.493648, 0.493758, 0.494035, 0.494313, 0.494872, 0.495436, 0.496005, 0.497158, 0.498330, 0.499524, 0.500739, 0.501976, 0.503236, 0.504519, 0.505826, 0.507158, 0.508514, 0.509897, 0.511306, 0.514946, 0.518763, 0.522766, 0.526965, 0.531371, 0.535995, 0.540849, 0.545948, 0.551306, 0.556940, 0.562868, 0.569112, 0.575695, 0.582644, 0.589989, 0.597765, 0.606012, 0.614777, 0.624114, 0.634088, 0.644775, 0.656268, 0.668679, 0.682148, 0.696852, 0.713017, 0.730945, 0.751047, 0.759820, 0.769080, 0.778884, 0.789302, 0.800419, 0.812337, 0.825190, 0.839148, 0.854439, 0.871376, 0.890418, 0.912277, 0.924613, 0.938179, 0.953324, 0.961646, 0.970585, 0.974358, 0.978256, 0.982286, 0.986453, 0.990754, 0.995172, 0.999636, 1.001825, 1.003902, 1.005709, 1.006402, 1.006823, 1.006752, 1.006644, 1.006493, 1.006292, 1.006037, 1.005719, 1.004884, 1.003812, 1.003222, 1.002616},
{ 0.453935, 0.454400, 0.454380, 0.454081, 0.453894, 0.453854, 0.453838, 0.453837, 0.453845, 0.453857, 0.453919, 0.453957, 0.453984, 0.454042, 0.454104, 0.454165, 0.454286, 0.454408, 0.454530, 0.454652, 0.454775, 0.454897, 0.455019, 0.455326, 0.455633, 0.456250, 0.456870, 0.457494, 0.458752, 0.460026, 0.461318, 0.462628, 0.463959, 0.465311, 0.466686, 0.468084, 0.469507, 0.470957, 0.472434, 0.473939, 0.477830, 0.481919, 0.486219, 0.490742, 0.495504, 0.500517, 0.505796, 0.511357, 0.517216, 0.523391, 0.529901, 0.536770, 0.544022, 0.551685, 0.559790, 0.568375, 0.577480, 0.587154, 0.597454, 0.608445, 0.620207, 0.632835, 0.646443, 0.661174, 0.677206, 0.694768, 0.714161, 0.735793, 0.745193, 0.755089, 0.765534, 0.776597, 0.788357, 0.800914, 0.814392, 0.828949, 0.844795, 0.862215, 0.881614, 0.903610, 0.915876, 0.929229, 0.943952, 0.951955, 0.960488, 0.964074, 0.967772, 0.971596, 0.975560, 0.979688, 0.984015, 0.988606, 0.991038, 0.993603, 0.996365, 0.997858, 0.999462, 1.001225, 1.001601, 1.001985, 1.002374, 1.002765, 1.003150, 1.003835, 1.004148, 1.003996, 1.003539},
{ 0.404724, 0.403689, 0.403600, 0.404076, 0.404258, 0.404251, 0.404224, 0.404195, 0.404175, 0.404166, 0.404215, 0.404267, 0.404298, 0.404370, 0.404445, 0.404519, 0.404667, 0.404815, 0.404963, 0.405111, 0.405258, 0.405406, 0.405554, 0.405923, 0.406292, 0.407028, 0.407765, 0.408500, 0.409972, 0.411447, 0.412927, 0.414414, 0.415912, 0.417423, 0.418949, 0.420493, 0.422056, 0.423641, 0.425251, 0.426886, 0.431100, 0.435515, 0.440156, 0.445044, 0.450200, 0.455643, 0.461393, 0.467468, 0.473888, 0.480675, 0.487850, 0.495438, 0.503465, 0.511962, 0.520962, 0.530505, 0.540632, 0.551396, 0.562855, 0.575079, 0.588148, 0.602160, 0.617234, 0.633516, 0.651186, 0.670476, 0.691688, 0.715225, 0.725411, 0.736102, 0.747353, 0.759229, 0.771806, 0.785179, 0.799465, 0.814813, 0.831421, 0.849554, 0.869595, 0.892144, 0.904659, 0.918275, 0.933356, 0.941641, 0.950603, 0.954427, 0.958419, 0.962607, 0.967027, 0.971729, 0.976781, 0.982283, 0.985249, 0.988396, 0.991774, 0.993577, 0.995484, 0.997537, 0.997971, 0.998417, 0.998874, 0.999344, 0.999831, 1.000861, 1.001979, 1.002546, 1.003020},
{ 0.352819, 0.353913, 0.353592, 0.352947, 0.353195, 0.353300, 0.353333, 0.353317, 0.353284, 0.353256, 0.353311, 0.353371, 0.353402, 0.353505, 0.353594, 0.353689, 0.353876, 0.354063, 0.354249, 0.354435, 0.354621, 0.354807, 0.354992, 0.355454, 0.355914, 0.356831, 0.357741, 0.358645, 0.360438, 0.362212, 0.363970, 0.365716, 0.367451, 0.369182, 0.370910, 0.372639, 0.374373, 0.376117, 0.377872, 0.379642, 0.384158, 0.388840, 0.393728, 0.398863, 0.404276, 0.409999, 0.416059, 0.422484, 0.429298, 0.436529, 0.444202, 0.452347, 0.460993, 0.470174, 0.479926, 0.490291, 0.501316, 0.513053, 0.525565, 0.538924, 0.553214, 0.568538, 0.585017, 0.602800, 0.622072, 0.643068, 0.666091, 0.691546, 0.702528, 0.714032, 0.726111, 0.738830, 0.752263, 0.766506, 0.781674, 0.797919, 0.815443, 0.834526, 0.855593, 0.879346, 0.892606, 0.907139, 0.923415, 0.932448, 0.942290, 0.946510, 0.950927, 0.955571, 0.960487, 0.965731, 0.971391, 0.977596, 0.980967, 0.984569, 0.988462, 0.990546, 0.992747, 0.995099, 0.995592, 0.996096, 0.996609, 0.997135, 0.997675, 0.998807, 1.000038, 1.000709, 1.001429},
{ 0.307722, 0.307242, 0.308236, 0.308063, 0.307908, 0.308068, 0.308173, 0.308184, 0.308142, 0.308095, 0.308192, 0.308221, 0.308283, 0.308395, 0.308516, 0.308625, 0.308854, 0.309081, 0.309306, 0.309533, 0.309758, 0.309983, 0.310208, 0.310768, 0.311325, 0.312433, 0.313532, 0.314621, 0.316770, 0.318882, 0.320958, 0.323001, 0.325012, 0.326996, 0.328955, 0.330893, 0.332815, 0.334725, 0.336628, 0.338527, 0.343295, 0.348145, 0.353144, 0.358351, 0.363819, 0.369594, 0.375719, 0.382231, 0.389166, 0.396559, 0.404442, 0.412850, 0.421819, 0.431386, 0.441592, 0.452482, 0.464106, 0.476520, 0.489790, 0.503991, 0.519211, 0.535555, 0.553148, 0.572143, 0.592729, 0.615145, 0.639698, 0.666799, 0.678474, 0.690693, 0.703509, 0.716990, 0.731215, 0.746285, 0.762327, 0.779508, 0.798061, 0.818318, 0.840790, 0.866326, 0.880686, 0.896506, 0.914300, 0.924200, 0.935002, 0.939638, 0.944491, 0.949596, 0.955000, 0.960764, 0.966981, 0.973799, 0.977507, 0.981479, 0.985792, 0.988112, 0.990571, 0.993204, 0.993756, 0.994318, 0.994892, 0.995477, 0.996076, 0.997322, 0.998658, 0.999375, 1.000143},
{ 0.271458, 0.270652, 0.270094, 0.270893, 0.270470, 0.270595, 0.270749, 0.270789, 0.270736, 0.270675, 0.270818, 0.270821, 0.270923, 0.271042, 0.271163, 0.271299, 0.271556, 0.271813, 0.272070, 0.272328, 0.272584, 0.272841, 0.273097, 0.273736, 0.274373, 0.275640, 0.276897, 0.278144, 0.280607, 0.283028, 0.285404, 0.287735, 0.290021, 0.292262, 0.294461, 0.296619, 0.298740, 0.300827, 0.302885, 0.304919, 0.309930, 0.314908, 0.319939, 0.325108, 0.330488, 0.336148, 0.342147, 0.348538, 0.355370, 0.362688, 0.370534, 0.378950, 0.387977, 0.397660, 0.408042, 0.419173, 0.431107, 0.443903, 0.457628, 0.472361, 0.488191, 0.505226, 0.523593, 0.543446, 0.564978, 0.588431, 0.614121, 0.642471, 0.654683, 0.667464, 0.680874, 0.694985, 0.709889, 0.725698, 0.742564, 0.760684, 0.780336, 0.801915, 0.826017, 0.853601, 0.869186, 0.886397, 0.905791, 0.916587, 0.928363, 0.933413, 0.938698, 0.944254, 0.950130, 0.956394, 0.963145, 0.970538, 0.974556, 0.978858, 0.983534, 0.986056, 0.988736, 0.991618, 0.992224, 0.992841, 0.993470, 0.994112, 0.994769, 0.996134, 0.997587, 0.998359, 0.999176},
{ 0.239041, 0.240558, 0.239971, 0.240247, 0.239966, 0.239996, 0.240167, 0.240230, 0.240169, 0.240104, 0.240251, 0.240310, 0.240357, 0.240506, 0.240654, 0.240790, 0.241071, 0.241349, 0.241627, 0.241907, 0.242186, 0.242464, 0.242743, 0.243437, 0.244130, 0.245511, 0.246885, 0.248252, 0.250959, 0.253628, 0.256256, 0.258838, 0.261372, 0.263856, 0.266288, 0.268666, 0.270993, 0.273269, 0.275497, 0.277680, 0.282971, 0.288097, 0.293157, 0.298256, 0.303491, 0.308954, 0.314724, 0.320873, 0.327466, 0.334560, 0.342210, 0.350466, 0.359377, 0.368992, 0.379362, 0.390540, 0.402581, 0.415547, 0.429506, 0.444538, 0.460733, 0.478196, 0.497054, 0.517462, 0.539613, 0.563751, 0.590201, 0.619411, 0.632006, 0.645199, 0.659060, 0.673672, 0.689141, 0.705603, 0.723235, 0.742271, 0.763031, 0.785965, 0.811731, 0.841375, 0.858181, 0.876773, 0.897746, 0.909424, 0.922158, 0.927616, 0.933325, 0.939323, 0.945661, 0.952411, 0.959676, 0.967623, 0.971935, 0.976549, 0.981559, 0.984263, 0.987140, 0.990241, 0.990894, 0.991560, 0.992240, 0.992935, 0.993647, 0.995125, 0.996696, 0.997527, 0.998401}};
  const double reomegaqnm31[8][107] = {{ 0.578812, 0.579132, 0.578821, 0.578212, 0.577910, 0.577830, 0.577777, 0.577742, 0.577719, 0.577704, 0.577678, 0.577674, 0.577670, 0.577653, 0.577632, 0.577609, 0.577560, 0.577512, 0.577464, 0.577417, 0.577370, 0.577324, 0.577278, 0.577165, 0.577054, 0.576842, 0.576641, 0.576450, 0.576102, 0.575796, 0.575532, 0.575308, 0.575125, 0.574981, 0.574876, 0.574809, 0.574780, 0.574788, 0.574832, 0.574913, 0.575268, 0.575841, 0.576625, 0.577620, 0.578823, 0.580234, 0.581854, 0.583686, 0.585734, 0.588003, 0.590499, 0.593231, 0.596209, 0.599443, 0.602949, 0.606742, 0.610842, 0.615270, 0.620053, 0.625222, 0.630813, 0.636869, 0.643444, 0.650601, 0.658418, 0.666994, 0.676453, 0.686960, 0.691503, 0.696265, 0.701265, 0.706528, 0.712080, 0.717952, 0.724182, 0.730815, 0.737907, 0.745525, 0.753756, 0.762706, 0.767493, 0.772513, 0.777787, 0.780525, 0.783334, 0.784477, 0.785632, 0.786799, 0.787978, 0.789167, 0.790369, 0.791581, 0.792191, 0.792805, 0.793420, 0.793729, 0.794038, 0.794347, 0.794409, 0.794470, 0.794532, 0.794593, 0.794655, 0.794780, 0.794921, 0.795016, 0.795171},
{ 0.559537, 0.560143, 0.560464, 0.560752, 0.560852, 0.560873, 0.560884, 0.560889, 0.560891, 0.560891, 0.560879, 0.560867, 0.560857, 0.560839, 0.560821, 0.560804, 0.560768, 0.560733, 0.560698, 0.560663, 0.560628, 0.560593, 0.560558, 0.560470, 0.560384, 0.560213, 0.560045, 0.559881, 0.559567, 0.559274, 0.559005, 0.558763, 0.558551, 0.558369, 0.558220, 0.558104, 0.558022, 0.557974, 0.557962, 0.557986, 0.558204, 0.558650, 0.559326, 0.560233, 0.561372, 0.562744, 0.564352, 0.566199, 0.568289, 0.570629, 0.573225, 0.576086, 0.579221, 0.582644, 0.586368, 0.590410, 0.594790, 0.599531, 0.604659, 0.610208, 0.616212, 0.622718, 0.629779, 0.637457, 0.645830, 0.654996, 0.665072, 0.676210, 0.681005, 0.686015, 0.691257, 0.696749, 0.702510, 0.708564, 0.714933, 0.721641, 0.728711, 0.736157, 0.743970, 0.752084, 0.756196, 0.760280, 0.764241, 0.766132, 0.767934, 0.768623, 0.769291, 0.769936, 0.770558, 0.771152, 0.771719, 0.772256, 0.772513, 0.772762, 0.773003, 0.773120, 0.773235, 0.773348, 0.773371, 0.773393, 0.773415, 0.773437, 0.773459, 0.773503, 0.773544, 0.773561, 0.773573},
{ 0.528589, 0.528767, 0.528753, 0.528654, 0.528604, 0.528596, 0.528593, 0.528593, 0.528595, 0.528597, 0.528605, 0.528607, 0.528608, 0.528609, 0.528611, 0.528613, 0.528616, 0.528619, 0.528620, 0.528621, 0.528621, 0.528621, 0.528620, 0.528614, 0.528604, 0.528575, 0.528532, 0.528478, 0.528341, 0.528176, 0.527993, 0.527800, 0.527606, 0.527418, 0.527242, 0.527083, 0.526946, 0.526833, 0.526749, 0.526695, 0.526710, 0.526957, 0.527454, 0.528211, 0.529239, 0.530542, 0.532127, 0.533999, 0.536165, 0.538632, 0.541407, 0.544500, 0.547921, 0.551685, 0.555805, 0.560300, 0.565191, 0.570500, 0.576257, 0.582494, 0.589249, 0.596568, 0.604504, 0.613119, 0.622488, 0.632702, 0.643865, 0.656103, 0.661330, 0.666761, 0.672405, 0.678269, 0.684360, 0.690678, 0.697216, 0.703951, 0.710831, 0.717744, 0.724456, 0.730463, 0.732890, 0.734638, 0.735394, 0.735313, 0.734917, 0.734675, 0.734387, 0.734056, 0.733684, 0.733272, 0.732824, 0.732340, 0.732087, 0.731825, 0.731556, 0.731419, 0.731280, 0.731140, 0.731111, 0.731083, 0.731054, 0.731026, 0.730997, 0.730940, 0.730884, 0.730856, 0.730826},
{ 0.484184, 0.483875, 0.483854, 0.483978, 0.484024, 0.484025, 0.484021, 0.484018, 0.484017, 0.484018, 0.484042, 0.484065, 0.484085, 0.484127, 0.484168, 0.484209, 0.484290, 0.484369, 0.484446, 0.484522, 0.484596, 0.484669, 0.484741, 0.484913, 0.485076, 0.485375, 0.485640, 0.485872, 0.486244, 0.486507, 0.486677, 0.486770, 0.486800, 0.486782, 0.486730, 0.486656, 0.486569, 0.486480, 0.486397, 0.486326, 0.486241, 0.486343, 0.486682, 0.487294, 0.488203, 0.489426, 0.490979, 0.492871, 0.495114, 0.497717, 0.500692, 0.504049, 0.507801, 0.511962, 0.516549, 0.521581, 0.527079, 0.533069, 0.539580, 0.546644, 0.554302, 0.562597, 0.571582, 0.581316, 0.591867, 0.603312, 0.615732, 0.629208, 0.634908, 0.640786, 0.646839, 0.653058, 0.659424, 0.665905, 0.672440, 0.678924, 0.685167, 0.690812, 0.695146, 0.696586, 0.695185, 0.691462, 0.685653, 0.682791, 0.679803, 0.678575, 0.677372, 0.676170, 0.674960, 0.673761, 0.672565, 0.671376, 0.670784, 0.670195, 0.669607, 0.669314, 0.669021, 0.668729, 0.668671, 0.668612, 0.668554, 0.668496, 0.668438, 0.668321, 0.668204, 0.668146, 0.668093},
{ 0.431741, 0.432151, 0.432119, 0.431947, 0.432003, 0.432038, 0.432058, 0.432067, 0.432071, 0.432073, 0.432118, 0.432172, 0.432219, 0.432319, 0.432417, 0.432515, 0.432709, 0.432901, 0.433091, 0.433280, 0.433466, 0.433650, 0.433833, 0.434281, 0.434716, 0.435549, 0.436332, 0.437065, 0.438381, 0.439503, 0.440442, 0.441211, 0.441828, 0.442312, 0.442682, 0.442959, 0.443160, 0.443303, 0.443403, 0.443474, 0.443601, 0.443773, 0.444096, 0.444649, 0.445484, 0.446641, 0.448151, 0.450035, 0.452314, 0.455003, 0.458118, 0.461674, 0.465687, 0.470174, 0.475154, 0.480646, 0.486675, 0.493266, 0.500448, 0.508257, 0.516729, 0.525907, 0.535839, 0.546578, 0.558179, 0.570697, 0.584178, 0.598632, 0.604674, 0.610848, 0.617133, 0.623496, 0.629882, 0.636205, 0.642325, 0.648001, 0.652812, 0.655938, 0.655571, 0.646432, 0.632014, 0.585761, 0.561844, 0.551901, 0.541633, 0.537490, 0.533347, 0.529204, 0.525061, 0.520912, 0.516755, 0.512588, 0.510500, 0.508409, 0.506313, 0.505264, 0.504214, 0.503162, 0.502952, 0.502741, 0.502531, 0.502320, 0.502109, 0.501688, 0.501266, 0.501055, 0.500844},
{ 0.378411, 0.378009, 0.378304, 0.378423, 0.378337, 0.378379, 0.378429, 0.378463, 0.378477, 0.378480, 0.378547, 0.378632, 0.378705, 0.378866, 0.379023, 0.379180, 0.379492, 0.379803, 0.380112, 0.380420, 0.380726, 0.381030, 0.381333, 0.382082, 0.382820, 0.384261, 0.385654, 0.386995, 0.389515, 0.391806, 0.393858, 0.395669, 0.397243, 0.398591, 0.399730, 0.400680, 0.401465, 0.402108, 0.402632, 0.403060, 0.403833, 0.404384, 0.404907, 0.405540, 0.406384, 0.407512, 0.408977, 0.410822, 0.413078, 0.415773, 0.418930, 0.422571, 0.426716, 0.431386, 0.436603, 0.442389, 0.448768, 0.455767, 0.463416, 0.471746, 0.480794, 0.490597, 0.501194, 0.512627, 0.524930, 0.538125, 0.552204, 0.567080, 0.573206, 0.579391, 0.585591, 0.591738, 0.597731, 0.603413, 0.608532, 0.612672, 0.615090, 0.614343, 0.607403, 0.591834, 0.589895, 0.618559, 0.610264, 0.568180, 0.552753, 0.547793, 0.542827, 0.537718, 0.532494, 0.527212, 0.521893, 0.516526, 0.513820, 0.511097, 0.508356, 0.506978, 0.505594, 0.504205, 0.503926, 0.503647, 0.503368, 0.503089, 0.502810, 0.502250, 0.501689, 0.501408, 0.501127},
{ 0.327891, 0.327969, 0.327530, 0.327908, 0.327791, 0.327789, 0.327855, 0.327920, 0.327949, 0.327950, 0.328039, 0.328130, 0.328237, 0.328435, 0.328639, 0.328840, 0.329242, 0.329642, 0.330043, 0.330442, 0.330840, 0.331238, 0.331634, 0.332621, 0.333602, 0.335541, 0.337450, 0.339323, 0.342949, 0.346387, 0.349608, 0.352585, 0.355298, 0.357735, 0.359891, 0.361774, 0.363399, 0.364787, 0.365963, 0.366957, 0.368811, 0.370063, 0.371012, 0.371881, 0.372830, 0.373978, 0.375413, 0.377201, 0.379393, 0.382031, 0.385148, 0.388774, 0.392936, 0.397660, 0.402969, 0.408889, 0.415444, 0.422662, 0.430570, 0.439196, 0.448571, 0.458722, 0.469677, 0.481456, 0.494064, 0.507477, 0.521610, 0.536251, 0.542155, 0.548018, 0.553764, 0.559288, 0.564439, 0.568992, 0.572615, 0.574817, 0.574976, 0.572941, 0.572216, 0.580712, 0.582336, 0.575398, 0.576187, 0.607780, 0.599756, 0.600712, 0.601044, 0.596450, 0.597300, 0.595576, 0.594049, 0.592565, 0.591920, 0.591289, 0.590644, 0.590323, 0.590004, 0.589686, 0.589623, 0.589559, 0.589496, 0.589433, 0.589369, 0.589243, 0.589117, 0.589053, 0.588991},
{ 0.282233, 0.282890, 0.282956, 0.282808, 0.282895, 0.282817, 0.282871, 0.282969, 0.283015, 0.283004, 0.283121, 0.283198, 0.283332, 0.283555, 0.283776, 0.284000, 0.284453, 0.284905, 0.285356, 0.285808, 0.286259, 0.286710, 0.287161, 0.288288, 0.289414, 0.291659, 0.293895, 0.296117, 0.300508, 0.304798, 0.308952, 0.312931, 0.316696, 0.320211, 0.323444, 0.326372, 0.328986, 0.331286, 0.333289, 0.335018, 0.338331, 0.340577, 0.342167, 0.343420, 0.344573, 0.345802, 0.347235, 0.348969, 0.351079, 0.353624, 0.356651, 0.360199, 0.364303, 0.368992, 0.374296, 0.380240, 0.386850, 0.394150, 0.402165, 0.410917, 0.420425, 0.430706, 0.441768, 0.453601, 0.466170, 0.479388, 0.493075, 0.506864, 0.512262, 0.517493, 0.522458, 0.527028, 0.531035, 0.534286, 0.536616, 0.538129, 0.539814, 0.544015, 0.551154, 0.555856, 0.559470, 0.567869, 0.567126, 0.562079, 0.566807, 0.560762, 0.551738, 0.544985, 0.538995, 0.532949, 0.526659, 0.520224, 0.516962, 0.513664, 0.510325, 0.508638, 0.506939, 0.505227, 0.504883, 0.504538, 0.504193, 0.503847, 0.503500, 0.502805, 0.502108, 0.501758, 0.501408}};
  const double reomegaqnm30[8][107] = {{ 0.282233, 0.282890, 0.282956, 0.282808, 0.282895, 0.282817, 0.282871, 0.282969, 0.283015, 0.283004, 0.283121, 0.283198, 0.283332, 0.283555, 0.283776, 0.284000, 0.284453, 0.284905, 0.285356, 0.285808, 0.286259, 0.286710, 0.287161, 0.288288, 0.289414, 0.291659, 0.293895, 0.296117, 0.300508, 0.304798, 0.308952, 0.312931, 0.316696, 0.320211, 0.323444, 0.326372, 0.328986, 0.331286, 0.333289, 0.335018, 0.338331, 0.340577, 0.342167, 0.343420, 0.344573, 0.345802, 0.347235, 0.348969, 0.351079, 0.353624, 0.356651, 0.360199, 0.364303, 0.368992, 0.599562, 0.599920, 0.600519, 0.601364, 0.602462, 0.603820, 0.605450, 0.607365, 0.609583, 0.612123, 0.615010, 0.618276, 0.621957, 0.626099, 0.627897, 0.629782, 0.631760, 0.633834, 0.636010, 0.638296, 0.640697, 0.643221, 0.645877, 0.648674, 0.651621, 0.654730, 0.656348, 0.658011, 0.659719, 0.660591, 0.661475, 0.661832, 0.662191, 0.662551, 0.662914, 0.663279, 0.663646, 0.664014, 0.664199, 0.664385, 0.664569, 0.664660, 0.664749, 0.664839, 0.664859, 0.664880, 0.664904, 0.664934, 0.664972, 0.665109, 0.665498, 0.665982, 0.666966},
{ 0.282233, 0.282890, 0.282956, 0.282808, 0.282895, 0.282817, 0.282871, 0.282969, 0.283015, 0.283004, 0.283121, 0.283198, 0.283332, 0.283555, 0.283776, 0.284000, 0.284453, 0.284905, 0.285356, 0.285808, 0.286259, 0.286710, 0.287161, 0.288288, 0.289414, 0.291659, 0.293895, 0.296117, 0.300508, 0.304798, 0.308952, 0.312931, 0.316696, 0.320211, 0.323444, 0.326372, 0.328986, 0.331286, 0.333289, 0.335018, 0.338331, 0.340577, 0.342167, 0.343420, 0.344573, 0.345802, 0.347235, 0.348969, 0.351079, 0.353624, 0.356651, 0.360199, 0.364303, 0.368992, 0.582779, 0.583184, 0.583863, 0.584820, 0.586062, 0.587597, 0.589436, 0.591594, 0.594085, 0.596930, 0.600152, 0.603777, 0.607836, 0.612363, 0.614312, 0.616344, 0.618461, 0.620665, 0.622957, 0.625337, 0.627805, 0.630358, 0.632992, 0.635698, 0.638461, 0.641259, 0.642661, 0.644057, 0.645442, 0.646128, 0.646807, 0.647077, 0.647346, 0.647614, 0.647880, 0.648144, 0.648407, 0.648669, 0.648799, 0.648929, 0.649058, 0.649122, 0.649187, 0.649253, 0.649266, 0.649279, 0.649291, 0.649303, 0.649313, 0.649323, 0.649294, 0.649244, 0.649152},
{ 0.282233, 0.282890, 0.282956, 0.282808, 0.282895, 0.282817, 0.282871, 0.282969, 0.283015, 0.283004, 0.283121, 0.283198, 0.283332, 0.283555, 0.283776, 0.284000, 0.284453, 0.284905, 0.285356, 0.285808, 0.286259, 0.286710, 0.287161, 0.288288, 0.289414, 0.291659, 0.293895, 0.296117, 0.300508, 0.304798, 0.308952, 0.312931, 0.316696, 0.320211, 0.323444, 0.326372, 0.328986, 0.331286, 0.333289, 0.335018, 0.338331, 0.340577, 0.342167, 0.343420, 0.344573, 0.345802, 0.347235, 0.348969, 0.351079, 0.353624, 0.356651, 0.360199, 0.364303, 0.368992, 0.551849, 0.552343, 0.553168, 0.554330, 0.555836, 0.557693, 0.559913, 0.562508, 0.565493, 0.568882, 0.572694, 0.576945, 0.581647, 0.586805, 0.588993, 0.591248, 0.593567, 0.595944, 0.598370, 0.600831, 0.603310, 0.605782, 0.608213, 0.610555, 0.612746, 0.614707, 0.615572, 0.616342, 0.617006, 0.617294, 0.617551, 0.617645, 0.617733, 0.617816, 0.617894, 0.617966, 0.618032, 0.618093, 0.618122, 0.618148, 0.618174, 0.618186, 0.618198, 0.618209, 0.618210, 0.618212, 0.618214, 0.618216, 0.618219, 0.618230, 0.618254, 0.618264, 0.618233},
{ 0.282233, 0.282890, 0.282956, 0.282808, 0.282895, 0.282817, 0.282871, 0.282969, 0.283015, 0.283004, 0.283121, 0.283198, 0.283332, 0.283555, 0.283776, 0.284000, 0.284453, 0.284905, 0.285356, 0.285808, 0.286259, 0.286710, 0.287161, 0.288288, 0.289414, 0.291659, 0.293895, 0.296117, 0.300508, 0.304798, 0.308952, 0.312931, 0.316696, 0.320211, 0.323444, 0.326372, 0.328986, 0.331286, 0.333289, 0.335018, 0.338331, 0.340577, 0.342167, 0.343420, 0.344573, 0.345802, 0.347235, 0.348969, 0.351079, 0.353624, 0.356651, 0.360199, 0.364303, 0.368992, 0.512162, 0.512763, 0.513767, 0.515179, 0.517004, 0.519250, 0.521925, 0.525038, 0.528599, 0.532614, 0.537089, 0.542016, 0.547375, 0.553111, 0.555487, 0.557894, 0.560315, 0.562730, 0.565110, 0.567417, 0.569602, 0.571599, 0.573324, 0.574673, 0.575528, 0.575774, 0.575639, 0.575325, 0.574828, 0.574511, 0.574151, 0.573994, 0.573831, 0.573660, 0.573483, 0.573299, 0.573109, 0.572912, 0.572811, 0.572709, 0.572605, 0.572552, 0.572499, 0.572445, 0.572435, 0.572425, 0.572415, 0.572405, 0.572395, 0.572369, 0.572328, 0.572320, 0.572370},
{ 0.282233, 0.282890, 0.282956, 0.282808, 0.282895, 0.282817, 0.282871, 0.282969, 0.283015, 0.283004, 0.283121, 0.283198, 0.283332, 0.283555, 0.283776, 0.284000, 0.284453, 0.284905, 0.285356, 0.285808, 0.286259, 0.286710, 0.287161, 0.288288, 0.289414, 0.291659, 0.293895, 0.296117, 0.300508, 0.304798, 0.308952, 0.312931, 0.316696, 0.320211, 0.323444, 0.326372, 0.328986, 0.331286, 0.333289, 0.335018, 0.338331, 0.340577, 0.342167, 0.343420, 0.344573, 0.345802, 0.347235, 0.348969, 0.351079, 0.353624, 0.356651, 0.360199, 0.364303, 0.368992, 0.470407, 0.471107, 0.472275, 0.473914, 0.476029, 0.478623, 0.481699, 0.485259, 0.489303, 0.493821, 0.498793, 0.504175, 0.509885, 0.515766, 0.518107, 0.520404, 0.522620, 0.524706, 0.526601, 0.528226, 0.529475, 0.530221, 0.530314, 0.529605, 0.527994, 0.525482, 0.523916, 0.522167, 0.520255, 0.519245, 0.518203, 0.517777, 0.517347, 0.516912, 0.516474, 0.516031, 0.515584, 0.515133, 0.514906, 0.514679, 0.514450, 0.514336, 0.514221, 0.514107, 0.514085, 0.514062, 0.514038, 0.514013, 0.513986, 0.513938, 0.513927, 0.513901, 0.513786},
{ 0.282233, 0.282890, 0.282956, 0.282808, 0.282895, 0.282817, 0.282871, 0.282969, 0.283015, 0.283004, 0.283121, 0.283198, 0.283332, 0.283555, 0.283776, 0.284000, 0.284453, 0.284905, 0.285356, 0.285808, 0.286259, 0.286710, 0.287161, 0.288288, 0.289414, 0.291659, 0.293895, 0.296117, 0.300508, 0.304798, 0.308952, 0.312931, 0.316696, 0.320211, 0.323444, 0.326372, 0.328986, 0.331286, 0.333289, 0.335018, 0.338331, 0.340577, 0.342167, 0.343420, 0.344573, 0.345802, 0.347235, 0.348969, 0.351079, 0.353624, 0.356651, 0.360199, 0.364303, 0.368992, 0.431644, 0.432418, 0.433707, 0.435512, 0.437834, 0.440669, 0.444013, 0.447856, 0.452178, 0.456946, 0.462098, 0.467532, 0.473068, 0.478389, 0.480341, 0.482117, 0.483646, 0.484836, 0.485569, 0.485694, 0.485025, 0.483359, 0.480530, 0.476531, 0.471580, 0.465954, 0.462965, 0.459898, 0.456775, 0.455199, 0.453616, 0.452982, 0.452347, 0.451711, 0.451075, 0.450438, 0.449801, 0.449164, 0.448846, 0.448528, 0.448209, 0.448050, 0.447889, 0.447735, 0.447703, 0.447668, 0.447631, 0.447595, 0.447565, 0.447530, 0.447412, 0.447346, 0.447470},
{ 0.282233, 0.282890, 0.282956, 0.282808, 0.282895, 0.282817, 0.282871, 0.282969, 0.283015, 0.283004, 0.283121, 0.283198, 0.283332, 0.283555, 0.283776, 0.284000, 0.284453, 0.284905, 0.285356, 0.285808, 0.286259, 0.286710, 0.287161, 0.288288, 0.289414, 0.291659, 0.293895, 0.296117, 0.300508, 0.304798, 0.308952, 0.312931, 0.316696, 0.320211, 0.323444, 0.326372, 0.328986, 0.331286, 0.333289, 0.335018, 0.338331, 0.340577, 0.342167, 0.343420, 0.344573, 0.345802, 0.347235, 0.348969, 0.351079, 0.353624, 0.356651, 0.360199, 0.364303, 0.368992, 0.397933, 0.398754, 0.400121, 0.402029, 0.404472, 0.407440, 0.410914, 0.414868, 0.419255, 0.424004, 0.428999, 0.434048, 0.438831, 0.442785, 0.443930, 0.444691, 0.444941, 0.444515, 0.443201, 0.440739, 0.436855, 0.431399, 0.424647, 0.417293, 0.409623, 0.401877, 0.398025, 0.394206, 0.390427, 0.388555, 0.386695, 0.385955, 0.385217, 0.384480, 0.383746, 0.383014, 0.382285, 0.381557, 0.381194, 0.380831, 0.380468, 0.380291, 0.380104, 0.379937, 0.379896, 0.379849, 0.379803, 0.379771, 0.379760, 0.379683, 0.379573, 0.379702, 0.379469},
{ 0.282233, 0.282890, 0.282956, 0.282808, 0.282895, 0.282817, 0.282871, 0.282969, 0.283015, 0.283004, 0.283121, 0.283198, 0.283332, 0.283555, 0.283776, 0.284000, 0.284453, 0.284905, 0.285356, 0.285808, 0.286259, 0.286710, 0.287161, 0.288288, 0.289414, 0.291659, 0.293895, 0.296117, 0.300508, 0.304798, 0.308952, 0.312931, 0.316696, 0.320211, 0.323444, 0.326372, 0.328986, 0.331286, 0.333289, 0.335018, 0.338331, 0.340577, 0.342167, 0.343420, 0.344573, 0.345802, 0.347235, 0.348969, 0.351079, 0.353624, 0.356651, 0.360199, 0.364303, 0.368992, 0.369276, 0.370125, 0.371535, 0.373496, 0.375995, 0.379009, 0.382504, 0.386428, 0.390703, 0.395206, 0.399746, 0.404011, 0.407482, 0.409242, 0.409125, 0.408309, 0.406575, 0.403632, 0.399118, 0.392662, 0.384204, 0.374776, 0.365847, 0.356745, 0.348034, 0.339576, 0.335477, 0.331464, 0.327534, 0.325598, 0.323683, 0.322922, 0.322164, 0.321409, 0.320658, 0.319909, 0.319163, 0.318421, 0.318049, 0.317679, 0.317312, 0.317127, 0.316938, 0.316781, 0.316729, 0.316663, 0.316616, 0.316616, 0.316630, 0.316423, 0.316583, 0.316299, 0.316258}};
  const double reomegaqnm44[8][107] = {{ 0.603485, 0.613847, 0.619636, 0.623952, 0.624219, 0.623894, 0.623504, 0.623122, 0.622780, 0.622487, 0.621648, 0.621375, 0.621309, 0.621365, 0.621483, 0.621613, 0.621877, 0.622141, 0.622404, 0.622667, 0.622930, 0.623194, 0.623458, 0.624119, 0.624781, 0.626113, 0.627452, 0.628799, 0.631518, 0.634269, 0.637054, 0.639872, 0.642726, 0.645615, 0.648541, 0.651503, 0.654504, 0.657544, 0.660623, 0.663743, 0.671728, 0.679989, 0.688543, 0.697411, 0.706611, 0.716168, 0.726107, 0.736455, 0.747243, 0.758508, 0.770286, 0.782624, 0.795569, 0.809178, 0.823517, 0.838660, 0.854693, 0.871718, 0.889853, 0.909242, 0.930054, 0.952500, 0.976839, 1.003396, 1.032592, 1.064981, 1.101315, 1.142654, 1.160918, 1.180359, 1.201137, 1.223450, 1.247547, 1.273743, 1.302451, 1.334225, 1.369837, 1.410416, 1.457726, 1.514785, 1.548623, 1.587589, 1.633903, 1.661025, 1.691943, 1.705697, 1.720452, 1.736406, 1.753835, 1.773136, 1.794920, 1.820217, 1.834740, 1.851041, 1.869853, 1.880596, 1.892605, 1.906400, 1.909447, 1.912614, 1.915915, 1.919368, 1.922993, 1.930875, 1.939891, 1.945001, 1.950681},
{ 0.592310, 0.596523, 0.598412, 0.599977, 0.600617, 0.600806, 0.600947, 0.601053, 0.601136, 0.601202, 0.601389, 0.601485, 0.601558, 0.601693, 0.601828, 0.601964, 0.602238, 0.602511, 0.602785, 0.603060, 0.603334, 0.603609, 0.603884, 0.604574, 0.605265, 0.606653, 0.608050, 0.609455, 0.612290, 0.615158, 0.618061, 0.620998, 0.623972, 0.626982, 0.630029, 0.633115, 0.636240, 0.639405, 0.642610, 0.645858, 0.654166, 0.662758, 0.671651, 0.680865, 0.690420, 0.700341, 0.710653, 0.721383, 0.732565, 0.744232, 0.756425, 0.769188, 0.782571, 0.796632, 0.811434, 0.827055, 0.843581, 0.861114, 0.879773, 0.899703, 0.921074, 0.944098, 0.969034, 0.996209, 1.026044, 1.059094, 1.096112, 1.138157, 1.156711, 1.176444, 1.197519, 1.220132, 1.244532, 1.271035, 1.300052, 1.332138, 1.368063, 1.408958, 1.456584, 1.513957, 1.547951, 1.587070, 1.633535, 1.660729, 1.691718, 1.705500, 1.720282, 1.736263, 1.753718, 1.773044, 1.794851, 1.820170, 1.834704, 1.851014, 1.869835, 1.880582, 1.892595, 1.906393, 1.909441, 1.912608, 1.915910, 1.919363, 1.922989, 1.930872, 1.939889, 1.945000, 1.950680},
{ 0.559386, 0.561881, 0.562985, 0.563673, 0.563747, 0.563732, 0.563712, 0.563695, 0.563683, 0.563678, 0.563718, 0.563794, 0.563872, 0.564021, 0.564168, 0.564316, 0.564612, 0.564908, 0.565204, 0.565501, 0.565798, 0.566096, 0.566394, 0.567139, 0.567887, 0.569390, 0.570901, 0.572420, 0.575484, 0.578584, 0.581720, 0.584892, 0.588102, 0.591350, 0.594637, 0.597965, 0.601333, 0.604743, 0.608195, 0.611692, 0.620630, 0.629865, 0.639415, 0.649299, 0.659540, 0.670161, 0.681189, 0.692653, 0.704586, 0.717023, 0.730006, 0.743579, 0.757794, 0.772710, 0.788392, 0.804918, 0.822375, 0.840868, 0.860518, 0.881470, 0.903897, 0.928012, 0.954076, 0.982419, 1.013463, 1.047766, 1.086081, 1.129470, 1.148575, 1.168866, 1.190507, 1.213693, 1.238674, 1.265764, 1.295376, 1.328062, 1.364594, 1.406100, 1.454338, 1.512324, 1.546623, 1.586043, 1.632803, 1.660142, 1.691272, 1.705109, 1.719944, 1.735978, 1.753484, 1.772859, 1.794714, 1.820077, 1.834631, 1.850961, 1.869799, 1.880554, 1.892574, 1.906379, 1.909429, 1.912597, 1.915900, 1.919355, 1.922982, 1.930867, 1.939885, 1.944997, 1.950678},
{ 0.514416, 0.513669, 0.512866, 0.512200, 0.512256, 0.512336, 0.512404, 0.512455, 0.512488, 0.512510, 0.512565, 0.512640, 0.512726, 0.512889, 0.513051, 0.513214, 0.513540, 0.513866, 0.514193, 0.514520, 0.514848, 0.515175, 0.515503, 0.516325, 0.517149, 0.518804, 0.520467, 0.522139, 0.525511, 0.528920, 0.532367, 0.535852, 0.539377, 0.542943, 0.546549, 0.550198, 0.553889, 0.557625, 0.561406, 0.565233, 0.575009, 0.585097, 0.595517, 0.606289, 0.617438, 0.628986, 0.640963, 0.653399, 0.666327, 0.679785, 0.693815, 0.708464, 0.723784, 0.739837, 0.756690, 0.774421, 0.793122, 0.812899, 0.833875, 0.856197, 0.880043, 0.905628, 0.933216, 0.963140, 0.995828, 1.031838, 1.071929, 1.117168, 1.137033, 1.158097, 1.180522, 1.204506, 1.230297, 1.258209, 1.288655, 1.322187, 1.359576, 1.401950, 1.451065, 1.509932, 1.544671, 1.584530, 1.631722, 1.659272, 1.690609, 1.704527, 1.719441, 1.735553, 1.753135, 1.772584, 1.794508, 1.819937, 1.834521, 1.850880, 1.869745, 1.880512, 1.892544, 1.906359, 1.909410, 1.912581, 1.915886, 1.919342, 1.922970, 1.930859, 1.939880, 1.944993, 1.950675},
{ 0.454601, 0.454746, 0.455842, 0.456343, 0.456046, 0.455998, 0.456018, 0.456071, 0.456127, 0.456171, 0.456246, 0.456327, 0.456423, 0.456596, 0.456773, 0.456949, 0.457302, 0.457655, 0.458009, 0.458363, 0.458717, 0.459072, 0.459427, 0.460317, 0.461208, 0.462999, 0.464799, 0.466609, 0.470257, 0.473946, 0.477675, 0.481446, 0.485259, 0.489115, 0.493016, 0.496962, 0.500954, 0.504993, 0.509081, 0.513219, 0.523786, 0.534689, 0.545948, 0.557585, 0.569625, 0.582093, 0.595020, 0.608436, 0.622378, 0.636885, 0.652000, 0.667772, 0.684256, 0.701516, 0.719620, 0.738651, 0.758702, 0.779882, 0.802317, 0.826160, 0.851591, 0.878829, 0.908143, 0.939873, 0.974449, 1.012439, 1.054606, 1.102023, 1.122789, 1.144773, 1.168136, 1.193076, 1.219842, 1.248749, 1.280208, 1.314774, 1.353216, 1.396662, 1.446869, 1.506844, 1.542142, 1.582559, 1.630306, 1.658130, 1.689736, 1.703759, 1.718778, 1.734991, 1.752673, 1.772218, 1.794235, 1.819751, 1.834376, 1.850773, 1.869673, 1.880456, 1.892503, 1.906332, 1.909386, 1.912559, 1.915866, 1.919324, 1.922955, 1.930848, 1.939873, 1.944987, 1.950672},
{ 0.405546, 0.404573, 0.403366, 0.404025, 0.404115, 0.403989, 0.403940, 0.403971, 0.404037, 0.404098, 0.404160, 0.404265, 0.404355, 0.404535, 0.404715, 0.404897, 0.405260, 0.405624, 0.405988, 0.406353, 0.406718, 0.407083, 0.407449, 0.408365, 0.409284, 0.411129, 0.412986, 0.414853, 0.418619, 0.422430, 0.426286, 0.430188, 0.434137, 0.438134, 0.442180, 0.446277, 0.450425, 0.454625, 0.458878, 0.463187, 0.474206, 0.485594, 0.497376, 0.509573, 0.522212, 0.535321, 0.548930, 0.563075, 0.577791, 0.593121, 0.609109, 0.625808, 0.643274, 0.661572, 0.680777, 0.700971, 0.722252, 0.744732, 0.768542, 0.793839, 0.820807, 0.849672, 0.880708, 0.914263, 0.950774, 0.990817, 1.035165, 1.084901, 1.106636, 1.129614, 1.153995, 1.179980, 1.207817, 1.237823, 1.270410, 1.306132, 1.345761, 1.390425, 1.441882, 1.503141, 1.539093, 1.580170, 1.628580, 1.656733, 1.688664, 1.702815, 1.717960, 1.734298, 1.752101, 1.771765, 1.793895, 1.819518, 1.834194, 1.850639, 1.869583, 1.880387, 1.892453, 1.906298, 1.909355, 1.912532, 1.915842, 1.919303, 1.922936, 1.930834, 1.939864, 1.944981, 1.950667},
{ 0.359760, 0.362048, 0.362088, 0.361405, 0.361838, 0.361724, 0.361635, 0.361653, 0.361731, 0.361798, 0.361829, 0.361951, 0.362022, 0.362204, 0.362384, 0.362559, 0.362914, 0.363269, 0.363624, 0.363980, 0.364336, 0.364693, 0.365050, 0.365945, 0.366843, 0.368648, 0.370465, 0.372293, 0.375986, 0.379729, 0.383522, 0.387367, 0.391264, 0.395214, 0.399220, 0.403281, 0.407400, 0.411576, 0.415813, 0.420110, 0.431129, 0.442560, 0.454427, 0.466756, 0.479575, 0.492914, 0.506807, 0.521289, 0.536401, 0.552185, 0.568691, 0.585972, 0.604089, 0.623109, 0.643109, 0.664176, 0.686412, 0.709933, 0.734875, 0.761399, 0.789696, 0.819999, 0.852590, 0.887824, 0.926150, 0.968153, 1.014622, 1.066653, 1.089358, 1.113337, 1.138753, 1.165806, 1.194747, 1.225892, 1.259656, 1.296595, 1.337482, 1.383447, 1.436255, 1.498918, 1.535593, 1.577409, 1.626568, 1.655098, 1.687403, 1.701703, 1.716995, 1.733478, 1.751424, 1.771226, 1.793491, 1.819241, 1.833977, 1.850478, 1.869475, 1.880303, 1.892391, 1.906258, 1.909318, 1.912499, 1.915812, 1.919277, 1.922914, 1.930818, 1.939853, 1.944972, 1.950661},
{ 0.330474, 0.328796, 0.329888, 0.329377, 0.329765, 0.329704, 0.329609, 0.329627, 0.329709, 0.329764, 0.329799, 0.329879, 0.329976, 0.330138, 0.330301, 0.330468, 0.330801, 0.331135, 0.331469, 0.331804, 0.332139, 0.332474, 0.332810, 0.333653, 0.334498, 0.336199, 0.337912, 0.339638, 0.343129, 0.346673, 0.350272, 0.353926, 0.357636, 0.361405, 0.365233, 0.369122, 0.373073, 0.377087, 0.381166, 0.385312, 0.395976, 0.407089, 0.418680, 0.430776, 0.443408, 0.456611, 0.470421, 0.484877, 0.500022, 0.515903, 0.532572, 0.550085, 0.568507, 0.587909, 0.608371, 0.629986, 0.652859, 0.677111, 0.702884, 0.730344, 0.759691, 0.791165, 0.825057, 0.861733, 0.901654, 0.945421, 0.993838, 1.048020, 1.071648, 1.096587, 1.123003, 1.151096, 1.181120, 1.213393, 1.248331, 1.286493, 1.328654, 1.375949, 1.430149, 1.494278, 1.531719, 1.574326, 1.624300, 1.653246, 1.685968, 1.700434, 1.715892, 1.732539, 1.750646, 1.770607, 1.793024, 1.818920, 1.833724, 1.850291, 1.869349, 1.880205, 1.892320, 1.906211, 1.909276, 1.912460, 1.915778, 1.919247, 1.922887, 1.930799, 1.939840, 1.944963, 1.950654}};
  const double reomegaqnm43[8][107] = {{ 0.664561, 0.671781, 0.674607, 0.674567, 0.672823, 0.672081, 0.671488, 0.671028, 0.670676, 0.670408, 0.669768, 0.669607, 0.669582, 0.669640, 0.669730, 0.669825, 0.670015, 0.670204, 0.670393, 0.670583, 0.670772, 0.670962, 0.671152, 0.671628, 0.672106, 0.673067, 0.674034, 0.675008, 0.676976, 0.678971, 0.680993, 0.683044, 0.685124, 0.687232, 0.689371, 0.691539, 0.693739, 0.695971, 0.698234, 0.700531, 0.706420, 0.712531, 0.718876, 0.725468, 0.732322, 0.739454, 0.746883, 0.754628, 0.762712, 0.771159, 0.779996, 0.789254, 0.798968, 0.809178, 0.819929, 0.831271, 0.843264, 0.855978, 0.869492, 0.883903, 0.899326, 0.915897, 0.933787, 0.953206, 0.974424, 0.997788, 1.023764, 1.052994, 1.065793, 1.079333, 1.093709, 1.109033, 1.125444, 1.143116, 1.162272, 1.183207, 1.206320, 1.232181, 1.261648, 1.296128, 1.315984, 1.338258, 1.363840, 1.378331, 1.394372, 1.401333, 1.408674, 1.416458, 1.424768, 1.433721, 1.443487, 1.454334, 1.460298, 1.466741, 1.473812, 1.477658, 1.481774, 1.486238, 1.487183, 1.488148, 1.489135, 1.490146, 1.491183, 1.493346, 1.495650, 1.496867, 1.498136},
{ 0.649267, 0.650865, 0.651497, 0.652059, 0.652330, 0.652416, 0.652482, 0.652533, 0.652574, 0.652606, 0.652704, 0.652761, 0.652810, 0.652906, 0.653002, 0.653098, 0.653292, 0.653486, 0.653680, 0.653874, 0.654069, 0.654263, 0.654459, 0.654947, 0.655438, 0.656424, 0.657416, 0.658415, 0.660434, 0.662481, 0.664556, 0.666660, 0.668793, 0.670957, 0.673152, 0.675378, 0.677636, 0.679927, 0.682252, 0.684611, 0.690663, 0.696946, 0.703473, 0.710258, 0.717317, 0.724667, 0.732325, 0.740314, 0.748654, 0.757372, 0.766495, 0.776055, 0.786087, 0.796632, 0.807733, 0.819445, 0.831827, 0.844949, 0.858893, 0.873754, 0.889649, 0.906718, 0.925129, 0.945097, 0.966889, 0.990856, 1.017463, 1.047353, 1.060422, 1.074238, 1.088892, 1.104496, 1.121189, 1.139143, 1.158579, 1.179788, 1.203166, 1.229273, 1.258954, 1.293593, 1.313493, 1.335773, 1.361305, 1.375739, 1.391692, 1.398606, 1.405892, 1.413612, 1.421846, 1.430710, 1.440368, 1.451085, 1.456973, 1.463330, 1.470306, 1.474100, 1.478163, 1.482573, 1.483508, 1.484463, 1.485440, 1.486442, 1.487470, 1.489620, 1.491920, 1.493142, 1.494424},
{ 0.618226, 0.619662, 0.620260, 0.620602, 0.620631, 0.620624, 0.620617, 0.620614, 0.620613, 0.620616, 0.620657, 0.620710, 0.620763, 0.620865, 0.620966, 0.621069, 0.621273, 0.621477, 0.621682, 0.621887, 0.622092, 0.622298, 0.622504, 0.623019, 0.623536, 0.624576, 0.625622, 0.626675, 0.628801, 0.630955, 0.633139, 0.635352, 0.637596, 0.639872, 0.642180, 0.644521, 0.646896, 0.649306, 0.651751, 0.654233, 0.660603, 0.667221, 0.674101, 0.681259, 0.688712, 0.696478, 0.704577, 0.713030, 0.721860, 0.731095, 0.740763, 0.750896, 0.761531, 0.772710, 0.784478, 0.796890, 0.810008, 0.823902, 0.838656, 0.854369, 0.871157, 0.889163, 0.908559, 0.929559, 0.952436, 0.977540, 1.005338, 1.036473, 1.050055, 1.064390, 1.079571, 1.095707, 1.112937, 1.131429, 1.151401, 1.173140, 1.197032, 1.223628, 1.253754, 1.288759, 1.308798, 1.331174, 1.356745, 1.371175, 1.387109, 1.394013, 1.401288, 1.408998, 1.417230, 1.426102, 1.435794, 1.446595, 1.452561, 1.459040, 1.466211, 1.470148, 1.474403, 1.479083, 1.480085, 1.481114, 1.482171, 1.483261, 1.484387, 1.486766, 1.489363, 1.490769, 1.492270},
{ 0.577515, 0.577137, 0.576762, 0.576508, 0.576555, 0.576591, 0.576620, 0.576642, 0.576657, 0.576668, 0.576714, 0.576768, 0.576824, 0.576935, 0.577046, 0.577156, 0.577378, 0.577600, 0.577822, 0.578044, 0.578266, 0.578489, 0.578712, 0.579270, 0.579830, 0.580954, 0.582084, 0.583221, 0.585513, 0.587833, 0.590180, 0.592557, 0.594964, 0.597402, 0.599874, 0.602379, 0.604920, 0.607496, 0.610110, 0.612762, 0.619569, 0.626640, 0.633995, 0.641651, 0.649629, 0.657947, 0.666628, 0.675696, 0.685176, 0.695095, 0.705484, 0.716378, 0.727815, 0.739837, 0.752493, 0.765838, 0.779937, 0.794862, 0.810700, 0.827552, 0.845537, 0.864800, 0.885517, 0.907906, 0.932243, 0.958881, 0.988290, 1.021114, 1.035393, 1.050437, 1.066338, 1.083205, 1.101173, 1.120410, 1.141131, 1.163618, 1.188250, 1.215570, 1.246391, 1.282050, 1.302397, 1.325072, 1.350952, 1.365555, 1.381693, 1.388695, 1.396082, 1.403923, 1.412311, 1.421378, 1.431320, 1.442459, 1.448645, 1.455393, 1.462908, 1.467057, 1.471563, 1.476549, 1.477621, 1.478723, 1.479859, 1.481032, 1.482246, 1.484822, 1.487650, 1.489191, 1.490846},
{ 0.525789, 0.525845, 0.526285, 0.526489, 0.526396, 0.526387, 0.526402, 0.526425, 0.526449, 0.526469, 0.526526, 0.526586, 0.526649, 0.526770, 0.526893, 0.527015, 0.527260, 0.527504, 0.527749, 0.527995, 0.528240, 0.528485, 0.528731, 0.529346, 0.529962, 0.531197, 0.532436, 0.533681, 0.536184, 0.538710, 0.541259, 0.543833, 0.546434, 0.549064, 0.551724, 0.554416, 0.557141, 0.559902, 0.562699, 0.565535, 0.572804, 0.580347, 0.588189, 0.596352, 0.604861, 0.613740, 0.623015, 0.632711, 0.642858, 0.653485, 0.664626, 0.676316, 0.688598, 0.701516, 0.715120, 0.729469, 0.744629, 0.760677, 0.777701, 0.795805, 0.815114, 0.835775, 0.857968, 0.881917, 0.907901, 0.936280, 0.967529, 1.002295, 1.017380, 1.033248, 1.049988, 1.067712, 1.086553, 1.106679, 1.128303, 1.151707, 1.177270, 1.205533, 1.237316, 1.273974, 1.294851, 1.318099, 1.344631, 1.359612, 1.376187, 1.383386, 1.390988, 1.399066, 1.407718, 1.417085, 1.427376, 1.438935, 1.445369, 1.452403, 1.460257, 1.464604, 1.469335, 1.474587, 1.475718, 1.476882, 1.478083, 1.479324, 1.480610, 1.483343, 1.486355, 1.488001, 1.489774},
{ 0.476971, 0.476710, 0.476193, 0.476399, 0.476477, 0.476442, 0.476428, 0.476440, 0.476467, 0.476494, 0.476558, 0.476627, 0.476696, 0.476830, 0.476965, 0.477100, 0.477370, 0.477640, 0.477910, 0.478180, 0.478450, 0.478720, 0.478991, 0.479667, 0.480344, 0.481698, 0.483055, 0.484415, 0.487142, 0.489882, 0.492637, 0.495408, 0.498200, 0.501013, 0.503849, 0.506712, 0.509604, 0.512526, 0.515481, 0.518472, 0.526117, 0.534029, 0.542242, 0.550788, 0.559696, 0.568998, 0.578725, 0.588908, 0.599580, 0.610775, 0.622529, 0.634884, 0.647882, 0.661572, 0.676009, 0.691252, 0.707372, 0.724448, 0.742573, 0.761856, 0.782423, 0.804427, 0.828056, 0.853536, 0.881154, 0.911277, 0.944388, 0.981145, 0.997065, 1.013790, 1.031413, 1.050044, 1.069820, 1.090910, 1.113530, 1.137967, 1.164607, 1.194005, 1.227005, 1.265008, 1.286634, 1.310709, 1.338182, 1.353696, 1.370862, 1.378319, 1.386193, 1.394562, 1.403528, 1.413236, 1.423906, 1.435897, 1.442576, 1.449883, 1.458049, 1.462574, 1.467504, 1.472983, 1.474165, 1.475382, 1.476637, 1.477935, 1.479282, 1.482147, 1.485310, 1.487042, 1.488911},
{ 0.430443, 0.431347, 0.431584, 0.431207, 0.431429, 0.431412, 0.431378, 0.431377, 0.431405, 0.431440, 0.431503, 0.431587, 0.431654, 0.431803, 0.431949, 0.432095, 0.432388, 0.432680, 0.432973, 0.433266, 0.433558, 0.433851, 0.434143, 0.434874, 0.435606, 0.437068, 0.438530, 0.439992, 0.442918, 0.445847, 0.448782, 0.451724, 0.454675, 0.457639, 0.460618, 0.463614, 0.466631, 0.469671, 0.472736, 0.475831, 0.483711, 0.491834, 0.500242, 0.508978, 0.518083, 0.527594, 0.537552, 0.547994, 0.558959, 0.570486, 0.582619, 0.595401, 0.608880, 0.623109, 0.638145, 0.654053, 0.670907, 0.688791, 0.707800, 0.728048, 0.749667, 0.772816, 0.797685, 0.824511, 0.853588, 0.885293, 0.920120, 0.958744, 0.975458, 0.993007, 1.011487, 1.031010, 1.051718, 1.073786, 1.097437, 1.122970, 1.150787, 1.181468, 1.215892, 1.255523, 1.278068, 1.303156, 1.331765, 1.347908, 1.365756, 1.373503, 1.381682, 1.390369, 1.399671, 1.409737, 1.420794, 1.433213, 1.440128, 1.447693, 1.456148, 1.460834, 1.465942, 1.471622, 1.472848, 1.474111, 1.475414, 1.476762, 1.478161, 1.481139, 1.484431, 1.486237, 1.488188},
{ 0.393654, 0.392526, 0.393009, 0.392902, 0.393058, 0.393081, 0.393041, 0.393030, 0.393062, 0.393104, 0.393163, 0.393249, 0.393326, 0.393480, 0.393635, 0.393790, 0.394099, 0.394409, 0.394718, 0.395027, 0.395336, 0.395646, 0.395955, 0.396727, 0.397499, 0.399043, 0.400585, 0.402126, 0.405206, 0.408283, 0.411357, 0.414431, 0.417507, 0.420586, 0.423670, 0.426763, 0.429866, 0.432984, 0.436119, 0.439273, 0.447270, 0.455468, 0.463919, 0.472677, 0.481794, 0.491318, 0.501298, 0.511782, 0.522814, 0.534442, 0.546714, 0.559680, 0.573393, 0.587909, 0.603291, 0.619609, 0.636939, 0.655370, 0.675002, 0.695954, 0.718361, 0.742390, 0.768236, 0.796144, 0.826417, 0.859443, 0.895735, 0.935992, 0.953414, 0.971707, 0.990971, 1.011324, 1.032916, 1.055929, 1.080602, 1.107247, 1.136292, 1.168346, 1.204332, 1.245775, 1.269347, 1.295564, 1.325428, 1.342256, 1.360839, 1.368897, 1.377396, 1.386417, 1.396067, 1.406499, 1.417946, 1.430788, 1.437932, 1.445743, 1.454468, 1.459304, 1.464574, 1.470436, 1.471702, 1.473006, 1.474351, 1.475743, 1.477188, 1.480267, 1.483672, 1.485542, 1.487563}};
  const double reomegaqnm42[8][107] = {{ 0.734414, 0.736680, 0.735600, 0.731902, 0.729696, 0.729070, 0.728636, 0.728333, 0.728117, 0.727963, 0.727630, 0.727557, 0.727547, 0.727572, 0.727608, 0.727645, 0.727718, 0.727792, 0.727866, 0.727940, 0.728015, 0.728090, 0.728165, 0.728354, 0.728546, 0.728935, 0.729332, 0.729738, 0.730574, 0.731442, 0.732344, 0.733278, 0.734246, 0.735247, 0.736280, 0.737348, 0.738448, 0.739582, 0.740750, 0.741952, 0.745108, 0.748482, 0.752080, 0.755907, 0.759972, 0.764284, 0.768851, 0.773686, 0.778801, 0.784212, 0.789935, 0.795988, 0.802395, 0.809178, 0.816368, 0.823994, 0.832096, 0.840715, 0.849903, 0.859717, 0.870227, 0.881516, 0.893686, 0.906859, 0.921188, 0.936870, 0.954157, 0.973389, 0.981725, 0.990483, 0.999708, 1.009452, 1.019779, 1.030763, 1.042500, 1.055107, 1.068735, 1.083586, 1.099933, 1.118171, 1.128177, 1.138907, 1.150500, 1.156676, 1.163142, 1.165816, 1.168543, 1.171326, 1.174167, 1.177067, 1.180028, 1.183050, 1.184585, 1.186134, 1.187698, 1.188486, 1.189277, 1.190071, 1.190231, 1.190390, 1.190550, 1.190709, 1.190869, 1.191189, 1.191511, 1.191675, 1.191848},
{ 0.712955, 0.712958, 0.712987, 0.713105, 0.713200, 0.713233, 0.713258, 0.713278, 0.713293, 0.713305, 0.713340, 0.713361, 0.713380, 0.713418, 0.713456, 0.713494, 0.713571, 0.713647, 0.713725, 0.713802, 0.713880, 0.713957, 0.714036, 0.714232, 0.714430, 0.714832, 0.715240, 0.715655, 0.716507, 0.717387, 0.718298, 0.719238, 0.720210, 0.721214, 0.722250, 0.723319, 0.724421, 0.725557, 0.726727, 0.727932, 0.731100, 0.734496, 0.738126, 0.742000, 0.746125, 0.750512, 0.755170, 0.760113, 0.765353, 0.770907, 0.776791, 0.783024, 0.789629, 0.796632, 0.804059, 0.811946, 0.820329, 0.829251, 0.838765, 0.848928, 0.859813, 0.871502, 0.884097, 0.897721, 0.912529, 0.928713, 0.946524, 0.966294, 0.974847, 0.983819, 0.993255, 1.003202, 1.013720, 1.024877, 1.036759, 1.049467, 1.063130, 1.077905, 1.093991, 1.111627, 1.121100, 1.131030, 1.141374, 1.146648, 1.151921, 1.154007, 1.156065, 1.158081, 1.160039, 1.161916, 1.163689, 1.165332, 1.166096, 1.166819, 1.167499, 1.167821, 1.168132, 1.168431, 1.168489, 1.168547, 1.168605, 1.168662, 1.168718, 1.168829, 1.168939, 1.168993, 1.169046},
{ 0.684948, 0.685604, 0.685862, 0.685990, 0.685994, 0.685991, 0.685989, 0.685988, 0.685989, 0.685992, 0.686012, 0.686034, 0.686056, 0.686100, 0.686143, 0.686186, 0.686273, 0.686360, 0.686447, 0.686534, 0.686621, 0.686708, 0.686796, 0.687015, 0.687234, 0.687676, 0.688120, 0.688569, 0.689479, 0.690407, 0.691357, 0.692330, 0.693329, 0.694355, 0.695410, 0.696495, 0.697611, 0.698761, 0.699944, 0.701163, 0.704371, 0.707818, 0.711519, 0.715485, 0.719728, 0.724260, 0.729094, 0.734242, 0.739721, 0.745546, 0.751735, 0.758310, 0.765293, 0.772710, 0.780590, 0.788969, 0.797884, 0.807381, 0.817511, 0.828336, 0.839927, 0.852369, 0.865765, 0.880239, 0.895943, 0.913069, 0.931859, 0.952631, 0.961585, 0.970954, 0.980778, 0.991098, 1.001965, 1.013435, 1.025573, 1.038451, 1.052150, 1.066743, 1.082273, 1.098644, 1.106992, 1.115198, 1.122744, 1.125910, 1.128265, 1.128859, 1.129181, 1.129177, 1.128807, 1.128080, 1.127083, 1.125883, 1.125214, 1.124508, 1.123769, 1.123389, 1.123003, 1.122610, 1.122531, 1.122452, 1.122372, 1.122293, 1.122213, 1.122052, 1.121891, 1.121810, 1.121729},
{ 0.647990, 0.647822, 0.647676, 0.647600, 0.647625, 0.647639, 0.647649, 0.647657, 0.647663, 0.647668, 0.647693, 0.647720, 0.647748, 0.647803, 0.647858, 0.647913, 0.648023, 0.648133, 0.648243, 0.648352, 0.648461, 0.648570, 0.648679, 0.648950, 0.649221, 0.649759, 0.650295, 0.650828, 0.651891, 0.652954, 0.654019, 0.655093, 0.656179, 0.657281, 0.658401, 0.659544, 0.660713, 0.661909, 0.663135, 0.664394, 0.667697, 0.671244, 0.675059, 0.679160, 0.683567, 0.688296, 0.693363, 0.698785, 0.704579, 0.710763, 0.717358, 0.724385, 0.731869, 0.739837, 0.748320, 0.757354, 0.766979, 0.777242, 0.788196, 0.799903, 0.812438, 0.825885, 0.840350, 0.855954, 0.872851, 0.891225, 0.911311, 0.933401, 0.942880, 0.952766, 0.963093, 0.973893, 0.985204, 0.997064, 1.009510, 1.022572, 1.036258, 1.050524, 1.065176, 1.079598, 1.086192, 1.091620, 1.094299, 1.093339, 1.088849, 1.085066, 1.078826, 1.065901, 1.050163, 1.040584, 1.032493, 1.024579, 1.020564, 1.016511, 1.012427, 1.010375, 1.008316, 1.006250, 1.005835, 1.005421, 1.005006, 1.004591, 1.004175, 1.003343, 1.002510, 1.002092, 1.001675},
{ 0.601499, 0.601533, 0.601690, 0.601760, 0.601739, 0.601742, 0.601751, 0.601762, 0.601772, 0.601782, 0.601819, 0.601857, 0.601896, 0.601972, 0.602048, 0.602124, 0.602276, 0.602428, 0.602578, 0.602729, 0.602878, 0.603028, 0.603176, 0.603546, 0.603912, 0.604635, 0.605347, 0.606049, 0.607423, 0.608764, 0.610080, 0.611377, 0.612660, 0.613937, 0.615213, 0.616494, 0.617784, 0.619089, 0.620413, 0.621761, 0.625254, 0.628967, 0.632939, 0.637206, 0.641797, 0.646736, 0.652047, 0.657752, 0.663873, 0.670430, 0.677447, 0.684949, 0.692962, 0.701516, 0.710643, 0.720381, 0.730772, 0.741865, 0.753714, 0.766384, 0.779950, 0.794500, 0.810136, 0.826982, 0.845188, 0.864931, 0.886430, 0.909952, 0.919996, 0.930438, 0.941299, 0.952604, 0.964373, 0.976623, 0.989359, 1.002561, 1.016156, 1.029960, 1.043523, 1.055685, 1.060352, 1.062953, 1.061689, 1.059014, 1.055963, 1.055545, 1.056757, 1.063454, 1.070851, 1.067367, 1.044575, 1.031316, 1.026188, 1.021151, 1.016017, 1.013408, 1.010774, 1.008118, 1.007584, 1.007049, 1.006513, 1.005977, 1.005439, 1.004360, 1.003277, 1.002734, 1.002190},
{ 0.552601, 0.552551, 0.552370, 0.552435, 0.552479, 0.552476, 0.552478, 0.552488, 0.552501, 0.552516, 0.552570, 0.552623, 0.552677, 0.552784, 0.552891, 0.552998, 0.553210, 0.553422, 0.553633, 0.553842, 0.554051, 0.554260, 0.554467, 0.554981, 0.555491, 0.556493, 0.557475, 0.558437, 0.560304, 0.562100, 0.563833, 0.565508, 0.567135, 0.568721, 0.570275, 0.571804, 0.573315, 0.574817, 0.576317, 0.577820, 0.581634, 0.585594, 0.589768, 0.594213, 0.598977, 0.604097, 0.609608, 0.615541, 0.621924, 0.628785, 0.636151, 0.644050, 0.652513, 0.661572, 0.671264, 0.681627, 0.692705, 0.704551, 0.717219, 0.730778, 0.745301, 0.760879, 0.777614, 0.795629, 0.815066, 0.836096, 0.858919, 0.883769, 0.894334, 0.905279, 0.916621, 0.928371, 0.940536, 0.953109, 0.966066, 0.979341, 0.992803, 1.006179, 1.018929, 1.030017, 1.034492, 1.038274, 1.042328, 1.044870, 1.046874, 1.046988, 1.046512, 1.045620, 1.045029, 1.046766, 1.061806, 1.053066, 1.057122, 1.055044, 1.053047, 1.052074, 1.051228, 1.050375, 1.050254, 1.050082, 1.049951, 1.049799, 1.049650, 1.049361, 1.049070, 1.048925, 1.048781},
{ 0.503269, 0.503552, 0.503720, 0.503584, 0.503679, 0.503691, 0.503690, 0.503695, 0.503710, 0.503730, 0.503802, 0.503875, 0.503945, 0.504088, 0.504229, 0.504371, 0.504653, 0.504935, 0.505215, 0.505494, 0.505772, 0.506049, 0.506326, 0.507011, 0.507690, 0.509029, 0.510341, 0.511626, 0.514118, 0.516507, 0.518795, 0.520990, 0.523096, 0.525123, 0.527077, 0.528969, 0.530808, 0.532603, 0.534363, 0.536098, 0.540379, 0.544679, 0.549101, 0.553732, 0.558643, 0.563892, 0.569530, 0.575600, 0.582139, 0.589184, 0.596769, 0.604928, 0.613696, 0.623109, 0.633206, 0.644029, 0.655627, 0.668051, 0.681360, 0.695621, 0.710913, 0.727322, 0.744951, 0.763918, 0.784362, 0.806439, 0.830331, 0.856234, 0.867202, 0.878534, 0.890237, 0.902314, 0.914759, 0.927554, 0.940658, 0.953997, 0.967447, 0.980821, 0.993913, 1.006815, 1.013500, 1.020569, 1.027727, 1.031190, 1.034877, 1.036509, 1.038140, 1.039449, 1.039885, 1.039184, 1.038655, 1.053066, 1.057122, 1.055044, 1.053047, 1.052073, 1.051227, 1.050376, 1.050254, 1.050082, 1.049951, 1.049799, 1.049650, 1.049361, 1.049070, 1.048925, 1.048781},
{ 0.458276, 0.457751, 0.457881, 0.457945, 0.457989, 0.458029, 0.458033, 0.458033, 0.458048, 0.458074, 0.458160, 0.458253, 0.458338, 0.458513, 0.458688, 0.458863, 0.459211, 0.459558, 0.459904, 0.460249, 0.460594, 0.460937, 0.461279, 0.462130, 0.462975, 0.464645, 0.466288, 0.467904, 0.471049, 0.474076, 0.476984, 0.479771, 0.482441, 0.484996, 0.487443, 0.489788, 0.492042, 0.494213, 0.496312, 0.498350, 0.503245, 0.507984, 0.512709, 0.517542, 0.522584, 0.527919, 0.533617, 0.539736, 0.546328, 0.553439, 0.561112, 0.569387, 0.578306, 0.587909, 0.598239, 0.609341, 0.621266, 0.634068, 0.647807, 0.662552, 0.678378, 0.695373, 0.713636, 0.733279, 0.754431, 0.777236, 0.801852, 0.828439, 0.839660, 0.851226, 0.863141, 0.875405, 0.888012, 0.900947, 0.914190, 0.927720, 0.941538, 0.955735, 0.970632, 0.986868, 0.995669, 1.004963, 1.014867, 1.019991, 1.024971, 1.026899, 1.028851, 1.030914, 1.033082, 1.034847, 1.034943, 1.033977, 1.035495, 1.031710, 1.022650, 1.018972, 1.015358, 1.011667, 1.010917, 1.010162, 1.009404, 1.008642, 1.007876, 1.006334, 1.004777, 1.003992, 1.003203}};
  const double reomegaqnm41[8][107] = {{ 0.812182, 0.808144, 0.804532, 0.801074, 0.799893, 0.799611, 0.799426, 0.799300, 0.799213, 0.799150, 0.799003, 0.798947, 0.798911, 0.798850, 0.798791, 0.798733, 0.798616, 0.798500, 0.798384, 0.798270, 0.798156, 0.798043, 0.797930, 0.797653, 0.797381, 0.796850, 0.796339, 0.795846, 0.794914, 0.794054, 0.793261, 0.792535, 0.791872, 0.791272, 0.790731, 0.790250, 0.789825, 0.789457, 0.789142, 0.788881, 0.788453, 0.788336, 0.788518, 0.788990, 0.789746, 0.790780, 0.792090, 0.793675, 0.795538, 0.797681, 0.800109, 0.802828, 0.805847, 0.809178, 0.812834, 0.816829, 0.821184, 0.825920, 0.831063, 0.836645, 0.842701, 0.849275, 0.856419, 0.864196, 0.872681, 0.881970, 0.892182, 0.903467, 0.908325, 0.913401, 0.918714, 0.924282, 0.930130, 0.936284, 0.942774, 0.949638, 0.956920, 0.964670, 0.972954, 0.981849, 0.986554, 0.991450, 0.996552, 0.999185, 1.001875, 1.002968, 1.004070, 1.005183, 1.006305, 1.007437, 1.008580, 1.009733, 1.010313, 1.010896, 1.011482, 1.011775, 1.012069, 1.012365, 1.012425, 1.012486, 1.012547, 1.012610, 1.012675, 1.012826, 1.013062, 1.013298, 1.013824},
{ 0.787016, 0.786705, 0.786633, 0.786636, 0.786654, 0.786658, 0.786660, 0.786659, 0.786658, 0.786655, 0.786633, 0.786608, 0.786583, 0.786532, 0.786482, 0.786432, 0.786332, 0.786233, 0.786133, 0.786034, 0.785936, 0.785837, 0.785740, 0.785496, 0.785255, 0.784780, 0.784314, 0.783860, 0.782983, 0.782153, 0.781373, 0.780643, 0.779964, 0.779339, 0.778765, 0.778245, 0.777777, 0.777362, 0.776999, 0.776689, 0.776136, 0.775899, 0.775972, 0.776348, 0.777024, 0.777997, 0.779265, 0.780828, 0.782689, 0.784850, 0.787316, 0.790096, 0.793197, 0.796632, 0.800412, 0.804555, 0.809080, 0.814010, 0.819370, 0.825192, 0.831513, 0.838376, 0.845834, 0.853949, 0.862794, 0.872463, 0.883066, 0.894745, 0.899756, 0.904979, 0.910431, 0.916126, 0.922083, 0.928320, 0.934858, 0.941718, 0.948921, 0.956483, 0.964413, 0.972695, 0.976951, 0.981262, 0.985603, 0.987773, 0.989934, 0.990794, 0.991650, 0.992503, 0.993351, 0.994195, 0.995032, 0.995862, 0.996275, 0.996685, 0.997094, 0.997297, 0.997500, 0.997702, 0.997743, 0.997783, 0.997823, 0.997864, 0.997904, 0.997983, 0.998061, 0.998100, 0.998148},
{ 0.762154, 0.762371, 0.762447, 0.762470, 0.762460, 0.762454, 0.762450, 0.762446, 0.762442, 0.762439, 0.762424, 0.762410, 0.762395, 0.762365, 0.762336, 0.762306, 0.762245, 0.762184, 0.762123, 0.762060, 0.761998, 0.761934, 0.761871, 0.761709, 0.761545, 0.761209, 0.760866, 0.760516, 0.759805, 0.759091, 0.758382, 0.757690, 0.757020, 0.756379, 0.755772, 0.755203, 0.754676, 0.754192, 0.753754, 0.753363, 0.752600, 0.752154, 0.752031, 0.752234, 0.752765, 0.753625, 0.754814, 0.756336, 0.758192, 0.760388, 0.762929, 0.765823, 0.769079, 0.772710, 0.776728, 0.781151, 0.785999, 0.791294, 0.797065, 0.803342, 0.810163, 0.817572, 0.825620, 0.834367, 0.843884, 0.854256, 0.865580, 0.877973, 0.883258, 0.888742, 0.894435, 0.900343, 0.906472, 0.912826, 0.919402, 0.926186, 0.933150, 0.940233, 0.947322, 0.954199, 0.957445, 0.960474, 0.963195, 0.964409, 0.965508, 0.965912, 0.966295, 0.966656, 0.966994, 0.967309, 0.967600, 0.967867, 0.967991, 0.968108, 0.968220, 0.968273, 0.968325, 0.968376, 0.968385, 0.968395, 0.968405, 0.968415, 0.968424, 0.968443, 0.968462, 0.968471, 0.968475},
{ 0.727482, 0.727419, 0.727374, 0.727356, 0.727363, 0.727365, 0.727367, 0.727368, 0.727369, 0.727369, 0.727372, 0.727375, 0.727378, 0.727384, 0.727390, 0.727395, 0.727404, 0.727411, 0.727417, 0.727421, 0.727423, 0.727424, 0.727423, 0.727414, 0.727396, 0.727333, 0.727237, 0.727112, 0.726782, 0.726368, 0.725889, 0.725364, 0.724810, 0.724241, 0.723670, 0.723106, 0.722558, 0.722034, 0.721540, 0.721082, 0.720114, 0.719440, 0.719091, 0.719085, 0.719436, 0.720153, 0.721242, 0.722709, 0.724560, 0.726801, 0.729440, 0.732484, 0.735946, 0.739837, 0.744172, 0.748968, 0.754247, 0.760031, 0.766349, 0.773233, 0.780719, 0.788851, 0.797679, 0.807257, 0.817652, 0.828933, 0.841176, 0.854453, 0.860066, 0.865854, 0.871814, 0.877939, 0.884216, 0.890619, 0.897108, 0.903610, 0.910008, 0.916106, 0.921568, 0.925840, 0.927280, 0.928103, 0.928217, 0.927993, 0.927582, 0.927367, 0.927123, 0.926851, 0.926552, 0.926227, 0.925876, 0.925500, 0.925304, 0.925101, 0.924893, 0.924786, 0.924679, 0.924570, 0.924548, 0.924526, 0.924504, 0.924482, 0.924459, 0.924415, 0.924370, 0.924348, 0.924328},
{ 0.683045, 0.683066, 0.683115, 0.683138, 0.683141, 0.683147, 0.683153, 0.683160, 0.683166, 0.683173, 0.683202, 0.683232, 0.683261, 0.683319, 0.683377, 0.683434, 0.683546, 0.683656, 0.683763, 0.683867, 0.683969, 0.684068, 0.684165, 0.684396, 0.684612, 0.684997, 0.685323, 0.685593, 0.685975, 0.686168, 0.686198, 0.686092, 0.685876, 0.685572, 0.685204, 0.684790, 0.684348, 0.683891, 0.683432, 0.682983, 0.681954, 0.681143, 0.680622, 0.680439, 0.680626, 0.681205, 0.682194, 0.683604, 0.685446, 0.687731, 0.690469, 0.693671, 0.697348, 0.701516, 0.706189, 0.711386, 0.717128, 0.723441, 0.730350, 0.737890, 0.746095, 0.755006, 0.764669, 0.775134, 0.786452, 0.798673, 0.811839, 0.825957, 0.831859, 0.837893, 0.844039, 0.850270, 0.856541, 0.862785, 0.868895, 0.874706, 0.879948, 0.884181, 0.886682, 0.886368, 0.884820, 0.882330, 0.879045, 0.877163, 0.875153, 0.874319, 0.873469, 0.872606, 0.871730, 0.870843, 0.869945, 0.869039, 0.868583, 0.868125, 0.867665, 0.867434, 0.867203, 0.866972, 0.866925, 0.866879, 0.866833, 0.866786, 0.866740, 0.866647, 0.866554, 0.866507, 0.866460},
{ 0.632260, 0.632262, 0.632219, 0.632253, 0.632284, 0.632294, 0.632304, 0.632316, 0.632329, 0.632343, 0.632405, 0.632467, 0.632529, 0.632653, 0.632776, 0.632898, 0.633140, 0.633379, 0.633615, 0.633849, 0.634079, 0.634306, 0.634531, 0.635078, 0.635607, 0.636608, 0.637532, 0.638380, 0.639849, 0.641028, 0.641936, 0.642598, 0.643043, 0.643301, 0.643404, 0.643379, 0.643254, 0.643052, 0.642794, 0.642500, 0.641703, 0.640966, 0.640422, 0.640165, 0.640257, 0.640743, 0.641654, 0.643015, 0.644845, 0.647160, 0.649976, 0.653306, 0.657167, 0.661572, 0.666542, 0.672094, 0.678251, 0.685038, 0.692482, 0.700614, 0.709468, 0.719080, 0.729489, 0.740734, 0.752848, 0.765853, 0.779737, 0.794417, 0.800467, 0.806579, 0.812714, 0.818811, 0.824779, 0.830485, 0.835720, 0.840156, 0.843255, 0.844101, 0.841167, 0.832816, 0.827120, 0.821047, 0.814772, 0.811623, 0.808488, 0.807240, 0.805995, 0.804756, 0.803521, 0.802292, 0.801069, 0.799852, 0.799245, 0.798641, 0.798037, 0.797736, 0.797436, 0.797136, 0.797076, 0.797016, 0.796956, 0.796896, 0.796836, 0.796716, 0.796596, 0.796538, 0.796478},
{ 0.577671, 0.577747, 0.577832, 0.577822, 0.577876, 0.577897, 0.577913, 0.577930, 0.577948, 0.577969, 0.578066, 0.578162, 0.578258, 0.578450, 0.578641, 0.578831, 0.579210, 0.579586, 0.579960, 0.580331, 0.580699, 0.581065, 0.581428, 0.582323, 0.583200, 0.584900, 0.586522, 0.588064, 0.590896, 0.593380, 0.595512, 0.597299, 0.598760, 0.599921, 0.600814, 0.601472, 0.601932, 0.602227, 0.602387, 0.602441, 0.602270, 0.601899, 0.601548, 0.601371, 0.601477, 0.601940, 0.602816, 0.604145, 0.605960, 0.608286, 0.611144, 0.614555, 0.618537, 0.623109, 0.628291, 0.634105, 0.640572, 0.647718, 0.655568, 0.664152, 0.673498, 0.683637, 0.694598, 0.706401, 0.719056, 0.732542, 0.746777, 0.761555, 0.767525, 0.773458, 0.779281, 0.784886, 0.790116, 0.794732, 0.798360, 0.800381, 0.799703, 0.794215, 0.779821, 0.761639, 0.752939, 0.745281, 0.737595, 0.733911, 0.730347, 0.728943, 0.727558, 0.726183, 0.724821, 0.723472, 0.722136, 0.720811, 0.720153, 0.719498, 0.718845, 0.718520, 0.718196, 0.717872, 0.717808, 0.717743, 0.717679, 0.717614, 0.717549, 0.717420, 0.717292, 0.717224, 0.717163},
{ 0.523121, 0.522966, 0.522983, 0.523079, 0.523114, 0.523151, 0.523176, 0.523196, 0.523218, 0.523244, 0.523371, 0.523498, 0.523623, 0.523873, 0.524124, 0.524374, 0.524872, 0.525369, 0.525864, 0.526357, 0.526849, 0.527339, 0.527826, 0.529037, 0.530235, 0.532589, 0.534883, 0.537110, 0.541345, 0.545250, 0.548791, 0.551942, 0.554695, 0.557052, 0.559032, 0.560663, 0.561980, 0.563023, 0.563831, 0.564442, 0.565326, 0.565642, 0.565721, 0.565798, 0.566039, 0.566564, 0.567456, 0.568779, 0.570581, 0.572899, 0.575766, 0.579207, 0.583247, 0.587909, 0.593214, 0.599185, 0.605845, 0.613217, 0.621327, 0.630197, 0.639852, 0.650311, 0.661589, 0.673683, 0.686569, 0.700168, 0.714307, 0.728612, 0.734225, 0.739661, 0.744800, 0.749470, 0.753412, 0.756227, 0.757257, 0.755324, 0.747973, 0.728096, 0.650476, 0.611311, 0.592090, 0.573322, 0.554778, 0.545580, 0.536421, 0.532767, 0.529117, 0.525471, 0.521828, 0.534982, 0.528313, 0.521510, 0.518051, 0.514548, 0.510997, 0.509203, 0.507394, 0.505570, 0.505203, 0.504836, 0.504468, 0.504099, 0.503730, 0.502990, 0.502247, 0.501874, 0.501501}};
  const double reomegaqnm40[8][107] = {{ 0.523121, 0.522966, 0.522983, 0.523079, 0.523114, 0.523151, 0.523176, 0.523196, 0.523218, 0.523244, 0.523371, 0.523498, 0.523623, 0.523873, 0.524124, 0.524374, 0.524872, 0.525369, 0.525864, 0.526357, 0.526849, 0.527339, 0.527826, 0.529037, 0.530235, 0.532589, 0.534883, 0.537110, 0.541345, 0.545250, 0.548791, 0.551942, 0.554695, 0.557052, 0.559032, 0.560663, 0.561980, 0.563023, 0.563831, 0.564442, 0.565326, 0.565642, 0.565721, 0.565798, 0.566039, 0.566564, 0.567456, 0.568779, 0.570581, 0.572899, 0.575766, 0.579207, 0.583247, 0.587909, 0.809327, 0.809774, 0.810523, 0.811579, 0.812950, 0.814646, 0.816680, 0.819068, 0.821832, 0.824994, 0.828585, 0.832642, 0.837209, 0.842340, 0.844566, 0.846898, 0.849342, 0.851904, 0.854591, 0.857411, 0.860371, 0.863482, 0.866753, 0.870197, 0.873826, 0.877655, 0.879649, 0.881700, 0.883809, 0.884886, 0.885979, 0.886420, 0.886864, 0.887310, 0.887760, 0.888211, 0.888666, 0.889123, 0.889352, 0.889582, 0.889813, 0.889929, 0.890048, 0.890186, 0.890221, 0.890261, 0.890311, 0.890374, 0.890462, 0.890801, 0.891872, 0.893423, 0.897340},
{ 0.523121, 0.522966, 0.522983, 0.523079, 0.523114, 0.523151, 0.523176, 0.523196, 0.523218, 0.523244, 0.523371, 0.523498, 0.523623, 0.523873, 0.524124, 0.524374, 0.524872, 0.525369, 0.525864, 0.526357, 0.526849, 0.527339, 0.527826, 0.529037, 0.530235, 0.532589, 0.534883, 0.537110, 0.541345, 0.545250, 0.548791, 0.551942, 0.554695, 0.557052, 0.559032, 0.560663, 0.561980, 0.563023, 0.563831, 0.564442, 0.565326, 0.565642, 0.565721, 0.565798, 0.566039, 0.566564, 0.567456, 0.568779, 0.570581, 0.572899, 0.575766, 0.579207, 0.583247, 0.587909, 0.796792, 0.797273, 0.798078, 0.799213, 0.800686, 0.802506, 0.804687, 0.807245, 0.810200, 0.813574, 0.817397, 0.821700, 0.826523, 0.831911, 0.834235, 0.836662, 0.839194, 0.841836, 0.844591, 0.847462, 0.850452, 0.853565, 0.856799, 0.860156, 0.863630, 0.867213, 0.869041, 0.870889, 0.872754, 0.873692, 0.874632, 0.875008, 0.875385, 0.875762, 0.876139, 0.876516, 0.876893, 0.877270, 0.877458, 0.877647, 0.877835, 0.877929, 0.878024, 0.878117, 0.878136, 0.878154, 0.878172, 0.878190, 0.878206, 0.878236, 0.878267, 0.878303, 0.878429},
{ 0.523121, 0.522966, 0.522983, 0.523079, 0.523114, 0.523151, 0.523176, 0.523196, 0.523218, 0.523244, 0.523371, 0.523498, 0.523623, 0.523873, 0.524124, 0.524374, 0.524872, 0.525369, 0.525864, 0.526357, 0.526849, 0.527339, 0.527826, 0.529037, 0.530235, 0.532589, 0.534883, 0.537110, 0.541345, 0.545250, 0.548791, 0.551942, 0.554695, 0.557052, 0.559032, 0.560663, 0.561980, 0.563023, 0.563831, 0.564442, 0.565326, 0.565642, 0.565721, 0.565798, 0.566039, 0.566564, 0.567456, 0.568779, 0.570581, 0.572899, 0.575766, 0.579207, 0.583247, 0.587909, 0.772891, 0.773436, 0.774349, 0.775634, 0.777299, 0.779355, 0.781814, 0.784691, 0.788004, 0.791775, 0.796025, 0.800780, 0.806065, 0.811904, 0.814397, 0.816982, 0.819656, 0.822419, 0.825266, 0.828192, 0.831188, 0.834241, 0.837333, 0.840435, 0.843512, 0.846511, 0.847960, 0.849364, 0.850709, 0.851356, 0.851984, 0.852229, 0.852471, 0.852710, 0.852944, 0.853175, 0.853402, 0.853625, 0.853735, 0.853843, 0.853951, 0.854005, 0.854058, 0.854111, 0.854121, 0.854132, 0.854142, 0.854153, 0.854164, 0.854187, 0.854205, 0.854201, 0.854163},
{ 0.523121, 0.522966, 0.522983, 0.523079, 0.523114, 0.523151, 0.523176, 0.523196, 0.523218, 0.523244, 0.523371, 0.523498, 0.523623, 0.523873, 0.524124, 0.524374, 0.524872, 0.525369, 0.525864, 0.526357, 0.526849, 0.527339, 0.527826, 0.529037, 0.530235, 0.532589, 0.534883, 0.537110, 0.541345, 0.545250, 0.548791, 0.551942, 0.554695, 0.557052, 0.559032, 0.560663, 0.561980, 0.563023, 0.563831, 0.564442, 0.565326, 0.565642, 0.565721, 0.565798, 0.566039, 0.566564, 0.567456, 0.568779, 0.570581, 0.572899, 0.575766, 0.579207, 0.583247, 0.587909, 0.740047, 0.740678, 0.741735, 0.743220, 0.745143, 0.747512, 0.750339, 0.753636, 0.757418, 0.761700, 0.766495, 0.771814, 0.777658, 0.784009, 0.786680, 0.789419, 0.792217, 0.795061, 0.797936, 0.800820, 0.803685, 0.806492, 0.809190, 0.811715, 0.813983, 0.815893, 0.816680, 0.817335, 0.817845, 0.818042, 0.818198, 0.818249, 0.818293, 0.818330, 0.818361, 0.818384, 0.818400, 0.818409, 0.818411, 0.818412, 0.818410, 0.818409, 0.818407, 0.818404, 0.818404, 0.818403, 0.818403, 0.818402, 0.818401, 0.818399, 0.818400, 0.818408, 0.818424},
{ 0.523121, 0.522966, 0.522983, 0.523079, 0.523114, 0.523151, 0.523176, 0.523196, 0.523218, 0.523244, 0.523371, 0.523498, 0.523623, 0.523873, 0.524124, 0.524374, 0.524872, 0.525369, 0.525864, 0.526357, 0.526849, 0.527339, 0.527826, 0.529037, 0.530235, 0.532589, 0.534883, 0.537110, 0.541345, 0.545250, 0.548791, 0.551942, 0.554695, 0.557052, 0.559032, 0.560663, 0.561980, 0.563023, 0.563831, 0.564442, 0.565326, 0.565642, 0.565721, 0.565798, 0.566039, 0.566564, 0.567456, 0.568779, 0.570581, 0.572899, 0.575766, 0.579207, 0.583247, 0.587909, 0.701757, 0.702483, 0.703695, 0.705399, 0.707599, 0.710305, 0.713523, 0.717264, 0.721533, 0.726336, 0.731670, 0.737521, 0.743851, 0.750576, 0.753344, 0.756135, 0.758927, 0.761694, 0.764400, 0.766998, 0.769430, 0.771620, 0.773474, 0.774879, 0.775711, 0.775850, 0.775630, 0.775206, 0.774575, 0.774183, 0.773739, 0.773548, 0.773349, 0.773141, 0.772926, 0.772704, 0.772473, 0.772235, 0.772113, 0.771990, 0.771864, 0.771801, 0.771737, 0.771673, 0.771660, 0.771647, 0.771634, 0.771621, 0.771608, 0.771583, 0.771556, 0.771534, 0.771516},
{ 0.523121, 0.522966, 0.522983, 0.523079, 0.523114, 0.523151, 0.523176, 0.523196, 0.523218, 0.523244, 0.523371, 0.523498, 0.523623, 0.523873, 0.524124, 0.524374, 0.524872, 0.525369, 0.525864, 0.526357, 0.526849, 0.527339, 0.527826, 0.529037, 0.530235, 0.532589, 0.534883, 0.537110, 0.541345, 0.545250, 0.548791, 0.551942, 0.554695, 0.557052, 0.559032, 0.560663, 0.561980, 0.563023, 0.563831, 0.564442, 0.565326, 0.565642, 0.565721, 0.565798, 0.566039, 0.566564, 0.567456, 0.568779, 0.570581, 0.572899, 0.575766, 0.579207, 0.583247, 0.587909, 0.661843, 0.662654, 0.664009, 0.665910, 0.668360, 0.671365, 0.674926, 0.679045, 0.683719, 0.688936, 0.694669, 0.700864, 0.707423, 0.714165, 0.716846, 0.719476, 0.722013, 0.724408, 0.726595, 0.728492, 0.729996, 0.730981, 0.731307, 0.730830, 0.729436, 0.727079, 0.725549, 0.723799, 0.721846, 0.720799, 0.719709, 0.719262, 0.718808, 0.718348, 0.717882, 0.717410, 0.716932, 0.716449, 0.716206, 0.715961, 0.715715, 0.715591, 0.715468, 0.715343, 0.715318, 0.715294, 0.715269, 0.715244, 0.715220, 0.715168, 0.715118, 0.715104, 0.715080},
{ 0.523121, 0.522966, 0.522983, 0.523079, 0.523114, 0.523151, 0.523176, 0.523196, 0.523218, 0.523244, 0.523371, 0.523498, 0.523623, 0.523873, 0.524124, 0.524374, 0.524872, 0.525369, 0.525864, 0.526357, 0.526849, 0.527339, 0.527826, 0.529037, 0.530235, 0.532589, 0.534883, 0.537110, 0.541345, 0.545250, 0.548791, 0.551942, 0.554695, 0.557052, 0.559032, 0.560663, 0.561980, 0.563023, 0.563831, 0.564442, 0.565326, 0.565642, 0.565721, 0.565798, 0.566039, 0.566564, 0.567456, 0.568779, 0.570581, 0.572899, 0.575766, 0.579207, 0.583247, 0.587909, 0.623402, 0.624280, 0.625745, 0.627796, 0.630435, 0.633659, 0.637463, 0.641838, 0.646764, 0.652205, 0.658097, 0.664333, 0.670727, 0.676954, 0.679282, 0.681442, 0.683368, 0.684972, 0.686146, 0.686757, 0.686646, 0.685638, 0.683574, 0.680369, 0.676056, 0.670788, 0.667863, 0.664780, 0.661570, 0.659924, 0.658256, 0.657583, 0.656907, 0.656228, 0.655546, 0.654862, 0.654175, 0.653487, 0.653141, 0.652796, 0.652450, 0.652276, 0.652103, 0.651929, 0.651894, 0.651860, 0.651826, 0.651791, 0.651756, 0.651684, 0.651624, 0.651570, 0.651532},
{ 0.523121, 0.522966, 0.522983, 0.523079, 0.523114, 0.523151, 0.523176, 0.523196, 0.523218, 0.523244, 0.523371, 0.523498, 0.523623, 0.523873, 0.524124, 0.524374, 0.524872, 0.525369, 0.525864, 0.526357, 0.526849, 0.527339, 0.527826, 0.529037, 0.530235, 0.532589, 0.534883, 0.537110, 0.541345, 0.545250, 0.548791, 0.551942, 0.554695, 0.557052, 0.559032, 0.560663, 0.561980, 0.563023, 0.563831, 0.564442, 0.565326, 0.565642, 0.565721, 0.565798, 0.566039, 0.566564, 0.567456, 0.568779, 0.570581, 0.572899, 0.575766, 0.579207, 0.583247, 0.587909, 0.588217, 0.589140, 0.590677, 0.592826, 0.595581, 0.598932, 0.602865, 0.607353, 0.612354, 0.617799, 0.623576, 0.629502, 0.635269, 0.640353, 0.642007, 0.643325, 0.644200, 0.644492, 0.644035, 0.642627, 0.640056, 0.636153, 0.630889, 0.624446, 0.617123, 0.609231, 0.605155, 0.601029, 0.596874, 0.594791, 0.592707, 0.591874, 0.591041, 0.590209, 0.589376, 0.588544, 0.587713, 0.586883, 0.586468, 0.586053, 0.585638, 0.585431, 0.585224, 0.585016, 0.584975, 0.584935, 0.584894, 0.584851, 0.584807, 0.584733, 0.584628, 0.584607, 0.584599}};

  const double imomegaqnm22[8][107] = {{-0.078461,-0.080101,-0.081647,-0.084004,-0.085495,-0.086013,-0.086422,-0.086745,-0.087003,-0.087210,-0.087780,-0.087988,-0.088065,-0.088096,-0.088088,-0.088076,-0.088061,-0.088057,-0.088059,-0.088063,-0.088068,-0.088073,-0.088078,-0.088091,-0.088104,-0.088130,-0.088156,-0.088181,-0.088231,-0.088281,-0.088329,-0.088376,-0.088423,-0.088468,-0.088512,-0.088555,-0.088598,-0.088639,-0.088679,-0.088717,-0.088809,-0.088892,-0.088966,-0.089032,-0.089087,-0.089131,-0.089164,-0.089185,-0.089191,-0.089183,-0.089157,-0.089114,-0.089050,-0.088962,-0.088849,-0.088706,-0.088528,-0.088311,-0.088048,-0.087729,-0.087345,-0.086882,-0.086321,-0.085639,-0.084802,-0.083765,-0.082462,-0.080793,-0.079991,-0.079093,-0.078082,-0.076936,-0.075630,-0.074126,-0.072378,-0.070321,-0.067864,-0.064869,-0.061119,-0.056231,-0.053149,-0.049434,-0.044790,-0.041959,-0.038630,-0.037116,-0.035468,-0.033659,-0.031652,-0.029390,-0.026791,-0.023710,-0.021911,-0.019866,-0.017474,-0.016092,-0.014534,-0.012727,-0.012326,-0.011908,-0.011471,-0.011013,-0.010531,-0.009478,-0.008267,-0.007577,-0.006807},
{-0.284478,-0.282567,-0.281162,-0.279245,-0.278072,-0.277670,-0.277358,-0.277118,-0.276936,-0.276800,-0.276536,-0.276565,-0.276647,-0.276753,-0.276778,-0.276775,-0.276764,-0.276768,-0.276776,-0.276784,-0.276791,-0.276797,-0.276803,-0.276818,-0.276834,-0.276864,-0.276893,-0.276922,-0.276977,-0.277028,-0.277076,-0.277120,-0.277160,-0.277197,-0.277230,-0.277259,-0.277283,-0.277304,-0.277321,-0.277333,-0.277344,-0.277326,-0.277278,-0.277196,-0.277080,-0.276926,-0.276732,-0.276495,-0.276212,-0.275877,-0.275486,-0.275033,-0.274512,-0.273915,-0.273232,-0.272452,-0.271562,-0.270546,-0.269383,-0.268049,-0.266512,-0.264733,-0.262661,-0.260225,-0.257331,-0.253847,-0.249580,-0.244238,-0.241705,-0.238888,-0.235736,-0.232183,-0.228149,-0.223524,-0.218165,-0.211876,-0.204376,-0.195252,-0.183847,-0.169019,-0.159687,-0.148458,-0.134453,-0.125927,-0.115917,-0.111365,-0.106415,-0.100985,-0.094960,-0.088175,-0.080377,-0.071134,-0.065738,-0.059605,-0.052426,-0.048280,-0.043605,-0.038184,-0.036979,-0.035724,-0.034413,-0.033038,-0.031592,-0.028433,-0.024800,-0.022730,-0.020422},
{-0.504534,-0.501693,-0.501238,-0.503711,-0.506007,-0.506490,-0.506633,-0.506559,-0.506365,-0.506120,-0.505173,-0.505120,-0.505381,-0.505519,-0.505371,-0.505318,-0.505354,-0.505311,-0.505262,-0.505236,-0.505210,-0.505177,-0.505141,-0.505059,-0.504978,-0.504812,-0.504645,-0.504476,-0.504135,-0.503786,-0.503432,-0.503070,-0.502702,-0.502326,-0.501944,-0.501554,-0.501156,-0.500751,-0.500338,-0.499917,-0.498828,-0.497686,-0.496488,-0.495230,-0.493910,-0.492523,-0.491065,-0.489531,-0.487913,-0.486206,-0.484399,-0.482484,-0.480448,-0.478277,-0.475955,-0.473463,-0.470778,-0.467872,-0.464713,-0.461260,-0.457463,-0.453259,-0.448569,-0.443287,-0.437269,-0.430315,-0.422131,-0.412262,-0.407689,-0.402664,-0.397103,-0.390894,-0.383895,-0.375919,-0.366715,-0.355940,-0.343110,-0.327518,-0.308058,-0.282827,-0.266998,-0.248008,-0.224410,-0.210086,-0.193307,-0.185690,-0.177413,-0.168340,-0.158283,-0.146966,-0.133965,-0.118563,-0.109572,-0.099352,-0.087389,-0.080478,-0.072685,-0.063646,-0.061638,-0.059545,-0.057359,-0.055068,-0.052656,-0.047390,-0.041333,-0.037883,-0.034035},
{-0.777682,-0.778170,-0.774907,-0.771079,-0.772456,-0.773571,-0.774249,-0.774423,-0.774236,-0.773865,-0.772678,-0.773376,-0.773489,-0.772990,-0.773229,-0.773121,-0.773005,-0.772942,-0.772818,-0.772776,-0.772680,-0.772579,-0.772506,-0.772294,-0.772084,-0.771654,-0.771224,-0.770787,-0.769900,-0.768995,-0.768072,-0.767129,-0.766168,-0.765187,-0.764186,-0.763165,-0.762123,-0.761061,-0.759977,-0.758872,-0.756015,-0.753020,-0.749885,-0.746606,-0.743182,-0.739611,-0.735891,-0.732017,-0.727985,-0.723788,-0.719419,-0.714865,-0.710114,-0.705148,-0.699946,-0.694481,-0.688721,-0.682628,-0.676155,-0.669243,-0.661823,-0.653808,-0.645091,-0.635536,-0.624969,-0.613154,-0.599764,-0.584301,-0.577364,-0.569885,-0.561756,-0.552827,-0.542888,-0.531648,-0.518699,-0.503480,-0.485224,-0.462864,-0.434823,-0.398473,-0.375741,-0.348572,-0.314974,-0.294663,-0.270945,-0.260200,-0.248542,-0.235779,-0.221649,-0.205769,-0.187547,-0.165980,-0.153396,-0.139094,-0.122354,-0.112682,-0.101773,-0.089118,-0.086305,-0.083375,-0.080314,-0.077105,-0.073726,-0.066352,-0.057868,-0.053037,-0.047648},
{-1.044309,-1.046603,-1.050655,-1.048278,-1.047193,-1.048092,-1.048935,-1.049247,-1.049061,-1.048626,-1.048121,-1.048623,-1.048064,-1.048425,-1.048083,-1.048244,-1.048026,-1.047992,-1.047878,-1.047811,-1.047683,-1.047624,-1.047519,-1.047294,-1.047043,-1.046571,-1.046088,-1.045602,-1.044600,-1.043569,-1.042506,-1.041410,-1.040279,-1.039113,-1.037909,-1.036667,-1.035386,-1.034063,-1.032697,-1.031287,-1.027561,-1.023531,-1.019176,-1.014480,-1.009427,-1.004003,-0.998200,-0.992012,-0.985437,-0.978476,-0.971133,-0.963412,-0.955317,-0.946845,-0.937987,-0.928725,-0.919026,-0.908845,-0.898118,-0.886764,-0.874681,-0.861738,-0.847776,-0.832595,-0.815947,-0.797527,-0.776957,-0.753800,-0.743710,-0.733106,-0.721967,-0.710268,-0.697962,-0.684902,-0.670601,-0.653750,-0.632042,-0.603295,-0.565870,-0.517102,-0.486783,-0.450771,-0.406552,-0.379969,-0.349048,-0.335080,-0.319949,-0.303412,-0.285135,-0.264631,-0.241142,-0.213382,-0.197201,-0.178818,-0.157307,-0.144878,-0.130859,-0.114594,-0.110978,-0.107211,-0.103275,-0.099148,-0.094804,-0.085320,-0.074409,-0.068195,-0.061264},
{-1.321876,-1.316952,-1.316680,-1.319467,-1.317311,-1.317699,-1.318525,-1.318939,-1.318766,-1.318308,-1.318440,-1.318071,-1.318219,-1.318003,-1.318199,-1.317998,-1.317922,-1.317906,-1.317816,-1.317701,-1.317641,-1.317538,-1.317449,-1.317225,-1.317008,-1.316565,-1.316117,-1.315660,-1.314736,-1.313781,-1.312801,-1.311792,-1.310749,-1.309671,-1.308555,-1.307400,-1.306200,-1.304955,-1.303660,-1.302314,-1.298700,-1.294695,-1.290248,-1.285302,-1.279801,-1.273681,-1.266873,-1.259306,-1.250911,-1.241631,-1.231433,-1.220321,-1.208348,-1.195608,-1.182210,-1.168248,-1.153773,-1.138779,-1.123199,-1.106915,-1.089762,-1.071525,-1.051940,-1.030673,-1.007299,-0.981265,-0.951825,-0.917950,-0.902851,-0.886706,-0.869389,-0.850781,-0.830803,-0.809534,-0.787531,-0.766572,-0.750871,-0.748723,-0.741744,-0.732374,-0.728044,-0.723765,-0.719565,-0.717492,-0.715438,-0.714621,-0.713807,-0.712996,-0.712188,-0.711383,-0.710581,-0.709781,-0.709382,-0.708984,-0.708587,-0.708388,-0.708190,-0.707992,-0.707952,-0.707911,-0.707871,-0.707832,-0.707796,-0.707720,-0.707620,-0.707592,-0.707598},
{-1.582041,-1.586347,-1.583178,-1.585108,-1.583555,-1.583429,-1.584183,-1.584679,-1.584484,-1.583983,-1.584406,-1.583828,-1.584168,-1.584097,-1.583865,-1.583835,-1.583730,-1.583671,-1.583575,-1.583419,-1.583359,-1.583229,-1.583150,-1.582888,-1.582639,-1.582125,-1.581635,-1.581142,-1.580158,-1.579185,-1.578210,-1.577232,-1.576245,-1.575242,-1.574220,-1.573172,-1.572096,-1.570987,-1.569838,-1.568647,-1.565452,-1.561889,-1.557887,-1.553369,-1.548253,-1.542442,-1.535819,-1.528233,-1.519487,-1.509316,-1.497379,-1.483278,-1.466729,-1.447911,-1.427683,-1.407115,-1.386816,-1.366830,-1.346890,-1.326617,-1.305614,-1.283473,-1.259763,-1.233995,-1.205580,-1.173764,-1.137526,-1.095408,-1.076453,-1.056023,-1.033875,-1.009698,-0.983084,-0.953463,-0.919986,-0.881204,-0.833927,-0.770337,-0.707988,-0.641106,-0.601525,-0.555418,-0.499613,-0.466355,-0.427874,-0.410550,-0.391822,-0.371395,-0.348864,-0.323638,-0.294800,-0.260789,-0.240992,-0.218519,-0.192238,-0.177057,-0.159933,-0.140065,-0.135647,-0.131044,-0.126235,-0.121193,-0.115884,-0.104293,-0.090955,-0.083359,-0.074885},
{-1.849571,-1.849682,-1.851341,-1.850066,-1.849828,-1.849286,-1.850035,-1.850597,-1.850266,-1.849681,-1.850075,-1.850080,-1.849639,-1.849608,-1.849634,-1.849658,-1.849410,-1.849226,-1.849059,-1.848840,-1.848638,-1.848508,-1.848291,-1.847881,-1.847459,-1.846618,-1.845835,-1.845061,-1.843607,-1.842250,-1.840966,-1.839752,-1.838590,-1.837469,-1.836377,-1.835304,-1.834239,-1.833173,-1.832097,-1.831005,-1.828147,-1.825021,-1.821526,-1.817570,-1.813060,-1.807903,-1.801987,-1.795176,-1.787281,-1.778014,-1.766864,-1.752776,-1.733234,-1.703841,-1.668818,-1.638934,-1.614234,-1.592062,-1.570644,-1.548919,-1.526188,-1.501903,-1.475550,-1.446567,-1.414276,-1.377806,-1.335978,-1.287119,-1.265086,-1.241327,-1.215578,-1.187510,-1.156708,-1.122625,-1.084532,-1.041406,-0.991742,-0.933178,-0.861906,-0.773444,-0.721837,-0.663505,-0.594647,-0.554166,-0.507666,-0.486825,-0.464348,-0.439888,-0.412969,-0.382898,-0.348601,-0.308246,-0.284798,-0.258210,-0.227147,-0.209213,-0.188989,-0.165524,-0.160306,-0.154870,-0.149189,-0.143232,-0.136961,-0.123266,-0.107504,-0.113694,-0.102135}};
  const double imomegaqnm21[8][107] = {{-0.074312,-0.077266,-0.079182,-0.081281,-0.082265,-0.082556,-0.082768,-0.082926,-0.083045,-0.083135,-0.083360,-0.083430,-0.083456,-0.083471,-0.083481,-0.083492,-0.083519,-0.083551,-0.083583,-0.083616,-0.083648,-0.083680,-0.083713,-0.083792,-0.083870,-0.084024,-0.084174,-0.084319,-0.084599,-0.084865,-0.085117,-0.085356,-0.085583,-0.085799,-0.086003,-0.086198,-0.086383,-0.086558,-0.086725,-0.086884,-0.087247,-0.087566,-0.087846,-0.088091,-0.088302,-0.088484,-0.088637,-0.088763,-0.088862,-0.088935,-0.088983,-0.089004,-0.088997,-0.088962,-0.088897,-0.088798,-0.088664,-0.088489,-0.088268,-0.087995,-0.087662,-0.087257,-0.086767,-0.086173,-0.085450,-0.084564,-0.083466,-0.082085,-0.081430,-0.080703,-0.079893,-0.078983,-0.077955,-0.076785,-0.075439,-0.073874,-0.072027,-0.069804,-0.067061,-0.063549,-0.061372,-0.058791,-0.055644,-0.053776,-0.051643,-0.050698,-0.049691,-0.048614,-0.047457,-0.046208,-0.044856,-0.043388,-0.042607,-0.041794,-0.040950,-0.040517,-0.040078,-0.039632,-0.039542,-0.039452,-0.039362,-0.039271,-0.039180,-0.038997,-0.038811,-0.038714,-0.038609},
{-0.258785,-0.258526,-0.258215,-0.257703,-0.257385,-0.257284,-0.257210,-0.257158,-0.257123,-0.257101,-0.257089,-0.257133,-0.257178,-0.257248,-0.257304,-0.257357,-0.257465,-0.257574,-0.257683,-0.257791,-0.257898,-0.258005,-0.258112,-0.258376,-0.258637,-0.259149,-0.259649,-0.260137,-0.261076,-0.261967,-0.262812,-0.263613,-0.264372,-0.265089,-0.265768,-0.266410,-0.267016,-0.267589,-0.268130,-0.268640,-0.269792,-0.270782,-0.271629,-0.272346,-0.272945,-0.273434,-0.273821,-0.274112,-0.274309,-0.274415,-0.274430,-0.274354,-0.274183,-0.273915,-0.273543,-0.273058,-0.272452,-0.271711,-0.270819,-0.269754,-0.268489,-0.266992,-0.265218,-0.263108,-0.260587,-0.257547,-0.253838,-0.249238,-0.247078,-0.244692,-0.242045,-0.239089,-0.235766,-0.231999,-0.227686,-0.222686,-0.216800,-0.209731,-0.201006,-0.189803,-0.182820,-0.174484,-0.164205,-0.158029,-0.150891,-0.147700,-0.144280,-0.140606,-0.136657,-0.132444,-0.128112,-0.124511,-0.123925,-0.123321,-0.121670,-0.121162,-0.120732,-0.120140,-0.120048,-0.119951,-0.119844,-0.119734,-0.119631,-0.119429,-0.119227,-0.119127,-0.119027},
{-0.451082,-0.450126,-0.449946,-0.450393,-0.450836,-0.450954,-0.451016,-0.451037,-0.451032,-0.451015,-0.450927,-0.450953,-0.451028,-0.451151,-0.451242,-0.451336,-0.451538,-0.451736,-0.451932,-0.452129,-0.452326,-0.452521,-0.452716,-0.453201,-0.453683,-0.454634,-0.455570,-0.456489,-0.458275,-0.459989,-0.461626,-0.463185,-0.464665,-0.466067,-0.467390,-0.468636,-0.469808,-0.470906,-0.471935,-0.472897,-0.475022,-0.476783,-0.478216,-0.479352,-0.480220,-0.480840,-0.481231,-0.481407,-0.481377,-0.481149,-0.480726,-0.480109,-0.479294,-0.478277,-0.477047,-0.475592,-0.473893,-0.471929,-0.469669,-0.467076,-0.464104,-0.460691,-0.456761,-0.452210,-0.446903,-0.440653,-0.433196,-0.424144,-0.419951,-0.415358,-0.410300,-0.404693,-0.398434,-0.391383,-0.383355,-0.374094,-0.363227,-0.350188,-0.334044,-0.313093,-0.299808,-0.283583,-0.262719,-0.249472,-0.233119,-0.225306,-0.216483,-0.206365,-0.194563,-0.180538,-0.163450,-0.141483,-0.127310,-0.111596,-0.095488,-0.086832,-0.077419,-0.066857,-0.064554,-0.062171,-0.059699,-0.057125,-0.054435,-0.048632,-0.042066,-0.038376,-0.034303},
{-0.670736,-0.672092,-0.671812,-0.670663,-0.670549,-0.670730,-0.670927,-0.671078,-0.671164,-0.671189,-0.671021,-0.671075,-0.671219,-0.671310,-0.671427,-0.671572,-0.671807,-0.672062,-0.672309,-0.672557,-0.672808,-0.673057,-0.673305,-0.673929,-0.674552,-0.675799,-0.677043,-0.678283,-0.680743,-0.683166,-0.685536,-0.687840,-0.690067,-0.692205,-0.694247,-0.696187,-0.698020,-0.699744,-0.701359,-0.702865,-0.706165,-0.708833,-0.710915,-0.712460,-0.713510,-0.714105,-0.714277,-0.714053,-0.713452,-0.712490,-0.711176,-0.709516,-0.707509,-0.705148,-0.702423,-0.699317,-0.695803,-0.691851,-0.687416,-0.682445,-0.676867,-0.670592,-0.663504,-0.655451,-0.646227,-0.635554,-0.623036,-0.608090,-0.601244,-0.593791,-0.585630,-0.576638,-0.566651,-0.555455,-0.542760,-0.528156,-0.511038,-0.490465,-0.464847,-0.431189,-0.409563,-0.382936,-0.348824,-0.327730,-0.302947,-0.291759,-0.279694,-0.266590,-0.252176,-0.235931,-0.216835,-0.192946,-0.178198,-0.160681,-0.138951,-0.125667,-0.110911,-0.095128,-0.091773,-0.088322,-0.084758,-0.081061,-0.077210,-0.068928,-0.059584,-0.054342,-0.048559},
{-0.910592,-0.910063,-0.911380,-0.911832,-0.911012,-0.911003,-0.911199,-0.911439,-0.911606,-0.911660,-0.911352,-0.911585,-0.911648,-0.911704,-0.911854,-0.911933,-0.912192,-0.912425,-0.912663,-0.912897,-0.913141,-0.913376,-0.913616,-0.914220,-0.914830,-0.916061,-0.917309,-0.918574,-0.921148,-0.923769,-0.926420,-0.929083,-0.931738,-0.934365,-0.936941,-0.939446,-0.941862,-0.944173,-0.946367,-0.948436,-0.953025,-0.956761,-0.959656,-0.961747,-0.963080,-0.963701,-0.963651,-0.962967,-0.961680,-0.959815,-0.957389,-0.954417,-0.950902,-0.946845,-0.942236,-0.937057,-0.931283,-0.924873,-0.917775,-0.909918,-0.901208,-0.891524,-0.880707,-0.868545,-0.854753,-0.838939,-0.820547,-0.798756,-0.788822,-0.778034,-0.766250,-0.753290,-0.738922,-0.722836,-0.704612,-0.683652,-0.659084,-0.629562,-0.592913,-0.545420,-0.515651,-0.480063,-0.436051,-0.409334,-0.377864,-0.363486,-0.347803,-0.330563,-0.311454,-0.290066,-0.265802,-0.237412,-0.220683,-0.201065,-0.176930,-0.162373,-0.145371,-0.124789,-0.120176,-0.115422,-0.110541,-0.105527,-0.100358,-0.089395,-0.077178,-0.070358,-0.062849},
{-1.163133,-1.162051,-1.161016,-1.162363,-1.161800,-1.161553,-1.161645,-1.161909,-1.162120,-1.162171,-1.161849,-1.162143,-1.162003,-1.162223,-1.162278,-1.162400,-1.162612,-1.162792,-1.163001,-1.163209,-1.163415,-1.163621,-1.163836,-1.164362,-1.164893,-1.165982,-1.167096,-1.168236,-1.170596,-1.173058,-1.175616,-1.178263,-1.180984,-1.183762,-1.186579,-1.189408,-1.192222,-1.194992,-1.197689,-1.200289,-1.206244,-1.211270,-1.215276,-1.218241,-1.220174,-1.221102,-1.221052,-1.220056,-1.218138,-1.215325,-1.211642,-1.207111,-1.201759,-1.195608,-1.188677,-1.180978,-1.172507,-1.163245,-1.153145,-1.142130,-1.130089,-1.116863,-1.102237,-1.085922,-1.067530,-1.046522,-1.022136,-0.993243,-0.980058,-0.965724,-0.950047,-0.932779,-0.913601,-0.892095,-0.867699,-0.839644,-0.806856,-0.767797,-0.720146,-0.659944,-0.622848,-0.578732,-0.524237,-0.491317,-0.452927,-0.435542,-0.416661,-0.395943,-0.372912,-0.346898,-0.316949,-0.281670,-0.261342,-0.238436,-0.211307,-0.195088,-0.176156,-0.153337,-0.148132,-0.142654,-0.136869,-0.130747,-0.124273,-0.110319,-0.094939,-0.086464,-0.077185},
{-1.414276,-1.416039,-1.415596,-1.415363,-1.415597,-1.415238,-1.415194,-1.415427,-1.415648,-1.415679,-1.415505,-1.415544,-1.415631,-1.415658,-1.415791,-1.415907,-1.416079,-1.416241,-1.416430,-1.416629,-1.416797,-1.416999,-1.417174,-1.417648,-1.418130,-1.419109,-1.420112,-1.421142,-1.423282,-1.425540,-1.427915,-1.430412,-1.433028,-1.435759,-1.438595,-1.441525,-1.444529,-1.447581,-1.450649,-1.453695,-1.460994,-1.467516,-1.473003,-1.477325,-1.480421,-1.482261,-1.482821,-1.482069,-1.479966,-1.476459,-1.471497,-1.465052,-1.457153,-1.447911,-1.437516,-1.426190,-1.414119,-1.401396,-1.388008,-1.373845,-1.358718,-1.342365,-1.324453,-1.304557,-1.282128,-1.256434,-1.226447,-1.190653,-1.174220,-1.156285,-1.136589,-1.114807,-1.090529,-1.063232,-1.032247,-0.996712,-0.955487,-0.906959,-0.848541,-0.775494,-0.730792,-0.678016,-0.613349,-0.574379,-0.528885,-0.508284,-0.485946,-0.461517,-0.434493,-0.404108,-0.369135,-0.327547,-0.303278,-0.275875,-0.244209,-0.225953,-0.204982,-0.179727,-0.173965,-0.167908,-0.161519,-0.154752,-0.147552,-0.131555,-0.113035,-0.102763,-0.091610},
{-1.669503,-1.668395,-1.669442,-1.668664,-1.669275,-1.668971,-1.668820,-1.668995,-1.669204,-1.669212,-1.669190,-1.669059,-1.669253,-1.669326,-1.669380,-1.669414,-1.669602,-1.669773,-1.669951,-1.670142,-1.670305,-1.670479,-1.670661,-1.671105,-1.671552,-1.672460,-1.673398,-1.674357,-1.676355,-1.678461,-1.680685,-1.683035,-1.685516,-1.688135,-1.690893,-1.693787,-1.696812,-1.699961,-1.703216,-1.706546,-1.714937,-1.722933,-1.730080,-1.736116,-1.740904,-1.744368,-1.746449,-1.747073,-1.746118,-1.743369,-1.738448,-1.730726,-1.719332,-1.703841,-1.685684,-1.667396,-1.650218,-1.634024,-1.618223,-1.602186,-1.585342,-1.567163,-1.547117,-1.524616,-1.498944,-1.469166,-1.433974,-1.391423,-1.371712,-1.350090,-1.326232,-1.299741,-1.270135,-1.236831,-1.199118,-1.156089,-1.106489,-1.048413,-0.978780,-0.892317,-0.839828,-0.778168,-0.702974,-0.657884,-0.605411,-0.581667,-0.555906,-0.527711,-0.496517,-0.461510,-0.421398,-0.373867,-0.346042,-0.314367,-0.277467,-0.256370,-0.232725,-0.204918,-0.198597,-0.191947,-0.184925,-0.177485,-0.169568,-0.151985,-0.131247,-0.119312,-0.106197}};
  const double imomegaqnm20[8][107] = {{-1.669503,-1.668395,-1.669442,-1.668664,-1.669275,-1.668971,-1.668820,-1.668995,-1.669204,-1.669212,-1.669190,-1.669059,-1.669253,-1.669326,-1.669380,-1.669414,-1.669602,-1.669773,-1.669951,-1.670142,-1.670305,-1.670479,-1.670661,-1.671105,-1.671552,-1.672460,-1.673398,-1.674357,-1.676355,-1.678461,-1.680685,-1.683035,-1.685516,-1.688135,-1.690893,-1.693787,-1.696812,-1.699961,-1.703216,-1.706546,-1.714937,-1.722933,-1.730080,-1.736116,-1.740904,-1.744368,-1.746449,-1.747073,-1.746118,-1.743369,-1.738448,-1.730726,-1.719332,-1.703841,-0.088946,-0.088898,-0.088817,-0.088700,-0.088547,-0.088353,-0.088115,-0.087827,-0.087481,-0.087069,-0.086579,-0.085995,-0.085295,-0.084453,-0.084067,-0.083648,-0.083194,-0.082698,-0.082156,-0.081562,-0.080908,-0.080185,-0.079381,-0.078483,-0.077473,-0.076330,-0.075701,-0.075028,-0.074306,-0.073926,-0.073532,-0.073371,-0.073207,-0.073041,-0.072872,-0.072701,-0.072527,-0.072351,-0.072263,-0.072174,-0.072087,-0.072043,-0.071996,-0.071930,-0.071911,-0.071886,-0.071856,-0.071815,-0.071760,-0.071567,-0.071095,-0.070577,-0.069561},
{-1.669503,-1.668395,-1.669442,-1.668664,-1.669275,-1.668971,-1.668820,-1.668995,-1.669204,-1.669212,-1.669190,-1.669059,-1.669253,-1.669326,-1.669380,-1.669414,-1.669602,-1.669773,-1.669951,-1.670142,-1.670305,-1.670479,-1.670661,-1.671105,-1.671552,-1.672460,-1.673398,-1.674357,-1.676355,-1.678461,-1.680685,-1.683035,-1.685516,-1.688135,-1.690893,-1.693787,-1.696812,-1.699961,-1.703216,-1.706546,-1.714937,-1.722933,-1.730080,-1.736116,-1.740904,-1.744368,-1.746449,-1.747073,-1.746118,-1.743369,-1.738448,-1.730726,-1.719332,-1.703841,-0.273860,-0.273694,-0.273414,-0.273014,-0.272487,-0.271821,-0.271003,-0.270014,-0.268831,-0.267421,-0.265747,-0.263754,-0.261372,-0.258507,-0.257198,-0.255780,-0.254240,-0.252564,-0.250736,-0.248735,-0.246539,-0.244119,-0.241444,-0.238480,-0.235186,-0.231528,-0.229555,-0.227483,-0.225314,-0.224195,-0.223053,-0.222591,-0.222125,-0.221655,-0.221182,-0.220707,-0.220228,-0.219746,-0.219504,-0.219261,-0.219017,-0.218894,-0.218769,-0.218646,-0.218624,-0.218602,-0.218583,-0.218567,-0.218555,-0.218550,-0.218576,-0.218586,-0.218541},
{-1.669503,-1.668395,-1.669442,-1.668664,-1.669275,-1.668971,-1.668820,-1.668995,-1.669204,-1.669212,-1.669190,-1.669059,-1.669253,-1.669326,-1.669380,-1.669414,-1.669602,-1.669773,-1.669951,-1.670142,-1.670305,-1.670479,-1.670661,-1.671105,-1.671552,-1.672460,-1.673398,-1.674357,-1.676355,-1.678461,-1.680685,-1.683035,-1.685516,-1.688135,-1.690893,-1.693787,-1.696812,-1.699961,-1.703216,-1.706546,-1.714937,-1.722933,-1.730080,-1.736116,-1.740904,-1.744368,-1.746449,-1.747073,-1.746118,-1.743369,-1.738448,-1.730726,-1.719332,-1.703841,-0.478161,-0.477809,-0.477217,-0.476373,-0.475263,-0.473863,-0.472148,-0.470080,-0.467612,-0.464686,-0.461221,-0.457117,-0.452237,-0.446396,-0.443736,-0.440863,-0.437753,-0.434379,-0.430711,-0.426714,-0.422353,-0.417586,-0.412380,-0.406711,-0.400596,-0.394120,-0.390798,-0.387457,-0.384120,-0.382462,-0.380814,-0.380158,-0.379504,-0.378853,-0.378204,-0.377558,-0.376915,-0.376275,-0.375956,-0.375638,-0.375319,-0.375160,-0.375004,-0.374856,-0.374826,-0.374795,-0.374762,-0.374724,-0.374680,-0.374571,-0.374466,-0.374476,-0.374628},
{-1.669503,-1.668395,-1.669442,-1.668664,-1.669275,-1.668971,-1.668820,-1.668995,-1.669204,-1.669212,-1.669190,-1.669059,-1.669253,-1.669326,-1.669380,-1.669414,-1.669602,-1.669773,-1.669951,-1.670142,-1.670305,-1.670479,-1.670661,-1.671105,-1.671552,-1.672460,-1.673398,-1.674357,-1.676355,-1.678461,-1.680685,-1.683035,-1.685516,-1.688135,-1.690893,-1.693787,-1.696812,-1.699961,-1.703216,-1.706546,-1.714937,-1.722933,-1.730080,-1.736116,-1.740904,-1.744368,-1.746449,-1.747073,-1.746118,-1.743369,-1.738448,-1.730726,-1.719332,-1.703841,-0.704945,-0.704331,-0.703296,-0.701824,-0.699889,-0.697457,-0.694481,-0.690905,-0.686650,-0.681622,-0.675691,-0.668694,-0.660408,-0.650537,-0.646058,-0.641230,-0.636017,-0.630380,-0.624277,-0.617664,-0.610506,-0.602788,-0.594547,-0.585942,-0.577339,-0.569239,-0.565445,-0.561815,-0.558363,-0.556704,-0.555087,-0.554453,-0.553825,-0.553204,-0.552590,-0.551982,-0.551381,-0.550785,-0.550490,-0.550199,-0.549903,-0.549755,-0.549623,-0.549478,-0.549438,-0.549395,-0.549349,-0.549308,-0.549280,-0.549303,-0.549380,-0.549273,-0.548898},
{-1.669503,-1.668395,-1.669442,-1.668664,-1.669275,-1.668971,-1.668820,-1.668995,-1.669204,-1.669212,-1.669190,-1.669059,-1.669253,-1.669326,-1.669380,-1.669414,-1.669602,-1.669773,-1.669951,-1.670142,-1.670305,-1.670479,-1.670661,-1.671105,-1.671552,-1.672460,-1.673398,-1.674357,-1.676355,-1.678461,-1.680685,-1.683035,-1.685516,-1.688135,-1.690893,-1.693787,-1.696812,-1.699961,-1.703216,-1.706546,-1.714937,-1.722933,-1.730080,-1.736116,-1.740904,-1.744368,-1.746449,-1.747073,-1.746118,-1.743369,-1.738448,-1.730726,-1.719332,-1.703841,-0.946541,-0.945626,-0.944084,-0.941891,-0.939010,-0.935394,-0.930976,-0.925674,-0.919380,-0.911953,-0.903214,-0.892924,-0.880767,-0.866316,-0.859772,-0.852729,-0.845140,-0.836957,-0.828139,-0.818665,-0.808571,-0.798035,-0.787576,-0.778347,-0.771240,-0.764674,-0.761819,-0.759101,-0.756567,-0.755353,-0.754178,-0.753717,-0.753262,-0.752812,-0.752366,-0.751927,-0.751490,-0.751064,-0.750845,-0.750633,-0.750426,-0.750296,-0.750238,-0.750094,-0.750035,-0.749986,-0.749967,-0.749993,-0.750061,-0.750126,-0.749650,-0.749564,-0.750138},
{-1.669503,-1.668395,-1.669442,-1.668664,-1.669275,-1.668971,-1.668820,-1.668995,-1.669204,-1.669212,-1.669190,-1.669059,-1.669253,-1.669326,-1.669380,-1.669414,-1.669602,-1.669773,-1.669951,-1.670142,-1.670305,-1.670479,-1.670661,-1.671105,-1.671552,-1.672460,-1.673398,-1.674357,-1.676355,-1.678461,-1.680685,-1.683035,-1.685516,-1.688135,-1.690893,-1.693787,-1.696812,-1.699961,-1.703216,-1.706546,-1.714937,-1.722933,-1.730080,-1.736116,-1.740904,-1.744368,-1.746449,-1.747073,-1.746118,-1.743369,-1.738448,-1.730726,-1.719332,-1.703841,-1.195198,-1.193960,-1.191877,-1.188916,-1.185032,-1.180162,-1.174222,-1.167105,-1.158673,-1.148745,-1.137087,-1.123387,-1.107232,-1.088067,-1.079403,-1.070093,-1.060089,-1.049356,-1.037903,-1.025862,-1.013708,-1.002960,-0.997335,-0.993889,-0.987227,-0.983917,-0.982151,-0.980231,-0.978641,-0.977871,-0.977090,-0.976786,-0.976474,-0.976178,-0.975870,-0.975582,-0.975274,-0.974976,-0.974851,-0.974680,-0.974600,-0.974444,-0.974424,-0.974293,-0.974188,-0.974146,-0.974213,-0.974371,-0.974505,-0.974110,-0.974112,-0.974787,-0.974253},
{-1.669503,-1.668395,-1.669442,-1.668664,-1.669275,-1.668971,-1.668820,-1.668995,-1.669204,-1.669212,-1.669190,-1.669059,-1.669253,-1.669326,-1.669380,-1.669414,-1.669602,-1.669773,-1.669951,-1.670142,-1.670305,-1.670479,-1.670661,-1.671105,-1.671552,-1.672460,-1.673398,-1.674357,-1.676355,-1.678461,-1.680685,-1.683035,-1.685516,-1.688135,-1.690893,-1.693787,-1.696812,-1.699961,-1.703216,-1.706546,-1.714937,-1.722933,-1.730080,-1.736116,-1.740904,-1.744368,-1.746449,-1.747073,-1.746118,-1.743369,-1.738448,-1.730726,-1.719332,-1.703841,-1.447378,-1.445774,-1.443079,-1.439259,-1.434262,-1.428019,-1.420435,-1.411388,-1.400713,-1.388197,-1.373555,-1.356407,-1.336243,-1.312390,-1.301635,-1.290113,-1.277800,-1.264747,-1.251235,-1.238360,-1.230978,-1.236823,-1.228953,-1.223151,-1.221395,-1.219601,-1.218691,-1.216721,-1.215773,-1.215081,-1.214311,-1.214157,-1.213848,-1.213637,-1.213402,-1.213208,-1.212985,-1.212747,-1.212620,-1.212586,-1.212371,-1.212445,-1.212213,-1.212199,-1.212018,-1.212001,-1.212205,-1.212475,-1.212482,-1.211727,-1.212758,-1.211922,-1.211527},
{-1.669503,-1.668395,-1.669442,-1.668664,-1.669275,-1.668971,-1.668820,-1.668995,-1.669204,-1.669212,-1.669190,-1.669059,-1.669253,-1.669326,-1.669380,-1.669414,-1.669602,-1.669773,-1.669951,-1.670142,-1.670305,-1.670479,-1.670661,-1.671105,-1.671552,-1.672460,-1.673398,-1.674357,-1.676355,-1.678461,-1.680685,-1.683035,-1.685516,-1.688135,-1.690893,-1.693787,-1.696812,-1.699961,-1.703216,-1.706546,-1.714937,-1.722933,-1.730080,-1.736116,-1.740904,-1.744368,-1.746449,-1.747073,-1.746118,-1.743369,-1.738448,-1.730726,-1.719332,-1.703841,-1.703116,-1.700942,-1.697329,-1.692278,-1.685776,-1.677781,-1.668216,-1.656957,-1.643827,-1.628580,-1.610879,-1.590273,-1.566155,-1.537745,-1.524995,-1.511417,-1.497110,-1.482566,-1.470178,-1.480076,-1.483391,-1.467377,-1.472722,-1.464347,-1.468574,-1.463725,-1.464201,-1.462140,-1.458072,-1.458542,-1.458162,-1.458344,-1.458079,-1.457758,-1.457217,-1.457199,-1.457032,-1.456856,-1.456708,-1.456496,-1.456405,-1.456612,-1.456203,-1.456343,-1.456084,-1.456111,-1.456452,-1.456702,-1.456399,-1.456163,-1.456141,-1.455774,-1.457166}};
  const double imomegaqnm33[8][107] = {{-0.068612,-0.073535,-0.077498,-0.082919,-0.086037,-0.087057,-0.087838,-0.088440,-0.088909,-0.089277,-0.090269,-0.090639,-0.090797,-0.090910,-0.090941,-0.090952,-0.090960,-0.090966,-0.090973,-0.090980,-0.090987,-0.090994,-0.091001,-0.091020,-0.091038,-0.091074,-0.091110,-0.091146,-0.091217,-0.091287,-0.091356,-0.091424,-0.091492,-0.091558,-0.091624,-0.091688,-0.091751,-0.091814,-0.091875,-0.091934,-0.092078,-0.092214,-0.092339,-0.092455,-0.092558,-0.092649,-0.092726,-0.092787,-0.092830,-0.092854,-0.092856,-0.092834,-0.092784,-0.092703,-0.092587,-0.092430,-0.092228,-0.091973,-0.091656,-0.091267,-0.090793,-0.090218,-0.089521,-0.088676,-0.087647,-0.086385,-0.084821,-0.082856,-0.081925,-0.080892,-0.079741,-0.078451,-0.076995,-0.075339,-0.073436,-0.071223,-0.068611,-0.065463,-0.061565,-0.056537,-0.053388,-0.049609,-0.044905,-0.042044,-0.038688,-0.037163,-0.035505,-0.033687,-0.031671,-0.029403,-0.026797,-0.023711,-0.021911,-0.019865,-0.017473,-0.016091,-0.014533,-0.012727,-0.012325,-0.011907,-0.011470,-0.011012,-0.010530,-0.009477,-0.008266,-0.007576,-0.006807},
{-0.277496,-0.278402,-0.278809,-0.278955,-0.278824,-0.278742,-0.278664,-0.278594,-0.278534,-0.278482,-0.278334,-0.278289,-0.278281,-0.278294,-0.278307,-0.278318,-0.278336,-0.278354,-0.278373,-0.278391,-0.278409,-0.278427,-0.278446,-0.278491,-0.278537,-0.278627,-0.278716,-0.278805,-0.278980,-0.279153,-0.279323,-0.279489,-0.279653,-0.279813,-0.279970,-0.280123,-0.280273,-0.280419,-0.280561,-0.280699,-0.281026,-0.281324,-0.281591,-0.281825,-0.282020,-0.282175,-0.282285,-0.282345,-0.282350,-0.282293,-0.282168,-0.281967,-0.281681,-0.281298,-0.280807,-0.280191,-0.279434,-0.278515,-0.277407,-0.276080,-0.274494,-0.272602,-0.270340,-0.267630,-0.264363,-0.260394,-0.255518,-0.249435,-0.246568,-0.243397,-0.239871,-0.235928,-0.231489,-0.226451,-0.220675,-0.213971,-0.206071,-0.196570,-0.184822,-0.169692,-0.160224,-0.148867,-0.134739,-0.126150,-0.116076,-0.111499,-0.106524,-0.101069,-0.095019,-0.088212,-0.080393,-0.071135,-0.065734,-0.059597,-0.052418,-0.048273,-0.043599,-0.038180,-0.036976,-0.035721,-0.034410,-0.033036,-0.031590,-0.028432,-0.024799,-0.022729,-0.020420},
{-0.486993,-0.484741,-0.483427,-0.482389,-0.482301,-0.482372,-0.482460,-0.482542,-0.482611,-0.482664,-0.482744,-0.482718,-0.482702,-0.482709,-0.482722,-0.482729,-0.482743,-0.482759,-0.482774,-0.482789,-0.482804,-0.482819,-0.482834,-0.482871,-0.482907,-0.482980,-0.483051,-0.483121,-0.483256,-0.483386,-0.483511,-0.483630,-0.483742,-0.483849,-0.483949,-0.484043,-0.484130,-0.484210,-0.484283,-0.484348,-0.484477,-0.484555,-0.484575,-0.484535,-0.484428,-0.484247,-0.483987,-0.483638,-0.483192,-0.482640,-0.481968,-0.481164,-0.480211,-0.479093,-0.477786,-0.476267,-0.474506,-0.472466,-0.470105,-0.467369,-0.464195,-0.460500,-0.456181,-0.451103,-0.445090,-0.437899,-0.429190,-0.418467,-0.413455,-0.407937,-0.401829,-0.395030,-0.387407,-0.378790,-0.368950,-0.357573,-0.344212,-0.328195,-0.308447,-0.283082,-0.267235,-0.248247,-0.224647,-0.210309,-0.193501,-0.185866,-0.177568,-0.168469,-0.158382,-0.147032,-0.133998,-0.118564,-0.109560,-0.099331,-0.087366,-0.080456,-0.072667,-0.063634,-0.061627,-0.059535,-0.057351,-0.055061,-0.052650,-0.047387,-0.041331,-0.037882,-0.034034},
{-0.709974,-0.711601,-0.713499,-0.714385,-0.713785,-0.713546,-0.713418,-0.713383,-0.713411,-0.713468,-0.713655,-0.713595,-0.713563,-0.713584,-0.713571,-0.713561,-0.713550,-0.713535,-0.713522,-0.713507,-0.713493,-0.713479,-0.713465,-0.713428,-0.713392,-0.713317,-0.713241,-0.713162,-0.712999,-0.712828,-0.712649,-0.712461,-0.712264,-0.712058,-0.711843,-0.711618,-0.711383,-0.711138,-0.710882,-0.710616,-0.709899,-0.709105,-0.708229,-0.707264,-0.706201,-0.705033,-0.703748,-0.702336,-0.700785,-0.699079,-0.697202,-0.695135,-0.692855,-0.690337,-0.687550,-0.684459,-0.681020,-0.677183,-0.672887,-0.668056,-0.662599,-0.656401,-0.649315,-0.641153,-0.631664,-0.620511,-0.607221,-0.591101,-0.583641,-0.575473,-0.566484,-0.556529,-0.545428,-0.532943,-0.518759,-0.502439,-0.483360,-0.460589,-0.432625,-0.396831,-0.374520,-0.347819,-0.314676,-0.294558,-0.270986,-0.260282,-0.248651,-0.235902,-0.221769,-0.205870,-0.187614,-0.166001,-0.153393,-0.139070,-0.122317,-0.112642,-0.101735,-0.089089,-0.086278,-0.083351,-0.080292,-0.077086,-0.073710,-0.066342,-0.057864,-0.053035,-0.047648},
{-0.972477,-0.969229,-0.967660,-0.969029,-0.969442,-0.969155,-0.968886,-0.968746,-0.968738,-0.968815,-0.969029,-0.968878,-0.968919,-0.968877,-0.968857,-0.968828,-0.968768,-0.968706,-0.968649,-0.968589,-0.968529,-0.968470,-0.968410,-0.968260,-0.968110,-0.967806,-0.967500,-0.967190,-0.966561,-0.965920,-0.965265,-0.964597,-0.963916,-0.963220,-0.962509,-0.961784,-0.961043,-0.960287,-0.959514,-0.958725,-0.956678,-0.954518,-0.952237,-0.949827,-0.947277,-0.944577,-0.941715,-0.938677,-0.935446,-0.932005,-0.928331,-0.924401,-0.920185,-0.915649,-0.910755,-0.905456,-0.899695,-0.893406,-0.886510,-0.878908,-0.870479,-0.861073,-0.850501,-0.838516,-0.824795,-0.808901,-0.790225,-0.767883,-0.757639,-0.746483,-0.734270,-0.720819,-0.705900,-0.689212,-0.670355,-0.648776,-0.623682,-0.593881,-0.557455,-0.511016,-0.482143,-0.447640,-0.404868,-0.378929,-0.348555,-0.334768,-0.319791,-0.303378,-0.285189,-0.264731,-0.241246,-0.213447,-0.197233,-0.178815,-0.157271,-0.144831,-0.130807,-0.114545,-0.110932,-0.107167,-0.103234,-0.099111,-0.094772,-0.085297,-0.074397,-0.068188,-0.061261},
{-1.234880,-1.238501,-1.237567,-1.236381,-1.237353,-1.237185,-1.236888,-1.236715,-1.236725,-1.236844,-1.236889,-1.236862,-1.236868,-1.236807,-1.236739,-1.236707,-1.236605,-1.236507,-1.236413,-1.236315,-1.236219,-1.236121,-1.236024,-1.235780,-1.235534,-1.235039,-1.234539,-1.234033,-1.233006,-1.231956,-1.230884,-1.229790,-1.228672,-1.227531,-1.226366,-1.225176,-1.223961,-1.222721,-1.221454,-1.220161,-1.216809,-1.213278,-1.209559,-1.205642,-1.201516,-1.197169,-1.192585,-1.187749,-1.182642,-1.177243,-1.171527,-1.165464,-1.159019,-1.152151,-1.144813,-1.136947,-1.128483,-1.119340,-1.109418,-1.098594,-1.086720,-1.073606,-1.059017,-1.042646,-1.024094,-1.002820,-0.978074,-0.948772,-0.935434,-0.920971,-0.905209,-0.887929,-0.868853,-0.847622,-0.823753,-0.796583,-0.765161,-0.728048,-0.682917,-0.625639,-0.590118,-0.547732,-0.495250,-0.463448,-0.426230,-0.409344,-0.391005,-0.370913,-0.348652,-0.323623,-0.294897,-0.260904,-0.241081,-0.218564,-0.192229,-0.177023,-0.159881,-0.140004,-0.135587,-0.130985,-0.126178,-0.121138,-0.115834,-0.104253,-0.090930,-0.083341,-0.074875},
{-1.508123,-1.507143,-1.508863,-1.507649,-1.508328,-1.508308,-1.508054,-1.507900,-1.507943,-1.508075,-1.507959,-1.508051,-1.507942,-1.507906,-1.507868,-1.507795,-1.507680,-1.507563,-1.507448,-1.507330,-1.507211,-1.507093,-1.506973,-1.506674,-1.506374,-1.505766,-1.505151,-1.504528,-1.503256,-1.501952,-1.500614,-1.499242,-1.497835,-1.496393,-1.494914,-1.493399,-1.491846,-1.490254,-1.488623,-1.486953,-1.482598,-1.477977,-1.473078,-1.467888,-1.462391,-1.456574,-1.450419,-1.443908,-1.437021,-1.429733,-1.422014,-1.413833,-1.405148,-1.395912,-1.386070,-1.375552,-1.364277,-1.352147,-1.339041,-1.324812,-1.309279,-1.292214,-1.273331,-1.252260,-1.228516,-1.201450,-1.170160,-1.133351,-1.116676,-1.098649,-1.079064,-1.057665,-1.034127,-1.008030,-0.978819,-0.945725,-0.907651,-0.862934,-0.808869,-0.740600,-0.698384,-0.648072,-0.585828,-0.548130,-0.504030,-0.484027,-0.462309,-0.438520,-0.412171,-0.382553,-0.348572,-0.308374,-0.284937,-0.258319,-0.227191,-0.209218,-0.188957,-0.165464,-0.160243,-0.154805,-0.149123,-0.143167,-0.136898,-0.123211,-0.107464,-0.098495,-0.102103},
{-1.779474,-1.778289,-1.778279,-1.778446,-1.778602,-1.778683,-1.778496,-1.778376,-1.778435,-1.778543,-1.778425,-1.778411,-1.778415,-1.778342,-1.778265,-1.778203,-1.778078,-1.777952,-1.777827,-1.777701,-1.777572,-1.777445,-1.777316,-1.776993,-1.776668,-1.776009,-1.775340,-1.774660,-1.773268,-1.771834,-1.770354,-1.768830,-1.767260,-1.765643,-1.763977,-1.762263,-1.760499,-1.758683,-1.756815,-1.754894,-1.749851,-1.744448,-1.738667,-1.732489,-1.725894,-1.718862,-1.711372,-1.703401,-1.694926,-1.685918,-1.676347,-1.666175,-1.655359,-1.643845,-1.631569,-1.618456,-1.604409,-1.589315,-1.573032,-1.555388,-1.536168,-1.515101,-1.491847,-1.465968,-1.436889,-1.403838,-1.365752,-1.321105,-1.300933,-1.279162,-1.255553,-1.229810,-1.201558,-1.170316,-1.135450,-1.096094,-1.051014,-0.998352,-0.935078,-0.855690,-0.806777,-0.748562,-0.676569,-0.632966,-0.581960,-0.558828,-0.533715,-0.506213,-0.475757,-0.441533,-0.402280,-0.355861,-0.328804,-0.298080,-0.262156,-0.241415,-0.218035,-0.190926,-0.184902,-0.178626,-0.172070,-0.165197,-0.157962,-0.142169,-0.123998,-0.113649,-0.115718}};
  const double imomegaqnm32[8][107] = {{-0.069981,-0.075891,-0.079961,-0.084549,-0.086675,-0.087288,-0.087731,-0.088055,-0.088298,-0.088483,-0.088949,-0.089111,-0.089177,-0.089224,-0.089240,-0.089249,-0.089264,-0.089280,-0.089296,-0.089311,-0.089327,-0.089343,-0.089359,-0.089399,-0.089438,-0.089516,-0.089592,-0.089667,-0.089815,-0.089958,-0.090097,-0.090231,-0.090362,-0.090489,-0.090611,-0.090731,-0.090846,-0.090958,-0.091066,-0.091171,-0.091418,-0.091644,-0.091851,-0.092037,-0.092203,-0.092349,-0.092475,-0.092580,-0.092663,-0.092724,-0.092760,-0.092770,-0.092752,-0.092703,-0.092620,-0.092500,-0.092336,-0.092124,-0.091857,-0.091525,-0.091117,-0.090620,-0.090016,-0.089280,-0.088383,-0.087280,-0.085912,-0.084190,-0.083373,-0.082467,-0.081457,-0.080325,-0.079046,-0.077590,-0.075918,-0.073972,-0.071674,-0.068905,-0.065473,-0.061044,-0.058268,-0.054933,-0.050781,-0.048255,-0.045293,-0.043948,-0.042487,-0.040887,-0.039116,-0.037130,-0.034861,-0.032200,-0.030668,-0.028952,-0.026993,-0.025895,-0.024695,-0.023373,-0.023091,-0.022804,-0.022510,-0.022210,-0.021904,-0.021273,-0.020618,-0.020284,-0.019946},
{-0.269958,-0.271103,-0.271569,-0.271797,-0.271769,-0.271739,-0.271709,-0.271682,-0.271660,-0.271642,-0.271600,-0.271599,-0.271610,-0.271637,-0.271663,-0.271689,-0.271738,-0.271788,-0.271837,-0.271887,-0.271936,-0.271985,-0.272034,-0.272155,-0.272276,-0.272514,-0.272749,-0.272979,-0.273430,-0.273867,-0.274290,-0.274699,-0.275094,-0.275477,-0.275847,-0.276204,-0.276548,-0.276881,-0.277202,-0.277511,-0.278233,-0.278886,-0.279471,-0.279990,-0.280444,-0.280831,-0.281152,-0.281405,-0.281588,-0.281697,-0.281728,-0.281677,-0.281536,-0.281298,-0.280953,-0.280489,-0.279891,-0.279142,-0.278220,-0.277098,-0.275743,-0.274112,-0.272151,-0.269788,-0.266929,-0.263444,-0.259149,-0.253778,-0.251243,-0.248435,-0.245311,-0.241815,-0.237876,-0.233400,-0.228266,-0.222302,-0.215266,-0.206793,-0.196296,-0.182737,-0.174223,-0.163971,-0.151146,-0.143303,-0.134051,-0.129825,-0.125215,-0.120137,-0.114476,-0.108062,-0.100631,-0.091721,-0.086456,-0.080395,-0.073168,-0.068900,-0.063968,-0.058005,-0.056627,-0.055164,-0.053600,-0.051916,-0.050086,-0.045842,-0.040492,-0.037243,-0.033496},
{-0.467568,-0.466534,-0.465963,-0.465576,-0.465576,-0.465611,-0.465647,-0.465678,-0.465703,-0.465721,-0.465757,-0.465769,-0.465786,-0.465830,-0.465874,-0.465917,-0.466002,-0.466087,-0.466172,-0.466257,-0.466342,-0.466426,-0.466510,-0.466719,-0.466926,-0.467335,-0.467737,-0.468132,-0.468902,-0.469646,-0.470362,-0.471052,-0.471716,-0.472354,-0.472967,-0.473555,-0.474117,-0.474656,-0.475171,-0.475662,-0.476788,-0.477775,-0.478627,-0.479346,-0.479936,-0.480396,-0.480728,-0.480929,-0.480996,-0.480925,-0.480709,-0.480339,-0.479805,-0.479093,-0.478187,-0.477067,-0.475709,-0.474081,-0.472149,-0.469864,-0.467171,-0.463997,-0.460250,-0.455807,-0.450509,-0.444136,-0.436378,-0.426779,-0.422279,-0.417316,-0.411814,-0.405678,-0.398787,-0.390983,-0.382055,-0.371712,-0.359535,-0.344894,-0.326766,-0.303324,-0.288565,-0.270734,-0.248298,-0.234485,-0.218082,-0.210545,-0.202287,-0.193147,-0.182897,-0.171205,-0.157550,-0.141037,-0.131219,-0.119895,-0.106426,-0.098550,-0.089605,-0.079206,-0.076902,-0.074511,-0.072025,-0.069436,-0.066736,-0.060945,-0.054409,-0.050632,-0.046214},
{-0.676421,-0.677009,-0.677656,-0.677994,-0.677837,-0.677774,-0.677743,-0.677739,-0.677751,-0.677771,-0.677847,-0.677865,-0.677889,-0.677954,-0.678013,-0.678072,-0.678191,-0.678309,-0.678427,-0.678545,-0.678662,-0.678779,-0.678896,-0.679186,-0.679474,-0.680043,-0.680603,-0.681153,-0.682225,-0.683259,-0.684252,-0.685206,-0.686119,-0.686991,-0.687822,-0.688612,-0.689362,-0.690072,-0.690741,-0.691371,-0.692774,-0.693937,-0.694869,-0.695575,-0.696061,-0.696329,-0.696381,-0.696216,-0.695833,-0.695224,-0.694383,-0.693299,-0.691956,-0.690337,-0.688418,-0.686172,-0.683563,-0.680548,-0.677075,-0.673077,-0.668472,-0.663158,-0.657000,-0.649824,-0.641401,-0.631413,-0.619417,-0.604759,-0.597941,-0.590456,-0.582192,-0.573014,-0.562747,-0.551163,-0.537956,-0.522703,-0.504794,-0.483307,-0.456741,-0.422406,-0.400785,-0.374655,-0.341786,-0.321582,-0.297656,-0.286701,-0.274734,-0.261540,-0.246825,-0.230161,-0.210883,-0.187861,-0.174326,-0.158846,-0.140582,-0.129950,-0.117883,-0.103783,-0.100635,-0.097352,-0.093916,-0.090311,-0.086514,-0.078232,-0.068781,-0.063492,-0.057725},
{-0.909938,-0.908951,-0.908223,-0.908499,-0.908759,-0.908711,-0.908640,-0.908591,-0.908577,-0.908590,-0.908700,-0.908704,-0.908742,-0.908816,-0.908884,-0.908954,-0.909092,-0.909230,-0.909367,-0.909505,-0.909642,-0.909779,-0.909915,-0.910255,-0.910593,-0.911263,-0.911925,-0.912578,-0.913857,-0.915095,-0.916290,-0.917440,-0.918541,-0.919593,-0.920592,-0.921538,-0.922429,-0.923266,-0.924046,-0.924769,-0.926330,-0.927539,-0.928398,-0.928916,-0.929098,-0.928950,-0.928475,-0.927673,-0.926544,-0.925081,-0.923275,-0.921114,-0.918580,-0.915649,-0.912293,-0.908473,-0.904145,-0.899253,-0.893726,-0.887477,-0.880398,-0.872351,-0.863160,-0.852593,-0.840344,-0.825994,-0.808951,-0.788349,-0.778833,-0.768426,-0.756980,-0.744315,-0.730198,-0.714323,-0.696284,-0.675516,-0.651207,-0.622130,-0.586300,-0.540190,-0.511288,-0.476530,-0.433121,-0.406628,-0.375448,-0.361237,-0.345759,-0.328746,-0.309828,-0.288470,-0.263842,-0.234536,-0.217363,-0.197780,-0.174761,-0.161405,-0.146283,-0.128649,-0.124714,-0.120609,-0.116313,-0.111801,-0.107044,-0.096630,-0.084618,-0.077780,-0.070190},
{-1.152050,-1.153709,-1.153872,-1.153087,-1.153488,-1.153542,-1.153476,-1.153392,-1.153351,-1.153363,-1.153489,-1.153480,-1.153543,-1.153597,-1.153671,-1.153738,-1.153876,-1.154015,-1.154153,-1.154291,-1.154429,-1.154567,-1.154704,-1.155048,-1.155391,-1.156075,-1.156756,-1.157432,-1.158768,-1.160078,-1.161357,-1.162600,-1.163801,-1.164956,-1.166059,-1.167107,-1.168097,-1.169023,-1.169885,-1.170680,-1.172359,-1.173585,-1.174349,-1.174649,-1.174488,-1.173869,-1.172794,-1.171266,-1.169282,-1.166836,-1.163919,-1.160514,-1.156601,-1.152151,-1.147129,-1.141488,-1.135171,-1.128109,-1.120212,-1.111371,-1.101448,-1.090269,-1.077610,-1.063177,-1.046581,-1.027290,-1.004550,-0.977259,-0.964715,-0.951032,-0.936026,-0.919465,-0.901055,-0.880410,-0.857015,-0.830164,-0.798841,-0.761530,-0.715803,-0.657401,-0.621064,-0.577636,-0.523777,-0.491090,-0.452772,-0.435359,-0.416427,-0.395655,-0.372601,-0.346625,-0.316732,-0.281231,-0.260459,-0.236798,-0.209028,-0.192939,-0.174750,-0.153576,-0.148857,-0.143934,-0.138785,-0.133379,-0.127679,-0.115203,-0.100791,-0.092559,-0.083365},
{-1.406836,-1.405392,-1.406268,-1.406057,-1.406142,-1.406299,-1.406271,-1.406165,-1.406107,-1.406129,-1.406223,-1.406264,-1.406282,-1.406354,-1.406410,-1.406470,-1.406596,-1.406721,-1.406845,-1.406969,-1.407094,-1.407219,-1.407344,-1.407657,-1.407971,-1.408601,-1.409232,-1.409865,-1.411131,-1.412394,-1.413647,-1.414882,-1.416094,-1.417274,-1.418417,-1.419514,-1.420559,-1.421547,-1.422471,-1.423325,-1.425130,-1.426417,-1.427149,-1.427306,-1.426878,-1.425861,-1.424254,-1.422054,-1.419260,-1.415863,-1.411852,-1.407208,-1.401906,-1.395912,-1.389184,-1.381667,-1.373291,-1.363970,-1.353597,-1.342038,-1.329123,-1.314638,-1.298306,-1.279767,-1.258538,-1.233961,-1.205106,-1.170609,-1.154795,-1.137571,-1.118711,-1.097933,-1.074875,-1.049071,-1.019901,-0.986518,-0.947721,-0.901729,-0.845717,-0.774746,-0.730886,-0.678728,-0.614393,-0.575510,-0.530067,-0.509459,-0.487080,-0.462557,-0.435377,-0.404792,-0.369646,-0.327970,-0.303613,-0.275888,-0.243375,-0.224553,-0.203287,-0.178557,-0.173049,-0.167306,-0.161301,-0.154998,-0.148355,-0.133821,-0.117037,-0.107447,-0.096722},
{-1.662798,-1.663080,-1.662335,-1.662964,-1.662684,-1.662889,-1.662913,-1.662801,-1.662735,-1.662773,-1.662813,-1.662899,-1.662888,-1.662944,-1.663002,-1.663061,-1.663164,-1.663269,-1.663373,-1.663478,-1.663585,-1.663691,-1.663798,-1.664065,-1.664335,-1.664878,-1.665428,-1.665983,-1.667108,-1.668250,-1.669401,-1.670556,-1.671708,-1.672850,-1.673973,-1.675069,-1.676129,-1.677144,-1.678106,-1.679006,-1.680938,-1.682332,-1.683111,-1.683222,-1.682636,-1.681333,-1.679303,-1.676539,-1.673034,-1.668778,-1.663757,-1.657948,-1.651324,-1.643845,-1.635461,-1.626108,-1.615705,-1.604150,-1.591316,-1.577043,-1.561127,-1.543311,-1.523262,-1.500544,-1.474575,-1.444558,-1.409367,-1.367353,-1.348111,-1.327168,-1.304254,-1.279033,-1.251079,-1.219847,-1.184614,-1.144406,-1.097849,-1.042913,-0.976381,-0.892625,-0.841139,-0.780155,-0.705249,-0.660126,-0.607512,-0.583693,-0.557852,-0.529565,-0.498246,-0.463041,-0.422630,-0.374769,-0.346825,-0.315037,-0.277784,-0.256229,-0.231884,-0.203589,-0.197290,-0.190724,-0.183858,-0.176655,-0.169065,-0.152466,-0.133310,-0.122367,-0.110130}};
  const double imomegaqnm31[8][107] = {{-0.073965,-0.078720,-0.081273,-0.083514,-0.084354,-0.084576,-0.084731,-0.084843,-0.084925,-0.084987,-0.085144,-0.085201,-0.085229,-0.085261,-0.085286,-0.085309,-0.085356,-0.085403,-0.085450,-0.085496,-0.085542,-0.085588,-0.085634,-0.085746,-0.085857,-0.086073,-0.086282,-0.086485,-0.086872,-0.087235,-0.087577,-0.087899,-0.088203,-0.088490,-0.088761,-0.089017,-0.089259,-0.089488,-0.089706,-0.089912,-0.090380,-0.090791,-0.091150,-0.091464,-0.091737,-0.091972,-0.092173,-0.092340,-0.092476,-0.092582,-0.092658,-0.092704,-0.092719,-0.092703,-0.092653,-0.092568,-0.092445,-0.092279,-0.092066,-0.091800,-0.091472,-0.091074,-0.090593,-0.090012,-0.089309,-0.088456,-0.087414,-0.086124,-0.085522,-0.084860,-0.084129,-0.083320,-0.082420,-0.081411,-0.080274,-0.078981,-0.077496,-0.075769,-0.073730,-0.071271,-0.069834,-0.068222,-0.066393,-0.065381,-0.064294,-0.063835,-0.063362,-0.062875,-0.062371,-0.061850,-0.061312,-0.060755,-0.060470,-0.060179,-0.059884,-0.059734,-0.059583,-0.059430,-0.059399,-0.059368,-0.059336,-0.059303,-0.059270,-0.059196,-0.059100,-0.059030,-0.058923},
{-0.257000,-0.257665,-0.257908,-0.258023,-0.258024,-0.258019,-0.258015,-0.258013,-0.258013,-0.258015,-0.258039,-0.258074,-0.258112,-0.258186,-0.258260,-0.258334,-0.258480,-0.258625,-0.258770,-0.258914,-0.259056,-0.259199,-0.259340,-0.259689,-0.260034,-0.260708,-0.261363,-0.261998,-0.263215,-0.264363,-0.265445,-0.266467,-0.267431,-0.268341,-0.269202,-0.270015,-0.270783,-0.271511,-0.272198,-0.272849,-0.274329,-0.275619,-0.276741,-0.277716,-0.278556,-0.279274,-0.279879,-0.280377,-0.280775,-0.281074,-0.281276,-0.281383,-0.281391,-0.281298,-0.281099,-0.280787,-0.280353,-0.279785,-0.279069,-0.278184,-0.277108,-0.275810,-0.274252,-0.272382,-0.270135,-0.267421,-0.264118,-0.260049,-0.258153,-0.256073,-0.253782,-0.251247,-0.248428,-0.245276,-0.241726,-0.237695,-0.233072,-0.227706,-0.221382,-0.213796,-0.209396,-0.204502,-0.199040,-0.196076,-0.192951,-0.191656,-0.190336,-0.188992,-0.187625,-0.186235,-0.184824,-0.183393,-0.182671,-0.181945,-0.181214,-0.180847,-0.180480,-0.180111,-0.180037,-0.179963,-0.179890,-0.179816,-0.179742,-0.179595,-0.179448,-0.179373,-0.179291},
{-0.438425,-0.438087,-0.437919,-0.437841,-0.437874,-0.437896,-0.437917,-0.437936,-0.437953,-0.437968,-0.438032,-0.438094,-0.438157,-0.438284,-0.438411,-0.438537,-0.438789,-0.439039,-0.439289,-0.439538,-0.439785,-0.440032,-0.440278,-0.440887,-0.441489,-0.442675,-0.443833,-0.444965,-0.447147,-0.449222,-0.451191,-0.453059,-0.454827,-0.456502,-0.458085,-0.459583,-0.460999,-0.462337,-0.463601,-0.464796,-0.467500,-0.469841,-0.471860,-0.473592,-0.475065,-0.476303,-0.477323,-0.478139,-0.478760,-0.479195,-0.479446,-0.479514,-0.479398,-0.479093,-0.478589,-0.477876,-0.476938,-0.475754,-0.474299,-0.472539,-0.470433,-0.467927,-0.464954,-0.461426,-0.457227,-0.452200,-0.446129,-0.438708,-0.435265,-0.431497,-0.427357,-0.422787,-0.417716,-0.412055,-0.405691,-0.398475,-0.390214,-0.380643,-0.369405,-0.356038,-0.348409,-0.340129,-0.331334,-0.326854,-0.322403,-0.320644,-0.318901,-0.317177,-0.315474,-0.313795,-0.312140,-0.310513,-0.309710,-0.308914,-0.308125,-0.307734,-0.307344,-0.306956,-0.306879,-0.306802,-0.306724,-0.306647,-0.306570,-0.306416,-0.306263,-0.306188,-0.306116},
{-0.628868,-0.629059,-0.629251,-0.629368,-0.629364,-0.629366,-0.629377,-0.629392,-0.629411,-0.629432,-0.629528,-0.629614,-0.629701,-0.629877,-0.630052,-0.630227,-0.630577,-0.630926,-0.631275,-0.631623,-0.631970,-0.632317,-0.632663,-0.633525,-0.634382,-0.636081,-0.637758,-0.639410,-0.642638,-0.645751,-0.648743,-0.651607,-0.654340,-0.656943,-0.659416,-0.661762,-0.663984,-0.666085,-0.668072,-0.669947,-0.674181,-0.677822,-0.680932,-0.683568,-0.685774,-0.687587,-0.689038,-0.690149,-0.690940,-0.691421,-0.691601,-0.691482,-0.691063,-0.690337,-0.689294,-0.687916,-0.686181,-0.684059,-0.681512,-0.678492,-0.674936,-0.670767,-0.665883,-0.660153,-0.653405,-0.645405,-0.635830,-0.624217,-0.618856,-0.613004,-0.606588,-0.599521,-0.591693,-0.582965,-0.573162,-0.562050,-0.549321,-0.534558,-0.517219,-0.496761,-0.485452,-0.474177,-0.464760,-0.460806,-0.457005,-0.455613,-0.454269,-0.452951,-0.451682,-0.450454,-0.449265,-0.448112,-0.447548,-0.446993,-0.446447,-0.446176,-0.445908,-0.445642,-0.445589,-0.445536,-0.445483,-0.445430,-0.445378,-0.445273,-0.445167,-0.445112,-0.445058},
{-0.835448,-0.835253,-0.835042,-0.835103,-0.835219,-0.835235,-0.835240,-0.835247,-0.835261,-0.835279,-0.835396,-0.835495,-0.835597,-0.835803,-0.836008,-0.836214,-0.836625,-0.837036,-0.837448,-0.837860,-0.838272,-0.838685,-0.839098,-0.840131,-0.841164,-0.843233,-0.845299,-0.847361,-0.851456,-0.855491,-0.859442,-0.863287,-0.867009,-0.870593,-0.874030,-0.877314,-0.880443,-0.883414,-0.886230,-0.888894,-0.894914,-0.900079,-0.904466,-0.908148,-0.911188,-0.913640,-0.915547,-0.916946,-0.917861,-0.918314,-0.918317,-0.917875,-0.916988,-0.915649,-0.913844,-0.911551,-0.908740,-0.905371,-0.901393,-0.896739,-0.891326,-0.885045,-0.877759,-0.869287,-0.859391,-0.847746,-0.833903,-0.817210,-0.809530,-0.801158,-0.791989,-0.781896,-0.770716,-0.758241,-0.744199,-0.728220,-0.709790,-0.688167,-0.662240,-0.630487,-0.612549,-0.571494,-0.486130,-0.438446,-0.387187,-0.365452,-0.342801,-0.319027,-0.293847,-0.266859,-0.237443,-0.204552,-0.186235,-0.166131,-0.143489,-0.130811,-0.116844,-0.101053,-0.097600,-0.094024,-0.090311,-0.086443,-0.082398,-0.073659,-0.063756,-0.058185,-0.052028},
{-1.055096,-1.055587,-1.055823,-1.055596,-1.055704,-1.055763,-1.055785,-1.055787,-1.055787,-1.055798,-1.055925,-1.056017,-1.056124,-1.056330,-1.056539,-1.056746,-1.057163,-1.057581,-1.058000,-1.058420,-1.058842,-1.059265,-1.059689,-1.060755,-1.061828,-1.063995,-1.066188,-1.068404,-1.072890,-1.077425,-1.081977,-1.086510,-1.090990,-1.095386,-1.099668,-1.103814,-1.107806,-1.111631,-1.115281,-1.118752,-1.126641,-1.133437,-1.139208,-1.144031,-1.147984,-1.151132,-1.153535,-1.155237,-1.156276,-1.156679,-1.156463,-1.155637,-1.154203,-1.152151,-1.149466,-1.146119,-1.142073,-1.137277,-1.131663,-1.125146,-1.117617,-1.108936,-1.098922,-1.087336,-1.073866,-1.058079,-1.039373,-1.016863,-1.006512,-0.995224,-0.982851,-0.969207,-0.954050,-0.937061,-0.917797,-0.895619,-0.869517,-0.837664,-0.795682,-0.727748,-0.673462,-0.625162,-0.604395,-0.565264,-0.499648,-0.471392,-0.441689,-0.410470,-0.377515,-0.342341,-0.304152,-0.261610,-0.237990,-0.212119,-0.183050,-0.166802,-0.148923,-0.128738,-0.124327,-0.119761,-0.115020,-0.110083,-0.104922,-0.093776,-0.081153,-0.074054,-0.066212},
{-1.289045,-1.288255,-1.288433,-1.288640,-1.288549,-1.288644,-1.288703,-1.288706,-1.288688,-1.288684,-1.288818,-1.288890,-1.288996,-1.289180,-1.289366,-1.289557,-1.289935,-1.290316,-1.290698,-1.291083,-1.291469,-1.291857,-1.292247,-1.293230,-1.294224,-1.296246,-1.298314,-1.300425,-1.304775,-1.309281,-1.313921,-1.318664,-1.323475,-1.328313,-1.333134,-1.337897,-1.342561,-1.347094,-1.351472,-1.355673,-1.365337,-1.373750,-1.380929,-1.386937,-1.391850,-1.395744,-1.398684,-1.400729,-1.401921,-1.402296,-1.401875,-1.400672,-1.398687,-1.395912,-1.392326,-1.387896,-1.382574,-1.376297,-1.368983,-1.360525,-1.350784,-1.339586,-1.326700,-1.311825,-1.294559,-1.274346,-1.250397,-1.221536,-1.208233,-1.193693,-1.177704,-1.159992,-1.140188,-1.117780,-1.092012,-1.061679,-1.024671,-0.976981,-0.912501,-0.833125,-0.790484,-0.734372,-0.645066,-0.613912,-0.604454,-0.609849,-0.605557,-0.606365,-0.604589,-0.604930,-0.604121,-0.603299,-0.603050,-0.602792,-0.602503,-0.602362,-0.602223,-0.602084,-0.602056,-0.602029,-0.602001,-0.601973,-0.601946,-0.601891,-0.601836,-0.601808,-0.601780},
{-1.529937,-1.530686,-1.530097,-1.530516,-1.530284,-1.530367,-1.530473,-1.530487,-1.530447,-1.530426,-1.530556,-1.530632,-1.530702,-1.530869,-1.531033,-1.531192,-1.531519,-1.531846,-1.532176,-1.532508,-1.532841,-1.533177,-1.533514,-1.534366,-1.535229,-1.536994,-1.538809,-1.540677,-1.544569,-1.548674,-1.552990,-1.557506,-1.562202,-1.567050,-1.572010,-1.577037,-1.582076,-1.587077,-1.591989,-1.596774,-1.607981,-1.617910,-1.626466,-1.633664,-1.639564,-1.644238,-1.647757,-1.650184,-1.651570,-1.651955,-1.651367,-1.649819,-1.647315,-1.643845,-1.639383,-1.633891,-1.627312,-1.619571,-1.610568,-1.600172,-1.588217,-1.574486,-1.558699,-1.540482,-1.519332,-1.494548,-1.465118,-1.429498,-1.412999,-1.394890,-1.374865,-1.352518,-1.327280,-1.298333,-1.264444,-1.223753,-1.173773,-1.112633,-1.040469,-0.950308,-0.891893,-0.826341,-0.752817,-0.699230,-0.623128,-0.578017,-0.540747,-0.502810,-0.462361,-0.418819,-0.371581,-0.319165,-0.290138,-0.258403,-0.222812,-0.202950,-0.181119,-0.156500,-0.151124,-0.145560,-0.139786,-0.133773,-0.127489,-0.113925,-0.098570,-0.089940,-0.080407}};
  const double imomegaqnm30[8][107] = {{-1.529937,-1.530686,-1.530097,-1.530516,-1.530284,-1.530367,-1.530473,-1.530487,-1.530447,-1.530426,-1.530556,-1.530632,-1.530702,-1.530869,-1.531033,-1.531192,-1.531519,-1.531846,-1.532176,-1.532508,-1.532841,-1.533177,-1.533514,-1.534366,-1.535229,-1.536994,-1.538809,-1.540677,-1.544569,-1.548674,-1.552990,-1.557506,-1.562202,-1.567050,-1.572010,-1.577037,-1.582076,-1.587077,-1.591989,-1.596774,-1.607981,-1.617910,-1.626466,-1.633664,-1.639564,-1.644238,-1.647757,-1.650184,-1.651570,-1.651955,-1.651367,-1.649819,-1.647315,-1.643845,-0.092687,-0.092637,-0.092553,-0.092433,-0.092275,-0.092076,-0.091832,-0.091538,-0.091186,-0.090769,-0.090275,-0.089691,-0.088997,-0.088168,-0.087792,-0.087386,-0.086947,-0.086471,-0.085955,-0.085393,-0.084779,-0.084107,-0.083367,-0.082549,-0.081642,-0.080628,-0.080076,-0.079490,-0.078867,-0.078540,-0.078202,-0.078064,-0.077924,-0.077783,-0.077639,-0.077493,-0.077346,-0.077196,-0.077121,-0.077045,-0.076969,-0.076929,-0.076884,-0.076820,-0.076802,-0.076779,-0.076750,-0.076713,-0.076663,-0.076489,-0.076039,-0.075485,-0.074203},
{-1.529937,-1.530686,-1.530097,-1.530516,-1.530284,-1.530367,-1.530473,-1.530487,-1.530447,-1.530426,-1.530556,-1.530632,-1.530702,-1.530869,-1.531033,-1.531192,-1.531519,-1.531846,-1.532176,-1.532508,-1.532841,-1.533177,-1.533514,-1.534366,-1.535229,-1.536994,-1.538809,-1.540677,-1.544569,-1.548674,-1.552990,-1.557506,-1.562202,-1.567050,-1.572010,-1.577037,-1.582076,-1.587077,-1.591989,-1.596774,-1.607981,-1.617910,-1.626466,-1.633664,-1.639564,-1.644238,-1.647757,-1.650184,-1.651570,-1.651955,-1.651367,-1.649819,-1.647315,-1.643845,-0.281245,-0.281086,-0.280817,-0.280433,-0.279928,-0.279291,-0.278511,-0.277569,-0.276446,-0.275114,-0.273539,-0.271675,-0.269463,-0.266825,-0.265627,-0.264336,-0.262942,-0.261433,-0.259797,-0.258018,-0.256079,-0.253960,-0.251636,-0.249081,-0.246264,-0.243150,-0.241470,-0.239702,-0.237841,-0.236875,-0.235884,-0.235481,-0.235073,-0.234662,-0.234247,-0.233827,-0.233404,-0.232976,-0.232761,-0.232544,-0.232327,-0.232217,-0.232108,-0.232000,-0.231978,-0.231958,-0.231937,-0.231918,-0.231899,-0.231862,-0.231806,-0.231736,-0.231556},
{-1.529937,-1.530686,-1.530097,-1.530516,-1.530284,-1.530367,-1.530473,-1.530487,-1.530447,-1.530426,-1.530556,-1.530632,-1.530702,-1.530869,-1.531033,-1.531192,-1.531519,-1.531846,-1.532176,-1.532508,-1.532841,-1.533177,-1.533514,-1.534366,-1.535229,-1.536994,-1.538809,-1.540677,-1.544569,-1.548674,-1.552990,-1.557506,-1.562202,-1.567050,-1.572010,-1.577037,-1.582076,-1.587077,-1.591989,-1.596774,-1.607981,-1.617910,-1.626466,-1.633664,-1.639564,-1.644238,-1.647757,-1.650184,-1.651570,-1.651955,-1.651367,-1.649819,-1.647315,-1.643845,-0.478993,-0.478693,-0.478187,-0.477465,-0.476516,-0.475321,-0.473856,-0.472093,-0.469991,-0.467502,-0.464563,-0.461092,-0.456981,-0.452089,-0.449873,-0.447487,-0.444914,-0.442135,-0.439128,-0.435869,-0.432331,-0.428485,-0.424299,-0.419744,-0.414795,-0.409439,-0.406611,-0.403687,-0.400675,-0.399138,-0.397582,-0.396955,-0.396325,-0.395693,-0.395058,-0.394421,-0.393782,-0.393140,-0.392819,-0.392497,-0.392175,-0.392013,-0.391852,-0.391691,-0.391658,-0.391625,-0.391592,-0.391559,-0.391525,-0.391457,-0.391405,-0.391405,-0.391445},
{-1.529937,-1.530686,-1.530097,-1.530516,-1.530284,-1.530367,-1.530473,-1.530487,-1.530447,-1.530426,-1.530556,-1.530632,-1.530702,-1.530869,-1.531033,-1.531192,-1.531519,-1.531846,-1.532176,-1.532508,-1.532841,-1.533177,-1.533514,-1.534366,-1.535229,-1.536994,-1.538809,-1.540677,-1.544569,-1.548674,-1.552990,-1.557506,-1.562202,-1.567050,-1.572010,-1.577037,-1.582076,-1.587077,-1.591989,-1.596774,-1.607981,-1.617910,-1.626466,-1.633664,-1.639564,-1.644238,-1.647757,-1.650184,-1.651570,-1.651955,-1.651367,-1.649819,-1.647315,-1.643845,-0.690175,-0.689687,-0.688863,-0.687691,-0.686150,-0.684213,-0.681842,-0.678992,-0.675601,-0.671595,-0.666873,-0.661311,-0.654741,-0.646948,-0.643426,-0.639642,-0.635570,-0.631185,-0.626456,-0.621355,-0.615851,-0.609918,-0.603541,-0.596726,-0.589514,-0.581992,-0.578157,-0.574299,-0.570439,-0.568516,-0.566600,-0.565836,-0.565074,-0.564314,-0.563556,-0.562800,-0.562047,-0.561296,-0.560921,-0.560547,-0.560174,-0.559988,-0.559802,-0.559615,-0.559578,-0.559540,-0.559503,-0.559467,-0.559432,-0.559363,-0.559277,-0.559209,-0.559138},
{-1.529937,-1.530686,-1.530097,-1.530516,-1.530284,-1.530367,-1.530473,-1.530487,-1.530447,-1.530426,-1.530556,-1.530632,-1.530702,-1.530869,-1.531033,-1.531192,-1.531519,-1.531846,-1.532176,-1.532508,-1.532841,-1.533177,-1.533514,-1.534366,-1.535229,-1.536994,-1.538809,-1.540677,-1.544569,-1.548674,-1.552990,-1.557506,-1.562202,-1.567050,-1.572010,-1.577037,-1.582076,-1.587077,-1.591989,-1.596774,-1.607981,-1.617910,-1.626466,-1.633664,-1.639564,-1.644238,-1.647757,-1.650184,-1.651570,-1.651955,-1.651367,-1.649819,-1.647315,-1.643845,-0.915410,-0.914686,-0.913468,-0.911735,-0.909458,-0.906598,-0.903103,-0.898906,-0.893923,-0.888043,-0.881128,-0.872996,-0.863415,-0.852081,-0.846972,-0.841494,-0.835614,-0.829301,-0.822525,-0.815260,-0.807496,-0.799244,-0.790564,-0.781585,-0.772513,-0.763590,-0.759253,-0.755027,-0.750929,-0.748931,-0.746968,-0.746193,-0.745424,-0.744661,-0.743903,-0.743152,-0.742406,-0.741667,-0.741299,-0.740933,-0.740568,-0.740387,-0.740205,-0.740023,-0.739987,-0.739952,-0.739918,-0.739883,-0.739847,-0.739761,-0.739695,-0.739705,-0.739695},
{-1.529937,-1.530686,-1.530097,-1.530516,-1.530284,-1.530367,-1.530473,-1.530487,-1.530447,-1.530426,-1.530556,-1.530632,-1.530702,-1.530869,-1.531033,-1.531192,-1.531519,-1.531846,-1.532176,-1.532508,-1.532841,-1.533177,-1.533514,-1.534366,-1.535229,-1.536994,-1.538809,-1.540677,-1.544569,-1.548674,-1.552990,-1.557506,-1.562202,-1.567050,-1.572010,-1.577037,-1.582076,-1.587077,-1.591989,-1.596774,-1.607981,-1.617910,-1.626466,-1.633664,-1.639564,-1.644238,-1.647757,-1.650184,-1.651570,-1.651955,-1.651367,-1.649819,-1.647315,-1.643845,-1.151824,-1.150837,-1.149175,-1.146812,-1.143709,-1.139814,-1.135056,-1.129348,-1.122576,-1.114592,-1.105211,-1.094192,-1.081227,-1.065919,-1.059035,-1.051666,-1.043780,-1.035350,-1.026359,-1.016818,-1.006788,-0.996417,-0.985980,-0.975863,-0.966386,-0.957682,-0.953634,-0.949785,-0.946128,-0.944368,-0.942653,-0.941979,-0.941311,-0.940651,-0.939997,-0.939349,-0.938708,-0.938073,-0.937758,-0.937445,-0.937132,-0.936977,-0.936822,-0.936667,-0.936639,-0.936611,-0.936580,-0.936544,-0.936504,-0.936448,-0.936428,-0.936315,-0.936219},
{-1.529937,-1.530686,-1.530097,-1.530516,-1.530284,-1.530367,-1.530473,-1.530487,-1.530447,-1.530426,-1.530556,-1.530632,-1.530702,-1.530869,-1.531033,-1.531192,-1.531519,-1.531846,-1.532176,-1.532508,-1.532841,-1.533177,-1.533514,-1.534366,-1.535229,-1.536994,-1.538809,-1.540677,-1.544569,-1.548674,-1.552990,-1.557506,-1.562202,-1.567050,-1.572010,-1.577037,-1.582076,-1.587077,-1.591989,-1.596774,-1.607981,-1.617910,-1.626466,-1.633664,-1.639564,-1.644238,-1.647757,-1.650184,-1.651570,-1.651955,-1.651367,-1.649819,-1.647315,-1.643845,-1.395494,-1.394233,-1.392109,-1.389089,-1.385124,-1.380148,-1.374073,-1.366785,-1.358140,-1.347953,-1.335985,-1.321933,-1.305409,-1.285931,-1.277193,-1.267863,-1.257920,-1.247362,-1.236235,-1.224669,-1.212958,-1.201632,-1.191349,-1.182302,-1.174267,-1.167204,-1.163981,-1.160939,-1.158062,-1.156680,-1.155334,-1.154806,-1.154282,-1.153764,-1.153251,-1.152743,-1.152240,-1.151741,-1.151494,-1.151247,-1.151003,-1.150879,-1.150761,-1.150640,-1.150623,-1.150601,-1.150566,-1.150523,-1.150492,-1.150513,-1.150334,-1.150373,-1.150585},
{-1.529937,-1.530686,-1.530097,-1.530516,-1.530284,-1.530367,-1.530473,-1.530487,-1.530447,-1.530426,-1.530556,-1.530632,-1.530702,-1.530869,-1.531033,-1.531192,-1.531519,-1.531846,-1.532176,-1.532508,-1.532841,-1.533177,-1.533514,-1.534366,-1.535229,-1.536994,-1.538809,-1.540677,-1.544569,-1.548674,-1.552990,-1.557506,-1.562202,-1.567050,-1.572010,-1.577037,-1.582076,-1.587077,-1.591989,-1.596774,-1.607981,-1.617910,-1.626466,-1.633664,-1.639564,-1.644238,-1.647757,-1.650184,-1.651570,-1.651955,-1.651367,-1.649819,-1.647315,-1.643845,-1.643335,-1.641797,-1.639208,-1.635528,-1.630696,-1.624631,-1.617226,-1.608344,-1.597807,-1.585389,-1.570800,-1.553671,-1.533537,-1.509851,-1.499261,-1.488002,-1.476093,-1.463621,-1.450821,-1.438215,-1.426792,-1.417582,-1.409842,-1.402860,-1.396979,-1.391746,-1.389346,-1.387063,-1.384890,-1.383840,-1.382815,-1.382410,-1.382010,-1.381612,-1.381218,-1.380828,-1.380440,-1.380056,-1.379863,-1.379676,-1.379489,-1.379383,-1.379306,-1.379208,-1.379208,-1.379184,-1.379129,-1.379081,-1.379097,-1.379105,-1.379024,-1.379212,-1.378677}};
  const double imomegaqnm44[8][107] = {{-0.047289,-0.057085,-0.065286,-0.076920,-0.083531,-0.085574,-0.087060,-0.088144,-0.088942,-0.089536,-0.090962,-0.091420,-0.091606,-0.091741,-0.091784,-0.091803,-0.091819,-0.091828,-0.091837,-0.091845,-0.091853,-0.091861,-0.091870,-0.091890,-0.091911,-0.091952,-0.091993,-0.092034,-0.092115,-0.092196,-0.092275,-0.092354,-0.092432,-0.092510,-0.092586,-0.092662,-0.092736,-0.092810,-0.092882,-0.092953,-0.093127,-0.093292,-0.093448,-0.093595,-0.093730,-0.093853,-0.093962,-0.094056,-0.094132,-0.094189,-0.094223,-0.094233,-0.094215,-0.094164,-0.094077,-0.093948,-0.093770,-0.093537,-0.093239,-0.092865,-0.092401,-0.091829,-0.091127,-0.090268,-0.089212,-0.087909,-0.086287,-0.084240,-0.083269,-0.082192,-0.080991,-0.079644,-0.078125,-0.076399,-0.074418,-0.072119,-0.069410,-0.066156,-0.062140,-0.056982,-0.053763,-0.049911,-0.045132,-0.042232,-0.038838,-0.037297,-0.035624,-0.033790,-0.031759,-0.029475,-0.026854,-0.023753,-0.021945,-0.019893,-0.017493,-0.016108,-0.014546,-0.012736,-0.012334,-0.011915,-0.011478,-0.011019,-0.010536,-0.009482,-0.008270,-0.007580,-0.006809},
{-0.267496,-0.272430,-0.275117,-0.277412,-0.278166,-0.278332,-0.278431,-0.278489,-0.278523,-0.278542,-0.278554,-0.278543,-0.278539,-0.278544,-0.278555,-0.278567,-0.278590,-0.278613,-0.278636,-0.278659,-0.278682,-0.278704,-0.278727,-0.278784,-0.278841,-0.278955,-0.279068,-0.279180,-0.279403,-0.279623,-0.279841,-0.280057,-0.280270,-0.280480,-0.280687,-0.280892,-0.281093,-0.281291,-0.281485,-0.281676,-0.282137,-0.282572,-0.282979,-0.283355,-0.283696,-0.283998,-0.284257,-0.284467,-0.284624,-0.284721,-0.284749,-0.284701,-0.284567,-0.284334,-0.283990,-0.283519,-0.282901,-0.282113,-0.281129,-0.279913,-0.278426,-0.276614,-0.274412,-0.271733,-0.268465,-0.264452,-0.259480,-0.253233,-0.250278,-0.247003,-0.243357,-0.239276,-0.234678,-0.229458,-0.223476,-0.216539,-0.208375,-0.198576,-0.186494,-0.170992,-0.161324,-0.149756,-0.135408,-0.126707,-0.116519,-0.111896,-0.106875,-0.101374,-0.095279,-0.088426,-0.080562,-0.071259,-0.065836,-0.059678,-0.052479,-0.048323,-0.043639,-0.038210,-0.037003,-0.035747,-0.034434,-0.033058,-0.031609,-0.028447,-0.024810,-0.022739,-0.020428},
{-0.476992,-0.476552,-0.476059,-0.475354,-0.475023,-0.474945,-0.474902,-0.474880,-0.474873,-0.474873,-0.474904,-0.474923,-0.474932,-0.474945,-0.474960,-0.474975,-0.475006,-0.475036,-0.475066,-0.475097,-0.475127,-0.475157,-0.475187,-0.475263,-0.475338,-0.475487,-0.475635,-0.475782,-0.476073,-0.476359,-0.476641,-0.476918,-0.477190,-0.477456,-0.477718,-0.477973,-0.478223,-0.478467,-0.478704,-0.478936,-0.479483,-0.479984,-0.480434,-0.480828,-0.481160,-0.481423,-0.481610,-0.481712,-0.481721,-0.481624,-0.481410,-0.481064,-0.480570,-0.479908,-0.479056,-0.477986,-0.476666,-0.475060,-0.473120,-0.470791,-0.468003,-0.464669,-0.460679,-0.455890,-0.450113,-0.443090,-0.434465,-0.423712,-0.418650,-0.413055,-0.406842,-0.399905,-0.392108,-0.383276,-0.373175,-0.361487,-0.347759,-0.331313,-0.311071,-0.285140,-0.268985,-0.249669,-0.225726,-0.211210,-0.194221,-0.186512,-0.178140,-0.168968,-0.158807,-0.147383,-0.134275,-0.118768,-0.109729,-0.099465,-0.087465,-0.080539,-0.072732,-0.063683,-0.061672,-0.059578,-0.057390,-0.055096,-0.052682,-0.047413,-0.041350,-0.037898,-0.034047},
{-0.688887,-0.687276,-0.686870,-0.687194,-0.687484,-0.687529,-0.687535,-0.687523,-0.687504,-0.687487,-0.687464,-0.687480,-0.687488,-0.687497,-0.687509,-0.687521,-0.687544,-0.687567,-0.687590,-0.687613,-0.687636,-0.687658,-0.687681,-0.687738,-0.687794,-0.687905,-0.688015,-0.688122,-0.688333,-0.688537,-0.688734,-0.688923,-0.689105,-0.689278,-0.689444,-0.689601,-0.689749,-0.689889,-0.690018,-0.690138,-0.690394,-0.690579,-0.690690,-0.690717,-0.690652,-0.690487,-0.690211,-0.689812,-0.689276,-0.688589,-0.687732,-0.686685,-0.685425,-0.683924,-0.682151,-0.680068,-0.677629,-0.674782,-0.671462,-0.667589,-0.663066,-0.657772,-0.651551,-0.644203,-0.635463,-0.624972,-0.612229,-0.596503,-0.589146,-0.581044,-0.572077,-0.562097,-0.550915,-0.538287,-0.523888,-0.507273,-0.487810,-0.464555,-0.436002,-0.399509,-0.376808,-0.349693,-0.316109,-0.295761,-0.271954,-0.261154,-0.249426,-0.236578,-0.222347,-0.206349,-0.187993,-0.166281,-0.153625,-0.139254,-0.122453,-0.112755,-0.101826,-0.089156,-0.086341,-0.083409,-0.080346,-0.077135,-0.073755,-0.066378,-0.057891,-0.053057,-0.047665},
{-0.918235,-0.920515,-0.920895,-0.920137,-0.920012,-0.920094,-0.920167,-0.920207,-0.920214,-0.920202,-0.920149,-0.920166,-0.920164,-0.920160,-0.920158,-0.920156,-0.920152,-0.920148,-0.920144,-0.920140,-0.920136,-0.920132,-0.920127,-0.920116,-0.920104,-0.920079,-0.920051,-0.920022,-0.919956,-0.919881,-0.919796,-0.919702,-0.919597,-0.919482,-0.919356,-0.919218,-0.919069,-0.918908,-0.918734,-0.918547,-0.918020,-0.917401,-0.916681,-0.915851,-0.914900,-0.913815,-0.912584,-0.911190,-0.909617,-0.907843,-0.905847,-0.903603,-0.901078,-0.898239,-0.895043,-0.891442,-0.887378,-0.882780,-0.877566,-0.871633,-0.864855,-0.857075,-0.848091,-0.837647,-0.825400,-0.810890,-0.793470,-0.772205,-0.762324,-0.751485,-0.739534,-0.726280,-0.711484,-0.694831,-0.675907,-0.654142,-0.628729,-0.598457,-0.561400,-0.514171,-0.484847,-0.449863,-0.406581,-0.380377,-0.349729,-0.335831,-0.320740,-0.304211,-0.285904,-0.265327,-0.241720,-0.213799,-0.197524,-0.179045,-0.157442,-0.144973,-0.130920,-0.114630,-0.111011,-0.107241,-0.103302,-0.099174,-0.094829,-0.085343,-0.074431,-0.068217,-0.061284},
{-1.172429,-1.170250,-1.170724,-1.171528,-1.171085,-1.171073,-1.171150,-1.171223,-1.171252,-1.171240,-1.171165,-1.171180,-1.171155,-1.171139,-1.171115,-1.171094,-1.171049,-1.171005,-1.170960,-1.170916,-1.170871,-1.170826,-1.170781,-1.170668,-1.170555,-1.170326,-1.170094,-1.169858,-1.169379,-1.168887,-1.168382,-1.167864,-1.167332,-1.166786,-1.166225,-1.165649,-1.165057,-1.164449,-1.163825,-1.163183,-1.161499,-1.159693,-1.157756,-1.155675,-1.153437,-1.151026,-1.148428,-1.145621,-1.142586,-1.139297,-1.135727,-1.131843,-1.127608,-1.122977,-1.117900,-1.112316,-1.106154,-1.099328,-1.091735,-1.083249,-1.073714,-1.062936,-1.050670,-1.036597,-1.020299,-1.001210,-0.978540,-0.951144,-0.938500,-0.924680,-0.909499,-0.892724,-0.874063,-0.853135,-0.829435,-0.802272,-0.770663,-0.733138,-0.687353,-0.629184,-0.593147,-0.550213,-0.497162,-0.465071,-0.427558,-0.410551,-0.392089,-0.371872,-0.349482,-0.324320,-0.295457,-0.261323,-0.241428,-0.218839,-0.192433,-0.177192,-0.160015,-0.140105,-0.135681,-0.131073,-0.126259,-0.121213,-0.115902,-0.104308,-0.090972,-0.083376,-0.074903},
{-1.434106,-1.435046,-1.433745,-1.434332,-1.434110,-1.434007,-1.434061,-1.434149,-1.434181,-1.434154,-1.434096,-1.434067,-1.434053,-1.434006,-1.433967,-1.433925,-1.433841,-1.433756,-1.433671,-1.433587,-1.433502,-1.433417,-1.433332,-1.433118,-1.432903,-1.432470,-1.432032,-1.431590,-1.430693,-1.429777,-1.428843,-1.427889,-1.426915,-1.425920,-1.424905,-1.423867,-1.422808,-1.421726,-1.420620,-1.419490,-1.416555,-1.413454,-1.410174,-1.406700,-1.403018,-1.399110,-1.394956,-1.390535,-1.385822,-1.380787,-1.375397,-1.369615,-1.363395,-1.356686,-1.349428,-1.341548,-1.332961,-1.323565,-1.313236,-1.301825,-1.289146,-1.274967,-1.258995,-1.240852,-1.220040,-1.195886,-1.167453,-1.133385,-1.117752,-1.100721,-1.082074,-1.061538,-1.038767,-1.013313,-0.984584,-0.951766,-0.913705,-0.868675,-0.813920,-0.744589,-0.701738,-0.650764,-0.587868,-0.549856,-0.505448,-0.485323,-0.463480,-0.439564,-0.413085,-0.383331,-0.349205,-0.308853,-0.285337,-0.258637,-0.227427,-0.209413,-0.189112,-0.165580,-0.160352,-0.154906,-0.149217,-0.143253,-0.136976,-0.123274,-0.107512,-0.098535,-0.088522},
{-1.701724,-1.702654,-1.702929,-1.702488,-1.702586,-1.702446,-1.702476,-1.702559,-1.702577,-1.702533,-1.702508,-1.702450,-1.702433,-1.702375,-1.702317,-1.702258,-1.702143,-1.702028,-1.701913,-1.701797,-1.701681,-1.701565,-1.701448,-1.701156,-1.700862,-1.700270,-1.699671,-1.699066,-1.697836,-1.696579,-1.695294,-1.693982,-1.692640,-1.691270,-1.689869,-1.688438,-1.686975,-1.685480,-1.683952,-1.682390,-1.678334,-1.674047,-1.669517,-1.664727,-1.659659,-1.654293,-1.648606,-1.642574,-1.636168,-1.629355,-1.622096,-1.614348,-1.606060,-1.597171,-1.587611,-1.577296,-1.566129,-1.553988,-1.540731,-1.526181,-1.510123,-1.492287,-1.472329,-1.449810,-1.424148,-1.394562,-1.359963,-1.318780,-1.299967,-1.279528,-1.257209,-1.232697,-1.205593,-1.175383,-1.141384,-1.102665,-1.057901,-1.005112,-0.941138,-0.860413,-0.810643,-0.751534,-0.678709,-0.634741,-0.583406,-0.560151,-0.534917,-0.507294,-0.476715,-0.442361,-0.402967,-0.356392,-0.329251,-0.298439,-0.262423,-0.241636,-0.218210,-0.191057,-0.185023,-0.178739,-0.172175,-0.165293,-0.158050,-0.142240,-0.124053,-0.113695,-0.102141}};
  const double imomegaqnm43[8][107] = {{-0.050967,-0.062587,-0.071427,-0.082053,-0.086727,-0.087934,-0.088737,-0.089284,-0.089666,-0.089939,-0.090561,-0.090757,-0.090839,-0.090902,-0.090925,-0.090937,-0.090952,-0.090965,-0.090977,-0.090990,-0.091003,-0.091015,-0.091028,-0.091059,-0.091090,-0.091152,-0.091213,-0.091273,-0.091393,-0.091510,-0.091625,-0.091738,-0.091848,-0.091956,-0.092062,-0.092166,-0.092267,-0.092367,-0.092464,-0.092559,-0.092786,-0.092999,-0.093198,-0.093382,-0.093550,-0.093702,-0.093836,-0.093952,-0.094048,-0.094123,-0.094175,-0.094201,-0.094198,-0.094164,-0.094095,-0.093985,-0.093831,-0.093624,-0.093357,-0.093020,-0.092601,-0.092084,-0.091449,-0.090670,-0.089712,-0.088528,-0.087053,-0.085187,-0.084300,-0.083315,-0.082216,-0.080981,-0.079586,-0.077998,-0.076170,-0.074044,-0.071530,-0.068498,-0.064738,-0.059879,-0.056829,-0.053157,-0.048569,-0.045767,-0.042466,-0.040960,-0.039319,-0.037514,-0.035505,-0.033234,-0.030612,-0.027485,-0.025648,-0.023550,-0.021079,-0.019644,-0.018017,-0.016120,-0.015696,-0.015255,-0.014793,-0.014308,-0.013797,-0.012678,-0.011388,-0.010652,-0.009830},
{-0.267208,-0.271308,-0.273219,-0.274661,-0.275084,-0.275171,-0.275221,-0.275250,-0.275266,-0.275275,-0.275283,-0.275284,-0.275289,-0.275306,-0.275325,-0.275344,-0.275382,-0.275420,-0.275458,-0.275495,-0.275533,-0.275570,-0.275608,-0.275701,-0.275793,-0.275977,-0.276159,-0.276339,-0.276694,-0.277041,-0.277381,-0.277714,-0.278040,-0.278358,-0.278669,-0.278973,-0.279270,-0.279560,-0.279842,-0.280118,-0.280775,-0.281387,-0.281952,-0.282469,-0.282937,-0.283353,-0.283715,-0.284019,-0.284261,-0.284436,-0.284538,-0.284561,-0.284496,-0.284334,-0.284064,-0.283670,-0.283138,-0.282446,-0.281571,-0.280483,-0.279144,-0.277507,-0.275512,-0.273080,-0.270109,-0.266454,-0.261917,-0.256203,-0.253495,-0.250490,-0.247140,-0.243383,-0.239143,-0.234319,-0.228777,-0.222332,-0.214721,-0.205548,-0.194178,-0.179489,-0.170264,-0.159161,-0.145275,-0.136787,-0.126779,-0.122211,-0.117228,-0.111743,-0.105633,-0.098719,-0.090720,-0.081161,-0.075534,-0.069090,-0.061475,-0.057034,-0.051986,-0.046068,-0.044743,-0.043358,-0.041905,-0.040378,-0.038763,-0.035212,-0.031077,-0.028696,-0.026012},
{-0.467961,-0.467928,-0.467721,-0.467392,-0.467251,-0.467223,-0.467210,-0.467207,-0.467209,-0.467213,-0.467240,-0.467258,-0.467274,-0.467304,-0.467335,-0.467366,-0.467428,-0.467490,-0.467551,-0.467613,-0.467674,-0.467735,-0.467796,-0.467948,-0.468099,-0.468399,-0.468695,-0.468987,-0.469562,-0.470123,-0.470670,-0.471203,-0.471723,-0.472229,-0.472721,-0.473200,-0.473665,-0.474117,-0.474555,-0.474980,-0.475982,-0.476899,-0.477729,-0.478470,-0.479120,-0.479676,-0.480131,-0.480483,-0.480722,-0.480843,-0.480834,-0.480685,-0.480382,-0.479908,-0.479245,-0.478369,-0.477252,-0.475861,-0.474154,-0.472081,-0.469580,-0.466571,-0.462953,-0.458595,-0.453321,-0.446894,-0.438977,-0.429080,-0.424409,-0.419240,-0.413489,-0.407057,-0.399812,-0.391586,-0.382153,-0.371204,-0.358294,-0.342758,-0.323524,-0.298692,-0.283102,-0.264334,-0.240854,-0.226493,-0.209553,-0.201816,-0.193377,-0.184084,-0.173730,-0.162011,-0.148455,-0.132258,-0.122731,-0.111831,-0.098972,-0.091490,-0.083002,-0.073087,-0.070872,-0.068561,-0.066141,-0.063599,-0.060917,-0.055039,-0.048237,-0.044343,-0.039981},
{-0.672249,-0.671448,-0.671290,-0.671459,-0.671573,-0.671589,-0.671592,-0.671589,-0.671585,-0.671583,-0.671597,-0.671622,-0.671642,-0.671683,-0.671725,-0.671766,-0.671849,-0.671931,-0.672013,-0.672095,-0.672177,-0.672258,-0.672340,-0.672542,-0.672743,-0.673140,-0.673532,-0.673919,-0.674676,-0.675411,-0.676124,-0.676815,-0.677484,-0.678131,-0.678756,-0.679359,-0.679940,-0.680499,-0.681036,-0.681551,-0.682741,-0.683793,-0.684705,-0.685475,-0.686100,-0.686575,-0.686895,-0.687052,-0.687038,-0.686842,-0.686450,-0.685846,-0.685012,-0.683924,-0.682556,-0.680873,-0.678838,-0.676402,-0.673507,-0.670082,-0.666037,-0.661260,-0.655609,-0.648898,-0.640878,-0.631213,-0.619430,-0.604832,-0.597984,-0.590428,-0.582049,-0.572704,-0.562209,-0.550324,-0.536732,-0.520992,-0.502477,-0.480242,-0.452767,-0.417355,-0.395150,-0.368437,-0.335051,-0.314653,-0.290615,-0.279647,-0.267693,-0.254542,-0.239906,-0.223365,-0.204271,-0.181520,-0.168174,-0.152942,-0.135027,-0.124632,-0.112868,-0.099165,-0.096110,-0.092924,-0.089591,-0.086093,-0.082405,-0.074336,-0.065019,-0.059696,-0.053742},
{-0.890457,-0.891421,-0.891587,-0.891311,-0.891280,-0.891313,-0.891341,-0.891358,-0.891365,-0.891365,-0.891375,-0.891404,-0.891428,-0.891476,-0.891524,-0.891572,-0.891669,-0.891765,-0.891860,-0.891956,-0.892051,-0.892146,-0.892240,-0.892476,-0.892709,-0.893171,-0.893625,-0.894073,-0.894945,-0.895787,-0.896599,-0.897380,-0.898130,-0.898849,-0.899536,-0.900191,-0.900814,-0.901405,-0.901963,-0.902490,-0.903662,-0.904630,-0.905391,-0.905943,-0.906283,-0.906405,-0.906302,-0.905967,-0.905388,-0.904551,-0.903440,-0.902035,-0.900311,-0.898239,-0.895783,-0.892901,-0.889542,-0.885644,-0.881130,-0.875907,-0.869859,-0.862840,-0.854663,-0.845086,-0.833786,-0.820324,-0.804084,-0.784160,-0.774872,-0.764659,-0.753372,-0.740824,-0.726777,-0.710918,-0.692834,-0.671952,-0.647457,-0.618121,-0.581970,-0.535513,-0.506452,-0.471562,-0.428069,-0.401560,-0.370386,-0.356189,-0.340731,-0.323748,-0.304876,-0.283583,-0.259050,-0.229886,-0.212810,-0.193352,-0.170509,-0.157275,-0.142315,-0.124914,-0.121039,-0.116998,-0.112773,-0.108340,-0.103669,-0.093454,-0.081672,-0.074946,-0.067429},
{-1.126859,-1.125834,-1.125954,-1.126319,-1.126176,-1.126168,-1.126195,-1.126225,-1.126243,-1.126248,-1.126253,-1.126286,-1.126307,-1.126359,-1.126409,-1.126459,-1.126559,-1.126659,-1.126759,-1.126858,-1.126957,-1.127056,-1.127154,-1.127399,-1.127642,-1.128122,-1.128594,-1.129058,-1.129961,-1.130829,-1.131662,-1.132458,-1.133215,-1.133934,-1.134612,-1.135250,-1.135845,-1.136399,-1.136910,-1.137378,-1.138355,-1.139054,-1.139470,-1.139598,-1.139435,-1.138972,-1.138204,-1.137119,-1.135705,-1.133945,-1.131820,-1.129304,-1.126368,-1.122977,-1.119085,-1.114642,-1.109583,-1.103831,-1.097292,-1.089849,-1.081358,-1.071637,-1.060455,-1.047509,-1.032398,-1.014577,-0.993280,-0.967385,-0.955381,-0.942227,-0.927737,-0.911678,-0.893755,-0.873583,-0.850650,-0.824251,-0.793379,-0.756525,-0.711269,-0.653346,-0.617238,-0.574013,-0.520310,-0.487672,-0.449379,-0.431968,-0.413032,-0.392253,-0.369188,-0.343199,-0.313298,-0.277807,-0.257054,-0.233428,-0.205721,-0.189684,-0.171568,-0.150512,-0.145825,-0.140939,-0.135831,-0.130473,-0.124828,-0.112489,-0.098264,-0.090147,-0.081079},
{-1.372466,-1.373275,-1.372701,-1.372840,-1.372836,-1.372781,-1.372788,-1.372825,-1.372853,-1.372859,-1.372859,-1.372887,-1.372910,-1.372957,-1.373005,-1.373052,-1.373147,-1.373241,-1.373335,-1.373428,-1.373522,-1.373615,-1.373707,-1.373939,-1.374168,-1.374622,-1.375069,-1.375509,-1.376365,-1.377189,-1.377976,-1.378725,-1.379434,-1.380101,-1.380722,-1.381298,-1.381825,-1.382302,-1.382728,-1.383101,-1.383798,-1.384146,-1.384132,-1.383747,-1.382982,-1.381828,-1.380274,-1.378309,-1.375916,-1.373076,-1.369766,-1.365955,-1.361610,-1.356686,-1.351132,-1.344884,-1.337866,-1.329985,-1.321128,-1.311153,-1.299886,-1.287108,-1.272539,-1.255814,-1.236450,-1.213789,-1.186908,-1.154456,-1.139486,-1.123126,-1.105153,-1.085290,-1.063183,-1.038372,-1.010246,-0.977967,-0.940341,-0.895582,-0.840838,-0.771099,-0.727798,-0.676120,-0.612136,-0.573358,-0.527953,-0.507339,-0.484939,-0.460381,-0.433149,-0.402495,-0.367263,-0.325492,-0.301088,-0.273325,-0.240789,-0.221967,-0.200716,-0.176027,-0.170533,-0.164807,-0.158821,-0.152543,-0.145930,-0.131477,-0.114820,-0.105319,-0.094706},
{-1.626688,-1.626751,-1.627193,-1.626831,-1.626996,-1.626921,-1.626905,-1.626944,-1.626977,-1.626979,-1.626985,-1.626995,-1.627023,-1.627062,-1.627102,-1.627143,-1.627225,-1.627306,-1.627387,-1.627469,-1.627550,-1.627630,-1.627711,-1.627912,-1.628112,-1.628508,-1.628900,-1.629286,-1.630041,-1.630769,-1.631466,-1.632130,-1.632756,-1.633340,-1.633881,-1.634374,-1.634816,-1.635205,-1.635538,-1.635812,-1.636228,-1.636233,-1.635804,-1.634922,-1.633571,-1.631734,-1.629398,-1.626546,-1.623158,-1.619211,-1.614677,-1.609522,-1.603703,-1.597171,-1.589863,-1.581706,-1.572609,-1.562462,-1.551132,-1.538451,-1.524214,-1.508161,-1.489960,-1.469181,-1.445251,-1.417395,-1.384523,-1.345045,-1.326899,-1.307110,-1.285418,-1.261497,-1.234937,-1.205201,-1.171580,-1.133104,-1.088396,-1.035401,-0.970849,-0.889002,-0.838381,-0.778147,-0.703806,-0.658863,-0.606333,-0.582515,-0.556653,-0.528321,-0.496929,-0.461622,-0.421075,-0.373046,-0.345006,-0.313120,-0.275774,-0.254178,-0.229802,-0.201493,-0.195195,-0.188631,-0.181771,-0.174575,-0.166996,-0.150436,-0.131355,-0.120473,-0.108320}};
  const double imomegaqnm42[8][107] = {{-0.058810,-0.071056,-0.078796,-0.085560,-0.087557,-0.087981,-0.088246,-0.088420,-0.088539,-0.088624,-0.088828,-0.088902,-0.088937,-0.088971,-0.088989,-0.089004,-0.089030,-0.089056,-0.089082,-0.089107,-0.089133,-0.089158,-0.089183,-0.089245,-0.089307,-0.089429,-0.089548,-0.089665,-0.089892,-0.090110,-0.090319,-0.090520,-0.090713,-0.090899,-0.091078,-0.091250,-0.091416,-0.091576,-0.091729,-0.091877,-0.092222,-0.092535,-0.092818,-0.093072,-0.093299,-0.093501,-0.093676,-0.093827,-0.093952,-0.094051,-0.094123,-0.094167,-0.094181,-0.094164,-0.094112,-0.094023,-0.093891,-0.093712,-0.093480,-0.093187,-0.092822,-0.092374,-0.091825,-0.091157,-0.090340,-0.089338,-0.088098,-0.086545,-0.085812,-0.085002,-0.084103,-0.083099,-0.081972,-0.080698,-0.079245,-0.077572,-0.075621,-0.073305,-0.070497,-0.066982,-0.064850,-0.062371,-0.059422,-0.057713,-0.055802,-0.054970,-0.054093,-0.053167,-0.052186,-0.051143,-0.050031,-0.048842,-0.048214,-0.047563,-0.046887,-0.046540,-0.046185,-0.045824,-0.045750,-0.045677,-0.045603,-0.045529,-0.045454,-0.045304,-0.045151,-0.045072,-0.044992},
{-0.264390,-0.266773,-0.267754,-0.268435,-0.268620,-0.268657,-0.268678,-0.268691,-0.268699,-0.268704,-0.268721,-0.268737,-0.268756,-0.268796,-0.268835,-0.268875,-0.268954,-0.269033,-0.269112,-0.269190,-0.269268,-0.269346,-0.269423,-0.269615,-0.269805,-0.270179,-0.270546,-0.270905,-0.271603,-0.272272,-0.272916,-0.273534,-0.274128,-0.274699,-0.275248,-0.275776,-0.276283,-0.276770,-0.277238,-0.277688,-0.278737,-0.279683,-0.280534,-0.281295,-0.281970,-0.282563,-0.283075,-0.283507,-0.283860,-0.284131,-0.284318,-0.284418,-0.284425,-0.284334,-0.284137,-0.283822,-0.283378,-0.282788,-0.282034,-0.281092,-0.279931,-0.278514,-0.276792,-0.274702,-0.272161,-0.269054,-0.265225,-0.260445,-0.258194,-0.255707,-0.252948,-0.249872,-0.246422,-0.242524,-0.238083,-0.232970,-0.227005,-0.219928,-0.211340,-0.200582,-0.194047,-0.186442,-0.177398,-0.172168,-0.166341,-0.163817,-0.161172,-0.158397,-0.155488,-0.152440,-0.149256,-0.145944,-0.144246,-0.142522,-0.140776,-0.139896,-0.139012,-0.138124,-0.137946,-0.137768,-0.137590,-0.137412,-0.137233,-0.136876,-0.136519,-0.136340,-0.136160},
{-0.454114,-0.454188,-0.454117,-0.453995,-0.453956,-0.453954,-0.453958,-0.453964,-0.453971,-0.453979,-0.454018,-0.454053,-0.454087,-0.454156,-0.454225,-0.454294,-0.454431,-0.454568,-0.454704,-0.454839,-0.454974,-0.455109,-0.455243,-0.455576,-0.455906,-0.456556,-0.457193,-0.457819,-0.459033,-0.460200,-0.461321,-0.462398,-0.463432,-0.464425,-0.465378,-0.466292,-0.467169,-0.468009,-0.468815,-0.469587,-0.471377,-0.472977,-0.474401,-0.475659,-0.476759,-0.477707,-0.478508,-0.479163,-0.479672,-0.480035,-0.480247,-0.480302,-0.480193,-0.479908,-0.479434,-0.478754,-0.477847,-0.476685,-0.475236,-0.473460,-0.471304,-0.468706,-0.465581,-0.461823,-0.457290,-0.451789,-0.445051,-0.436686,-0.432761,-0.428434,-0.423641,-0.418305,-0.412329,-0.405585,-0.397909,-0.389076,-0.378772,-0.366539,-0.351667,-0.332968,-0.321562,-0.308233,-0.292322,-0.283123,-0.272956,-0.268620,-0.264155,-0.259609,-0.255066,-0.250638,-0.246403,-0.242355,-0.240406,-0.238508,-0.236662,-0.235759,-0.234868,-0.233989,-0.233815,-0.233641,-0.233468,-0.233296,-0.233123,-0.232781,-0.232440,-0.232270,-0.232101},
{-0.648389,-0.648069,-0.648032,-0.648120,-0.648171,-0.648183,-0.648191,-0.648199,-0.648206,-0.648215,-0.648264,-0.648315,-0.648365,-0.648465,-0.648565,-0.648665,-0.648863,-0.649062,-0.649259,-0.649456,-0.649653,-0.649848,-0.650044,-0.650528,-0.651009,-0.651959,-0.652892,-0.653808,-0.655592,-0.657311,-0.658964,-0.660553,-0.662079,-0.663543,-0.664946,-0.666290,-0.667577,-0.668807,-0.669982,-0.671104,-0.673687,-0.675969,-0.677968,-0.679703,-0.681185,-0.682426,-0.683433,-0.684210,-0.684759,-0.685079,-0.685164,-0.685008,-0.684600,-0.683924,-0.682962,-0.681688,-0.680073,-0.678078,-0.675656,-0.672749,-0.669283,-0.665166,-0.660278,-0.654466,-0.647525,-0.639177,-0.629034,-0.616531,-0.610691,-0.604267,-0.597168,-0.589279,-0.580458,-0.570517,-0.559212,-0.546206,-0.531024,-0.512964,-0.490908,-0.462919,-0.445629,-0.425112,-0.399954,-0.384908,-0.367650,-0.360049,-0.352082,-0.342567,-0.318968,-0.290453,-0.258664,-0.222654,-0.202571,-0.180561,-0.155823,-0.141995,-0.126777,-0.109593,-0.105838,-0.101951,-0.097915,-0.093712,-0.089318,-0.079830,-0.069083,-0.063040,-0.056364},
{-0.853446,-0.853813,-0.853876,-0.853807,-0.853824,-0.853845,-0.853864,-0.853880,-0.853893,-0.853905,-0.853966,-0.854032,-0.854097,-0.854226,-0.854356,-0.854485,-0.854743,-0.855000,-0.855257,-0.855513,-0.855768,-0.856023,-0.856277,-0.856910,-0.857539,-0.858783,-0.860011,-0.861221,-0.863587,-0.865877,-0.868089,-0.870221,-0.872273,-0.874244,-0.876134,-0.877943,-0.879673,-0.881325,-0.882899,-0.884397,-0.887822,-0.890809,-0.893382,-0.895565,-0.897376,-0.898832,-0.899944,-0.900719,-0.901163,-0.901274,-0.901048,-0.900477,-0.899547,-0.898239,-0.896528,-0.894382,-0.891761,-0.888616,-0.884884,-0.880488,-0.875329,-0.869287,-0.862201,-0.853868,-0.844015,-0.832273,-0.818121,-0.800806,-0.792753,-0.783915,-0.774169,-0.763359,-0.751287,-0.737698,-0.722247,-0.704463,-0.683664,-0.658816,-0.628211,-0.588666,-0.563573,-0.532655,-0.491591,-0.463983,-0.427824,-0.409990,-0.389707,-0.367796,-0.356393,-0.346180,-0.324863,-0.280112,-0.254936,-0.227115,-0.195789,-0.178303,-0.159092,-0.137437,-0.132711,-0.127819,-0.122742,-0.117457,-0.111934,-0.100014,-0.086524,-0.078944,-0.070572},
{-1.072047,-1.071660,-1.071696,-1.071852,-1.071838,-1.071847,-1.071867,-1.071889,-1.071908,-1.071924,-1.071994,-1.072072,-1.072147,-1.072299,-1.072450,-1.072602,-1.072904,-1.073206,-1.073508,-1.073809,-1.074110,-1.074410,-1.074710,-1.075459,-1.076204,-1.077687,-1.079157,-1.080613,-1.083479,-1.086277,-1.089000,-1.091641,-1.094197,-1.096662,-1.099033,-1.101308,-1.103486,-1.105566,-1.107549,-1.109433,-1.113723,-1.117427,-1.120571,-1.123179,-1.125276,-1.126882,-1.128014,-1.128684,-1.128897,-1.128656,-1.127956,-1.126788,-1.125136,-1.122977,-1.120280,-1.117006,-1.113105,-1.108514,-1.103154,-1.096929,-1.089715,-1.081356,-1.071653,-1.060343,-1.047082,-1.031397,-1.012625,-0.989793,-0.979214,-0.967624,-0.954863,-0.940725,-0.924950,-0.907196,-0.887000,-0.863715,-0.836390,-0.803542,-0.762631,-0.708697,-0.673728,-0.630147,-0.573902,-0.539636,-0.500193,-0.482250,-0.462281,-0.439394,-0.412490,-0.380102,-0.351248,-0.347751,-0.343291,-0.343798,-0.342229,-0.342109,-0.341525,-0.341141,-0.341022,-0.340957,-0.340861,-0.340789,-0.340701,-0.340537,-0.340374,-0.340293,-0.340212},
{-1.300896,-1.301336,-1.301154,-1.301182,-1.301235,-1.301228,-1.301239,-1.301262,-1.301287,-1.301306,-1.301379,-1.301462,-1.301542,-1.301704,-1.301865,-1.302026,-1.302349,-1.302672,-1.302995,-1.303318,-1.303641,-1.303964,-1.304287,-1.305095,-1.305902,-1.307515,-1.309125,-1.310729,-1.313916,-1.317065,-1.320162,-1.323196,-1.326158,-1.329037,-1.331825,-1.334515,-1.337102,-1.339582,-1.341951,-1.344206,-1.349342,-1.353760,-1.357472,-1.360501,-1.362870,-1.364602,-1.365717,-1.366229,-1.366147,-1.365474,-1.364207,-1.362333,-1.359835,-1.356686,-1.352849,-1.348276,-1.342906,-1.336663,-1.329452,-1.321152,-1.311612,-1.300640,-1.287991,-1.273342,-1.256266,-1.236177,-1.212248,-1.183263,-1.169862,-1.155197,-1.139060,-1.121190,-1.101249,-1.078793,-1.053212,-1.023645,-0.988819,-0.946728,-0.894001,-0.824570,-0.780378,-0.727096,-0.660767,-0.620196,-0.572343,-0.550699,-0.527420,-0.502221,-0.474283,-0.441723,-0.401557,-0.347751,-0.343291,-0.343798,-0.342229,-0.342109,-0.341525,-0.341141,-0.341022,-0.340957,-0.340861,-0.340789,-0.340701,-0.340537,-0.340374,-0.340293,-0.340212},
{-1.539724,-1.539542,-1.539848,-1.539693,-1.539814,-1.539804,-1.539800,-1.539820,-1.539849,-1.539871,-1.539941,-1.540021,-1.540102,-1.540260,-1.540419,-1.540579,-1.540898,-1.541217,-1.541537,-1.541858,-1.542178,-1.542500,-1.542821,-1.543628,-1.544436,-1.546060,-1.547692,-1.549330,-1.552618,-1.555910,-1.559191,-1.562447,-1.565662,-1.568822,-1.571913,-1.574923,-1.577839,-1.580653,-1.583357,-1.585943,-1.591863,-1.596971,-1.601252,-1.604711,-1.607367,-1.609238,-1.610346,-1.610704,-1.610324,-1.609208,-1.607352,-1.604743,-1.601360,-1.597171,-1.592132,-1.586186,-1.579262,-1.571268,-1.562088,-1.551580,-1.539561,-1.525801,-1.510003,-1.491777,-1.470605,-1.445775,-1.416275,-1.380611,-1.364135,-1.346106,-1.326267,-1.304287,-1.279742,-1.252065,-1.220484,-1.183911,-1.140760,-1.088628,-1.023770,-0.940046,-0.887831,-0.825435,-0.748211,-0.701587,-0.647081,-0.622271,-0.595247,-0.565632,-0.533066,-0.497000,-0.455359,-0.402452,-0.368523,-0.320551,-0.276061,-0.251386,-0.224156,-0.193441,-0.186744,-0.179816,-0.172632,-0.165157,-0.157350,-0.140519,-0.121500,-0.110824,-0.099042}};
  const double imomegaqnm41[8][107] = {{-0.070745,-0.079568,-0.082950,-0.084642,-0.084972,-0.085040,-0.085086,-0.085120,-0.085146,-0.085167,-0.085233,-0.085268,-0.085292,-0.085328,-0.085359,-0.085389,-0.085448,-0.085506,-0.085564,-0.085621,-0.085678,-0.085734,-0.085791,-0.085929,-0.086065,-0.086330,-0.086585,-0.086831,-0.087299,-0.087736,-0.088145,-0.088528,-0.088888,-0.089226,-0.089544,-0.089844,-0.090127,-0.090394,-0.090646,-0.090885,-0.091427,-0.091900,-0.092313,-0.092673,-0.092986,-0.093256,-0.093488,-0.093683,-0.093844,-0.093972,-0.094068,-0.094132,-0.094164,-0.094164,-0.094130,-0.094060,-0.093951,-0.093801,-0.093605,-0.093357,-0.093051,-0.092678,-0.092226,-0.091682,-0.091027,-0.090235,-0.089273,-0.088095,-0.087548,-0.086951,-0.086295,-0.085574,-0.084777,-0.083892,-0.082904,-0.081793,-0.080536,-0.079099,-0.077437,-0.075488,-0.074378,-0.073160,-0.071812,-0.071083,-0.070312,-0.069991,-0.069662,-0.069325,-0.068981,-0.068627,-0.068265,-0.067893,-0.067704,-0.067512,-0.067318,-0.067219,-0.067120,-0.067018,-0.066997,-0.066976,-0.066954,-0.066932,-0.066909,-0.066863,-0.066835,-0.066862,-0.067010},
{-0.255428,-0.256296,-0.256641,-0.256879,-0.256948,-0.256965,-0.256978,-0.256989,-0.256998,-0.257007,-0.257050,-0.257094,-0.257139,-0.257228,-0.257318,-0.257407,-0.257585,-0.257761,-0.257936,-0.258110,-0.258282,-0.258454,-0.258624,-0.259045,-0.259459,-0.260266,-0.261046,-0.261801,-0.263238,-0.264584,-0.265846,-0.267031,-0.268146,-0.269194,-0.270181,-0.271111,-0.271988,-0.272817,-0.273599,-0.274339,-0.276017,-0.277478,-0.278752,-0.279860,-0.280821,-0.281648,-0.282353,-0.282944,-0.283428,-0.283809,-0.284090,-0.284271,-0.284354,-0.284334,-0.284209,-0.283973,-0.283618,-0.283133,-0.282505,-0.281718,-0.280751,-0.279577,-0.278162,-0.276461,-0.274418,-0.271957,-0.268974,-0.265325,-0.263635,-0.261789,-0.259765,-0.257540,-0.255084,-0.252358,-0.249318,-0.245906,-0.242047,-0.237646,-0.232573,-0.226657,-0.223312,-0.219664,-0.215673,-0.213535,-0.211297,-0.210371,-0.209429,-0.208469,-0.207491,-0.206495,-0.205481,-0.204448,-0.203925,-0.203397,-0.202865,-0.202597,-0.202327,-0.202057,-0.202003,-0.201949,-0.201894,-0.201840,-0.201786,-0.201677,-0.201564,-0.201502,-0.201427},
{-0.432356,-0.432408,-0.432395,-0.432382,-0.432399,-0.432413,-0.432427,-0.432443,-0.432458,-0.432474,-0.432550,-0.432626,-0.432701,-0.432852,-0.433002,-0.433151,-0.433449,-0.433746,-0.434041,-0.434334,-0.434626,-0.434916,-0.435204,-0.435918,-0.436623,-0.438002,-0.439342,-0.440644,-0.443137,-0.445486,-0.447701,-0.449788,-0.451754,-0.453608,-0.455356,-0.457005,-0.458560,-0.460029,-0.461416,-0.462727,-0.465696,-0.468276,-0.470516,-0.472456,-0.474129,-0.475560,-0.476769,-0.477773,-0.478583,-0.479207,-0.479651,-0.479917,-0.480003,-0.479908,-0.479624,-0.479142,-0.478447,-0.477524,-0.476348,-0.474891,-0.473116,-0.470978,-0.468417,-0.465358,-0.461701,-0.457315,-0.452021,-0.445570,-0.442589,-0.439338,-0.435779,-0.431871,-0.427562,-0.422789,-0.417473,-0.411520,-0.404805,-0.397177,-0.388443,-0.378379,-0.372777,-0.366767,-0.360348,-0.356992,-0.353546,-0.352145,-0.350732,-0.349307,-0.347872,-0.346428,-0.344975,-0.343514,-0.342781,-0.342047,-0.341311,-0.340943,-0.340574,-0.340206,-0.340132,-0.340058,-0.339984,-0.339910,-0.339836,-0.339689,-0.339542,-0.339469,-0.339396},
{-0.613987,-0.613908,-0.613922,-0.613986,-0.614035,-0.614055,-0.614075,-0.614096,-0.614116,-0.614137,-0.614241,-0.614346,-0.614451,-0.614659,-0.614867,-0.615075,-0.615490,-0.615904,-0.616315,-0.616726,-0.617135,-0.617542,-0.617948,-0.618956,-0.619955,-0.621922,-0.623847,-0.625731,-0.629369,-0.632834,-0.636125,-0.639246,-0.642201,-0.644997,-0.647640,-0.650139,-0.652500,-0.654730,-0.656836,-0.658826,-0.663330,-0.667232,-0.670606,-0.673512,-0.676001,-0.678111,-0.679875,-0.681318,-0.682458,-0.683309,-0.683879,-0.684172,-0.684189,-0.683924,-0.683370,-0.682510,-0.681328,-0.679796,-0.677882,-0.675543,-0.672726,-0.669363,-0.665368,-0.660629,-0.655000,-0.648289,-0.640231,-0.630458,-0.625957,-0.621054,-0.615698,-0.609825,-0.603360,-0.596211,-0.588266,-0.579387,-0.569409,-0.558136,-0.545364,-0.530960,-0.523178,-0.515098,-0.506854,-0.502721,-0.498611,-0.496978,-0.495353,-0.493738,-0.492133,-0.490540,-0.488960,-0.487394,-0.486616,-0.485842,-0.485072,-0.484688,-0.484306,-0.483924,-0.483848,-0.483772,-0.483695,-0.483619,-0.483543,-0.483391,-0.483239,-0.483163,-0.483089},
{-0.803913,-0.804044,-0.804080,-0.804106,-0.804156,-0.804184,-0.804211,-0.804237,-0.804263,-0.804288,-0.804417,-0.804546,-0.804674,-0.804932,-0.805189,-0.805446,-0.805959,-0.806473,-0.806985,-0.807497,-0.808007,-0.808518,-0.809027,-0.810297,-0.811561,-0.814070,-0.816550,-0.818997,-0.823783,-0.828406,-0.832850,-0.837105,-0.841166,-0.845032,-0.848705,-0.852188,-0.855489,-0.858612,-0.861566,-0.864358,-0.870678,-0.876142,-0.880850,-0.884884,-0.888315,-0.891198,-0.893579,-0.895493,-0.896969,-0.898027,-0.898678,-0.898931,-0.898787,-0.898239,-0.897277,-0.895882,-0.894028,-0.891682,-0.888798,-0.885319,-0.881174,-0.876270,-0.870490,-0.863682,-0.855648,-0.846124,-0.834750,-0.821019,-0.814714,-0.807856,-0.800375,-0.792184,-0.783178,-0.773233,-0.762196,-0.749887,-0.736102,-0.720635,-0.703395,-0.684742,-0.675289,-0.666104,-0.657406,-0.653282,-0.649319,-0.647780,-0.646267,-0.644780,-0.643319,-0.641885,-0.640475,-0.639091,-0.638408,-0.637732,-0.637061,-0.636729,-0.636397,-0.636067,-0.636001,-0.635936,-0.635870,-0.635804,-0.635739,-0.635608,-0.635477,-0.635412,-0.635344},
{-1.004519,-1.004422,-1.004453,-1.004545,-1.004593,-1.004620,-1.004649,-1.004679,-1.004709,-1.004738,-1.004881,-1.005025,-1.005169,-1.005456,-1.005744,-1.006032,-1.006609,-1.007186,-1.007764,-1.008342,-1.008921,-1.009500,-1.010080,-1.011530,-1.012983,-1.015889,-1.018793,-1.021690,-1.027440,-1.033098,-1.038626,-1.043993,-1.049176,-1.054156,-1.058924,-1.063474,-1.067804,-1.071917,-1.075817,-1.079509,-1.087881,-1.095121,-1.101345,-1.106658,-1.111150,-1.114895,-1.117955,-1.120376,-1.122199,-1.123448,-1.124145,-1.124298,-1.123911,-1.122977,-1.121482,-1.119405,-1.116711,-1.113357,-1.109287,-1.104426,-1.098681,-1.091935,-1.084033,-1.074781,-1.063919,-1.051102,-1.035857,-1.017518,-1.009112,-0.999979,-0.990022,-0.979125,-0.967148,-0.953922,-0.939246,-0.922887,-0.904602,-0.884255,-0.862248,-0.840921,-0.831775,-0.823609,-0.816386,-0.813081,-0.809960,-0.808759,-0.807585,-0.806435,-0.805310,-0.804207,-0.803127,-0.802069,-0.801548,-0.801031,-0.800520,-0.800267,-0.800014,-0.799763,-0.799713,-0.799663,-0.799613,-0.799563,-0.799513,-0.799413,-0.799313,-0.799263,-0.799218},
{-1.215956,-1.216148,-1.216128,-1.216170,-1.216241,-1.216264,-1.216291,-1.216321,-1.216352,-1.216383,-1.216528,-1.216676,-1.216822,-1.217117,-1.217412,-1.217708,-1.218300,-1.218894,-1.219490,-1.220087,-1.220687,-1.221288,-1.221891,-1.223404,-1.224928,-1.228002,-1.231108,-1.234240,-1.240562,-1.246918,-1.253256,-1.259525,-1.265676,-1.271670,-1.277473,-1.283063,-1.288423,-1.293543,-1.298421,-1.303055,-1.313603,-1.322749,-1.330611,-1.337310,-1.342952,-1.347629,-1.351418,-1.354381,-1.356565,-1.358007,-1.358731,-1.358751,-1.358072,-1.356686,-1.354578,-1.351719,-1.348070,-1.343576,-1.338166,-1.331748,-1.324207,-1.315393,-1.305115,-1.293126,-1.279099,-1.262596,-1.243013,-1.219489,-1.208710,-1.196998,-1.184222,-1.170228,-1.154824,-1.137778,-1.118809,-1.097590,-1.073814,-1.047555,-1.021630,-1.006614,-0.999428,-0.994254,-0.989417,-0.987226,-0.985098,-0.984273,-0.983473,-0.982685,-0.981910,-0.981149,-0.980402,-0.979666,-0.979303,-0.978942,-0.978585,-0.978407,-0.978230,-0.978054,-0.978019,-0.977984,-0.977949,-0.977914,-0.977879,-0.977808,-0.977740,-0.977704,-0.977662},
{-1.438671,-1.438539,-1.438690,-1.438687,-1.438772,-1.438797,-1.438817,-1.438842,-1.438873,-1.438904,-1.439041,-1.439182,-1.439321,-1.439601,-1.439882,-1.440163,-1.440728,-1.441295,-1.441865,-1.442438,-1.443013,-1.443590,-1.444170,-1.445632,-1.447109,-1.450111,-1.453172,-1.456292,-1.462688,-1.469264,-1.475970,-1.482750,-1.489541,-1.496283,-1.502917,-1.509394,-1.515673,-1.521727,-1.527534,-1.533082,-1.545796,-1.556882,-1.566435,-1.574574,-1.581418,-1.587074,-1.591631,-1.595164,-1.597732,-1.599378,-1.600134,-1.600016,-1.599031,-1.597171,-1.594416,-1.590734,-1.586076,-1.580376,-1.573548,-1.565481,-1.556033,-1.545023,-1.532216,-1.517307,-1.499894,-1.479430,-1.455158,-1.425982,-1.412595,-1.398026,-1.382103,-1.364610,-1.345276,-1.323754,-1.299592,-1.272190,-1.240773,-1.204723,-1.125253,-0.938477,-0.838838,-0.734651,-0.622895,-0.562606,-0.497864,-0.470296,-0.441499,-0.411211,-0.379069,-0.646318,-0.573546,-0.492714,-0.447938,-0.398980,-0.344066,-0.313415,-0.279721,-0.241717,-0.233418,-0.224829,-0.215913,-0.206630,-0.196927,-0.175980,-0.152268,-0.138939,-0.124215}};
  const double imomegaqnm40[8][107] = {{-1.438671,-1.438539,-1.438690,-1.438687,-1.438772,-1.438797,-1.438817,-1.438842,-1.438873,-1.438904,-1.439041,-1.439182,-1.439321,-1.439601,-1.439882,-1.440163,-1.440728,-1.441295,-1.441865,-1.442438,-1.443013,-1.443590,-1.444170,-1.445632,-1.447109,-1.450111,-1.453172,-1.456292,-1.462688,-1.469264,-1.475970,-1.482750,-1.489541,-1.496283,-1.502917,-1.509394,-1.515673,-1.521727,-1.527534,-1.533082,-1.545796,-1.556882,-1.566435,-1.574574,-1.581418,-1.587074,-1.591631,-1.595164,-1.597732,-1.599378,-1.600134,-1.600016,-1.599031,-1.597171,-0.094147,-0.094096,-0.094010,-0.093888,-0.093727,-0.093524,-0.093276,-0.092977,-0.092621,-0.092200,-0.091703,-0.091116,-0.090423,-0.089598,-0.089224,-0.088822,-0.088389,-0.087921,-0.087414,-0.086864,-0.086265,-0.085611,-0.084894,-0.084105,-0.083233,-0.082264,-0.081737,-0.081180,-0.080588,-0.080279,-0.079960,-0.079829,-0.079697,-0.079563,-0.079427,-0.079290,-0.079151,-0.079010,-0.078938,-0.078867,-0.078793,-0.078755,-0.078713,-0.078663,-0.078651,-0.078639,-0.078626,-0.078613,-0.078601,-0.078589,-0.078624,-0.078591,-0.077756},
{-1.438671,-1.438539,-1.438690,-1.438687,-1.438772,-1.438797,-1.438817,-1.438842,-1.438873,-1.438904,-1.439041,-1.439182,-1.439321,-1.439601,-1.439882,-1.440163,-1.440728,-1.441295,-1.441865,-1.442438,-1.443013,-1.443590,-1.444170,-1.445632,-1.447109,-1.450111,-1.453172,-1.456292,-1.462688,-1.469264,-1.475970,-1.482750,-1.489541,-1.496283,-1.502917,-1.509394,-1.515673,-1.521727,-1.527534,-1.533082,-1.545796,-1.556882,-1.566435,-1.574574,-1.581418,-1.587074,-1.591631,-1.595164,-1.597732,-1.599378,-1.600134,-1.600016,-1.599031,-1.597171,-0.284282,-0.284123,-0.283856,-0.283475,-0.282973,-0.282342,-0.281569,-0.280639,-0.279531,-0.278220,-0.276673,-0.274849,-0.272693,-0.270131,-0.268972,-0.267724,-0.266380,-0.264929,-0.263359,-0.261656,-0.259805,-0.257787,-0.255580,-0.253159,-0.250493,-0.247547,-0.245957,-0.244282,-0.242515,-0.241595,-0.240650,-0.240265,-0.239876,-0.239482,-0.239084,-0.238682,-0.238275,-0.237864,-0.237657,-0.237449,-0.237239,-0.237134,-0.237029,-0.236923,-0.236902,-0.236882,-0.236860,-0.236839,-0.236817,-0.236767,-0.236680,-0.236587,-0.236384},
{-1.438671,-1.438539,-1.438690,-1.438687,-1.438772,-1.438797,-1.438817,-1.438842,-1.438873,-1.438904,-1.439041,-1.439182,-1.439321,-1.439601,-1.439882,-1.440163,-1.440728,-1.441295,-1.441865,-1.442438,-1.443013,-1.443590,-1.444170,-1.445632,-1.447109,-1.450111,-1.453172,-1.456292,-1.462688,-1.469264,-1.475970,-1.482750,-1.489541,-1.496283,-1.502917,-1.509394,-1.515673,-1.521727,-1.527534,-1.533082,-1.545796,-1.556882,-1.566435,-1.574574,-1.581418,-1.587074,-1.591631,-1.595164,-1.597732,-1.599378,-1.600134,-1.600016,-1.599031,-1.597171,-0.479814,-0.479529,-0.479050,-0.478367,-0.477469,-0.476339,-0.474956,-0.473292,-0.471312,-0.468970,-0.466209,-0.462957,-0.459115,-0.454557,-0.452497,-0.450282,-0.447898,-0.445327,-0.442550,-0.439545,-0.436285,-0.432744,-0.428889,-0.424686,-0.420099,-0.415093,-0.412423,-0.409638,-0.406737,-0.405243,-0.403720,-0.403102,-0.402480,-0.401854,-0.401223,-0.400587,-0.399947,-0.399303,-0.398979,-0.398654,-0.398328,-0.398165,-0.398001,-0.397837,-0.397804,-0.397771,-0.397738,-0.397705,-0.397672,-0.397608,-0.397552,-0.397528,-0.397490},
{-1.438671,-1.438539,-1.438690,-1.438687,-1.438772,-1.438797,-1.438817,-1.438842,-1.438873,-1.438904,-1.439041,-1.439182,-1.439321,-1.439601,-1.439882,-1.440163,-1.440728,-1.441295,-1.441865,-1.442438,-1.443013,-1.443590,-1.444170,-1.445632,-1.447109,-1.450111,-1.453172,-1.456292,-1.462688,-1.469264,-1.475970,-1.482750,-1.489541,-1.496283,-1.502917,-1.509394,-1.515673,-1.521727,-1.527534,-1.533082,-1.545796,-1.556882,-1.566435,-1.574574,-1.581418,-1.587074,-1.591631,-1.595164,-1.597732,-1.599378,-1.600134,-1.600016,-1.599031,-1.597171,-0.683779,-0.683339,-0.682598,-0.681544,-0.680158,-0.678415,-0.676283,-0.673720,-0.670673,-0.667075,-0.662838,-0.657852,-0.651974,-0.645014,-0.641874,-0.638502,-0.634878,-0.630978,-0.626774,-0.622239,-0.617340,-0.612046,-0.606325,-0.600151,-0.593508,-0.586401,-0.582682,-0.578863,-0.574955,-0.572971,-0.570970,-0.570165,-0.569358,-0.568549,-0.567738,-0.566925,-0.566110,-0.565293,-0.564885,-0.564476,-0.564066,-0.563861,-0.563657,-0.563452,-0.563411,-0.563370,-0.563329,-0.563288,-0.563247,-0.563164,-0.563077,-0.563036,-0.563014},
{-1.438671,-1.438539,-1.438690,-1.438687,-1.438772,-1.438797,-1.438817,-1.438842,-1.438873,-1.438904,-1.439041,-1.439182,-1.439321,-1.439601,-1.439882,-1.440163,-1.440728,-1.441295,-1.441865,-1.442438,-1.443013,-1.443590,-1.444170,-1.445632,-1.447109,-1.450111,-1.453172,-1.456292,-1.462688,-1.469264,-1.475970,-1.482750,-1.489541,-1.496283,-1.502917,-1.509394,-1.515673,-1.521727,-1.527534,-1.533082,-1.545796,-1.556882,-1.566435,-1.574574,-1.581418,-1.587074,-1.591631,-1.595164,-1.597732,-1.599378,-1.600134,-1.600016,-1.599031,-1.597171,-0.898030,-0.897399,-0.896336,-0.894824,-0.892837,-0.890341,-0.887290,-0.883627,-0.879278,-0.874148,-0.868116,-0.861030,-0.852691,-0.842842,-0.838407,-0.833654,-0.828554,-0.823079,-0.817197,-0.810876,-0.804088,-0.796809,-0.789030,-0.780765,-0.772061,-0.763015,-0.758408,-0.753773,-0.749132,-0.746816,-0.744509,-0.743588,-0.742669,-0.741753,-0.740838,-0.739926,-0.739016,-0.738108,-0.737656,-0.737204,-0.736752,-0.736527,-0.736302,-0.736077,-0.736032,-0.735987,-0.735942,-0.735897,-0.735852,-0.735762,-0.735677,-0.735631,-0.735564},
{-1.438671,-1.438539,-1.438690,-1.438687,-1.438772,-1.438797,-1.438817,-1.438842,-1.438873,-1.438904,-1.439041,-1.439182,-1.439321,-1.439601,-1.439882,-1.440163,-1.440728,-1.441295,-1.441865,-1.442438,-1.443013,-1.443590,-1.444170,-1.445632,-1.447109,-1.450111,-1.453172,-1.456292,-1.462688,-1.469264,-1.475970,-1.482750,-1.489541,-1.496283,-1.502917,-1.509394,-1.515673,-1.521727,-1.527534,-1.533082,-1.545796,-1.556882,-1.566435,-1.574574,-1.581418,-1.587074,-1.591631,-1.595164,-1.597732,-1.599378,-1.600134,-1.600016,-1.599031,-1.597171,-1.122693,-1.121836,-1.120394,-1.118342,-1.115648,-1.112266,-1.108136,-1.103182,-1.097304,-1.090380,-1.082250,-1.072712,-1.061506,-1.048301,-1.042369,-1.036021,-1.029226,-1.021952,-1.014170,-1.005854,-0.996995,-0.987606,-0.977740,-0.967503,-0.957067,-0.946652,-0.941522,-0.936480,-0.931545,-0.929125,-0.926737,-0.925792,-0.924853,-0.923919,-0.922991,-0.922069,-0.921152,-0.920242,-0.919789,-0.919338,-0.918888,-0.918664,-0.918440,-0.918216,-0.918172,-0.918127,-0.918082,-0.918037,-0.917993,-0.917906,-0.917810,-0.917765,-0.917749},
{-1.438671,-1.438539,-1.438690,-1.438687,-1.438772,-1.438797,-1.438817,-1.438842,-1.438873,-1.438904,-1.439041,-1.439182,-1.439321,-1.439601,-1.439882,-1.440163,-1.440728,-1.441295,-1.441865,-1.442438,-1.443013,-1.443590,-1.444170,-1.445632,-1.447109,-1.450111,-1.453172,-1.456292,-1.462688,-1.469264,-1.475970,-1.482750,-1.489541,-1.496283,-1.502917,-1.509394,-1.515673,-1.521727,-1.527534,-1.533082,-1.545796,-1.556882,-1.566435,-1.574574,-1.581418,-1.587074,-1.591631,-1.595164,-1.597732,-1.599378,-1.600134,-1.600016,-1.599031,-1.597171,-1.356319,-1.355212,-1.353349,-1.350699,-1.347220,-1.342855,-1.337528,-1.331141,-1.323569,-1.314654,-1.304196,-1.291938,-1.277556,-1.260642,-1.253060,-1.244963,-1.236319,-1.227100,-1.217291,-1.206897,-1.195961,-1.184586,-1.172958,-1.161345,-1.150044,-1.139303,-1.134195,-1.129278,-1.124555,-1.122267,-1.120028,-1.119146,-1.118271,-1.117404,-1.116544,-1.115692,-1.114847,-1.114010,-1.113594,-1.113180,-1.112768,-1.112563,-1.112358,-1.112153,-1.112112,-1.112071,-1.112030,-1.111990,-1.111950,-1.111865,-1.111791,-1.111758,-1.111672},
{-1.438671,-1.438539,-1.438690,-1.438687,-1.438772,-1.438797,-1.438817,-1.438842,-1.438873,-1.438904,-1.439041,-1.439182,-1.439321,-1.439601,-1.439882,-1.440163,-1.440728,-1.441295,-1.441865,-1.442438,-1.443013,-1.443590,-1.444170,-1.445632,-1.447109,-1.450111,-1.453172,-1.456292,-1.462688,-1.469264,-1.475970,-1.482750,-1.489541,-1.496283,-1.502917,-1.509394,-1.515673,-1.521727,-1.527534,-1.533082,-1.545796,-1.556882,-1.566435,-1.574574,-1.581418,-1.587074,-1.591631,-1.595164,-1.597732,-1.599378,-1.600134,-1.600016,-1.599031,-1.597171,-1.596716,-1.595345,-1.593037,-1.589757,-1.585451,-1.580049,-1.573457,-1.565556,-1.556192,-1.545172,-1.532249,-1.517110,-1.499363,-1.478531,-1.469216,-1.459292,-1.448735,-1.437538,-1.425727,-1.413381,-1.400670,-1.387885,-1.375423,-1.363657,-1.352804,-1.342937,-1.338366,-1.334022,-1.329895,-1.327908,-1.325970,-1.325208,-1.324453,-1.323706,-1.322966,-1.322233,-1.321507,-1.320788,-1.320431,-1.320076,-1.319722,-1.319546,-1.319370,-1.319195,-1.319159,-1.319124,-1.319091,-1.319057,-1.319021,-1.318947,-1.318885,-1.318817,-1.318855}};

  /* Stuff for interpolating the data */
  gsl_spline    *spline = NULL;
  gsl_interp_accel *acc = NULL;

  const double (*reOm)[107] = NULL;
  const double (*imOm)[107] = NULL;

  switch ( l ) {
  case (2):
    if ( m == 2 ) {
      reOm = reomegaqnm22;
      imOm = imomegaqnm22;
    }
    else
      if ( m == 1 ) {
	reOm = reomegaqnm21;
	imOm = imomegaqnm21;
      }
      else
	if ( m == 0 ) {
	  reOm = reomegaqnm20;
	  imOm = imomegaqnm20;
	}
        else {
	  XLALPrintError( "Unsupported combination of l, m (%d, %d). It must be 0<=m<=l\n", l, m );
	  XLAL_ERROR( XLAL_EINVAL );
	}
    break;
  case (3):
    if ( m == 3 ) {
      reOm = reomegaqnm33;
      imOm = imomegaqnm33;
    }
    else 
      if ( m == 2 ) {
	reOm = reomegaqnm32;
	imOm = imomegaqnm32;
      }
      else
	if ( m == 1 ) {
	  reOm = reomegaqnm31;
	  imOm = imomegaqnm31;
	}
	else
	  if ( m == 0 ) {
	    reOm = reomegaqnm30;
	    imOm = imomegaqnm30;
	  }
          else {
	    XLALPrintError( "Unsupported combination of l, m (%d, %d). It must be 0<=m<=l\n", l, m );
	    XLAL_ERROR( XLAL_EINVAL );
	  }
    break;
  case (4):
    if ( m == 4 ) {
      reOm = reomegaqnm44;
      imOm = imomegaqnm44;
    }
    else
      if ( m == 3 ) {
	reOm = reomegaqnm43;
	imOm = imomegaqnm43;
      }
      else 
	if ( m == 2 ) {
	  reOm = reomegaqnm42;
	  imOm = imomegaqnm42;
	}
	else
	  if ( m == 1 ) {
	    reOm = reomegaqnm41;
	    imOm = imomegaqnm41;
	  }
	  else
	    if ( m == 0 ) {
	      reOm = reomegaqnm40;
	      imOm = imomegaqnm40;
	    }
	    else {
	      XLALPrintError( "Unsupported combination of l, m (%d, %d). It must be 0<=m<=l\n", l, m );
	      XLAL_ERROR( XLAL_EINVAL );
	    }
    break;
  default:
    XLALPrintError( "Unsupported combination of l, m (%d, %d)\n", l, m );
    XLAL_ERROR( XLAL_EINVAL );
    break;
  };

  spline = gsl_spline_alloc( gsl_interp_cspline, 107 );
  acc    = gsl_interp_accel_alloc();
  for ( idx = 0; idx < (int)modefreqs->length; idx++ ) {
    gsl_spline_init( spline, afinallist, reOm[idx], 107 );
    gsl_interp_accel_reset( acc );
    
    modefreqs->data[idx] = gsl_spline_eval( spline, finalSpin, acc );

    gsl_spline_init( spline, afinallist, imOm[idx], 107 );
    gsl_interp_accel_reset( acc );

    modefreqs->data[idx] += I*gsl_spline_eval( spline, finalSpin, acc );

    /* Scale by the appropriate mass factors */
    modefreqs->data[idx] *= 1./ finalMass / (totalMass * LAL_MTSUN_SI);
  }
  /* Free memory and exit */

  gsl_spline_free( spline );
  gsl_interp_accel_free( acc );

  return XLAL_SUCCESS;
}
