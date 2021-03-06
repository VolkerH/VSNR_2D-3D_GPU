

// --------------------------------------------------- //
//             VSNR DYNAMIC LIBRARY CUDA               //
// --------------------------------------------------- //

/////////////////////////////////////////////////////////
//      IMPORTANT : cuda expect row-major 3D array     //
/////////////////////////////////////////////////////////


#ifdef __linux
#define _export_ extern "C"
#elif _WIN32
#define _export_ extern "C" __declspec(dllexport)
#endif


#include <math.h>
#include <stdio.h>
#include "cuda.h"
#include "cuda_runtime.h"
#include "cufft.h"
#include <cuda_runtime.h>
#include <cublas_v2.h>

#define PI (3.141592653589793)

#define SQ(a) ((a)*(a))
#define CB(a) ((a)*(a)*(a))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

typedef cufftComplex CuC; // struct { float x, y }
typedef cufftReal    CuR; // float


// DEBUG
// -------------------------------------------------------------------------


// Disp lastCudaError in file
void __dispLastCudaError(FILE* file, const char* string)
{
    // -
    fprintf(file,"~ %s: %s\n", string, cudaGetErrorString(cudaGetLastError()));
}

// Disp a string relative to err from a cufft function in file
void __dispCufftError(FILE* file, const char* string, int err)
{
    switch (err) {
        case 0 :
            fprintf(file, "# %s: CUFFT_SUCCESS\n", string);
            break;
        case 1 :
            fprintf(file, "# %s: CUFFT_INVALID_PLAN\n", string);
            break;
        case 2 :
            fprintf(file, "# %s: CUFFT_ALLOC_FAILED\n", string);
            break;
        case 3 :
            fprintf(file, "# %s: CUFFT_INVALID_TYPE\n", string);
            break;
        case 4 :
            fprintf(file, "# %s: CUFFT_INVALID_VALUE\n", string);
            break;
        case 5 :
            fprintf(file, "# %s: CUFFT_INTERNAL_ERROR\n", string);
            break;
        case 6 :
            fprintf(file, "# %s: CUFFT_EXEC_FAILED\n", string);
            break;
        case 7 :
            fprintf(file, "# %s: CUFFT_SETUP_FAILED\n", string);
            break;
        case 8 :
            fprintf(file, "# %s: CUFFT_INVALID_SIZE\n", string);
            break;
        case 9 :
            fprintf(file, "# %s: CUFFT_UNALIGNED_DATA\n", string);
            break;
        case 10 :
            fprintf(file, "# %s: CUFFT_INCOMPLETE_PARAMETER_LIST\n", string);
            break;
        case 11 :
            fprintf(file, "# %s: CUFFT_INVALID_DEVICE\n", string);
            break;
        case 12 :
            fprintf(file, "# %s: CUFFT_PARSE_ERROR\n", string);
            break;
        case 13 :
            fprintf(file, "# %s: CUFFT_NO_WORKSPACE\n", string);
            break;
        case 14 :
            fprintf(file, "# %s: CUFFT_NOT_IMPLEMENTED\n", string);
            break;
        case 15 :
            fprintf(file, "# %s: CUFFT_LICENSE_ERROR\n", string);
            break;
        case 16 :
            fprintf(file, "# %s: CUFFT_NOT_SUPPORTED\n", string);
            break;
        default :
            fprintf(file, "# %s: UNKNOWN_ERROR\n", string);
    }
}


// -------------------------------------------------------------------------


// Computes out = u1.*u2
__global__ void product_carray(CuC* u1, CuC* u2, CuC* out, long n)
{
    long i    = blockIdx.x * blockDim.x + threadIdx.x;
    long step = blockDim.x * gridDim.x;

    for ( ; i < n ; i += step) {
        out[i].x = (u1[i].x * u2[i].x) - (u1[i].y * u2[i].y);
        out[i].y = (u1[i].y * u2[i].x) + (u1[i].x * u2[i].y);
    }
}

// Normalize an array
__global__ void normalize(CuR* u, long n)
{
    long i    = blockIdx.x * blockDim.x + threadIdx.x;
    long step = blockDim.x * gridDim.x;

    for ( ; i < n ; i += step)
        u[i] = u[i] / (float)n;
}

