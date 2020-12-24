/* Copyright (c) 2018 Mozilla
                 2008-2011 Octasic Inc.
                 2012-2017 Jean-Marc Valin */
/*
   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

   - Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

   - Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR
   CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
   PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "nnet.h"

/* No AVX2/FMA support */
#ifndef LPCNET_TEST
static float celt_exp2(float x)
{
   int integer;
   float frac;
   union {
      float f;
      opus_uint32 i;
   } res;
   integer = floor(x);
   if (integer < -50)
      return 0;
   frac = x-integer;
   /* K0 = 1, K1 = log(2), K2 = 3-4*log(2), K3 = 3*log(2) - 2 */
   res.f = 0.99992522f + frac * (0.69583354f
           + frac * (0.22606716f + 0.078024523f*frac));
   res.i = (res.i + (integer<<23)) & 0x7fffffff;
   return res.f;
}
#define celt_exp(x) celt_exp2((x)*1.44269504f)

static float tansig_approx(float x)
{
    int i;
    float y, dy;
    float sign=1;
    if (x<0)
    {
       x=-x;
       sign=-1;
    }
    i = (int)floor(.5f+25*x);
    i = IMAX(0, IMIN(200, i));
    x -= .04f*i;
    y = tansig_table[i];
    dy = 1-y*y;
    y = y + x*dy*(1 - y*x);
    return sign*y;
}

static OPUS_INLINE float sigmoid_approx(float x)
{
   return .5f + .5f*tansig_approx(.5f*x);
}

static void softmax(float *y, const float *x, int N)
{
    int i;
    for (i=0;i<N;i++)
        y[i] = celt_exp(x[i]);
}

static void vec_tanh(float *y, const float *x, int N)
{
    int i;
    for (i=0;i<N;i++)
    {
        y[i] = tansig_approx(x[i]);
    }
}

static void vec_sigmoid(float *y, const float *x, int N)
{
    int i;
    for (i=0;i<N;i++)
    {
        y[i] = sigmoid_approx(x[i]);
    }
}
#endif
static void sgemv_accum16(float *out, const float *weights, int rows, int cols, int col_stride, const float *x)
{
   int i, j;
   for (i=0;i<rows;i+=16)
   {
      for (j=0;j<cols;j++)
      {
         const float * restrict w;
         float * restrict y;
         float xj;
         w = &weights[j*col_stride + i];
         xj = x[j];
         y = &out[i];
         y[0] += w[0]*xj;
         y[1] += w[1]*xj;
         y[2] += w[2]*xj;
         y[3] += w[3]*xj;
         y[4] += w[4]*xj;
         y[5] += w[5]*xj;
         y[6] += w[6]*xj;
         y[7] += w[7]*xj;
         y[8] += w[8]*xj;
         y[9] += w[9]*xj;
         y[10] += w[10]*xj;
         y[11] += w[11]*xj;
         y[12] += w[12]*xj;
         y[13] += w[13]*xj;
         y[14] += w[14]*xj;
         y[15] += w[15]*xj;
      }
   }
}

static void sparse_sgemv_accum16(float *out, const float *w, int rows, const int *idx, const float *x)
{
   int i, j;
   for (i=0;i<rows;i+=16)
   {
      int cols;
      cols = *idx++;
      for (j=0;j<cols;j++)
      {
         float * restrict y;
         float xj;
         xj = x[*idx++];
         y = &out[i];
         y[0] += w[0]*xj;
         y[1] += w[1]*xj;
         y[2] += w[2]*xj;
         y[3] += w[3]*xj;
         y[4] += w[4]*xj;
         y[5] += w[5]*xj;
         y[6] += w[6]*xj;
         y[7] += w[7]*xj;
         y[8] += w[8]*xj;
         y[9] += w[9]*xj;
         y[10] += w[10]*xj;
         y[11] += w[11]*xj;
         y[12] += w[12]*xj;
         y[13] += w[13]*xj;
         y[14] += w[14]*xj;
         y[15] += w[15]*xj;
         w += 16;
      }
   }
}

