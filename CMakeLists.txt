cmake_minimum_required(VERSION 3.16)
project(transit_polling C)
set(CMAKE_C_STANDARD 99)
if(NOT WIN32)
  add_compile_definitions(_POSIX_C_SOURCE=200809L)
endif()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")
if(WIN32)
  include(FetchContent)
  set(FETCHCONTENT_BASE_DIR "${CMAKE_SOURCE_DIR}/.deps")
  FetchContent_Declare(libnfc GIT_REPOSITORY https://github.com/nfc-tools/libnfc.git
    GIT_TAG 3fa0751ad58fb0053d3de2e61bbbd4066259104c
    BINARY_DIR "${CMAKE_BINARY_DIR}/libnfc"
    UPDATE_DISCONNECTED TRUE)
  set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(BUILD_UTILS OFF CACHE BOOL "" FORCE)
  foreach(driver ACR122_USB ACR122_PCSC PCSC ACR122S ARYGON PN53X_USB PN532_I2C PN532_SPI)
    set(LIBNFC_DRIVER_${driver} OFF CACHE BOOL "" FORCE)
  endforeach()
  set(LIBNFC_DRIVER_PN532_UART ON CACHE BOOL "" FORCE)
  set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
  FetchContent_MakeAvailable(libnfc)
  # Upstream's environment shim uses char *str[32] as a character buffer.
  # GCC 14+ rejects it; replace it with the CRT implementation, which also
  # avoids the fixed buffer size and incorrect overwrite=0 return value.
  get_target_property(nfc_sources nfc SOURCES)
  list(FILTER nfc_sources EXCLUDE REGEX "contrib/win32/stdlib\\.c$")
  set_property(TARGET nfc PROPERTY SOURCES "${nfc_sources}")
  target_sources(nfc PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/common/nfc_windows_env.c")
  # MinGW already produces an import library; the upstream MSVC helper assumes
  # a different DLL output directory and is not needed by these executables.
  set_target_properties(win32lib PROPERTIES EXCLUDE_FROM_ALL TRUE)
  set(NFC_INCLUDES "${libnfc_SOURCE_DIR}/include" "${libnfc_BINARY_DIR}/include")
  set(NFC_LIBRARIES nfc)
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(NFC REQUIRED IMPORTED_TARGET libnfc)
  set(NFC_LIBRARIES PkgConfig::NFC)
endif()
foreach(program express_poll felica_poll octopus_balance probe_system suica_balance)
  add_executable(${program} src/${program}.c src/common/platform.c)
  target_include_directories(${program} PRIVATE ${NFC_INCLUDES})
  target_link_libraries(${program} PRIVATE ${NFC_LIBRARIES})
endforeach()