// u = u*val;
__global__ void multiply(CuR* u, long n, float val)
{
    long i    = blockIdx.x * blockDim.x + threadIdx.x;
    long step = blockDim.x * gridDim.x;

    for ( ; i < n ; i += step)
        u[i] = u[i] * val;
}

// u = u/val;
__global__ void divide(CuR* u, long n, float val)
{
    long i    = blockIdx.x * blockDim.x + threadIdx.x;
    long step = blockDim.x * gridDim.x;

    for ( ; i < n ; i += step)
        u[i] = u[i] / val;
}

// adds two vectors w = u + v
__global__ void add(CuR* u, CuR* v, CuR* w, long n)
{
    long i    = blockIdx.x * blockDim.x + threadIdx.x;
    long step = blockDim.x * gridDim.x;

    for ( ; i < n ; i += step)
        w[i] = u[i] + v[i];
}

// substracts two vectors w = u - v
__global__ void substract(CuR* u, CuR* v, CuR* w, long n)
{
    long i    = blockIdx.x * blockDim.x + threadIdx.x;
    long step = blockDim.x * gridDim.x;

    for ( ; i < n ; i += step)
        w[i] = u[i] - v[i];
}

// Sets finite difference 1
__global__ void setd1(CuR* d1, long n, int n0, int n1, float dx)
{
    long i     = blockIdx.x * blockDim.x + threadIdx.x ;
    long step  = blockDim.x * gridDim.x;
    long id[2] = {0, n1-1};

    for ( ; i < n ; i += step) {
        if      (i == id[0]) d1[i] =  1.0 / dx;
        else if (i == id[1]) d1[i] = -1.0 / dx;
        else                 d1[i] =  0.0;
    }
}

// Sets finite difference 2
__global__ void setd2(CuR* d2, long n, int n0, int n1, float dy)
{
    long i     = blockIdx.x * blockDim.x + threadIdx.x ;
    long step  = blockDim.x * gridDim.x;
    long id[2] = {0, n1*(n0-1)};

    for ( ; i < n ; i += step) {
        if      (i == id[0]) d2[i] =  1.0 / dy;
        else if (i == id[1]) d2[i] = -1.0 / dy;
        else                 d2[i] =  0.0;
    }
}

// Sets finite difference 3
__global__ void setd3(CuR* d3, long n, int n0, int n1, float dz)
{
    long i     = blockIdx.x * blockDim.x + threadIdx.x ;
    long step  = blockDim.x * gridDim.x;
    long id[2] = {0, n-(n1*n0)};

    for ( ; i < n ; i += step) {
        if      (i == id[0]) d3[i] =  1.0 / dz;
        else if (i == id[1]) d3[i] = -1.0 / dz;
        else                 d3[i] =  0.0;
    }
}

// Compute Phi
__global__ void compute_phi(CuC* fphi1, CuC* fphi2, CuC* fphi3, CuC* fphi, float beta, long n)
{
    long i    = blockIdx.x * blockDim.x + threadIdx.x;
    long step = blockDim.x * gridDim.x;

    for ( ; i < n ; i += step) {
        fphi[i].x = 1 + beta*(SQ(fphi1[i].x) + SQ(fphi1[i].y) + SQ(fphi2[i].x) + SQ(fphi2[i].y) + SQ(fphi3[i].x) + SQ(fphi3[i].y));
        fphi[i].y = 0.0;
    }
}

// Computes tmpi = -lambdai + beta * yi
__global__ void betay_m_lambda(CuR* l1, CuR* l2, CuR* l3, CuR* y1, CuR* y2, CuR* y3, CuR* tmp1, CuR* tmp2, CuR* tmp3, float beta, long n)
{
    long i    = blockIdx.x * blockDim.x + threadIdx.x;
    long step = blockDim.x * gridDim.x;

    for ( ; i < n ; i += step) {
        tmp1[i] = (beta * y1[i]) - l1[i];
        tmp2[i] = (beta * y2[i]) - l2[i];
        tmp3[i] = (beta * y3[i]) - l3[i];
    }
}

