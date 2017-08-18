#include <algorithm>
#include <cstdlib>
#include <interface/blas1_interface_sycl.hpp>
#include <interface/blas2_interface_sycl.hpp>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace cl::sycl;
using namespace blas;

#define DEF_SIZE_VECT 1200
#define ERROR_ALLOWED 1.0E-8
#define RANDOM_DATA 1
#define SHOW_VALUES   1

#define EXECUTED_ON_GPU 1
// #define EXECUTING_FLOAT

#define SHOW_TIMES 1  // If it exists, the code prints the execution time
                      // The ... should be changed by the corresponding routine
#define NUMBER_REPEATS 6  // Number of times the computations are made
// If it is greater than 1, the compile time is not considered


#ifdef EXECUTED_ON_GPU
  #define DEFAULT_ACCESS false
#else
  #define DEFAULT_ACCESS true
#endif

#define ROW_TEST "Tr"
#define COL_TEST "No"

#ifdef EXECUTING_FLOAT
  #define BASETYPE float
  #define CONS_ROW1 1.5F
  #define CONS_ROW2 2.0F
  #define CONS_COL1 0.5F
  #define CONS_COL2 2.5F
  #define CONS_GER  3.0F
#else
  #define BASETYPE double
  #define CONS_ROW1 1.5
  #define CONS_ROW2 2.0
  #define CONS_COL1 0.5
  #define CONS_COL2 2.5
  #define CONS_GER  3.0
#endif

// INITIAL MATRIZ VECTOR PRODUCT

template <typename ExecutorType, typename T, typename ContainerT>
void _gemv0(Executor<ExecutorType> ex, std::string _Trans, size_t _M, size_t _N,
            T _alpha, matrix_view<T, ContainerT> _mA, size_t _lda,
            vector_view<T, ContainerT> _vx, size_t _incx, T _beta,
            vector_view<T, ContainerT> _vy, size_t _incy) {
  if ((_Trans[0] != 'n') && (_Trans[0] != 'N') && (_Trans[0] != 't') &&
      (_Trans[0] != 'T') && (_Trans[0] != 'c') && (_Trans[0] != 'C'))
    std::cout << "Erroneous parameter" << std::endl;
  bool accessOpr = ((_Trans[0] == 'n') || (_Trans[0] == 'N'));
  size_t M = (accessOpr ? _M : _N);
  size_t N = (accessOpr ? _N : _M);
  auto my_mA =
      matrix_view<T, ContainerT>(_mA, _M, _N, accessOpr, _lda, _mA.getDisp());
  auto my_vx = vector_view<T, ContainerT>(_vx, _vx.getDisp(), _incx, N);
  auto my_vy = vector_view<T, ContainerT>(_vy, _vy.getDisp(), _incy, M);
#ifdef VERBOSE
  std::cout << "alpha = " << _alpha << " , beta = " << _beta << std::endl;
  my_mA.printH("MA");
  my_vx.printH("VX");
  my_vy.printH("VY");
#endif  // VERBOSE
  if (my_mA.getAccess()) {
    auto scalOp = make_op<ScalarOp, prdOp2_struct>(_beta, my_vy);
    auto assignOp = make_op<Assign>(my_vy, scalOp);
    ex.execute(assignOp);
    auto disp = my_mA.getDisp();
    for (size_t i = 0; i < M; i++) {
      auto my_row = vector_view<T, ContainerT>(my_mA.getData(), disp, 1, N);
      auto my_rs = vector_view<T, ContainerT>(my_vy, i + my_vy.getDisp(), 1, 1);
      auto scl = my_rs.eval(0);
      auto prdOp1 = make_op<BinaryOp, prdOp2_struct>(my_row, my_vx);
      auto localSize = 256;
      auto nWG = 128;
#ifdef SYCL_CODE
      auto assignOp1 =
          make_addAssignReduction(my_rs, prdOp1, localSize, localSize * nWG);
      ex.reduce(assignOp1);
#else   // SYCL_CODE
      ContainerT valT1(nWG);
      auto val1 = vector_view<T, ContainerT>(valT1, 0, 1, nWG);
      auto assignOp11 =
          make_addAssignReduction(val1, prdOp1, localSize, localSize * nWG);
      ex.execute(assignOp11);
      auto assignOp1 = make_addAssignReduction(my_rs, val1, localSize, nWG);
      ex.execute(assignOp1);
#endif  // SYCL_CODE
      auto prdOp2 = make_op<ScalarOp, prdOp2_struct>(_alpha, my_rs);
      auto addOp2 = make_op<ScalarOp, addOp2_struct>(scl, prdOp2);
      auto assignOp2 = make_op<Assign>(my_rs, addOp2);
      ex.execute(assignOp2);
      disp += _lda;
    }
  } else {
    auto scalOp1 = make_op<ScalarOp, prdOp2_struct>(_beta, my_vy);
    auto assignOp1 = make_op<Assign>(my_vy, scalOp1);
    ex.execute(assignOp1);
    auto disp = my_mA.getDisp();
    for (size_t j = 0; j < N; j++) {
      auto my_col = vector_view<T, ContainerT>(my_mA.getData(), disp, 1, M);
      auto scalOp2 =
          make_op<ScalarOp, prdOp2_struct>(_alpha * my_vx.eval(j), my_col);
      auto addOp2 = make_op<BinaryOp, addOp2_struct>(my_vy, scalOp2);
      auto assignOp2 = make_op<Assign>(my_vy, addOp2);
      ex.execute(assignOp2);
      disp += _lda;
    }
  }
#ifdef VERBOSE
  my_vy.printH("VY");
#endif  // VERBOSE
}

// INITIAL RANK 1 MODIFICATION

template <typename ExecutorType, typename T, typename ContainerT>
void _ger0(Executor<ExecutorType> ex, size_t _M, size_t _N, T _alpha,
           vector_view<T, ContainerT> _vx, size_t _incx,
           vector_view<T, ContainerT> _vy, size_t _incy,
           matrix_view<T, ContainerT> _mA, size_t _lda) {
  bool accessOpr = true;
  size_t M = _M;
  size_t N = _N;
  auto my_mA =
      matrix_view<T, ContainerT>(_mA, _M, _N, accessOpr, _lda, _mA.getDisp());
  auto my_vx = vector_view<T, ContainerT>(_vx, _vx.getDisp(), _incx, M);
  auto my_vy = vector_view<T, ContainerT>(_vy, _vy.getDisp(), _incy, N);
#ifdef VERBOSE
  std::cout << "alpha = " << _alpha << std::endl;
  my_mA.printH("MA");
  my_vx.printH("VX");
  my_vy.printH("VY");
#endif  // VERBOSE
  if (my_mA.getAccess()) {
    auto disp = my_mA.getDisp();
    for (size_t i = 0; i < M; i++) {
      auto my_row = vector_view<T, ContainerT>(my_mA.getData(), disp, 1, N);
      auto scalOp2 =
          make_op<ScalarOp, prdOp2_struct>(_alpha * my_vx.eval(i), my_vy);
      auto addOp2 = make_op<BinaryOp, addOp2_struct>(my_row, scalOp2);
      auto assignOp2 = make_assign(my_row, addOp2);
      ex.execute(assignOp2);
      disp += _lda;
    }
  } else {
    auto disp = my_mA.getDisp();
    for (size_t j = 0; j < N; j++) {
      auto my_col = vector_view<T, ContainerT>(my_mA.getData(), disp, 1, M);
      auto scalOp2 =
          make_op<ScalarOp, prdOp2_struct>(_alpha * my_vy.eval(j), my_vx);
      auto addOp2 = make_op<BinaryOp, addOp2_struct>(my_col, scalOp2);
      auto assignOp2 = make_assign(my_col, addOp2);
      ex.execute(assignOp2);
      disp += _lda;
    }
  }
#ifdef VERBOSE
  my_vy.printH("VY");
#endif  // VERBOSE
}

// TESTING ROUTINE

