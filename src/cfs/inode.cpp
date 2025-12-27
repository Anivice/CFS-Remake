#include "inode.h"
#include "lz4frame.h"

void cfs::inode_t::save_dentry_unblocked()
{
    if (dentry_map_.empty()) {
        referenced_inode_->resize(dentry_start_);
        return;
    }
    std::vector<uint8_t> buffer_;
    buffer_.clear();
    buffer_.reserve(dentry_map_.size() * 32);

    auto save_bytes = [&](const auto data_type) {
        for (int i = 0; i < sizeof(data_type); i++) { // dentry start signature
            buffer_.push_back(((char*)&data_type)[i]);
        }
    };

    for (const auto & [string, inode] : dentry_map_)
    {
        for (const auto c : string) { buffer_.push_back(c); }
        buffer_.push_back(0); // null terminated string TODO: DO NOT ALLOW ANY CREATION OF NULL TERMINATED DENTRY NAME
        save_bytes(inode);
    }

    const auto compressed = utils::arithmetic::compress(buffer_);
    if (dentry_start_ != 0) {
        referenced_inode_->resize(dentry_start_ + compressed.size() + sizeof(cfs_magick_number));
        referenced_inode_->write(reinterpret_cast<const char *>(&cfs_magick_number), sizeof(cfs_magick_number), dentry_start_);
        referenced_inode_->write(reinterpret_cast<const char *>(compressed.data()), compressed.size(), dentry_start_ + sizeof(cfs_magick_number));
    } else {
        uint64_t offset = 0;
        referenced_inode_->resize(sizeof(dentry_start_) + compressed.size() + sizeof(cfs_magick_number));
        dentry_start_ = sizeof(dentry_start_); // write dentry_start first

        offset += referenced_inode_->write(reinterpret_cast<const char *>(&dentry_start_), sizeof(dentry_start_), 0);
        offset += referenced_inode_->write(reinterpret_cast<const char *>(&cfs_magick_number), sizeof(cfs_magick_number), offset);
        referenced_inode_->write(reinterpret_cast<const char *>(compressed.data()), compressed.size(), offset);
    }
}

void cfs::inode_t::read_dentry_unblocked()
{
    if (size_unblocked() < sizeof(uint64_t)) {
        return;
    }

    referenced_inode_->read(reinterpret_cast<char *>(&dentry_start_), sizeof(dentry_start_), 0);
    dentry_map_.clear();
    dentry_map_reversed_search_map_.clear();
    const auto inode_size = size_unblocked();
    const int64_t buffer_size = static_cast<int64_t>(inode_size) - static_cast<int64_t>(dentry_start_);

    if (buffer_size == 0) {
        return;
    }

    if (inode_size < dentry_start_) {
        elog("dentry_start=", dentry_start_, ", but size is ", inode_size, "\n");
        return;
    }

    std::vector<uint8_t> buffer_(buffer_size, 0);
    const auto rSize = referenced_inode_->read(reinterpret_cast<char *>(buffer_.data()), buffer_.size(), dentry_start_);
    cfs_assert_simple(rSize == buffer_.size());

    if (const char * magic = reinterpret_cast<char *>(buffer_.data());
        !!std::memcmp(magic, &cfs_magick_number, sizeof(cfs_magick_number)))
    {
        elog("Designated signature missing!\n");
        return;
    }

    std::vector dentry_data_(buffer_.begin() + sizeof(cfs_magick_number), buffer_.end());
    // decompress data
    const auto decompressed = utils::arithmetic::decompress(dentry_data_);

    std::string name;
    uint64_t ptr;
    int offset = 0;
    bool name_is_true_ptr_is_false = true;
    std::ranges::for_each(decompressed, [&](const char c)
    {
        if (name_is_true_ptr_is_false)
        {
            if (c == 0) {
                offset = 0;
                name_is_true_ptr_is_false = false;
                return;
            }

            name += c;
        }
        else
        {
            reinterpret_cast<char *>(&ptr)[offset++] = c;
            if (offset == sizeof(uint64_t))
            {
                dentry_map_.emplace(name, ptr);
                dentry_map_reversed_search_map_.emplace(ptr, name);
                name_is_true_ptr_is_false = true;
                offset = 0;
                name.clear();
            }
        }
    });
}

std::vector<uint8_t> cfs::inode_t::dump_inode_raw() const
{
    std::vector<uint8_t> my_data;
    my_data.resize(static_info_->block_size); // inode metadata size
    std::memcpy(my_data.data(), referenced_inode_->inode_effective_lock_.data(), my_data.size());
    return my_data;
}

