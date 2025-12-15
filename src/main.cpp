#include <iostream>
#include "colors.h"
#include "generalCFSbaseError.h"
#include "utils.h"

int main(int argc, char** argv)
{
    try
    {
        dlog(cfs::utils::arithmetic::count_cell_with_cell_size(4, 9), "\n");
    }
    catch (cfs::error::generalCFSbaseError & e) {
        elog(e.what(), "\n");
    }
    catch (std::exception & e) {
        elog(e.what(), "\n");
    }

    return 0;
}
