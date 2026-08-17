cmake_minimum_required(VERSION 3.15)
project(solver VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS "-g")

set(SRC_FILES 
    "src/main.cpp"
)

set(TSEITIN_SRC
    "src/formula2cnf.cpp"
)

set (DPLL_SRC
    "src/dpll_main.cpp"
)

add_executable(formula2cnf ${TSEITIN_SRC})

add_executable(dpll ${DPLL_SRC})

# add_executable(solver ${SRC_FILES})

target_include_directories(formula2cnf PRIVATE
    ${PROJECT_SOURCE_DIR}/include    
)

target_include_directories(dpll PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

# target_include_directories(solver PRIVATE
#     ${PROJECT_SOURCE_DIR}/include
# )