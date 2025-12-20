#include "cfsBasicComponents.h"

void cfs::cfs_journaling_t::putc(const char c)
{
    journal_body_[journal_header_->head] = c;
    journal_header_->head = (journal_header_->head + 1) % capacity_;
    if (journal_header_->size < capacity_) {
        ++journal_header_->size;
    } else {
        // buffer full - discard the oldest element
        journal_header_->tail = (journal_header_->tail + 1) % capacity_;
    }
}

std::vector<uint8_t> cfs::cfs_journaling_t::dump() const
{
    std::vector<uint8_t> out;
    out.reserve(journal_header_->size);
    for (size_t i = 0; i < journal_header_->size; ++i) {
        const size_t index = (journal_header_->tail + i) % capacity_;
        out.push_back(journal_body_[index]);
    }
    return out;
}

cfs::cfs_journaling_t::cfs_journaling_t(cfs::filesystem *parent_fs_governor):
    parent_fs_governor_(parent_fs_governor),
    journal_start_(parent_fs_governor->static_info_.journal_start),
    journal_end_(parent_fs_governor->static_info_.journal_end),
    block_size_(parent_fs_governor->static_info_.block_size)
{
    for (auto i = parent_fs_governor->static_info_.journal_start; i < parent_fs_governor->static_info_.journal_end; i++) {
        parent_fs_governor->bitlocker_.lock(i);
    }

    journal_raw_buffer_ = parent_fs_governor->file_.data() + journal_start_ * block_size_;
    journal_body_ = journal_raw_buffer_ + sizeof(journal_header_t);
    journal_header_ = (journal_header_t*)journal_raw_buffer_;
    journal_header_cow_ = (journal_header_t*)(parent_fs_governor->file_.data() + journal_end_ * block_size_ - sizeof(journal_header_t));
    *(uint64_t*)&capacity_ = (journal_end_ - journal_start_) * block_size_ - (sizeof(journal_header_t) * 2);
}

cfs::cfs_journaling_t::~cfs_journaling_t()
{
    for (auto i = parent_fs_governor_->static_info_.journal_start;
        i < parent_fs_governor_->static_info_.journal_end; i++)
    {
        parent_fs_governor_->bitlocker_.unlock(i);
    }
}

std::vector<cfs::cfs_action_t> cfs::cfs_journaling_t::dump_actions()
{
    std::lock_guard<std::mutex> guard(mutex_);
    std::vector < cfs_action_t > actions;
    auto data = dump();
    for (uint64_t i = 0; i < data.size();)
    {
        if (const auto * action = reinterpret_cast<cfs_action_t *>(data.data() + i);
            action->cfs_magic == cfs_magick_number)
        {
            actions.push_back(*action);
            i += sizeof(cfs_action_t);
        } else {
            i++;
        }
    }

    return actions;
}

void cfs::cfs_journaling_t::push_action(
    const uint64_t action,
    const uint64_t action_param0,
    const uint64_t action_param1,
    const uint64_t action_param2,
    const uint64_t action_param3,
    const uint64_t action_param4)
{
    std::lock_guard<std::mutex> guard(mutex_);
    cfs_action_t j_action = { };
    j_action.cfs_magic = cfs_magick_number;
    j_action.action_data.action_plain = {
        .action = action,
        .action_param0 = action_param0,
        .action_param1 = action_param1,
        .action_param2 = action_param2,
        .action_param3 = action_param3,
        .action_param4 = action_param4,
    };
    j_action.action_param_crc64 = cfs::utils::arithmetic::hashcrc64((uint8_t*)&j_action.action_data, sizeof(j_action.action_data));
    *journal_header_cow_ = *journal_header_;
    for (int i = 0; i < sizeof(j_action); i++) {
        putc(reinterpret_cast<const char *>(&j_action)[i]);
    }
}

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

cfs::cfs_bitmap_block_mirroring_t::cfs_bitmap_block_mirroring_t(cfs::filesystem *parent_fs_governor, cfs_journaling_t * journal)
:
    mirror1(parent_fs_governor->file_.data() + parent_fs_governor->static_info_.data_table_start * parent_fs_governor->static_info_.block_size,
        parent_fs_governor->static_info_.data_table_end - parent_fs_governor->static_info_.data_table_start),
    mirror2(parent_fs_governor->file_.data()+ parent_fs_governor->static_info_.data_bitmap_backup_start * parent_fs_governor->static_info_.block_size,
            parent_fs_governor->static_info_.data_table_end - parent_fs_governor->static_info_.data_table_start),
    parent_fs_governor_(parent_fs_governor),
    journal_(journal)
{
}

