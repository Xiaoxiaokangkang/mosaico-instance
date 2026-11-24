idf_component_register(
    SRCS "src/pet_store.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash pet_core
)
