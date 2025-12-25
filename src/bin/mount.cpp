#include "routes.h"
#include "logger.h"
#include <cstdlib>

#ifdef CFS_COMPILE_FUSE
#else
int mount_main(int argc, char** argv)
{
    elog("CFS is not compiled with FUSE\n");
    return EXIT_FAILURE;
}
#endif
