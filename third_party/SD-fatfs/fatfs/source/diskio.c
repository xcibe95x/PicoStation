#include "ff.h"
#include "diskio.h"
#include <string.h>

DSTATUS disk_status(BYTE pdrv) {
    return 0; // STA_NOINIT se vuoi simulare errore
}

DSTATUS disk_initialize(BYTE pdrv) {
    return 0; // OK
}

DRESULT disk_read(BYTE pdrv, BYTE* buff, LBA_t sector, UINT count) {
    memset(buff, 0xFF, count * 512);
    return RES_OK;
}

#if FF_FS_READONLY == 0
DRESULT disk_write(BYTE pdrv, const BYTE* buff, LBA_t sector, UINT count) {
    return RES_OK;
}
#endif

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void* buff) {
    return RES_OK;
}
