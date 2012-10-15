/*
*  Copyright (C) 2011 Drew Keppel
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

#include <lal/LALConstants.h>
#include <lal/LALAtomicDatatypes.h>

#ifdef __GNUC__
#define UNUSED __attribute__ ((unused))
#else
#define UNUSED
#endif

/**
 * Computes the PN Coefficients for using in the PN energy equation.
 *
 * Terms given in equation 3.1 of: Alessandra Buonanno, Bala R Iyer, Evan
 * Ochsner, Yi Pan, and B S Sathyaprakash, "Comparison of post-Newtonian
 * templates for compact binary inspiral signals in gravitational-wave
 * detectors", Phys. Rev. D 80, 084043 (2009), arXiv:0907.0700v1
 */

static REAL8 UNUSED
XLALSimInspiralPNEnergy_0PNCoeff(
	REAL8 eta)
{
	return -eta / 2.0;
}

static REAL8 UNUSED
XLALSimInspiralPNEnergy_2PNCoeff(
	REAL8 eta)
{
	return -(3.0/4.0 + 1.0/12.0 * eta);
}

static REAL8 UNUSED
XLALSimInspiralPNEnergy_4PNCoeff(
	REAL8 eta)
{
	return -(27.0/8.0 - 19.0/8.0 * eta + 1./24.0 * eta*eta);
}

static REAL8 UNUSED
XLALSimInspiralPNEnergy_6PNCoeff(
	REAL8 eta)
{
	return -(67.5/6.4 - (344.45/5.76 - 20.5/9.6 * LAL_PI*LAL_PI) * eta + 15.5/9.6 * eta*eta + 3.5/518.4 * eta*eta*eta);
}


/**
 * Computes the PN Coefficients for using in the TaylorT1 flux equation.
 *
 * Terms given in equation 3.2 of: Alessandra Buonanno, Bala R Iyer, Evan
 * Ochsner, Yi Pan, and B S Sathyaprakash, "Comparison of post-Newtonian
 * templates for compact binary inspiral signals in gravitational-wave
 * detectors", Phys. Rev. D 80, 084043 (2009), arXiv:0907.0700v1
 */

static REAL8 UNUSED
XLALSimInspiralTaylorT1Flux_0PNCoeff(
	REAL8 eta)
{
	return 32.0 * eta*eta / 5.0;
}

static REAL8 UNUSED
XLALSimInspiralTaylorT1Flux_2PNCoeff(
	REAL8 eta)
{
	return -(12.47/3.36 + 3.5/1.2 * eta);
}

static REAL8 UNUSED
XLALSimInspiralTaylorT1Flux_3PNCoeff(
	REAL8 UNUSED eta)
{
	return 4.0 * LAL_PI;
}

static REAL8 UNUSED
XLALSimInspiralTaylorT1Flux_4PNCoeff(
	REAL8 eta)
{
	return -(44.711/9.072 - 92.71/5.04 * eta - 6.5/1.8 * eta*eta);
}

static REAL8 UNUSED
XLALSimInspiralTaylorT1Flux_5PNCoeff(
	REAL8 eta)
{
	return -(81.91/6.72 + 58.3/2.4 * eta) * LAL_PI;
}

static REAL8 UNUSED
XLALSimInspiralTaylorT1Flux_6PNCoeff(
	REAL8 eta)
{
	return (664.3739519/6.9854400 + 16.0/3.0 * LAL_PI*LAL_PI - 17.12/1.05 * LAL_GAMMA
		+ (4.1/4.8 * LAL_PI*LAL_PI - 134.543/7.776) * eta
		- 94.403/3.024 * eta*eta - 7.75/3.24 * eta*eta*eta);
}

static REAL8 UNUSED
XLALSimInspiralTaylorT1Flux_6PNLogCoeff(
	REAL8 UNUSED eta)
{
	return -8.56/1.05;
}

static REAL8 UNUSED
XLALSimInspiralTaylorT1Flux_7PNCoeff(
	REAL8 eta)
{
	return -(162.85/5.04 - 214.745/1.728 * eta - 193.385/3.024 * eta*eta) * LAL_PI;
}


