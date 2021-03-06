if(BLAS_VERIFY_BENCHMARK)
  find_package(SystemBLAS REQUIRED)
endif()

find_package(SB_CLBlast REQUIRED)

set(sources
  # Level 1 blas
  blas1/asum.cpp
  blas1/axpy.cpp
  blas1/dot.cpp
  blas1/iamax.cpp
  blas1/iamin.cpp
  blas1/nrm2.cpp
  blas1/scal.cpp
  # Level 2 blas
  blas2/gemv.cpp
  # Level 3 blas
  blas3/gemm.cpp
  blas3/gemm_batched.cpp
  blas3/trsm.cpp
)

# Add individual benchmarks for each method
foreach(clblast_benchmark ${sources})
  get_filename_component(clblast_bench_exec ${clblast_benchmark} NAME_WE)
  add_executable(bench_clblast_${clblast_bench_exec} ${clblast_benchmark} main.cpp)
  target_link_libraries(bench_clblast_${clblast_bench_exec} PRIVATE benchmark Clara::Clara clblast sycl_blas_common)
  target_include_directories(bench_clblast_${clblast_bench_exec} PRIVATE ${CBLAS_INCLUDE})

  if(BLAS_VERIFY_BENCHMARK)
    target_link_libraries(bench_clblast_${clblast_bench_exec} PRIVATE blas::blas)
    target_compile_definitions(bench_clblast_${clblast_bench_exec} PRIVATE BLAS_VERIFY_BENCHMARK)
  endif()

  message(STATUS "Created clBLAST benchmark: ${clblast_bench_exec}")
  install(TARGETS bench_clblast_${clblast_bench_exec}
    RUNTIME
      DESTINATION ${CMAKE_INSTALL_BINDIR}
      COMPONENT benchmarks
  )
endforeach()