size_t TestingBLAS2(bool accessDev, size_t dim, size_t divSz, size_t shftR,
                    size_t shftC) {
  // CREATING DATA
  size_t dimR = dim / divSz;
  size_t dimC = dim * divSz;
  size_t dimL = ((accessDev) ? dimC : dimR);
  std::vector<BASETYPE> vM0(dimR * dimC);
  std::vector<BASETYPE> vM1(dimR * dimC);
  std::vector<BASETYPE> vX0(dimC);
  std::vector<BASETYPE> vY0(dimR);
  std::vector<BASETYPE> vX1(dimC);
  std::vector<BASETYPE> vY1(dimR);
  std::vector<BASETYPE> vX2(dimC);
  std::vector<BASETYPE> vY2(dimR);
  std::vector<BASETYPE> vR(9);
  std::vector<BASETYPE> vS(13);
  std::vector<BASETYPE> vT(3);
#ifdef SHOW_TIMES
  std::chrono::time_point<std::chrono::steady_clock> t_start, t_stop;
  std::chrono::duration<BASETYPE> t1_gmvR, t1_gmvC, t1_ger;
  std::chrono::duration<BASETYPE> t2_gmvR, t2_gmvC, t2_ger;
  std::chrono::duration<BASETYPE> t3_gmvR, t3_gmvC, t3_ger;
  std::chrono::duration<BASETYPE> t4_gmvR, t4_gmvC, t4_ger;
  std::chrono::duration<BASETYPE> t5_gmvR, t5_gmvC, t5_ger;
  std::chrono::duration<BASETYPE> t6_gmvR, t6_gmvC, t6_ger;
  std::chrono::duration<BASETYPE> t7_gmvR, t7_gmvC, t7_ger;
  std::chrono::duration<BASETYPE> t8_gmvR, t8_gmvC, t8_ger;
  std::chrono::duration<BASETYPE> t9_gmvR, t9_gmvC, t9_ger;
  std::chrono::duration<BASETYPE> t10_gmvR, t10_gmvC, t10_ger;
  std::chrono::duration<BASETYPE> t11_gmvR, t11_gmvC, t11_ger;
  std::chrono::duration<BASETYPE> t12_gmvR, t12_gmvC, t12_ger;
  std::chrono::duration<BASETYPE> t13_gmvR, t13_gmvC, t13_ger;
  std::vector<std::chrono::duration<BASETYPE>> v1_gmvR(NUMBER_REPEATS), v1_gmvC(NUMBER_REPEATS), v1_ger(NUMBER_REPEATS);
  std::vector<std::chrono::duration<BASETYPE>> v2_gmvR(NUMBER_REPEATS), v2_gmvC(NUMBER_REPEATS), v2_ger(NUMBER_REPEATS);
  std::vector<std::chrono::duration<BASETYPE>> v3_gmvR(NUMBER_REPEATS), v3_gmvC(NUMBER_REPEATS), v3_ger(NUMBER_REPEATS);
  std::vector<std::chrono::duration<BASETYPE>> v4_gmvR(NUMBER_REPEATS), v4_gmvC(NUMBER_REPEATS), v4_ger(NUMBER_REPEATS);
  std::vector<std::chrono::duration<BASETYPE>> v5_gmvR(NUMBER_REPEATS), v5_gmvC(NUMBER_REPEATS), v5_ger(NUMBER_REPEATS);
  std::vector<std::chrono::duration<BASETYPE>> v6_gmvR(NUMBER_REPEATS), v6_gmvC(NUMBER_REPEATS), v6_ger(NUMBER_REPEATS);
  std::vector<std::chrono::duration<BASETYPE>> v7_gmvR(NUMBER_REPEATS), v7_gmvC(NUMBER_REPEATS), v7_ger(NUMBER_REPEATS);
  std::vector<std::chrono::duration<BASETYPE>> v8_gmvR(NUMBER_REPEATS), v8_gmvC(NUMBER_REPEATS), v8_ger(NUMBER_REPEATS);
  std::vector<std::chrono::duration<BASETYPE>> v9_gmvR(NUMBER_REPEATS), v9_gmvC(NUMBER_REPEATS), v9_ger(NUMBER_REPEATS);
  std::vector<std::chrono::duration<BASETYPE>> v10_gmvR(NUMBER_REPEATS), v10_gmvC(NUMBER_REPEATS), v10_ger(NUMBER_REPEATS);
  std::vector<std::chrono::duration<BASETYPE>> v11_gmvR(NUMBER_REPEATS), v11_gmvC(NUMBER_REPEATS), v11_ger(NUMBER_REPEATS);
  std::vector<std::chrono::duration<BASETYPE>> v12_gmvR(NUMBER_REPEATS), v12_gmvC(NUMBER_REPEATS), v12_ger(NUMBER_REPEATS);
  std::vector<std::chrono::duration<BASETYPE>> v13_gmvR(NUMBER_REPEATS), v13_gmvC(NUMBER_REPEATS), v13_ger(NUMBER_REPEATS);
#endif

  // INITIALIZING DATA
  size_t vSeed, gap;
  BASETYPE minV, maxV;
#ifdef RANDOM_DATA
  vSeed = 1;
  minV = -10.0;
  maxV = 10.0;
  gap = (size_t)(maxV - minV + 1);
  srand(vSeed);
#else   // RANDOM_DATA
  minV = 1.00;
  maxV = 1.00;
#endif  // RANDOM_DATA
  std::for_each(std::begin(vM1), std::end(vM1), [&](BASETYPE &elem) {
#ifdef RANDOM_DATA
    elem = minV + (BASETYPE)(rand() % gap);
#else   // RANDOM_DATA
    elem = minV; minV += maxV;
#endif  // RANDOM_DATA

  });

#ifdef RANDOM_DATA
  vSeed = 1;
  minV = -10.0;
  maxV = 10.0;
  gap = (size_t)(maxV - minV + 1);
  srand(vSeed);
#else   // RANDOM_DATA
  minV = 1.00;
  maxV = 1.00;
#endif  // RANDOM_DATA
  std::for_each(std::begin(vX0), std::end(vX0), [&](BASETYPE &elem) {
#ifdef RANDOM_DATA
    elem = minV + (BASETYPE)(rand() % gap);
#else   // RANDOM_DATA
    elem = minV; minV += maxV;
#endif  // RANDOM_DATA
  });

#ifdef RANDOM_DATA
  vSeed = 2;
  minV = -10.0;
  maxV = 10.0;
  gap = (size_t)(maxV - minV + 1);
  srand(vSeed);
#else   // RANDOM_DATA
  minV = 1.00;
  maxV = 1.00;
#endif  // RANDOM_DATA
  std::for_each(std::begin(vX1), std::end(vX1), [&](BASETYPE &elem) {
#ifdef RANDOM_DATA
    elem = minV + (BASETYPE)(rand() % gap);
#else   // RANDOM_DATA
    elem = minV; minV += maxV;
#endif  // RANDOM_DATA
  });

#ifdef RANDOM_DATA
  vSeed = 3;
  minV = -10.0;
  maxV = 10.0;
  gap = (size_t)(maxV - minV + 1);
  srand(vSeed);
#else   // RANDOM_DATA
  minV = 1.00;
  maxV = 1.00;
#endif  // RANDOM_DATA
  std::for_each(std::begin(vY0), std::end(vY0), [&](BASETYPE &elem) {
#ifdef RANDOM_DATA
    elem = minV + (BASETYPE)(rand() % gap);
#else   // RANDOM_DATA
    elem = minV; minV += maxV;
#endif  // RANDOM_DATA
  });

#ifdef RANDOM_DATA
  vSeed = 4;
  minV = -10.0;
  maxV = 10.0;
  gap = (size_t)(maxV - minV + 1);
  srand(vSeed);
#else   // RANDOM_DATA
  minV = 1.00;
  maxV = 1.00;
#endif  // RANDOM_DATA
  std::for_each(std::begin(vY1), std::end(vY1), [&](BASETYPE &elem) {
#ifdef RANDOM_DATA
    elem = minV + (BASETYPE)(rand() % gap);
#else   // RANDOM_DATA
    elem = minV; minV += maxV;
#endif  // RANDOM_DATA
  });

  for (int i=0; i<3; i++) {
    vR[i] = 0.0;
    vS[i] = 0.0;
    vT[i] = 0.0;
  }
/*
  // CREATING HOST STRUCTURES
  matrix_view<BASETYPE, std::vector<BASETYPE>> v_M0(vM0, accessDev, dimR, dimC, true,
                                                dimL, 0);
  matrix_view<BASETYPE, std::vector<BASETYPE>> v_M1(vM1, accessDev, dimR, dimC, true,
                                                dimL, 0);
  vector_view<BASETYPE, std::vector<BASETYPE>> v_X0(vX0, 0, 1, dimC);
  vector_view<BASETYPE, std::vector<BASETYPE>> v_Y0(vY0, 0, 1, dimR);
  vector_view<BASETYPE, std::vector<BASETYPE>> v_R(vR, 0, 1, 1);
*/
  // COMPUTING THE RESULTS
  size_t returnVal = 0;
  BASETYPE res;

  BASETYPE addY = 0.0, auxY;
  for (size_t i = shftR; i < dimR; i++) {
//    vY2[i - shftR] = 1.5 * vY[i - shftR];
    auxY = CONS_ROW1 * vY1[i - shftR];
//    printf ("(AB) %f = 1.5 * %f\n", auxY, vY2[i - shftC]);
    for (size_t j = shftC; j < dimC; j++) {
      if (accessDev) {
//        vY2[i - shftR] += 2.0 * vM[dimC * i + j] * vY[j - shftC];
//        printf ("(A) %f += 2.0 * %f * %f\n", auxY, vM[dimC * i + j], vY[j - shftC]);
        auxY += CONS_ROW2 * vM1[dimC * i + j] * vY0[j - shftC];
      } else {
//        vY2[i - shftR] += 2.0 * vM[dimR * j + i] * vY[j - shftC];
//        printf ("(B) %f += 2.0 * %f * %f\n", auxY, vM[dimR * j + i], vY[j - shftC]);
        auxY += CONS_ROW2 * vM1[dimR * j + i] * vY0[j - shftC];
      }
    }
//    addY += vY2[i - shftR];
    addY += auxY;
//    printf("VY2(%lu) = %f\n", i, auxY);
  }
  for (size_t i = dimR - shftR; i < dimR; i++) {
#ifdef VERBOSE
    std::cout << "+" << vY[i] << std::endl;
#endif  // VERBOSE
    addY += vY1[i];
  }

  BASETYPE addX = 0.0, auxX;
  for (size_t j = shftC; j < dimC; j++) {
//    vX2[j - shftC] = 0.5 * vX2[j - shftC];
    auxX = CONS_COL1 * vX1[j - shftC];
    for (size_t i = shftR; i < dimR; i++) {
      if (accessDev) {
//        vX2[j - shftC] += 2.5 * vM[dimC * i + j] * vX[i - shftR];
        auxX += CONS_COL2 * vM1[dimC * i + j] * vX0[i - shftR];
      } else {
//        vX2[j - shftC] += 2.5 * vM[dimR * j + i] * vX[i - shftR];
        auxX += CONS_COL2 * vM1[dimR * j + i] * vX0[i - shftR];
      }
    }
//    addX += vX2[j - shftC];
//    printf("VX2(%lu) = %f\n", j, auxX);
    addX += auxX;
  }
  for (size_t j = dimC - shftC; j < dimC; j++) {
    addX += vX1[j];
  }

  BASETYPE addRng1 = 0.0;
  for (size_t i = 0; i < dimR; i++) {
    for (size_t j = 0; j < dimC; j++) {
      BASETYPE aux = 0.0;
//      addRng1 += (accessDev) ? vM1[dimC * i + j] : vM1[dimR * j + i];
      aux += (accessDev) ? vM1[dimC * i + j] : vM1[dimR * j + i];
      if ((i >= shftR) && (j >= shftC)) {
//        addRng1 += (3.0 * vY0[i - shftR] * vX0[j - shftC]);
        aux += (CONS_GER * vY0[i - shftR] * vX0[j - shftC]);
      }
      addRng1 += aux;
    }
  }

  // CREATING THE SYCL QUEUE AND EXECUTOR
  // cl::sycl::cpu_selector s; NOOOOO
#ifdef EXECUTED_ON_GPU
  cl::sycl::gpu_selector   s;
#else
  cl::sycl::intel_selector s;
#endif
  cl::sycl::queue q([=](cl::sycl::exception_list eL) {
    try {
      for (auto &e : eL) {
        std::rethrow_exception(e);
      }
    } catch (cl::sycl::exception &e) {
      std::cout << " E " << e.what() << std::endl;
    } catch (...) {
      std::cout << " An exception " << std::endl;
    }
  });
  Executor<SYCL> ex(q);

  {
    // CREATION OF THE BUFFERS
    buffer<BASETYPE, 1> bM0(vM0.data(), range<1>{vM0.size()});
    buffer<BASETYPE, 1> bM1(vM1.data(), range<1>{vM1.size()});
    buffer<BASETYPE, 1> bX0(vX0.data(), range<1>{vX0.size()});
    buffer<BASETYPE, 1> bY0(vY0.data(), range<1>{vY0.size()});
    buffer<BASETYPE, 1> bX1(vX1.data(), range<1>{vX1.size()});
    buffer<BASETYPE, 1> bY1(vY1.data(), range<1>{vY1.size()});
    buffer<BASETYPE, 1> bX2(vX2.data(), range<1>{vX2.size()});
    buffer<BASETYPE, 1> bY2(vY2.data(), range<1>{vY2.size()});
    buffer<BASETYPE, 1> bR(vR.data(), range<1>{vR.size()});
    buffer<BASETYPE, 1> bS(vS.data(), range<1>{vS.size()});
    buffer<BASETYPE, 1> bT(vT.data(), range<1>{vT.size()});

    // BUILDING A SYCL VIEW OF THE BUFFERS
    BufferMatrixView<BASETYPE> bmM0(bM0, accessDev, dimR, dimC);
    BufferMatrixView<BASETYPE> bmM1(bM1, accessDev, dimR, dimC);
    BufferVectorView<BASETYPE> bvV0(bM0);
    BufferVectorView<BASETYPE> bvX0(bX0);
    BufferVectorView<BASETYPE> bvY0(bY0);
    BufferVectorView<BASETYPE> bvX1(bX1);
    BufferVectorView<BASETYPE> bvY1(bY1);
    BufferVectorView<BASETYPE> bvX2(bX2);
    BufferVectorView<BASETYPE> bvY2(bY2);
    BufferVectorView<BASETYPE> bvR1(bR,0);
    BufferVectorView<BASETYPE> bvR2(bR,1);
    BufferVectorView<BASETYPE> bvR3(bR,2);
    BufferVectorView<BASETYPE> bvR4(bR,3);
    BufferVectorView<BASETYPE> bvR5(bR,4);
    BufferVectorView<BASETYPE> bvR6(bR,5);
    BufferVectorView<BASETYPE> bvS1(bS,0);
    BufferVectorView<BASETYPE> bvS2(bS,1);
    BufferVectorView<BASETYPE> bvS3(bS,2);
    BufferVectorView<BASETYPE> bvS4(bS,3);
    BufferVectorView<BASETYPE> bvS5(bS,4);
    BufferVectorView<BASETYPE> bvS6(bS,5);
    BufferVectorView<BASETYPE> bvS7(bS,6);
    BufferVectorView<BASETYPE> bvS8(bS,7);
    BufferVectorView<BASETYPE> bvS9(bS,8);
    BufferVectorView<BASETYPE> bvS10(bS,9);
    BufferVectorView<BASETYPE> bvS11(bS,10);
    BufferVectorView<BASETYPE> bvS12(bS,11);
    BufferVectorView<BASETYPE> bvS13(bS,12);
    BufferVectorView<BASETYPE> bvT1(bT,0);
    BufferVectorView<BASETYPE> bvT2(bT,1);
    BufferVectorView<BASETYPE> bvT3(bT,2);

    // EXECUTION OF THE ROUTINES
    for (int i = 0; i < NUMBER_REPEATS; i++) {
      /*****************************************/
      auto assign_M0 = make_op<Assign>(bmM0, bmM1);
      ex.execute(assign_M0); q.wait_and_throw();

      /*****************************************/
/* */
      auto assign_X2_1 = make_op<Assign>(bvX2, bvX1);
      ex.execute(assign_X2_1); q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_start = std::chrono::steady_clock::now();
  #endif
      _gemv<1, SYCL>(ex, ROW_TEST, dimR - shftC, dimR - shftR, CONS_COL2, bmM0(shftR, shftC),
                  dimL, bvX0, 1, CONS_COL1, bvX2, 1);
      q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_stop = std::chrono::steady_clock::now();
      if (NUMBER_REPEATS == 1) {
        t1_gmvR = t_stop - t_start;
      } else if (i > 0) {
        t1_gmvR += t_stop - t_start;
      } else {
        t1_gmvR = t_start - t_start;
      }
      v1_gmvR[i] = t_stop - t_start;
  #endif
      auto reducOpX_1 = make_addAssignReduction(bvR1, bvX2, 256, 256);
      ex.reduce(reducOpX_1); q.wait_and_throw();
/* */
      /*****************************************/
/* */
      auto assign_X2_11 = make_op<Assign>(bvX2, bvX1);
      ex.execute(assign_X2_11); q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_start = std::chrono::steady_clock::now();
  #endif
      _gemv<11, SYCL>(ex, ROW_TEST, dimR - shftC, dimR - shftR, CONS_COL2, bmM0(shftR, shftC),
                  dimL, bvX0, 1, CONS_COL1, bvX2, 1);
      q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_stop = std::chrono::steady_clock::now();
      if (NUMBER_REPEATS == 1) {
        t2_gmvR = t_stop - t_start;
      } else if (i > 0) {
        t2_gmvR += t_stop - t_start;
      } else {
        t2_gmvR = t_start - t_start;
      }
      v2_gmvR[i] = t_stop - t_start;
  #endif
      auto reducOpX_11 = make_addAssignReduction(bvR2, bvX2, 256, 256);
      ex.reduce(reducOpX_11); q.wait_and_throw();
/* */
      /*****************************************/
/* */
      auto assign_X2_12 = make_op<Assign>(bvX2, bvX1);
      ex.execute(assign_X2_12); q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_start = std::chrono::steady_clock::now();
  #endif
      _gemv<12, SYCL>(ex, ROW_TEST, dimR - shftC, dimR - shftR, CONS_COL2, bmM0(shftR, shftC),
                  dimL, bvX0, 1, CONS_COL1, bvX2, 1);
      q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_stop = std::chrono::steady_clock::now();
      if (NUMBER_REPEATS == 1) {
        t3_gmvR = t_stop - t_start;
      } else if (i > 0) {
        t3_gmvR += t_stop - t_start;
      } else {
        t3_gmvR = t_start - t_start;
      }
      v3_gmvR[i] = t_stop - t_start;
  #endif
      auto reducOpX_12 = make_addAssignReduction(bvR3, bvX2, 256, 256);
      ex.reduce(reducOpX_12); q.wait_and_throw();
/* */
      /*****************************************/
/* */
      auto assign_X2_13 = make_op<Assign>(bvX2, bvX1);
      ex.execute(assign_X2_13); q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_start = std::chrono::steady_clock::now();
  #endif
      _gemv<13, SYCL>(ex, ROW_TEST, dimR - shftC, dimR - shftR, CONS_COL2, bmM0(shftR, shftC),
                  dimL, bvX0, 1, CONS_COL1, bvX2, 1);
      q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_stop = std::chrono::steady_clock::now();
      if (NUMBER_REPEATS == 1) {
        t4_gmvR = t_stop - t_start;
      } else if (i > 0) {
        t4_gmvR += t_stop - t_start;
      } else {
        t4_gmvR = t_start - t_start;
      }
      v4_gmvR[i] = t_stop - t_start;
  #endif
      auto reducOpX_13 = make_addAssignReduction(bvR4, bvX2, 256, 256);
      ex.reduce(reducOpX_13); q.wait_and_throw();
/* */
      /*****************************************/
/* */
      auto assign_X2_14 = make_op<Assign>(bvX2, bvX1);
      ex.execute(assign_X2_14); q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_start = std::chrono::steady_clock::now();
  #endif
      _gemv<14, SYCL>(ex, ROW_TEST, dimR - shftC, dimR - shftR, CONS_COL2, bmM0(shftR, shftC),
                  dimL, bvX0, 1, CONS_COL1, bvX2, 1);
      q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_stop = std::chrono::steady_clock::now();
      if (NUMBER_REPEATS == 1) {
        t5_gmvR = t_stop - t_start;
      } else if (i > 0) {
        t5_gmvR += t_stop - t_start;
      } else {
        t5_gmvR = t_start - t_start;
      }
      v5_gmvR[i] = t_stop - t_start;
  #endif
      auto reducOpX_14 = make_addAssignReduction(bvR5, bvX2, 256, 256);
      ex.reduce(reducOpX_14); q.wait_and_throw();
/* */
      /*****************************************/
/* */
      auto assign_X2_20 = make_op<Assign>(bvX2, bvX1);
      ex.execute(assign_X2_20); q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_start = std::chrono::steady_clock::now();
  #endif
      _gemv<20, SYCL>(ex, ROW_TEST, dimR - shftC, dimR - shftR, CONS_COL2, bmM0(shftR, shftC),
                  dimL, bvX0, 1, CONS_COL1, bvX2, 1);
      q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_stop = std::chrono::steady_clock::now();
      if (NUMBER_REPEATS == 1) {
        t6_gmvR = t_stop - t_start;
      } else if (i > 0) {
        t6_gmvR += t_stop - t_start;
      } else {
        t6_gmvR = t_start - t_start;
      }
      v6_gmvR[i] = t_stop - t_start;
  #endif
      auto reducOpX_20 = make_addAssignReduction(bvR6, bvX2, 256, 256);
      ex.reduce(reducOpX_20); q.wait_and_throw();
/* */
      /*****************************************/
//      auto assign1_Seg = make_op<Assign>(bvX2, bvY2);
//      ex.execute(assign1_Seg); q.wait_and_throw();
/* */
      auto assign_Y2_1 = make_op<Assign>(bvY2, bvY1);
      ex.execute(assign_Y2_1); q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_start = std::chrono::steady_clock::now();
  #endif
      _gemv<1, SYCL>(ex, COL_TEST, dimR - shftR, dimC - shftC, CONS_ROW2, bmM0(shftR, shftC),
                  dimL, bvY0, 1, CONS_ROW1, bvY2, 1);
      q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_stop = std::chrono::steady_clock::now();
      if (NUMBER_REPEATS == 1) {
        t1_gmvC = t_stop - t_start;
      } else if (i > 0) {
        t1_gmvC += t_stop - t_start;
      } else {
        t1_gmvC = t_start - t_start;
      }
      v1_gmvC[i] = t_stop - t_start;
  #endif
      auto reducOpY_1 = make_addAssignReduction(bvS1, bvY2, 256, 256);
      ex.reduce(reducOpY_1); q.wait_and_throw();
/* */
//      auto assign1_Rec = make_op<Assign>(bvY2, bvX2);
//      ex.execute(assign1_Rec); q.wait_and_throw();
      /*****************************************/
/* */
      auto assign_Y2_2 = make_op<Assign>(bvY2, bvY1);
      ex.execute(assign_Y2_2); q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_start = std::chrono::steady_clock::now();
  #endif
      _gemv<2, SYCL>(ex, COL_TEST, dimR - shftR, dimC - shftC, CONS_ROW2, bmM0(shftR, shftC),
                  dimL, bvY0, 1, CONS_ROW1, bvY2, 1);
      q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_stop = std::chrono::steady_clock::now();
      if (NUMBER_REPEATS == 1) {
        t2_gmvC = t_stop - t_start;
      } else if (i > 0) {
        t2_gmvC += t_stop - t_start;
      } else {
        t2_gmvC = t_start - t_start;
      }
      v2_gmvC[i] = t_stop - t_start;
  #endif
      auto reducOpY_2 = make_addAssignReduction(bvS2, bvY2, 256, 256);
      ex.reduce(reducOpY_2); q.wait_and_throw();
/* */
//      auto assign2_Rec = make_op<Assign>(bvY2, bvX2);
//      ex.execute(assign2_Rec); q.wait_and_throw();
      /*****************************************/
/* */
      auto assign_Y2_3 = make_op<Assign>(bvY2, bvY1);
      ex.execute(assign_Y2_3); q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_start = std::chrono::steady_clock::now();
  #endif
      _gemv<3, SYCL>(ex, COL_TEST, dimR - shftR, dimC - shftC, CONS_ROW2, bmM0(shftR, shftC),
                  dimL, bvY0, 1, CONS_ROW1, bvY2, 1);
      q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_stop = std::chrono::steady_clock::now();
      if (NUMBER_REPEATS == 1) {
        t3_gmvC = t_stop - t_start;
      } else if (i > 0) {
        t3_gmvC += t_stop - t_start;
      } else {
        t3_gmvC = t_start - t_start;
      }
      v3_gmvC[i] = t_stop - t_start;
  #endif
      auto reducOpY_3 = make_addAssignReduction(bvS3, bvY2, 256, 256);
      ex.reduce(reducOpY_3); q.wait_and_throw();
/* */
      /*****************************************/
/* */
      auto assign_Y2_11 = make_op<Assign>(bvY2, bvY1);
      ex.execute(assign_Y2_11); q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_start = std::chrono::steady_clock::now();
  #endif
      _gemv<11, SYCL>(ex, COL_TEST, dimR - shftR, dimC - shftC, CONS_ROW2, bmM0(shftR, shftC),
                  dimL, bvY0, 1, CONS_ROW1, bvY2, 1);
      q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_stop = std::chrono::steady_clock::now();
      if (NUMBER_REPEATS == 1) {
        t4_gmvC = t_stop - t_start;
      } else if (i > 0) {
        t4_gmvC += t_stop - t_start;
      } else {
        t4_gmvC = t_start - t_start;
      }
      v4_gmvC[i] = t_stop - t_start;
  #endif
      auto reducOpY_11= make_addAssignReduction(bvS4, bvY2, 256, 256);
      ex.reduce(reducOpY_11); q.wait_and_throw();
/* */
      /*****************************************/
/* */
      auto assign_Y2_12 = make_op<Assign>(bvY2, bvY1);
      ex.execute(assign_Y2_12); q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_start = std::chrono::steady_clock::now();
  #endif
      _gemv<12, SYCL>(ex, COL_TEST, dimR - shftR, dimC - shftC, CONS_ROW2, bmM0(shftR, shftC),
                  dimL, bvY0, 1, CONS_ROW1, bvY2, 1);
      q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_stop = std::chrono::steady_clock::now();
      if (NUMBER_REPEATS == 1) {
        t5_gmvC = t_stop - t_start;
      } else if (i > 0) {
        t5_gmvC += t_stop - t_start;
      } else {
        t5_gmvC = t_start - t_start;
      }
      v5_gmvC[i] = t_stop - t_start;
  #endif
      auto reducOpY_12 = make_addAssignReduction(bvS5, bvY2, 256, 256);
      ex.reduce(reducOpY_12); q.wait_and_throw();
/* */
      /*****************************************/
/* */
      auto assign_Y2_13 = make_op<Assign>(bvY2, bvY1);
      ex.execute(assign_Y2_13); q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_start = std::chrono::steady_clock::now();
  #endif
      _gemv<13, SYCL>(ex, COL_TEST, dimR - shftR, dimC - shftC, CONS_ROW2, bmM0(shftR, shftC),
                  dimL, bvY0, 1, CONS_ROW1, bvY2, 1);
      q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_stop = std::chrono::steady_clock::now();
      if (NUMBER_REPEATS == 1) {
        t6_gmvC = t_stop - t_start;
      } else if (i > 0) {
        t6_gmvC += t_stop - t_start;
      } else {
        t6_gmvC = t_start - t_start;
      }
      v6_gmvC[i] = t_stop - t_start;
  #endif
      auto reducOpY_13 = make_addAssignReduction(bvS6, bvY2, 256, 256);
      ex.reduce(reducOpY_13); q.wait_and_throw();
/* */
      /*****************************************/
/* */
      auto assign_Y2_14 = make_op<Assign>(bvY2, bvY1);
      ex.execute(assign_Y2_14); q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_start = std::chrono::steady_clock::now();
  #endif
      _gemv<14, SYCL>(ex, COL_TEST, dimR - shftR, dimC - shftC, CONS_ROW2, bmM0(shftR, shftC),
                  dimL, bvY0, 1, CONS_ROW1, bvY2, 1);
      q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_stop = std::chrono::steady_clock::now();
      if (NUMBER_REPEATS == 1) {
        t7_gmvC = t_stop - t_start;
      } else if (i > 0) {
        t7_gmvC += t_stop - t_start;
      } else {
        t7_gmvC = t_start - t_start;
      }
      v7_gmvC[i] = t_stop - t_start;
  #endif
      auto reducOpY_14 = make_addAssignReduction(bvS7, bvY2, 256, 256);
      ex.reduce(reducOpY_14); q.wait_and_throw();
/* */
      /*****************************************/
/* */
      auto assign_Y2_15 = make_op<Assign>(bvY2, bvY1);
      ex.execute(assign_Y2_15); q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_start = std::chrono::steady_clock::now();
  #endif
      _gemv<15, SYCL>(ex, COL_TEST, dimR - shftR, dimC - shftC, CONS_ROW2, bmM0(shftR, shftC),
                  dimL, bvY0, 1, CONS_ROW1, bvY2, 1);
      q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_stop = std::chrono::steady_clock::now();
      if (NUMBER_REPEATS == 1) {
        t8_gmvC = t_stop - t_start;
      } else if (i > 0) {
        t8_gmvC += t_stop - t_start;
      } else {
        t8_gmvC = t_start - t_start;
      }
      v8_gmvC[i] = t_stop - t_start;
  #endif
      auto reducOpY_15 = make_addAssignReduction(bvS8, bvY2, 256, 256);
      ex.reduce(reducOpY_15); q.wait_and_throw();
/* */
      /*****************************************/
/* */
      auto assign_Y2_16 = make_op<Assign>(bvY2, bvY1);
      ex.execute(assign_Y2_16); q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_start = std::chrono::steady_clock::now();
  #endif
      _gemv<16, SYCL>(ex, COL_TEST, dimR - shftR, dimC - shftC, CONS_ROW2, bmM0(shftR, shftC),
                  dimL, bvY0, 1, CONS_ROW1, bvY2, 1);
      q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_stop = std::chrono::steady_clock::now();
      if (NUMBER_REPEATS == 1) {
        t9_gmvC = t_stop - t_start;
      } else if (i > 0) {
        t9_gmvC += t_stop - t_start;
      } else {
        t9_gmvC = t_start - t_start;
      }
      v9_gmvC[i] = t_stop - t_start;
  #endif
      auto reducOpY_16 = make_addAssignReduction(bvS9, bvY2, 256, 256);
      ex.reduce(reducOpY_16); q.wait_and_throw();
/* */
      /*****************************************/
/* */
      auto assign_Y2_17 = make_op<Assign>(bvY2, bvY1);
      ex.execute(assign_Y2_17); q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_start = std::chrono::steady_clock::now();
  #endif
      _gemv<17, SYCL>(ex, COL_TEST, dimR - shftR, dimC - shftC, CONS_ROW2, bmM0(shftR, shftC),
                  dimL, bvY0, 1, CONS_ROW1, bvY2, 1);
      q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_stop = std::chrono::steady_clock::now();
      if (NUMBER_REPEATS == 1) {
        t10_gmvC = t_stop - t_start;
      } else if (i > 0) {
        t10_gmvC += t_stop - t_start;
      } else {
        t10_gmvC = t_start - t_start;
      }
      v10_gmvC[i] = t_stop - t_start;
  #endif
      auto reducOpY_17 = make_addAssignReduction(bvS10, bvY2, 256, 256);
      ex.reduce(reducOpY_17); q.wait_and_throw();
/* */
      /*****************************************/
/* */
      auto assign_Y2_18 = make_op<Assign>(bvY2, bvY1);
      ex.execute(assign_Y2_18); q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_start = std::chrono::steady_clock::now();
  #endif
      _gemv<18, SYCL>(ex, COL_TEST, dimR - shftR, dimC - shftC, CONS_ROW2, bmM0(shftR, shftC),
                  dimL, bvY0, 1, CONS_ROW1, bvY2, 1);
      q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_stop = std::chrono::steady_clock::now();
      if (NUMBER_REPEATS == 1) {
        t11_gmvC = t_stop - t_start;
      } else if (i > 0) {
        t11_gmvC += t_stop - t_start;
      } else {
        t11_gmvC = t_start - t_start;
      }
      v11_gmvC[i] = t_stop - t_start;
  #endif
      auto reducOpY_18 = make_addAssignReduction(bvS11, bvY2, 256, 256);
      ex.reduce(reducOpY_18); q.wait_and_throw();
/* */
      /*****************************************/
/* */
      auto assign_Y2_19 = make_op<Assign>(bvY2, bvY1);
      ex.execute(assign_Y2_19); q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_start = std::chrono::steady_clock::now();
  #endif
      _gemv<19, SYCL>(ex, COL_TEST, dimR - shftR, dimC - shftC, CONS_ROW2, bmM0(shftR, shftC),
                  dimL, bvY0, 1, CONS_ROW1, bvY2, 1);
      q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_stop = std::chrono::steady_clock::now();
      if (NUMBER_REPEATS == 1) {
        t12_gmvC = t_stop - t_start;
      } else if (i > 0) {
        t12_gmvC += t_stop - t_start;
      } else {
        t12_gmvC = t_start - t_start;
      }
      v12_gmvC[i] = t_stop - t_start;
  #endif
      auto reducOpY_19 = make_addAssignReduction(bvS12, bvY2, 256, 256);
      ex.reduce(reducOpY_19); q.wait_and_throw();
/* */
      /*****************************************/
/* */
      auto assign_Y2_20 = make_op<Assign>(bvY2, bvY1);
      ex.execute(assign_Y2_20); q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_start = std::chrono::steady_clock::now();
  #endif
      _gemv<20, SYCL>(ex, COL_TEST, dimR - shftR, dimC - shftC, CONS_ROW2, bmM0(shftR, shftC),
                  dimL, bvY0, 1, CONS_ROW1, bvY2, 1);
      q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_stop = std::chrono::steady_clock::now();
      if (NUMBER_REPEATS == 1) {
        t13_gmvC = t_stop - t_start;
      } else if (i > 0) {
        t13_gmvC += t_stop - t_start;
      } else {
        t13_gmvC = t_start - t_start;
      }
      v13_gmvC[i] = t_stop - t_start;
  #endif
      auto reducOpY_20 = make_addAssignReduction(bvS13, bvY2, 256, 256);
      ex.reduce(reducOpY_20); q.wait_and_throw();
/* */
      /*****************************************/
/* */
  #ifdef SHOW_TIMES
      t_start = std::chrono::steady_clock::now();
  #endif
      _ger<1, SYCL>(ex, dimR - shftR, dimC - shftC, CONS_GER, bvY0, 1, bvX0, 1,
                 bmM0(shftR, shftC), dimL);
      q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_stop = std::chrono::steady_clock::now();
      if (NUMBER_REPEATS == 1) {
        t1_ger = t_stop - t_start;
      } else if (i > 0) {
        t1_ger += t_stop - t_start;
      } else {
        t1_ger = t_start - t_start;
      }
      v1_ger[i] = t_stop - t_start;
  #endif
      auto reducOpV_1 = make_addAssignReduction(bvT1, bvV0, 256, 256);
      ex.reduce(reducOpV_1); q.wait_and_throw();
/* */
      /*****************************************/
/* */
      auto assign_M0_11 = make_op<Assign>(bmM0, bmM1);
      ex.execute(assign_M0_11); q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_start = std::chrono::steady_clock::now();
  #endif
      _ger<11, SYCL>(ex, dimR - shftR, dimC - shftC, CONS_GER, bvY0, 1, bvX0, 1,
                 bmM0(shftR, shftC), dimL);
      q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_stop = std::chrono::steady_clock::now();
      if (NUMBER_REPEATS == 1) {
        t2_ger = t_stop - t_start;
      } else if (i > 0) {
        t2_ger += t_stop - t_start;
      } else {
        t2_ger = t_start - t_start;
      }
      v2_ger[i] = t_stop - t_start;
  #endif
      auto reducOpV_11 = make_addAssignReduction(bvT2, bvV0, 256, 256);
      ex.reduce(reducOpV_11); q.wait_and_throw();
/* */
      /*****************************************/
/* */
      auto assign_M0_12 = make_op<Assign>(bmM0, bmM1);
      ex.execute(assign_M0_12); q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_start = std::chrono::steady_clock::now();
  #endif
      _ger<12, SYCL>(ex, dimR - shftR, dimC - shftC, CONS_GER, bvY0, 1, bvX0, 1,
                 bmM0(shftR, shftC), dimL);
      q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_stop = std::chrono::steady_clock::now();
      if (NUMBER_REPEATS == 1) {
        t3_ger = t_stop - t_start;
      } else if (i > 0) {
        t3_ger += t_stop - t_start;
      } else {
        t3_ger = t_start - t_start;
      }
      v3_ger[i] = t_stop - t_start;
  #endif
      auto reducOpV_12 = make_addAssignReduction(bvT3, bvV0, 256, 256);
      ex.reduce(reducOpV_12); q.wait_and_throw();
/* */
    }
  }

#ifdef SHOW_TIMES
    int div = (NUMBER_REPEATS == 1)? 1: (NUMBER_REPEATS-1);
//    int div = 1;
    // COMPUTATIONAL TIMES
    std::cout << "t_gemvR , " << t1_gmvR.count()/div
              << ", " << t2_gmvR.count()/div
              << ", " << t3_gmvR.count()/div
              << ", " << t4_gmvR.count()/div
              << ", " << t5_gmvR.count()/div
              << ", " << t6_gmvR.count()/div
              << std::endl;
    std::cout << "t_gemvC , " << t1_gmvC.count()/div
              << ", " << t2_gmvC.count()/div
              << ", " << t3_gmvC.count()/div
              << ", " << t4_gmvC.count()/div
              << ", " << t5_gmvC.count()/div
              << ", " << t6_gmvC.count()/div
              << ", " << t7_gmvC.count()/div
              << ", " << t8_gmvC.count()/div
              << ", " << t9_gmvC.count()/div
              << ", " << t10_gmvC.count()/div
              << ", " << t11_gmvC.count()/div
              << ", " << t12_gmvC.count()/div
              << ", " << t13_gmvC.count()/div
              << std::endl;
    std::cout << "t_ger   , " << t1_ger.count()/div
              <<  ", "        << t2_ger.count()/div
              <<  ", "        << t3_ger.count()/div
              << std::endl;
    std::sort (v1_gmvR.begin()+1, v1_gmvR.end());
    std::sort (v2_gmvR.begin()+1, v2_gmvR.end());
    std::sort (v3_gmvR.begin()+1, v3_gmvR.end());
    std::sort (v4_gmvR.begin()+1, v4_gmvR.end());
    std::sort (v5_gmvR.begin()+1, v5_gmvR.end());
    std::sort (v6_gmvR.begin()+1, v6_gmvR.end());
    std::cout << "m_gemvR , " << v1_gmvR[(NUMBER_REPEATS+1)/2].count()
              << ", "         << v2_gmvR[(NUMBER_REPEATS+1)/2].count()
              << ", "         << v3_gmvR[(NUMBER_REPEATS+1)/2].count()
              << ", "         << v4_gmvR[(NUMBER_REPEATS+1)/2].count()
              << ", "         << v5_gmvR[(NUMBER_REPEATS+1)/2].count()
              << ", "         << v6_gmvR[(NUMBER_REPEATS+1)/2].count()
              << std::endl;
    std::sort (v1_gmvC.begin()+1, v1_gmvC.end());
    std::sort (v2_gmvC.begin()+1, v2_gmvC.end());
    std::sort (v3_gmvC.begin()+1, v3_gmvC.end());
    std::sort (v4_gmvC.begin()+1, v4_gmvC.end());
    std::sort (v5_gmvC.begin()+1, v5_gmvC.end());
    std::sort (v6_gmvC.begin()+1, v6_gmvC.end());
    std::sort (v7_gmvC.begin()+1, v7_gmvC.end());
    std::sort (v8_gmvC.begin()+1, v8_gmvC.end());
    std::sort (v9_gmvC.begin()+1, v9_gmvC.end());
    std::sort (v10_gmvC.begin()+1, v10_gmvC.end());
    std::sort (v11_gmvC.begin()+1, v11_gmvC.end());
    std::sort (v12_gmvC.begin()+1, v12_gmvC.end());
    std::sort (v13_gmvC.begin()+1, v13_gmvC.end());
    std::cout << "m_gemvC , " << v1_gmvC[(NUMBER_REPEATS+1)/2].count()
              << ", "         << v2_gmvC[(NUMBER_REPEATS+1)/2].count()
              << ", "         << v3_gmvC[(NUMBER_REPEATS+1)/2].count()
              << ", "         << v4_gmvC[(NUMBER_REPEATS+1)/2].count()
              << ", "         << v5_gmvC[(NUMBER_REPEATS+1)/2].count()
              << ", "         << v6_gmvC[(NUMBER_REPEATS+1)/2].count()
              << ", "         << v7_gmvC[(NUMBER_REPEATS+1)/2].count()
              << ", "         << v8_gmvC[(NUMBER_REPEATS+1)/2].count()
              << ", "         << v9_gmvC[(NUMBER_REPEATS+1)/2].count()
              << ", "         << v10_gmvC[(NUMBER_REPEATS+1)/2].count()
              << ", "         << v11_gmvC[(NUMBER_REPEATS+1)/2].count()
              << ", "         << v12_gmvC[(NUMBER_REPEATS+1)/2].count()
              << ", "         << v13_gmvC[(NUMBER_REPEATS+1)/2].count()
              << std::endl;
    std::sort (v1_ger.begin()+1, v1_ger.end());
    std::sort (v2_ger.begin()+1, v2_ger.end());
    std::sort (v3_ger.begin()+1, v3_ger.end());
    std::cout << "m_ger   , " << v1_ger[(NUMBER_REPEATS+1)/2].count()
              << ", "         << v2_ger[(NUMBER_REPEATS+1)/2].count()
              << ", "         << v3_ger[(NUMBER_REPEATS+1)/2].count()
              << std::endl;

#endif

  std::cout << "GEMVR ANALYSYS!!" << std::endl;
  // ANALYSIS OF THE RESULTS
  for (int i=0; i<6; i++) {
    res = vR[i];
#ifdef SHOW_VALUES
    std::cout << "( " << i+((i>4)?15:((i>0)?10:1)) << ") ";
    std::cout << "VALUES!! --> res = " << res << " , addX = " << addX
              << " , err = " << addX - res << std::endl;
#endif  // VERBOSE
    if (std::abs((res - addX) / res) > ERROR_ALLOWED) {
      std::cout << "( " << i+((i>0)?10:1) << ") ";
      std::cout << "ERROR!! --> res = " << res << " , addX = " << addX
                << " , err = " << addX - res << std::endl;
      returnVal += 1;
    }
  }

  std::cout << "GEMVC ANALYSYS!!" << std::endl;
  for (int i=0; i<13; i++) {
    res = vS[i];
  #ifdef SHOW_VALUES
    std::cout << "( " << i+((i>2)?8:1) << ") ";
    std::cout << "VALUES!! --> res = " << res << " , addY = " << addY
              << " , err = " << addY - res << std::endl;
  #endif  // VERBOSE
    if (std::abs((res - addY) / res) > ERROR_ALLOWED) {
      std::cout << "( " << i+((i>2)?8:1) << ") ";
      std::cout << "ERROR!! --> res = " << res << " , addY = " << addY
                << " , err = " << addY - res << std::endl;
      returnVal += 2;
    }
  }

  std::cout << "GER ANALYSYS!!" << std::endl;
  for (int i=0; i<3; i++) {
    res = vT[i];
  #ifdef SHOW_VALUES
    std::cout << "( " << i+((i>0)?10:1) << ") ";
    std::cout << "VALUES!! --> res = " << res << " , addRng1 = " << addRng1
              << " , err = " << addRng1 - res << std::endl;
  #endif  // VERBOSE
    if (std::abs((res - addRng1) / res) > ERROR_ALLOWED) {
      std::cout << "( " << i+((i>0)?10:1) << ") ";
      std::cout << "ERROR!! --> res = " << res << " , addRng1 = " << addRng1
                << " , err = " << addRng1 - res << std::endl;
      returnVal += 2;
    }
  }

  return returnVal;
}

