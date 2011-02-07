/**
 * @file LALSQTPNWaveform.c
 *		Contains the function definition to create GWforms.
 * @author László Veréb
 * @date 2010.05.21.
 */

#include <lal/LALSQTPNWaveform.h>
#include <lal/LALSQTPNIntegrator.h>
#include <lal/LALSQTPNWaveformInterface.h>

NRCSID (LALSQTPNWAVEFORMC, "$Id LALSQTPN_Waveform.c$");

/**		The macro function calculates the scalar product of two vectors.
 * @param[in]  a1	: the left vector
 * @param[in]  a2	: the right vector
 * @return	the product
 */
#define SCALAR_PRODUCT3(a1, a2) \
	((a1)[0] * (a2)[0] + (a1)[1] * (a2)[1] + (a1)[2] * (a2)[2]);

/**		The macro function calculates the vector product of two vectors.
 * @param[in]  left		: the left vector
 * @param[in]  right	: the right vector
 * @param[out] product	: the vector product
 */
#define VECTOR_PRODUCT3(left, right, product)\
	(product)[0] = ((left)[1] * (right)[2] - (left)[2] * (right)[1]);\
	(product)[1] = ((left)[2] * (right)[0] - (left)[0] * (right)[2]);\
	(product)[2] = ((left)[0] * (right)[1] - (left)[1] * (right)[0]);

void XLALSQTPNFillCoefficients(LALSQTPNWaveformParams * const params) {

	// variable declaration and initialization
	REAL8 thetahat = 1039. / 4620.;
	REAL8 spin_MPow2[2];
	REAL8 m_m[2] = { params->mass[1] / params->mass[0], params->mass[0] / params->mass[1] };
	REAL8 piPow2 = SQT_SQR(LAL_PI);
	REAL8 etaPow2 = SQT_SQR(params->eta);
	REAL8 etaPow3 = etaPow2 * params->eta;
	INT2 i;
	for (i = 0; i < 2; i++) {
		spin_MPow2[i] = params->chiAmp[i] * SQT_SQR(params->mass[i]) / SQT_SQR(params->totalMass);
	}

	// calculating the coefficients
	params->coeff.domegaGlobal = params->eta * 96. / 5.;
	for (i = LAL_PNORDER_NEWTONIAN; i < LAL_PNORDER_PSEUDO_FOUR; i += 2) {
		params->coeff.meco[i] = -0.5 * params->eta * (REAL8) (i + 2) / 3.;
	}
	switch (params->order) {
		case LAL_PNORDER_THREE_POINT_FIVE:
			params->coeff.domega[LAL_PNORDER_THREE_POINT_FIVE] = (-4415. / 4032. + params->eta * 358675. / 6048.
					+ etaPow2 * 91495. / 1512.) * LAL_PI;
		case LAL_PNORDER_THREE:
			params->coeff.domega[LAL_PNORDER_THREE] = (16447322263. / 139708800. - LAL_GAMMA * 1712. / 105. + piPow2
					* 16. / 3.) + (-273811877. / 1088640. + piPow2 * 451. / 48. - thetahat * 88. / 3.) * params->eta
					+ etaPow2 * 541. / 896. - etaPow3 * 5605. / 2592.;
			params->coeff.domegaLN = -856. / 105.;
			params->coeff.meco[LAL_PNORDER_THREE] *= -675. / 64. + (209323. / 4032. - 205. * piPow2 / 96. + (110. / 9.)
					* (1987. / 3080.)) * params->eta - 155. * etaPow2 / 96. - 35. * etaPow3 / 5184.;
		case LAL_PNORDER_TWO_POINT_FIVE:
			params->coeff.domega[LAL_PNORDER_TWO_POINT_FIVE] = -(4159. + 15876. * params->eta) * LAL_PI / 672.;
		case LAL_PNORDER_TWO:
			params->coeff.domega[LAL_PNORDER_TWO] = 34103. / 18144. + params->eta * 13661. / 2016. + etaPow2 * 59.
					/ 18.;
			params->coeff.domegaSSselfConst = 0.;
			params->coeff.domegaQMConst = 0.;
			if ((params->spinInteraction & LAL_SSInter) == LAL_SSInter) {
				params->coeff.dchihSS[0] = spin_MPow2[1] / 2.;
				params->coeff.dchihSS[1] = spin_MPow2[0] / 2.;
				params->coeff.domegaSS[0] = 721. * params->eta * params->chiAmp[0] * params->chiAmp[1] / 48.;
				params->coeff.domegaSS[1] = -247. * params->coeff.domegaSS[0] / 721.;
				params->coeff.mecoSS = -spin_MPow2[0] * spin_MPow2[1];
			}
			if ((params->spinInteraction & LAL_SSselfInter) == LAL_SSselfInter) {
				for (i = 0; i < 2; i++) {
					params->coeff.domegaSSself[i] = -spin_MPow2[i] * params->chiAmp[i] / 96.;
					params->coeff.domegaSSselfConst -= 7. * params->coeff.domegaSSself[i];
				}
			}
			if ((params->spinInteraction & LAL_QMInter) == LAL_QMInter) {
				for (i = 0; i < 2; i++) {
					params->coeff.domegaQM[i] = spin_MPow2[i] * params->chiAmp[i] * params->qmParameter[i] * 7.5;
					params->coeff.domegaQMConst -= params->coeff.domegaQM[i] / 3.;
					params->coeff.dchihQM[i] = -params->qmParameter[i] * params->eta * params->chiAmp[i] * 3. / 2.;
				}
				params->coeff.mecoQM = 2. * params->eta;
			}
			params->coeff.meco[LAL_PNORDER_TWO] *= (-81. + 57. * params->eta - etaPow2) / 24.;
		case LAL_PNORDER_ONE_POINT_FIVE:
			params->coeff.domega[LAL_PNORDER_ONE_POINT_FIVE] = 4. * LAL_PI;
			if (params->spinInteraction != 0) {
				for (i = 0; i < 2; i++) {
					params->coeff.dchihSO[i] = (4. + 3. * m_m[i]) * params->eta / 2.;
					params->coeff.dLNh[i] = -spin_MPow2[i] / params->eta;
					params->coeff.domegaSO[i] = -spin_MPow2[i] * (113. + 75. * m_m[i]) / 12.;
					params->coeff.mecoSO[i] = -spin_MPow2[i] * 5. * params->eta * (4. + 3. * m_m[i]) / 9.;
				}
			}
		case LAL_PNORDER_ONE:
			params->coeff.domega[LAL_PNORDER_ONE] = -(743. + 924. * params->eta) / 336.;
			params->coeff.meco[LAL_PNORDER_ONE] *= -(9. + params->eta) / 12.;
		case LAL_PNORDER_HALF:
			params->coeff.domega[LAL_PNORDER_HALF] = 0.;
		case LAL_PNORDER_NEWTONIAN:
			params->coeff.domega[LAL_PNORDER_NEWTONIAN] = 1.;
		default:
			break;
	}
}