// Computes w = conj(u) * v
__global__ void conju_x_v(CuC* u, CuC* v, CuC* w, long n)
{
    long i    = blockIdx.x * blockDim.x + threadIdx.x;
    long step = blockDim.x * gridDim.x;
    float a1, a2, b1, b2;

    for ( ; i < n ; i += step) {
        a1 = u[i].x;
        b1 = u[i].y;
        a2 = v[i].x;
        b2 = v[i].y;
        w[i].x = (a1 * a2) + (b1 * b2);
        w[i].y = (b2 * a1) - (b1 * a2);
    }
}

// fx = (ftmp1 + ftmp2 + ftmp3) / fphi;
__global__ void update_fx(CuC* ftmp1, CuC* ftmp2, CuC* ftmp3, CuC* fphi, CuC* fx, long n)
{
    long i    = blockIdx.x * blockDim.x + threadIdx.x;
    long step = blockDim.x * gridDim.x;

    for ( ; i < n ; i += step) {
        fx[i].x = (ftmp1[i].x + ftmp2[i].x + ftmp3[i].x) / fphi[i].x;
        fx[i].y = (ftmp1[i].y + ftmp2[i].y + ftmp3[i].y) / fphi[i].x;
    }
}

// -
__global__ void update_y(CuR* d1u0, CuR* d2u0, CuR* d3u0, CuR* tmp1, CuR* tmp2, CuR* tmp3, CuR* l1, CuR* l2, CuR* l3, CuR* y1, CuR* y2, CuR* y3, float beta, long n)
{
    long i    = blockIdx.x * blockDim.x + threadIdx.x;
    long step = blockDim.x * gridDim.x;
    float ng, t1, t2, t3;

    for ( ; i < n ; i += step) {
        t1 = d1u0[i] - (tmp1[i] + (l1[i] / beta));
        t2 = d2u0[i] - (tmp2[i] + (l2[i] / beta));
        t3 = d3u0[i] - (tmp3[i] + (l3[i] / beta));
        ng = sqrtf(SQ(t1) + SQ(t2) + SQ(t3));

        if (ng > 1.0 / beta) {
            y1[i] = d1u0[i] - t1 * (1.0 - (1.0 / (beta * ng)));
            y2[i] = d2u0[i] - t2 * (1.0 - (1.0 / (beta * ng)));
            y3[i] = d3u0[i] - t3 * (1.0 - (1.0 / (beta * ng)));
        } else {
            y1[i] = d1u0[i];
            y2[i] = d2u0[i];
            y3[i] = d3u0[i];
        }
    }
}

// -
__global__ void update_lambda(CuR* lambda, CuR* tmp, CuR* y, float beta, long n)
{
    long i    = blockIdx.x * blockDim.x + threadIdx.x ;
    long step = blockDim.x * gridDim.x;

    for ( ; i < n ; i += step)
        lambda[i] = lambda[i] + (beta * (tmp[i] - y[i]));
}

