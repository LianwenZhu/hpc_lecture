#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <vector>

#include <mkl_cblas.h>
#include <mkl_lapacke.h>
#include <starpu.h>

using namespace std;

void getrf(void *buffers[], void *) {
  double *A = (double *)STARPU_MATRIX_GET_PTR(buffers[0]);
  int N = (int)STARPU_MATRIX_GET_NX(buffers[0]);
  vector<int> ipiv(N);
  LAPACKE_dgetrf(LAPACK_ROW_MAJOR, N, N, A, N, ipiv.data());
}

void trsm(bool left, bool up, vector<double>& A, int NA, vector<double>& B, int NB) {
  cblas_dtrsm(CblasRowMajor, left ? CblasLeft : CblasRight, up ? CblasUpper : CblasLower,
              CblasNoTrans, up ? CblasNonUnit : CblasUnit, NA, NB, 1.0, A.data(), NA, B.data(), NB);
}

void gemm(vector<double>& A, int NA, vector<double>& B, int NB, vector<double>& C) {
  cblas_dgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans, NA, NB, NA, -1.0,
              A.data(), NA, B.data(), NB, 1.0, C.data(), NB);
}

int main() {
  int M = 3;
  int N = 16;
  bool left = true, right = false, upper = true, lower = false;
  vector<vector<double> > A(M*M);
  vector<vector<double> > x(M);
  vector<vector<double> > b(M);
  vector<int> ipiv(N);
  vector<starpu_data_handle_t> A_h(M*M);
  vector<starpu_data_handle_t> b_h(M);

  int ret = starpu_init(NULL); 
  for (int m=0; m<M*M; m++) {
    A[m] = vector<double>(N*N);
    starpu_matrix_data_register(&A_h[m],0,(uintptr_t)A[m].data(),N,N,N,sizeof(double));
  }
  for (int m=0; m<M; m++) {
    x[m] = vector<double>(N);
    b[m] = vector<double>(N);
    starpu_vector_data_register(&b_h[m],0,(uintptr_t)b[m].data(),N,sizeof(double));
  }
  for (int m=0; m<M; m++) {
    for (int i=0; i<N; i++) {
      x[m][i] = drand48();
      b[m][i] = 0;
    }
  }
  for (int m=0; m<M; m++) {
    for (int n=0; n<M; n++) {
      for (int i=0; i<N; i++) {
        for (int j=0; j<N; j++) {
          A[M*m+n][N*i+j] = drand48() + (m == n) * (i == j) * 10;
          b[m][i] += A[M*m+n][N*i+j] * x[n][j];
	}
      }
    }
  }
  starpu_codelet getrf_cl;
  starpu_codelet_init(&getrf_cl);
  getrf_cl.cpu_funcs[0] = getrf;
  getrf_cl.nbuffers = 1;
  getrf_cl.modes[0] = STARPU_RW;
  for (int l=0; l<M; l++) {
    starpu_task_insert(&getrf_cl, STARPU_RW, A_h[M*l+l], 0);
    starpu_task_wait_for_all();
    for (int m=l+1; m<M; m++) {
      trsm(left, lower, A[M*l+l], N, A[M*l+m], N);
      trsm(right, upper, A[M*l+l], N, A[M*m+l], N);
    }
    for (int m=l+1; m<M; m++)
      for (int n=l+1; n<M; n++)
        gemm(A[M*m+l], N, A[M*l+n], N, A[M*m+n]);
  }
  for (int m=0; m<M; m++) {
    for (int n=0; n<m; n++)
      gemm(A[M*m+n], N, b[n], 1, b[m]);
    trsm(left, lower, A[M*m+m], N, b[m], 1);
  }
  for (int m=M-1; m>=0; m--) {
    for (int n=M-1; n>m; n--)
      gemm(A[M*m+n], N, b[n], 1, b[m]);
    trsm(left, upper, A[M*m+m], N, b[m], 1);
  }
  starpu_task_wait_for_all();
  for (int m=0; m<M*M; m++)
    starpu_data_unregister(A_h[m]);
  for (int m=0; m<M; m++)
    starpu_data_unregister(b_h[m]);

  double diff = 0, norm = 0;
  for (int m=0; m<M; m++) {
    for (int i=0; i<N; i++) {
      diff += (x[m][i] - b[m][i]) * (x[m][i] - b[m][i]);
      norm += x[m][i] * x[m][i];
    }
  }
  printf("Error: %g\n",std::sqrt(diff/norm));
  starpu_shutdown();
}
