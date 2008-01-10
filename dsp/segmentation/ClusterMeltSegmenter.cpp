/* -*- c-basic-offset: 4 indent-tabs-mode: nil -*-  vi:set ts=8 sts=4 sw=4: */

/*
 * ClusterMeltSegmenter.cpp
 *
 * Created by Mark Levy on 23/03/2006.
 * Copyright 2006 Centre for Digital Music, Queen Mary, University of London.
 * All rights reserved.
 */

#include <cfloat>
#include <cmath>

#include "ClusterMeltSegmenter.h"
#include "cluster_segmenter.h"
#include "segment.h"

#include "dsp/transforms/FFT.h"
#include "dsp/chromagram/ConstantQ.h"
#include "dsp/rateconversion/Decimator.h"

ClusterMeltSegmenter::ClusterMeltSegmenter(ClusterMeltSegmenterParams params) :
    window(NULL),
    constq(NULL),
    featureType(params.featureType),
    hopSize(params.hopSize),
    windowSize(params.windowSize),
    fmin(params.fmin),
    fmax(params.fmax),
    nbins(params.nbins),
    ncomponents(params.ncomponents),	// NB currently not passed - no. of PCA components is set in cluser_segmenter.c
    nHMMStates(params.nHMMStates),
    nclusters(params.nclusters),
    histogramLength(params.histogramLength),
    neighbourhoodLimit(params.neighbourhoodLimit),
    decimator(0)
{
}

void ClusterMeltSegmenter::initialise(int fs)
{
    samplerate = fs;

    if (featureType != FEATURE_TYPE_UNKNOWN)
    {
        // always run internal processing at 11025 or thereabouts
        int internalRate = 11025;
        int decimationFactor = samplerate / internalRate;
        if (decimationFactor < 1) decimationFactor = 1;

        // must be a power of two
        while (decimationFactor & (decimationFactor - 1)) ++decimationFactor;

        if (decimationFactor > Decimator::getHighestSupportedFactor()) {
            decimationFactor = Decimator::getHighestSupportedFactor();
        }

        if (decimationFactor > 1) {
            decimator = new Decimator(getWindowsize(), decimationFactor);
        }

        CQConfig config;
        config.FS = samplerate / decimationFactor;
        config.min = fmin;
        config.max = fmax;
        config.BPO = nbins;
        config.CQThresh = 0.0054;

        constq = new ConstantQ(config);
        constq->sparsekernel();

        ncoeff = constq->getK();
    }
}

ClusterMeltSegmenter::~ClusterMeltSegmenter() 
{
    delete window;
    delete constq;
    delete decimator;
}

int
ClusterMeltSegmenter::getWindowsize()
{
    if (featureType != FEATURE_TYPE_UNKNOWN) {

        if (constq) {
/*
            std::cerr << "ClusterMeltSegmenter::getWindowsize: "
                      << "rate = " << samplerate
                      << ", dec factor = " << (decimator ? decimator->getFactor() : 1)
                      << ", fft length = " << constq->getfftlength()
                      << ", fmin = " << fmin
                      << ", fmax = " << fmax
                      << ", nbins = " << nbins
                      << ", K = " << constq->getK()
                      << ", Q = " << constq->getQ()
                      << std::endl;
*/
        }
    }

    return static_cast<int>(windowSize * samplerate);
}

int
ClusterMeltSegmenter::getHopsize()
{
    return static_cast<int>(hopSize * samplerate);
}