// Main function
void VSNR_ADMM_GPU(float *u0, float *psi, int n0, int n1, int n2, int nit, float beta, float *u, int dimGrid, int dimBlock, float dx, float dy, float dz)
{
    cufftHandle planR2C, planC2R;

    CuC *fpsi, *fu0, *fphi, *fx; // complex

    CuC *fphi1, *fphi2, *fphi3; // complex
    CuC *ftmp1, *ftmp2, *ftmp3; // complex
    CuR  *tmp1,  *tmp2,  *tmp3; // real
    CuR  *d1u0,  *d2u0,  *d3u0; // real
    CuC   *fd1,   *fd2,   *fd3; // complex
    CuR    *d1,    *d2,    *d3; // real
    CuR    *y1,    *y2,    *y3; // real
    CuR    *l1,    *l2,    *l3; // real

    long n = n0*n1*n2;
    long m = n0*n2*(n1/2+1);

    cudaMalloc((void**)&fpsi, m*sizeof(CuC));
    cudaMalloc((void**)&fu0,  m*sizeof(CuC));

    cudaMalloc((void**)&d1u0, n*sizeof(CuR));
    cudaMalloc((void**)&d2u0, n*sizeof(CuR));
    cudaMalloc((void**)&d3u0, n*sizeof(CuR));

    cudaMalloc((void**)&tmp1, n*sizeof(CuR));
    cudaMalloc((void**)&tmp2, n*sizeof(CuR));
    cudaMalloc((void**)&tmp3, n*sizeof(CuR));

    cudaMalloc((void**)&ftmp1, m*sizeof(CuC));
    cudaMalloc((void**)&ftmp2, m*sizeof(CuC));
    cudaMalloc((void**)&ftmp3, m*sizeof(CuC));

    cufftPlan3d(&planR2C, n2, n0, n1, CUFFT_R2C);
    cufftPlan3d(&planC2R, n2, n0, n1, CUFFT_C2R);

    cufftExecR2C(planR2C,  u0,  fu0); // fu0  = fftn(u0);
    cufftExecR2C(planR2C, psi, fpsi); // fpsi = fftn(psi);

    // Computes d1u0 & fphi1
    cudaMalloc((void**)&d1,    n*sizeof(CuR));
    cudaMalloc((void**)&fd1,   m*sizeof(CuC));
    cudaMalloc((void**)&fphi1, m*sizeof(CuC));

    setd1<<<dimGrid,dimBlock>>>(d1, n, n0, n1, dx); // d1[0] = 1; d1[n1-1] = -1;
    cufftExecR2C(planR2C, d1, fd1); // fd1 = fft(d1);
    cudaFree(d1);

    product_carray<<<dimGrid,dimBlock>>>(fd1, fu0, ftmp1, m);
    cufftExecC2R(planC2R, ftmp1, d1u0); // d1u0 = ifftn(fd1.*fu0);
    normalize<<<dimGrid,dimBlock>>>(d1u0, n);

    product_carray<<<dimGrid,dimBlock>>>(fd1, fpsi, fphi1, m); // fphi1 = fpsi.*fd1;
    cudaFree(fd1);

    // Computes d2u0 & fphi2
    cudaMalloc((void**)&d2,    n*sizeof(CuR));
    cudaMalloc((void**)&fd2,   m*sizeof(CuC));
    cudaMalloc((void**)&fphi2, m*sizeof(CuC));

    setd2<<<dimGrid,dimBlock>>>(d2, n, n0, n1, dy); // d2[0] = 1; d2[n0n1-n1] = -1;
    cufftExecR2C(planR2C, d2, fd2); // fd2 = fft(d2);
    cudaFree(d2);

    product_carray<<<dimGrid,dimBlock>>>(fd2, fu0, ftmp2, m);
    cufftExecC2R(planC2R, ftmp2, d2u0); // d2u0 = ifftn(fd2.*fu0);
    normalize<<<dimGrid,dimBlock>>>(d2u0, n);


    product_carray<<<dimGrid,dimBlock>>>(fd2, fpsi, fphi2, m); // fphi2 = fpsi.*fd2;
    cudaFree(fd2);

    // Computes d3u0 & fphi3
    cudaMalloc((void**)&d3,    n*sizeof(CuR));
    cudaMalloc((void**)&fd3,   m*sizeof(CuC));
    cudaMalloc((void**)&fphi3, m*sizeof(CuC));

    setd3<<<dimGrid,dimBlock>>>(d3, n, n0, n1, dz); // d3[0] = 1; d3[n-n0n1] = -1;
    cufftExecR2C(planR2C, d3, fd3); // fd3 = fft(d3);
    cudaFree(d3);

    product_carray<<<dimGrid,dimBlock>>>(fd3, fu0, ftmp3, m);
    cufftExecC2R(planC2R, ftmp3, d3u0); // d3u0 = ifftn(fd3.*fu0);
    normalize<<<dimGrid,dimBlock>>>(d3u0, n);

    product_carray<<<dimGrid,dimBlock>>>(fd3, fpsi, fphi3, m); // fphi3 = fpsi.*fd3;
    cudaFree(fd3);

    // unused till end
    cudaFree(fu0);

    // Computes fphi
    cudaMalloc((void**)&fphi, m*sizeof(CuC));
    compute_phi<<<dimGrid,dimBlock>>>(fphi1, fphi2, fphi3, fphi, beta, m);

    // Initialization
    cudaMalloc((void**)&y1, n*sizeof(CuR));
    cudaMalloc((void**)&y2, n*sizeof(CuR));
    cudaMalloc((void**)&y3, n*sizeof(CuR));

    cudaMalloc((void**)&l1, n*sizeof(CuR));
    cudaMalloc((void**)&l2, n*sizeof(CuR));
    cudaMalloc((void**)&l3, n*sizeof(CuR));

    cudaMalloc((void**)&fx, m*sizeof(CuC));

    cudaMemset(y1, 0, n*sizeof(CuR));
    cudaMemset(y2, 0, n*sizeof(CuR));
    cudaMemset(y3, 0, n*sizeof(CuR));

    cudaMemset(l1, 0, n*sizeof(CuR));
    cudaMemset(l2, 0, n*sizeof(CuR));
    cudaMemset(l3, 0, n*sizeof(CuR));

    // Main algorithm
    for (int k = 0 ; k < nit ; ++k) {

        // -------------------------------------------------------------
        // First step, x update : (I+beta ATA)x = AT (-lambda+beta*ATy)
        // -------------------------------------------------------------
        // ftmp1 = conj(fphi1).*(fftn(-lambda1+beta*y1));
        // ftmp2 = conj(fphi2).*(fftn(-lambda2+beta*y2));
        // ftmp3 = conj(fphi3).*(fftn(-lambda3+beta*y3));
        betay_m_lambda<<<dimGrid,dimBlock>>>(l1, l2, l3, y1, y2, y3, tmp1, tmp2, tmp3, beta, n);
        cufftExecR2C(planR2C, tmp1, ftmp1);
        cufftExecR2C(planR2C, tmp2, ftmp2);
        cufftExecR2C(planR2C, tmp3, ftmp3);
        conju_x_v<<<dimGrid,dimBlock>>>(fphi1, ftmp1, ftmp1, m);
        conju_x_v<<<dimGrid,dimBlock>>>(fphi2, ftmp2, ftmp2, m);
        conju_x_v<<<dimGrid,dimBlock>>>(fphi3, ftmp3, ftmp3, m);
        update_fx<<<dimGrid,dimBlock>>>(ftmp1, ftmp2, ftmp3, fphi, fx, m);

        // --------------------------------------------------------
        // Second step y update : y = prox_{f1/beta}(Ax+lambda/beta)
        // --------------------------------------------------------
        product_carray<<<dimGrid,dimBlock>>>(fphi1, fx, ftmp1, m);
        product_carray<<<dimGrid,dimBlock>>>(fphi2, fx, ftmp2, m);
        product_carray<<<dimGrid,dimBlock>>>(fphi3, fx, ftmp3, m);
        cufftExecC2R(planC2R, ftmp1, tmp1); // tmp1 = Ax1
        cufftExecC2R(planC2R, ftmp2, tmp2); // tmp2 = Ax2
        cufftExecC2R(planC2R, ftmp3, tmp3); // tmp2 = Ax2
        normalize<<<dimGrid,dimBlock>>>(tmp1, n);
        normalize<<<dimGrid,dimBlock>>>(tmp2, n);
        normalize<<<dimGrid,dimBlock>>>(tmp3, n);
        update_y<<<dimGrid,dimBlock>>>(d1u0, d2u0, d3u0, tmp1, tmp2, tmp3, l1, l2, l3, y1, y2, y3, beta, n);

        // --------------------------
        // Third step lambda update
        // --------------------------
        update_lambda<<<dimGrid,dimBlock>>>(l1, tmp1, y1, beta, n);
        update_lambda<<<dimGrid,dimBlock>>>(l2, tmp2, y2, beta, n);
        update_lambda<<<dimGrid,dimBlock>>>(l3, tmp3, y3, beta, n);

    }

    // Last but not the least : u = u0 - (psi * x)
    product_carray<<<dimGrid,dimBlock>>>(fx, fpsi, ftmp1, m);
    cufftExecC2R(planC2R, ftmp1, u);
    normalize<<<dimGrid,dimBlock>>>(u, n);
    substract<<<dimGrid,dimBlock>>>(u0, u, u, n);

    // Free memory
    cudaFree(fpsi);
    cudaFree(fphi);
    cudaFree(fx);

    cudaFree(fphi1);
    cudaFree(fphi2);
    cudaFree(fphi3);

    cudaFree(ftmp1);
    cudaFree(ftmp2);
    cudaFree(ftmp3);

    cudaFree(d1u0);
    cudaFree(d2u0);
    cudaFree(d3u0);

    cudaFree(y1);
    cudaFree(y2);
    cudaFree(y3);

    cudaFree(l1);
    cudaFree(l2);
    cudaFree(l3);

    cudaFree(tmp1);
    cudaFree(tmp2);
    cudaFree(tmp3);

    cufftDestroy(planR2C);
    cufftDestroy(planC2R);
}

