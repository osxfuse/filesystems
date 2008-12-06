#!/usr/sbin/dtrace -s

#pragma D option quiet

macfuse_objc*:::delegate-entry 
/execname == "LoopbackFS"/
{
    printf("%-14d %s: %s\r\n", timestamp, probefunc, copyinstr(arg0));
}