#ifdef DOT_PROD
#define SCALE_1 (1.f/128.f/127.f)
static void sparse_sgemv_accum8x4(float *out, const qweight *w, int rows, const int *idx, const float *x)
{
   int i, j;
   for (i=0;i<rows;i+=8)
   {
      int cols;
      cols = *idx++;
      for (j=0;j<cols;j++)
      {
         int pos;
         float * restrict y;
         int xj0, xj1, xj2, xj3;
         pos = 4 * (*idx++);
         xj0 = floor(.5+127*x[pos+0]);
         xj1 = floor(.5+127*x[pos+1]);
         xj2 = floor(.5+127*x[pos+2]);
         xj3 = floor(.5+127*x[pos+3]);
         y = &out[i];
         y[0] += SCALE_1*(w[0]*xj0+w[1]*xj1+w[2]*xj2+w[3]*xj3);
         y[1] += SCALE_1*(w[4]*xj0+w[5]*xj1+w[6]*xj2+w[7]*xj3);
         y[2] += SCALE_1*(w[8]*xj0+w[9]*xj1+w[10]*xj2+w[11]*xj3);
         y[3] += SCALE_1*(w[12]*xj0+w[13]*xj1+w[14]*xj2+w[15]*xj3);
         y[4] += SCALE_1*(w[16]*xj0+w[17]*xj1+w[18]*xj2+w[19]*xj3);
         y[5] += SCALE_1*(w[20]*xj0+w[21]*xj1+w[22]*xj2+w[23]*xj3);
         y[6] += SCALE_1*(w[24]*xj0+w[25]*xj1+w[26]*xj2+w[27]*xj3);
         y[7] += SCALE_1*(w[28]*xj0+w[29]*xj1+w[30]*xj2+w[31]*xj3);
         w += 32;
      }
   }
}

#else
static void sparse_sgemv_accum8x4(float *out, const qweight *w, int rows, const int *idx, const float *x)
{
   int i, j;
   for (i=0;i<rows;i+=8)
   {
      int cols;
      cols = *idx++;
      for (j=0;j<cols;j++)
      {
         int pos;
         float * restrict y;
         float xj0, xj1, xj2, xj3;
         pos = 4 * (*idx++);
         xj0 = x[pos+0];
         xj1 = x[pos+1];
         xj2 = x[pos+2];
         xj3 = x[pos+3];
         y = &out[i];
         y[0] += w[0]*xj0;
         y[1] += w[1]*xj0;
         y[2] += w[2]*xj0;
         y[3] += w[3]*xj0;
         y[4] += w[4]*xj0;
         y[5] += w[5]*xj0;
         y[6] += w[6]*xj0;
         y[7] += w[7]*xj0;

         y[0] += w[8]*xj1;
         y[1] += w[9]*xj1;
         y[2] += w[10]*xj1;
         y[3] += w[11]*xj1;
         y[4] += w[12]*xj1;
         y[5] += w[13]*xj1;
         y[6] += w[14]*xj1;
         y[7] += w[15]*xj1;

         y[0] += w[16]*xj2;
         y[1] += w[17]*xj2;
         y[2] += w[18]*xj2;
         y[3] += w[19]*xj2;
         y[4] += w[20]*xj2;
         y[5] += w[21]*xj2;
         y[6] += w[22]*xj2;
         y[7] += w[23]*xj2;

         y[0] += w[24]*xj3;
         y[1] += w[25]*xj3;
         y[2] += w[26]*xj3;
         y[3] += w[27]*xj3;
         y[4] += w[28]*xj3;
         y[5] += w[29]*xj3;
         y[6] += w[30]*xj3;
         y[7] += w[31]*xj3;
         w += 32;
      }
   }
}
#endif
