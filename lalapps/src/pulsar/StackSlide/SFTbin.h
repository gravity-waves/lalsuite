/*
*  Copyright (C) 2007 Gregory Mendell
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

/*-----------------------------------------------------------------------
 *
 * File Name: SFTbin.h
 *
 * Authors: Sintes, A.M.,  Krishnan, B. & inspired from Siemens, X.
 *
 * Revision: $Id$
 *
 * History:   Created by Sintes May 21, 2003
 *            Modified...
 *
 *-----------------------------------------------------------------------
 */
 
/**
\author Sintes, A.M.,
*/

/* REVISIONS: */
/* 07/13/05 gam; make RandomParams *randPar a parameter for CleanCOMPLEX8SFT; initialze RandomParams *randPar once to avoid repeatly opening /dev/urandom */

/**
\heading{Header \ref SFTbin.h}
Routines for reading SFT binary files

\heading{Synopsis}

\code
#include <lal/SFTbin.h>
\endcode
*/

/*
 * 4.  Protection against double inclusion (include-loop protection)
 *     Note the naming convention!
 */

#ifndef _SFTBIN_H
#define _SFTBIN_H

/*
 * 5. Includes. This header may include others; if so, they go immediately 
 *    after include-loop protection. Includes should appear in the following 
 *    order: 
 *    a. Standard library includes
 *    b. LDAS includes
 *    c. LAL includes
 */

#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h> 
#include <lal/LALStdlib.h>
#include <lal/LALConstants.h>
#include <lal/AVFactories.h>
#include <lal/SeqFactories.h>
#include <lal/SFTfileIO.h>
#include <lal/Random.h>
#include <lal/PulsarDataTypes.h>
#include <lal/UserInput.h>
#include <lal/LUT.h>
#include <lal/RngMedBias.h>

#include <gsl/gsl_statistics.h>
#include <gsl/gsl_rng.h>
#include <gsl/gsl_randist.h>
#include <gsl/gsl_sort.h>  
/*
 *   Protection against C++ name mangling
 */

