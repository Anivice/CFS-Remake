#include "mmap.h"
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace cfs::basic_io
{
    mmap::~mmap()
    {
        close();
    }

    void mmap::open(const std::string& file)
    {
        fd = ::open(file.c_str(), O_RDWR);
        if (fd == -1) {
            throw error::BasicIOcannotOpenFile("invalid fd returned by ::open(\"" + file + "\", O_RDWR)");
        }

        struct stat st = { };
        if (fstat(fd, &st) == -1) {
            throw error::BasicIOcannotOpenFile("fstat failed for file " + file);
        }

        // Map the entire file into virtual address space
        data_ = static_cast<char*>(::mmap(nullptr, st.st_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0));

        if (data_ == MAP_FAILED) {
            throw error::BasicIOcannotOpenFile("mmap failed for file " + file);
        }

        size_ = st.st_size;
    }

    void mmap::close()
    {
        if (data_ != MAP_FAILED)
        {
            ::munmap(data_, size_);
            ::close(fd);
        }
    }
}
