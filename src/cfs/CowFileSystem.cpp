#include "CowFileSystem.h"
#include "args.h"
#include "cfs.h"
#include "routes.h"
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

#define print_case(name) case cfs::name: ss << cfs::name##_c_str; break;
#define print_default(data) default: ss << std::hex << (uint32_t)(data) << std::dec; break;

#define replicate(name, sss) \
    case cfs::name:             ss  << highlight(cfs::name##_c_str)             << sss; break; \
    case cfs::name##_Completed: ss  << highlight(cfs::name##_Completed_c_str)   << sss; break; \
    case cfs::name##_Failed:    ss  << highlight(cfs::name##_Failed_c_str)      << sss; break;

std::string print_attribute(const uint32_t val)
{
    const auto * attr_ = (cfs::cfs_block_attribute_t *)&val;
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

namespace cfs
{
    std::vector<std::string> CowFileSystem::ls_under_pwd_of_cfs(const std::string &)
    {
        return {};
    }

    bool CowFileSystem::command_main_entry_point(const std::vector<std::string> &vec)
    {
        if (vec.empty()) return true;

        if (vec.front() == "quit" || vec.front() == "exit") {
            return false;
        }

        if (vec.front() == "help") {
            std::cout << cmdTpTree::command_template_tree.get_help();
        }
        else if (vec.front() == "help_at") {
            try {
                const std::vector help_path(vec.begin() + 1, vec.end());
                std::ranges::for_each(help_path, [](const auto & v) { std::cout << v << " "; });
                std::cout << ": " << cmdTpTree::command_template_tree.get_help(help_path) << std::endl;
            } catch (std::exception & e) {
                elog(e.what(), "\n");
            }
        }
        else if (vec.front() == "version") {
            std::cout.write(reinterpret_cast<const char *>(version_text), version_text_len);
            std::cout << std::endl;
        }
        else if (vec.front() == "debug" && vec.size() >= 2)
        {
            if (vec[1] == "cat" && vec.size() >= 3)
            {
                if (vec[2] == "bitmap")
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

                        if (mirrored_bitmap_.get_bit(i))
                        {
                            switch (const auto attr = block_attribute_.get<block_type>(i))
                            {
                                case CowRedundancy:
                                    std::cout << color::color(3,3,3) << "R" << color::no_color();
                                    break;
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
                        } else {
                            std::cout << ".";
                        }
                    }

                    if (fs_bitmap_size % col != 0) {
                        std::cout << "\n";
                    }
                }
                else if (vec[2] == "journal")
                {
                    const auto journal = this->journaling_.dump_actions();
                    std::ranges::for_each(journal, [](const cfs_action_t & action) {
                        std::cout << translate_action_into_literal(action) << std::endl;
                    });
                }
                else if (vec[2] == "attribute" && vec.size() == 4)
                {
                    const auto loc = vec[3];
                    const auto pos = std::strtol(loc.c_str(), nullptr, 10);
                    const auto attr = block_attribute_.get(pos);
                    if (!mirrored_bitmap_.get_bit(pos)) wlog("Not allocated!\n");
                    std::cout << print_attribute(*(uint32_t*)&attr) << std::endl;
                }
                else {
                    elog("Failed to parse command\n");
                }
            }
            else if (vec[1] == "check" && vec.size() == 3)
            {
                if (vec[2] == "block_hash5")
                {
                    const auto len = this->cfs_basic_filesystem_.static_info_.data_table_end - cfs_basic_filesystem_.static_info_.data_table_start;
                    for (uint64_t i = 0; i < len; i++)
                    {
                        if (mirrored_bitmap_.get_bit(i))
                        {
                            auto pg = cfs_basic_filesystem_.lock(i + cfs_basic_filesystem_.static_info_.data_table_start);
                            const uint8_t checksum = cfs::utils::arithmetic::hash5((uint8_t*)pg.data(), pg.size());
                            const auto comp = block_attribute_.get<block_checksum>(i);
                            if (!block_attribute_.get<newly_allocated_thus_no_cow>(i) && comp != checksum) {
                                elog(std::dec, "Checksum mismatch at block index ", i, ", attribute says it's ", (uint8_t)comp, ", but we have ", checksum, "\n");
                            }
                        }
                    }
                }
                else {
                    elog("Failed to parse command\n");
                }
            }
            else {
                elog("Failed to parse command\n");
            }
        }

        return true;
    }
} // cfs