#include "smart_block_t.h"
#include "utils.h"
#include <fcntl.h>
#include <filesystem>
#include <cstdint>
#include <cstring>
#include <unistd.h>

#include "cfsBasicComponents.h"
#include "CowFileSystem.h"
#include "CowFileSystem.h"
#include "CowFileSystem.h"
#include "CowFileSystem.h"
#include "CowFileSystem.h"
#include "CowFileSystem.h"

void cfs::bitmap_base::init(const uint64_t required_blocks)
{
    *(uint64_t*)&bytes_required_ = cfs::utils::arithmetic::count_cell_with_cell_size(8, required_blocks);
    *(uint64_t*)&particles_ = required_blocks;
    cfs_assert_simple(init_data_array(bytes_required_));
}

bool cfs::bitmap_base::get_bit(const uint64_t index, const bool use_mutex)
{
    cfs_assert_simple(index < particles_);
    const uint64_t q = index >> 3;
    const uint64_t r = index & 7; // div by 8
    uint8_t c = 0;
    if (use_mutex) {
        std::lock_guard<std::mutex> lock(array_mtx_);
        c = data_array_[q];
    }
    else {
        c = data_array_[q];
    }

    c >>= r;
    c &= 0x01;
    return c;
}

void cfs::bitmap_base::set_bit(const uint64_t index, const bool new_bit, const bool use_mutex)
{
    cfs_assert_simple(index < particles_);
    const uint64_t q = index >> 3;
    const uint64_t r = index & 7; // div by 8
    uint8_t c = 0x01;
    c <<= r;

    auto set = [&]
    {
        if (new_bit) // set to true
        {
            data_array_[q] |= c;
        }
        else // clear bit
        {
            data_array_[q] &= ~c;
        }
    };

    if (use_mutex)
    {
        std::lock_guard<std::mutex> lock(array_mtx_);
        set();
    } else {
        set();
    }
}

namespace solver
{
    namespace arith = cfs::utils::arithmetic;
    uint64_t solve(const uint64_t total, const uint64_t bsize, const uint64_t journal)
    {
        const uint64_t available_blocks = total - journal;
        const uint64_t bits_per_block = bsize << 3;  // bsize*8
        const uint64_t attributes_per_block = bsize >> 2;  // bsize/4

        // total blocks implied by data block number
        auto f_all_blocks = [&](const uint64_t data_blocks)->uint64_t {
            return 2 * arith::count_cell_with_cell_size(bits_per_block, data_blocks)
                + arith::count_cell_with_cell_size(attributes_per_block, data_blocks)
                + data_blocks;
        };

        auto lower_bound_f_ge = [&f_all_blocks](const uint64_t T, uint64_t hi)->uint64_t
        {
            uint64_t lo = 0;
            while (lo < hi)
            {
                const uint64_t mid = lo + (hi - lo) / 2;
                const uint64_t all = f_all_blocks(mid);
                if (all > T || lo == mid) break;
                lo = mid;
            }
            return lo;
        };

        const uint64_t ub = available_blocks;
        const uint64_t lo = lower_bound_f_ge(available_blocks, ub);
        return lo;
    }

}

static bool is_2_power_of(unsigned long long x)
{
    for (unsigned long long i = 1; i <= sizeof(x) * 8; i++)
    {
        if (x & 0x01)
        {
            x >>= 1;
            return !x;
        }

        x >>= 1;
    }

    return false;
}