void cfs::inode_t::copy_on_write() // CoW entry
{
    // check if we are a snapshot entry
    cfs_assert_simple(inode_construct_info_.block_attribute->get<block_status>(current_referenced_inode_)
        != BLOCK_FROZEN_AND_IS_ENTRY_POINT_OF_SNAPSHOTS_0x01);

    const std::vector<uint8_t> my_data = dump_inode_raw();
    if (parent_inode_ != nullptr)
    {
        const auto new_inode_num_ = parent_inode_->copy_on_write_invoked_from_child(current_referenced_inode_, my_data); // upload
        // dlog("copy-on-write, block=", current_referenced_inode_, ", redirect=", new_inode_num_, ", parent=", parent_inode_->current_referenced_inode_, "\n");
        if (new_inode_num_ != current_referenced_inode_) // I got referenced
        {
            const auto old_ = current_referenced_inode_;
            current_referenced_inode_ = new_inode_num_; // get new inode
            referenced_inode_.reset();
            referenced_inode_ = std::make_unique<cfs_inode_service_t>(new_inode_num_,
                                                                      inode_construct_info_.parent_fs_governor,
                                                                      inode_construct_info_.block_manager,
                                                                      inode_construct_info_.journal,
                                                                      inode_construct_info_.block_attribute); // relocate reference
            referenced_inode_->cfs_inode_attribute->st_ino = current_referenced_inode_;

            if (inode_construct_info_.block_attribute->get<block_status>(old_) == BLOCK_AVAILABLE_TO_MODIFY_0x00) {
                inode_construct_info_.block_attribute->move<block_type, block_type_cow>(old_);
                inode_construct_info_.block_attribute->set<block_type>(old_, COW_REDUNDANCY_BLOCK);
            } else {
                inode_construct_info_.block_attribute->dec<index_node_referencing_number>(old_);
            }
        }
    }
    else
    {
        // dlog("/: copy-on-write, block=", current_referenced_inode_);
        root_cow();
        // dlog(", redirect=", current_referenced_inode_, "\n");
    }
}

void cfs::inode_t::root_cow()
{
    std::vector<uint8_t> data_;
    // create a new block
    const auto new_inode_num_ = inode_construct_info_.block_manager->allocate();
    // set attributes
    inode_construct_info_.block_attribute->clear(new_inode_num_, {
                                                     .block_status = BLOCK_AVAILABLE_TO_MODIFY_0x00,
                                                     .block_type = INDEX_NODE_BLOCK,
                                                     .block_type_cow = 0,
                                                     .allocation_oom_scan_per_refresh_count = 0,
                                                     .index_node_referencing_number = 1,
                                                     .block_checksum = 0,
                                                 });

    /// dump my own data to new inode
    {
        const auto new_lock = inode_construct_info_.parent_fs_governor->
                lock(new_inode_num_ + static_info_->data_table_start);
        std::memcpy(new_lock.data(), referenced_inode_->inode_effective_lock_.data(), static_info_->block_size);
    }

    const auto old_ = current_referenced_inode_;
    current_referenced_inode_ = new_inode_num_; // get new inode
    referenced_inode_.reset();
    referenced_inode_ = std::make_unique<cfs_inode_service_t>(new_inode_num_,
                                                              inode_construct_info_.parent_fs_governor,
                                                              inode_construct_info_.block_manager,
                                                              inode_construct_info_.journal,
                                                              inode_construct_info_.block_attribute); // relocate

    ////////////////////////////////////////////////////////////////////////////////////////////////////////
    /////////////  _    _______ _____ _____ _____  ______ _____   _____ _   _ ______ _____    //////////////
    ///////////// | |  | | ___ \_   _|_   _|  ___| |  ___/  ___| |_   _| \ | ||  ___|  _  |   //////////////
    ///////////// | |  | | |_/ / | |   | | | |__   | |_  \ `--.    | | |  \| || |_  | | | |   //////////////
    ///////////// | |/\| |    /  | |   | | |  __|  |  _|  `--. \   | | | . ` ||  _| | | | |   //////////////
    ///////////// \  /\  / |\ \ _| |_  | | | |___  | |   /\__/ /  _| |_| |\  || |   \ \_/ /   //////////////
    /////////////  \/  \/\_| \_|\___/  \_/ \____/  \_|   \____/   \___/\_| \_/\_|    \___/    //////////////
    ////////////////////////////////////////////////////////////////////////////////////////////////////////

    // [DENTRY ENTRY POINTER] -> 8 bytes, 64bit pointer
    // [BITMAP]             -> Compressed
    // [INODE]              -> Compressed
    // [STATIC DATA]        -> Uncompressed, place holder
    // [DENTRY ENTRY]

    const auto bitmap_dump = referenced_inode_->block_manager_->dump_bitmap_data();
    // const auto attribute_dump = referenced_inode_->block_attribute_->dump();
    std::vector<uint8_t> inode_metadata(referenced_inode_->block_size_);
    std::memcpy(inode_metadata.data(), referenced_inode_->inode_effective_lock_.data(),
                referenced_inode_->inode_effective_lock_.size());

    data_.resize(bitmap_dump.size() /* + attribute_dump.size() */ + inode_metadata.size()); // we keep this buffer
    /// so that less malloc is called
    /// data will be overwritten anyway so

    uint64_t offset = 0;
    auto write_to_buffer = [&](const std::vector<uint8_t> & buffer) {
        std::memcpy(data_.data() + offset, buffer.data(), buffer.size());
        offset += buffer.size();
    };

    write_to_buffer(bitmap_dump);
    // write_to_buffer(attribute_dump);
    write_to_buffer(inode_metadata);
    const auto empty_size =
        (static_info_->data_block_attribute_table_end - static_info_->data_block_attribute_table_start) * static_info_->block_size + /* attribute map */
        (static_info_->data_bitmap_end - static_info_->data_bitmap_start) * static_info_->block_size /* bitmap */;

    const auto data_compressed = utils::arithmetic::compress(data_);
    dentry_start_ = data_compressed.size() + sizeof(dentry_start_) + empty_size; // so save_dentry_unblocked() knows where to continue

    offset = 0;

    referenced_inode_->resize(dentry_start_);
    offset += referenced_inode_->write(reinterpret_cast<char *>(&dentry_start_), sizeof(uint64_t), offset);
    offset += referenced_inode_->write(reinterpret_cast<const char *>(data_compressed.data()), data_compressed.size(), offset);
    /// Now, we have written all the metadata:
    save_dentry_unblocked();

    // relink root
    inode_construct_info_.parent_fs_governor->cfs_header_block.set_info<root_inode_pointer>(new_inode_num_);
    referenced_inode_->cfs_inode_attribute->st_ino = new_inode_num_;

    // change old to redundancy
    if (inode_construct_info_.block_attribute->get<block_status>(old_) == BLOCK_AVAILABLE_TO_MODIFY_0x00) {
        inode_construct_info_.block_attribute->move<block_type, block_type_cow>(old_);
        inode_construct_info_.block_attribute->set<block_type>(old_, COW_REDUNDANCY_BLOCK);
    } else {
        inode_construct_info_.block_attribute->dec<index_node_referencing_number>(old_);
    }
}