bool cfs::cfs_bitmap_block_mirroring_t::get_bit(const uint64_t index)
{
    // huge multipage state lock
    auto lock = parent_fs_governor_->lock(
        parent_fs_governor_->static_info_.data_bitmap_start,
        parent_fs_governor_->static_info_.data_bitmap_backup_end - 1); // [start, end)
    const auto map1 = mirror1.get_bit(index, false);
    const auto map2 = mirror2.get_bit(index, false);
    if (map1 != map2)
    {
        elog("Filesystem bitmap corrupted at runtime, fsck needed!\n");
        journal_->push_action(CorruptionDetected, BitmapMirrorInconsistent);
        const auto header_runtime_lock = parent_fs_governor_->cfs_header_block.make_lock();
        const auto mirror1_checksum = mirror1.dump_crc64();
        const auto mirror2_checksum = mirror2.dump_crc64();
        const auto header_runtime_info = header_runtime_lock.load();
        if (header_runtime_info.allocation_bitmap_checksum == mirror1_checksum) {
            std::memcpy(
                // dest: mirror
                lock.data() + parent_fs_governor_->static_info_.data_bitmap_end * parent_fs_governor_->static_info_.block_size,
                // src: non mirror
                lock.data(),
                // length: map len
                (parent_fs_governor_->static_info_.data_bitmap_end - parent_fs_governor_->static_info_.data_bitmap_start) * parent_fs_governor_->static_info_.block_size);
        } else if (header_runtime_info.allocation_bitmap_checksum == mirror2_checksum) {
            std::memcpy(
                // src: non mirror
                lock.data(),
                // dest: mirror
                lock.data() + parent_fs_governor_->static_info_.data_bitmap_end * parent_fs_governor_->static_info_.block_size,
                // length: map len
                (parent_fs_governor_->static_info_.data_bitmap_end - parent_fs_governor_->static_info_.data_bitmap_start) * parent_fs_governor_->static_info_.block_size);
        } else {
            // both fucked
            elog("Cannot recover from fatal fault");
            throw cfs::error::filesystem_head_corrupt_and_unable_to_recover();
        }

        journal_->push_action(AttemptedFixFinishedAndAssumedFine, BitmapMirrorInconsistent);
        wlog("Attempted fix finished and assumed fine\n");
    }

    // if (const auto ptr = debug_map_.find(index); ptr != debug_map_.end()) cfs_assert_simple(ptr->second == map1);

    return map1;
}

void cfs::cfs_bitmap_block_mirroring_t::set_bit(const uint64_t index, const bool new_bit)
{
    const auto original = this->get_bit(index);
    journal_->push_action(FilesystemBitmapModification, original, new_bit, index);
    // huge multipage state lock
    auto lock = parent_fs_governor_->lock(
        parent_fs_governor_->static_info_.data_bitmap_start,
        parent_fs_governor_->static_info_.data_bitmap_backup_end - 1); // [start, end)

    // debug_map_[index] = new_bit;

    mirror1.set_bit(index, new_bit, false);
    mirror2.set_bit(index, new_bit, false);

    const auto header_runtime_lock = parent_fs_governor_->cfs_header_block.make_lock();
    auto header_runtime_info = header_runtime_lock.load();
    header_runtime_info.allocation_bitmap_checksum_cow = header_runtime_info.allocation_bitmap_checksum;
    header_runtime_info.allocation_bitmap_checksum = mirror1.dump_crc64();
    header_runtime_lock.set(header_runtime_info);

    journal_->push_action(ActionFinishedAndNoExceptionCaughtDuringTheOperation);
}

cfs::cfs_block_attribute_access_t::cfs_block_attribute_access_t(filesystem *parent_fs_governor,
    cfs_journaling_t *journal): parent_fs_governor_(parent_fs_governor), journal_(journal)
{
}

cfs::cfs_block_attribute_t cfs::cfs_block_attribute_access_t::get(const uint64_t index)
{
    const auto offset = index * sizeof(cfs_block_attribute_t);
    const auto in_page_offset = offset % parent_fs_governor_->static_info_.block_size;
    const auto page = offset / parent_fs_governor_->static_info_.block_size +
                      parent_fs_governor_->static_info_.data_block_attribute_table_start;
    cfs_block_attribute_t ret {};
    const auto lock = parent_fs_governor_->lock(page);
    std::memcpy(&ret, lock.data() + in_page_offset, sizeof(ret));
    return ret;
}

void cfs::cfs_block_attribute_access_t::set(const uint64_t index, const cfs_block_attribute_t attr)
{
    const auto offset = index * sizeof(cfs_block_attribute_t);
    const auto in_page_offset = offset % parent_fs_governor_->static_info_.block_size;
    const auto page = offset / parent_fs_governor_->static_info_.block_size +
                      parent_fs_governor_->static_info_.data_block_attribute_table_start;
    cfs_block_attribute_t original {};
    const auto lock = parent_fs_governor_->lock(page);
    std::memcpy(&original, lock.data() + in_page_offset, sizeof(original));
    journal_->push_action(FilesystemAttributeModification,
                          *reinterpret_cast<uint32_t *>(&original),
                          *reinterpret_cast<const uint32_t *>(&attr),
                          index);
    std::memcpy(lock.data() + in_page_offset, &attr, sizeof(attr));
    journal_->push_action(ActionFinishedAndNoExceptionCaughtDuringTheOperation);
}