// Sets Gabor
__global__ void create_gabor(CuR* psi, int n0, int n1, int n2, float level, float sigmax, float sigmay, float sigmaz, float thetax, float thetay, float thetaz, float phase, float lambda)
{
    long n = n0*n1*n2;
    long c = blockIdx.x * blockDim.x + threadIdx.x;
    long step = blockDim.x * gridDim.x;

    float tx = thetax * PI / 180.0;
    float ty = thetay * PI / 180.0;
    float tz = thetaz * PI / 180.0;

    float off_x = (n1 / 2) + 1;
    float off_y = (n0 / 2) + 1;
    float off_z = (n2 / 2) + 1;

    float x, y, z;
    float x_t, y_t, z_t;

    float val, nn;
    int i, j, k;

    float cx = cos(tx);
    float sx = sin(tx);

    float cy = cos(ty);
    float sy = sin(ty);

    float cz = cos(tz);
    float sz = sin(tz);

    phase = phase * PI / 180.0;
    nn    = PI / sqrtf(sigmax*sigmay*sigmaz);

    for ( ; c < n ; c += step) {

        i =  c % n1;
        j = (c / n1) % n0;
        k =  c / (n1*n0);

        x = off_x - i;
        y = off_y - j;
        z = off_z - k;

        x_t = (x*(cy*cz))              - (y*(sz*cy))              + (z*sy);
        y_t = (x*((sy*sx*cz)+(sz*cx))) + (y*((cx*cz)-(sz*sy*sx))) - (z*(sx*cy));
        z_t = (x*((sz*sx)-(sy*cx*cz))) + (y*((sx*cz)+(sy*sz*cx))) + (z*(cy*cx));

        val = exp(-0.5*(SQ(x_t/sigmax)+SQ(y_t/sigmay)+SQ(z_t/sigmaz))) * cos((x_t*lambda/sigmax)+phase);
        psi[c] = level * val / nn;

    }
}