/**
 * Computes the PN Coefficients for using in the TaylorT2 phasing equation.
 *
 * Terms given in equation 3.8a of: Alessandra Buonanno, Bala R Iyer, Evan
 * Ochsner, Yi Pan, and B S Sathyaprakash, "Comparison of post-Newtonian
 * templates for compact binary inspiral signals in gravitational-wave
 * detectors", Phys. Rev. D 80, 084043 (2009), arXiv:0907.0700v1
 */

static REAL8 UNUSED
XLALSimInspiralTaylorT2Phasing_0PNCoeff(
	REAL8 eta)
{
	return -1./(32.*eta);
}

static REAL8 UNUSED
XLALSimInspiralTaylorT2Phasing_2PNCoeff(
	REAL8 eta)
{
	return 3.715/1.008 + 5.5/1.2 * eta;
}

static REAL8 UNUSED
XLALSimInspiralTaylorT2Phasing_3PNCoeff(
	REAL8 UNUSED eta)
{
	return -10. * LAL_PI;
}

static REAL8 UNUSED
XLALSimInspiralTaylorT2Phasing_4PNCoeff(
	REAL8 eta)
{
	return 15.293365/1.016064 + 27.145/1.008 * eta + 30.85/1.44 * eta*eta;
}

static REAL8 UNUSED
XLALSimInspiralTaylorT2Phasing_5PNCoeff(
	REAL8 eta)
{
	return (386.45/6.72 - 65./8. * eta) * LAL_PI;
}

static REAL8 UNUSED
XLALSimInspiralTaylorT2Phasing_6PNCoeff(
	REAL8 eta)
{
	return 1234.8611926451/1.8776862720 - 160./3. * LAL_PI*LAL_PI - 171.2/2.1 * LAL_GAMMA
		+ (225.5/4.8 * LAL_PI*LAL_PI - 1573.7765635/1.2192768) * eta
		+ 76.055/6.912 * eta*eta - 127.825/5.184 * eta*eta*eta;
}

static REAL8 UNUSED
XLALSimInspiralTaylorT2Phasing_6PNLogCoeff(
	REAL8 UNUSED eta)
{
	return -85.6/2.1;
}

static REAL8 UNUSED
XLALSimInspiralTaylorT2Phasing_7PNCoeff(
	REAL8 eta)
{
	return (77.096675/2.032128 + 37.8515/1.2096 * eta - 74.045/6.048 * eta*eta) * LAL_PI;
}


/**
 * Computes the PN Coefficients for using in the TaylorT2 timing equation.
 *
 * Terms given in equation 3.8b of: Alessandra Buonanno, Bala R Iyer, Evan
 * Ochsner, Yi Pan, and B S Sathyaprakash, "Comparison of post-Newtonian
 * templates for compact binary inspiral signals in gravitational-wave
 * detectors", Phys. Rev. D 80, 084043 (2009), arXiv:0907.0700v1
 */

static REAL8 UNUSED
XLALSimInspiralTaylorT2Timing_0PNCoeff(
	REAL8 totalmass,
	REAL8 eta)
{
	totalmass *= LAL_G_SI / pow(LAL_C_SI, 3.0); /* convert totalmass from kilograms to seconds */
	return -5.*totalmass/(256.*eta);
}

static REAL8 UNUSED
XLALSimInspiralTaylorT2Timing_2PNCoeff(
	REAL8 eta)
{
	return 7.43/2.52 + 11./3. * eta;
}

static REAL8 UNUSED
XLALSimInspiralTaylorT2Timing_3PNCoeff(
	REAL8 UNUSED eta)
{
	return -32./5. * LAL_PI;;
}

static REAL8 UNUSED
XLALSimInspiralTaylorT2Timing_4PNCoeff(
	REAL8 eta)
{
	return 30.58673/5.08032 + 54.29/5.04*eta + 61.7/7.2*eta*eta;
}

static REAL8 UNUSED
XLALSimInspiralTaylorT2Timing_5PNCoeff(
	REAL8 eta)
{
	return -(77.29/2.52 -13./3.*eta) * LAL_PI;
}

