#include <iostream>
#include "colors.h"
#include "generalCFSbaseError.h"
#include "mmap.h"
#include "utils.h"

void f2()
{
    throw cfs::error::generalCFSbaseError("Error", true);
}

void f1()
{
    f2();
}

int main(int argc, char** argv)
{
    try
    {
        dlog("\n", cfs::utils::get_screen_col_row(), "\n");
        cfs::basic_io::mmap file(argv[1]);
        std::cout.write(file.data(), file.size());
        f1();
    }
    catch (cfs::error::generalCFSbaseError & e)
    {
        elog(e.what(), "\n");
    }

    return 0;
}
