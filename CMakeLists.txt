cmake_minimum_required(VERSION 3.27)
project(ffpack)

set(FFPACK_EXE ${PROJECT_NAME})

add_executable(${FFPACK_EXE})
target_compile_features(${FFPACK_EXE} PUBLIC
    c_std_11
)

add_subdirectory(src)
