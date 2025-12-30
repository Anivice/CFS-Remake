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
    if (inode_construct_info_.parent_fs_governor->global_control_flags.load().no_pointer_and_storage_cow) {
        return;
    }

    const auto current_referenced_inode_ = referenced_inode_->get_stat().st_ino;

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
            // current_referenced_inode_ = new_inode_num_; // get new inode
            referenced_inode_.reset();
            referenced_inode_ = std::make_shared<cfs_inode_service_t>(new_inode_num_,
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
    if (inode_construct_info_.parent_fs_governor->global_control_flags.load().no_pointer_and_storage_cow) {
        return;
    }

    const auto current_referenced_inode_ = referenced_inode_->get_stat().st_ino;

    std::vector<uint8_t> data_;
    // create a new block
    const auto new_inode_num_ = inode_construct_info_.block_manager->allocate();
    // set attributes
    inode_construct_info_.block_attribute->set<block_type>(new_inode_num_, INDEX_NODE_BLOCK);

    /// dump my own data to new inode
    {
        const auto new_lock = inode_construct_info_.parent_fs_governor->
                lock(new_inode_num_ + static_info_->data_table_start);
        std::memcpy(new_lock.data(), referenced_inode_->inode_effective_lock_.data(), static_info_->block_size);
    }

    const auto old_ = current_referenced_inode_;
    // current_referenced_inode_ = new_inode_num_; // get new inode
    referenced_inode_.reset();
    referenced_inode_ = std::make_shared<cfs_inode_service_t>(new_inode_num_,
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
    // [STATIC DATA]        -> Uncompressed, placeholder
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
    const auto current_referenced_inode_ = referenced_inode_->get_stat().st_ino;

    // check if we are a snapshot entry
    cfs_assert_simple(inode_construct_info_.block_attribute->get<block_status>(current_referenced_inode_)
        != BLOCK_FROZEN_AND_IS_ENTRY_POINT_OF_SNAPSHOTS_0x01);
    // const auto old_ = current_referenced_inode_;
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
    inode_construct_info_.block_attribute->set<block_type>(new_block, INDEX_NODE_BLOCK);
    const auto new_lock = referenced_inode_->lock_page(new_block);
    cfs_assert_simple(content.size() == static_info_->block_size);
    std::memcpy(new_lock->data(), content.data(), content.size());
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
    // current_referenced_inode_ = index;
    referenced_inode_ = std::make_shared<cfs_inode_service_t>(index, parent_fs_governor, block_manager, journal, block_attribute);
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

cfs::dentry_t::dentry_t(const inode_t & downgraded_target) : inode_t()
{
    std::lock_guard lock(operation_mutex_);
    referenced_inode_ = downgraded_target.referenced_inode_; // transfer control
    inode_construct_info_ = {
        .parent_fs_governor = downgraded_target.inode_construct_info_.parent_fs_governor,
        .block_manager = downgraded_target.inode_construct_info_.block_manager,
        .journal = downgraded_target.inode_construct_info_.journal,
        .block_attribute = downgraded_target.inode_construct_info_.block_attribute,
    };

    if (inode_type() != S_IFDIR) {
        throw error::cannot_initialize_dentry_on_non_dentry_inodes();
    }

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
        inode_construct_info_.block_manager->deallocate(target.referenced_inode_->get_stat().st_ino); // remove child inode
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
    if (inode_construct_info_.parent_fs_governor->global_control_flags.load().no_pointer_and_storage_cow) {
        return;
    }

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
        new_inode_index = new_inode.referenced_inode_->get_stat().st_ino;
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
        new_inode_index = new_dentry.referenced_inode_->get_stat().st_ino;
    }

    // under current root, add snapshot entry link
    dentry_map_.emplace(name, new_inode_index);
    dentry_map_reversed_search_map_.emplace(new_inode_index, name);
    save_dentry_unblocked();

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

    /// [dentry_start_]
    /// [Compressed Metadata]
    /// [Attribute Map, size=(data_block_attribute_table_end - data_block_attribute_table_start) * block_size]
    /// [Bitmap, size=(data_bitmap_end - data_bitmap_start) * block_size]
    /// -> dentry_start_
    /// [Dentry, compressed]

    // write bitmap data
    const uint64_t map_bytes = (static_info_->data_bitmap_end - static_info_->data_bitmap_start) * static_info_->block_size;
    replace_write(bitmap_dump, old_dentry_start_ - map_bytes);

    // write attribute map data
    const auto bytes_by_attribute_map =
        (static_info_->data_block_attribute_table_end - static_info_->data_block_attribute_table_start) * static_info_->block_size;
    const auto attribute_dump = inode_construct_info_.block_attribute->dump();
    replace_write(attribute_dump, old_dentry_start_ - map_bytes - bytes_by_attribute_map);

    // then we mark again to mask any missing ones during CoW
    for (uint64_t i = 0; i < static_info_->data_table_end - static_info_->data_table_start; i++)
    {
        const auto attr = inode_construct_info_.block_attribute->get(i);
        if (inode_construct_info_.block_manager->blk_at(i))
        {
            if (attr.block_status == BLOCK_AVAILABLE_TO_MODIFY_0x00) {
                inode_construct_info_.block_attribute->set<block_status>(i, BLOCK_FROZEN_AND_IS_SNAPSHOT_REGULAR_BLOCK_0x02);
            }

            if (attr.block_type != COW_REDUNDANCY_BLOCK) {
                inode_construct_info_.block_attribute->set<index_node_referencing_number>(i, 2); // reset to 2
            }
        }
    }

    // set new inode as snapshot entry point
    inode_construct_info_.block_attribute->set<block_status>(new_inode_index,
                                                             BLOCK_FROZEN_AND_IS_ENTRY_POINT_OF_SNAPSHOTS_0x01);

    inode_construct_info_.parent_fs_governor->sync(); // sync when snapshot
    success = true;
}

void cfs::dentry_t::revert(const std::string &name)
{
    if (inode_construct_info_.parent_fs_governor->global_control_flags.load().no_pointer_and_storage_cow) {
        return;
    }

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
    // current_referenced_inode_ = inode_pointer;
    referenced_inode_.reset();
    referenced_inode_ = std::make_shared<cfs_inode_service_t>(inode_pointer,
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
    const auto current_referenced_inode_ = referenced_inode_->get_stat().st_ino;
    for (const auto & [pointer_name, pointer] : snapshot_entry_list)
    {
        if (pointer != current_referenced_inode_) {
            dentry_map_.emplace(pointer_name, pointer);
            dentry_map_reversed_search_map_.emplace(pointer, pointer_name);
        }
    }

    save_dentry_unblocked(); // save on disk

    // reset reference state
    for (uint64_t i = 0; i < static_info_->data_table_end - static_info_->data_table_start; i++)
    {
        const auto attr = inode_construct_info_.block_attribute->get(i);
        if (inode_construct_info_.block_manager->blk_at(i))
        {
            if (attr.block_status == BLOCK_AVAILABLE_TO_MODIFY_0x00) {
                inode_construct_info_.block_attribute->set<block_status>(i, BLOCK_FROZEN_AND_IS_SNAPSHOT_REGULAR_BLOCK_0x02);
            }

            if (attr.block_type != COW_REDUNDANCY_BLOCK) {
                inode_construct_info_.block_attribute->set<index_node_referencing_number>(i, 2);
            }
        }
    }

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
    success = true;
}

void cfs::dentry_t::delete_snapshot(const std::string &name)
{
    if (inode_construct_info_.parent_fs_governor->global_control_flags.load().no_pointer_and_storage_cow) {
        return;
    }

    std::lock_guard<std::mutex> lock(operation_mutex_);
    copy_on_write();
    cfs_assert_simple(parent_inode_ == nullptr);
    bool success = false;
    g_transaction(inode_construct_info_.journal, success, GlobalTransaction_Major_SnapshotCreation);

    const auto ptr = dentry_map_.find(name);
    cfs_assert_simple(ptr != dentry_map_.end());
    uint64_t target_index = ptr->second;
    const uint64_t old_index_reference = target_index;

    auto delete_blocks = [this](const std::vector<uint64_t> & blk)
    {
        std::ranges::for_each(blk, [this](const uint64_t p) {
            inode_construct_info_.block_attribute->move<block_type, block_type_cow>(p); // backup block type
            inode_construct_info_.block_attribute->set<block_type>(p, COW_REDUNDANCY_BLOCK); // set that as redundancy
        });
    };

    ////////////////////////
    /// SORT GENERATIONS ///
    ////////////////////////
    /// first, we need to sort generations by creation time
    std::vector < std::pair < uint64_t /* snapshot creation time */, uint64_t /* snapshot entry index */ > > generation_reference_table;
    for (const auto & pointer: dentry_map_ | std::views::values)
    {
        if (inode_construct_info_.block_attribute->get<block_status>(pointer)
            == BLOCK_FROZEN_AND_IS_ENTRY_POINT_OF_SNAPSHOTS_0x01)
        {
            /// create a static index node
            inode_t inode(pointer,
                inode_construct_info_.parent_fs_governor,
                inode_construct_info_.block_manager,
                inode_construct_info_.journal,
                inode_construct_info_.block_attribute,
                this);
            generation_reference_table.emplace_back(inode.get_stat().st_mtim.tv_sec, pointer); // <creation second: pointer>
        }
    }
    // put this root into the list as the latest generation
    const auto current_referenced_inode_ = referenced_inode_->get_stat().st_ino;
    generation_reference_table.emplace_back(utils::get_timespec().tv_sec, current_referenced_inode_);
    // then we sort the list
    std::ranges::sort(generation_reference_table,
        [](const std::pair < uint64_t /* snapshot creation time */, uint64_t /* snapshot entry index */ > & a,
            const std::pair < uint64_t , uint64_t  > & b){ return a.first < b.first; });

    /////////////////////////////////////
    /// FIND OUT GENERAL PROGRESSIONS ///
    /////////////////////////////////////
    /// now, compare map to map, find out generational progression
    struct generation_t {
        std::vector<uint8_t> bitmap;
        std::vector<uint8_t> attributes;
    };

    struct result_t {
        std::vector<uint64_t> these_blocks_allocated_during_this_gen;
        std::vector<uint64_t> these_blocks_removed_during_this_gen;

        // used by others
        uint64_t before = 0;
        uint64_t after = 0;
    };

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
    };

    auto compare_generations = [](generation_t & before, generation_t & after,  const uint64_t particles)->result_t
    {
        per_snapshot_bitmap_t bitmap_before(before.bitmap.data(), particles);
        per_snapshot_bitmap_t bitmap_after(after.bitmap.data(), particles);
        result_t result;

        auto get_attribute_by_index = [](const uint64_t index, const std::vector<uint8_t> & attributes)->cfs_block_attribute_t {
            cfs_block_attribute_t ret{};
            std::memcpy(&ret, attributes.data() + index * sizeof(ret), sizeof(ret));
            return ret;
        };

        for (uint64_t i = 0; i < particles; ++i)
        {
            if (!bitmap_before.get_bit(i) && bitmap_after.get_bit(i)) { // referenced in later map but never in before
                result.these_blocks_allocated_during_this_gen.emplace_back(i);
                continue; // determined
            }

            if (bitmap_before.get_bit(i) && get_attribute_by_index(i, before.attributes).index_node_referencing_number < 2)
                // referenced in the previous map, but delinked by later
            {
                result.these_blocks_removed_during_this_gen.emplace_back(i);
            }

            // anything else is carried over
        }

        return result;
    };

    // load data
    tsl::hopscotch_map<uint64_t, std::vector<uint8_t>> bitmaps_from_all_snapshots;
    tsl::hopscotch_map<uint64_t, std::vector<uint8_t>> attributes_from_all_snapshots;
    const auto map_size = static_info_->data_table_end - static_info_->data_table_start;
    auto read_bitmap_and_attributes_from_snapshot_entry_point = [&](const uint64_t snapshot_entry_index, const dentry_t * dentry = nullptr)
    {
        std::unique_ptr < dentry_t > dentry_unique_ptr;
        if (dentry == nullptr)
        {
            dentry_unique_ptr = std::make_unique<dentry_t>(snapshot_entry_index,
                inode_construct_info_.parent_fs_governor,
                inode_construct_info_.block_manager,
                inode_construct_info_.journal,
                inode_construct_info_.block_attribute,
                this); // construct target child
            dentry = dentry_unique_ptr.get();
        }

        const uint64_t map_bytes = (static_info_->data_bitmap_end - static_info_->data_bitmap_start) * static_info_->block_size;
        const uint64_t map_start = dentry->dentry_start_ - map_bytes;
        const uint64_t attr_bytes = (static_info_->data_block_attribute_table_end - static_info_->data_block_attribute_table_start) * static_info_->block_size;
        const uint64_t attr_start = dentry->dentry_start_ - attr_bytes - map_bytes;

        std::vector<uint8_t> bitmap_data(map_bytes);
        dentry->referenced_inode_->read(reinterpret_cast<char *>(bitmap_data.data()), map_bytes, map_start);
        bitmaps_from_all_snapshots.emplace(snapshot_entry_index, bitmap_data);

        std::vector<uint8_t> attributes_data(attr_bytes);
        dentry->referenced_inode_->read(reinterpret_cast<char *>(attributes_data.data()), attr_bytes, attr_start);
        attributes_from_all_snapshots.emplace(snapshot_entry_index, attributes_data);
    };

    // from generation_reference_table, read all generations
    for (const auto & pointer: generation_reference_table | std::views::values) {
        if (pointer == current_referenced_inode_) // pointer is literally fucking me right now
        {
            read_bitmap_and_attributes_from_snapshot_entry_point(pointer, this); // I can't lock onto me so
        }
        else { // previous me's
            read_bitmap_and_attributes_from_snapshot_entry_point(pointer);
        }
    }

    // bitwise comparison
    tsl::hopscotch_map < uint64_t /* any of the before or after */, std::vector < result_t * > > bitwise_comparison_results;
    std::vector < std::unique_ptr < result_t > > results;
    {
        const uint64_t offset_end = generation_reference_table.size() - 2;
        // say we have 3 generations {0, 1, 2}, compare [0,1], [1,2], offset end = 3 - 2
        // for loop: for (i = 0) ==> 1 { compare(i, i+1); }
        for (uint64_t i = 0; i <= offset_end; ++i)
        {
            const auto particles = static_info_->data_table_end - static_info_->data_table_start;
            const uint64_t before_index = generation_reference_table[i].second; // first is time, second is index
            const uint64_t after_index = generation_reference_table[i + 1].second;
            generation_t generation_before {
                .bitmap = bitmaps_from_all_snapshots.at(before_index),
                .attributes = attributes_from_all_snapshots.at(before_index),
            };

            generation_t generation_after{
                .bitmap = bitmaps_from_all_snapshots.at(after_index),
                .attributes = attributes_from_all_snapshots.at(after_index),
            };

            auto result = compare_generations(generation_before, generation_after, particles);
            result.before = before_index;
            result.after = after_index;

            results.emplace_back(std::make_unique<result_t>(result));
            bitwise_comparison_results[before_index].emplace_back(results.back().get());
            bitwise_comparison_results[after_index].emplace_back(results.back().get());
        }
    }

    //////////////////////////////////////////////////////
    /// FIGURE OUT HOW TO DELETE GENERATION COHESIVELY ///
    //////////////////////////////////////////////////////
    // there are couple rules here:
    // for generation cohesive, you can only delete blocks that has the following attributes:
    //  * never referenced by previous generations, that is, allocated by this generation
    //  * never referenced by later generations, that is, deleted after this generation
    const auto comparison_results = bitwise_comparison_results.at(target_index);
    result_t * before_progression = nullptr, * after_progression = nullptr;
    for (const auto & result : comparison_results)
    {
        if (result->after == target_index) {
            before_progression = result;
        } else if (result->before == target_index) {
            after_progression = result;
        }
    }

    cfs_assert_simple(after_progression != nullptr);

    auto remove_snapshot_entry_occupied_blocks = [&]
    {
        // remove blocks occupied by snapshot entry point
        cfs_inode_service_t::linearized_block_t occupied_blocks;
        {
            auto dentry = dentry_t(target_index,
                inode_construct_info_.parent_fs_governor,
                inode_construct_info_.block_manager,
                inode_construct_info_.journal,
                inode_construct_info_.block_attribute,
                this); // construct target child
            occupied_blocks = dentry.referenced_inode_->linearize_all_blocks();
            target_index = dentry.referenced_inode_->get_stat().st_ino; // update reference
        }

        // remove all occupied blocks by snapshot entry point
        delete_blocks(occupied_blocks.level1_pointers);
        delete_blocks(occupied_blocks.level2_pointers);
        delete_blocks(occupied_blocks.level3_pointers);
        delete_blocks({ target_index });
    };

    // after cannot be nullptr, but before can, if it is the first generation
    // if before is nullptr, then all the blocks are referenced by later generation until
    // later ones are removed, unless after is active root.
    if (before_progression != nullptr)
    {
        tsl::hopscotch_map < uint64_t, bool > allocated_in_this_snapshot;
        std::vector<uint64_t> allocated_and_removed;
        std::ranges::for_each(before_progression->these_blocks_allocated_during_this_gen, [&](const uint64_t index) {
            allocated_in_this_snapshot.emplace(index, true);
        });

        std::ranges::for_each(after_progression->these_blocks_removed_during_this_gen, [&](const uint64_t index)
        {
            if (allocated_in_this_snapshot.contains(index)) {
                allocated_and_removed.emplace_back(index);
            }
        });

        delete_blocks(allocated_and_removed);
        remove_snapshot_entry_occupied_blocks();
    }
    else if (after_progression->after == current_referenced_inode_ && generation_reference_table.size() == 2)
        // first generation, and later one is literally root
        // easy way is to compare and dedup manually
    {
        delete_blocks(after_progression->these_blocks_removed_during_this_gen);
        // calculate overlaps
        std::vector<uint8_t> actual_blocks_used_by_real_root((static_info_->data_bitmap_end - static_info_->data_bitmap_start) * static_info_->block_size, 0);
        per_snapshot_bitmap_t this_root_bitmap(actual_blocks_used_by_real_root.data(), map_size);
        auto venture_non_dentry = [&](const uint64_t index)
        {
            cfs_inode_service_t inode(index, inode_construct_info_.parent_fs_governor,
                inode_construct_info_.block_manager,
                inode_construct_info_.journal,
                inode_construct_info_.block_attribute); // OK DO NOT MODIFY THIS DENTRY!!!
            const auto [lv1, lv2, lv3] = inode.linearize_all_blocks();
            auto mark = [&](const std::vector<uint64_t> & p) {
                std::ranges::for_each(p, [&](const uint64_t b){ this_root_bitmap.set_bit(b, true); });
            };

            mark(lv1);
            mark(lv2);
            mark(lv3);
            mark({ inode.get_stat().st_ino });
        };

        auto venture_dentry = [&](const uint64_t index)
        {
            dentry_t inode(index, inode_construct_info_.parent_fs_governor,
                inode_construct_info_.block_manager,
                inode_construct_info_.journal,
                inode_construct_info_.block_attribute, nullptr); // OK DO NOT MODIFY THIS DENTRY!!!
            const auto [lv1, lv2, lv3] = inode.referenced_inode_->linearize_all_blocks();
            auto mark = [&](const std::vector<uint64_t> & p) {
                std::ranges::for_each(p, [&](const uint64_t b){ this_root_bitmap.set_bit(b, true); });
            };

            mark(lv1);
            mark(lv2);
            mark(lv3);
            mark({ inode.get_stat().st_ino });
            return inode.ls() | std::views::values;
        };

        std::function<void(uint64_t)> venture_from_one_entry;
        venture_from_one_entry = [&](const uint64_t index)
        {
            mode_t mode = 0;
            {
                cfs_inode_service_t inode(index, inode_construct_info_.parent_fs_governor,
                    inode_construct_info_.block_manager,
                    inode_construct_info_.journal,
                    inode_construct_info_.block_attribute);
                mode = inode.get_stat().st_mode;
            }

            if ((mode & S_IFMT) == S_IFDIR) {
                auto list = venture_dentry(index);
                std::ranges::for_each(list, [&](const uint64_t b){ venture_from_one_entry(b); });
            } else {
                venture_non_dentry(index);
            }

            this_root_bitmap.set_bit(index, true);
        };

        // mark all children except entry point
        for (const auto & pointer : dentry_map_ | std::views::values)
        {
            if (inode_construct_info_.block_attribute->get<block_status>(pointer)
                != BLOCK_FROZEN_AND_IS_ENTRY_POINT_OF_SNAPSHOTS_0x01)
            {
                venture_from_one_entry(pointer);
            }
        }

        // mark root node
        const auto [lv1, lv2, lv3] = referenced_inode_->linearize_all_blocks();
        auto mark = [&](const std::vector<uint64_t> & p) {
            std::ranges::for_each(p, [&](const uint64_t b){ this_root_bitmap.set_bit(b, true); });
        };

        mark(lv1);
        mark(lv2);
        mark(lv3);
        mark({ current_referenced_inode_ });

        // now, this_root_bitmap contains data that only referenced by root, remove all other data
        for (uint64_t i = 0; i < map_size; i++)
        {
            if (inode_construct_info_.block_manager->blk_at(i)
                && inode_construct_info_.block_attribute->get<block_type>(i) != COW_REDUNDANCY_BLOCK
                && !this_root_bitmap.get_bit(i))
            {
                // allocated, non-redundancy, BUT!, not present in root tree in any way or form
                delete_blocks({i}); // mark this block as redundancy
            }
        }

        // since we skipped snapshot entry points, that means no additional cleanups are needed
        // they are never referenced in the root, so they will never be added into the bitmap
        // and the above step already freed all unmarked data in the root reference

        // mark all remaining as 1 ref, available to be modified
        for (uint64_t i = 0; i < map_size; i++)
        {
            if (this_root_bitmap.get_bit(i))
            {
                inode_construct_info_.block_attribute->set<index_node_referencing_number>(i, 1);
                inode_construct_info_.block_attribute->set<block_status>(i, BLOCK_AVAILABLE_TO_MODIFY_0x00);
            }
        }
    }
    else // first gen, later is not root, all nodes are referenced unfortunately until in the last gen we do a proper clean up
    {
        // this means, if you keep deleting the oldest gen, you will be forced to keep all the data since
        // CFS has no idea which block belongs to which generation.
        // the best practice is that, you create a snapshot of an empty system, then continue to use CFS
        // with that snapshot present. This way, CFS can know which block belongs to which generation
        // and everything is done with cohesive delete.
        // But in-depth cleanups are, indeed, requires you to delete all generations, including the preceeding empty ones
        remove_snapshot_entry_occupied_blocks();
    }

    // remove corresponding link as well
    dentry_map_.erase(name);
    dentry_map_reversed_search_map_.erase(target_index);
    save_dentry_unblocked();

    // static field. no CoW after this
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
    success = true;
}
