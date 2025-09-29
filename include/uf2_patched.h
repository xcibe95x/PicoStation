#pragma once
#include <stdint.h>

#define UF2_MAGIC_START0 0x0A324655
#define UF2_MAGIC_START1 0x9E5D5157
#define UF2_MAGIC_END    0x0AB16F30

// UF2 flags
#define UF2_FLAG_NOT_MAIN_FLASH     0x00000001
#define UF2_FLAG_FILE_CONTAINER     0x00001000
#define UF2_FLAG_FAMILY_ID_PRESENT  0x00002000
#define UF2_FLAG_MD5_PRESENT        0x00004000

struct __attribute__((packed)) uf2_block {
    uint32_t magic_start0;
    uint32_t magic_start1;
    uint32_t flags;
    uint32_t target_addr;
    uint32_t payload_size;
    uint32_t block_no;
    uint32_t num_blocks;
    uint32_t file_size;
    uint8_t  data[476];
    uint32_t magic_end;
};
