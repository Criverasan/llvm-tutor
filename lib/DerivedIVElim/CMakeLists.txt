cmake_minimum_required(VERSION 3.13)
project(DerivedIVElim)

# Build the plugin as a MODULE (bundle on macOS)
add_library(DerivedIVElim MODULE
  DerivedInductionVarTransform.cpp
)

set_target_properties(DerivedIVElim PROPERTIES
  CXX_STANDARD 17
  PREFIX ""                 # produce "DerivedIVElim.so" not "libDerivedIVElim.so"
)

# Include LLVM headers
target_include_directories(DerivedIVElim PRIVATE ${LLVM_INCLUDE_DIRS})
target_compile_definitions(DerivedIVElim PRIVATE ${LLVM_DEFINITIONS})

# On Darwin, DON'T hard-link libLLVM*.a — let the loader resolve symbols at runtime.
# This avoids duplicate/undefined symbol headaches.
target_link_libraries(DerivedIVElim
  "$<$<PLATFORM_ID:Darwin>:-undefined dynamic_lookup>"
)