uint64_t cfs::inode_t::copy_on_write_invoked_from_child(const uint64_t cow_index, const std::vector<uint8_t> &content)
{
    // check if we are a snapshot entry
    cfs_assert_simple(inode_construct_info_.block_attribute->get<block_status>(current_referenced_inode_)
        != BLOCK_FROZEN_AND_IS_ENTRY_POINT_OF_SNAPSHOTS_0x01);
    const auto old_ = current_referenced_inode_;
    copy_on_write();

    // now, we start to change current dentry reference
    const auto ptr = dentry_map_reversed_search_map_.find(cow_index);
    cfs_assert_simple(ptr != dentry_map_reversed_search_map_.end());
    const auto name = ptr->second;
    dentry_map_reversed_search_map_.erase(ptr);
    const auto ptr_ = dentry_map_.find(name);
    cfs_assert_simple(ptr_ != dentry_map_.end());
    dentry_map_.erase(ptr_);

    // copy over
    const auto new_block = inode_construct_info_.block_manager->allocate();
    const auto new_lock = referenced_inode_->lock_page(new_block);
    cfs_assert_simple(content.size() == static_info_->block_size);
    std::memcpy(new_lock->data(), content.data(), content.size());
    // set attributes
    inode_construct_info_.block_attribute->clear(new_block, {
                                                     .block_status = BLOCK_AVAILABLE_TO_MODIFY_0x00,
                                                     .block_type = INDEX_NODE_BLOCK,
                                                     .block_type_cow = 0,
                                                     .allocation_oom_scan_per_refresh_count = 0,
                                                     .index_node_referencing_number = 1,
                                                     .block_checksum = 0,
                                                 });
    // child old block redefined as cow by themselves

    // new pair: name, new_block, relink here
    dentry_map_.emplace(name, new_block);
    dentry_map_reversed_search_map_.emplace(new_block, name);

    // write to dentry
    referenced_inode_->resize(dentry_start_); // clear old data
    save_dentry_unblocked(); // write

    // dlog("copy-on-write child-relink, child block=", cow_index, ", child redirect=", new_block,
        // ", parent (me)=", current_referenced_inode_, " (from=", old_, ", name=", name, ")\n");
    // return the new block to child
    return new_block;
}

cfs::inode_t::inode_t(
    const uint64_t index,
    filesystem *parent_fs_governor,
    cfs_block_manager_t *block_manager,
    cfs_journaling_t *journal,
    cfs_block_attribute_access_t *block_attribute,
    inode_t *parent_inode)
:
    parent_inode_(parent_inode),
    static_info_(&parent_fs_governor->static_info_)
{
    current_referenced_inode_ = index;
    referenced_inode_ = std::make_unique<cfs_inode_service_t>(index, parent_fs_governor, block_manager, journal, block_attribute);
    inode_construct_info_ = {
        .parent_fs_governor = parent_fs_governor,
        .block_manager = block_manager,
        .journal = journal,
        .block_attribute = block_attribute,
    };
}