static REAL8 UNUSED
XLALSimInspiralTaylorT2Timing_6PNCoeff(
	REAL8 eta)
{
	return -1005.2469856691/2.3471078400 + 128./3. * LAL_PI*LAL_PI + 68.48/1.05 * LAL_GAMMA
		+ (3147.553127/3.048192 - 45.1/1.2 * LAL_PI*LAL_PI) * eta
		- 15.211/1.728 * eta*eta + 25.565/1.296 * eta*eta*eta;
}

static REAL8 UNUSED
XLALSimInspiralTaylorT2Timing_6PNLogCoeff(
	REAL8 UNUSED eta)
{
	return 34.24/1.05;
}

static REAL8 UNUSED
XLALSimInspiralTaylorT2Timing_7PNCoeff(
	REAL8 eta)
{
	return (-154.19335/1.27008 - 757.03/7.56 * eta + 148.09/3.78 * eta*eta) * LAL_PI;
}


/**
 * Computes the PN Coefficients for using in the TaylorT3 phasing equation.
 *
 * Terms given in equation 3.10a of: Alessandra Buonanno, Bala R Iyer, Evan
 * Ochsner, Yi Pan, and B S Sathyaprakash, "Comparison of post-Newtonian
 * templates for compact binary inspiral signals in gravitational-wave
 * detectors", Phys. Rev. D 80, 084043 (2009), arXiv:0907.0700v1
 */


static REAL8 UNUSED
XLALSimInspiralTaylorT3Phasing_0PNCoeff(
	REAL8 eta)
{
	return -1./eta;
}

static REAL8 UNUSED
XLALSimInspiralTaylorT3Phasing_2PNCoeff(
	REAL8 eta)
{
	return 3.715/8.064 + 5.5/9.6 * eta;
}

static REAL8 UNUSED
XLALSimInspiralTaylorT3Phasing_3PNCoeff(
	REAL8 UNUSED eta)
{
	return -3./4. * LAL_PI;
}

static REAL8 UNUSED
XLALSimInspiralTaylorT3Phasing_4PNCoeff(
	REAL8 eta)
{
	return 9.275495/14.450688 + 2.84875/2.58048 * eta + 1.855/2.048 * eta*eta;
}

static REAL8 UNUSED
XLALSimInspiralTaylorT3Phasing_5PNCoeff(
	REAL8 eta)
{
	return (3.8645/2.1504 - 6.5/25.6 * eta) * LAL_PI;
}

static REAL8 UNUSED
XLALSimInspiralTaylorT3Phasing_6PNCoeff(
	REAL8 eta)
{
	return 83.1032450749357/5.7682522275840 - 5.3/4.0 * LAL_PI*LAL_PI - 10.7/5.6 * LAL_GAMMA
		+ (-126.510089885/4.161798144 + 2.255/2.048 * LAL_PI*LAL_PI) * eta
		+ 1.54565/18.35008 * eta*eta - 1.179625/1.769472 * eta*eta*eta;
}

static REAL8 UNUSED
XLALSimInspiralTaylorT3Phasing_6PNLogCoeff(
	REAL8 UNUSED eta)
{
	return -10.7/5.6;
}

static REAL8 UNUSED
XLALSimInspiralTaylorT3Phasing_7PNCoeff(
	REAL8 eta)
{
	return (1.88516689/1.73408256 + 4.88825/5.16096 * eta - 1.41769/5.16096 * eta*eta) * LAL_PI;
}


/**
 * Computes the PN Coefficients for using in the TaylorT3 frequency equation.
 *
 * Terms given in equation 3.10b of: Alessandra Buonanno, Bala R Iyer, Evan
 * Ochsner, Yi Pan, and B S Sathyaprakash, "Comparison of post-Newtonian
 * templates for compact binary inspiral signals in gravitational-wave
 * detectors", Phys. Rev. D 80, 084043 (2009), arXiv:0907.0700v1
 */

