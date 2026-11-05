cmake_minimum_required(VERSION 3.20)
project(procedural_synth_tests LANGUAGES CXX)
enable_testing()
add_executable(synth-tests test.cpp ../../apps/openmw/mwsound/proceduralsynth.cpp ../../apps/openmw/mwsound/proceduralstudio.cpp)
target_compile_features(synth-tests PRIVATE cxx_std_17)
if(MSVC)
  target_compile_options(synth-tests PRIVATE /W4 /EHsc)
  # This harness deliberately keeps many complete instruments on its stack.
  # Production creates them on the control-thread heap. Match the usual Unix
  # test-stack budget instead of Windows' small 1-MiB executable default.
  target_link_options(synth-tests PRIVATE /STACK:8388608)
endif()
add_test(NAME procedural-synth COMMAND synth-tests)
add_executable(acoustics-tests acoustics-test.cpp)
target_compile_features(acoustics-tests PRIVATE cxx_std_17)
add_test(NAME instrument-acoustics COMMAND acoustics-tests)
add_executable(transient-tests transient-tests.cpp)
target_compile_features(transient-tests PRIVATE cxx_std_17)
add_test(NAME sample-transients COMMAND transient-tests)
add_executable(captured-amp-tests captured-amp-test.cpp)
target_compile_features(captured-amp-tests PRIVATE cxx_std_17)
add_test(NAME captured-amp COMMAND captured-amp-tests)
add_executable(captured-amp-scalar-tests captured-amp-test.cpp)
target_compile_features(captured-amp-scalar-tests PRIVATE cxx_std_17)
target_compile_definitions(captured-amp-scalar-tests PRIVATE OPENMW_CAPTURED_AMP_SCALAR=1)
add_test(NAME captured-amp-scalar COMMAND captured-amp-scalar-tests)
add_executable(bus-tests bus-tests.cpp ../../apps/openmw/mwsound/proceduralsynth.cpp ../../apps/openmw/mwsound/proceduralstudio.cpp)
target_compile_features(bus-tests PRIVATE cxx_std_17)
add_test(NAME member-master-buses COMMAND bus-tests)
add_executable(release-tests release-tests.cpp ../../apps/openmw/mwsound/proceduralsynth.cpp ../../apps/openmw/mwsound/proceduralstudio.cpp)
target_compile_features(release-tests PRIVATE cxx_std_17)
add_test(NAME recorded-release COMMAND release-tests)
add_executable(sample-bank-tool bank-tool.cpp ../../apps/openmw/mwsound/proceduralsynth.cpp
  ../../components/files/hash.cpp ../../extern/smhasher/MurmurHash3.cpp)
target_compile_features(sample-bank-tool PRIVATE cxx_std_17)
target_include_directories(sample-bank-tool PRIVATE ../../extern/smhasher/include ../../extern/smhasher/include/smhasher)
add_executable(score-audition score-audition.cpp ../../apps/openmw/mwsound/proceduralsynth.cpp
  ../../apps/openmw/mwsound/proceduralstudio.cpp ../../components/files/hash.cpp ../../extern/smhasher/MurmurHash3.cpp)
target_compile_features(score-audition PRIVATE cxx_std_17)
target_include_directories(score-audition PRIVATE ../../extern/smhasher/include ../../extern/smhasher/include/smhasher)

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  add_test(NAME recorded-bass-attacks COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/test-bass-attack-banks.py" "$<TARGET_FILE:sample-bank-tool>")
  add_test(NAME recorded-slap-pop COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/test-slap-bass-banks.py" "$<TARGET_FILE:sample-bank-tool>")
  add_test(NAME recorded-guitar-harmonics COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/test-guitar-harmonic-banks.py" "$<TARGET_FILE:sample-bank-tool>")
  add_test(NAME recorded-string-gestures COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/test-gesture-banks.py" "$<TARGET_FILE:sample-bank-tool>")
  add_test(NAME score-audition-config COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/test-score-audition.py" "$<TARGET_FILE:score-audition>")
endif()
