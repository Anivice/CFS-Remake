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
    j_action.action_param_crc64 = cfs::utils::arithmetic::hash64((uint8_t*)&j_action.action_data, sizeof(j_action.action_data));
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

// uint64_t cfs::cfs_bitmap_singular_t::dump_checksum64()
// {
    // std::lock_guard lock_(this->overall_mtx_);
    // return cfs::utils::arithmetic::hash64(data_array_, bytes_required_);
// }

cfs::cfs_bitmap_block_mirroring_t::cfs_bitmap_block_mirroring_t(cfs::filesystem *parent_fs_governor, cfs_journaling_t * journal)
:
    mirror1(parent_fs_governor->file_.data() + parent_fs_governor->static_info_.data_bitmap_start * parent_fs_governor->static_info_.block_size,
        parent_fs_governor->static_info_.data_table_end - parent_fs_governor->static_info_.data_table_start),
    mirror2(parent_fs_governor->file_.data() + parent_fs_governor->static_info_.data_bitmap_backup_start * parent_fs_governor->static_info_.block_size,
            parent_fs_governor->static_info_.data_table_end - parent_fs_governor->static_info_.data_table_start),
    parent_fs_governor_(parent_fs_governor),
    journal_(journal)
{
}

bool cfs::cfs_bitmap_block_mirroring_t::get_bit(const uint64_t index)
{
    cfs_assert_simple(index < (parent_fs_governor_->static_info_.data_table_end - parent_fs_governor_->static_info_.data_table_start))
    // if (const auto result = small_cache_.get_fast_cache(index); result != -1) {
        // return result & 0x01;
    // }

    // huge multipage state lock (really slow, so it'd better be in cache pool)
    const auto page_bare = index / (parent_fs_governor_->static_info_.block_size * 8);
    const auto bitmap_01 = parent_fs_governor_->static_info_.data_bitmap_start + page_bare;
    const auto bitmap_02 = parent_fs_governor_->static_info_.data_bitmap_backup_start + page_bare;
    bool map1, map2;
    {
        auto lock1 = parent_fs_governor_->lock(bitmap_01);
        auto lock2 = parent_fs_governor_->lock(bitmap_02);
        map1 = mirror1.get_bit(index, false);
        map2 = mirror2.get_bit(index, false);
    }
    if (map1 != map2)
    {
        elog("Filesystem bitmap corrupted at runtime, fsck needed!\n");
        journal_->push_action(CorruptionDetected, BitmapMirrorInconsistent);
        elog("Cannot recover from fatal fault\n");
        throw cfs::error::filesystem_head_corrupt_and_unable_to_recover();
    }

    // small_cache_.set_fast_cache(index, map1);
    return map1;
}

void cfs::cfs_bitmap_block_mirroring_t::set_bit(const uint64_t index, const bool new_bit)
{
    cfs_assert_simple(index < (parent_fs_governor_->static_info_.data_table_end - parent_fs_governor_->static_info_.data_table_start))
    // if (const auto result = small_cache_.get_fast_cache(index); result != -1) {
        // if (new_bit == (result & 0x01)) return;
    // }

    bool success = false;
    const auto original = this->get_bit(index);
    g_transaction(journal_, success, FilesystemBitmapModification, original, new_bit, index);
    // journal_->push_action(FilesystemBitmapModification, original, new_bit, index);
    const auto page_bare = index / (parent_fs_governor_->static_info_.block_size * 8);
    const auto bitmap_01 = parent_fs_governor_->static_info_.data_bitmap_start + page_bare;
    const auto bitmap_02 = parent_fs_governor_->static_info_.data_bitmap_backup_start + page_bare;
    auto lock1 = parent_fs_governor_->lock(bitmap_01);
    auto lock2 = parent_fs_governor_->lock(bitmap_02);
    mirror1.set_bit(index, new_bit, false);
    mirror2.set_bit(index, new_bit, false);

    // small_cache_.set_fast_cache(index, new_bit);

    auto & header_runtime = parent_fs_governor_->cfs_header_block;
    // header_runtime.set_info<allocation_bitmap_checksum_cow>(header_runtime.get_info<allocation_bitmap_checksum>());
    // header_runtime.set_info<allocation_bitmap_checksum>(mirror1.dump_checksum64());
    // journal_->push_action(ActionFinishedAndNoExceptionCaughtDuringTheOperation);
    success = true;
}