void cfs::inode_t::resize(const uint64_t size)
{
    std::lock_guard lock(operation_mutex_);
    copy_on_write(); // relink
    referenced_inode_->resize(size);
}

uint64_t cfs::inode_t::read(char *data, const uint64_t size, const uint64_t offset)
{
    std::lock_guard lock(operation_mutex_);
    return referenced_inode_->read(data, size, offset);
}

uint64_t cfs::inode_t::write(const char *data, const uint64_t size, const uint64_t offset)
{
    std::lock_guard lock(operation_mutex_);
    copy_on_write(); // relink
    return referenced_inode_->write(data, size, offset);
}

void cfs::inode_t::chdev(const dev_t dev)
{
    std::lock_guard lock(operation_mutex_);
    copy_on_write(); // relink
    return referenced_inode_->chdev(dev);
}

void cfs::inode_t::chrdev(const dev_t dev)
{
    std::lock_guard lock(operation_mutex_);
    copy_on_write(); // relink
    return referenced_inode_->chrdev(dev);
}

void cfs::inode_t::chmod(const mode_t mode)
{
    std::lock_guard lock(operation_mutex_);
    copy_on_write(); // relink
    return referenced_inode_->chmod(mode);
}

void cfs::inode_t::chown(const uid_t uid, const gid_t gid)
{
    std::lock_guard lock(operation_mutex_);
    copy_on_write(); // relink
    return referenced_inode_->chown(uid, gid);
}

void cfs::inode_t::set_atime(const timespec st_atim)
{
    std::lock_guard lock(operation_mutex_);
    copy_on_write(); // relink
    return referenced_inode_->set_atime(st_atim);
}

void cfs::inode_t::set_ctime(const timespec st_ctim)
{
    std::lock_guard lock(operation_mutex_);
    copy_on_write(); // relink
    return referenced_inode_->set_atime(st_ctim);
}

void cfs::inode_t::set_mtime(const timespec st_mtim)
{
    std::lock_guard lock(operation_mutex_);
    copy_on_write(); // relink
    return referenced_inode_->set_atime(st_mtim);
}

uint64_t cfs::inode_t::size()
{
    std::lock_guard lock(operation_mutex_);
    return size_unblocked();
}

cfs::stat cfs::inode_t::get_stat()
{
    std::lock_guard lock(operation_mutex_);
    return get_stat_unblocked();
}

cfs::dentry_t::dentry_t(
    const uint64_t index,
    filesystem *parent_fs_governor,
    cfs_block_manager_t *block_manager,
    cfs_journaling_t *journal,
    cfs_block_attribute_access_t *block_attribute,
    inode_t *parent_inode)
: inode_t(index, parent_fs_governor, block_manager, journal, block_attribute, parent_inode)
{
    if (inode_type() != S_IFDIR) {
        throw error::cannot_initialize_dentry_on_non_dentry_inodes();
    }
    std::lock_guard lock(operation_mutex_);
    read_dentry_unblocked();
}

cfs::dentry_t::dentry_pairs_t cfs::dentry_t::ls()
{
    // updated from disk and should change sync with the memory so, just read memory
    dentry_pairs_t pairs;
    std::lock_guard lock(operation_mutex_);
    for (const auto& [name, pointer] : dentry_map_) {
        pairs.emplace(name, pointer);
    }
    return pairs;
}

void cfs::dentry_t::unlink(const std::string & name)
{
    std::lock_guard lock(operation_mutex_);
    copy_on_write(); // relink
    const auto it = dentry_map_.find(name);
    cfs_assert_simple(it != dentry_map_.end());
    const uint64_t inode_pointer = it->second;

    if (inode_construct_info_.block_attribute->get<block_status>(inode_pointer) == BLOCK_AVAILABLE_TO_MODIFY_0x00)
    {
        /// create child inode
        inode_t target(inode_pointer,
                inode_construct_info_.parent_fs_governor,
                inode_construct_info_.block_manager,
                inode_construct_info_.journal,
                inode_construct_info_.block_attribute,
                this);
        target.copy_on_write();
        target.resize(0); // remove all data in inode pointers
        inode_construct_info_.block_manager->deallocate(target.current_referenced_inode_); // remove child inode
    }
    else {
        // delink blocks one by one
        auto delink_once = [&](const std::vector<uint64_t> & list) {
            std::ranges::for_each(list, [&](const uint64_t pointer) {
                inode_construct_info_.block_attribute->dec<index_node_referencing_number>(pointer);
            });
        };

        cfs_inode_service_t target(inode_pointer,
                inode_construct_info_.parent_fs_governor,
                inode_construct_info_.block_manager,
                inode_construct_info_.journal,
                inode_construct_info_.block_attribute);
        const auto [lv1, lv2, lv3] = target.linearize_all_blocks();
        delink_once(lv1);
        delink_once(lv2);
        delink_once(lv3);
        delink_once({inode_pointer});
    }

    // remove child from dentry
    dentry_map_.erase(name);
    dentry_map_reversed_search_map_.erase(inode_pointer);
    save_dentry_unblocked();
}