size_t TestingBLAS2_New(bool accessDev, size_t dim, size_t divSz, size_t shftR,
                        size_t shftC) {
  // CREATING DATA
  size_t dimR = dim / divSz;
  size_t dimC = dim * divSz;
  size_t dimL = ((accessDev) ? dimC : dimR);
  std::vector<BASETYPE> vM0(dimR * dimC);
  std::vector<BASETYPE> vM1(dimR * dimC);
/*
  std::vector<BASETYPE> vX0(dimC);
  std::vector<BASETYPE> vY0(dimR);
  std::vector<BASETYPE> vX1(dimC);
  std::vector<BASETYPE> vY1(dimR);
  std::vector<BASETYPE> vX2(dimC);
  std::vector<BASETYPE> vY2(dimR);
*/
  std::vector<BASETYPE> vX0(dimR);
  std::vector<BASETYPE> vY0(dimC);
  std::vector<BASETYPE> vX1(dimC);
  std::vector<BASETYPE> vY1(dimR);
  std::vector<BASETYPE> vX2(dimC);
  std::vector<BASETYPE> vY2(dimR);
/**/
  std::vector<BASETYPE> vR(10);
  std::vector<BASETYPE> vS(10);
  std::vector<BASETYPE> vT(10);
  std::vector<BASETYPE> vL(10);
  std::vector<BASETYPE> vD(10);
  std::vector<BASETYPE> vU(10);
#ifdef SHOW_TIMES
  std::chrono::time_point<std::chrono::steady_clock> t_start, t_stop;
  std::chrono::duration<BASETYPE> t0_gmvR, t0_gmvC, t0_ger, t0_lowY, t0_uppY;
  std::chrono::duration<BASETYPE> t1_gmvR, t1_gmvC, t1_ger, t1_lowY, t1_uppY;
  std::chrono::duration<BASETYPE> t2_gmvR, t2_gmvC, t2_ger, t2_lowY, t2_uppY;
  std::chrono::duration<BASETYPE> t3_gmvR, t3_gmvC, t3_ger, t3_lowY, t3_uppY;
  std::chrono::duration<BASETYPE> t4_gmvR, t4_gmvC, t4_ger, t4_lowY, t4_uppY;
  std::chrono::duration<BASETYPE> t5_gmvR, t5_gmvC, t5_ger, t5_lowY, t5_uppY;
  std::chrono::duration<BASETYPE> t6_gmvR, t6_gmvC, t6_ger, t6_lowY, t6_uppY;
  std::chrono::duration<BASETYPE> t7_gmvR, t7_gmvC, t7_ger, t7_lowY, t7_uppY;
  std::chrono::duration<BASETYPE> t8_gmvR, t8_gmvC, t8_ger, t8_lowY, t8_uppY;
  std::chrono::duration<BASETYPE> t9_gmvR, t9_gmvC, t9_ger, t9_lowY, t9_uppY;
  std::vector<std::chrono::duration<BASETYPE>> v0_gmvR(NUMBER_REPEATS), v0_gmvC(NUMBER_REPEATS), v0_ger(NUMBER_REPEATS);
  std::vector<std::chrono::duration<BASETYPE>> v1_gmvR(NUMBER_REPEATS), v1_gmvC(NUMBER_REPEATS), v1_ger(NUMBER_REPEATS);
  std::vector<std::chrono::duration<BASETYPE>> v2_gmvR(NUMBER_REPEATS), v2_gmvC(NUMBER_REPEATS), v2_ger(NUMBER_REPEATS);
  std::vector<std::chrono::duration<BASETYPE>> v3_gmvR(NUMBER_REPEATS), v3_gmvC(NUMBER_REPEATS), v3_ger(NUMBER_REPEATS);
  std::vector<std::chrono::duration<BASETYPE>> v4_gmvR(NUMBER_REPEATS), v4_gmvC(NUMBER_REPEATS), v4_ger(NUMBER_REPEATS);
  std::vector<std::chrono::duration<BASETYPE>> v5_gmvR(NUMBER_REPEATS), v5_gmvC(NUMBER_REPEATS), v5_ger(NUMBER_REPEATS);
  std::vector<std::chrono::duration<BASETYPE>> v6_gmvR(NUMBER_REPEATS), v6_gmvC(NUMBER_REPEATS), v6_ger(NUMBER_REPEATS);
  std::vector<std::chrono::duration<BASETYPE>> v7_gmvR(NUMBER_REPEATS), v7_gmvC(NUMBER_REPEATS), v7_ger(NUMBER_REPEATS);
  std::vector<std::chrono::duration<BASETYPE>> v8_gmvR(NUMBER_REPEATS), v8_gmvC(NUMBER_REPEATS), v8_ger(NUMBER_REPEATS);
  std::vector<std::chrono::duration<BASETYPE>> v9_gmvR(NUMBER_REPEATS), v9_gmvC(NUMBER_REPEATS), v9_ger(NUMBER_REPEATS);
  std::vector<std::chrono::duration<BASETYPE>> v0_lowY(NUMBER_REPEATS), v0_uppY(NUMBER_REPEATS);
  std::vector<std::chrono::duration<BASETYPE>> v1_lowY(NUMBER_REPEATS), v1_uppY(NUMBER_REPEATS);
  std::vector<std::chrono::duration<BASETYPE>> v2_lowY(NUMBER_REPEATS), v2_uppY(NUMBER_REPEATS);
  std::vector<std::chrono::duration<BASETYPE>> v3_lowY(NUMBER_REPEATS), v3_uppY(NUMBER_REPEATS);
  std::vector<std::chrono::duration<BASETYPE>> v4_lowY(NUMBER_REPEATS), v4_uppY(NUMBER_REPEATS);
  std::vector<std::chrono::duration<BASETYPE>> v5_lowY(NUMBER_REPEATS), v5_uppY(NUMBER_REPEATS);
  std::vector<std::chrono::duration<BASETYPE>> v6_lowY(NUMBER_REPEATS), v6_uppY(NUMBER_REPEATS);
  std::vector<std::chrono::duration<BASETYPE>> v7_lowY(NUMBER_REPEATS), v7_uppY(NUMBER_REPEATS);
  std::vector<std::chrono::duration<BASETYPE>> v8_lowY(NUMBER_REPEATS), v8_uppY(NUMBER_REPEATS);
  std::vector<std::chrono::duration<BASETYPE>> v9_lowY(NUMBER_REPEATS), v9_uppY(NUMBER_REPEATS);
#endif

  // INITIALIZING DATA
  size_t vSeed, gap;
  BASETYPE minV, maxV;
#ifdef RANDOM_DATA
  vSeed = 1;
  minV = -10.0;
  maxV = 10.0;
  gap = (size_t)(maxV - minV + 1);
  srand(vSeed);
#else   // RANDOM_DATA
  minV = 1.00;
  maxV = 1.00;
#endif  // RANDOM_DATA
  std::for_each(std::begin(vM1), std::end(vM1), [&](BASETYPE &elem) {
#ifdef RANDOM_DATA
    elem = minV + (BASETYPE)(rand() % gap);
#else   // RANDOM_DATA
    elem = minV; minV += maxV;
#endif  // RANDOM_DATA

  });

#ifdef RANDOM_DATA
  vSeed = 1;
  minV = -10.0;
  maxV = 10.0;
  gap = (size_t)(maxV - minV + 1);
  srand(vSeed);
#else   // RANDOM_DATA
  minV = 1.00;
  maxV = 1.00;
#endif  // RANDOM_DATA
  std::for_each(std::begin(vX0), std::end(vX0), [&](BASETYPE &elem) {
#ifdef RANDOM_DATA
    elem = minV + (BASETYPE)(rand() % gap);
#else   // RANDOM_DATA
    elem = minV; minV += maxV;
#endif  // RANDOM_DATA
  });

#ifdef RANDOM_DATA
  vSeed = 2;
  minV = -10.0;
  maxV = 10.0;
  gap = (size_t)(maxV - minV + 1);
  srand(vSeed);
#else   // RANDOM_DATA
  minV = 1.00;
  maxV = 1.00;
#endif  // RANDOM_DATA
  std::for_each(std::begin(vX1), std::end(vX1), [&](BASETYPE &elem) {
#ifdef RANDOM_DATA
    elem = minV + (BASETYPE)(rand() % gap);
#else   // RANDOM_DATA
    elem = minV; minV += maxV;
#endif  // RANDOM_DATA
  });

#ifdef RANDOM_DATA
  vSeed = 3;
  minV = -10.0;
  maxV = 10.0;
  gap = (size_t)(maxV - minV + 1);
  srand(vSeed);
#else   // RANDOM_DATA
  minV = 1.00;
  maxV = 1.00;
#endif  // RANDOM_DATA
  std::for_each(std::begin(vY0), std::end(vY0), [&](BASETYPE &elem) {
#ifdef RANDOM_DATA
    elem = minV + (BASETYPE)(rand() % gap);
#else   // RANDOM_DATA
    elem = minV; minV += maxV;
#endif  // RANDOM_DATA
  });

#ifdef RANDOM_DATA
  vSeed = 4;
  minV = -10.0;
  maxV = 10.0;
  gap = (size_t)(maxV - minV + 1);
  srand(vSeed);
#else   // RANDOM_DATA
  minV = 1.00;
  maxV = 1.00;
#endif  // RANDOM_DATA
  std::for_each(std::begin(vY1), std::end(vY1), [&](BASETYPE &elem) {
#ifdef RANDOM_DATA
    elem = minV + (BASETYPE)(rand() % gap);
#else   // RANDOM_DATA
    elem = minV; minV += maxV;
#endif  // RANDOM_DATA
  });

  for (int i=0; i<10; i++) {
    vR[i] = 0.0;
    vS[i] = 0.0;
    vT[i] = 0.0;
    vL[i] = 0.0;
    vD[i] = 0.0;
    vU[i] = 0.0;
  }

  // COMPUTING THE RESULTS
  size_t returnVal = 0;
  BASETYPE res;

  BASETYPE addY = 0.0, auxY;
  BASETYPE lowY = 0.0, diaY = 0.0, uppY = 0.0, untY = 0.0;
  for (size_t i = shftR; i < dimR; i++) {
//    vY2[i - shftR] = 1.5 * vY[i - shftR];
    auxY = CONS_ROW1 * vY1[i - shftR];
    for (size_t j = shftC; j < dimC; j++) {
      if (accessDev) {
//        vY2[i - shftR] += 2.0 * vM[dimC * i + j] * vY[j - shftC];
        auxY += CONS_ROW2 * vM1[dimC * i + j] * vY0[j - shftC];
      } else {
//        vY2[i - shftR] += 2.0 * vM[dimR * j + i] * vY[j - shftC];
        auxY += CONS_ROW2 * vM1[dimR * j + i] * vY0[j - shftC];
      }
      if (i-shftR > j-shftC)
        lowY += vM1[(accessDev?(dimC*i+j):(dimR*j+i))] * vY0[j - shftC];
      else if (i-shftR < j-shftC)
        uppY += vM1[(accessDev?(dimC*i+j):(dimR*j+i))] * vY0[j - shftC];
      else {
        diaY += vM1[(accessDev?(dimC*i+j):(dimR*j+i))] * vY0[j - shftC];
        untY += vM1[(accessDev?(dimC*i+j):(dimR*j+i))];
      }
    }
//    addY += vY2[i - shftR];
    addY += auxY;
  }
  for (size_t i = dimR - shftR; i < dimR; i++) {
    addY += vY1[i];
    lowY += vY1[i];
    diaY += vY1[i];
    uppY += vY1[i];
  }

  BASETYPE addX = 0.0, auxX;
  BASETYPE lowX = 0.0, diaX = 0.0, uppX = 0.0, untX = 0.0;
  for (size_t j = shftC; j < dimC; j++) {
//    vX2[j - shftC] = 0.5 * vX2[j - shftC];
    auxX = CONS_COL1 * vX1[j - shftC];
    for (size_t i = shftR; i < dimR; i++) {
      if (accessDev) {
//        vX2[j - shftC] += 2.5 * vM[dimC * i + j] * vX[i - shftR];
        auxX += CONS_COL2 * vM1[dimC * i + j] * vX0[i - shftR];
      } else {
//        vX2[j - shftC] += 2.5 * vM[dimR * j + i] * vX[i - shftR];
        auxX += CONS_COL2 * vM1[dimR * j + i] * vX0[i - shftR];
      }
      if (i-shftR > j-shftC)
        lowX += vM1[(accessDev?(dimC*i+j):(dimR*j+i))] * vX0[i - shftR];
      else if (i-shftR < j-shftC)
        uppX += vM1[(accessDev?(dimC*i+j):(dimR*j+i))] * vX0[i - shftR];
      else {
        diaX += vM1[(accessDev?(dimC*i+j):(dimR*j+i))] * vX0[i - shftR];
        untX += vM1[(accessDev?(dimC*i+j):(dimR*j+i))];
      }
    }
//    addX += vX2[j - shftC];
//    printf("VX2(%lu) = %f\n", j, auxX);
    addX += auxX;
  }
  for (size_t j = dimC - shftC; j < dimC; j++) {
    addX += vX1[j];
    lowX += vX1[j];
    diaX += vX1[j];
    uppX += vX1[j];
  }

  BASETYPE addRng1 = 0.0;
  for (size_t i = 0; i < dimR; i++) {
    for (size_t j = 0; j < dimC; j++) {
      BASETYPE aux = 0.0;
//      addRng1 += (accessDev) ? vM1[dimC * i + j] : vM1[dimR * j + i];
      aux += (accessDev) ? vM1[dimC * i + j] : vM1[dimR * j + i];
      if ((i >= shftR) && (j >= shftC)) {
//        addRng1 += (3.0 * vY0[i - shftR] * vX0[j - shftC]);
//        aux += (CONS_GER * vY0[i - shftR] * vX0[j - shftC]);
        aux += (CONS_GER * vX0[i - shftR] * vY0[j - shftC]);
      }
      addRng1 += aux;
    }
  }

  // CREATING THE SYCL QUEUE AND EXECUTOR
  // cl::sycl::cpu_selector s; NOOOOO
#ifdef EXECUTED_ON_GPU
  cl::sycl::gpu_selector   s;
#else
  cl::sycl::intel_selector s;
#endif
  cl::sycl::queue q([=](cl::sycl::exception_list eL) {
    try {
      for (auto &e : eL) {
        std::rethrow_exception(e);
      }
    } catch (cl::sycl::exception &e) {
      std::cout << " E " << e.what() << std::endl;
    } catch (...) {
      std::cout << " An exception " << std::endl;
    }
  });
  Executor<SYCL> ex(q);

  {
    // CREATION OF THE BUFFERS
    buffer<BASETYPE, 1> bM0(vM0.data(), range<1>{vM0.size()});
    buffer<BASETYPE, 1> bM1(vM1.data(), range<1>{vM1.size()});
    buffer<BASETYPE, 1> bX0(vX0.data(), range<1>{vX0.size()});
    buffer<BASETYPE, 1> bY0(vY0.data(), range<1>{vY0.size()});
    buffer<BASETYPE, 1> bX1(vX1.data(), range<1>{vX1.size()});
    buffer<BASETYPE, 1> bY1(vY1.data(), range<1>{vY1.size()});
    buffer<BASETYPE, 1> bX2(vX2.data(), range<1>{vX2.size()});
    buffer<BASETYPE, 1> bY2(vY2.data(), range<1>{vY2.size()});
    buffer<BASETYPE, 1> bR(vR.data(), range<1>{vR.size()});
    buffer<BASETYPE, 1> bS(vS.data(), range<1>{vS.size()});
    buffer<BASETYPE, 1> bT(vT.data(), range<1>{vT.size()});
    buffer<BASETYPE, 1> bL(vL.data(), range<1>{vL.size()});
    buffer<BASETYPE, 1> bD(vD.data(), range<1>{vD.size()});
    buffer<BASETYPE, 1> bU(vU.data(), range<1>{vU.size()});

    // BUILDING A SYCL VIEW OF THE BUFFERS
    BufferMatrixView<BASETYPE> bmM0(bM0, accessDev, dimR, dimC);
    BufferMatrixView<BASETYPE> bmM1(bM1, accessDev, dimR, dimC);
    BufferVectorView<BASETYPE> bvV0(bM0);
    BufferVectorView<BASETYPE> bvX0(bX0);
    BufferVectorView<BASETYPE> bvY0(bY0);
    BufferVectorView<BASETYPE> bvX1(bX1);
    BufferVectorView<BASETYPE> bvY1(bY1);
    BufferVectorView<BASETYPE> bvX2(bX2);
    BufferVectorView<BASETYPE> bvY2(bY2);
    BufferVectorView<BASETYPE> bvR0(bR,0);
    BufferVectorView<BASETYPE> bvR1(bR,1);
    BufferVectorView<BASETYPE> bvR2(bR,2);
    BufferVectorView<BASETYPE> bvR3(bR,3);
    BufferVectorView<BASETYPE> bvR4(bR,4);
    BufferVectorView<BASETYPE> bvR5(bR,5);
    BufferVectorView<BASETYPE> bvR6(bR,6);
    BufferVectorView<BASETYPE> bvR7(bR,7);
    BufferVectorView<BASETYPE> bvR8(bR,8);
    BufferVectorView<BASETYPE> bvR9(bR,9);
    BufferVectorView<BASETYPE> bvS0(bS,0);
    BufferVectorView<BASETYPE> bvS1(bS,1);
    BufferVectorView<BASETYPE> bvS2(bS,2);
    BufferVectorView<BASETYPE> bvS3(bS,3);
    BufferVectorView<BASETYPE> bvS4(bS,4);
    BufferVectorView<BASETYPE> bvS5(bS,5);
    BufferVectorView<BASETYPE> bvS6(bS,6);
    BufferVectorView<BASETYPE> bvS7(bS,7);
    BufferVectorView<BASETYPE> bvS8(bS,8);
    BufferVectorView<BASETYPE> bvS9(bS,9);
    BufferVectorView<BASETYPE> bvT0(bT,0);
    BufferVectorView<BASETYPE> bvT1(bT,1);
    BufferVectorView<BASETYPE> bvT2(bT,2);
    BufferVectorView<BASETYPE> bvT3(bT,3);
    BufferVectorView<BASETYPE> bvT4(bT,4);
    BufferVectorView<BASETYPE> bvT5(bT,5);
    BufferVectorView<BASETYPE> bvT6(bT,6);
    BufferVectorView<BASETYPE> bvT7(bT,7);
    BufferVectorView<BASETYPE> bvT8(bT,8);
    BufferVectorView<BASETYPE> bvT9(bT,9);
    BufferVectorView<BASETYPE> bvL0(bL,0);
    BufferVectorView<BASETYPE> bvL1(bL,1);
    BufferVectorView<BASETYPE> bvL2(bL,2);
    BufferVectorView<BASETYPE> bvL3(bL,3);
    BufferVectorView<BASETYPE> bvL4(bL,4);
    BufferVectorView<BASETYPE> bvL5(bL,5);
    BufferVectorView<BASETYPE> bvL6(bL,6);
    BufferVectorView<BASETYPE> bvL7(bL,7);
    BufferVectorView<BASETYPE> bvL8(bL,8);
    BufferVectorView<BASETYPE> bvL9(bL,9);
    BufferVectorView<BASETYPE> bvD0(bD,0);
    BufferVectorView<BASETYPE> bvD1(bD,1);
    BufferVectorView<BASETYPE> bvD2(bD,2);
    BufferVectorView<BASETYPE> bvD3(bD,3);
    BufferVectorView<BASETYPE> bvD4(bD,4);
    BufferVectorView<BASETYPE> bvD5(bD,5);
    BufferVectorView<BASETYPE> bvD6(bD,6);
    BufferVectorView<BASETYPE> bvD7(bD,7);
    BufferVectorView<BASETYPE> bvD8(bD,8);
    BufferVectorView<BASETYPE> bvD9(bD,9);
    BufferVectorView<BASETYPE> bvU0(bU,0);
    BufferVectorView<BASETYPE> bvU1(bU,1);
    BufferVectorView<BASETYPE> bvU2(bU,2);
    BufferVectorView<BASETYPE> bvU3(bU,3);
    BufferVectorView<BASETYPE> bvU4(bU,4);
    BufferVectorView<BASETYPE> bvU5(bU,5);
    BufferVectorView<BASETYPE> bvU6(bU,6);
    BufferVectorView<BASETYPE> bvU7(bU,7);
    BufferVectorView<BASETYPE> bvU8(bU,8);
    BufferVectorView<BASETYPE> bvU9(bU,9);

    // EXECUTION OF THE ROUTINES
    for (int i = 0; i < NUMBER_REPEATS; i++) {
      /*****************************************/
      auto assign_M0 = make_op<Assign>(bmM0, bmM1);
      ex.execute(assign_M0); q.wait_and_throw();

      /*****************************************/
/**/
      auto assign_X2_0 = make_op<Assign>(bvX2, bvX1);
      ex.execute(assign_X2_0); q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_start = std::chrono::steady_clock::now();
  #endif
      _GEMV(ex, ROW_TEST, dimC - shftC, dimR - shftR, CONS_COL2, bmM0(shftR, shftC),
                  dimL, bvX0, 1, CONS_COL1, bvX2, 1);
      q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_stop = std::chrono::steady_clock::now();
      if (NUMBER_REPEATS == 1) {
        t0_gmvR = t_stop - t_start;
      } else if (i > 0) {
        t0_gmvR += t_stop - t_start;
      } else {
        t0_gmvR = t_start - t_start;
      }
      v0_gmvR[i] = t_stop - t_start;
  #endif
      auto reducOpX_0 = make_addAssignReduction(bvR0, bvX2, 256, 256);
      ex.reduce(reducOpX_0); q.wait_and_throw();
/**/
      /*****************************************/
/**/
      auto assign_X2_1 = make_op<Assign>(bvX2, bvX1);
      ex.execute(assign_X2_1); q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_start = std::chrono::steady_clock::now();
  #endif
      _GEMV<256>(ex, ROW_TEST, dimC - shftC, dimR - shftR, CONS_COL2, bmM0(shftR, shftC),
                  dimL, bvX0, 1, CONS_COL1, bvX2, 1);
      q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_stop = std::chrono::steady_clock::now();
      if (NUMBER_REPEATS == 1) {
        t1_gmvR = t_stop - t_start;
      } else if (i > 0) {
        t1_gmvR += t_stop - t_start;
      } else {
        t1_gmvR = t_start - t_start;
      }
      v1_gmvR[i] = t_stop - t_start;
  #endif
      auto reducOpX_1 = make_addAssignReduction(bvR1, bvX2, 256, 256);
      ex.reduce(reducOpX_1); q.wait_and_throw();
/**/
      /*****************************************/
/**/
      auto assign_X2_2 = make_op<Assign>(bvX2, bvX1);
      ex.execute(assign_X2_2); q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_start = std::chrono::steady_clock::now();
  #endif
      _GEMV<256,256,256,256>(ex, ROW_TEST, dimC - shftC, dimR - shftR, CONS_COL2, bmM0(shftR, shftC),
                  dimL, bvX0, 1, CONS_COL1, bvX2, 1);
      q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_stop = std::chrono::steady_clock::now();
      if (NUMBER_REPEATS == 1) {
        t2_gmvR = t_stop - t_start;
      } else if (i > 0) {
        t2_gmvR += t_stop - t_start;
      } else {
        t2_gmvR = t_start - t_start;
      }
      v2_gmvR[i] = t_stop - t_start;
  #endif
      auto reducOpX_2 = make_addAssignReduction(bvR2, bvX2, 256, 256);
      ex.reduce(reducOpX_2); q.wait_and_throw();
/**/
      /*****************************************/
/**/
      auto assign_Y2_0 = make_op<Assign>(bvY2, bvY1);
      ex.execute(assign_Y2_0); q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_start = std::chrono::steady_clock::now();
  #endif
      _GEMV(ex, COL_TEST, dimR - shftR, dimC - shftC, CONS_ROW2, bmM0(shftR, shftC),
                  dimL, bvY0, 1, CONS_ROW1, bvY2, 1);
      q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_stop = std::chrono::steady_clock::now();
      if (NUMBER_REPEATS == 1) {
        t0_gmvC = t_stop - t_start;
      } else if (i > 0) {
        t0_gmvC += t_stop - t_start;
      } else {
        t0_gmvC = t_start - t_start;
      }
      v0_gmvC[i] = t_stop - t_start;
  #endif
      auto reducOpY_0 = make_addAssignReduction(bvS0, bvY2, 256, 256);
      ex.reduce(reducOpY_0); q.wait_and_throw();
/**/
      /*****************************************/
/**/
      auto assign_Y2_1 = make_op<Assign>(bvY2, bvY1);
      ex.execute(assign_Y2_1); q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_start = std::chrono::steady_clock::now();
  #endif
      _GEMV<256>(ex, COL_TEST, dimR - shftR, dimC - shftC, CONS_ROW2, bmM0(shftR, shftC),
                  dimL, bvY0, 1, CONS_ROW1, bvY2, 1);
      q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_stop = std::chrono::steady_clock::now();
      if (NUMBER_REPEATS == 1) {
        t1_gmvC = t_stop - t_start;
      } else if (i > 0) {
        t1_gmvC += t_stop - t_start;
      } else {
        t1_gmvC = t_start - t_start;
      }
      v1_gmvC[i] = t_stop - t_start;
  #endif
      auto reducOpY_1 = make_addAssignReduction(bvS1, bvY2, 256, 256);
      ex.reduce(reducOpY_1); q.wait_and_throw();
/**/
      /*****************************************/
/**/
      auto assign_Y2_2 = make_op<Assign>(bvY2, bvY1);
      ex.execute(assign_Y2_2); q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_start = std::chrono::steady_clock::now();
  #endif
  #ifdef EXECUTED_ON_GPU
      if ((dimR - shftR) == 4096) {
        _GEMV<256,256,1,4096>(ex, COL_TEST, dimR - shftR, dimC - shftC, CONS_ROW2, bmM0(shftR, shftC),
                    dimL, bvY0, 1, CONS_ROW1, bvY2, 1);
      } else if ((dimR - shftR) == 8192) {
        _GEMV<256,256,1,8192>(ex, COL_TEST, dimR - shftR, dimC - shftC, CONS_ROW2, bmM0(shftR, shftC),
                    dimL, bvY0, 1, CONS_ROW1, bvY2, 1);
      } else if ((dimR - shftR) == 16384) {
        _GEMV<256,256,1,16384>(ex, COL_TEST, dimR - shftR, dimC - shftC, CONS_ROW2, bmM0(shftR, shftC),
                    dimL, bvY0, 1, CONS_ROW1, bvY2, 1);
      } else if ((dimR - shftR) == 4000) {
        _GEMV<256,256,1,4000>(ex, COL_TEST, dimR - shftR, dimC - shftC, CONS_ROW2, bmM0(shftR, shftC),
                    dimL, bvY0, 1, CONS_ROW1, bvY2, 1);
      } else if ((dimR - shftR) == 1990) {
        _GEMV<256,256,1,1990>(ex, COL_TEST, dimR - shftR, dimC - shftC, CONS_ROW2, bmM0(shftR, shftC),
                    dimL, bvY0, 1, CONS_ROW1, bvY2, 1);
      } else {
        std::cout << "The value of " << dimR << " is not considered " << std::endl;
      }
  #else
    _GEMV<256,256,256,256>(ex, COL_TEST, dimR - shftR, dimC - shftC, CONS_ROW2, bmM0(shftR, shftC),
                dimL, bvY0, 1, CONS_ROW1, bvY2, 1);
  #endif
      q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_stop = std::chrono::steady_clock::now();
      if (NUMBER_REPEATS == 1) {
        t2_gmvC = t_stop - t_start;
      } else if (i > 0) {
        t2_gmvC += t_stop - t_start;
      } else {
        t2_gmvC = t_start - t_start;
      }
      v2_gmvC[i] = t_stop - t_start;
  #endif
      auto reducOpY_2 = make_addAssignReduction(bvS2, bvY2, 256, 256);
      ex.reduce(reducOpY_2); q.wait_and_throw();
/**/
      /*****************************************/
/**/
      auto assign_UY_0 = make_op<Assign>(bvY2, bvY0);
      ex.execute(assign_UY_0); q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_start = std::chrono::steady_clock::now();
  #endif
      _TRMV(ex, "U", COL_TEST, "N", dimR - shftR, bmM0(shftR, shftC), dimL, bvY2, 1);
      q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_stop = std::chrono::steady_clock::now();
      if (NUMBER_REPEATS == 1) {
        t0_uppY = t_stop - t_start;
      } else if (i > 0) {
        t0_uppY += t_stop - t_start;
      } else {
        t0_uppY = t_start - t_start;
      }
      v0_uppY[i] = t_stop - t_start;
  #endif
      auto reducOpUY_0 = make_addAssignReduction(bvU0, bvY2, 256, 256);
      ex.reduce(reducOpUY_0); q.wait_and_throw();
/**/
      /*****************************************/
/**/
      auto assign_UY_1 = make_op<Assign>(bvY2, bvY0);
      ex.execute(assign_UY_1); q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_start = std::chrono::steady_clock::now();
  #endif
//      _TRMV<256,0>(ex, "U", COL_TEST, "N", dimR - shftR, bmM0(shftR, shftC), dimL, bvY2, 1);
      _TRMV<256>(ex, "U", COL_TEST, "N", dimR - shftR, bmM0(shftR, shftC), dimL, bvY2, 1);
      q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_stop = std::chrono::steady_clock::now();
      if (NUMBER_REPEATS == 1) {
        t1_uppY = t_stop - t_start;
      } else if (i > 0) {
        t1_uppY += t_stop - t_start;
      } else {
        t1_uppY = t_start - t_start;
      }
      v1_uppY[i] = t_stop - t_start;
  #endif
      auto reducOpUY_1 = make_addAssignReduction(bvU1, bvY2, 256, 256);
      ex.reduce(reducOpUY_1); q.wait_and_throw();
/**/
      /*****************************************/
/**/
      auto assign_UY_2 = make_op<Assign>(bvY2, bvY0);
      ex.execute(assign_UY_2); q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_start = std::chrono::steady_clock::now();
  #endif
//      _TRMV<256,0>(ex, "U", COL_TEST, "N", dimR - shftR, bmM0(shftR, shftC), dimL, bvY2, 1);
      _TRMV<256,256,256,256>(ex, "U", COL_TEST, "N", dimR - shftR, bmM0(shftR, shftC), dimL, bvY2, 1);
      q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_stop = std::chrono::steady_clock::now();
      if (NUMBER_REPEATS == 1) {
        t2_uppY = t_stop - t_start;
      } else if (i > 0) {
        t2_uppY += t_stop - t_start;
      } else {
        t2_uppY = t_start - t_start;
      }
      v2_uppY[i] = t_stop - t_start;
  #endif
      auto reducOpUY_2 = make_addAssignReduction(bvU2, bvY2, 256, 256);
      ex.reduce(reducOpUY_2); q.wait_and_throw();
/**/
      /*****************************************/
/**/
      auto assign_LY_0 = make_op<Assign>(bvY2, bvY0);
      ex.execute(assign_LY_0); q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_start = std::chrono::steady_clock::now();
  #endif
//    _TRMV<256,0>(ex, "L", COL_TEST, "U", dimR - shftR, bmM0(shftR, shftC), dimL, bvY2, 1);
      _TRMV(ex, "L", COL_TEST, "U", dimR - shftR, bmM0(shftR, shftC), dimL, bvY2, 1);
      q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_stop = std::chrono::steady_clock::now();
      if (NUMBER_REPEATS == 1) {
        t0_lowY = t_stop - t_start;
      } else if (i > 0) {
        t0_lowY += t_stop - t_start;
      } else {
        t0_lowY = t_start - t_start;
      }
      v0_lowY[i] = t_stop - t_start;
  #endif
      auto reducOpLY_0 = make_addAssignReduction(bvL0, bvY2, 256, 256);
      ex.reduce(reducOpLY_0); q.wait_and_throw();
/**/
      /*****************************************/
/**/
      auto assign_LY_1 = make_op<Assign>(bvY2, bvY0);
      ex.execute(assign_LY_1); q.wait_and_throw();
      #ifdef SHOW_TIMES
      t_start = std::chrono::steady_clock::now();
      #endif
      //    _TRMV<256,0>(ex, "L", COL_TEST, "U", dimR - shftR, bmM0(shftR, shftC), dimL, bvY2, 1);
      _TRMV<256>(ex, "L", COL_TEST, "U", dimR - shftR, bmM0(shftR, shftC), dimL, bvY2, 1);
      q.wait_and_throw();
      #ifdef SHOW_TIMES
      t_stop = std::chrono::steady_clock::now();
      if (NUMBER_REPEATS == 1) {
        t1_lowY = t_stop - t_start;
      } else if (i > 0) {
        t1_lowY += t_stop - t_start;
      } else {
        t1_lowY = t_start - t_start;
      }
      v1_lowY[i] = t_stop - t_start;
      #endif
      auto reducOpLY_1 = make_addAssignReduction(bvL1, bvY2, 256, 256);
      ex.reduce(reducOpLY_1); q.wait_and_throw();
/**/
      /*****************************************/
/**/
      auto assign_LY_2 = make_op<Assign>(bvY2, bvY0);
      ex.execute(assign_LY_2); q.wait_and_throw();
      #ifdef SHOW_TIMES
      t_start = std::chrono::steady_clock::now();
      #endif
      //    _TRMV<256,0>(ex, "L", COL_TEST, "U", dimR - shftR, bmM0(shftR, shftC), dimL, bvY2, 1);
      _TRMV<256,256,256,256>(ex, "L", COL_TEST, "U", dimR - shftR, bmM0(shftR, shftC), dimL, bvY2, 1);
      q.wait_and_throw();
      #ifdef SHOW_TIMES
      t_stop = std::chrono::steady_clock::now();
      if (NUMBER_REPEATS == 1) {
        t2_lowY = t_stop - t_start;
      } else if (i > 0) {
        t2_lowY += t_stop - t_start;
      } else {
        t2_lowY = t_start - t_start;
      }
      v2_lowY[i] = t_stop - t_start;
      #endif
      auto reducOpLY_2 = make_addAssignReduction(bvL2, bvY2, 256, 256);
      ex.reduce(reducOpLY_2); q.wait_and_throw();
/**/
      /*****************************************/
/**/
  #ifdef SHOW_TIMES
      t_start = std::chrono::steady_clock::now();
  #endif
      _GER(ex, dimR - shftR, dimC - shftC, CONS_GER, bvX0, 1, bvY0, 1,
                 bmM0(shftR, shftC), dimL);
      q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_stop = std::chrono::steady_clock::now();
      if (NUMBER_REPEATS == 1) {
        t0_ger = t_stop - t_start;
      } else if (i > 0) {
        t0_ger += t_stop - t_start;
      } else {
        t0_ger = t_start - t_start;
      }
      v0_ger[i] = t_stop - t_start;
  #endif
      auto reducOpV_0 = make_addAssignReduction(bvT0, bvV0, 256, 256);
      ex.reduce(reducOpV_0); q.wait_and_throw();
/**/
      /*****************************************/
/**/
      auto assign_M0_1 = make_op<Assign>(bmM0, bmM1);
      ex.execute(assign_M0_1); q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_start = std::chrono::steady_clock::now();
  #endif
      _GER<256>(ex, dimR - shftR, dimC - shftC, CONS_GER, bvX0, 1, bvY0, 1,
                 bmM0(shftR, shftC), dimL);
      q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_stop = std::chrono::steady_clock::now();
      if (NUMBER_REPEATS == 1) {
        t1_ger = t_stop - t_start;
      } else if (i > 0) {
        t1_ger += t_stop - t_start;
      } else {
        t1_ger = t_start - t_start;
      }
      v1_ger[i] = t_stop - t_start;
  #endif
      auto reducOpV_1 = make_addAssignReduction(bvT1, bvV0, 256, 256);
      ex.reduce(reducOpV_1); q.wait_and_throw();
/**/
      /*****************************************/
/**/
      auto assign_M0_2 = make_op<Assign>(bmM0, bmM1);
      ex.execute(assign_M0_2); q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_start = std::chrono::steady_clock::now();
  #endif
  #ifdef EXECUTED_ON_GPU
      _GER<256,256,1,256>(ex, dimR - shftR, dimC - shftC, CONS_GER, bvX0, 1, bvY0, 1,
                 bmM0(shftR, shftC), dimL);
  #else
      _GER<256,256,256,256>(ex, dimR - shftR, dimC - shftC, CONS_GER, bvX0, 1, bvY0, 1,
                 bmM0(shftR, shftC), dimL);
  #endif
      q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_stop = std::chrono::steady_clock::now();
      if (NUMBER_REPEATS == 1) {
        t2_ger = t_stop - t_start;
      } else if (i > 0) {
        t2_ger += t_stop - t_start;
      } else {
        t2_ger = t_start - t_start;
      }
      v2_ger[i] = t_stop - t_start;
  #endif
      auto reducOpV_2 = make_addAssignReduction(bvT2, bvV0, 256, 256);
      ex.reduce(reducOpV_2); q.wait_and_throw();
/**/
    }
  }

#ifdef SHOW_TIMES
    int div = (NUMBER_REPEATS == 1)? 1: (NUMBER_REPEATS-1);
//    int div = 1;
    // COMPUTATIONAL TIMES
    std::cout << "t_gemvR , " << t0_gmvR.count()/div
              << ", "         << t1_gmvR.count()/div
              << ", "         << t2_gmvR.count()/div
//              << ", " << t3_gmvR.count()/div
//              << ", " << t4_gmvR.count()/div
//              << ", " << t5_gmvR.count()/div
//              << ", " << t6_gmvR.count()/div
              << std::endl;
    std::cout << "t_gemvC , " << t0_gmvC.count()/div
              << ", "         << t1_gmvC.count()/div
              << ", "         << t2_gmvC.count()/div
//              << ", " << t3_gmvC.count()/div
//              << ", " << t4_gmvC.count()/div
//              << ", " << t5_gmvC.count()/div
//              << ", " << t6_gmvC.count()/div
//              << ", " << t7_gmvC.count()/div
//              << ", " << t8_gmvC.count()/div
//              << ", " << t9_gmvC.count()/div
              << std::endl;
    std::cout << "t_uppY  , " << t0_uppY.count()/div
              << ", "         << t1_uppY.count()/div
              << ", "         << t2_uppY.count()/div
              << std::endl;
    std::cout << "t_lowY  , " << t0_lowY.count()/div
              << ", "         << t1_lowY.count()/div
              << ", "         << t2_lowY.count()/div
              << std::endl;
    std::cout << "t_ger   , " << t0_ger.count()/div
              <<  ", "        << t1_ger.count()/div
              <<  ", "        << t2_ger.count()/div
              << std::endl;
    std::sort (v0_gmvR.begin()+1, v0_gmvR.end());
    std::sort (v1_gmvR.begin()+1, v1_gmvR.end());
    std::sort (v2_gmvR.begin()+1, v2_gmvR.end());
//    std::sort (v3_gmvR.begin()+1, v3_gmvR.end());
//    std::sort (v4_gmvR.begin()+1, v4_gmvR.end());
//    std::sort (v5_gmvR.begin()+1, v5_gmvR.end());
//    std::sort (v6_gmvR.begin()+1, v6_gmvR.end());
    std::cout << "m_gemvR , " << v0_gmvR[(NUMBER_REPEATS+1)/2].count()
              << ", "         << v1_gmvR[(NUMBER_REPEATS+1)/2].count()
              << ", "         << v2_gmvR[(NUMBER_REPEATS+1)/2].count()
//              << ", "         << v3_gmvR[(NUMBER_REPEATS+1)/2].count()
//              << ", "         << v4_gmvR[(NUMBER_REPEATS+1)/2].count()
//              << ", "         << v5_gmvR[(NUMBER_REPEATS+1)/2].count()
//              << ", "         << v6_gmvR[(NUMBER_REPEATS+1)/2].count()
              << std::endl;
    std::sort (v0_gmvC.begin()+1, v0_gmvC.end());
    std::sort (v1_gmvC.begin()+1, v1_gmvC.end());
    std::sort (v2_gmvC.begin()+1, v2_gmvC.end());
//    std::sort (v3_gmvC.begin()+1, v3_gmvC.end());
//    std::sort (v4_gmvC.begin()+1, v4_gmvC.end());
//    std::sort (v5_gmvC.begin()+1, v5_gmvC.end());
//    std::sort (v6_gmvC.begin()+1, v6_gmvC.end());
//    std::sort (v7_gmvC.begin()+1, v7_gmvC.end());
//    std::sort (v8_gmvC.begin()+1, v8_gmvC.end());
//    std::sort (v9_gmvC.begin()+1, v9_gmvC.end());
    std::cout << "m_gemvC , " << v0_gmvC[(NUMBER_REPEATS+1)/2].count()
              << ", "         << v1_gmvC[(NUMBER_REPEATS+1)/2].count()
              << ", "         << v2_gmvC[(NUMBER_REPEATS+1)/2].count()
//              << ", "         << v3_gmvC[(NUMBER_REPEATS+1)/2].count()
//              << ", "         << v4_gmvC[(NUMBER_REPEATS+1)/2].count()
//              << ", "         << v5_gmvC[(NUMBER_REPEATS+1)/2].count()
//              << ", "         << v6_gmvC[(NUMBER_REPEATS+1)/2].count()
//              << ", "         << v7_gmvC[(NUMBER_REPEATS+1)/2].count()
//              << ", "         << v8_gmvC[(NUMBER_REPEATS+1)/2].count()
//              << ", "         << v9_gmvC[(NUMBER_REPEATS+1)/2].count()
              << std::endl;
    std::sort (v0_uppY.begin()+1, v0_uppY.end());
    std::sort (v1_uppY.begin()+1, v1_uppY.end());
    std::sort (v2_uppY.begin()+1, v2_uppY.end());
    std::cout << "m_uppY  , " << v0_uppY[(NUMBER_REPEATS+1)/2].count()
              << ", "         << v1_uppY[(NUMBER_REPEATS+1)/2].count()
              << ", "         << v2_uppY[(NUMBER_REPEATS+1)/2].count()
              << std::endl;
    std::sort (v0_lowY.begin()+1, v0_lowY.end());
    std::sort (v1_lowY.begin()+1, v1_lowY.end());
    std::sort (v2_lowY.begin()+1, v2_lowY.end());
    std::cout << "m_lowY  , " << v0_lowY[(NUMBER_REPEATS+1)/2].count()
              << ", "         << v1_lowY[(NUMBER_REPEATS+1)/2].count()
              << ", "         << v2_lowY[(NUMBER_REPEATS+1)/2].count()
              << std::endl;
    std::sort (v0_ger.begin()+1, v0_ger.end());
    std::sort (v1_ger.begin()+1, v1_ger.end());
    std::sort (v2_ger.begin()+1, v2_ger.end());
    std::cout << "m_ger   , " << v0_ger[(NUMBER_REPEATS+1)/2].count()
              << ", "         << v1_ger[(NUMBER_REPEATS+1)/2].count()
              << ", "         << v2_ger[(NUMBER_REPEATS+1)/2].count()
              << std::endl;

#endif

  std::cout << "GEMVR ANALYSYS!!" << std::endl;
  // ANALYSIS OF THE RESULTS
  for (int i=0; i<3; i++) {
    res = vR[i];
#ifdef SHOW_VALUES
//    std::cout << "( " << i+((i>4)?15:((i>0)?10:1)) << ") ";
    std::cout << "( " << i << ") ";
    std::cout << "VALUES!! --> res = " << res << " , addX = " << addX
              << " , err = " << addX - res << std::endl;
#endif  // VERBOSE
    if (std::abs((res - addX) / res) > ERROR_ALLOWED) {
      std::cout << "( " << i << ") ";
      std::cout << "ERROR!! --> res = " << res << " , addX = " << addX
                << " , err = " << addX - res << std::endl;
      returnVal += 1;
    }
  }

  std::cout << "GEMVC ANALYSYS!!" << std::endl;
  for (int i=0; i<3; i++) {
    res = vS[i];
  #ifdef SHOW_VALUES
//    std::cout << "( " << i+((i>2)?8:1) << ") ";
    std::cout << "( " << i << ") ";
    std::cout << "VALUES!! --> res = " << res << " , addY = " << addY
              << " , err = " << addY - res << std::endl;
  #endif  // VERBOSE
    if (std::abs((res - addY) / res) > ERROR_ALLOWED) {
      std::cout << "( " << i << ") ";
      std::cout << "ERROR!! --> res = " << res << " , addY = " << addY
                << " , err = " << addY - res << std::endl;
      returnVal += 2;
    }
  }

  std::cout << "UPPY ANALYSYS!!" << std::endl;
  for (int i=0; i<3; i++) {
    res = vU[i];
  #ifdef SHOW_VALUES
//    std::cout << "( " << i+((i>2)?8:1) << ") ";
    std::cout << "( " << i << ") ";
    std::cout << "VALUES!! --> res = " << res << " , uppY = " << uppY
              << " , diaY = " << diaY << " , untY = " << untY
              << " , err = " << (uppY+diaY) - res << std::endl;
//              << " , err = " << (uppY+untY) - res << std::endl;
  #endif  // VERBOSE
    if (std::abs((res - (uppY+diaY)) / res) > ERROR_ALLOWED) {
//    if (std::abs((res - (uppY+untY)) / res) > ERROR_ALLOWED) {
      std::cout << "( " << i << ") ";
      std::cout << "ERROR!! --> res = " << res << " , uppY = " << uppY
                << " , diaY = " << diaY << " , untY = " << untY
                << " , err = " << (uppY+diaY) - res << std::endl;
//                << " , err = " << (uppY+untY) - res << std::endl;
      returnVal += 2;
    }
  }

  std::cout << "LOWY ANALYSYS!!" << std::endl;
  for (int i=0; i<3; i++) {
    res = vL[i];
  #ifdef SHOW_VALUES
//    std::cout << "( " << i+((i>2)?8:1) << ") ";
    std::cout << "( " << i << ") ";
    std::cout << "VALUES!! --> res = " << res << " , lowY = " << lowY
              << " , diaY = " << diaY << " , untY = " << untY
              << " , err = " << (lowY+untY) - res << std::endl;
//              << " , err = " << (lowY+diaY) - res << std::endl;
  #endif  // VERBOSE
//    if (std::abs((res - (lowY+diaY)) / res) > ERROR_ALLOWED) {
    if (std::abs((res - (lowY+untY)) / res) > ERROR_ALLOWED) {
      std::cout << "( " << i << ") ";
      std::cout << "ERROR!! --> res = " << res << " , lowY = " << lowY
                << " , diaY = " << diaY << " , untY = " << untY
                << " , err = " << (lowY+untY) - res << std::endl;
//                << " , err = " << (lowY+diaY) - res << std::endl;
      returnVal += 2;
    }
  }

  std::cout << "GER ANALYSYS!!" << std::endl;
  for (int i=0; i<3; i++) {
    res = vT[i];
  #ifdef SHOW_VALUES
//    std::cout << "( " << i+((i>0)?10:1) << ") ";
    std::cout << "( " << i << ") ";
    std::cout << "VALUES!! --> res = " << res << " , addRng1 = " << addRng1
              << " , err = " << addRng1 - res << std::endl;
  #endif  // VERBOSE
    if (std::abs((res - addRng1) / res) > ERROR_ALLOWED) {
      std::cout << "( " << i << ") ";
      std::cout << "ERROR!! --> res = " << res << " , addRng1 = " << addRng1
                << " , err = " << addRng1 - res << std::endl;
      returnVal += 2;
    }
  }

  return returnVal;
}


