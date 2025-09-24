# ---- Library ----
add_library(Obfuscator SHARED)




# --- Sources ---
target_sources(Obfuscator PRIVATE
    "src/Main.cpp"
   )


target_compile_definitions(Obfuscator PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

# --- Headers ---
target_include_directories(Obfuscator PUBLIC
   "Include" 
)

find_package(Python3 REQUIRED)

  # Paths
  set(OBFUSCATE_SCRIPT "${CMAKE_SOURCE_DIR}/External/Script/obfuscate.py")
  set(OBFUSCATION_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")  # <- only the Warden folder

  if(NOT EXISTS "${OBFUSCATE_SCRIPT}")
    message(FATAL_ERROR "OBFUSCATE_SCRIPT not found: ${OBFUSCATE_SCRIPT}")
  endif()

  # Stamps & backup dir
  set(SOURCE_BACKUP_DIR "${CMAKE_BINARY_DIR}/source_backup")
  set(BACKUP_STAMP     "${SOURCE_BACKUP_DIR}/.backup.stamp")
  set(OBFUSCATE_STAMP  "${CMAKE_BINARY_DIR}/.obfuscate.stamp")

  # Helper to clean .bak after restore (limit to Warden tree)
  file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/cmake")
  set(CLEAN_BAK_SCRIPT "${CMAKE_BINARY_DIR}/cmake/clean_bak.cmake")
  file(WRITE "${CLEAN_BAK_SCRIPT}" "file(GLOB_RECURSE _baks \"${CMAKE_CURRENT_SOURCE_DIR}/*.bak\")
    if(_baks)
      foreach(f IN LISTS _baks)
        file(REMOVE \"\${f}\")
      endforeach()
    endif()
    ")

  # 1) Backup originals (only copy what exists)
  add_custom_command(
    OUTPUT "${BACKUP_STAMP}"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${SOURCE_BACKUP_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SOURCE_BACKUP_DIR}"
    # src
    COMMAND ${CMAKE_COMMAND} -E echo "Backing up: ${CMAKE_CURRENT_SOURCE_DIR}/src"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/src" "${SOURCE_BACKUP_DIR}/src"
    # Include (copy only if it exists in Warden)
    COMMAND ${CMAKE_COMMAND} -E echo "Backing up: ${CMAKE_CURRENT_SOURCE_DIR}/Include (if present)"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/Include" "${SOURCE_BACKUP_DIR}/Include"
    COMMAND ${CMAKE_COMMAND} -E touch "${BACKUP_STAMP}"
    COMMENT "Backing up original Warden source files"
    VERBATIM
  )
  add_custom_target(backup_sources DEPENDS "${BACKUP_STAMP}")

  # 2) Run obfuscator (your script: std::cout/printf-only wrapping)
  add_custom_command(
    OUTPUT "${OBFUSCATE_STAMP}"
    COMMAND ${Python3_EXECUTABLE} "${OBFUSCATE_SCRIPT}"
            "${OBFUSCATION_ROOT}"
            --write
            --whitelist src Include
            --exclude "src/hmac" "src/SHA"
    COMMAND ${CMAKE_COMMAND} -E touch "${OBFUSCATE_STAMP}"
    DEPENDS "${BACKUP_STAMP}"
    WORKING_DIRECTORY "${OBFUSCATION_ROOT}"
    COMMENT "Applying junk code obfuscation (std::cout/printf-only) in Warden/"
    VERBATIM
  )
  add_custom_target(obfuscate_sources DEPENDS "${OBFUSCATE_STAMP}")
  add_dependencies(Obfuscator obfuscate_sources)


  # 3) Restore originals after linking (you still get an obfuscated DLL)
  add_custom_command(TARGET Obfuscator POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "Restoring original Warden source files"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${SOURCE_BACKUP_DIR}/src"     "${CMAKE_CURRENT_SOURCE_DIR}/src"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${SOURCE_BACKUP_DIR}/Include" "${CMAKE_CURRENT_SOURCE_DIR}/Include"
    COMMAND ${CMAKE_COMMAND} -P "${CLEAN_BAK_SCRIPT}"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${SOURCE_BACKUP_DIR}"
    COMMAND ${CMAKE_COMMAND} -E rm -f "${OBFUSCATE_STAMP}"
    COMMENT "Restored originals and cleaned *.bak"
    VERBATIM
  )
