cmake_minimum_required(VERSION 3.15)

project(oj_platform LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(OJ_PLATFORM_BUILD_TESTS "Build oj_platform tests" OFF)

find_package(PkgConfig REQUIRED)
find_package(hiredis REQUIRED)
find_package(redis++ REQUIRED)
find_package(OpenSSL REQUIRED)
find_path(MYSQLCPP_INCLUDE_DIR cppconn/connection.h)
find_library(MYSQLCPP_LIBRARY NAMES mysqlcppconn mysqlcppconn8)

if(NOT MYSQLCPP_INCLUDE_DIR OR NOT MYSQLCPP_LIBRARY)
    message(FATAL_ERROR "MySQL Connector/C++ not found. Please install mysqlcppconn development package.")
endif()

add_subdirectory(third_party/crow)

add_library(oj_judge_core
    common/path_utils.cpp
    services/judge_worker/judge_core.cpp
    services/judge_worker/compile_service.cpp
    services/judge_worker/run_service.cpp
)

target_include_directories(oj_judge_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(oj_server
    services/oj_server/main.cpp
    services/oj_server/routes.cpp
    services/oj_server/auth_service.cpp
    services/oj_server/judge_service.cpp
    services/oj_server/problem_repository.cpp
    services/oj_server/mysql_client.cpp
    services/oj_server/redis_client.cpp
)

target_include_directories(oj_server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${MYSQLCPP_INCLUDE_DIR})
target_link_libraries(oj_server PRIVATE Crow::Crow oj_judge_core redis++::redis++ hiredis::hiredis OpenSSL::Crypto crypt ${MYSQLCPP_LIBRARY})

add_executable(judge_worker
    services/judge_worker/main.cpp
    services/judge_worker/routes.cpp
)

target_include_directories(judge_worker PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(judge_worker PRIVATE Crow::Crow oj_judge_core)

add_executable(judge_dispatcher
    services/judge_dispatcher/main.cpp
    services/judge_dispatcher/worker_client.cpp
    services/oj_server/problem_repository.cpp
    services/oj_server/mysql_client.cpp
    services/oj_server/redis_client.cpp
)

target_include_directories(judge_dispatcher PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${MYSQLCPP_INCLUDE_DIR})
target_link_libraries(judge_dispatcher PRIVATE Crow::Crow oj_judge_core redis++::redis++ hiredis::hiredis ${MYSQLCPP_LIBRARY})

add_executable(problem_migrator
    tools/problem_migrator.cpp
    common/path_utils.cpp
    services/oj_server/mysql_client.cpp
)

target_include_directories(problem_migrator PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${MYSQLCPP_INCLUDE_DIR})
target_link_libraries(problem_migrator PRIVATE Crow::Crow ${MYSQLCPP_LIBRARY})