cfs::cfs_block_attribute_access_t::cfs_block_attribute_access_t(filesystem *parent_fs_governor,
    cfs_journaling_t *journal): parent_fs_governor_(parent_fs_governor), journal_(journal)
{
    *(uint64_t*)&location_lock_.blocks_ = parent_fs_governor->static_info_.data_table_end - parent_fs_governor->static_info_.data_table_start;
    location_lock_.init();
}

cfs::cfs_block_attribute_access_t::smart_lock_t::smart_lock_t(
    cfs_block_attribute_access_t *parent,
    const uint64_t index)
:
    parent_(parent), index_(index)
{
    parent->location_lock_.lock(index);
    const auto offset = index * sizeof(cfs_block_attribute_t);
    const auto in_page_offset = offset % parent->parent_fs_governor_->static_info_.block_size;
    const auto page = offset / parent->parent_fs_governor_->static_info_.block_size +
                      parent->parent_fs_governor_->static_info_.data_block_attribute_table_start;
    cfs_assert_simple(parent->parent_fs_governor_->static_info_.data_block_attribute_table_start <= page
        && page < parent->parent_fs_governor_->static_info_.data_block_attribute_table_end);
    const auto pg_data = parent->parent_fs_governor_->lock(page);
    data_ = (cfs_block_attribute_t*)(void*)(pg_data.data() + in_page_offset);
    before_ = *data_;
}

cfs::cfs_block_attribute_access_t::smart_lock_t::~smart_lock_t()
{
    parent_->location_lock_.unlock(index_);
}

// cfs::cfs_block_attribute_t cfs::cfs_block_attribute_access_t::get(const uint64_t index)
// {
//     const auto offset = index * sizeof(cfs_block_attribute_t);
//     const auto in_page_offset = offset % parent_fs_governor_->static_info_.block_size;
//     const auto page = offset / parent_fs_governor_->static_info_.block_size +
//                       parent_fs_governor_->static_info_.data_block_attribute_table_start;
//     cfs_block_attribute_t ret {};
//     const auto lock = parent_fs_governor_->lock(page);
//     std::memcpy(&ret, lock.data() + in_page_offset, sizeof(ret));
//     return ret;
// }

// void cfs::cfs_block_attribute_access_t::set(const uint64_t index, const cfs_block_attribute_t attr)
// {
//     const auto offset = index * sizeof(cfs_block_attribute_t);
//     const auto in_page_offset = offset % parent_fs_governor_->static_info_.block_size;
//     const auto page = offset / parent_fs_governor_->static_info_.block_size +
//                       parent_fs_governor_->static_info_.data_block_attribute_table_start;
//     cfs_block_attribute_t original {};
//     const auto lock = parent_fs_governor_->lock(page);
//     std::memcpy(&original, lock.data() + in_page_offset, sizeof(original));
//     journal_->push_action(FilesystemAttributeModification,
//                           *reinterpret_cast<uint32_t *>(&original),
//                           *reinterpret_cast<const uint32_t *>(&attr),
//                           index);
//     std::memcpy(lock.data() + in_page_offset, &attr, sizeof(attr));
//     journal_->push_action(ActionFinishedAndNoExceptionCaughtDuringTheOperation);
// }

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
        block_attribute_->clear(index, { .newly_allocated_thus_no_cow = 1 });
    };

    auto refresh_allocate = [&](bool & success, const uint64_t start, const uint64_t end)
    {
        /// start from 0
        for (uint64_t i = start; i < end; ++i)
        {
            if (!bitmap_->get_bit(i)) {
                /// get one
                /// record last allocated position
                header_->set_info<last_allocated_block>(i);
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
                    block_attribute_->inc<allocation_oom_scan_per_refresh_count>(i);
                }
            }

            /// deallocate oldest redundancies
            uint64_t oldest = 0;
            std::map < uint64_t, uint64_t > scanned_cow_blocks;
            for (uint64_t i = 0; i < map_size; ++i)
            {
                if (bitmap_->get_bit(i))
                {
                    if (block_attribute_->get<block_type>(i) == CowRedundancy) {
                        const auto refresh = block_attribute_->get<allocation_oom_scan_per_refresh_count>(i);
                        if (oldest < refresh) oldest = refresh;
                        scanned_cow_blocks[i] = refresh;
                    }
                }
            }

            /// remove oldest ones
            std::ranges::for_each(scanned_cow_blocks, [&](const std::pair<uint64_t, uint64_t> & block)
            {
                if (block.second >= oldest / 2) {
                    deallocate(block.first);
                }
            });

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

    if (header_->get_info<last_allocated_block>() + 1 < map_size)
    {
        bool success;
        if (const auto ret = refresh_allocate(
            success,
            header_->get_info<last_allocated_block>() + 1,
            map_size); success)
        {
            return ret;
        }
    }

    return fully_allocate_by_running_through();
}

