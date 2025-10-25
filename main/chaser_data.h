typedef struct  {
  uint8_t color[18];        //  0-17: Six 24-bit colors in RGB triplets
  uint16_t position_offset; // 18-19: Starting position of this chaser
  int8_t position_speed;    //    20: The direction and speed of motion
  uint8_t position_delay;   //    21: Move position every n'th frame (minimum 1)
  uint8_t color_offset;     //    22: Starting color of this chaser (0 to 239)
  uint8_t color_speed;      //    23: The speed of color shift
  uint8_t color_delay;      //    24: Move color every n'th frame (minimum 1)
  uint8_t repeat;           //    25: Apply this to every n'th pixel (0 = no repeat)
  uint16_t limit_low;       // 26-27: Not implemented
  uint16_t limit_high;      // 28-29: Not implemented
  uint8_t reserved[2];      // 30-31: Not implemented
} chaser_data_t;