// Sets dirac
__global__ void create_dirac(CuR* psi, float val, long n)
{
    long i    = blockIdx.x * blockDim.x + threadIdx.x;
    long step = blockDim.x * gridDim.x;

    for ( ; i < n ; i += step) {
        if (i == 0) psi[i] = val;
        else        psi[i] = 0.0;
    }
}

// Sets Psi = |Psi|^2
__global__ void compute_squared_norm(CuC* fpsi, long m)
{
    long i    = blockIdx.x * blockDim.x + threadIdx.x;
    long step = blockDim.x * gridDim.x;

    for ( ; i < m ; i += step) {
        fpsi[i].x = SQ(fpsi[i].x) + SQ(fpsi[i].y);
        fpsi[i].y = 0.0;
    }
}

// Sets Psi = sqrtf(|Psi|^2)
__global__ void compute_norm(CuC* fpsi, long m)
{
    long i    = blockIdx.x * blockDim.x + threadIdx.x;
    long step = blockDim.x * gridDim.x;

    for ( ; i < m ; i += step) {
        fpsi[i].x = sqrtf(SQ(fpsi[i].x) + SQ(fpsi[i].y));
        fpsi[i].y = 0.0;
    }
}

// Sets fsum = sqrtf(fsum)
__global__ void compute_sqrtf(CuC* fsum, long m)
{
    long i    = blockIdx.x * blockDim.x + threadIdx.x;
    long step = blockDim.x * gridDim.x;

    for ( ; i < m ; i += step) {
        fsum[i].x = sqrtf(fsum[i].x);
        fsum[i].y = 0.0;
    }
}

// Sets ftmp = fpsi * fd
__global__ void compute_product(CuC* fpsi, CuC* fd, float* ftmp, long m)
{
    long i    = blockIdx.x * blockDim.x + threadIdx.x;
    long step = blockDim.x * gridDim.x;

    for ( ; i < m ; i += step) {
        ftmp[i] = fpsi[i].x * fd[i].x;
    }
}

