cmake_minimum_required(VERSION 3.20)
project(ObfuscatorProj LANGUAGES CXX)

# ---- Library ----
add_library(Obfuscator SHARED)

# --- Sources ---
target_sources(Obfuscator PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src/Main.cpp"
)

target_compile_definitions(Obfuscator PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

# --- Headers ---
target_include_directories(Obfuscator PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/Include"
)

# ---- Python + scripts ----
# Need a Python interpreter for obfuscation + hashing
find_package(Python3 COMPONENTS Interpreter REQUIRED)

# Paths (repo-root scripts assumed here)
set(OBFUSCATE_SCRIPT "${CMAKE_SOURCE_DIR}/External/Script/obfuscate.py")
set(HASH_SCRIPT      "${CMAKE_SOURCE_DIR}/External/Script/hashdll.py")

# Only the Obfuscator folder is transformed
set(OBFUSCATION_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")

if(NOT EXISTS "${OBFUSCATE_SCRIPT}")
  message(FATAL_ERROR "OBFUSCATE_SCRIPT not found: ${OBFUSCATE_SCRIPT}")
endif()
if(NOT EXISTS "${HASH_SCRIPT}")
  message(FATAL_ERROR "HASH_SCRIPT not found: ${HASH_SCRIPT}")
endif()

# ---- Stamps & backup dir ----
set(SOURCE_BACKUP_DIR "${CMAKE_BINARY_DIR}/source_backup")
set(BACKUP_STAMP      "${SOURCE_BACKUP_DIR}/.backup.stamp")
set(OBFUSCATE_STAMP   "${CMAKE_BINARY_DIR}/.obfuscate.stamp")

# ---- Helper to clean *.bak only under Obfuscator tree ----
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/cmake")
set(CLEAN_BAK_SCRIPT "${CMAKE_BINARY_DIR}/cmake/clean_bak.cmake")
file(WRITE "${CLEAN_BAK_SCRIPT}"
"file(GLOB_RECURSE _baks \"${CMAKE_CURRENT_SOURCE_DIR}/*.bak\")
if(_baks)
  foreach(f IN LISTS _baks)
    file(REMOVE \"\${f}\")
  endforeach()
endif()
")

# ---- 1) Backup originals ----
add_custom_command(
  OUTPUT "${BACKUP_STAMP}"
  COMMAND ${CMAKE_COMMAND} -E remove_directory "${SOURCE_BACKUP_DIR}"
  COMMAND ${CMAKE_COMMAND} -E make_directory    "${SOURCE_BACKUP_DIR}"
  # src
  COMMAND ${CMAKE_COMMAND} -E echo "Backing up: ${CMAKE_CURRENT_SOURCE_DIR}/src"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/src"     "${SOURCE_BACKUP_DIR}/src"
  # Include
  COMMAND ${CMAKE_COMMAND} -E echo "Backing up: ${CMAKE_CURRENT_SOURCE_DIR}/Include"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/Include" "${SOURCE_BACKUP_DIR}/Include"
  COMMAND ${CMAKE_COMMAND} -E touch "${BACKUP_STAMP}"
  COMMENT "Backing up original Obfuscator source files"
  VERBATIM
)
add_custom_target(backup_sources DEPENDS "${BACKUP_STAMP}")

# ---- 2) Run obfuscator (writes in-place) ----
add_custom_command(
  OUTPUT "${OBFUSCATE_STAMP}"
  COMMAND "${Python3_EXECUTABLE}" "${OBFUSCATE_SCRIPT}"
          "${OBFUSCATION_ROOT}"
          --write
          --whitelist src Include
          --exclude "src/hmac" "src/SHA"
  COMMAND ${CMAKE_COMMAND} -E touch "${OBFUSCATE_STAMP}"
  DEPENDS "${BACKUP_STAMP}"
  WORKING_DIRECTORY "${OBFUSCATION_ROOT}"
  COMMENT "Applying junk code obfuscation (std::cout/printf-only) in Obfuscator/"
  VERBATIM
)
add_custom_target(obfuscate_sources DEPENDS "${OBFUSCATE_STAMP}")
add_dependencies(Obfuscator obfuscate_sources)

# ---- 3) Restore originals after linking, then hash the DLL ----
add_custom_command(TARGET Obfuscator POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E echo "Restoring original obfuscated source files"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${SOURCE_BACKUP_DIR}/src"     "${CMAKE_CURRENT_SOURCE_DIR}/src"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${SOURCE_BACKUP_DIR}/Include" "${CMAKE_CURRENT_SOURCE_DIR}/Include"
  COMMAND ${CMAKE_COMMAND} -P "${CLEAN_BAK_SCRIPT}"
  COMMAND ${CMAKE_COMMAND} -E remove_directory "${SOURCE_BACKUP_DIR}"
  COMMAND ${CMAKE_COMMAND} -E rm -f "${OBFUSCATE_STAMP}"

  # --- print hashes of the built binary (choose ONE style) ---
  # Compact one-liner:
  COMMAND ${CMAKE_COMMAND} -E echo "Hashing artifact: $<TARGET_FILE:Obfuscator>"
  COMMAND "${Python3_EXECUTABLE}" "${HASH_SCRIPT}" "$<TARGET_FILE:Obfuscator>"

  # Pretty multi-line (use instead of the two lines above):
  # COMMAND ${CMAKE_COMMAND} -E echo "Hashing artifact: $<TARGET_FILE:Obfuscator>"
  # COMMAND "${Python3_EXECUTABLE}" "${HASH_SCRIPT}" --pretty "$<TARGET_FILE:Obfuscator>"

  COMMENT "Restored originals, cleaned *.bak, and printed build artifact hashes"
  VERBATIM
)