static void print_table(
    const std::vector<std::pair < std::string, int > > & titles,
    const std::vector<std::vector < std::string > > & vales,
    const std::string & label)
{

    const auto [row, col] = cfs::utils::get_screen_row_col();
    const auto max_available_per_col = (col - (titles.size() + 1)) / titles.size();
    std::map < uint64_t, uint64_t > spaces;
    bool use_max_avail = true;
    auto find_max = [&](const auto & list, std::map < uint64_t, uint64_t > & spaces_)
    {
        cfs_assert_simple(list.size() == titles.size());
        for (auto i = 0ul; i < list.size(); i++) {
            if (list[i].length() > max_available_per_col) use_max_avail = false;
            if (spaces_[i] < list[i].length()) spaces_[i] = list[i].length();
        }
    };

    find_max(titles | std::views::keys, spaces);
    std::ranges::for_each(vales, [&](const std::vector<std::string> & value){ find_max(value, spaces); });
    if (use_max_avail) { std::ranges::for_each(spaces, [&](auto & value){ value.second = max_available_per_col; }); }

    const std::string separator(col, '=');
    {
        const std::string left((col - (label.length() + 2)) / 2, '=');
        const std::string right(col - left.length() - label.length() - 2, '=');
        std::cout << left << " " << label << " " << right << std::endl;
    }

    std::vector<std::string> on_screen_content;
    auto print = [&on_screen_content, &spaces, &col](const auto & values, const auto & justification)
    {
        uint64_t index = 0;
        for (const auto & value : values)
        {
            std::ostringstream oss;
            const auto max_len = spaces[index];
            if (justification[index] == 1) // center
            {
                const auto left_len = std::max((max_len - value.length()) / 2, 0ul);
                const auto right_len = std::max(max_len - left_len - value.length(), 0ul);
                const std::string left(left_len, ' ');
                const std::string right(right_len, ' ');
                oss << left << value << right;
            }
            else if (justification[index] == 2) { // left
                constexpr auto left_len = 1;
                const auto right_len = std::max(static_cast<int>(max_len) - static_cast<int>(left_len) - static_cast<int>(value.length()), 0);
                const std::string left(left_len, ' ');
                const std::string right(right_len, ' ');
                oss << left << value << right;
            }
            else if (justification[index] == 3) { // right
                constexpr auto right_len = 1;
                const auto left_len = std::max(static_cast<int>(max_len) - static_cast<int>(right_len) - static_cast<int>(value.length()), 0);
                const std::string left(left_len, ' ');
                const std::string right(right_len, ' ');
                oss << left << value << right;
            }
            on_screen_content.push_back(oss.str());
            index++;
        }
    };

    auto show = [&]
    {
        int index = 0;
        std::ostringstream oss;
        std::ranges::for_each(on_screen_content, [&](const std::string & str) {
            oss << "|" << str;
            index++;
        });
        const auto before = oss.str().length();
        oss << std::string(std::max(static_cast<int>(col) - static_cast<int>(before) - 1, 0), ' ') << "|";
        std::cout << oss.str();
    };

    print(titles | std::views::keys, std::vector<int>(titles.size(), 1));
    show(); on_screen_content.clear();
    std::cout << std::endl;
    if (col - 2 > 0) std::cout << "+" << std::string(col - 2, '-') << "+" << std::endl;

    std::ranges::for_each(vales, [&](const std::vector<std::string> & value)
    {
        print(value, titles | std::views::values);
        show(); on_screen_content.clear();
        std::cout << std::endl;
    });

    std::cout << separator << std::endl;
}

#define gen_info(a, b) region_gen(a, b), blk_gen(a, b)

