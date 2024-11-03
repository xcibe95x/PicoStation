/*

MIT License

Copyright (c) 2020 PCSX-Redux authors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/

#include "posix_file.h"

#include "ff_stdio.h"

#include <stdio.h>


static void posix_destroy(struct CueFile *file) {}

static void posix_close(struct CueFile *file, struct CueScheduler *scheduler, void (*cb)(struct CueFile *, struct CueScheduler *)) {
    ff_fclose((FIL *)file->opaque);
    File_schedule_close(file, scheduler, cb);
}

static void posix_size(struct CueFile *file, struct CueScheduler *scheduler, int compressed,
                       void (*cb)(struct CueFile *, struct CueScheduler *, uint64_t)) {
    FIL *f = (FIL *)file->opaque;
    ff_fseek(f, 0, FF_SEEK_END);
    uint64_t size = ff_ftell(f);
    File_schedule_size(file, scheduler, size, cb);
}

static void posix_read(struct CueFile *file, struct CueScheduler *scheduler, uint32_t amount, uint64_t cursor,
                       uint8_t *buffer,
                       void (*cb)(struct CueFile *, struct CueScheduler *, int error, uint32_t amount, uint8_t *buffer)) {
    FIL *f = (FIL *)file->opaque;
    ff_fseek(f, cursor, FF_SEEK_SET);
    f->err = 0; //clearerr(f);
    size_t r = ff_fread(buffer, 1, amount, f);
    File_schedule_read(file, scheduler, f_error(f) == 0 ? 0 : 1, r, buffer, cb);
}

static void posix_write(struct CueFile *file, struct CueScheduler *scheduler, uint32_t amount, uint64_t cursor,
                        const uint8_t *buffer,
                        void (*cb)(struct CueFile *, struct CueScheduler *, int error, uint32_t amount)) {
    FIL *f = (FIL *)file->opaque;
    ff_fseek(f, cursor, FF_SEEK_SET);
    f->err = 0; //clearerr(f);
    size_t r = ff_fwrite(buffer, 1, amount, f);
    File_schedule_write(file, scheduler, f_error(f) == 0 ? 0 : 1, r, cb);
}

struct CueFile *create_posix_file(struct CueFile *file, const char *filename, const char *mode) {
    file->opaque = ff_fopen(filename, mode);
    file->destroy = posix_destroy;
    file->close = posix_close;
    file->size = posix_size;
    file->read = posix_read;
    file->write = posix_write;
    file->cfilename = NULL;
    file->filename = NULL;
    file->references = 1;
    return file->opaque ? file : NULL;
}