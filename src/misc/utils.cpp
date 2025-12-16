#include <sys/stat.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "utils.h"
#include "generalCFSbaseError.h"

std::string cfs::utils::getenv(const std::string& name) noexcept
{
    const auto var = ::getenv(name.c_str());
    if (var == nullptr) {
        return "";
    }

    return var;
}

std::string cfs::utils::replace_all(
    std::string & original,
    const std::string & target,
    const std::string & replacement) noexcept
{
    if (target.empty()) return original; // Avoid infinite loop if target is empty

    if (target.size() == 1 && replacement.empty()) {
        std::erase_if(original, [&target](const char c) { return c == target[0]; });
        return original;
    }

    size_t pos = 0;
    while ((pos = original.find(target, pos)) != std::string::npos) {
        original.replace(pos, target.length(), replacement);
        pos += replacement.length(); // Move past the replacement to avoid infinite loop
    }

    return original;
}

std::pair < const int, const int > cfs::utils::get_screen_col_row() noexcept
{
    constexpr int term_col_size = 80;
    constexpr int term_row_size = 25;
    const auto col_size_from_env = cfs::utils::getenv("COLUMNS");
    const auto row_size_from_env = cfs::utils::getenv("LINES");
    long col_env = -1;
    long row_env = -1;

    try
    {
        if (!col_size_from_env.empty() && !row_size_from_env.empty()) {
            col_env = std::strtol(col_size_from_env.c_str(), nullptr, 10);
            row_env = std::strtol(row_size_from_env.c_str(), nullptr, 10);
        }
    } catch (...) {
        col_env = -1;
        row_env = -1;
    }

    auto get_pair = [&]->std::pair < const int, const int >
    {
        if (col_env != -1 && row_env != -1) {
            return {row_env, col_env};
        }

        return {term_row_size, term_col_size};
    };

    bool is_terminal = false;
    struct stat st{};
    if (fstat(STDOUT_FILENO, &st) == -1) {
        return get_pair();
    }

    if (isatty(STDOUT_FILENO)) {
        is_terminal = true;
    }

    if (is_terminal)
    {
        winsize w{};
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) != 0 || (w.ws_col | w.ws_row) == 0) {
            return get_pair();
        }

        return {w.ws_row, w.ws_col};
    }

    return get_pair();
}

uint64_t cfs::utils::get_timestamp() noexcept
{
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

uint64_t cfs::utils::arithmetic::count_cell_with_cell_size(const uint64_t cell_size, const uint64_t particles)
{
    assert_throw(cell_size != 0, "DIV/0");
#ifdef __x86_64__
    // 50% performance boost bc we used div only once
    uint64_t q, r;
    asm ("divq %[d]"
         : "=a"(q), "=d"(r)
         : "0"(particles), "1"(0), [d]"r"(cell_size)
         : "cc");

    if (r != 0) return q + 1;
    else return q;
#else
    const uint64_t cells = particles / cell_size;
    if (const uint64_t reminder = particles % cell_size; reminder != 0) {
        return cells + 1;
    }

    return cells;
#endif
}

cfs::utils::arithmetic::CRC64::CRC64() noexcept
{
    init_crc64();
}

void cfs::utils::arithmetic::CRC64::update(const uint8_t* data, const size_t length) noexcept
{
    for (size_t i = 0; i < length; ++i) {
        crc64_value = table[(crc64_value ^ data[i]) & 0xFF] ^ (crc64_value >> 8);
    }
}

[[nodiscard]] uint64_t cfs::utils::arithmetic::CRC64::get_checksum() const noexcept
{
    // add the final complement that ECMA‑182 requires
    return (crc64_value ^ 0xFFFFFFFFFFFFFFFFULL);
}

void cfs::utils::arithmetic::CRC64::init_crc64() noexcept
{
    crc64_value = 0xFFFFFFFFFFFFFFFF;
    for (uint64_t i = 0; i < 256; ++i) {
        uint64_t crc = i;
        for (uint64_t j = 8; j--; ) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xC96C5795D7870F42;  // Standard CRC-64 polynomial
            else
                crc >>= 1;
        }
        table[i] = crc;
    }
}