/// Make a filesystem header
/// @param file_size Total disk size
/// @param block_size Block size
/// @param label FS label
/// @return header
/// @throws cfs::error::invalid_argument
[[nodiscard]] static
cfs::cfs_head_t make_head(const uint64_t file_size, const uint64_t block_size, const std::string & label)
{
    using namespace cfs;
    cfs_head_t head{};
    head.magick = cfs_magick_number;

    // ────── basic sanity checks ──────
    if (constexpr int SECTOR_SIZE = 512;
        !(block_size >= SECTOR_SIZE && block_size % SECTOR_SIZE == 0 && is_2_power_of(block_size / SECTOR_SIZE)))
    {
        throw error::invalid_argument("Block size not aligned");
    }

    head.static_info.block_size        = block_size;
    head.static_info.blocks            = file_size / block_size;
    std::strncpy(head.static_info.label, label.c_str(), sizeof(head.static_info.label));
    const uint64_t body_size           = head.static_info.blocks - 2;     // head & tail
    const uint64_t journaling_section_size = std::max<uint64_t>(body_size / 100, 32);
    if (body_size <= journaling_section_size) {
        throw error::invalid_argument("Not enough space");
    }
    const uint64_t data_blocks = solver::solve(body_size, block_size, journaling_section_size);
    if (data_blocks == UINT64_MAX) throw cfs::error::invalid_argument("Disk too small");

    // ────── compute bitmap & attribute sizes with correct precedence ──────
    const uint64_t bytes_per_block = block_size;
    const uint64_t bits_per_block  = bytes_per_block * 8ULL;

    const uint64_t data_block_bitmap = utils::arithmetic::count_cell_with_cell_size(bits_per_block, data_blocks);
    const uint64_t data_block_attribute_region = utils::arithmetic::count_cell_with_cell_size(bytes_per_block, data_blocks * 4ULL);

    // ────── carve the regions ──────
    uint64_t block_offset = 1;                 // block 0 is the head itself

    head.static_info.data_bitmap_start = block_offset;
    head.static_info.data_bitmap_end   = block_offset + data_block_bitmap;
    block_offset += data_block_bitmap;

    head.static_info.data_bitmap_backup_start = block_offset;
    head.static_info.data_bitmap_backup_end   = block_offset + data_block_bitmap;
    block_offset += data_block_bitmap;

    head.static_info.data_block_attribute_table_start = block_offset;
    head.static_info.data_block_attribute_table_end   = block_offset + data_block_attribute_region;
    block_offset += data_block_attribute_region;

    head.static_info.data_table_start = block_offset;
    head.static_info.data_table_end   = block_offset + data_blocks;
    block_offset += data_blocks;

    head.static_info.journal_start = block_offset;
    head.static_info.journal_end   = head.static_info.blocks - 1;

    // ────── checksum & timestamps ──────
    head.static_info_checksum = head.static_info_checksum_dup = utils::arithmetic::hash64(head.static_info);
    head.static_info_dup = head.static_info;

    const auto now = utils::get_timestamp();
    head.runtime_info.mount_timestamp = head.runtime_info.last_check_timestamp = now;

    auto region_gen = [](const uint64_t start, const uint64_t end) {
        return "[" + std::to_string(start) + ", " + std::to_string(end) + ")";
    };

    auto blk_gen = [](const uint64_t start, const uint64_t end) {
        return std::to_string(end - start) + " block" + (end - start > 1 ? "s" : "");
    };

    std::vector < std::vector < std::string > > lines;
    std::vector<std::pair < std::string, int > > title = { { "Entry", 3 }, { "Region", 2 }, { "Block count", 2 } };
    auto printLine = [&](const std::string & name, const std::string & line, const std::string & line2) {
        lines.emplace_back(std::vector {name, line, line2});
    };

    printLine("Block size", std::to_string(head.static_info.block_size), "/");
    printLine("Addressable region", gen_info(0, head.static_info.blocks));
    printLine("FILE SYSTEM HEAD", gen_info(0, 1));
    printLine("DATA REGION BITMAP", gen_info(head.static_info.data_bitmap_start, head.static_info.data_bitmap_end));
    printLine("DATA BITMAP BACKUP", gen_info(head.static_info.data_bitmap_backup_start, head.static_info.data_bitmap_backup_end));
    printLine("DATA BLOCK ATTRIBUTE", gen_info(head.static_info.data_block_attribute_table_start, head.static_info.data_block_attribute_table_end));
    printLine("DATA BLOCK", gen_info(head.static_info.data_table_start, head.static_info.data_table_end));
    printLine("JOURNAL REGION", gen_info(head.static_info.journal_start, head.static_info.journal_end));
    printLine("FILE SYSTEM HEAD BACKUP", gen_info(head.static_info.blocks - 1, head.static_info.blocks));
    print_table(title, lines, "Disk Overview");
    return head;
}

#undef gen_info

void cfs::make_cfs(const std::string &path_to_block_file, const uint64_t block_size, const std::string & label)
{
    ilog("Discarding blocks...\n");
    namespace fs = std::filesystem;
    int fd = 0;
    const uint64_t size_bytes = fs::file_size(path_to_block_file);
    assert_throw((fd = open(path_to_block_file.c_str(), O_RDWR)) > 0, "invalid file descriptor");
    assert_throw(fallocate(fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE, 0, size_bytes) == 0, "fallocate() failed");
    assert_throw(fallocate(fd, FALLOC_FL_ZERO_RANGE, 0, size_bytes) == 0, "fallocate() failed");
    close(fd);
    ilog("Discarding finished\n");
    std::vector<uint8_t> empty_blk;
    cfs_head_t head;
    {
        basic_io::mmap file(path_to_block_file);
        assert_throw(file.size() >= cfs_minimum_size, "Disk too small");
        ilog("Calculating CFS info...\n");
        head = make_head(file.size(), block_size, label);
        ilog("Calculating CFS info done.\n");
        ilog("Writing header to file...");
        std::memcpy(file.data(), &head, sizeof(head)); // head
        std::memcpy(file.data() + file.size() - sizeof(head), &head, sizeof(head)); // tail
        ilog("done.\n");
        file.close();
        empty_blk.resize(head.static_info.block_size, 0);
    }
    ilog("Set up bitmap..");
    cfs::filesystem disk_file(path_to_block_file);
    cfs::cfs_journaling_t journal(&disk_file);
    cfs::cfs_bitmap_block_mirroring_t raid1_bitmap(&disk_file, &journal);
    cfs::cfs_block_attribute_access_t attribute(&disk_file, &journal);
    raid1_bitmap.set_bit(0, true);
    attribute.clear(0,
    {
        .block_status = BLOCK_AVAILABLE_TO_MODIFY_0x00,
        .block_type = INDEX_NODE_BLOCK,
        .block_type_cow = 0,
        .allocation_oom_scan_per_refresh_count = 0,
        .newly_allocated_thus_no_cow = 0,
        .index_node_referencing_number = 1,
        .block_checksum = cfs::utils::arithmetic::hash5(empty_blk.data(), empty_blk.size()),
    });
    const auto root = disk_file.lock(head.static_info.data_table_start);
    const auto now = utils::get_timespec();
    struct stat root_stat = {
        .st_dev = 0,
        .st_ino = 0,
        .st_nlink = 1,
        .st_mode = S_IFDIR | 0755,
        .st_uid = getuid(),
        .st_gid = getgid(),
        .st_rdev = 0,
        .st_size = 0,
        .st_blksize = static_cast<decltype(root_stat.st_blksize)>(head.static_info.block_size),
        .st_blocks = 0,
        .st_atim = now,
        .st_mtim = now,
        .st_ctim = now,
    };
    std::memcpy(root.data(), &root_stat, sizeof(root_stat));
    ilog("done.\n");
    ilog("CFS format complete\n");
    ilog("Sync data...\n");
}