uint64_t cfs::dentry_t::erase_entry(const std::string &name)
{
    std::lock_guard lock(operation_mutex_);
    copy_on_write();
    const auto ptr = dentry_map_.find(name);
    cfs_assert_simple (ptr != dentry_map_.end());
    const auto inode = ptr->second;
    dentry_map_.erase(name);
    dentry_map_reversed_search_map_.erase(inode);
    save_dentry_unblocked();
    return inode;
}

void cfs::dentry_t::add_entry(const std::string &name, const uint64_t index)
{
    std::lock_guard lock(operation_mutex_);
    copy_on_write();
    const auto ptr = dentry_map_.find(name);
    cfs_assert_simple (ptr == dentry_map_.end());
    dentry_map_.emplace(name, index);
    dentry_map_reversed_search_map_.emplace(index, name);
    save_dentry_unblocked();
}

void cfs::dentry_t::snapshot(const std::string &name)
{
    std::lock_guard<std::mutex> lock(operation_mutex_);
    copy_on_write();
    cfs_assert_simple(parent_inode_ == nullptr);
    bool success = false;
    g_transaction(inode_construct_info_.journal, success, GlobalTransaction_Major_SnapshotCreation);
    std::vector<uint8_t> root_raw_dump;
    uint64_t old_dentry_start_ = 0;
    std::vector<uint64_t> level3s;

    uint64_t new_inode_index = 0;
    {
        auto new_inode = make_inode_unblocked<dentry_t>(name);
        root_raw_dump.resize(referenced_inode_->get_stat().st_size);
        referenced_inode_->read(reinterpret_cast<char *>(root_raw_dump.data()), root_raw_dump.size(), 0);
        new_inode.write(reinterpret_cast<char *>(root_raw_dump.data()), root_raw_dump.size(), 0);
        new_inode_index = new_inode.current_referenced_inode_;
        old_dentry_start_ = dentry_start_;
        const auto [lv1, lv2, lv3] = new_inode.referenced_inode_->linearize_all_blocks();
        level3s = lv3;
    }

    // force CoW updates from now on
    for (uint64_t i = 0; i < static_info_->data_table_end - static_info_->data_table_start; i++)
    {
        const auto attr = inode_construct_info_.block_attribute->get(i);
        if (inode_construct_info_.block_manager->blk_at(i) && attr.block_status == BLOCK_AVAILABLE_TO_MODIFY_0x00) {
            inode_construct_info_.block_attribute->set<block_status>(i, BLOCK_FROZEN_AND_IS_SNAPSHOT_REGULAR_BLOCK_0x02);
        }
    }

    // remove all snapshot links in new root shadow copy
    {
        dentry_t new_dentry = dentry_t(new_inode_index,
            inode_construct_info_.parent_fs_governor,
            inode_construct_info_.block_manager,
            inode_construct_info_.journal,
            inode_construct_info_.block_attribute,
            this);

        const auto dentry_map_local = dentry_map_; // erase_entry will cause a CoW that will, in turn, modify dentry_map_
        // that means for loop will be reference an object that no longer exists, thus we use a local copy instead of direct global
        // object
        for (const auto & [ptr_name, ptr]: dentry_map_local)
        {
            if (inode_construct_info_.block_attribute->get<block_status>(ptr) == BLOCK_FROZEN_AND_IS_ENTRY_POINT_OF_SNAPSHOTS_0x01
                || !inode_construct_info_.block_manager->blk_at(ptr) /* CoW refresh happened, and this inode was not notified */
                || ptr_name == name) // me
            {
                new_dentry.erase_entry(ptr_name); // CoW is enforced, so any updates should not temper the root
            }
        }

        new_dentry.copy_on_write(); // force at least one copy of CoW

        // update reference
        new_inode_index = new_dentry.current_referenced_inode_;
    }

    // under current root, add snapshot entry link
    dentry_map_.emplace(name, new_inode_index);
    dentry_map_reversed_search_map_.emplace(new_inode_index, name);
    save_dentry_unblocked();

    // TODO: Dump attribute map, that way we can compare two attribute maps and see just which block is deallocated
    // TODO: in which generation. that will help clean up process
    // dump bitmap
    const auto map_size = static_info_->data_table_end - static_info_->data_table_start;
    auto bitmap_dump = inode_construct_info_.block_manager->dump_bitmap_data();
    // remove all cow blocks from this bitmap
    class per_snapshot_bitmap_t : public bitmap_base {
    public:
        explicit per_snapshot_bitmap_t(uint8_t * data, const uint64_t size)
        {
            init_data_array = [&](const uint64_t)->bool
            {
                data_array_ = data;
                return true;
            };

            init(size);
        }
    } per_snapshot_bitmap(bitmap_dump.data(), // bitmap start
        map_size); // particles. bytes are calculated automatically

    for (uint64_t i = 0; i < map_size; i++) {
        if (per_snapshot_bitmap.get_bit(i) && inode_construct_info_.block_attribute->get<block_type>(i) == COW_REDUNDANCY_BLOCK) {
            per_snapshot_bitmap.set_bit(i, false); // remove it when it's just CoW
        }
    }

    /// now, we need to replace target dump with our current bitmap dump
    auto replace_write = [this, &level3s](const std::vector<uint8_t> & data, const uint64_t offset)
    {
        const uint64_t bytes = data.size();
        const uint64_t start = offset;
        const uint64_t start_block = start / static_info_->block_size;
        const uint64_t first_block_skip = start % static_info_->block_size;
        const uint64_t first_block_write = bytes > (static_info_->block_size - first_block_skip)
            ? (static_info_->block_size - first_block_skip)
            : (bytes - first_block_skip);
        const uint64_t rest_of_the_bytes = bytes - first_block_write;
        const uint64_t rest_continuous = rest_of_the_bytes / static_info_->block_size;
        const uint64_t last_block_write = rest_of_the_bytes % static_info_->block_size;

        uint64_t src_offset = 0;
        auto replace_write_sig = [&](const uint64_t block, const uint64_t size, const uint64_t w_off) {
            const auto blk_lock = inode_construct_info_.parent_fs_governor->lock(level3s[block] + static_info_->data_table_start);
            std::memcpy(blk_lock.data() + w_off, data.data() + src_offset, size);
            src_offset += size;
        };

        // without CoW, in place replace
        replace_write_sig(start_block, first_block_write, first_block_skip);
        for (uint64_t i = 1; i <= rest_continuous; i++) {
            replace_write_sig(start_block + i, static_info_->block_size, 0);
        }
        if (last_block_write != 0) {
            replace_write_sig(start_block + rest_continuous + 1, last_block_write, 0);
        }
    };

    // write bitmap data
    const uint64_t map_bytes = (static_info_->data_bitmap_end - static_info_->data_bitmap_start) * static_info_->block_size;
    replace_write(bitmap_dump, old_dentry_start_ - map_bytes);

    // write attribute map data
    const auto bytes_by_attribute_map =
        (static_info_->data_block_attribute_table_end - static_info_->data_block_attribute_table_start) * static_info_->block_size;
    const auto attribute_dump = inode_construct_info_.block_attribute->dump();
    replace_write(attribute_dump, old_dentry_start_ - map_bytes - bytes_by_attribute_map);

    // then we mark again to mask any missing ones, and increate link count this time
    for (uint64_t i = 0; i < static_info_->data_table_end - static_info_->data_table_start; i++)
    {
        const auto attr = inode_construct_info_.block_attribute->get(i);
        if (inode_construct_info_.block_manager->blk_at(i))
        {
            if (attr.block_status == BLOCK_AVAILABLE_TO_MODIFY_0x00) {
                inode_construct_info_.block_attribute->set<block_status>(i, BLOCK_FROZEN_AND_IS_SNAPSHOT_REGULAR_BLOCK_0x02);
            }

            if (attr.block_type != COW_REDUNDANCY_BLOCK) {
                inode_construct_info_.block_attribute->inc<index_node_referencing_number>(i);
            }
        }
    }

    // set new inode as snapshot entry point
    inode_construct_info_.block_attribute->set<block_status>(new_inode_index,
                                                             BLOCK_FROZEN_AND_IS_ENTRY_POINT_OF_SNAPSHOTS_0x01);

    inode_construct_info_.parent_fs_governor->sync(); // sync when snapshot
}

