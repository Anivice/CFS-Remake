#include "utils.h"
extern void show();

int main()
{
    const auto [fd_stdout, fd_stderr, exit_status] =
        cfs::utils::exec_command("/bin/sh", "", "-c",
            "fail=0; for t in " CMAKE_BINARY_DIR R"(/*.test; do if ! $t; then echo "$t failed!" >&2; fail=1; fi; done; exit $fail;)");
    if (exit_status == 0) {
        show();
        std::cout << "Test passed" << std::endl;
    } else {
        std::cout << "<=== STDOUT ===>" << std::endl;
        std::cout << fd_stdout << std::endl;
        std::cout << "<=== STDERR ===>" << std::endl;
        std::cout << fd_stderr << std::endl;
        std::cout << "<==============>" << std::endl;
    }
    return exit_status;
}
