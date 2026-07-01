if(NOT DEFINED GREENFLAME_EXPECTED_PRODUCT_VERSION)
    message(FATAL_ERROR "GREENFLAME_EXPECTED_PRODUCT_VERSION is required")
endif()
if(NOT DEFINED GREENFLAME_VERSION_HEADER)
    message(FATAL_ERROR "GREENFLAME_VERSION_HEADER is required")
endif()
if(NOT DEFINED GREENFLAME_RC_FILE)
    message(FATAL_ERROR "GREENFLAME_RC_FILE is required")
endif()

if(NOT EXISTS "${GREENFLAME_VERSION_HEADER}")
    message(FATAL_ERROR "Missing version header: ${GREENFLAME_VERSION_HEADER}")
endif()
if(NOT EXISTS "${GREENFLAME_RC_FILE}")
    message(FATAL_ERROR "Missing resource file: ${GREENFLAME_RC_FILE}")
endif()

file(READ "${GREENFLAME_VERSION_HEADER}" version_header)
file(READ "${GREENFLAME_RC_FILE}" resource_file)

set(expected_header
    "#define GREENFLAME_PRODUCT_VERSION_WIDE L\"${GREENFLAME_EXPECTED_PRODUCT_VERSION}\""
)
string(FIND "${version_header}" "${expected_header}" header_found_at)
if(header_found_at EQUAL -1)
    message(FATAL_ERROR
        "version_string.h does not contain expected product version "
        "'${GREENFLAME_EXPECTED_PRODUCT_VERSION}'"
    )
endif()

set(expected_resource
    "VALUE \"ProductVersion\", \"${GREENFLAME_EXPECTED_PRODUCT_VERSION}\\0\""
)
string(FIND "${resource_file}" "${expected_resource}" resource_found_at)
if(resource_found_at EQUAL -1)
    message(FATAL_ERROR
        "greenflame.rc does not contain expected product version "
        "'${GREENFLAME_EXPECTED_PRODUCT_VERSION}'"
    )
endif()
