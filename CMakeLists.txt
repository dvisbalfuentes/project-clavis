cmake_minimum_required(VERSION 3.20)
project(clavis_signal_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(clavis_ingest src/engine/ingest.cpp)
target_include_directories(clavis_ingest PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_library(clavis_qrs src/engine/pan_tompkins.cpp)
target_include_directories(clavis_qrs PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_library(clavis_classifier src/engine/classifier.cpp)
target_include_directories(clavis_classifier PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_executable(ecg_playback apps/ecg_playback.cpp)
target_link_libraries(ecg_playback PRIVATE clavis_ingest clavis_qrs clavis_classifier)

find_package(OpenSSL REQUIRED)

add_library(clavis_crypto src/crypto/envelope.cpp src/protocol/replay.cpp)
target_include_directories(clavis_crypto PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(clavis_crypto PRIVATE OpenSSL::SSL OpenSSL::Crypto)

add_executable(device_gateway apps/device_gateway.cpp)
target_link_libraries(device_gateway PRIVATE 
    clavis_ingest 
    clavis_qrs 
    clavis_classifier 
    clavis_crypto
)

include(CTest)
if(BUILD_TESTING)
  add_executable(ingest_test tests/ingest_test.cpp)
  target_link_libraries(ingest_test PRIVATE clavis_ingest)
  add_test(NAME ingest_test COMMAND ingest_test)

  add_executable(pan_tompkins_test tests/pan_tompkins_test.cpp)
  target_link_libraries(pan_tompkins_test PRIVATE clavis_qrs)
  add_test(NAME pan_tompkins_test COMMAND pan_tompkins_test)

  add_executable(classifier_test tests/classifier_test.cpp)
  target_link_libraries(classifier_test PRIVATE clavis_classifier)
  add_test(NAME classifier_test COMMAND classifier_test)
endif()
