#! /usr/sbin/dtrace -s

#pragma D option quiet

BEGIN
{
    begints = timestamp;
}

pid$1:libfuse*dylib:fuse_fs_init:entry,
pid$1:libfuse*dylib:fuse_fs_destroy:entry
{
    self->begints = timestamp;
    self->init_destroy = 1;
}

pid$1:libfuse*dylib:fuse_fs_init:return,
pid$1:libfuse*dylib:fuse_fs_destroy:return
/ self->init_destroy /
{
    this->elapsed = timestamp - self->begints;

    printf("+%-12d %3d.%03d %-8d%-16s\n", (timestamp - begints) / 1000,
           this->elapsed / 1000000, (this->elapsed / 1000) % 1000,
           (int)arg1, probefunc + 8);

    self->begints = 0;
    self->init_destroy = 0;
}

pid$1:libfuse*dylib:fuse_fs*:entry
/ !self->init_destroy /
{
    self->traceme = 1;
    self->pathptr = arg1;
    self->begints = timestamp;
    self->arg2 = arg2;
}

pid$1:libfuse*dylib:fuse_fs*:return
/ self->traceme && probefunc != "fuse_fs_getattr" /
{
    this->elapsed = timestamp - self->begints;

    printf("+%-12d %3d.%03d %-8d%-16s%s\n", (timestamp - begints) / 1000,
           this->elapsed / 1000000, (this->elapsed / 1000) % 1000,
           (int)arg1, probefunc + 8, copyinstr(self->pathptr));

    self->traceme = 0;
    self->pathptr = 0;
    self->begints = 0;
}

pid$1:libfuse*dylib:fuse_fs*:return
/ self->traceme && probefunc == "fuse_fs_getattr" /
{
    this->elapsed = timestamp - self->begints;
    this->st = (struct stat *)copyin(self->arg2, sizeof(struct stat));

    printf("+%-12d %3d.%03d %-8d%-16s%s (st_size=%lld)\n", (timestamp - begints) / 1000,
           this->elapsed / 1000000, (this->elapsed / 1000) % 1000,
           (int)arg1, probefunc + 8, copyinstr(self->pathptr), this->st->st_size);

    self->traceme = 0;
    self->pathptr = 0;
    self->begints = 0;
}
