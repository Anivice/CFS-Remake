#include "execute.h"
#include <iostream>

int main()
{
    std::cout << cfs::utils::exec_command("/usr/bin/bc", "32 + 5").fd_stdout << std::endl;
}
