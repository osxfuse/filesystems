LoopBackFS

This is a simple but complete example filesystem that mounts a local 
directory. You can modify this to see how the Finder reacts to returning
specific error codes or not implementing a particular UserFileSystem
operation.

For example, you can mount "/tmp" in /Volumes/loop. Note: It is 
probably not a good idea to mount "/" through this filesystem.

You can build a .app version from LoopbackFS.xcodeproj and a standalone 
command line version using:

gcc -o loop LoopbackFS.m loop.m -framework MacFUSE -framework Foundation

This will create a binary called "loop" in the current directory.
