include_directories(src)
add_library(chck_inihck STATIC
   src/chck/unicode/unicode.c
   src/chck/string/string.c
   src/chck/pool/pool.c
   src/chck/lut/lut.c)