void cfs::filesystem::block_shared_lock_t::bitmap_t::create(const uint64_t size)
{
    init_data_array = [&](const uint64_t bytes)->bool
    {
        try {
            data.resize(bytes, 0);
            data_array_ = data.data();
            return true;
        }
        catch (cfs::error::assertion_failed &) {
            throw;
        }
        catch (...) {
            return false;
        }
    };

    cfs::bitmap_base::init(size);
}

cfs::cfs_head_t::runtime_info_t cfs::filesystem::cfs_header_block_t::load()
{
    auto blk0 = parent_->lock(0);
    auto blk_last = parent_->lock(tailing_header_blk_id_);
    const auto ret = fs_head->runtime_info;
    return ret;
}

void cfs::filesystem::cfs_header_block_t::set(const cfs_head_t::runtime_info_t & info)
{
    // first, lock both
    auto blk0 = parent_->lock(0);
    auto blk_last = parent_->lock(tailing_header_blk_id_);

    // then, cow
    fs_head->runtime_info_cow = fs_head->runtime_info;
    fs_end->runtime_info_cow = fs_end->runtime_info;

    // then, we load in
    fs_head->runtime_info = info;
    fs_end->runtime_info = info;
}

void cfs::filesystem::block_shared_lock_t::lock(const uint64_t index)
{
    auto try_acquire_lock = [&]->bool
    {
        std::lock_guard lock(bitmap_mtx_);
        // release_block_id_this_time = UINT64_MAX;
        if (bitmap.get_bit(index, false)) {
            return false;
        }

        bitmap.set_bit(index, true, false);
        return true;
    };

    while (!try_acquire_lock())
    {
        std::unique_lock<std::mutex> lock(bitmap_mtx_);
        cv.wait(lock, [&]->bool { return !bitmap.get_bit(index, false); });
    }
}

void cfs::filesystem::block_shared_lock_t::unlock(const uint64_t index)
{
    try {
        std::lock_guard lock(bitmap_mtx_);
        bitmap.set_bit(index, false, false);
        // release_block_id_this_time = index;
    }
    catch (cfs::error::assertion_failed &) {
        throw;
    }
    catch (...) {
    }
    cv.notify_all();
}

