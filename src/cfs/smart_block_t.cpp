#include "smart_block_t.h"
#include "smart_block_t.h"
#include "utils.h"
#include <errno.h>
#include <fcntl.h>
#include <filesystem>
#include <linux/fs.h>   // BLKDISCARD, BLKGETSIZE64
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int discard_range(const int fd, const uint64_t offset, const uint64_t length)
{
    uint64_t range[2] = { offset, length };          // bytes
    return ioctl(fd, BLKDISCARD, range);
}

void cfs::bitmap_base::init(const uint64_t required_blocks)
{
    *(uint64_t*)&bytes_required_ = cfs::utils::arithmetic::count_cell_with_cell_size(8, required_blocks);
    if (!init_data_array(bytes_required_)) {
        throw cfs::error::bitmap_base_init_data_array_returns_false("Init data array failed");
    }
}

bool cfs::bitmap_base::get_bit(const uint64_t index)
{
    const uint64_t q = index >> 3;
    const uint64_t r = index & 7; // div by 8
    uint8_t c = 0;
    {
        std::lock_guard<std::mutex> lock(array_mtx_);
        c = data_array_[q];
    }

    c >>= r;
    c &= 0x01;
    return c;
}

void cfs::bitmap_base::set_bit(const uint64_t index, const bool new_bit)
{
    const uint64_t q = index >> 3;
    const uint64_t r = index & 7; // div by 8
    uint8_t c = new_bit & 0x01;
    c <<= r;
    {
        std::lock_guard<std::mutex> lock(array_mtx_);
        if (new_bit) // set to true
        {
            data_array_[q] |= c;
        }
        else // clear bit
        {
            data_array_[q] &= ~c;
        }
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

bool is_2_power_of(unsigned long long x)
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

/// Make a filesystem header
/// @param file_size Total disk size
/// @param block_size Block size
/// @param label FS label
/// @return header
[[nodiscard]]
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
    head.static_info.journal_end   = head.static_info.blocks - 2;
    block_offset += head.static_info.journal_end - head.static_info.journal_start;

    // ────── checksum & timestamps ──────
    head.static_info_checksum = head.static_info_checksum_dup = utils::arithmetic::hashcrc64(head.static_info);
    head.static_info_dup = head.static_info;

    const auto now = utils::get_timestamp();
    head.runtime_info.mount_timestamp = head.runtime_info.last_check_timestamp = now;

    auto region_gen = [](const uint64_t start, const uint64_t end) {
        return color::color(1,5,4) + "[" + std::to_string(start) + ", " + std::to_string(end) + ")" + color::color(3,3,3)
        + " (" + std::to_string(end - start) + " block<s>)" + color::no_color();
    };

    const auto data_block_attribute = region_gen(head.static_info.data_block_attribute_table_start, head.static_info.data_block_attribute_table_end);
    const auto data_bitmap_region = region_gen(head.static_info.data_bitmap_start, head.static_info.data_bitmap_end);
    const auto data_bitmap_backup_region = region_gen(head.static_info.data_bitmap_backup_start, head.static_info.data_bitmap_backup_end);
    const auto data_region = region_gen(head.static_info.data_table_start, head.static_info.data_table_end);
    const auto journal_region = region_gen(head.static_info.journal_start, head.static_info.journal_end);

    ilog("============================================ Disk Overview ============================================\n");
    ilog("               ", head.static_info.blocks, " blocks (addressable region: ", region_gen(0, head.static_info.blocks), ")\n");
    ilog(" Block size:   ", head.static_info.block_size, " bytes\n");
    ilog("  ─────────────────────────────┬───────────────────────────────────────────────────────────────────────\n");
    ilog(color::color(5,5,5), "              FILE SYSTEM HEAD │ BLOCK: ", region_gen(0, 1), "\n");
    ilog("  ─────────────────────────────┼───────────────────────────────────────────────────────────────────────\n");
    ilog(color::color(5,5,5), "            DATA REGION BITMAP │ BLOCK: ", data_bitmap_region, "\n");
    ilog("  ─────────────────────────────┼───────────────────────────────────────────────────────────────────────\n");
    ilog(color::color(5,5,5), "        DATA BITMAP BACKUP MAP │ BLOCK: ", data_bitmap_backup_region, "\n");
    ilog("  ─────────────────────────────┼───────────────────────────────────────────────────────────────────────\n");
    ilog(color::color(5,5,5), "          DATA BLOCK ATTRIBUTE │ BLOCK: ", data_block_attribute, "\n");
    ilog("  ─────────────────────────────┼───────────────────────────────────────────────────────────────────────\n");
    ilog(color::color(5,5,5), "                    DATA BLOCK │ BLOCK: ", data_region, "\n");
    ilog("  ─────────────────────────────┼───────────────────────────────────────────────────────────────────────\n");
    ilog(color::color(5,5,5), "                JOURNAL REGION │ BLOCK: ", journal_region, "\n");
    ilog("  ─────────────────────────────┼───────────────────────────────────────────────────────────────────────\n");
    ilog(color::color(5,5,5), "       FILE SYSTEM HEAD BACKUP │ BLOCK: ", region_gen(block_offset, block_offset + 1), "\n");
    ilog("  ─────────────────────────────┴───────────────────────────────────────────────────────────────────────\n");
    ilog("=======================================================================================================\n");

    return head;
}

void cfs::make_cfs(const std::string &path_to_block_file, const uint64_t block_size, const std::string & label)
{
    ilog("Discarding blocks...\n");
    namespace fs = std::filesystem;
    if (fs::is_block_file(path_to_block_file))
    {
        const int fd = open(path_to_block_file.c_str(), O_RDWR | O_CLOEXEC);
        assert_throw(fd > 0, "invalid file descriptor");

        uint64_t size_bytes = 0;
        assert_throw(ioctl(fd, BLKGETSIZE64, &size_bytes) == 0, "ioctl(BLKGETSIZE64) failed")

        // Chunk size: pick something sane; if EINVAL, reduce it.
        uint64_t step = 256ULL * 1024 * 1024; // 256 MiB
        for (uint64_t off = 0; off < size_bytes; )
        {
            uint64_t len = size_bytes - off;
            if (len > step) len = step;

            if (discard_range(fd, off, len) != 0)
            {
                if (errno == EINVAL && step > (1ULL * 1024 * 1024)) {
                    step /= 2; // device may have a smaller max discard size
                    continue;
                }

                std::vector<char> buffer;
                buffer.resize(256, 0);
                sprintf(buffer.data(), "discard failed at off=%llu len=%llu",
                        static_cast<unsigned long long>(off), static_cast<unsigned long long>(len));
                throw error::cannot_discard_blocks(std::string(buffer.data()));
            }
            off += len;
        }
        close(fd);
    }
    else if (fs::is_regular_file(path_to_block_file))
    {
        int fd = 0;
        const uint64_t size_bytes = fs::file_size(path_to_block_file);
        assert_throw((fd = open(path_to_block_file.c_str(), O_RDWR)) > 0, "invalid file descriptor");
        assert_throw(fallocate(fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE, 0, size_bytes) == 0, "fallocate() failed");
        assert_throw(fallocate(fd, FALLOC_FL_ZERO_RANGE, 0, size_bytes) == 0, "fallocate() failed");
        close(fd);
    }
    ilog("Discard complete\n");
    basic_io::mmap file(path_to_block_file);
    assert_throw(file.size() >= cfs_minimum_size, "Disk too small");
    const auto head = make_head(file.size(), block_size, label);
    std::memcpy(file.data(), &head, sizeof(head)); // head
    std::memcpy(file.data() + file.size() - sizeof(head), &head, sizeof(head)); // tail
    ilog("CFS format complete\n");
    file.close();
}

cfs::cfs_head_t::runtime_info_t cfs::filesystem::cfs_header_block_t::load()
{
    bitlocker_->lock(0);
    bitlocker_->lock(tailing_header_blk_id_);
    const auto ret = fs_head->runtime_info;
    bitlocker_->unlock(0);
    bitlocker_->unlock(tailing_header_blk_id_);
    return ret;
}

void cfs::filesystem::cfs_header_block_t::set(const cfs_head_t::runtime_info_t & info)
{
    // first, lock both
    bitlocker_->lock(0);
    bitlocker_->lock(tailing_header_blk_id_);

    // then, cow
    fs_head->runtime_info_cow = fs_head->runtime_info;
    fs_end->runtime_info_cow = fs_end->runtime_info;

    // then, we load in
    fs_head->runtime_info = info;
    fs_end->runtime_info = info;

    // unlock
    bitlocker_->unlock(0);
    bitlocker_->unlock(tailing_header_blk_id_);
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

    const auto static_info_crc64 = utils::arithmetic::hashcrc64(header_temp->static_info);
    const auto static_info_dup_checksum = utils::arithmetic::hashcrc64(header_temp->static_info_dup);
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
    cfs_header_block.bitlocker_ = &bitlocker_;
    *(uint64_t*)&cfs_header_block.tailing_header_blk_id_ = static_info_.blocks - 1;
    cfs_header_block.fs_head = header_temp;
    cfs_header_block.fs_end = header_temp_tail;
}

cfs::filesystem::~filesystem()
{
    file_.sync();
}