void cfs::cfs_block_manager_t::deallocate(const uint64_t index)
{
    cfs_assert_simple(index != 0);
    bool success = false;
    g_transaction(journal_, success, GlobalTransaction_DeallocateBlock, index);
    bitmap_->set_bit(index, false);
    success = true;
}

uint64_t cfs::cfs_inode_service_t::copy_on_write(const uint64_t index, const bool linker)
{
    cfs_assert_simple(index != block_index_); // can't CoW on my own. this should be done by dentry
    if (!block_attribute_->get<newly_allocated_thus_no_cow>(index))
    {
        bool success = false;
        const auto new_block = block_manager_->allocate();
        block_attribute_->clear(new_block, {
            .block_status = BLOCK_AVAILABLE_TO_MODIFY_0x00,
            .block_type = STORAGE_BLOCK,
            .block_type_cow = 0,
            .allocation_oom_scan_per_refresh_count = 0,
            .newly_allocated_thus_no_cow = 0,
            .index_node_referencing_number = 0,
            .block_checksum = 0
        });
        g_transaction(journal_, success, GlobalTransaction_CreateRedundancy, index, new_block);
        const auto new_ = lock_page(new_block, linker);
        const auto old_ = lock_page(index, linker);
        std::memcpy(new_->data(), old_->data(), block_size_);

        block_attribute_->move<block_type, block_type_cow>(index); // move block type in old one to cow backup
        block_attribute_->set<block_type>(index, COW_REDUNDANCY_BLOCK); // mark the old one aas freeable CoW redundancy
        success = true;
        return new_block;
    }

    block_attribute_->set<newly_allocated_thus_no_cow>(index, 0);
    return index;
}

cfs::cfs_inode_service_t::linearized_block_t cfs::cfs_inode_service_t::linearize_all_blocks()
{
    const auto descriptor = size_to_linearized_block_descriptor(this->cfs_inode_attribute->st_size);
    std::vector < uint64_t > level1_pointers;

    for (uint64_t i = 0; i < descriptor.level1_pointers; i++) {
        level1_pointers.push_back(this->cfs_level_1_indexes[i]);
    }

    auto read_lower_by_upper_w_size = [this](
        const uint64_t pointers,
        const std::vector < uint64_t > & upper)->std::vector < uint64_t >
    {
        std::vector < uint64_t > ret;
        uint64_t index = 0;
        std::ranges::for_each(upper, [&](const uint64_t pointer)
        {
            const auto lock = lock_page(pointer, true);
            const auto length = std::min(lock->size(), (pointers - ret.size()) * sizeof(uint64_t));
            std::vector < uint64_t > new_data;
            new_data.resize(length / sizeof(uint64_t), 0);
            std::memcpy(new_data.data(), lock->data(), length);
            ret.insert_range(ret.end(), new_data);
            index++;
        });

        return ret;
    };

    const std::vector<uint64_t> level2_pointers = read_lower_by_upper_w_size(descriptor.level2_pointers, level1_pointers);
    const std::vector<uint64_t> level3_pointers = read_lower_by_upper_w_size(descriptor.level3_pointers, level2_pointers);

    return {
        .level1_pointers = level1_pointers,
        .level2_pointers = level2_pointers,
        .level3_pointers = level3_pointers
    };
}