#ifdef  __cplusplus
extern "C" {
#endif

/*
 * 7. Error codes and messages. This must be auto-extracted for 
 *    inclusion in the documentation.
 */
  
/**\name Error Codes */ /*@{*/
  
#define SFTBINH_ENULL 1
#define SFTBINH_EFILE 2
#define SFTBINH_EHEADER 3
#define SFTBINH_EENDIAN 4
#define SFTBINH_EVAL 5
#define SFTBINH_ESEEK 9
#define SFTBINH_EREAD 10
#define SFTBINH_EWRITE 11

#define SFTBINH_MSGENULL "Null pointer"
#define SFTBINH_MSGEFILE "Could not open file"
#define SFTBINH_MSGEHEADER "Incorrect header in file"
#define SFTBINH_MSGEENDIAN "Incorrect endian type" 
#define SFTBINH_MSGEVAL  "Invalid value"
#define SFTBINH_MSGESEEK "fseek failed"
#define SFTBINH_MSGEREAD "fread failed"
#define SFTBINH_MSGEWRITE "fwrite failed"


/*@}*/


/* ******************************************************
 * 8. Macros. But, note that macros are deprecated. 
 *    They could be moved to the modules where are needed 
 */  

/* *******************************************************
 * 9. Constant Declarations. (discouraged) 
 */
 


/* **************************************************************
 * 10. Structure, enum, union, etc., typdefs.
 */

  typedef struct tagSFTHeader1{
    REAL8  endian;
    INT4   gpsSeconds;
    INT4   gpsNanoSeconds;
    REAL8  timeBase;
    INT4   fminBinIndex;
    INT4   length;
  } SFTHeader1;
  
  typedef struct tagCOMPLEX8SFTData1{  /* simple case */
    LIGOTimeGPS  epoch; /* epoch of first series sample */
    REAL8        timeBase;
    INT4         fminBinIndex;
    INT4         length; /* number of elements in data */ 
    COMPLEX8     *data;  /* pointer to the data */
  } COMPLEX8SFTData1;

  typedef struct tagCOMPLEX8SFTvector1{  
    UINT4                length; /* number of elements  */ 
    COMPLEX8SFTData1     *sft;  /* pointer to the data */
  } COMPLEX8SFTvector1;
  
  typedef struct tagCOMPLEX16SFTData1{  /* simple case */
    LIGOTimeGPS  epoch; /* epoch of first series sample */
    REAL8        timeBase;
    INT4         fminBinIndex;
    INT4         length; /* number of elements in data */ 
    COMPLEX16     *data;  /* pointer to the data */
  } COMPLEX16SFTData1;

  typedef struct tagREAL8Periodogram1{  /* simple case */
    LIGOTimeGPS  epoch; /* epoch of first series sample */
    REAL8        timeBase;
    INT4         fminBinIndex;
    INT4         length; /* number of elements in data */ 
    REAL8        *data;  /* pointer to the data */
  } REAL8Periodogram1;

  typedef struct tagLineNoiseInfo{
    INT4         nLines; /* number of lines */ 
    REAL8        *lineFreq; /* central frequency of the lines */
    REAL8        *leftWing; /* width to the left from central ferquency */
    REAL8        *rightWing; /* width to the right */
  } LineNoiseInfo; 

  typedef struct tagLineHarmonicsInfo{
    INT4         nHarmonicSets; /* number of sets of harmonics */
    REAL8        *startFreq; /* starting frequency of set */
    REAL8        *gapFreq;  /* frequency difference between adjacent harmonics */
    INT4         *numHarmonics; /* Number of harmonics */  
    REAL8        *leftWing; /* width to the left of each line in set */
    REAL8        *rightWing; /* width to the right */
  } LineHarmonicsInfo; 

/*
 * 11. Extern Global variables. (discouraged) 
 */
  

/*
 * 12. Functions Declarations (i.e., prototypes).
 */

void ReadSFTbinHeader1 (LALStatus  *status,
                   SFTHeader1    *header,
		   CHAR          *fname
		   );

void ReadCOMPLEX8SFTbinData1 (LALStatus  *status,
		   COMPLEX8SFTData1    *sft,
		   CHAR                *fname
		   );

void ReadCOMPLEX16SFTbinData1 (LALStatus  *status,
		   COMPLEX16SFTData1    *sft,
		   CHAR                 *fname
		   );
void COMPLEX8SFT2Periodogram1 (LALStatus  *status,
                   REAL8Periodogram1    *peri,
		   COMPLEX8SFTData1    *sft		   
		   );

void SFT2Periodogram (LALStatus  *status,
                   REAL8Periodogram1    *peri,
		   SFTtype    *sft		   
		   );

void COMPLEX16SFT2Periodogram1 (LALStatus  *status,
                   REAL8Periodogram1    *peri,
		   COMPLEX16SFTData1    *sft		   
		   );

void FindNumberHarmonics (LALStatus           *status,
			  LineHarmonicsInfo   *harmonicInfo,
			  CHAR                *fname
			  );

void  ReadHarmonicsInfo (LALStatus          *status,
			 LineHarmonicsInfo  *lineInfo,
			 CHAR               *fname
			 );

void  Harmonics2Lines (LALStatus          *status,
		       LineNoiseInfo      *lineInfo,
		       LineHarmonicsInfo  *harmonicsInfo
		       );

void ChooseLines (LALStatus        *status,
		  LineNoiseInfo    *outLine,
		  LineNoiseInfo    *inLine,
		  REAL8            freqMin,
		  REAL8            freqMax
		  );


void CheckLines ( LALStatus           *status,
		  INT4                *flag,
		  LineNoiseInfo       *lines,
		  REAL8               freq);


void FindNumberLines (LALStatus        *status,
		      LineNoiseInfo    *lineInfo,
		      CHAR             *fname
		      );

void ReadLineInfo (LALStatus        *status,
		   LineNoiseInfo  *lineInfo,
		   CHAR           *fname
		   );

/* 07/13/05 gam; add RandomParams *randPar */
void CleanCOMPLEX8SFT (LALStatus          *status,
		       SFTtype            *sft,
		       INT4               width,
		       INT4               window,
		       LineNoiseInfo      *lineInfo,
		       RandomParams       *randPar
		       );

void CleanCOMPLEX16SFT (LALStatus               *status,
			COMPLEX16FrequencySeries *sft,
			INT4                    width,
			LineNoiseInfo           *lineInfo
			);

void WriteCOMPLEX8SFT (LALStatus          *status,
		       COMPLEX8SFTData1   *sft,
		       CHAR               *outfname
		       );

void WriteCOMPLEX16SFT (LALStatus          *status,
		       COMPLEX16SFTData1   *sft,
		       CHAR                *outfname
		       );

#ifdef  __cplusplus
}                /* Close C++ protection */
#endif

#endif     /* Close double-include protection _SFTBIN_H */
 







