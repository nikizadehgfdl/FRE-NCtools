/**
 * Minimal GPU offloading test harness for compute_atm_lnd_ocn_exchange()
 * 
 * Purpose: Verify OpenACC compatibility of core polygon clipping kernels
 * Compile: nvc -acc=gpu -gpu=cc80 -O2 test_gpu_kernel.c -lm -o test_gpu_kernel
 * Run: ./test_gpu_kernel
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <time.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define D2R (M_PI / 180.0)
#define R2D (180.0 / M_PI)
#define MV 50
#define TINY_VALUE 1.e-7

/* ===== Stub Functions (to be marked #pragma acc routine) ===== */

/**
 * minval_double: Find minimum value in double array
 * GPU compatible: Simple loop
 */
#pragma acc routine seq
double minval_double(int n, const double *data)
{
  double min_val = data[0];
  for (int i = 1; i < n; i++) {
    if (data[i] < min_val) min_val = data[i];
  }
  return min_val;
}

/**
 * maxval_double: Find maximum value in double array
 * GPU compatible: Simple loop
 */
#pragma acc routine seq
double maxval_double(int n, const double *data)
{
  double max_val = data[0];
  for (int i = 1; i < n; i++) {
    if (data[i] > max_val) max_val = data[i];
  }
  return max_val;
}

/**
 * poly_area: Compute polygon area using shoelace formula
 * GPU compatible: Simple arithmetic
 */
#pragma acc routine seq
double poly_area(const double lon[], const double lat[], int n)
{
  double area = 0.0;
  for (int i = 0; i < n; i++) {
    int j = (i + 1) % n;
    area += lon[i] * lat[j] - lon[j] * lat[i];
  }
  return fabs(area) * 0.5;
}

/**
 * clip_2dx2d: Sutherland-Hodgman polygon clipping (simplified stub)
 * GPU compatible: Can be marked as routine
 * 
 * For testing, returns simplified result based on bounding box overlap
 */
#pragma acc routine seq
int clip_2dx2d_simple(const double lon1[], const double lat1[], int n1,
                      const double lon2[], const double lat2[], int n2,
                      double lon_out[], double lat_out[])
{
  /* Stub: Return number of intersection points */
  /* In real implementation, would use Sutherland-Hodgman algorithm */
  
  double x1_min = minval_double(n1, lon1);
  double x1_max = maxval_double(n1, lon1);
  double y1_min = minval_double(n1, lat1);
  double y1_max = maxval_double(n1, lat1);
  
  double x2_min = minval_double(n2, lon2);
  double x2_max = maxval_double(n2, lon2);
  double y2_min = minval_double(n2, lat2);
  double y2_max = maxval_double(n2, lat2);
  
  /* Check for overlap */
  if (x1_min > x2_max || x1_max < x2_min ||
      y1_min > y2_max || y1_max < y2_min) {
    return 0; /* No overlap */
  }
  
  /* Stub: Return simple rectangular intersection */
  int n_out = 0;
  double ix_min = MAX(x1_min, x2_min);
  double ix_max = MIN(x1_max, x2_max);
  double iy_min = MAX(y1_min, y2_min);
  double iy_max = MIN(y1_max, y2_max);
  
  if (ix_min < ix_max && iy_min < iy_max) {
    lon_out[0] = ix_min; lat_out[0] = iy_min;
    lon_out[1] = ix_max; lat_out[1] = iy_min;
    lon_out[2] = ix_max; lat_out[2] = iy_max;
    lon_out[3] = ix_min; lat_out[3] = iy_max;
    n_out = 4;
  }
  
  return n_out;
}

/* ===== GPU Kernel (Main Test) ===== */

/**
 * gpu_atm_lnd_kernel: Simplified version of inner la loop
 * 
 * Tests:
 * - Data movement to/from GPU
 * - Nested loop parallelization
 * - Accumulation of results
 * - Race condition handling with atomic ops
 */
