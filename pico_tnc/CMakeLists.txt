set(SECP256K1_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SECP256K1_BUILD_EXHAUSTIVE_TESTS OFF CACHE BOOL "" FORCE)
set(SECP256K1_BUILD_CTIME_TESTS OFF CACHE BOOL "" FORCE)
set(SECP256K1_BUILD_BENCHMARK OFF CACHE BOOL "" FORCE)
set(SECP256K1_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SECP256K1_ENABLE_MODULE_RECOVERY ON CACHE BOOL "" FORCE)
set(SECP256K1_ENABLE_MODULE_ECDH OFF CACHE BOOL "" FORCE)
set(SECP256K1_ENABLE_MODULE_EXTRAKEYS OFF CACHE BOOL "" FORCE)
set(SECP256K1_ENABLE_MODULE_SCHNORRSIG OFF CACHE BOOL "" FORCE)
set(SECP256K1_ENABLE_MODULE_MUSIG OFF CACHE BOOL "" FORCE)
set(SECP256K1_ENABLE_MODULE_ELLSWIFT OFF CACHE BOOL "" FORCE)
add_subdirectory(secp256k1 EXCLUDE_FROM_ALL)

add_executable(pico_tnc
        main.c
        tnc.c
        send.c
        receive.c
        decode.c
        qsl_card.c
        filter.c
        bell202.c
        ax25.c
        usb_input.c
        usb_output.c
        cmd.c
        help.c
        serial.c
        tty.c
        flash.c
        gps.c
        unproto.c
        digipeat.c
        beacon.c
        kiss.c
        sjis_level1_table.c
        mona_backend_minimal.c
        libmona_pico/mona_compat.c
        libmona_pico/mona_pico_api.c
        )

pico_generate_pio_header(pico_tnc ${CMAKE_CURRENT_LIST_DIR}/pio_dac.pio)

target_include_directories(pico_tnc PRIVATE
        include
)

target_link_libraries(pico_tnc
		pico_stdlib
        #pico_stdio_uart
        #pico_stdio_usb
		hardware_adc
		hardware_dma
        hardware_pwm
        hardware_uart
		# For the dummy output:
		hardware_pio
        secp256k1
		#pico_multicore
		)

pico_enable_stdio_usb(pico_tnc 1)
pico_enable_stdio_uart(pico_tnc 1)

# create map/bin/hex file etc.
pico_add_extra_outputs(pico_tnc)

# add url via pico_set_program_url
example_auto_set_url(pico_tnc)
