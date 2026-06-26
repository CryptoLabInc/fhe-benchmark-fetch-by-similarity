# Copyright (C) 2026 CryptoLab, Inc.
# All rights reserved.
#
# This source code is protected under international copyright law.  All rights
# reserved and protected by the copyright holders.

add_library(externals INTERFACE)
target_link_libraries(externals INTERFACE OpenMP::OpenMP_CXX
                                          nlohmann_json::nlohmann_json)

add_executable(task-keygen task-keygen.cpp)
target_link_libraries(task-keygen PRIVATE juvia::juvia externals)

add_executable(task-encrypt-query task-encrypt-query.cpp)
target_link_libraries(task-encrypt-query PRIVATE juvia::juvia externals)

add_executable(task-encrypt-data task-encrypt-data.cpp)
target_link_libraries(task-encrypt-data PRIVATE juvia::juvia externals)

add_executable(task1-count-matches task1-count-matches.cpp)
target_link_libraries(task1-count-matches PRIVATE juvia::juvia externals)

add_executable(task1-decrypt task1-decrypt.cpp)
target_link_libraries(task1-decrypt PRIVATE juvia::juvia externals)

add_executable(task2-return-k-matches task2-return-k-matches.cpp)
target_link_libraries(task2-return-k-matches PRIVATE juvia::juvia externals)

add_executable(task2-decrypt task2-decrypt.cpp)
target_link_libraries(task2-decrypt PRIVATE juvia::juvia externals)