int main(int argc, char *argv[]) {
  //  using namespace SyclBlas;
  bool accessDev = DEFAULT_ACCESS;
  size_t sizeV = 0, divSz = 1, shftR = 0, shftC = 0;
  size_t returnVal = 0;

  if (argc == 1) {
    sizeV = DEF_SIZE_VECT;
  } else if (argc == 2) {
    if (atoi(argv[1]) < 0) {
      sizeV = -atoi(argv[1]);
      accessDev = !DEFAULT_ACCESS;
    } else
      sizeV = atoi(argv[1]);
  } else if (argc == 3) {
    if (atoi(argv[1]) < 0) {
      sizeV = -atoi(argv[1]);
      accessDev = !DEFAULT_ACCESS;
    } else
      sizeV = atoi(argv[1]);
    divSz = atoi(argv[2]);
    ;
  } else if (argc == 4) {
    if (atoi(argv[1]) < 0) {
      sizeV = -atoi(argv[1]);
      //      accessDev = false;
      accessDev = !DEFAULT_ACCESS;
    } else
      sizeV = atoi(argv[1]);
    shftR = atoi(argv[2]);
    shftC = atoi(argv[3]);
  } else if (argc == 5) {
    if (atoi(argv[1]) < 0) {
      sizeV = -atoi(argv[1]);
      //      accessDev = false;
      accessDev = !DEFAULT_ACCESS;
    } else
      sizeV = atoi(argv[1]);
    divSz = atoi(argv[2]);
    ;
    shftR = atoi(argv[3]);
    shftC = atoi(argv[4]);
  } else {
    std::cout << "ERROR!! --> Incorrect number of input parameters"
              << std::endl;
    returnVal = 1;
  }
/* */
  if (returnVal == 0) {
//    returnVal = 2 * TestingBLAS2(accessDev, sizeV, divSz, shftR, shftC);
    returnVal  = 2 * TestingBLAS2(DEFAULT_ACCESS, sizeV, divSz, shftR, shftC);
    returnVal += 4 * TestingBLAS2_New(accessDev, sizeV, divSz, shftR, shftC);
  }

  return returnVal;
}