int LALSQTPNDerivator(REAL8 t, const REAL8 values[], REAL8 dvalues[], void * param) {

	// variable declaration and initialization
	LALSQTPNWaveformParams *params = param;
	UNUSED(t);
	const REAL8 *chi_p[2] = { values + LALSQTPN_CHIH1_1, values + LALSQTPN_CHIH2_1 };
	UINT2 i, j, k; // indexes
	memset(dvalues, 0, LALSQTPN_NUM_OF_VAR * sizeof(REAL8));
	REAL8 omegaPowi_3[8];
	omegaPowi_3[0] = 1.;
	omegaPowi_3[1] = cbrt(values[LALSQTPN_OMEGA]);
	for (i = 2; i < 8; i++) {
		omegaPowi_3[i] = omegaPowi_3[i - 1] * omegaPowi_3[1];
	}
	REAL8 SS_Omega, SSself_Omega, QM_Omega;
	SS_Omega = SSself_Omega = QM_Omega = 0.;
	REAL8 chih1chih2, chih1xchih2[2][3], LNhchih[2], LNhxchih[2][3];
	chih1chih2 = SCALAR_PRODUCT3(chi_p[0], chi_p[1]);
	for (i = 0; i < 2; i++) {
		LNhchih[i] = SCALAR_PRODUCT3(values + LALSQTPN_LNH_1, chi_p[i]);
		VECTOR_PRODUCT3(values + LALSQTPN_LNH_1, chi_p[i], LNhxchih[i]);
	}

	// calculating domega and MECO without the spin components
	for (i = LAL_PNORDER_NEWTONIAN; i <= params->order; i++) {
		dvalues[LALSQTPN_OMEGA] += params->coeff.domega[i] * omegaPowi_3[i];
	}
	dvalues[LALSQTPN_MECO] += params->coeff.meco[0] / omegaPowi_3[1];
	for (i = LAL_PNORDER_NEWTONIAN + 2; i <= params->order; i += 2) {
		dvalues[LALSQTPN_MECO] += params->coeff.meco[i] * omegaPowi_3[i - 1];
	}

	// calculating the other derivatives and the domega and MECO with spin
	// components
	switch (params->order) {
		case LAL_PNORDER_THREE_POINT_FIVE:
		case LAL_PNORDER_THREE:
			dvalues[LALSQTPN_OMEGA] += params->coeff.domegaLN * log(16. * omegaPowi_3[2])
					* omegaPowi_3[LAL_PNORDER_THREE];
		case LAL_PNORDER_TWO_POINT_FIVE:
		case LAL_PNORDER_TWO:
			if ((params->spinInteraction & LAL_SSInter) == LAL_SSInter) {
				// SS for domega
				SS_Omega = params->coeff.domegaSS[0] * LNhchih[0] * LNhchih[1] + params->coeff.domegaSS[1] * chih1chih2;
				// SS for MECO
				dvalues[LALSQTPN_MECO] += params->coeff.mecoSS * (chih1chih2 - 3 * LNhchih[0] * LNhchih[1])
						* omegaPowi_3[3];
				// SS for dchih
				for (i = 0; i < 2; i++) {
					k = (i + 1) % 2; // the opposite index
					VECTOR_PRODUCT3(chi_p[k], chi_p[i], chih1xchih2[i]);
					for (j = 0; j < 3; j++) {
						// the 3*index is used, to acces the first spin, if index=0,
						// otherwise the second spin
						dvalues[LALSQTPN_CHIH1_1 + 3 * i + j] += params->coeff.dchihSS[i] * (chih1xchih2[i][j] - 3.
								* LNhchih[k] * LNhxchih[i][j]) * omegaPowi_3[6];
						//((1.-2.*(!i))chih1xchih2[i][j] - 3. * LNhchih[k] * LNhxchih[i][j]) * omegaPowi_3[6];
					}
				}
			}
			if ((params->spinInteraction & LAL_SSselfInter) == LAL_SSselfInter) {
				// SSself for domega
				SSself_Omega = params->coeff.domegaSSselfConst;
				for (i = 0; i < 2; i++) {
					SSself_Omega += params->coeff.domegaSSself[i] * SQT_SQR(LNhchih[i]);
				}
			}
			if ((params->spinInteraction & LAL_QMInter) == LAL_QMInter) {
				QM_Omega = params->coeff.domegaQMConst;
				for (i = 0; i < 2; i++) {
					QM_Omega += params->coeff.domegaQM[i] * SQT_SQR(LNhchih[i]);
					// QM for dchih
					for (j = 0; j < 3; j++) {
						// the 3*index is used, to acces the first spin, if index=0,
						// otherwise the second spin
						dvalues[LALSQTPN_CHIH1_1 + 3 * i + j] += params->coeff.dchihQM[i] * LNhchih[i] * LNhxchih[i][j]
								* omegaPowi_3[6];
					}
				}
				// QM for MECO
				dvalues[LALSQTPN_MECO] += params->coeff.mecoQM * QM_Omega * omegaPowi_3[3];
			}
			dvalues[LALSQTPN_OMEGA] += (QM_Omega + SSself_Omega + SS_Omega) * omegaPowi_3[LAL_PNORDER_TWO];
		case LAL_PNORDER_ONE_POINT_FIVE:
			if (params->spinInteraction != 0) {
				// SO for domega and MECO
				for (i = 0; i < 2; i++) {
					dvalues[LALSQTPN_OMEGA] += params->coeff.domegaSO[i] * LNhchih[i]
							* omegaPowi_3[LAL_PNORDER_ONE_POINT_FIVE];
					dvalues[LALSQTPN_MECO] += params->coeff.mecoSO[i] * LNhchih[i] * omegaPowi_3[2];
				}
				// dLNh and SO for dchih
				for (i = 0; i < 3; i++) {
					dvalues[LALSQTPN_CHIH1_1 + i] += params->coeff.dchihSO[0] * LNhxchih[0][i] * omegaPowi_3[5];
					dvalues[LALSQTPN_CHIH2_1 + i] += params->coeff.dchihSO[1] * LNhxchih[1][i] * omegaPowi_3[5];
					dvalues[LALSQTPN_LNH_1 + i] += (params->coeff.dLNh[0] * dvalues[LALSQTPN_CHIH1_1 + i]
							+ params->coeff.dLNh[1] * dvalues[LALSQTPN_CHIH2_1 + i]) * omegaPowi_3[1];
				}
			}
		case LAL_PNORDER_ONE:
		case LAL_PNORDER_HALF:
		case LAL_PNORDER_NEWTONIAN:
		default:
			break;
	}
	dvalues[LALSQTPN_OMEGA] *= params->coeff.domegaGlobal * omegaPowi_3[7] * omegaPowi_3[4];
	dvalues[LALSQTPN_PHASE] = values[LALSQTPN_OMEGA] + values[LALSQTPN_LNH_3] * (values[LALSQTPN_LNH_2]
			* dvalues[LALSQTPN_LNH_1] - values[LALSQTPN_LNH_1] * dvalues[LALSQTPN_LNH_2])
			/ (SQT_SQR(values[LALSQTPN_LNH_1]) + SQT_SQR(values[LALSQTPN_LNH_2]));
	return GSL_SUCCESS;
}

