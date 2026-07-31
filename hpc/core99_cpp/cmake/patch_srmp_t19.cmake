# T19 SRMP v1.01 modern-C++ safety patch.
#
# The pinned archive's Energy destructor deletes FactorType-derived objects
# through FactorType*, but FactorType has no virtual destructor. GCC/ASAN
# correctly diagnoses a new-delete-type-mismatch. Add the missing empty
# virtual destructor only; no solver arithmetic or data structure changes.
if(NOT DEFINED SRMP_SOURCE_DIR)
    message(FATAL_ERROR "SRMP_SOURCE_DIR is required")
endif()
set(header "${SRMP_SOURCE_DIR}/SRMP.h")
file(READ "${header}" content)
if(content MATCHES "virtual ~FactorType\\(\\)")
    return()
endif()
set(needle "\tstruct FactorType\n\t{")
set(replacement "\tstruct FactorType\n\t{\n\t\tvirtual ~FactorType() {}")
string(REPLACE "${needle}" "${replacement}" patched "${content}")
if(patched STREQUAL content)
    message(FATAL_ERROR "Pinned SRMP.h FactorType signature changed")
endif()
file(WRITE "${header}" "${patched}")