cfs::cfs_inode_service_t::linearized_block_descriptor_t
cfs::cfs_inode_service_t::size_to_linearized_block_descriptor(const uint64_t size) const
{
    auto blocks_requires_for_this_many_pointers = [&](const uint64_t pointers)->uint64_t {
        return cfs::utils::arithmetic::count_cell_with_cell_size(
            block_size_ / sizeof(uint64_t), // how many pointers can a storage hold?
            pointers); // this many pointers
    };
    linearized_block_t linearized_block;
    /// basically, how many storage blocks, i.e., level 3 blocks
    const uint64_t required_level3_blocks = cfs::utils::arithmetic::count_cell_with_cell_size(block_size_, size);
    const uint64_t required_level2_blocks = blocks_requires_for_this_many_pointers(required_level3_blocks);
    const uint64_t required_level1_blocks = blocks_requires_for_this_many_pointers(required_level2_blocks);
    if (required_level1_blocks > cfs_level_1_index_numbers) {
        std::cerr << "File size overload!" << std::endl;
        throw error::no_more_free_spaces("Max file size more than inode can hold");
    }

    return {
        .level1_pointers = required_level1_blocks,
        .level2_pointers = required_level2_blocks,
        .level3_pointers = required_level3_blocks
    };
}

cfs::cfs_inode_service_t::allocation_map_t
cfs::cfs_inode_service_t::reallocate_linearized_block_by_descriptor(const linearized_block_descriptor_t &descriptor)
{
    // copy_on_write(block_index_); // cow on inode TODO: Do this in dentry, now inode
    auto alloc = [&](const uint8_t blk_type)->uint64_t
    {
        parent_fs_governor_->cfs_header_block.inc<allocated_non_cow_blocks>();
        const auto blk = block_manager_->allocate();
        block_attribute_->clear(blk,
         {
            .block_status = 0,
            .block_type = blk_type,
            .block_type_cow = 0,
            .allocation_oom_scan_per_refresh_count = 0,
            .newly_allocated_thus_no_cow = 1,
            .index_node_referencing_number = 1,
            .block_checksum = 0, // just 0, newly_allocated_thus_no_cow indicated this block hasn't been modified
        });
        return blk;
    };

    auto dealloc = [&](const uint64_t index)
    {
        parent_fs_governor_->cfs_header_block.dec<allocated_non_cow_blocks>();
        return block_manager_->deallocate(index);
    };

    using smart_reallocate_func_t = smart_reallocate_t<decltype(alloc), decltype(dealloc)>;

    auto set_control = [](std::vector<std::unique_ptr<smart_reallocate_func_t>> & ctrl, const bool control)
    {
        std::ranges::for_each(ctrl, [&](const std::unique_ptr<smart_reallocate_func_t> & relc) {
            relc->control_ = control;
        });
    };

    auto register_into_list = [&](const std::vector < uint64_t > & list, const uint8_t type)->
    std::vector<std::unique_ptr<smart_reallocate_func_t>>
    {
        std::vector<std::unique_ptr<smart_reallocate_func_t>> ret;
        std::ranges::for_each(list, [&](const uint64_t blk) {
            ret.emplace_back(std::make_unique<smart_reallocate_func_t>(alloc, dealloc, blk, true, type));
        });

        return ret;
    };

    auto allocate_automatically = [&](std::vector<std::unique_ptr<smart_reallocate_func_t>> & list, const uint8_t type)->void
    {
        std::ranges::for_each(list, [&](std::unique_ptr<smart_reallocate_func_t> & relc) {
            if (relc == nullptr) {
                relc = std::make_unique<smart_reallocate_func_t>(alloc, dealloc, type);
            }
        });
    };

    auto make_return = [](const std::vector<std::unique_ptr<smart_reallocate_func_t>> & lv)->std::vector<std::pair<uint64_t, bool>>
    {
        std::vector<std::pair<uint64_t, bool>> ret;
        ret.reserve(lv.size());
        std::ranges::for_each(lv, [&](const std::unique_ptr<smart_reallocate_func_t> & blk) {
            ret.emplace_back(blk->block_index_, blk->allocated_by_smart_);
        });
        return ret;
    };

    auto [level1_vec, level2_vec, level3_vec] = linearize_all_blocks();

    std::vector<std::unique_ptr<smart_reallocate_func_t>> level1 = register_into_list(level1_vec, POINTER_BLOCK);
    std::vector<std::unique_ptr<smart_reallocate_func_t>> level2 = register_into_list(level2_vec, POINTER_BLOCK);
    std::vector<std::unique_ptr<smart_reallocate_func_t>> level3 = register_into_list(level3_vec, STORAGE_BLOCK);

    level1.resize(descriptor.level1_pointers);
    level2.resize(descriptor.level2_pointers);
    level3.resize(descriptor.level3_pointers);

    allocate_automatically(level1, POINTER_BLOCK);
    allocate_automatically(level2, POINTER_BLOCK);
    allocate_automatically(level3, STORAGE_BLOCK);

    set_control(level1, false);
    set_control(level2, false);
    set_control(level3, false);

    const std::vector<std::pair<uint64_t, bool>>
        level1_ret = make_return(level1),
        level2_ret = make_return(level2),
        level3_ret = make_return(level3);

    return { .level1_pointers = level1_ret, .level2_pointers = level2_ret, .level3_pointers = level3_ret };
}

