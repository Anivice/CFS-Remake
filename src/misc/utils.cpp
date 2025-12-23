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

std::pair < const int, const int > cfs::utils::get_screen_row_col() noexcept
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

timespec cfs::utils::get_timespec() noexcept
{
    timespec ts;
    timespec_get(&ts, TIME_UTC);
    return ts;
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
    const uint64_t cells = particles / cell_size + (particles % cell_size == 0 ? 0 : 1);
    return cells;
#endif
}

uint64_t cfs::utils::arithmetic::hash64(const uint8_t *data, const size_t length) noexcept
{
    return CRC64::ECMA::calc(data, length);
}

uint8_t cfs::utils::arithmetic::hash5(const uint8_t *data, const size_t length) noexcept
{
    const uint8_t checksum = CRC8::CDMA2000::calc(data, length);
    const uint8_t bit_0_1 = checksum & 0x03;        // 0 - 1
    const uint8_t bit_3 = (checksum & 0x08) >> 1;   // 2
    const uint8_t bit_5 = (checksum & 0x20) >> 2;   // 3
    const uint8_t bit_7 = (checksum & 0x80) >> 3;   // 4
    const uint8_t result = bit_0_1 | bit_3 | bit_5 | bit_7;
    return result;
}

std::vector<uint8_t> cfs::utils::arithmetic::compress(const std::vector<uint8_t> &data) noexcept
{
    if (data.size() > LZ4_MAX_INPUT_SIZE) return { };

    std::vector < uint8_t > ret;
    const int maxDst = LZ4_compressBound(data.size());   // worst-case bound
    ret.resize(maxDst);

    const int cSize = LZ4_compress_default((char*)data.data(), (char*)ret.data(), data.size(), maxDst);  // returns 0 on failure
    if (cSize == 0) { return { }; }
    ret.resize(cSize + sizeof(int));
    *(int*)(ret.data() + cSize) = cSize;
    return ret;
}

std::vector<uint8_t> cfs::utils::arithmetic::decompress(const std::vector<uint8_t> &data) noexcept
{
    std::vector < uint8_t > result;
    const int * cSize = (int*)(data.data() + data.size() - sizeof(int));
    result.resize(*cSize);
    const int dSize = LZ4_decompress_safe((char*)data.data(), (char*)result.data(),
                                          data.size() - sizeof(int), *cSize);
    if (dSize < 0) return { };   // malformed input or dst too small
    return result;
}
