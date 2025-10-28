idf_component_register(
    SRCS "main.c" "neopixel.c"
    INCLUDE_DIRS "."
    EMBED_TXTFILES "../html/index.min.html" "../html/s.css" "../html/j.js" 
)