void cfs::cfs_inode_service_t::commit_from_linearized_block(allocation_map_t descriptor)
{
    auto record_from_lower_to_upper = [&](
        std::vector<std::pair<uint64_t, bool>> & upper,
        const std::vector<std::pair<uint64_t, bool>> & lower)
    {
        std::vector<uint64_t> block_data; // upper level block data
        block_data.reserve(block_size_ / sizeof(uint64_t));
        uint64_t block_offset = 0; // upper level offset
        bool found_allocator = false;

        auto save = [&]
        {
            const auto parent_blk = upper[block_offset].first;
            if (found_allocator)
            {
                const auto new_parent = copy_on_write(parent_blk, true);
                const auto parent_blk_lock = lock_page(new_parent, true);
                if (new_parent != parent_blk)
                {
                    block_attribute_->set<block_type>(new_parent, POINTER_BLOCK);
                    upper[block_offset].second = true;
                    upper[block_offset].first = new_parent;
                }
                std::memcpy(parent_blk_lock->data(), block_data.data(), block_data.size() * sizeof(uint64_t));
                block_attribute_->set<newly_allocated_thus_no_cow>(parent_blk, 0);
            }

            block_data.clear();
            found_allocator = false;
            block_offset++;
        };

        std::ranges::for_each(lower, [&](const std::pair<uint64_t, bool> & relc)
        {
            block_data.push_back(relc.first);
            if (relc.second) found_allocator = true;

            // block data full
            if (block_data.size() == (block_size_ / sizeof(uint64_t))) {
                save();
            }
        });

        if (!block_data.empty()) {
            save();
        }
    };

    record_from_lower_to_upper(descriptor.level2_pointers, descriptor.level3_pointers);
    record_from_lower_to_upper(descriptor.level1_pointers, descriptor.level2_pointers);

    // record level 1 -> inode
    {
        int offset = 0;
        std::ranges::for_each(descriptor.level1_pointers, [&](const std::pair<uint64_t, bool> & relc) {
            this->cfs_level_1_indexes[offset++] = relc.first;
        });
    }
}

void cfs::cfs_inode_service_t::commit_from_block_descriptor(const linearized_block_descriptor_t &descriptor)
{
    return commit_from_linearized_block(reallocate_linearized_block_by_descriptor(descriptor));
}

cfs::cfs_inode_service_t::cfs_inode_service_t(
    const uint64_t index,
    filesystem *parent_fs_governor,
    cfs_block_manager_t *block_manager,
    cfs_journaling_t *journal,
    cfs_block_attribute_access_t *block_attribute)