cfs::filesystem::filesystem(const std::string &path_to_block_file) : static_info_({})
{
    file_.open(path_to_block_file);
    if (file_.size() < sizeof(cfs_head_t)) {
        throw error::cannot_even_read_cfs_header_in_that_small_tiny_file();
    }
    // get basic info
    auto * header_temp = (cfs_head_t *)file_.data();
    auto * header_temp_tail = (cfs_head_t *)(file_.data() + file_.size() - sizeof(cfs_head_t));
    if (header_temp->magick != cfs_magick_number) {
        throw error::not_even_a_cfs_filesystem();
    }

    header_temp->runtime_info_cow = header_temp->runtime_info;
    header_temp->runtime_info.flags.clean = 0;
    header_temp->runtime_info.mount_timestamp = utils::get_timestamp();
    header_temp_tail->runtime_info_cow = header_temp_tail->runtime_info;
    header_temp_tail->runtime_info = header_temp->runtime_info;

    const auto static_info_crc64 = utils::arithmetic::hash64(header_temp->static_info);
    const auto static_info_dup_checksum = utils::arithmetic::hash64(header_temp->static_info_dup);
    std::map < uint64_t, decltype(header_temp->static_info) > static_info_map;
    std::vector < std::pair < uint64_t, decltype(header_temp->static_info) > > static_info_vector;
    bool patch_zeros = false;
    cfs_head_t::static_info_t static_info_patch = { };
    auto push = [&](const uint64_t crc64, const decltype(header_temp->static_info) & info)
    {
        if (info.block_size == 0 || info.blocks == 0
            || info.data_bitmap_backup_start == 0
            || info.data_bitmap_backup_end == 0
            || info.data_bitmap_start == 0
            || info.data_bitmap_end == 0
            || info.data_block_attribute_table_start == 0
            || info.data_block_attribute_table_end == 0
            || info.data_table_start == 0
            || info.data_table_end == 0
            || info.journal_start == 0
            || info.journal_end == 0)
        {
            patch_zeros = true;
            return; // ignore obvious faulty ones
        }
        static_info_map[crc64] = info;
        static_info_vector.emplace_back(crc64, info);
    };
    push(static_info_crc64, header_temp->static_info);
    push(static_info_dup_checksum, header_temp->static_info_dup);
    push(header_temp->static_info_checksum, header_temp->static_info);
    push(header_temp->static_info_checksum_dup, header_temp->static_info_dup);
    push(static_info_crc64, header_temp_tail->static_info);
    push(static_info_dup_checksum, header_temp_tail->static_info_dup);
    push(header_temp_tail->static_info_checksum, header_temp_tail->static_info);
    push(header_temp_tail->static_info_checksum_dup, header_temp_tail->static_info_dup);

    if (static_info_map.size() > 1) // different checksum
    {
        // count which one has the highest similarity
        std::map < uint64_t /* checksum */, uint64_t /* appearances */ > appearances;
        std::ranges::for_each(static_info_vector | std::views::keys, [&](const uint64_t index) { appearances[index]++; });
        uint64_t most_appeared_crc64 = 0, most_appeared_crc64_count = 0;
        std::ranges::for_each(appearances, [&](const std::pair <uint64_t, uint64_t> & index)
        {
            if (most_appeared_crc64_count < index.second)
            {
                most_appeared_crc64 = index.first;
                most_appeared_crc64_count = index.second;
            }
        });

        if (most_appeared_crc64_count != 1)
        {
            header_temp->static_info                    = static_info_map[most_appeared_crc64];
            header_temp->static_info_dup                = header_temp->static_info;
            header_temp->static_info_checksum           = most_appeared_crc64;
            header_temp->static_info_checksum_dup       = most_appeared_crc64;
            header_temp_tail->static_info               = header_temp->static_info;
            header_temp_tail->static_info_dup           = header_temp->static_info;
            header_temp_tail->static_info_checksum      = header_temp->static_info_checksum;
            header_temp_tail->static_info_checksum_dup  = header_temp->static_info_checksum;
            static_info_patch = header_temp->static_info;
        }
        else
        {
            throw error::filesystem_head_corrupt_and_unable_to_recover();
        }
    }

    if (patch_zeros)
    {
        if (static_info_map.size() == 1) {
            static_info_patch = static_info_map.begin()->second;
        }
        header_temp->static_info            = static_info_patch;
        header_temp->static_info_dup        = static_info_patch;
        header_temp_tail->static_info       = static_info_patch;
        header_temp_tail->static_info_dup   = static_info_patch;
    }

    *(cfs_head_t::static_info_t*)&static_info_ = header_temp->static_info;

    // init header
    cfs_header_block.parent_ = this;
    *(uint64_t*)&cfs_header_block.tailing_header_blk_id_ = static_info_.blocks - 1;
    cfs_header_block.fs_head = header_temp;
    cfs_header_block.fs_end = header_temp_tail;
    *(uint64_t*)&bitlocker_.blocks_ = static_info_.blocks;
    bitlocker_.init();
}

cfs::filesystem::guard cfs::filesystem::lock(const uint64_t index)
{
    cfs_assert_simple(index < static_info_.blocks);
    return cfs::filesystem::guard(
        &this->bitlocker_,
        this->file_.data() + index * static_info_.block_size,
        index, static_info_.block_size);
}

cfs::filesystem::~filesystem() noexcept
{
    try {
        file_.sync();
    } catch (std::exception & e) {
        elog(e.what(), "\n");
    }
}