void LALSQTPNGenerator(LALStatus *status, LALSQTPNWave *waveform, LALSQTPNWaveformParams *params) {
	INITSTATUS(status, "LALSQTPNGenerator", LALSQTPNWAVEFORMC);
	ATTATCHSTATUSPTR(status);
	ASSERT(params, status, LALINSPIRALH_ENULL, LALINSPIRALH_MSGENULL);
	ASSERT(waveform, status, LALINSPIRALH_ENULL, LALINSPIRALH_MSGENULL);

	// variable declaration and initialization
	UINT4 i = 0; // index
	REAL8 time = 0.;
	REAL8 LNhztol = 1.0e-8;
	REAL8 alpha, amp, temp1, temp2;
	const REAL8 geometrized_m_total = params->totalMass * LAL_MTSUN_SI;
	const REAL8 freq_Step = geometrized_m_total * LAL_PI;
	const REAL8 step = params->samplingTime / geometrized_m_total;
	REAL8 values[LALSQTPN_NUM_OF_VAR], dvalues[LALSQTPN_NUM_OF_VAR];
	LALSQTPNIntegratorSystem integrator;
	xlalErrno = 0;
	if (XLALSQTPNIntegratorInit(&integrator, LALSQTPN_NUM_OF_VAR, params, LALSQTPNDerivator)) {
		if (XLAL_ENOMEM == XLALClearErrno()) {
			ABORT(status, LALINSPIRALH_EMEM, LALINSPIRALH_MSGEMEM);
		} else {
			ABORTXLAL(status);
		}
	}

	// initializing the dynamic variables
	values[LALSQTPN_PHASE] = params->phi;
	values[LALSQTPN_OMEGA] = params->lowerFreq * freq_Step;
	values[LALSQTPN_LNH_1] = sin(params->inclination); ///< \f$\hat{L_N}=\sin\iota\f$
	values[LALSQTPN_LNH_2] = 0.; ///< \f$\hat{L_N}=0\f$
	values[LALSQTPN_LNH_3] = cos(params->inclination); ///< \f$\hat{L_N}=\cos\iota\f$
	values[LALSQTPN_MECO] = 0.;
	for (i = 0; i < 3; i++) {
		values[LALSQTPN_CHIH1_1 + i] = params->chih[0][i];
		values[LALSQTPN_CHIH2_1 + i] = params->chih[1][i];
	}

	// filling the LALSQTPNCoefficients
	xlalErrno = 0;
	XLALSQTPNFillCoefficients(params);
	if (xlalErrno) {
		ABORTXLAL(status);
	}
	LALSQTPNDerivator(time, values, dvalues, params);
	dvalues[LALSQTPN_MECO] = -1.; // to be able to start the loop
	i = 0;
	do {
		alpha = atan2(values[LALSQTPN_LNH_2], values[LALSQTPN_LNH_1]);
		amp = params->signalAmp * pow(values[LALSQTPN_OMEGA], 2. / 3.);

		// calculating the waveform components
		if (waveform->hp || waveform->hc || waveform->h) {
			temp1 = -0.5 * amp * cos(2. * values[LALSQTPN_PHASE]) * (values[LALSQTPN_LNH_3] * values[LALSQTPN_LNH_3]
					+ 1.);
			temp2 = amp * sin(2. * values[LALSQTPN_PHASE]) * values[LALSQTPN_LNH_3];
			if (waveform->h) {
				waveform->hp->data[2 * i] = temp1 * cos(2. * alpha) + temp2 * sin(2. * alpha);
				waveform->hc->data[2 * i + 1] = temp1 * sin(2. * alpha) - temp2 * cos(2. * alpha);
			}
			if (waveform->hp) {
				waveform->hp->data[i] = temp1 * cos(2. * alpha) + temp2 * sin(2. * alpha);
			}
			if (waveform->hc) {
				waveform->hc->data[i] = temp1 * sin(2. * alpha) - temp2 * cos(2. * alpha);
			}
		}
		if (waveform->waveform) {
			waveform->waveform->a->data->data[2 * i] = -amp * 0.5 * (1. + values[LALSQTPN_LNH_3]
					* values[LALSQTPN_LNH_3]);
			waveform->waveform->a->data->data[2 * i + 1] = -amp * values[LALSQTPN_LNH_3];
			waveform->waveform->phi->data->data[i] = 2. * (values[LALSQTPN_PHASE] - params->phi);
			waveform->waveform->shift->data->data[i] = 2. * alpha;
			waveform->waveform->f->data->data[i] = values[LALSQTPN_OMEGA] / freq_Step;
		}

		// evolving
		time = i++ * params->samplingTime;
		xlalErrno = 0;
		if (XLALSQTPNIntegratorFunc(values, &integrator, step)) {
			ABORTXLAL(status);
		}
		// if one of the variables is nan, the PN approximation braked down
		if (isnan(values[LALSQTPN_PHASE]) || isnan(values[LALSQTPN_OMEGA]) || isnan(values[LALSQTPN_LNH_1])
				|| isnan(values[LALSQTPN_LNH_2]) || isnan(values[LALSQTPN_LNH_3]) || isnan(values[LALSQTPN_CHIH1_1])
				|| isnan(values[LALSQTPN_CHIH1_2]) || isnan(values[LALSQTPN_CHIH1_3])
				|| isnan(values[LALSQTPN_CHIH2_1]) || isnan(values[LALSQTPN_CHIH2_2])
				|| isnan(values[LALSQTPN_CHIH2_3])) {
			break;
		}
		LALSQTPNDerivator(time, values, dvalues, params);
		if ((waveform->waveform && i == waveform->waveform->f->data->length) || (waveform->hp && i
				== waveform->hp->length) || (waveform->hc && i == waveform->hc->length)) {
			XLALSQTPNIntegratorFree(&integrator);
			ABORT(status, LALINSPIRALH_ESIZE, LALINSPIRALH_MSGESIZE);
		}
	} while (dvalues[LALSQTPN_MECO] < 0. && dvalues[LALSQTPN_OMEGA] > 0.0 && SQT_SQR(values[LALSQTPN_LNH_3]) < 1.
			- LNhztol && values[LALSQTPN_OMEGA] / freq_Step < params->samplingFreq / 2./* && values[LALSQTPN_OMEGA] / freq_Step < params->finalFreq*/);
	if (waveform->hp || waveform->hc) {
		params->finalFreq = values[LALSQTPN_OMEGA] / (LAL_PI * geometrized_m_total);
		params->coalescenceTime = time;
	}
	if (waveform->waveform->a) {
		params->finalFreq = waveform->waveform->f->data->data[i - 1];
	}

	waveform->length = i;
	XLALSQTPNIntegratorFree(&integrator);
	DETATCHSTATUSPTR(status);
	RETURN(status);
}