// Sets fsum += fpsitemp / alpha
__global__ void update_psi(CuC* fpsitemp, CuC* fsum, float alpha, long m)
{
    long i    = blockIdx.x * blockDim.x + threadIdx.x;
    long step = blockDim.x * gridDim.x;

    for ( ; i < m ; i += step) {
        fsum[i].x += fpsitemp[i].x / alpha;
    }
}

// TOSEE
// This function creates the filters from a Java list of filters
void CREATE_FILTERS(float* psis, float *gu0, int length, float* gpsi, int n0, int n1, int n2, int dimGrid, int dimBlock, float dx, float dy, float dz)
{
    int  i = 0;
    long n = n0*n1*n2;
    long m = n0*n2*(n1/2+1);

    cufftHandle planR2C, planC2R;
    cublasHandle_t handle;

    float eta, alpha, mmax, norm;
    float *psitemp, *ftmp;
    CuC *fpsitemp, *fsum;
    CuC *fd1, *fd2, *fd3;
    CuR  *d1,  *d2,  *d3;
    float max1,  max2,  max3;
    int imax;

    cudaMalloc((void**)&psitemp,  n*sizeof(float));
    cudaMalloc((void**)&ftmp, 	  m*sizeof(float));
    cudaMalloc((void**)&fpsitemp, m*sizeof(CuC));
    cudaMalloc((void**)&fsum, 	  m*sizeof(CuC));
    cudaMalloc((void**)&fd1,	  m*sizeof(CuC));
    cudaMalloc((void**)&fd2,      m*sizeof(CuC));
    cudaMalloc((void**)&fd3,	  m*sizeof(CuC));
    cudaMalloc((void**)&d1,       n*sizeof(CuR));
    cudaMalloc((void**)&d2,       n*sizeof(CuR));
    cudaMalloc((void**)&d3,       n*sizeof(CuR));

    cudaMemset(fsum, 0, m*sizeof(CuC));

    cublasCreate(&handle);

    cufftPlan3d(&planR2C, n2, n0, n1, CUFFT_R2C);
    cufftPlan3d(&planC2R, n2, n0, n1, CUFFT_C2R);

    // Computes the l2 norm of u0 on GPU
    cublasSnrm2(handle, n, gu0, 1, &norm);

    // Computes d1 and fd1
    setd1<<<dimGrid,dimBlock>>>(d1, n, n0, n1, dx);
    cufftExecR2C(planR2C, d1, fd1);
    compute_norm<<<dimGrid,dimBlock>>>(fd1, m);
    cudaFree(d1);

    // Computes d2 and fd2
    setd2<<<dimGrid,dimBlock>>>(d2, n, n0, n1, dy);
    cufftExecR2C(planR2C, d2, fd2);
    compute_norm<<<dimGrid,dimBlock>>>(fd2, m);
    cudaFree(d2);

    // Computes d3 and fd3
    setd3<<<dimGrid,dimBlock>>>(d3, n, n0, n1, dz);
    cufftExecR2C(planR2C, d3, fd3);
    compute_norm<<<dimGrid,dimBlock>>>(fd3, m);
    cudaFree(d3);

    // Computes PSI = sum_{i=1}^m |PSI_i|^2/alpha_i, where alpha_i is defined in the paper.
    while (i < length) {

        if (psis[i] == 0.0) {
            create_dirac<<<dimGrid,dimBlock>>>(psitemp, 1, n);
            eta = psis[i+1];
            i += 2;
        } else if (psis[i] == 1.0) {
            // 1 : amplitude, 
            // 2 : sigmaX, 3 : sigmaY, 4 : sigmaZ,
            // 5 : thetaX, 6 : thetaY, 7 : thetaZ,
            create_gabor<<<dimGrid,dimBlock>>>(psitemp, n0, n1, n2, 1.0, psis[i+2], psis[i+3], psis[i+4], psis[i+5], psis[i+6], psis[i+7], 0.0, 0.0);
            eta = psis[i+1];
            i += 8;
        }

        cufftExecR2C(planR2C, psitemp, fpsitemp);

        compute_squared_norm<<<dimGrid,dimBlock>>>(fpsitemp, m); // fpsitemp = |fpsitemp|^2;

        compute_product<<<dimGrid,dimBlock>>>(fpsitemp, fd1, ftmp, m); // ftmp = |fd1|*|fpsitemp|;
        cublasIsamax(handle, (int)m, ftmp, 1, &imax);
        cudaMemcpy(&max1, &ftmp[imax-1], sizeof(float), cudaMemcpyDeviceToHost); // max1 = ftmp[imax];

        compute_product<<<dimGrid,dimBlock>>>(fpsitemp, fd2, ftmp, m); // ftmp = |fd2|*|fpsitemp|;
        cublasIsamax(handle, (int)m, ftmp, 1, &imax);
        cudaMemcpy(&max2, &ftmp[imax-1], sizeof(float), cudaMemcpyDeviceToHost); // max2 = ftmp[imax];

        compute_product<<<dimGrid,dimBlock>>>(fpsitemp, fd3, ftmp, m); // ftmp = |fd3|*|fpsitemp|;
        cublasIsamax(handle, (int)m, ftmp, 1, &imax);
        cudaMemcpy(&max3, &ftmp[imax-1], sizeof(float), cudaMemcpyDeviceToHost); // max3 = ftmp[imax];

        mmax = MAX(max1, max2);
        mmax = MAX(mmax, max3);

        alpha = sqrtf((float)n) * SQ((float)n) * mmax / (norm * eta);

        update_psi<<<dimGrid,dimBlock>>>(fpsitemp, fsum, alpha, m); // fsum += |fpsitemp|^2 / alpha_i;

    }

    compute_sqrtf<<<dimGrid,dimBlock>>>(fsum, m); // fsum = sqrtf(fsum);
    cufftExecC2R(planC2R, fsum, gpsi);

    cudaFree(psitemp);
    cudaFree(fpsitemp);
    cudaFree(ftmp);
    cudaFree(fsum);

    cudaFree(fd1);
    cudaFree(fd2);
    cudaFree(fd3);

    cufftDestroy(planR2C);
    cufftDestroy(planC2R);
    cublasDestroy(handle);
}