void ClusterMeltSegmenter::extractFeatures(const double* samples, int nsamples)
{
    if (!constq) {
        std::cerr << "ERROR: ClusterMeltSegmenter::extractFeatures: "
                  << "Cannot run unknown feature type (or initialise not called)"
                  << std::endl;
        return;
    }

    if (nsamples < getWindowsize()) {
        std::cerr << "ERROR: ClusterMeltSegmenter::extractFeatures: nsamples < windowsize (" << nsamples << " < " << getWindowsize() << ")" << std::endl;
        return;
    }

    int fftsize = constq->getfftlength();

    if (!window || window->getSize() != fftsize) {
        delete window;
        window = new Window<double>(HammingWindow, fftsize);
    }

    vector<double> cq(ncoeff);

    for (int i = 0; i < ncoeff; ++i) cq[i] = 0.0;
    
    const double *psource = samples;
    int pcount = nsamples;

    if (decimator) {
        pcount = nsamples / decimator->getFactor();
        double *decout = new double[pcount];
        decimator->process(samples, decout);
        psource = decout;
    }
    
    int origin = 0;
    
//    std::cerr << "nsamples = " << nsamples << ", pcount = " << pcount << std::endl;

    int frames = 0;

    double *frame = new double[fftsize];
    double *real = new double[fftsize];
    double *imag = new double[fftsize];
    double *cqre = new double[ncoeff];
    double *cqim = new double[ncoeff];

    while (origin <= pcount) {

        // always need at least one fft window per block, but after
        // that we want to avoid having any incomplete ones
        if (origin > 0 && origin + fftsize >= pcount) break;

        for (int i = 0; i < fftsize; ++i) {
            if (origin + i < pcount) {
                frame[i] = psource[origin + i];
            } else {
                frame[i] = 0.0;
            }
        }

        for (int i = 0; i < fftsize/2; ++i) {
            double value = frame[i];
            frame[i] = frame[i + fftsize/2];
            frame[i + fftsize/2] = value;
        }

        window->cut(frame);
        
        FFT::process(fftsize, false, frame, 0, real, imag);
        
        constq->process(real, imag, cqre, cqim);
	
        for (int i = 0; i < ncoeff; ++i) {
            cq[i] += sqrt(cqre[i] * cqre[i] + cqim[i] * cqim[i]);
        }
        ++frames;

        origin += fftsize/2;
    }

    delete [] cqre;
    delete [] cqim;
    delete [] real;
    delete [] imag;
    delete [] frame;

    for (int i = 0; i < ncoeff; ++i) {
//        std::cerr << cq[i] << " ";
        cq[i] /= frames;
    }
//    std::cerr << std::endl;

    if (decimator) delete[] psource;

    features.push_back(cq);
}

void ClusterMeltSegmenter::segment(int m)
{
    nclusters = m;
    segment();
}

void ClusterMeltSegmenter::setFeatures(const vector<vector<double> >& f)
{
    features = f;
    featureType = FEATURE_TYPE_UNKNOWN;
}

void ClusterMeltSegmenter::segment()
{
    if (constq)
    {
        delete constq;
        constq = 0;
        delete decimator;
        decimator = 0;
    }
	
    std::cerr << "ClusterMeltSegmenter::segment: have " << features.size()
              << " features with " << features[0].size() << " coefficients (ncoeff = " << ncoeff << ", ncomponents = " << ncomponents << ")" << std::endl;

    // copy the features to a native array and use the existing C segmenter...
    double** arrFeatures = new double*[features.size()];	
    for (int i = 0; i < features.size(); i++)
    {
        if (featureType == FEATURE_TYPE_UNKNOWN) {
            arrFeatures[i] = new double[features[0].size()];
            for (int j = 0; j < features[0].size(); j++)
                arrFeatures[i][j] = features[i][j];	
        } else {
            arrFeatures[i] = new double[ncoeff+1];	// allow space for the normalised envelope
            for (int j = 0; j < ncoeff; j++)
                arrFeatures[i][j] = features[i][j];	
        }
    }
	
    q = new int[features.size()];
	
    if (featureType == FEATURE_TYPE_UNKNOWN)
        cluster_segment(q, arrFeatures, features.size(), features[0].size(), nHMMStates, histogramLength, 
                        nclusters, neighbourhoodLimit);
    else
        constq_segment(q, arrFeatures, features.size(), nbins, ncoeff, featureType, 
                       nHMMStates, histogramLength, nclusters, neighbourhoodLimit);
	
    // convert the cluster assignment sequence to a segmentation
    makeSegmentation(q, features.size());		
	
    // de-allocate arrays
    delete [] q;
    for (int i = 0; i < features.size(); i++)
        delete [] arrFeatures[i];
    delete [] arrFeatures;
	
    // clear the features
    clear();
}

void ClusterMeltSegmenter::makeSegmentation(int* q, int len)
{
    segmentation.segments.clear();
    segmentation.nsegtypes = nclusters;
    segmentation.samplerate = samplerate;
	
    Segment segment;
    segment.start = 0;
    segment.type = q[0];
	
    for (int i = 1; i < len; i++)
    {
        if (q[i] != q[i-1])
        {
            segment.end = i * getHopsize();
            segmentation.segments.push_back(segment);
            segment.type = q[i];
            segment.start = segment.end;
        }
    }
    segment.end = len * getHopsize();
    segmentation.segments.push_back(segment);
}