void cfs::dentry_t::revert(const std::string &name)
{
    std::lock_guard<std::mutex> lock(operation_mutex_);
    cfs_assert_simple(parent_inode_ == nullptr); // force root
    bool success = false;
    g_transaction(inode_construct_info_.journal, success, GlobalTransaction_Major_SnapshotCreation);

    const auto ptr = dentry_map_.find(name);
    cfs_assert_simple(ptr != dentry_map_.end());

    // remove all post snapshot changes
    for (uint64_t i = 0; i < static_info_->data_table_end - static_info_->data_table_start; i++)
    {
        if (inode_construct_info_.block_manager->blk_at(i))
        {
            if (inode_construct_info_.block_attribute->get<block_status>(i) == BLOCK_AVAILABLE_TO_MODIFY_0x00) {
                inode_construct_info_.block_attribute->move<block_type, block_type_cow>(i);
                inode_construct_info_.block_attribute->set<block_type>(i, COW_REDUNDANCY_BLOCK);
            }
        }
    }

    // backup snapshot entries
    std::vector < std::pair < std::string, uint64_t > > snapshot_entry_list;
    for (const auto & [pointer_name, pointer] : dentry_map_) {
        if (inode_construct_info_.block_attribute->get<block_status>(pointer) == BLOCK_FROZEN_AND_IS_ENTRY_POINT_OF_SNAPSHOTS_0x01) {
            snapshot_entry_list.emplace_back(pointer_name, pointer);
        }
    }

    // get inode
    const uint64_t inode_pointer = ptr->second;
    // make a dentry from that
    current_referenced_inode_ = inode_pointer;
    referenced_inode_.reset();
    referenced_inode_ = std::make_unique<cfs_inode_service_t>(inode_pointer,
        inode_construct_info_.parent_fs_governor,
        inode_construct_info_.block_manager,
        inode_construct_info_.journal,
        inode_construct_info_.block_attribute);

    // reinit dentry
    dentry_map_.clear();
    dentry_map_reversed_search_map_.clear();
    read_dentry_unblocked();
    root_cow(); // force a CoW to redirect root, skip check (we will discard)

    // add missing snapshot entry link
    for (const auto & [pointer_name, pointer] : snapshot_entry_list)
    {
        if (pointer != current_referenced_inode_) {
            dentry_map_.emplace(pointer_name, pointer);
            dentry_map_reversed_search_map_.emplace(pointer, pointer_name);
        }
    }

    save_dentry_unblocked(); // save on disk

    uint64_t non_cow_blocks = 0;

    for (uint64_t i = 0; i < static_info_->data_table_end - static_info_->data_table_start; i++)
    {
        const auto attr = inode_construct_info_.block_attribute->get(i);
        if (inode_construct_info_.block_manager->blk_at(i)) {
            non_cow_blocks += (attr.block_type != 0x00) ? 1 : 0;
        }
    }

    inode_construct_info_.parent_fs_governor->cfs_header_block.set_info<allocated_non_cow_blocks>(non_cow_blocks);
    inode_construct_info_.parent_fs_governor->sync(); // sync when snapshot
}

