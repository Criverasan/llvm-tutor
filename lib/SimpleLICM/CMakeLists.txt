cmake_minimum_required(VERSION 3.13)
project(SimpleLICM)
add_library(SimpleLICM MODULE SimpleLICM.cpp)
set_target_properties(SimpleLICM PROPERTIES CXX_STANDARD 17 PREFIX "")
target_link_libraries(SimpleLICM PRIVATE LLVMCore LLVMIRReader LLVMSupport LLVMAnalysis LLVMTransformUtils)
target_include_directories(SimpleLICM PRIVATE ${LLVM_INCLUDE_DIRS})
target_compile_definitions(SimpleLICM PRIVATE ${LLVM_DEFINITIONS})

