#!/usr/sbin/dtrace -s

#pragma D option quiet
#pragma D option bufsize=16k

macfuse_objc*:::delegate-entry 
/execname == "LoopbackFS"/
{
    printf("%-14d %s: %s\r\n", timestamp, probefunc, copyinstr(arg0));
}