static REAL8 UNUSED
XLALSimInspiralTaylorT3Frequency_0PNCoeff(
	REAL8 totalmass)
{
	totalmass *= LAL_G_SI / pow(LAL_C_SI, 3.0); /* convert totalmass from kilograms to seconds */
	return 1. / (8. * LAL_PI * totalmass);
}

static REAL8 UNUSED
XLALSimInspiralTaylorT3Frequency_2PNCoeff(
	REAL8 eta)
{
	return 7.43/26.88 + 1.1/3.2 * eta;
}

static REAL8 UNUSED
XLALSimInspiralTaylorT3Frequency_3PNCoeff(
	REAL8 UNUSED eta)
{
	return -3./10. * LAL_PI;
}

static REAL8 UNUSED
XLALSimInspiralTaylorT3Frequency_4PNCoeff(
	REAL8 eta)
{
	return 1.855099/14.450688 + 5.6975/25.8048 * eta + 3.71/20.48 * eta*eta;
}

static REAL8 UNUSED
XLALSimInspiralTaylorT3Frequency_5PNCoeff(
	REAL8 eta)
{
	return (-7.729/21.504 + 1.3/25.6 * eta) * LAL_PI;
}

static REAL8 UNUSED
XLALSimInspiralTaylorT3Frequency_6PNCoeff(
	REAL8 eta)
{
	return -7.20817631400877/2.88412611379200 + 5.3/20.0 * LAL_PI*LAL_PI + 1.07/2.80 * LAL_GAMMA
		+ (25.302017977/4.161798144 - 4.51/20.48 * LAL_PI*LAL_PI) * eta
		- 3.0913/183.5008 * eta*eta + 2.35925/17.69472 * eta*eta*eta;
}

static REAL8 UNUSED
XLALSimInspiralTaylorT3Frequency_6PNLogCoeff(
	REAL8 UNUSED eta)
{
	return 1.07/2.80;
}

static REAL8 UNUSED
XLALSimInspiralTaylorT3Frequency_7PNCoeff(
	REAL8 eta)
{
	return (-1.88516689/4.33520640 - 9.7765/25.8048 * eta + 1.41769/12.90240 * eta*eta) * LAL_PI;
}


/**
 * Computes the PN Coefficients for using in the TaylorT4 angular acceleration
 * equation.
 *
 * Terms given in equation 3.6 of: Alessandra Buonanno, Bala R Iyer, Evan
 * Ochsner, Yi Pan, and B S Sathyaprakash, "Comparison of post-Newtonian
 * templates for compact binary inspiral signals in gravitational-wave
 * detectors", Phys. Rev. D 80, 084043 (2009), arXiv:0907.0700v1
 */

static REAL8 UNUSED
XLALSimInspiralTaylorT4AngularAccel_0PNCoeff(
	REAL8 totalmass,
	REAL8 eta)
{
	return 32.0 * eta / (5.0 * totalmass);
}

static REAL8 UNUSED
XLALSimInspiralTaylorT4AngularAccel_2PNCoeff(
	REAL8 eta)
{
	return -(7.43/3.36 + 11.0/4.0 * eta);
}

static REAL8 UNUSED
XLALSimInspiralTaylorT4AngularAccel_3PNCoeff(
	REAL8 UNUSED eta)
{
	return 4.0 * LAL_PI;
}

static REAL8 UNUSED
XLALSimInspiralTaylorT4AngularAccel_4PNCoeff(
	REAL8 eta)
{
	return (3.4103/1.8144 + 13.661/2.016 * eta + 5.9/1.8 * eta*eta);
}

static REAL8 UNUSED
XLALSimInspiralTaylorT4AngularAccel_5PNCoeff(
	REAL8 eta)
{
	return -(41.59/6.72 + 189.0/8.0 * eta) * LAL_PI;
}

static REAL8 UNUSED
XLALSimInspiralTaylorT4AngularAccel_6PNCoeff(
	REAL8 eta)
{
	return (164.47322263/1.39708800 + 16.0/3.0 * LAL_PI * LAL_PI 
		- 17.12/1.05 * LAL_GAMMA
		+ (45.1/4.8 * LAL_PI*LAL_PI - 561.98689/2.17728) * eta
		+ 5.41/8.96 * eta*eta - 5.605/2.592 * eta*eta*eta);
}

