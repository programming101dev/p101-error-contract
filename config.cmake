set(PROJECT_NAME "p101-error-contract")
set(PROJECT_VERSION "1.0.0")
set(PROJECT_DESCRIPTION "Programming 101 p101 error handling contract checker")
set(PROJECT_LANGUAGE "C")

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(STANDARD_FLAGS
        -D_POSIX_C_SOURCE=200809L
        -D_XOPEN_SOURCE=700
        -Werror
)

set(DARWIN_STANDARD_FLAGS
        -D_DARWIN_C_SOURCE
)

set(LINUX_STANDARD_FLAGS
)

set(BSD_STANDARD_FLAGS
)

set(EXECUTABLE_TARGETS main)
set(LIBRARY_TARGETS "")
set(main_OUTPUT_NAME p101-error-contract)

set(main_SOURCES
        src/cli.c
        src/contract.c
        src/contract_builder.c
        src/contract_model.c
        src/main.c
        src/native_analysis.c
        src/report.c
)

set(main_HEADERS
        include/arguments.h
        include/cli.h
        include/constants.h
        include/contract.h
        include/contract_builder.h
        include/contract_model.h
        include/contract_types.h
        include/errors.h
        include/native_analysis.h
        include/report.h
)

set(main_LINK_LIBRARIES
        p101_error
        p101_env
        p101_record
        p101_c
        p101_c_facts
        p101_cli
        p101_filesystem
        p101_io
        p101_process
        p101_convert
        m
)
