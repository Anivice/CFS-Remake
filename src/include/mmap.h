#ifndef CFS_MMAP_H
#define CFS_MMAP_H

#include "generalCFSbaseError.h"

/// Cannot open file
make_simple_error_class(BasicIOcannotOpenFile);

namespace cfs::basic_io
{

    class mmap
    {
        int fd = -1;
        char * data_ = nullptr;
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

        [[nodiscard]] char * data() const { return data_; }
        [[nodiscard]] unsigned long long int size() const { return size_; }
    };
}

#endif //CFS_MMAP_H