// -
_export_ int getMaxGrid()
{
    struct cudaDeviceProp properties;
    int device;
    cudaGetDevice(&device);
    cudaGetDeviceProperties(&properties, device);
    return properties.maxGridSize[1];
}

// -
_export_ int getMaxBlocks()
{
    struct cudaDeviceProp properties;
    int device;
    cudaGetDevice(&device);
    cudaGetDeviceProperties(&properties, device);
    return properties.maxThreadsDim[0];
}

// -
_export_ void VSNR_3D_FIJI_GPU(float* psis, int length, float *u0, int n0, int n1, int n2, int nit, float beta, float *u, int nBlocks, float max, float dx, float dy, float dz)
{
    long n = n0*n1*n2;
    float *gu, *gu0, *gpsi;

    int dimBlock = MIN(nBlocks, getMaxBlocks());
    dimBlock = MAX(dimBlock, 1);
    int dimGrid = MIN(n/dimBlock, getMaxGrid());
    dimGrid = MAX(dimGrid, 1);

    // 1. Alloc memory    
    cudaMalloc((void**)&gu,   n*sizeof(float));
    cudaMalloc((void**)&gpsi, n*sizeof(float));
    cudaMalloc((void**)&gu0,  n*sizeof(float));

    cudaMemcpy(gu0, u0, n*sizeof(float), cudaMemcpyHostToDevice);
    divide<<<dimGrid, dimBlock>>>(gu0, n, max);

    // 2. Prepares filters
    CREATE_FILTERS(psis, gu0, length, gpsi, n0, n1, n2, dimGrid, dimBlock, dx, dy, dz);

    // 3. Denoises the image
    VSNR_ADMM_GPU(gu0, gpsi, n0, n1, n2, nit, beta, gu, dimGrid, dimBlock, dx, dy, dz);

    // 4. Copies the result to u
    multiply<<<dimGrid, dimBlock>>>(gu, n, max);
    cudaMemcpy(u, gu, n*sizeof(float), cudaMemcpyDeviceToHost);

    // 5. Frees memory
    cudaFree(gu);
    cudaFree(gu0);
    cudaFree(gpsi);
}
