cmake_minimum_required(VERSION 3.4)

project(BronyaObfus)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /utf-8 /MD /std:c++latest")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /utf-8 /MDd /std:c++latest")

set(LLVM_BUILD_DIR "E:/Code/windows/Arkari/build")
set(LLVM_SRC_DIR "E:/Code/windows/Arkari/llvm")

aux_source_directory(./src src)
set(srcs ${src})

add_library(BronyaObfus SHARED ${srcs} export.def)

message("LLVM_BUILD_DIR: ${LLVM_BUILD_DIR}")
message("LLVM_SRC_DIR: ${LLVM_SRC_DIR}")

include_directories("${LLVM_BUILD_DIR}/include")
include_directories("${LLVM_SRC_DIR}/include")
include_directories("${PROJECT_SOURCE_DIR}/include")

set(mylibdir "${LLVM_BUILD_DIR}/Release/lib")

file(GLOB LIBS "${mylibdir}/LLVM*.lib")

set_target_properties(BronyaObfus PROPERTIES LINKER_LANGUAGE CXX)

target_link_libraries(BronyaObfus PUBLIC ${LIBS})