idf_component_register(SRCS "main.c" "settings.c" "mqtt.c" "mq135.c" "ky037.c" "dht11.c" "data.c" "aes-ctr.c"
        INCLUDE_DIRS "."
        REQUIRES mbedtls)