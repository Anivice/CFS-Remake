#include "cfsBasicComponents.h"

cfs::cfs_bitmap_singular_t::cfs_bitmap_singular_t(char *mapped_area, const uint64_t data_block_numbers)
{
    init_data_array = [&](const uint64_t)->bool
    {
        data_array_ = (uint8_t*)mapped_area;
        return true;
    };

    cfs::bitmap_base::init(data_block_numbers);
}

uint64_t cfs::cfs_bitmap_singular_t::dump_crc64() const
{
    return cfs::utils::arithmetic::hashcrc64(data_array_, bytes_required_);
}

cfs::cfs_bitmap_block_mirroring_t::cfs_bitmap_block_mirroring_t(cfs::filesystem *parent_fs_governor)
:
    mirror1(parent_fs_governor->file_.data() + parent_fs_governor->static_info_.data_table_start * parent_fs_governor->static_info_.block_size,
        parent_fs_governor->static_info_.data_table_end - parent_fs_governor->static_info_.data_table_start),
    mirror2(parent_fs_governor->file_.data()+ parent_fs_governor->static_info_.data_bitmap_backup_start * parent_fs_governor->static_info_.block_size,
            parent_fs_governor->static_info_.data_table_end - parent_fs_governor->static_info_.data_table_start),
    parent_fs_governor_(parent_fs_governor)
{
}

bool cfs::cfs_bitmap_block_mirroring_t::get_bit(const uint64_t index)
{
    // huge multipage state lock
    auto lock = parent_fs_governor_->lock(
        parent_fs_governor_->static_info_.data_bitmap_start,
        parent_fs_governor_->static_info_.data_bitmap_backup_end - 1); // [start, end)
    const auto map1 = mirror1.get_bit(index);
    const auto map2 = mirror2.get_bit(index);
    if (map1 != map2) {
        elog("Filesystem bitmap corrupted at runtime, fsck needed!\n");
    }

    return map1;
}

void cfs::cfs_bitmap_block_mirroring_t::set_bit(const uint64_t index, const bool new_bit)
{
    // huge multipage state lock
    auto lock = parent_fs_governor_->lock(
        parent_fs_governor_->static_info_.data_bitmap_start,
        parent_fs_governor_->static_info_.data_bitmap_backup_end - 1); // [start, end)
    mirror1.set_bit(index, new_bit);
    mirror2.set_bit(index, new_bit);

    const auto header_runtime_lock = parent_fs_governor_->cfs_header_block.make_lock();
    auto header_runtime_info = header_runtime_lock.load();
    header_runtime_info.allocation_bitmap_checksum_cow = header_runtime_info.allocation_bitmap_checksum;
    header_runtime_info.allocation_bitmap_checksum = mirror1.dump_crc64();
    header_runtime_lock.set(header_runtime_info);
}