:
    inode_effective_lock_(parent_fs_governor->lock(index + parent_fs_governor->static_info_.data_table_start)),
    parent_fs_governor_(parent_fs_governor),
    block_manager_(block_manager),
    journal_(journal),
    block_attribute_(block_attribute),
    block_size_(parent_fs_governor->static_info_.block_size),
    block_index_(index)
{
    convert(inode_effective_lock_.data(), parent_fs_governor->static_info_.block_size);
    before_.resize(inode_effective_lock_.size());
    std::memcpy(before_.data(), inode_effective_lock_.data(), inode_effective_lock_.size());
}

cfs::cfs_inode_service_t::~cfs_inode_service_t()
{
    if (!!std::memcpy(before_.data(), inode_effective_lock_.data(), inode_effective_lock_.size())) {
        block_attribute_->set<block_checksum>(block_index_,
            utils::arithmetic::hash5(reinterpret_cast<uint8_t *>(inode_effective_lock_.data()), inode_effective_lock_.size())
        );
    }
}

uint64_t cfs::cfs_inode_service_t::read(char * data, const uint64_t size, const uint64_t offset)
{
    if (this->cfs_inode_attribute->st_size == 0) return 0; // skip read if size is 0
    const auto [level1, level2, level3] = linearize_all_blocks();
    const auto skipped_blocks = offset / block_size_;
    const auto skipped_bytes = offset % block_size_;
    const auto bytes_to_read_in_the_first_block = std::min(size - skipped_bytes, block_size_ - skipped_bytes);
    const auto bytes_to_read_in_the_following_blocks = size - bytes_to_read_in_the_first_block;
    const auto adjacent_full_blocks = bytes_to_read_in_the_following_blocks / block_size_;
    const auto bytes_to_read_in_the_last_block = bytes_to_read_in_the_following_blocks % block_size_;
    uint64_t global_read_offset = 0;
    auto copy_to_buffer = [&](const void * ptr, const uint64_t r_size)
    {
        std::memcpy(data + global_read_offset, ptr, r_size);
        global_read_offset += r_size;
    };

    // read first page
    {
        const auto lock1 = lock_page(level3[skipped_blocks]);
        copy_to_buffer(lock1->data(), bytes_to_read_in_the_first_block);
    }

    // read continuous
    for (uint64_t i = 1; i <= adjacent_full_blocks; i++) {
        const auto lock = lock_page(level3[skipped_blocks + i]);
        copy_to_buffer(lock->data(), block_size_);
    }

    // read tail
    if (bytes_to_read_in_the_last_block != 0) {
        const auto lock = lock_page(level3[skipped_blocks + adjacent_full_blocks + 1]);
        copy_to_buffer(lock->data(), bytes_to_read_in_the_last_block);
    }

    return global_read_offset;
}

