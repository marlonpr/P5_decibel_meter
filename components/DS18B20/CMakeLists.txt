idf_component_register(
	SRCS "ds18b20.c"
	INCLUDE_DIRS "."
	REQUIRES  esp_driver_gpio
)