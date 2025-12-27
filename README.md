# Copy-on-write File System

Copy-on-write File System is a small CoW file system that is designed to
work with `mmap(2)` compatible files.
It servers as a storage format rather than a full-blown file system,
even though you can use CFS with FUSE.

# TODO:

 - [x] Snapshots
 - [x] Snapshot management (creation, rollback, deletion)
 - [x] `mkfs.cfs` utility
 - [x] `mount.cfs` utility
 - [ ] `fsck.cfs` utility
