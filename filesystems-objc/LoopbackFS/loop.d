#!/usr/sbin/dtrace -s

#pragma D option quiet

macfuse_objc*:::delegate-entry 
/execname == "LoopbackFS"/
{
    printf("%s: %s\n", probefunc, copyinstr(arg0));
}
