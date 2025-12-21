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

namespace cfs {
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
        else if (vec.front() == "debug" && vec.size() >= 2) {
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
                            switch (const auto attr = block_attribute_.get(i); attr.block_type)
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
            }
        }

        return true;
    }
} // cfs