static REAL8 UNUSED
XLALSimInspiralTaylorT4AngularAccel_6PNLogCoeff(
	REAL8 UNUSED eta)
{
	return -8.56/1.05;
}

static REAL8 UNUSED
XLALSimInspiralTaylorT4AngularAccel_7PNCoeff(
	REAL8 eta)
{
	return -(4.415/4.032 - 358.675/6.048 * eta - 91.495/1.512 * eta*eta) * LAL_PI;
}


/**
 * Computes the PN Coefficients for using in the TaylorEt v(zeta) equation,
 * which is the square root of the x(zeta) equation.
 *
 * Terms given in equation 3.11 of: Alessandra Buonanno, Bala R Iyer, Evan
 * Ochsner, Yi Pan, and B S Sathyaprakash, "Comparison of post-Newtonian
 * templates for compact binary inspiral signals in gravitational-wave
 * detectors", Phys. Rev. D 80, 084043 (2009), arXiv:0907.0700v1
 */

static REAL8 UNUSED
XLALSimInspiralTaylorEtVOfZeta_2PNCoeff(
	REAL8 eta)
{
	return (3.0/4.0 + 1.0/12.0 * eta);
}

static REAL8 UNUSED
XLALSimInspiralTaylorEtVOfZeta_4PNCoeff(
	REAL8 eta)
{
	return (9.0/2.0 - 17.0/8.0 * eta + 1.0/18.0 * eta*eta);
}

static REAL8 UNUSED
XLALSimInspiralTaylorEtVOfZeta_6PNCoeff(
	REAL8 eta)
{
	return (40.5/1.6 + (20.5/9.6 * LAL_PI*LAL_PI - 479.5/7.2) * eta
		+ 5.5/6.4 * eta*eta + 3.5/129.6 * eta*eta*eta);
}


/**
 * Computes the PN Coefficients for using in the TaylorEt dPhase/dt equation.
 *
 * Terms given in equation 3.13a of: Alessandra Buonanno, Bala R Iyer, Evan
 * Ochsner, Yi Pan, and B S Sathyaprakash, "Comparison of post-Newtonian
 * templates for compact binary inspiral signals in gravitational-wave
 * detectors", Phys. Rev. D 80, 084043 (2009), arXiv:0907.0700v1
 */

static REAL8 UNUSED
XLALSimInspiralTaylorEtPhasing_0PNCoeff(
	REAL8 m)
{
	return 1.0/m;
}

static REAL8 UNUSED
XLALSimInspiralTaylorEtPhasing_2PNCoeff(
	REAL8 eta)
{
	return (9.0/8.0 + 1.0/8.0 * eta);
}

static REAL8 UNUSED
XLALSimInspiralTaylorEtPhasing_4PNCoeff(
	REAL8 eta)
{
	return (8.91/1.28 - 20.1/6.4 * eta + 1.1/12.8 * eta*eta);
}

static REAL8 UNUSED
XLALSimInspiralTaylorEtPhasing_6PNCoeff(
	REAL8 eta)
{
	return (41.445/1.024 - (309.715/3.072 - 20.5/6.4 * LAL_PI*LAL_PI) * eta
		+ 1.215/1.024 * eta*eta + 4.5/102.4 * eta*eta*eta);
}


/**
 * Computes the PN Coefficients for using in the TaylorEt dZeta/dt equation.
 *
 * Terms given in equation 3.13b of: Alessandra Buonanno, Bala R Iyer, Evan
 * Ochsner, Yi Pan, and B S Sathyaprakash, "Comparison of post-Newtonian
 * templates for compact binary inspiral signals in gravitational-wave
 * detectors", Phys. Rev. D 80, 084043 (2009), arXiv:0907.0700v1
 */

static REAL8 UNUSED
XLALSimInspiralTaylorEtZeta_0PNCoeff(
	REAL8 m,
	REAL8 eta)
{
	return 64.0 * eta / (5.0 * m);
}

