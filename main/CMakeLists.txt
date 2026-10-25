set(SOURCES
    "main.cpp"
    "config/config_loader.cpp"
    "led/led.cpp"
    "logger/logger.cpp"
    "network/dns_server.cpp"
    "network/tcp_client.cpp"
    "network/webserver.cpp"
    "network/wifi_manager.cpp"
    "nfc/nfc.cpp"
    "speaker/speaker.cpp"
)

get_filename_component(CFG_DIR "config" ABSOLUTE)
get_filename_component(LED_DIR "led" ABSOLUTE)
get_filename_component(LOG_DIR "logger" ABSOLUTE)
get_filename_component(NET_DIR "network" ABSOLUTE)
get_filename_component(NFC_DIR "nfc" ABSOLUTE)
get_filename_component(SPK_DIR "speaker" ABSOLUTE)

if(NOT EXISTS "${COMPONENT_DIR}/config/config.json")
    configure_file("${COMPONENT_DIR}/config/config.json.example" "${COMPONENT_DIR}/config/config.json" COPYONLY)
endif()


idf_component_register(SRCS ${SOURCES}
    INCLUDE_DIRS "." ${CFG_DIR} ${LED_DIR} ${LOG_DIR} ${NET_DIR} ${NFC_DIR} ${SPK_DIR}
    EMBED_TXTFILES "config/config.json"
)

