#include <iostream>
#include "colors.h"
#include "generalCFSbaseError.h"
#include "utils.h"

void f2()
{
    throw cfs::error::generalCFSbaseError("Error", true);
}

void f1()
{
    f2();
}

int main()
{
    try
    {
        std::vector<std::map<int, std::vector<int>>> vec = { { { 1, {1,2,3,4,5,6,7,8,9,10,11,12} }, { 2, {1,2,3} } }, { { 1, {1,2,3} }, { 2, {1,2,3} } }, };
        dlog("\n", cfs::utils::get_screen_col_row(), "\n");
        ilog("\n", std::make_pair(vec, vec), "\n");
        f1();
    }
    catch (cfs::error::generalCFSbaseError & e)
    {
        elog(e.what(), "\n");
    }

    return 0;
}
