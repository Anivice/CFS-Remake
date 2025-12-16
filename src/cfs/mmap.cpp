#include "mmap.h"
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include "utils.h"

namespace cfs::basic_io
{
    mmap::~mmap() noexcept
    {
        try {
            close();
        }
        catch (std::exception & e) {
            elog(e.what(), "\n");
        }
        catch (...) {
            elog("Cannot sync file\n");
        }
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
        data_ = static_cast<char*>(::mmap(nullptr, st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));

        if (data_ == MAP_FAILED) {
            throw error::BasicIOcannotOpenFile("mmap failed for file " + file);
        }

        size_ = st.st_size;
    }

    void mmap::close()
    {
        if (data_ != MAP_FAILED)
        {
            sync();
            cfs_assert_simple(::munmap(data_, size_) == 0);
            ::close(fd);
            data_ = MAP_FAILED;
        }
    }

    void mmap::sync()
    {
        cfs_assert_simple(msync(data_, size_, MS_SYNC) == 0);
        cfs_assert_simple(fsync(fd) == 0);
    }
}
