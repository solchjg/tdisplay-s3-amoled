# Shadows the shipped `lcd` component: same name and header, stub behaviour.
idf_component_register(SRCS "lcd_fault.c"
                       INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../../../../components/lcd/include"
                       PRIV_REQUIRES log)