static REAL8 UNUSED
XLALSimInspiralTaylorEtZeta_2PNCoeff(
	REAL8 eta)
{
	return (1.3/33.6 - 5.0/2.0 * eta);
}

static REAL8 UNUSED
XLALSimInspiralTaylorEtZeta_3PNCoeff(
	REAL8 UNUSED eta)
{
	return 4.0 * LAL_PI;
}

static REAL8 UNUSED
XLALSimInspiralTaylorEtZeta_4PNCoeff(
	REAL8 eta)
{
	return (11.7857/1.8144 - 12.017/2.016 * eta + 5.0/2.0 * eta*eta);
}

static REAL8 UNUSED
XLALSimInspiralTaylorEtZeta_5PNCoeff(
	REAL8 eta)
{
	return (49.13/6.72 - 177.0/8.0 * eta) * LAL_PI;
}

static REAL8 UNUSED
XLALSimInspiralTaylorEtZeta_6PNCoeff(
	REAL8 eta)
{
	return (379.99588601/2.79417600 + 16.0/3.0 * LAL_PI*LAL_PI - 17.12/1.05 * LAL_GAMMA
		+ (36.9/3.2 * LAL_PI*LAL_PI - 2486.1497/7.2576) * eta
		+ 48.8849/1.6128 * eta*eta - 8.5/6.4 * eta*eta*eta);
}

static REAL8 UNUSED
XLALSimInspiralTaylorEtZeta_6PNLogCoeff(
	REAL8 UNUSED eta)
{
	return -8.56/1.05;
}

static REAL8 UNUSED
XLALSimInspiralTaylorEtZeta_7PNCoeff(
	REAL8 eta)
{
	return (129.817/2.304 - 320.7739/4.8384 * eta + 61.3373/1.2096 * eta*eta) * LAL_PI;
}

/** < TaylorF2 family */

REAL8 XLALSimInspiralTaylorF2_NewtCoeff(REAL8 eta)
{
    return  3.L/(128.L * eta);
}

REAL8 XLALSimInspiralTaylorF2_05PNCoeff(void)
{
    return 0.0;
}
REAL8 XLALSimInspiralTaylorF2_1PNCoeff(void)
{
    return 1.0;
}
REAL8 XLALSimInspiralTaylorF2_15PNCoeff(REAL8 eta, REAL8 beta)
{
    return -16.L*LAL_PI + 4.L*beta;
}
REAL8 XLALSimInspiralTaylorF2_2PNCoeff(REAL8 eta, REAL8 sigma)
{
    return 5.L*(3058.673L/7.056L + 5429.L/7.L * eta
                     + 617.L * eta*eta)/72.L - 10.L*sigma;
}
REAL8 XLALSimInspiralTaylorF2_25PNCoeff(REAL8 eta)
{
    return 5.L/9.L * (7729.L/84.L - 13.L * eta) * LAL_PI;
}

REAL8 XLALSimInspiralTaylorF2_25PNLogCoeff(REAL8 eta)
{
    return 5.L/9.L * (7729.L/84.L - 13.L * eta) * LAL_PI;
}

REAL8 XLALSimInspiralTaylorF2_3PNCoeff(REAL8 eta)
{
    const REAL8 lambda = -1987./3080.;
    const REAL8 theta = -11831./9240.;
    return (11583.231236531L/4.694215680L - 640.L/3.L * LAL_PI * LAL_PI - 6848.L/21.L*LAL_GAMMA)
                     + eta * (-15335.597827L/3.048192L + 2255./12. * LAL_PI * LAL_PI - 1760./3.*theta +12320./9.*lambda)
                     + eta*eta * 76055.L/1728.L
                     - eta*eta*eta*  127825.L/1296.L ;
}

REAL8 XLALSimInspiralTaylorF2_3PNLogCoeff(void)
{
    return -6848.L/21.L;
}

REAL8 XLALSimInspiralTaylorF2_35PNCoeff(REAL8 eta)
{
    return LAL_PI * 5.L/756.L * ( 15419335.L/336.L + 75703.L/2.L * eta - 14809.L * eta*eta);
}
