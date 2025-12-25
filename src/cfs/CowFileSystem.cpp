#include "CowFileSystem.h"
#include "cfs.h"
#include "readline.h"
#include "history.h"
#include "smart_block_t.h"
#include "version.h"
#include "cfs_command.h"
#include "generalCFSbaseError.h"
#include <iostream>
#include <algorithm>
#include <memory>
#include <sstream>
#include "colors.h"
#include <ranges>
#include "inode.h"
#include <fcntl.h>
#include <unistd.h>

#define print_case(name) case cfs::name: ss << cfs::name##_c_str; break;
#define print_default(data) default: ss << std::hex << (uint32_t)(data) << std::dec; break;

#define replicate(name, sss) \
    case cfs::name:             ss  << highlight(cfs::name##_c_str)             << sss; break; \
    case cfs::name##_Completed: ss  << highlight(cfs::name##_Completed_c_str)   << sss; break; \
    case cfs::name##_Failed:    ss  << highlight(cfs::name##_Failed_c_str)      << sss; break;

std::string print_attribute(const uint32_t val)
{
    const auto * attr_ = reinterpret_cast<const cfs::cfs_block_attribute_t *>(&val);
    const auto & attr = *attr_;
    std::stringstream ss;
    ss << "     Status:             ";
    switch (attr.block_status) {
        case cfs::BLOCK_AVAILABLE_TO_MODIFY_0x00: ss << "Normal"; break;
        case cfs::BLOCK_FROZEN_AND_IS_ENTRY_POINT_OF_SNAPSHOTS_0x01: ss << "Snapshot Entry Point"; break;
        case cfs::BLOCK_FROZEN_AND_IS_SNAPSHOT_REGULAR_BLOCK_0x02: ss << "Snapshot Regular Block"; break;
        default: ss << "Unknown (Possibly corruption)"; break;
    }
    ss << "\n";

    ss << "     Type:               ";
    switch (attr.block_type) {
        case cfs::INDEX_NODE_BLOCK: ss << "Index"; break;
        case cfs::POINTER_BLOCK: ss << "Pointer"; break;
        case cfs::STORAGE_BLOCK: ss << "Storage"; break;
        case cfs::COW_REDUNDANCY_BLOCK: ss << "Redundancy"; break;
        default: ss << "Unknown (Possibly corruption)"; break;
    }
    ss << "\n";

    ss << "     Type (COW Backup):  ";
    switch (attr.block_type_cow) {
        case cfs::INDEX_NODE_BLOCK: ss << "Index"; break;
        case cfs::POINTER_BLOCK: ss << "Pointer"; break;
        case cfs::STORAGE_BLOCK: ss << "Storage"; break;
        case cfs::COW_REDUNDANCY_BLOCK: ss << "Redundancy"; break;
        default: ss << "Unknown (Possibly corruption)"; break;
    }
    ss << "\n";

    ss << "     Age:                " << std::dec << attr.allocation_oom_scan_per_refresh_count << " cycles\n";
    ss << "     New alloc no CoW:   " << (attr.newly_allocated_thus_no_cow ? "True" : "False") << "\n";
    ss << "     Index referenced:   " << std::dec << attr.index_node_referencing_number << "\n";
    ss << "     HASH5:              " << std::hex << std::setw(2) << std::setfill('0') << attr.block_checksum << "\n";
    return ss.str();
}

#define print_transaction_arg1(name) \
    case cfs::name:             ss  << highlight(cfs::name##_c_str) << " " \
                                    << std::dec << highlight_pos(action.action_data.action_plain.action_param1); break; \
    case cfs::name##_Completed: ss  << highlight(cfs::name##_Completed_c_str) << " " \
                                    << std::dec << highlight_pos(action.action_data.action_plain.action_param1); break; \
    case cfs::name##_Failed:    ss  << highlight(cfs::name##_Failed_c_str) << " " \
                                    << std::dec << highlight_pos(action.action_data.action_plain.action_param1); break;

static std::string translate_action_into_literal(const cfs::cfs_action_t & action)
{
    using namespace cfs::color;
    std::stringstream ss;

    auto highlight = [](const std::string & str)->std::string {
        std::string replc = str;
        cfs::utils::replace_all(replc, "_Completed", "");
        cfs::utils::replace_all(replc, "_Failed", "");
        const auto crc16 = CRC16::DDS_110::calc((const uint8_t*)replc.data(), replc.size());
        return color(crc16 & 0x1F,(crc16 >> 5) & 0x1F,(crc16 >> 10) & 0x1F) + str + no_color();
    };

    auto highlight_val = [](const uint64_t val)->std::string {
        return color(0,5,5) + std::to_string(val) + no_color();
    };

    auto highlight_pos = [](const uint64_t val)->std::string {
        return color(5,0,5) + std::to_string(val) + no_color();
    };

    switch (action.action_data.action_plain.action)
    {
        case cfs::CorruptionDetected:
            ss << color(0,0,0,5,0,0) << cfs::CorruptionDetected_c_str << " Type: " << color(5,0,0);
            switch (action.action_data.action_plain.action_param0) {
                print_case(BitmapMirrorInconsistent);
                print_case(FilesystemBlockExhausted);
                print_default(action.action_data.action_plain.action_param0)
            }
            ss << no_color();
        break;

        case cfs::AttemptedFixFinishedAndAssumedFine:
            ss << highlight(cfs::AttemptedFixFinishedAndAssumedFine_c_str) << " ";
            switch (action.action_data.action_plain.action_param0) {
                print_case(BitmapMirrorInconsistent);
                print_case(FilesystemBlockExhausted);
                print_default(action.action_data.action_plain.action_param0)
            }
        break;

        case cfs::GlobalTransaction:
            ss << cfs::GlobalTransaction_c_str << " ";
            switch (action.action_data.action_plain.action_param0) {
                replicate(GlobalTransaction_AllocateBlock, "");
                replicate(GlobalTransaction_DeallocateBlock, " At " << highlight_pos(action.action_data.action_plain.action_param1));
                replicate(GlobalTransaction_CreateRedundancy,
                        " For " << std::dec << highlight_pos(action.action_data.action_plain.action_param1)
                        << " At " << highlight_pos(action.action_data.action_plain.action_param2));
                replicate(FilesystemBitmapModification,
                        " From " << std::dec << action.action_data.action_plain.action_param1
                        << " To " << action.action_data.action_plain.action_param2
                        << " At " << action.action_data.action_plain.action_param3)

                // Majors
                replicate(GlobalTransaction_Major_WriteInode,
                    " Inode=" << highlight_val(action.action_data.action_plain.action_param1)
                    << ", Offset=" << highlight_val(action.action_data.action_plain.action_param2)
                    << ", Size=" << highlight_val(action.action_data.action_plain.action_param3)) // [Which inode] [Offset] [Size]
                print_default(action.action_data.action_plain.action_param0);
            }
        break;
        print_default(action.action_data.action_plain.action);
    }

    return ss.str();
}

namespace fs = std::filesystem;

make_simple_error_class(no_such_file_or_directory)
using namespace cfs::error;

namespace cfs
{
    std::vector<std::string> CowFileSystem::ls_under_pwd_of_cfs(const std::string &)
    {
        std::vector<std::string> vec;
        do_readdir(cfs_pwd_, vec);
        return vec;
    }

    void CowFileSystem::help()
    {
        std::cout << cmdTpTree::command_template_tree.get_help();
    }

    void CowFileSystem::help_at(const std::vector<std::string> &vec)
    {
        try {
            const std::vector help_path(vec.begin() + 1, vec.end());
            std::ranges::for_each(help_path, [](const auto & v) { std::cout << v << " "; });
            std::cout << ": " << cmdTpTree::command_template_tree.get_help(help_path) << std::endl;
        } catch (std::exception & e) {
            elog(e.what(), "\n");
        }
    }

    void CowFileSystem::version()
    {
        std::cout.write(reinterpret_cast<const char *>(version_text), version_text_len);
        std::cout << std::endl;
    }

    void CowFileSystem::debug_cat_ditmap()
    {
        ilog("Bitmap for the whole filesystem:\n");
        const auto col = utils::get_screen_row_col().second;
        const auto fs_bitmap_size =
            cfs_basic_filesystem_.static_info_.data_table_end - cfs_basic_filesystem_.static_info_.data_table_start;
        for (uint64_t i = 0; i < fs_bitmap_size; i++)
        {
            if (i != 0 && i % col == 0) {
                std::cout << "\n";
            }

            auto show_bit = [](const uint32_t attr) {
                switch (attr)
                {
                    case IndexNode:
                        std::cout << color::color(0,0,5) << "I" << color::no_color();
                        break;
                    case PointerBlock:
                        std::cout << color::color(0,5,0) << "P" << color::no_color();
                        break;
                    case StorageBlock:
                        std::cout << color::color(5,5,0) << "S" << color::no_color();
                        break;
                    default:
                        std::cout << color::color(5,5,5) << "x" << color::no_color();
                }
            };

            if (mirrored_bitmap_.get_bit(i))
            {
                const auto attr = block_attribute_.get(i);
                if (attr.block_type == CowRedundancy) {
                    std::cout << color::bg_color(1,1,1);
                    show_bit(block_attribute_.get<block_type_cow>(i));
                }
                else {
                    if (attr.block_status != BLOCK_AVAILABLE_TO_MODIFY_0x00) {
                        std::cout << color::bg_color(0,5,5);
                    }
                    show_bit(attr.block_type);
                }
            } else {
                std::cout << ".";
            }
        }

        if (fs_bitmap_size % col != 0) {
            std::cout << "\n";
        }
    }

    void CowFileSystem::debug_cat_journal()
    {
        const auto journal = this->journaling_.dump_actions();
        std::ranges::for_each(journal, [](const cfs_action_t & action) {
            std::cout << translate_action_into_literal(action) << std::endl;
        });
    }

    void CowFileSystem::debug_cat_attribute(const std::vector<std::string> &vec)
    {
        const auto & loc = vec[3];
        const auto pos = std::strtol(loc.c_str(), nullptr, 10);
        const auto attr = block_attribute_.get(pos);
        if (!mirrored_bitmap_.get_bit(pos)) wlog("Not allocated!\n");
        std::cout << print_attribute(*(uint32_t*)&attr) << std::endl;
    }

    void CowFileSystem::debug_cat_head()
    {
        const auto head = cfs_basic_filesystem_.cfs_header_block.get_info();
        cat_header(head);
        std::vector<std::pair < std::string, int > > titles = {
            {"Entry", utils::Right },
            {"Data", utils::Left },
        };

        std::vector<std::vector < std::string > > vales;

        auto printLine = [&](const std::string & entry, uint64_t value)->void
        {
            std::vector<std::string> line;
            utils::print(line, entry, std::to_string(value));
            vales.emplace_back(line);
        };

        printLine("mount_timestamp", head.runtime_info.mount_timestamp);
        printLine("last_check_timestamp", head.runtime_info.last_check_timestamp);
        printLine("snapshot_number", head.runtime_info.snapshot_number);
        printLine("snapshot_number_cow", head.runtime_info.snapshot_number_cow);
        printLine("clean", head.runtime_info.flags.clean);
        printLine("last_allocated_block", head.runtime_info.last_allocated_block);
        printLine("allocated_non_cow_blocks", head.runtime_info.allocated_non_cow_blocks);
        printLine("root_inode_pointer", head.runtime_info.root_inode_pointer);
        utils::print_table(titles, vales, "Runtime Information");
    }

    void CowFileSystem::debug_check_hash5()
    {
        auto putc = [](const int status)
        {
            switch (status) {
                case 0: // not allocated
                    std::cout << ".";
                    break;
                case 1: // good
                    std::cout << color::color(0,5,0) << "*" << color::no_color();
                    break;
                case 2: // bad
                    std::cout << color::color(5,0,0) << "B" << color::no_color();
                    break;
                default: std::cout << "x";
            }
        };

        const auto len = this->cfs_basic_filesystem_.static_info_.data_table_end - cfs_basic_filesystem_.static_info_.data_table_start;
        std::vector<uint64_t> bad_blocks;
        for (uint64_t i = 0; i < len; i++)
        {
            if (mirrored_bitmap_.get_bit(i))
            {
                auto pg = cfs_basic_filesystem_.lock(i + cfs_basic_filesystem_.static_info_.data_table_start);
                const uint8_t checksum = cfs::utils::arithmetic::hash5((uint8_t*)pg.data(), pg.size());
                const auto comp = block_attribute_.get<block_checksum>(i);
                if (!block_attribute_.get<newly_allocated_thus_no_cow>(i) && comp != checksum) {
                    putc(2);
                } else {
                    putc(1);
                }
            }
            else
            {
                putc(0);
            }
        }

        if (len % utils::get_screen_row_col().second != 0) {
            std::cout << "\n";
        }

        std::ranges::for_each(bad_blocks, [](const uint64_t block) {
            elog(std::dec, "Checksum mismatch at block index ", block, "\n");
        });
        ilog("Total ", bad_blocks.size(), " bad blocks\n");
    }

    void CowFileSystem::ls(const std::vector<std::string> &vec)
    {
        auto ls = [&](std::string path)->void
        {
            path = path_calculator(path);
            std::vector<std::string> list;
            const int result_readdir = do_readdir(path, list);
            if (result_readdir != 0) {
                elog("Error: ", strerror(-result_readdir), "\n");
                return;
            }

            std::vector<std::pair < std::string, int > > titles = {
                {"Name", utils::Right },
                {"Size", utils::Left },
                {"Time", utils::Left},
            };

            std::vector<std::vector < std::string > > vales;

            std::ranges::for_each(list, [&](const std::string & path_)
            {
                const std::string tpath = path_calculator(path + "/" + path_);
                struct stat st{};
                const int result_getattr = do_getattr(tpath, &st);
                if (result_getattr != 0) {
                    elog("Error: ", strerror(-result_getattr), " when reading attributes for ", path_, "\n");
                }

                char buff[100] { };
                strftime(buff, sizeof buff, "%D %T", gmtime(&st.st_mtim.tv_sec));

                std::vector<std::string> line;
                utils::print(line, path_ + ((st.st_mode & S_IFMT) == S_IFDIR ? "/" : ""), utils::value_to_size(st.st_size), buff);
                vales.emplace_back(line);
            });

            utils::print_table(titles, vales, "Path: " + path);
        };

        if (vec.size() == 1) // no args
        {
            ls(cfs_pwd_);
        }
        else // has args
        {
            std::vector<std::string> vpaths = vec;
            vpaths.erase(vpaths.begin());
            std::ranges::for_each(vpaths, [&](std::string path)
            {
                if (!path.empty() && path.front() != '/') {
                    path = cfs_pwd_ + "/" + path;
                }

                ls(path);
            });
        }
    }

    void CowFileSystem::copy_to_host(const std::vector<std::string> &vec)
    {
        if (vec.size() != 3)
        {
            elog("copy_to_host [CFS Path] [Host Path]\n");
        }
        else
        {
            const auto cfs_path = path_calculator(vec[1]);
            const auto & host_full_path = vec[2];
            struct stat status {};
            if (const int result = do_getattr(cfs_path, &status); result != 0) {
                elog("getattr: ", strerror(-result), "\n");
            }
            else
            {
                if (std::ofstream file (host_full_path.c_str()); !file) {
                    elog("open: ", strerror(errno), "\n");
                }
                else
                {
                    std::vector<char> data;
                    data.resize(1024 * 1024 * 16);
                    uint64_t offset = 0;
                    while (const auto rSize = do_read(cfs_path, data.data(), data.size(),
                        static_cast<int>(offset)))
                    {
                        file.write(data.data(), rSize);
                        offset += rSize;
                    }
                }
            }
        }
    }

    void CowFileSystem::copy(const std::vector<std::string> &vec)
    {
        if (vec.size() != 3)
        {
            elog("copy [CFS Path] [CFS Path]\n");
        }
        else
        {
            const auto cfs_path_src = path_calculator(vec[1]);
            const auto cfs_path_dest = path_calculator(vec[2]);
            struct stat status {};
            if (const int result = do_getattr(cfs_path_src, &status); result != 0) {
                elog("getattr: ", strerror(-result), "\n");
            }
            else
            {
                if (status.st_mode & S_IFREG)
                {
                    std::vector<char> data;
                    data.resize(1024 * 1024 * 16);
                    off_t offset = 0;
                    if (const int alloc_result = do_fallocate(cfs_path_dest, S_IFREG | 0755, 0, status.st_size);
                        alloc_result != 0)
                    {
                        elog("fallocate: ", strerror(-alloc_result), "\n");
                        return;
                    }

                    while (const auto rSize = do_read(cfs_path_src, data.data(), data.size(), offset))
                    {
                        if (rSize < 0) {
                            elog("read: ", strerror(-rSize), "\n");
                            return;
                        }

                        const int wSize = do_write(cfs_path_dest, data.data(), rSize, offset);

                        if (wSize < 0) {
                            elog("write: ", strerror(-wSize), "\n");
                            return;
                        }

                        if (wSize != rSize) {
                            elog("Short write: size=", rSize, ", offset=", offset, "\n");
                            return;
                        }

                        offset += rSize;
                    }
                }
                else
                {
                    elog("Cannot copy a directory!\n");
                }
            }
        }
    }

    void CowFileSystem::copy_from_host(const std::vector<std::string> &vec)
    {
        if (vec.size() != 3) {
            elog("copy_from_host [Host Path] [CFS Path]\n");
        }
        else
        {
            const cfs::basic_io::mmap file(vec[1]); // test data
            const auto path = path_calculator(vec[2]);
            if (const int alloc_result = do_fallocate(path, S_IFREG | 0755, 0, static_cast<off_t>(file.size()));
                alloc_result != 0) {
                elog("fallocate: ", strerror(-alloc_result), "\n");
                return;
            }

            if (file.size() != do_write(path, file.data(), file.size(), 0)) {
                elog("Short write!\n");
            }
        }
    }

    void CowFileSystem::mkdir(const std::vector<std::string> &vec)
    {
        if (vec.size() == 2) {
            if (const int mkdir_result = do_mkdir(auto_path(vec[1]), S_IFDIR | 0755); mkdir_result != 0) {
                elog("mkdir: ", strerror(-mkdir_result), "\n");
            }
        } else {
            elog("mkdir [CFS Path]\n");
        }
    }

    void CowFileSystem::rmdir(const std::vector<std::string> &vec)
    {
        if (vec.size() == 2) {
            if (const int rmdir_result = do_rmdir(auto_path(vec[1])); rmdir_result != 0) {
                elog("rmdir: ", strerror(-rmdir_result), "\n");
            }
        } else {
            elog("rmdir [CFS Path]\n");
        }
    }

    void CowFileSystem::del(const std::vector<std::string> &vec)
    {
        if (vec.size() == 2) {
            if (const int unlink_result = do_unlink(auto_path(vec[1])); unlink_result != 0) {
                elog("unlink: ", strerror(unlink_result), "\n");
            }
        } else {
            elog("del [CFS Path]\n");
        }
    }

    void CowFileSystem::free()
    {
        const auto statvfs = do_fstat();
        std::cout   << "CFS: "
                    << utils::value_to_size(statvfs.f_bfree * statvfs.f_bsize) << " / "
                    << utils::value_to_size(statvfs.f_blocks * statvfs.f_bsize) << " "
                    << std::dec << std::fixed << std::setprecision(2)
                    << static_cast<double>(statvfs.f_bfree * statvfs.f_bsize) / static_cast<double>(statvfs.f_blocks * statvfs.f_bsize) * 100
                    << "%" << std::endl;
    }

    void CowFileSystem::cd(const std::vector<std::string> & vec)
    {
        if (vec.size() == 2)
        {
            const auto path = auto_path(vec[1]);
            struct stat status {};
            if (const int result = do_getattr(path, &status); result != 0) {
                elog("getattr: ", strerror(-result), "\n");
                return;
            }

            if (status.st_mode & S_IFDIR) {
                cfs_pwd_ = path_calculator(path);
            } else {
                elog("Not a directory!\n");
            }
        } else {
            cfs_pwd_ = "/";
        }
    }

    void CowFileSystem::pwd()
    {
        std::cout << cfs_pwd_ << std::endl;
    }

    void CowFileSystem::move(const std::vector<std::string> &vec)
    {
        if (vec.size() == 3) {
            const auto cfs_path_src = path_calculator(vec[1]);
            const auto cfs_path_dest = path_calculator(vec[2]);
            const int rename_result = do_rename(cfs_path_src, cfs_path_dest, 1);
            if (rename_result != 0) {
                elog("move: ", strerror(-rename_result), "\n");
            }
        } else {
            elog("move [CFS Path] [CFS Path]\n");
        }
    }

    void CowFileSystem::cat(const std::vector<std::string> &vec)
    {
        if (vec.size() == 2) {
            const auto cfs_path = path_calculator(vec[1]);
            struct stat status {};
            if (const int result = do_getattr(cfs_path, &status); result != 0) {
                elog("getattr: ", strerror(-result), "\n");
            }
            else
            {
                std::vector<char> data;
                data.resize(1024 * 1024 * 16);
                uint64_t offset = 0;
                while (const auto rSize = do_read(cfs_path, data.data(), data.size(), static_cast<off_t>(offset)))
                {
                    if (rSize < 0) {
                        elog("read: ", strerror(-rSize), "\n");
                        break;
                    }

                    ::write(STDOUT_FILENO, data.data(), rSize);
                    offset += rSize;
                }
            }
        } else {
            elog("del [CFS Path]\n");
        }
    }

    std::vector<std::string> CowFileSystem::path_to_vector(const std::string &path) noexcept
    {
        std::vector<std::string> result;
        std::stringstream ss(path);
        char c;
        std::string buf;
        while (ss >> c)
        {
            if (c == '/')
            {
                if (!buf.empty()) {
                    result.push_back(buf);
                    buf.clear();
                }
                continue;
            }

            buf += c;
        }

        if (!buf.empty()) {
            result.push_back(buf);
        }

        return result;
    }

    std::string CowFileSystem::path_calculator(const std::string &path) const noexcept
    {
        if (!path.empty() && path.front() != '/') {
            const fs::path n = (fs::path(cfs_pwd_) / fs::path(path)).lexically_normal();
            return n.generic_string();
        }

        const fs::path p {path};
        const fs::path n = p.lexically_normal(); // removes ".", ".." lexically
        return n.generic_string(); // stable '/' separators
    }

    dentry_t CowFileSystem::make_root_inode() {
        return make_child_inode<dentry_t>(cfs_basic_filesystem_.cfs_header_block.get_info<root_inode_pointer>(), nullptr);
    }

    CowFileSystem::deferenced_pairs_t CowFileSystem::deference_inode_from_path(vpath_t path)
    {
        if (!path.empty())
        {
            const auto target_name = path.back();
            path.pop_back();

            std::vector < std::shared_ptr < dentry_t > > dentries;
            dentries.push_back(
                std::make_shared<dentry_t>(cfs_basic_filesystem_.cfs_header_block.get_info<root_inode_pointer>(),
                &cfs_basic_filesystem_, &block_manager_, &journaling_, &block_attribute_, nullptr));

            auto deference = [&](const std::string & name)
            {
                dentry_t & parent = *dentries.back();
                const auto list = parent.ls();
                const auto ptr = list.find(name);
                if (ptr == list.end()) {
                    throw no_such_file_or_directory();
                }

                const uint64_t index = ptr->second;
                std::pair < uint64_t, const void * > ret;
                ret.first = index;
                ret.second = &parent;

                return ret;
            };

            for (const auto & entry : path)
            {
                const auto [index, parent] = deference(entry);
                dentries.emplace_back(std::make_shared<dentry_t>(
                    index, // inode index
                    &cfs_basic_filesystem_, &block_manager_, &journaling_, &block_attribute_,
                    (dentry_t*)parent));
            }

            const auto [index, parent] = deference(target_name);

            // now we made a list of reference table
            // get that last one
            return {
                .child = std::make_shared<inode_t>(index,
                    &cfs_basic_filesystem_, &block_manager_, &journaling_, &block_attribute_,
                    (dentry_t*)parent),
                .parents = std::move(dentries)
            };
        }

        return {
            .child = std::make_shared<dentry_t>(
                cfs_basic_filesystem_.cfs_header_block.get_info<root_inode_pointer>(),
                &cfs_basic_filesystem_,
                &block_manager_,
                &journaling_,
                &block_attribute_,
                nullptr),
            .parents = { nullptr }
        };
    }

#define GENERAL_TRY() try {
#define GENERAL_CATCH(XX)                       \
    } catch (no_such_file_or_directory &) {     \
        return -ENOENT;                         \
    } catch (no_more_free_spaces &) {           \
        return -ENOSPC;                         \
    } XX catch (std::exception &e) {            \
        elog(e.what(), "\n");                   \
        return -EIO;                            \
    }

    int CowFileSystem::do_getattr(const std::string & path, struct stat *stbuf) noexcept
    {
        GENERAL_TRY() {
            const auto vpath = path_to_vector(path);
            const auto [child, parent]
                = deference_inode_from_path(vpath);
            *stbuf = child->get_stat();
            return 0;
        }
        GENERAL_CATCH()
    }

    int CowFileSystem::do_readdir(const std::string &path, std::vector<std::string> &entries) noexcept
    {
        GENERAL_TRY() {
            const auto vpath = path_to_vector(path);
            auto [child, parents]
                = deference_inode_from_path(vpath);
            const auto child_stat = child->get_stat();
            child.reset(); // release child, its parents are under capture (so is root) so it cannot be tempered
            if ((child_stat.st_mode & S_IFMT) == S_IFDIR) {
                auto dentry = make_child_inode<dentry_t>(child_stat.st_ino, parents.back().get());
                const auto list = dentry.ls() | std::views::keys;
                entries.reserve(list.size());
                std::ranges::for_each(list, [&](const std::string & p){ entries.push_back(p); });
                return 0;
            }

            return -ENOTDIR;
        }
        GENERAL_CATCH()
    }

    int CowFileSystem::do_mkdir(const std::string & path, const mode_t mode) noexcept
    {
        GENERAL_TRY() {
            auto vpath = path_to_vector(path);
            if (vpath.empty()) {
                return -EINVAL; // WTF, mkdir without a fucking name?
            }

            const auto target = vpath.back();
            vpath.pop_back();

            auto [child, parents]
                = deference_inode_from_path(vpath);
            const auto child_stat = child->get_stat();
            child.reset();
            if ((child_stat.st_mode & S_IFMT) == S_IFDIR) {
                auto dentry = make_child_inode<dentry_t>(child_stat.st_ino, parents.back().get());

                // see if target exists
                if (const auto list = dentry.ls(); list.find(target) != list.end()) {
                    return -EEXIST;
                }

                auto new_dentry = dentry.make_inode<dentry_t>(target);
                new_dentry.chmod(mode | S_IFDIR);
                return 0;
            }

            return -ENOTDIR;
        }
        GENERAL_CATCH()
    }

    int CowFileSystem::do_chown(const std::string &path, const uid_t uid, const gid_t gid) noexcept
    {
        GENERAL_TRY() {
            const auto vpath = path_to_vector(path);
            const auto [child, parent]
                = deference_inode_from_path(vpath);
            child->chown(uid, gid);
            child->set_ctime(utils::get_timespec());
            return 0;
        }
        GENERAL_CATCH()
    }

    int CowFileSystem::do_chmod(const std::string &path, mode_t mode) noexcept
    {
        GENERAL_TRY() {
            const auto vpath = path_to_vector(path);
            const auto [child, parent]
                = deference_inode_from_path(vpath);
            child->chmod(mode);
            child->set_ctime(utils::get_timespec());
            return 0;
        }
        GENERAL_CATCH()
    }

    int CowFileSystem::do_create(const std::string &path, const mode_t mode) noexcept
    {
        GENERAL_TRY() {
            auto vpath = path_to_vector(path);
            if (vpath.empty()) {
                return -EINVAL;
            }

            const auto target = vpath.back();
            vpath.pop_back();

            auto [child, parents]
                = deference_inode_from_path(vpath);
            const auto child_stat = child->get_stat();
            child.reset();
            if ((child_stat.st_mode & S_IFMT) == S_IFDIR) {
                auto dentry = make_child_inode<dentry_t>(child_stat.st_ino, parents.back().get());
                if (const auto list = dentry.ls(); list.find(target) != list.end()) {
                    return -EEXIST;
                }
                auto inode = dentry.make_inode<inode_t>(target);
                inode.chmod(mode | S_IFREG);
                return 0;
            }

            return -ENOTDIR;
        }
        GENERAL_CATCH()
    }

    int CowFileSystem::do_flush() noexcept
    {
        GENERAL_TRY() {
            cfs_basic_filesystem_.sync();
            return 0;
        }
        GENERAL_CATCH()
    }

    int CowFileSystem::do_access(const std::string &path, int mode) noexcept
    {
        GENERAL_TRY() {
            const auto vpath = path_to_vector(path);
            const auto [child, parent]
                = deference_inode_from_path(vpath);
            if (mode == F_OK)
            {
                return 0;
            }

            // permission check:
            mode <<= 6;
            mode &= 0x01C0;
            auto st_mode = child->get_stat().st_mode;
            if (block_attribute_.get<block_status>(child->get_stat().st_ino) != BLOCK_AVAILABLE_TO_MODIFY_0x00) {
                st_mode &= 0500; // strip write permission
            }
            return -!(mode & st_mode);
        }
        GENERAL_CATCH()
    }

    int CowFileSystem::do_open(const std::string &path) noexcept
    {
        GENERAL_TRY() {
            const auto vpath = path_to_vector(path);
            const auto [child, parent]
                = deference_inode_from_path(vpath);
            return 0;
        }
        GENERAL_CATCH()
    }

    int CowFileSystem::do_read(const std::string &path, char *buffer, const size_t size, const off_t offset) noexcept
    {
        GENERAL_TRY() {
            const auto vpath = path_to_vector(path);
            const auto [child, parent]
                = deference_inode_from_path(vpath);
            return static_cast<int>(child->read(buffer, size, offset));
        }
        GENERAL_CATCH()
    }

    int CowFileSystem::do_write(const std::string &path, const char * buffer, const size_t size, const off_t offset) noexcept
    {
        GENERAL_TRY() {
            const auto vpath = path_to_vector(path);
            const auto [child, parent]
                = deference_inode_from_path(vpath);
            child->set_mtime(utils::get_timespec());
            return static_cast<int>(child->write(buffer, size, offset));
        }
        GENERAL_CATCH()
    }

    int CowFileSystem::do_utimens(const std::string &path, const timespec tv[2]) noexcept
    {
        GENERAL_TRY() {
            const auto vpath = path_to_vector(path);
            const auto [child, parent]
                = deference_inode_from_path(vpath);
            child->set_atime(tv[0]);
            child->set_ctime(utils::get_timespec());
            child->set_mtime(tv[1]);
            return 0;
        }
        GENERAL_CATCH()
    }

    int CowFileSystem::do_unlink(const std::string &path) noexcept
    {
        GENERAL_TRY() {
            auto vpath = path_to_vector(path);
            if (vpath.empty()) {
                return -EINVAL;
            }

            const auto target = vpath.back();
            vpath.pop_back();

            auto [child, parents]
                = deference_inode_from_path(vpath);
            const auto child_stat = child->get_stat();
            child.reset();
            if ((child_stat.st_mode & S_IFMT) == S_IFDIR)
            {
                auto dentry = make_child_inode<dentry_t>(child_stat.st_ino, parents.back().get());
                dentry.unlink(target);
                return 0;
            }

            return -ENOTDIR;
        }
        GENERAL_CATCH()
    }

    int CowFileSystem::do_rmdir(const std::string &path) noexcept
    {
        GENERAL_TRY() {
            auto vpath = path_to_vector(path);
            if (vpath.empty()) {
                return -EINVAL;
            }

            const auto target = vpath.back();
            vpath.pop_back();

            auto [child, parents]
                = deference_inode_from_path(vpath);
            const auto child_stat = child->get_stat();
            child.reset();
            if ((child_stat.st_mode & S_IFMT) == S_IFDIR)
            {
                auto dentry = make_child_inode<dentry_t>(child_stat.st_ino, parents.back().get());
                const auto list = dentry.ls();
                const auto ptr = list.find(target);
                if (ptr == list.end()) {
                    return -ENOENT;
                }

                // check if dir is empty
                {
                    const auto index = ptr->second;
                    auto tg_inode = make_child_inode<dentry_t>(index, &dentry);
                    if ((tg_inode.get_stat().st_mode & S_IFMT) != S_IFDIR) {
                        return -ENOTDIR;
                    }

                    if (!tg_inode.ls().empty()) {
                        return -ENOTEMPTY;
                    }
                }

                // remove when empty
                dentry.unlink(target);
                return 0;
            }

            return -ENOTDIR;
        }
        GENERAL_CATCH()
    }

    int CowFileSystem::do_truncate(const std::string &path, const off_t size) noexcept
    {
        GENERAL_TRY() {
            const auto vpath = path_to_vector(path);
            const auto [child, parent]
                = deference_inode_from_path(vpath);
            child->set_mtime(utils::get_timespec());
            child->resize(size);
            return 0;
        }
        GENERAL_CATCH()
    }

    int CowFileSystem::do_symlink(const std::string & path, const std::string & target_) noexcept
    {
        GENERAL_TRY() {
            auto source_vpath = path_to_vector(path);
            auto target_vpath = path_to_vector(target_);
            if (source_vpath.empty() || target_vpath.empty()) {
                return -EINVAL;
            }

            const auto target = target_vpath.back();
            target_vpath.pop_back();

            const auto [source, parents]
                = deference_inode_from_path(source_vpath);
            auto [target_parent, target_parent_parents]
                = deference_inode_from_path(source_vpath);

            const auto target_parent_stat = target_parent->get_stat();;
            target_parent.reset();

            auto dentry = make_child_inode<dentry_t>(target_parent_stat.st_ino, parents.back().get());
            auto inode = dentry.make_inode<inode_t>(target);
            inode.chmod(S_IFLNK | 0755);
            inode.write(path.c_str(), path.size(), 0);
            return -ENOTDIR;
        }
        GENERAL_CATCH()
    }

    int CowFileSystem::do_snapshot(const std::string & name) noexcept
    {
        GENERAL_TRY() {
            auto [child, parents] = deference_inode_from_path(path_to_vector("/"));
            const auto child_stat = child->get_stat();
            child.reset();
            if ((child_stat.st_mode & S_IFMT) == S_IFDIR)
            {
                auto dentry = make_child_inode<dentry_t>(child_stat.st_ino, parents.back().get());
                if (const auto list = dentry.ls(); list.find(name) != list.end()) {
                    return -EEXIST;
                }

                dentry.snapshot(name);
                return 0;
            }

            return -ENOTDIR;
        }
        GENERAL_CATCH()
    }

    int CowFileSystem::do_rollback(const std::string & name) noexcept
    {
        GENERAL_TRY() {
            auto [child, parents] = deference_inode_from_path(path_to_vector("/"));
            const auto child_stat = child->get_stat();
            child.reset();
            if ((child_stat.st_mode & S_IFMT) == S_IFDIR)
            {
                auto dentry = make_child_inode<dentry_t>(child_stat.st_ino, parents.back().get());
                if (const auto list = dentry.ls(); list.find(name) == list.end()) {
                    return -ENOENT;
                }
                dentry.revert(name);
                return 0;
            }

            return -ENOTDIR;
        }
        GENERAL_CATCH()
    }

    int CowFileSystem::do_cleanup(const std::string &name) noexcept
    {
        GENERAL_TRY() {
            auto [child, parents] = deference_inode_from_path(path_to_vector("/"));
            const auto child_stat = child->get_stat();
            child.reset();
            if ((child_stat.st_mode & S_IFMT) == S_IFDIR)
            {
                auto dentry = make_child_inode<dentry_t>(child_stat.st_ino, parents.back().get());
                if (const auto list = dentry.ls(); list.find(name) == list.end()) {
                    return -ENOENT;
                }
                dentry.delete_snapshot(name);
                return 0;
            }

            return -ENOTDIR;
        }
        GENERAL_CATCH()
    }

    int CowFileSystem::do_rename(const std::string &path, const std::string &new_path, int flags) noexcept
    {
        GENERAL_TRY() {
            auto source_vpath = path_to_vector(path);
            auto target_vpath = path_to_vector(new_path);
            if (source_vpath.empty() || target_vpath.empty()) {
                return -EINVAL;
            }

            const auto source = source_vpath.back();
            source_vpath.pop_back();
            const auto target = target_vpath.back();
            target_vpath.pop_back();

            auto [source_parent, source_parent_parents]
                = deference_inode_from_path(source_vpath);
            auto [target_parent, target_parent_parents]
                = deference_inode_from_path(source_vpath);

            const auto source_parent_stat = source_parent->get_stat();
            const auto target_parent_stat = target_parent->get_stat();
            source_parent.reset();
            target_parent.reset();

            auto source_parent_inode = make_child_inode<dentry_t>(source_parent_stat.st_ino, source_parent_parents.back().get());
            auto target_parent_inode = make_child_inode<dentry_t>(target_parent_stat.st_ino, target_parent_parents.back().get());

            if (flags == 0) {
                // RENAME_NOREPLACE is specified,
                // the filesystem must not overwrite *newname* if it exists and return an error instead.
                const auto target_parent_inode_list = target_parent_inode.ls();
                if (target_parent_inode_list.find(target) != target_parent_inode_list.end()) {
                    return -EEXIST;
                }
            } else if (flags == 1) {
                // If `RENAME_EXCHANGE` is specified, the filesystem
                // must atomically exchange the two files, i.e. both must
                // exist and neither may be deleted.
                const auto target_parent_inode_list = target_parent_inode.ls();
                if (target_parent_inode_list.find(target) != target_parent_inode_list.end()) {
                    target_parent_inode.erase_entry(target); // remove dentry
                }
            }

            const auto source_index = source_parent_inode.erase_entry(source);
            target_parent_inode.add_entry(target, source_index);

            return 0;
        }
        GENERAL_CATCH()
    }

    int CowFileSystem::do_fallocate(const std::string & path, const int mode, const off_t offset, const off_t length) noexcept
    {
        GENERAL_TRY() {
            auto vpath = path_to_vector(path);
            if (vpath.empty()) {
                return -EINVAL;
            }

            const auto target = vpath.back();
            vpath.pop_back();

            auto [child, parents]
                = deference_inode_from_path(vpath);
            const auto child_stat = child->get_stat();
            child.reset();
            if ((child_stat.st_mode & S_IFMT) == S_IFDIR)
            {
                auto dentry = make_child_inode<dentry_t>(child_stat.st_ino, parents.back().get());
                const auto list = dentry.ls();
                const auto ptr = list.find(target);
                if (ptr == list.end()) {
                    auto inode = dentry.make_inode<inode_t>(target);
                    inode.chmod(mode | S_IFREG);
                    inode.resize(offset + length);
                } else {
                    auto inode = make_child_inode<inode_t>(ptr->second, &dentry); // target exists so we write
                    inode.chmod(mode | S_IFREG);
                    inode.resize(offset + length);
                }
                return 0;
            }

            return -ENOTDIR;
        }
        GENERAL_CATCH()
    }

    int CowFileSystem::do_readlink(const std::string &path, char * buffer, const size_t size) noexcept
    {
        GENERAL_TRY() {
            const auto vpath = path_to_vector(path);
            const auto [child, parents]
                = deference_inode_from_path(vpath);
            if ((child->get_stat().st_mode & S_IFMT) == S_IFLNK) {
                const auto len = child->read(buffer, size, 0);
                buffer[std::min(len, size)] = '\0';
                return 0;
            }

            return -EIO;
        }
        GENERAL_CATCH()
    }

    int CowFileSystem::do_mknod(const std::string &path, const mode_t mode, const dev_t device) noexcept
    {
        GENERAL_TRY() {
            auto vpath = path_to_vector(path);
            if (vpath.empty()) {
                return -EINVAL;
            }

            const auto target = vpath.back();
            vpath.pop_back();

            auto [child, parents]
                = deference_inode_from_path(vpath);
            const auto child_stat = child->get_stat();
            child.reset(); // release child, its parents are under capture (so is root) so it cannot be tempered
            if ((child_stat.st_mode & S_IFMT) == S_IFDIR)
            {
                auto dentry = make_child_inode<dentry_t>(child_stat.st_ino, parents.back().get());
                auto inode = dentry.make_inode<inode_t>(target);
                inode.chmod(mode);
                inode.chdev(device);
                return 0;
            }

            return -ENOTDIR;
        }
        GENERAL_CATCH()
    }

    struct statvfs CowFileSystem::do_fstat() noexcept
    {
        const auto free = cfs_basic_filesystem_.cfs_header_block.get_info<allocated_non_cow_blocks>();
        struct statvfs status{};
        status.f_bsize = cfs_basic_filesystem_.static_info_.block_size;
        status.f_frsize = cfs_basic_filesystem_.static_info_.block_size;
        status.f_blocks = cfs_basic_filesystem_.static_info_.data_table_end - cfs_basic_filesystem_.static_info_.data_table_start;
        status.f_bfree = free;
        status.f_bavail = free;
        status.f_files = 0;
        status.f_ffree = 0;
        status.f_favail = 0;
        status.f_fsid = 0;
        status.f_flag = 0;
        status.f_namemax = 255;
        status.f_type = 0x65735546; // FUSE
        return status;
    }

    std::string CowFileSystem::auto_path(const std::string & path) const
    {
        if (!path.empty() && path.front() == '/') {
            return path_calculator(path);
        }

        return path_calculator(cfs_pwd_ + "/" + path);
    }

    bool CowFileSystem::command_main_entry_point(const std::vector<std::string> &vec)
    {
        if (vec.empty()) return true;

        //////////////////////////////////////////////////////////
        //////////////////// GENERAL COMMANDS ////////////////////
        //////////////////////////////////////////////////////////
        if (vec.front() == "quit" || vec.front() == "exit") {
            return false;
        }
        if (vec.front() == "help") {
            help();
        }
        else if (vec.front() == "help_at") {
            help_at(vec);
        }
        else if (vec.front() == "version") {
            version();
        }

        //////////////////////////////////////////////////////
        //////////////////// CFS COMMANDS ////////////////////
        //////////////////////////////////////////////////////
        else if (vec.front() == "ls") {
            ls(vec);
        }
        else if (vec.front() == "copy_to_host") {
            copy_to_host(vec);
        }
        else if (vec.front() =="copy_from_host") {
            copy_from_host(vec);
        }
        else if (vec.front() =="mkdir") {
            mkdir(vec);
        }
        else if (vec.front() =="rmdir") {
            rmdir(vec);
        }
        else if (vec.front() =="del") {
            del(vec);
        }
        else if (vec.front() =="copy") {
            copy(vec);
        }
        else if (vec.front() =="cd") {
            cd(vec);
        }
        else if (vec.front() =="pwd") {
            pwd();
        }
        else if (vec.front() =="free") {
            free();
        }
        else if (vec.front() =="move") {
            move(vec);
        }
        else if (vec.front() =="sync") {
            cfs_basic_filesystem_.sync();
        }
        else if (vec.front() =="cat") {
            cat(vec);
        }
        else if (vec.front() =="snapshot") {
            if (vec.size() == 2) {
                if (const int result = do_snapshot(vec[1]); result != 0) {
                    elog("snapshot: ", strerror(result), "\n");
                }
            } else {
                elog("snapshot [Name]\n");
            }
        }
        else if (vec.front() =="revert") {
            if (vec.size() == 2) {
                if (const int result = do_rollback(vec[1]); result != 0) {
                    elog("rollback: ", strerror(result), "\n");
                }
            } else {
                elog("rollback [Name]\n");
            }
        }
        else if (vec.front() =="delsnapshot") {
            if (vec.size() == 2) {
                if (const int result = do_cleanup(vec[1]); result != 0) {
                    elog("cleanup: ", strerror(result), "\n");
                }
            } else {
                elog("cleanup [Name]\n");
            }
        }


        ////////////////////////////////////////////////////////
        //////////////////// DEBUG COMMANDS ////////////////////
        ////////////////////////////////////////////////////////
        else if (vec.front() == "debug" && vec.size() >= 2)
        {
            if (vec[1] == "cat" && vec.size() >= 3)
            {
                if (vec[2] == "bitmap") {
                    debug_cat_ditmap();
                }
                else if (vec[2] == "journal") {
                    debug_cat_journal();
                }
                else if (vec[2] == "header") {
                    debug_cat_head();
                }
                else if (vec[2] == "attribute" && vec.size() == 4) {
                    debug_cat_attribute(vec);
                } else {
                    elog("Failed to parse command\n");
                }
            }
            else if (vec[1] == "check" && vec.size() == 3)
            {
                if (vec[2] == "hash5") {
                    debug_check_hash5();
                } else {
                    elog("Failed to parse command\n");
                }
            }
            else {
                elog("Failed to parse command\n");
            }
        }
        else {
            elog("Failed to parse command\n");
        }

        return true;
    }
} // cfs