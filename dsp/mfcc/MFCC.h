/* -*- c-basic-offset: 4 indent-tabs-mode: nil -*-  vi:set ts=8 sts=4 sw=4: */

/*
    QM DSP Library

    Centre for Digital Music, Queen Mary, University of London.
    This file copyright 2005 Nicolas Chetry, copyright 2008 QMUL.
    All rights reserved.
*/

#ifndef MFCC_H
#define MFCC_H

#include "base/Window.h"

struct MFCCConfig {
    int FS;
    int fftsize;
    int nceps;
    double logpower;
    bool want_c0;
    WindowType window;
    MFCCConfig(int _FS) :
        FS(_FS), fftsize(2048), nceps(19),
        logpower(1.0), want_c0(true), window(HammingWindow) { }
};

class MFCC
{
public:
    MFCC(MFCCConfig config);
    virtual ~MFCC();

    /**
     * Process time-domain input data.  inframe must contain
     * getfftlength() samples.  outceps must contain space for nceps
     * values, plus one if want_c0 is specified.
     */
    int process(const double *inframe, double *outceps);

    /**
     * Process time-domain input data.  real and imag must contain
     * getfftlength()/2+1 elements (i.e. the conjugate half of the FFT
     * is not expected).  outceps must contain space for nceps values,
     * plus one if want_c0 is specified.
     */
    int process(const double *real, const double *imag, double *outceps);

    int getfftlength() const { return fftSize; }

private:
    /* Filter bank parameters */
    double  lowestFrequency; 
    int     linearFilters; 
    double  linearSpacing;
    int     logFilters;
    double  logSpacing;
    
    /* FFT length */
    int     fftSize;
    
    int     totalFilters;
    double  logPower;
    
    /* Misc. */
    int     samplingRate;
    int     nceps;
    
    /* MFCC vector */
    double  *ceps;
    
    double  **mfccDCTMatrix;
    double  **mfccFilterWeights;
    
    /* The analysis window */
    Window<double> *window;
    
    /* For the FFT */
    double *imagIn;		// always zero
    double *realOut;
    double *imagOut;
    double *fftMag;
    double *earMag;

    /* Set if user want C0 */
    int WANT_C0;
};


#endif
