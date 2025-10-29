typedef struct  {
  uint8_t color[18];            //  0-17: Six 24-bit colors in RGB triplets
  uint16_t position_offset;     // 18-19: Starting position of this chaser
  int8_t position_speed;        //    20: The direction and speed of motion
  uint8_t position_delay;       //    21: Move position every n'th frame (minimum 1)
  uint8_t color_offset;         //    22: Starting color of this chaser (0 to 239)
  uint8_t color_speed;          //    23: The speed of color shift
  uint8_t color_delay;          //    24: Move color every n'th frame (minimum 1)
  uint8_t repeat;               //    25: Apply this to every n'th pixel (0 = no repeat)
  uint16_t range_length;        // 28-29: range length
  uint16_t range_offset;        // 26-27: range offset
  uint8_t flags;                //    30: flags
  uint8_t reserved;             //    31: padding
  uint16_t previous_position;   // 32-33: internal - keep track of previous position for clearing
} chaser_data_t;

#define FLAG_CLEAR_PREVIOUS   1
#define FLAG_RANDOM_POSITION  2
#define FLAG_RANDOM_COLOR     4
#define FLAG_SINUSOIDAL       8
#define FLAG_ADDITIVE_COLOR  16
#define FLAG_RESERVED_2      32
#define FLAG_RESERVED_3      64
#define FLAG_RESERVED_4     128