uint64_t cfs::cfs_inode_service_t::write(const char *data, const uint64_t size, const uint64_t offset)
{
    bool success = false;
    g_transaction(journal_, success, GlobalTransaction_Major_WriteInode, offset, size);
    if (this->cfs_inode_attribute->st_size < (size + offset)) {
        resize(size + offset); // append when short
    }

    const auto [level1, level2, level3] = linearize_all_blocks();
    allocation_map_t allocation_descriptor;
    tsl::hopscotch_map<uint64_t, uint64_t> relink_map;
    const auto skipped_blocks = offset / block_size_;
    const auto skipped_bytes = offset % block_size_;
    const auto bytes_to_write_in_the_first_block = std::min(size - skipped_bytes, block_size_ - skipped_bytes);
    const auto bytes_to_write_in_the_following_blocks = size - bytes_to_write_in_the_first_block;
    const auto adjacent_full_blocks = bytes_to_write_in_the_following_blocks / block_size_;
    const auto bytes_to_write_in_the_last_block = bytes_to_write_in_the_following_blocks % block_size_;
    uint64_t global_write_offset = 0;
    auto copy_to_buffer = [&](void * ptr, const uint64_t r_size)
    {
        std::memcpy(ptr, data + global_write_offset, r_size);
        global_write_offset += r_size;
    };

    auto cow_write = [&](const uint64_t index, const uint64_t w_size)
    {
        const auto new_blk = copy_on_write(index);
        if (new_blk != index) {
            // relink
            relink_map.emplace(index, new_blk);
        }

        const auto lock = lock_page(new_blk);
        copy_to_buffer(lock->data(), w_size);
    };

    auto init_alloc_map_from_vec = [&](const std::vector<uint64_t> & list,
        decltype(allocation_map_t::level1_pointers) & pointer_list)
    {
        std::ranges::for_each(list, [&](const uint64_t ptr) {
            pointer_list.emplace_back(ptr, false);
        });
    };

    // init allocation map
    init_alloc_map_from_vec(level1, allocation_descriptor.level1_pointers);
    init_alloc_map_from_vec(level2, allocation_descriptor.level2_pointers);
    init_alloc_map_from_vec(level3, allocation_descriptor.level3_pointers);

    // write first page
    {
        const auto target_blk = level3[skipped_blocks];
        cow_write(target_blk, bytes_to_write_in_the_first_block);
    }

    // write continuous
    for (uint64_t i = 1; i <= adjacent_full_blocks; i++) {
        const auto target_blk = level3[skipped_blocks + i];
        cow_write(target_blk, block_size_);
    }

    // write tail
    if (bytes_to_write_in_the_last_block != 0) {
        const auto target_blk = level3[skipped_blocks + adjacent_full_blocks + 1];
        cow_write(target_blk, bytes_to_write_in_the_last_block);
    }

    if (!relink_map.empty())
    {
        decltype(allocation_descriptor.level3_pointers) level3_pointers = allocation_descriptor.level3_pointers;

        // relink all level 3 blocks
        std::ranges::for_each(allocation_descriptor.level3_pointers, [&](std::pair <uint64_t, bool> & ptr)
        {
            if (const auto it = relink_map.find(ptr.first); it != relink_map.end()) {
                ptr.first = it->second; // replace parent
                ptr.second = true; // mark as reallocated
            }
        });

        // commit changes
        commit_from_linearized_block(allocation_descriptor);
    }

    success = true;
    return global_write_offset;
}

void cfs::cfs_inode_service_t::resize(const uint64_t new_size)
{
    if (new_size == this->cfs_inode_attribute->st_size) return; // skip size change if no size change is intended
    const auto descriptor = size_to_linearized_block_descriptor(new_size);
    commit_from_block_descriptor(descriptor);
    this->cfs_inode_attribute->st_size = static_cast<decltype(this->cfs_inode_attribute->st_size)>(new_size);
}

void cfs::cfs_inode_service_t::chdev(const int dev)
{
    this->cfs_inode_attribute->st_dev = dev;
}

void cfs::cfs_inode_service_t::chrdev(const dev_t dev)
{
    this->cfs_inode_attribute->st_rdev = dev;
}

void cfs::cfs_inode_service_t::chmod(const int mode)
{
    this->cfs_inode_attribute->st_mode = mode;
}

void cfs::cfs_inode_service_t::chown(const int uid, const int gid)
{
    this->cfs_inode_attribute->st_uid = uid;
    this->cfs_inode_attribute->st_gid = gid;
}

void cfs::cfs_inode_service_t::set_atime(const timespec st_atim)
{
    this->cfs_inode_attribute->st_atim = st_atim;
}

void cfs::cfs_inode_service_t::set_ctime(const timespec st_ctim)
{
    this->cfs_inode_attribute->st_atim = st_ctim;
}

void cfs::cfs_inode_service_t::set_mtime(const timespec st_mtim)
{
    this->cfs_inode_attribute->st_atim = st_mtim;
}

cfs::cfs_directory_t::cfs_directory_t(
    const uint64_t index,
    filesystem *parent_fs_governor,
    cfs_block_manager_t *block_manager,
    cfs_journaling_t *journal,
    cfs_block_attribute_access_t *block_attribute)
: cfs_inode_service_t(index, parent_fs_governor, block_manager, journal, block_attribute)
{
}

cfs::cfs_inode_service_t cfs::cfs_directory_t::make_inode(const std::string &name)
{
}