void XLALSQTPNCalculateHPHC(LALSQTPNWaveformParams *params, REAL8 values[], REAL8 *h) {
	REAL8 deltaM = params->mass[0] - params->mass[1];
	REAL8 amp = -2. * params->totalMass * params->eta * LAL_MRSUN_SI / params->distance * pow(values[LALSQTPN_OMEGA],
			2. / 3.);
	REAL8 Q[3][2] = { { 0., 0. }, { 0., 0. }, { 0., 0. } };
	REAL8 C[2], S[2], K[2], DC[2], DS[2];
	REAL8 cos_Iota = values[LALSQTPN_LNH_3];
	REAL8 sin_Iota = sin(acos(cos_iota));
	REAL8 cosPow2_Iota = SQT_SQR(cos_iota);
	REAL8 sinPow2_Iota = SQT_SQR(sin_iota);
	REAL8 alpha = atan2(values[LALSQTPN_LNH_2], values[LALSQTPN_LNH_1]);
	REAL8 twoAlpha = 2. * alpha;
	REAL8 cos_iPhi[5] = { 1., cos(values[LALSQTPN_PHASE]), cos(2. * values[LALSQTPN_PHASE]), cos(3.
			* values[LALSQTPN_PHASE]), cos(4. * values[LALSQTPN_PHASE]) };
	REAL8 sin_iPhi[5] = { 0., sin(values[LALSQTPN_PHASE]), sin(2. * values[LALSQTPN_PHASE]), sin(3.
			* values[LALSQTPN_PHASE]), sin(4. * values[LALSQTPN_PHASE]) };
	C[LALSQTPN_PLUS] = C[LALSQTPN_CROSS] = -1. / 2. * (1. + cos2_iota) * cos(twoAlpha);
	S[LALSQTPN_PLUS] = cos_Iota * sin(twoAlpha);
	S[LALSQTPN_CROSS] = -cos_Iota * cos(twoAlpha);
	UINT2 i;
	for (i = LALSQTPN_PLUS; i <= LALSQTPN_CROSS; i++) {
		Q[LALSQTPN_0][i] = -2. * (C[i] * cos(2. * values[LALSQTPN_PHASE]) + S[i] * sin(2. * values[LALSQTPN_PHASE]));
	}
	K[LALSQTPN_PLUS] = -1. / 2. * cos(twoAlpha) * sinPow2_Iota;
	K[LALSQTPN_CROSS] = -1. / 2. * sin(twoAlpha) * sinPow2_Iota;
	for (i = LALSQTPN_PLUS; i <= LALSQTPN_CROSS; i++) {
		Q[LALSQTPN_0_5][i] = 1. / 4. * deltaM / params->totalMass * (3. * C[i] * sin_Iota * (3. * cos_iPhi[3]
				- cos_iPhi[1]) + 3. * S[i] * sin_Iota * (3. * sin_iPhi[3] - sin_iPhi[1]) - 2. * K[i] * sin_Iota
				* cos_iPhi[1]);
	}
	REAL8 DELTAX = params->totalMass * (params->chi[1][LALSQTPN_CHIH2_1] * params->mass[1]
			- params->chi[0][LALSQTPN_CHIH1_1] * params->mass[0]);
	REAL8 DELTAY = params->totalMass * (params->chi[1][LALSQTPN_CHIH2_2] * params->mass[1]
			- params->chi[0][LALSQTPN_CHIH1_2] * params->mass[0]);
	REAL8 DC[2], DS[2];
	DC[LALSQTPN_PLUS] = (DELTAY * sin(alpha) - DELTAX * cos(alpha)) / params->totalMass;
	DC[LALSQTPN_CROSS] = (DELTAY * cos(alpha) - DELTAX * sin(alpha)) / params->totalMass;
	REAL8 cosin = cos_Iota * cos(alpha);
	REAL8 sinus = cos_Iota * sin(alpha);
	DS[LALSQTPN_PLUS] = (DELTAY * cosin - DELTAX * sinus) / params->totalMass;
	DS[LALSQTPN_CROSS] = (DELTAY * sinus - DELTAX * cosin) / params->totalMass;
	for (i = LALSQTPN_PLUS; i <= LALSQTPN_CROSS; i++) {
		Q[LALSQTPN_1][i] = -8. / 3. * (1. - 3. * params->eta) * sinPow2_Iota
				* (C[i] * cos_iPhi[4] + S[i] * sin_iPhi[4]) + DC[i] * cos_iPhi[1] + DS[i] * sin_iPhi[1] + 1. / 6. * (4.
				* (1. - 3. * params->eta) * sinPow2_Iota * cos_iPhi[2] * K[i] - (4. * (1. - 3. * params->eta)
				* sinPow2_Iota + (19. - 3. * params->eta)) * Q[LALSQTPN_0][i]);
	}
	for (i = LALSQTPN_PLUS; i <= LALSQTPN_CROSS; i++) {
		h[i] = amp * (Q[LALSQTPN_0][i] + Q[LALSQTPN_0_5][i] * pow(values[LALSQTPN_OMEGA], 1. / 3.) + Q[LALSQTPN_1][i]
				* pow(values[LALSQTPN_OMEGA], 2. / 3.));
	}
}
