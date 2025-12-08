#include <iostream>
#include "general_cfs_error.h"

void f2()
{
    throw cfs::error::general_cfs_error("Error", true);
}

void f1()
{
    f2();
}

int main()
{
    try
    {
        f1();
    }
    catch (cfs::error::general_cfs_error & e)
    {
        std::cout << e.what() << std::endl;
    }

    return 0;
}
