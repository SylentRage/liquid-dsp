//
// channel_cccf_example.c
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <getopt.h>
#include <time.h>
#include <assert.h>

#include "liquid.h"

#define OUTPUT_FILENAME "channel_cccf_example.m"

// print usage/help message
void usage()
{
    printf("channel_cccf_example [options]\n");
    printf("  h     : print this help file\n");
    printf("  m     : filter delay (symbols),  default: 3\n");
    printf("  b     : filter excess bandwidth, default: 0.5\n");
    printf("  s     : signal-to-noise ratio,   default: 30dB\n");
    printf("  w     : timing pll bandwidth,    default: 0.02\n");
    printf("  n     : number of symbols,       default: 800\n");
    printf("  t     : timing phase offset [%% symbol], t in [-0.5,0.5], default: -0.2\n");
}


int main(int argc, char*argv[]) {
    srand(time(NULL));

    // options
    unsigned int m           = 3;       // filter delay (symbols)
    float        beta        = 0.5f;    // filter excess bandwidth factor
    unsigned int num_symbols = 800;     // number of data symbols
    float        noise_floor = -60.0f;  // noise floor [dB]
    float        SNRdB       = 30.0f;   // signal-to-noise ratio [dB]
    float        bandwidth   =  0.02f;  // loop filter bandwidth
    float        tau         = -0.2f;   // fractional symbol offset
    float        rate        = 1.001f;  // sample rate offset
    
    float        dphi        =  0.1f;   // carrier frequency offset [radians/sample]
    float        phi         =  2.1f;   // carrier phase offset [radians]

    int dopt;
    while ((dopt = getopt(argc,argv,"hm:b:s:w:n:t:r:")) != EOF) {
        switch (dopt) {
        case 'h':   usage();                        return 0;
        case 'm':   m           = atoi(optarg);     break;
        case 'b':   beta        = atof(optarg);     break;
        case 's':   SNRdB       = atof(optarg);     break;
        case 'w':   bandwidth   = atof(optarg);     break;
        case 'n':   num_symbols = atoi(optarg);     break;
        case 't':   tau         = atof(optarg);     break;
        case 'r':   rate        = atof(optarg);     break;
        default:
            exit(1);
        }
    }

    // validate input
    if (m < 1) {
        fprintf(stderr,"error: m (filter delay) must be greater than 0\n");
        exit(1);
    } else if (beta <= 0.0f || beta > 1.0f) {
        fprintf(stderr,"error: beta (excess bandwidth factor) must be in (0,1]\n");
        exit(1);
    } else if (bandwidth <= 0.0f) {
        fprintf(stderr,"error: timing PLL bandwidth must be greater than 0\n");
        exit(1);
    } else if (num_symbols == 0) {
        fprintf(stderr,"error: number of symbols must be greater than 0\n");
        exit(1);
    } else if (tau < -1.0f || tau > 1.0f) {
        fprintf(stderr,"error: timing phase offset must be in [-1,1]\n");
        exit(1);
    } else if (rate > 1.02f || rate < 0.98f) {
        fprintf(stderr,"error: timing rate offset must be in [1.02,0.98]\n");
        exit(1);
    }

    unsigned int i;

    // derived/fixed values
    unsigned int k  =   2;   // samples per symbol
    unsigned int nx =   num_symbols*k;
    unsigned int ny = (unsigned int) ceilf(rate * k) + 64;

    float complex x[nx];    // interpolated samples
    float complex y[ny];    //
    float complex sym_out[num_symbols + 64];// synchronized symbols

    // 
    // generate input sequence
    //

    // design interpolating filter
    firinterp_crcf interp = firinterp_crcf_create_rnyquist(LIQUID_FIRFILT_RRC,k,m,beta,tau);
    for (i=0; i<num_symbols; i++) {
        // generate random QPSK symbol
        float complex s = cexpf(_Complex_I*0.5f*M_PI*((rand() % 4) + 0.5f));

        // interpolate
        firinterp_crcf_execute(interp, s, &x[i*k]);
    }
    firinterp_crcf_destroy(interp);

    // create channel
    channel_cccf channel = channel_cccf_create();

    // add channel impairments
    channel_cccf_add_awgn(channel, noise_floor, SNRdB);
    channel_cccf_add_carrier_offset(channel, dphi, phi);
    channel_cccf_add_multipath(channel, NULL, 10);

    // apply channel
    channel_cccf_execute(channel, x, nx, y, &ny);

    // destroy channel
    channel_cccf_destroy(channel);

    // 
    // create and run symbol synchronizer
    //

    symtrack_cccf symtrack = symtrack_cccf_create(LIQUID_FIRFILT_RRC,
                                            k, m, beta, LIQUID_MODEM_QPSK);

    unsigned int num_symbols_sync = 0;
    symtrack_cccf_execute_block(symtrack, y, ny, sym_out, &num_symbols_sync);
    symtrack_cccf_destroy(symtrack);

    // print results
    printf("symbols in  : %u\n", num_symbols);
    printf("symbols out : %u\n", num_symbols_sync);

    //
    // export output file
    //

    FILE * fid = fopen(OUTPUT_FILENAME,"w");
    fprintf(fid,"%% %s, auto-generated file\n\n", OUTPUT_FILENAME);
    fprintf(fid,"close all;\nclear all;\n\n");

    fprintf(fid,"num_symbols=%u;\n",num_symbols_sync);

    for (i=0; i<num_symbols_sync; i++)
        fprintf(fid,"z(%3u) = %12.8f + j*%12.8f;\n", i+1, crealf(sym_out[i]), cimagf(sym_out[i]));

    fprintf(fid,"iz0 = 1:round(length(z)*0.5);\n");
    fprintf(fid,"iz1 = round(length(z)*0.5):length(z);\n");
    fprintf(fid,"figure;\n");
    fprintf(fid,"hold on;\n");
    fprintf(fid,"plot(real(z(iz0)),imag(z(iz0)),'x','MarkerSize',4,'Color',[0.6 0.6 0.6]);\n");
    fprintf(fid,"plot(real(z(iz1)),imag(z(iz1)),'o','MarkerSize',4,'Color',[0 0.25 0.5]);\n");
    fprintf(fid,"hold off;\n");
    fprintf(fid,"axis square;\n");
    fprintf(fid,"grid on;\n");
    fprintf(fid,"axis([-1 1 -1 1]*1.6);\n");
    fprintf(fid,"xlabel('In-phase');\n");
    fprintf(fid,"ylabel('Quadrature');\n");
    fprintf(fid,"legend(['first 50%%'],['last 50%%'],'location','northeast');\n");

    fclose(fid);
    printf("results written to %s.\n", OUTPUT_FILENAME);

    // clean it up
    printf("done.\n");
    return 0;
}