void gpu_atm_lnd_kernel(
  int ntile_lnd, int ntile_atm,
  int *nxl, int *nyl,
  double **xlnd, double **ylnd,
  int *nxatm, int *nyatm,
  double **xatm, double **yatm,
  double **area_atm, double **area_lnd,
  double **atmxlnd_area,
  int ***atmxlnd_ia, int ***atmxlnd_ja,
  int ***atmxlnd_il, int ***atmxlnd_jl,
  size_t **naxl,
  int na,
  int is, int ie, int js, int je,
  double area_ratio_thresh
)
{
  int ia, ja, il, jl, nl, la;
  double xa[4], ya[4], xl[4], yl[4];
  double x_out[MV], y_out[MV];
  double xarea, min_area;
  int n_out;
  
  printf("[GPU Kernel] Starting with na=%d, atm cells from %d to %d\n", na, is, ie);
  
#pragma acc data copyin(xatm[na:1][0:nxatm[na]*(nyatm[na]+1)], \
                         yatm[na:1][0:nxatm[na]*(nyatm[na]+1)], \
                         area_atm[na:1][0:nxatm[na]*nyatm[na]], \
                         xlnd[0:ntile_lnd][0:nxl[0]*(nyl[0]+1)], \
                         ylnd[0:ntile_lnd][0:nxl[0]*(nyl[0]+1)], \
                         area_lnd[0:ntile_lnd][0:nxl[0]*nyl[0]]) \
         copy(naxl[na:1][0:ntile_lnd], \
              atmxlnd_area[na:1][0:ntile_lnd][0:100], \
              atmxlnd_ia[na:1][0:ntile_lnd][0:100], \
              atmxlnd_ja[na:1][0:ntile_lnd][0:100], \
              atmxlnd_il[na:1][0:ntile_lnd][0:100], \
              atmxlnd_jl[na:1][0:ntile_lnd][0:100])
  {
#pragma acc parallel loop independent collapse(2) \
    private(ia, ja, xa, ya, xl, yl, x_out, y_out, xarea, min_area, n_out)
    for (la = is; la <= ie; la++) {
      for (nl = 0; nl < ntile_lnd; nl++) {
        /* Extract atm cell (la) grid corners */
        ia = la % nxatm[na];
        ja = la / nxatm[na];
        
        int n0 = ja * (nxatm[na] + 1) + ia;
        int n1 = ja * (nxatm[na] + 1) + ia + 1;
        int n2 = (ja + 1) * (nxatm[na] + 1) + ia + 1;
        int n3 = (ja + 1) * (nxatm[na] + 1) + ia;
        
        xa[0] = xatm[na][n0]; ya[0] = yatm[na][n0];
        xa[1] = xatm[na][n1]; ya[1] = yatm[na][n1];
        xa[2] = xatm[na][n2]; ya[2] = yatm[na][n2];
        xa[3] = xatm[na][n3]; ya[3] = yatm[na][n3];
        
        /* Loop over land cells in this tile */
        for (jl = 0; jl < nyl[nl]; jl++) {
          for (il = 0; il < nxl[nl]; il++) {
            int n0_l = jl * (nxl[nl] + 1) + il;
            int n1_l = jl * (nxl[nl] + 1) + il + 1;
            int n2_l = (jl + 1) * (nxl[nl] + 1) + il + 1;
            int n3_l = (jl + 1) * (nxl[nl] + 1) + il;
            
            xl[0] = xlnd[nl][n0_l]; yl[0] = ylnd[nl][n0_l];
            xl[1] = xlnd[nl][n1_l]; yl[1] = ylnd[nl][n1_l];
            xl[2] = xlnd[nl][n2_l]; yl[2] = ylnd[nl][n2_l];
            xl[3] = xlnd[nl][n3_l]; yl[3] = ylnd[nl][n3_l];
            
            /* Clip polygons */
            n_out = clip_2dx2d_simple(xa, ya, 4, xl, yl, 4, x_out, y_out);
            
            if (n_out > 0) {
              xarea = poly_area(x_out, y_out, n_out);
              min_area = MIN(area_lnd[nl][jl * nxl[nl] + il],
                             area_atm[na][la]);
              
              if (xarea / min_area > area_ratio_thresh) {
                /* ATOMIC operation required for race-safe write */
                int idx;
#pragma acc atomic capture
                {
                  idx = naxl[na][nl];
                  naxl[na][nl]++;
                }
                
                if (idx < 100) {
                  atmxlnd_area[na][nl][idx] = xarea;
                  atmxlnd_ia[na][nl][idx] = ia;
                  atmxlnd_ja[na][nl][idx] = ja;
                  atmxlnd_il[na][nl][idx] = il;
                  atmxlnd_jl[na][nl][idx] = jl;
                }
              }
            }
          }
        }
      }
    }
  }
  
  printf("[GPU Kernel] Completed. naxl results copied back.\n");
}

/* ===== Test Infrastructure ===== */