void cfs::dentry_t::delete_snapshot(const std::string &name)
{
    std::lock_guard<std::mutex> lock(operation_mutex_);
    copy_on_write();
    cfs_assert_simple(parent_inode_ == nullptr);
    bool success = false;
    g_transaction(inode_construct_info_.journal, success, GlobalTransaction_Major_SnapshotCreation);

    const auto ptr = dentry_map_.find(name);
    cfs_assert_simple(ptr != dentry_map_.end());
    uint64_t target_index = ptr->second;
    const uint64_t old_index_reference = target_index;

    cfs_inode_service_t::linearized_block_t occupied_blocks;
    tsl::hopscotch_map<uint64_t, std::vector<uint8_t>> bitmaps_from_all_snapshots;
    const auto map_size = static_info_->data_table_end - static_info_->data_table_start;

    auto read_bitmap_from_snapshot_entry_point = [&](const uint64_t snapshot_entry_index)
    {
        auto dentry = dentry_t(snapshot_entry_index,
            inode_construct_info_.parent_fs_governor,
            inode_construct_info_.block_manager,
            inode_construct_info_.journal,
            inode_construct_info_.block_attribute,
            this); // construct target child
        const uint64_t map_bytes = (static_info_->data_bitmap_end - static_info_->data_bitmap_start) * static_info_->block_size;
        const uint64_t map_start = dentry.dentry_start_ - map_bytes;
        std::vector<uint8_t> per_snapshot_fs_info(map_bytes);
        dentry.read(reinterpret_cast<char *>(per_snapshot_fs_info.data()), map_bytes, map_start);
        bitmaps_from_all_snapshots.emplace(snapshot_entry_index, per_snapshot_fs_info);
    };

    for (const auto & pointer: dentry_map_ | std::views::values)
    {
        if (inode_construct_info_.block_attribute->get<block_status>(pointer)
            == BLOCK_FROZEN_AND_IS_ENTRY_POINT_OF_SNAPSHOTS_0x01)
        {
            read_bitmap_from_snapshot_entry_point(pointer);
        }
    }

    tsl::hopscotch_map<uint64_t /* block number */, uint64_t /* overlapping count */> bitmap_overview_map;
    auto record_bitmap = [&](std::vector<uint8_t> bitmap_data, const uint64_t bitmap_particles)
    {
        class per_snapshot_bitmap_t : public bitmap_base {
        public:
            explicit per_snapshot_bitmap_t(uint8_t * data, const uint64_t size)
            {
                init_data_array = [&](const uint64_t)->bool
                {
                    data_array_ = data;
                    return true;
                };

                init(size);
            }
        } per_snapshot_bitmap(bitmap_data.data(), // bitmap start
            bitmap_particles); // particles. bytes are calculated automatically

        for (uint64_t i = 0; i < bitmap_particles; i++)
        {
            if (per_snapshot_bitmap.get_bit(i)) {
                bitmap_overview_map[i]++;
            }
        }
    };

    std::ranges::for_each(bitmaps_from_all_snapshots | std::views::values, [&](const std::vector<uint8_t> & bitmap_data) {
        record_bitmap(bitmap_data, map_size);
    });

    {
        auto dentry = dentry_t(target_index,
            inode_construct_info_.parent_fs_governor,
            inode_construct_info_.block_manager,
            inode_construct_info_.journal,
            inode_construct_info_.block_attribute,
            this); // construct target child
        occupied_blocks = dentry.referenced_inode_->linearize_all_blocks();
        target_index = dentry.current_referenced_inode_; // update reference
    }

    auto delete_block = [this](const std::vector<uint64_t> & blk)
    {
        std::ranges::for_each(blk, [this](const uint64_t p) {
            inode_construct_info_.block_attribute->move<block_type, block_type_cow>(p); // backup block type
            inode_construct_info_.block_attribute->set<block_type>(p, COW_REDUNDANCY_BLOCK); // set that as redundancy
        });
    };

    // remove all occupied blocks
    delete_block(occupied_blocks.level1_pointers);
    delete_block(occupied_blocks.level2_pointers);
    delete_block(occupied_blocks.level3_pointers);
    delete_block({ target_index });

    // remove corresponding link as well
    dentry_map_.erase(name);
    dentry_map_reversed_search_map_.erase(target_index);
    save_dentry_unblocked();

    // static field. no CoW after this

    // record active bitmap data
    {
        std::vector<uint8_t> active_bitmap_suite = inode_construct_info_.block_manager->dump_bitmap_data();
        class per_snapshot_bitmap_t : public bitmap_base {
        public:
            explicit per_snapshot_bitmap_t(uint8_t * data, const uint64_t size)
            {
                init_data_array = [&](const uint64_t)->bool
                {
                    data_array_ = data;
                    return true;
                };

                init(size);
            }
        } per_snapshot_bitmap(active_bitmap_suite.data(), // bitmap start
            map_size); // particles. bytes are calculated automatically

        for (uint64_t i = 0; i < map_size; i++)
        {
            if (per_snapshot_bitmap.get_bit(i)
                && inode_construct_info_.block_attribute->get<block_type>(i) == COW_REDUNDANCY_BLOCK) // remove redundancies
            {
                per_snapshot_bitmap.set_bit(i, false);
            }
        }

        record_bitmap(active_bitmap_suite, map_size);
    }

    // dry run, remove blocks referenced by snapshot and no others
    {
        class per_snapshot_bitmap_t : public bitmap_base {
        public:
            explicit per_snapshot_bitmap_t(uint8_t * data, const uint64_t size)
            {
                init_data_array = [&](const uint64_t)->bool
                {
                    data_array_ = data;
                    return true;
                };

                init(size);
            }
        } per_snapshot_bitmap(bitmaps_from_all_snapshots.at(old_index_reference).data(), // bitmap start
            map_size); // particles. bytes are calculated automatically

        if (bitmaps_from_all_snapshots.size() > 1)
        {
            for (uint64_t i = 0; i < map_size; i++)
            {
                // present in this bitmap
                if (per_snapshot_bitmap.get_bit(i)) {
                    bitmap_overview_map[i]--;
                }
            }
        }
        else if (bitmaps_from_all_snapshots.size() == 1) // if only one snapshot is left, this means...
        {
            for (uint64_t i = 0; i < map_size; i++)
            {
                // present in this bitmap
                if (per_snapshot_bitmap.get_bit(i)) {
                    bitmap_overview_map[i]--;
                }
            }
        }
    }

    for (uint64_t i = 0; i < map_size; i++) {
        if (inode_construct_info_.block_manager->blk_at(i)) {
            inode_construct_info_.block_attribute->set<index_node_referencing_number>(i, 0); // place holder
        }
    }

    // now, bitmap_overview_map is the processed map
    for (const auto & [index, references] : bitmap_overview_map) {
        inode_construct_info_.block_attribute->set<index_node_referencing_number>(index, references); // set the corresponding reference
    }

    // see which ones are not yet set to redundancy
    for (uint64_t i = 0; i < map_size; i++)
    {
        if (inode_construct_info_.block_manager->blk_at(i))
        {
            const auto attr = inode_construct_info_.block_attribute->get(i);
            if (attr.index_node_referencing_number == 0 && attr.block_type != COW_REDUNDANCY_BLOCK) {
                inode_construct_info_.block_attribute->move<block_type, block_type_cow>(i);
                inode_construct_info_.block_attribute->set<block_type>(i, COW_REDUNDANCY_BLOCK); // CoW this block
            }
        }
    }

    // update corresponding flags
    uint64_t non_cow_blocks = 0;
    for (uint64_t i = 0; i < map_size; i++)
    {
        const auto attr = inode_construct_info_.block_attribute->get(i);
        if (inode_construct_info_.block_manager->blk_at(i)) {
            non_cow_blocks += (attr.block_type != 0x00) ? 1 : 0;
        }
    }

    inode_construct_info_.parent_fs_governor->cfs_header_block.set_info<allocated_non_cow_blocks>(non_cow_blocks);
    inode_construct_info_.parent_fs_governor->sync(); // sync
}
