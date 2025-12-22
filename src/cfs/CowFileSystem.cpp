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

std::string print_attribute(const uint32_t val)
{
    const auto * attr_ = (cfs::cfs_block_attribute_t *)&val;
    const auto & attr = *attr_;
    std::stringstream ss;
    ss << "     Status: ";
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
    case cfs::name:             ss << cfs::name##_c_str << " " << std::dec << action.action_data.action_plain.action_param1; break; \
    case cfs::name##_Completed: ss << cfs::name##_Completed_c_str; break; \
    case cfs::name##_Failed:    ss << cfs::name##_Failed_c_str; break;

static std::string translate_action_into_literal(const cfs::cfs_action_t & action)
{
    std::stringstream ss;
    switch (action.action_data.action_plain.action)
    {
        print_case(ActionFinishedAndNoExceptionCaughtDuringTheOperation);

        case cfs::CorruptionDetected:
            ss << cfs::CorruptionDetected_c_str << " ";
            switch (action.action_data.action_plain.action_param0) {
                print_case(BitmapMirrorInconsistent);
                print_case(FilesystemBlockExhausted);
                print_default(action.action_data.action_plain.action_param0)
            }
        break;

        case cfs::FilesystemAttributeModification:
            if (action.action_data.action_plain.action_param0 == action.action_data.action_plain.action_param1) {
                ss << cfs::FilesystemAttributeModification_c_str << " ACCESS (Index=" << std::dec << action.action_data.action_plain.action_param2 << ")\n";
                ss << print_attribute(action.action_data.action_plain.action_param0);
            } else {
                ss << cfs::FilesystemAttributeModification_c_str << " MODIFY (Index=" << std::dec << action.action_data.action_plain.action_param2 << ")\n";
                ss << "Before: \n" << print_attribute(action.action_data.action_plain.action_param0);
                ss << "After: \n" << print_attribute(action.action_data.action_plain.action_param1);
            }
        break;

        case cfs::FilesystemBitmapModification:
            ss << cfs::FilesystemBitmapModification_c_str
                << " From " << action.action_data.action_plain.action_param0
                << " To " << action.action_data.action_plain.action_param1
                << " At " << action.action_data.action_plain.action_param2;
        break;

        case cfs::AttemptedFixFinishedAndAssumedFine:
            ss << cfs::AttemptedFixFinishedAndAssumedFine_c_str << " ";
            switch (action.action_data.action_plain.action_param0) {
                print_case(BitmapMirrorInconsistent);
                print_case(FilesystemBlockExhausted);
                print_default(action.action_data.action_plain.action_param0)
            }
        break;

        case cfs::GlobalTransaction:
            ss << cfs::GlobalTransaction_c_str << " ";
            switch (action.action_data.action_plain.action_param0) {
                case cfs::GlobalTransaction_AllocateBlock: ss << cfs::GlobalTransaction_AllocateBlock_c_str; break;
                case cfs::GlobalTransaction_AllocateBlock_Completed: ss << cfs::GlobalTransaction_AllocateBlock_Completed_c_str; break;
                case cfs::GlobalTransaction_AllocateBlock_Failed: ss << cfs::GlobalTransaction_AllocateBlock_Failed_c_str; break;

                print_transaction_arg1(GlobalTransaction_DeallocateBlock);
                case cfs::GlobalTransaction_CreateRedundancy:
                    ss << cfs::GlobalTransaction_CreateRedundancy_c_str << " " << std::dec << action.action_data.action_plain.action_param1;
                    ss << " At " << action.action_data.action_plain.action_param2;
                break;
                case cfs::GlobalTransaction_CreateRedundancy_Completed:
                    ss << cfs::GlobalTransaction_CreateRedundancy_Completed_c_str;
                break;
                case cfs::GlobalTransaction_CreateRedundancy_Failed:
                    ss << cfs::GlobalTransaction_CreateRedundancy_Failed_c_str;
                break;
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
            if (vec[1] == "cat" && vec.size() == 3)
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
            }
            if (vec[1] == "check" && vec.size() == 3)
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
            }
        }

        return true;
    }
} // cfs