#ifndef CFS_MMAP_H
#define CFS_MMAP_H

#include "generalCFSbaseError.h"
#include <sys/mman.h>

/// Cannot open file
make_simple_error_class(BasicIOcannotOpenFile);

namespace cfs::basic_io
{
    /// Map file to address
    class mmap
    {
        int fd = -1;
        void * data_ = MAP_FAILED;
        unsigned long long int size_ = 0;
    public:
        mmap() = default;
        mmap(const mmap &) = delete;
        mmap(mmap &&) = delete;
        mmap & operator=(const mmap &) = delete;
        mmap & operator=(mmap &&) = delete;

        explicit mmap(const std::string & file) { open(file); }
        ~mmap();
        void open(const std::string & file);
        void close();

        /// Get mapped file array
        /// @return File array
        [[nodiscard]] char * data() const { return (char*)data_; }

        /// Get array size
        /// @return array size
        [[nodiscard]] unsigned long long int size() const { return size_; }

        /// Get file descriptor for this disk file
        /// @return file descriptor for this disk file
        [[nodiscard]] int get_fd() const { return fd; }

        void sync();
    };
}

#endif //CFS_MMAP_H