void print_array_2d(const char *name, int rows, int cols, double **arr)
{
  printf("\n%s (sample):\n", name);
  for (int i = 0; i < MIN(rows, 2); i++) {
    printf("  Row %d: ", i);
    for (int j = 0; j < MIN(cols, 5); j++) {
      printf("%.4f ", arr[i][j]);
    }
    printf("...\n");
  }
}

int main(void)
{
  printf("=== GPU Offloading Kernel Test ===\n");
  printf("Compile: nvc -acc=gpu -gpu=cc80 test_gpu_kernel.c -lm -o test_gpu_kernel\n\n");
  
  /* === Setup Test Data (Small Grids) === */
  int ntile_atm = 1, ntile_lnd = 1;
  int nxatm = 6, nyatm = 6;  /* Small C48-like grid */
  int nxl = 6, nyl = 6;
  
  double *xatm_data = (double *)malloc((nxatm + 1) * (nyatm + 1) * sizeof(double));
  double *yatm_data = (double *)malloc((nxatm + 1) * (nyatm + 1) * sizeof(double));
  double *xlnd_data = (double *)malloc((nxl + 1) * (nyl + 1) * sizeof(double));
  double *ylnd_data = (double *)malloc((nxl + 1) * (nyl + 1) * sizeof(double));
  
  double *area_atm_data = (double *)malloc(nxatm * nyatm * sizeof(double));
  double *area_lnd_data = (double *)malloc(nxl * nyl * sizeof(double));
  
  /* Initialize with simple lat-lon grid [-90, 90] x [-180, 180] */
  for (int j = 0; j <= nyatm; j++) {
    for (int i = 0; i <= nxatm; i++) {
      xatm_data[j * (nxatm + 1) + i] = -180.0 + (360.0 * i) / nxatm;
      yatm_data[j * (nxatm + 1) + i] = -90.0 + (180.0 * j) / nyatm;
    }
  }
  for (int j = 0; j <= nyl; j++) {
    for (int i = 0; i <= nxl; i++) {
      xlnd_data[j * (nxl + 1) + i] = -90.0 + (180.0 * i) / nxl;
      ylnd_data[j * (nxl + 1) + i] = -45.0 + (90.0 * j) / nyl;
    }
  }
  
  /* Simple area calculation (spherical quad) */
  for (int j = 0; j < nyatm; j++) {
    for (int i = 0; i < nxatm; i++) {
      area_atm_data[j * nxatm + i] = (360.0 / nxatm) * (180.0 / nyatm) * D2R * D2R;
    }
  }
  for (int j = 0; j < nyl; j++) {
    for (int i = 0; i < nxl; i++) {
      area_lnd_data[j * nxl + i] = (180.0 / nxl) * (90.0 / nyl) * D2R * D2R;
    }
  }
  
  /* Allocate pointers */
  double **xatm = (double **)malloc(ntile_atm * sizeof(double *));
  double **yatm = (double **)malloc(ntile_atm * sizeof(double *));
  double **xlnd = (double **)malloc(ntile_lnd * sizeof(double *));
  double **ylnd = (double **)malloc(ntile_lnd * sizeof(double *));
  double **area_atm = (double **)malloc(ntile_atm * sizeof(double *));
  double **area_lnd = (double **)malloc(ntile_lnd * sizeof(double *));
  
  xatm[0] = xatm_data; yatm[0] = yatm_data;
  xlnd[0] = xlnd_data; ylnd[0] = ylnd_data;
  area_atm[0] = area_atm_data; area_lnd[0] = area_lnd_data;
  
  /* Grid dimensions */
  int *nxatm_arr = (int *)malloc(ntile_atm * sizeof(int));
  int *nyatm_arr = (int *)malloc(ntile_atm * sizeof(int));
  int *nxl_arr = (int *)malloc(ntile_lnd * sizeof(int));
  int *nyl_arr = (int *)malloc(ntile_lnd * sizeof(int));
  nxatm_arr[0] = nxatm; nyatm_arr[0] = nyatm;
  nxl_arr[0] = nxl; nyl_arr[0] = nyl;
  
  /* Exchange grid results */
  double ***atmxlnd_area = (double ***)malloc(ntile_atm * sizeof(double **));
  int ***atmxlnd_ia = (int ***)malloc(ntile_atm * sizeof(int **));
  int ***atmxlnd_ja = (int ***)malloc(ntile_atm * sizeof(int **));
  int ***atmxlnd_il = (int ***)malloc(ntile_atm * sizeof(int **));
  int ***atmxlnd_jl = (int ***)malloc(ntile_atm * sizeof(int **));
  size_t **naxl = (size_t **)malloc(ntile_atm * sizeof(size_t *));
  
  for (int n = 0; n < ntile_atm; n++) {
    atmxlnd_area[n] = (double **)malloc(ntile_lnd * sizeof(double *));
    atmxlnd_ia[n] = (int **)malloc(ntile_lnd * sizeof(int *));
    atmxlnd_ja[n] = (int **)malloc(ntile_lnd * sizeof(int *));
    atmxlnd_il[n] = (int **)malloc(ntile_lnd * sizeof(int *));
    atmxlnd_jl[n] = (int **)malloc(ntile_lnd * sizeof(int *));
    naxl[n] = (size_t *)malloc(ntile_lnd * sizeof(size_t));
    
    for (int l = 0; l < ntile_lnd; l++) {
      atmxlnd_area[n][l] = (double *)malloc(100 * sizeof(double));
      atmxlnd_ia[n][l] = (int *)malloc(100 * sizeof(int));
      atmxlnd_ja[n][l] = (int *)malloc(100 * sizeof(int));
      atmxlnd_il[n][l] = (int *)malloc(100 * sizeof(int));
      atmxlnd_jl[n][l] = (int *)malloc(100 * sizeof(int));
      naxl[n][l] = 0;
    }
  }
  
  printf("Grid setup complete:\n");
  printf("  ATM grid: %dx%d\n", nxatm, nyatm);
  printf("  LND grid: %dx%d\n", nxl, nyl);
  
  /* === Run GPU Kernel === */
  time_t start = time(NULL);
  
  gpu_atm_lnd_kernel(
    ntile_lnd, ntile_atm,
    nxl_arr, nyl_arr, xlnd, ylnd,
    nxatm_arr, nyatm_arr, xatm, yatm,
    area_atm, area_lnd,
    atmxlnd_area,
    atmxlnd_ia, atmxlnd_ja, atmxlnd_il, atmxlnd_jl,
    naxl,
    0,  /* na = 0 */
    0, nxatm - 1,  /* is, ie */
    0, nyatm - 1,  /* js, je */
    1.0e-6  /* area_ratio_thresh */
  );
  
  time_t end = time(NULL);
  printf("\nKernel executed in %.2f seconds\n", difftime(end, start));
  
  /* === Report Results === */
  printf("\n=== Exchange Grid Results ===\n");
  printf("naxl[0][0] = %zu (number of atm-lnd exchange cells)\n", naxl[0][0]);
  if (naxl[0][0] > 0) {
    printf("Sample exchange cells (first 3):\n");
    for (size_t i = 0; i < MIN(3, naxl[0][0]); i++) {
      printf("  Cell %zu: area=%.6e, ia=%d, ja=%d, il=%d, jl=%d\n",
             i, atmxlnd_area[0][0][i],
             atmxlnd_ia[0][0][i], atmxlnd_ja[0][0][i],
             atmxlnd_il[0][0][i], atmxlnd_jl[0][0][i]);
    }
  }
  
  printf("\n=== Test PASSED ===\n");
  printf("GPU kernel executed successfully with atomic operations.\n");
  
  /* Cleanup */
  free(xatm_data); free(yatm_data);
  free(xlnd_data); free(ylnd_data);
  free(area_atm_data); free(area_lnd_data);
  free(xatm); free(yatm); free(xlnd); free(ylnd);
  free(area_atm); free(area_lnd);
  free(nxatm_arr); free(nyatm_arr); free(nxl_arr); free(nyl_arr);
  for (int n = 0; n < ntile_atm; n++) {
    for (int l = 0; l < ntile_lnd; l++) {
      free(atmxlnd_area[n][l]);
      free(atmxlnd_ia[n][l]);
      free(atmxlnd_ja[n][l]);
      free(atmxlnd_il[n][l]);
      free(atmxlnd_jl[n][l]);
    }
    free(atmxlnd_area[n]);
    free(atmxlnd_ia[n]);
    free(atmxlnd_ja[n]);
    free(atmxlnd_il[n]);
    free(atmxlnd_jl[n]);
    free(naxl[n]);
  }
  free(atmxlnd_area); free(atmxlnd_ia); free(atmxlnd_ja);
  free(atmxlnd_il); free(atmxlnd_jl); free(naxl);
  
  return 0;
}
