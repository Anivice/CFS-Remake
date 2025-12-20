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
        auto & header_runtime_lock = parent_fs_governor_->cfs_header_block;
        const auto mirror1_checksum = mirror1.dump_crc64();
        const auto mirror2_checksum = mirror2.dump_crc64();
        if (header_runtime_lock.get_info("allocation_bitmap_checksum") == mirror1_checksum) {
            std::memcpy(
                // dest: mirror
                lock.data() + parent_fs_governor_->static_info_.data_bitmap_end * parent_fs_governor_->static_info_.block_size,
                // src: non mirror
                lock.data(),
                // length: map len
                (parent_fs_governor_->static_info_.data_bitmap_end - parent_fs_governor_->static_info_.data_bitmap_start) * parent_fs_governor_->static_info_.block_size);
        } else if (header_runtime_lock.get_info("allocation_bitmap_checksum") == mirror2_checksum) {
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

    auto & header_runtime = parent_fs_governor_->cfs_header_block;
    header_runtime.set_info("allocation_bitmap_checksum_cow", header_runtime.get_info("allocation_bitmap_checksum"));
    header_runtime.set_info("allocation_bitmap_checksum", mirror1.dump_crc64());
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

cfs::journal_auto_write_t::journal_auto_write_t(cfs_journaling_t *journal, bool &success,
    const uint64_t start_action,
    const uint64_t start_action_param0,
    const uint64_t start_action_param1,
    const uint64_t start_action_param2,
    const uint64_t start_action_param3,
    const uint64_t start_action_param4,

    const uint64_t success_action,
    const uint64_t success_action_param0,
    const uint64_t success_action_param1,
    const uint64_t success_action_param2,
    const uint64_t success_action_param3,
    const uint64_t success_action_param4,

    const uint64_t failed_action,
    const uint64_t failed_action_param0,
    const uint64_t failed_action_param1,
    const uint64_t failed_action_param2,
    const uint64_t failed_action_param3,
    const uint64_t failed_action_param4): 
journal_(journal), success_(success),
success_action_(success_action),

// success
success_action_param0_(success_action_param0),
success_action_param1_(success_action_param1),
success_action_param2_(success_action_param2),
success_action_param3_(success_action_param3),
success_action_param4_(success_action_param4),

// failed
failed_action_(failed_action),
failed_action_param0_(failed_action_param0),
failed_action_param1_(failed_action_param1),
failed_action_param2_(failed_action_param2),
failed_action_param3_(failed_action_param3),
failed_action_param4_(failed_action_param4)
{
    journal_->push_action(
        start_action,
        start_action_param0,
        start_action_param1,
        start_action_param2,
        start_action_param3,
        start_action_param4);
}

cfs::journal_auto_write_t::~journal_auto_write_t()
{
    if (success_) {
        journal_->push_action(
            success_action_,
            success_action_param0_,
            success_action_param1_,
            success_action_param2_,
            success_action_param3_,
            success_action_param4_);
    } else {
        journal_->push_action(
            failed_action_,
            failed_action_param0_,
            failed_action_param1_,
            failed_action_param2_,
            failed_action_param3_,
            failed_action_param4_);
    }
}

cfs::cfs_block_manager_t::cfs_block_manager_t(
    cfs_bitmap_block_mirroring_t *bitmap,
    filesystem::cfs_header_block_t *header,
    cfs_block_attribute_access_t *block_attribute,
    cfs_journaling_t *journal)
: bitmap_(bitmap), header_(header), block_attribute_(block_attribute), journal_(journal)
{
}

uint64_t cfs::cfs_block_manager_t::allocate()
{
    const auto static_info = header_->get_static_info();
    const auto map_size = static_info.data_table_end - static_info.data_table_start;
    bool operation_success = true;
    g_transaction(journal_, operation_success, GlobalTransaction_AllocateBlock);

    auto allocate_at_this_index = [&](const uint64_t index)
    {
        bitmap_->set_bit(index, true);
        block_attribute_->set(index, {});
    };

    auto refresh_allocate = [&](bool & success, const uint64_t start, const uint64_t end)
    {
        /// start from 0
        for (uint64_t i = start; i < end; ++i)
        {
            if (!bitmap_->get_bit(i)) {
                /// get one
                /// record last allocated position
                header_->set_info("last_allocated_block", i);
                // allocate
                allocate_at_this_index(i);
                success = true;
                return i;
            }
        }

        success = false;
        return 0ul;
    };

    auto fully_allocate_by_running_through = [&]->uint64_t
    {
        bool success;
        if (const uint64_t ret = refresh_allocate(success, 0, map_size); !success)
        {
            // not found
            /// Record OOM refresh
            for (uint64_t i = 0; i < map_size; ++i)
            {
                if (bitmap_->get_bit(i)) {
                    auto attr = block_attribute_->get(i);
                    attr.allocation_oom_scan_per_refresh_count++;
                    block_attribute_->set(i, attr);
                }
            }

            /// deallocate oldest redundancies
            std::vector < std::pair <uint64_t, uint64_t > > pair_block_w_refresh;
            for (uint64_t i = 0; i < map_size; ++i)
            {
                if (bitmap_->get_bit(i)) {
                    const auto attr = block_attribute_->get(i);
                    if (attr.block_type == CowRedundancy) {
                        pair_block_w_refresh.emplace_back(i, static_cast<uint64_t>(attr.index_node_referencing_number));
                    }
                }
            }

            /// sort
            std::ranges::sort(pair_block_w_refresh,
                              [](const std::pair <uint64_t, uint64_t > & a, const std::pair <uint64_t, uint64_t > & b)->bool
                              { return a.second > b.second; });

            /// remove oldest ones
            if (!pair_block_w_refresh.empty())
            {
                const auto oldest = pair_block_w_refresh.front().second;
                for (uint64_t i = 0; i < pair_block_w_refresh.size(); ++i)
                {
                    if (pair_block_w_refresh[i].second == oldest) {
                        deallocate(i);
                    }
                }
            }

            // run the allocator again
            if (const auto ret_again = refresh_allocate(success, 0, map_size); !success) {
                journal_->push_action(FilesystemBlockExhausted);
                // still, no free blocks, report filesystem as fully occupied
                operation_success = false;
                throw error::no_more_free_spaces();
            } else {
                return ret_again; // found one
            }
        }
        else {
            return ret; // found
        }
    };

    if (header_->get_info("last_allocated_block") + 1 < map_size)
    {
        bool success;
        if (const auto ret = refresh_allocate(
            success,
            header_->get_info("last_allocated_block") + 1,
            map_size); success)
        {
            return ret;
        }
    }

    return fully_allocate_by_running_through();
}
