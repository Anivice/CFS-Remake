#ifndef CFS_COWFILESYSTEM_H
#define CFS_COWFILESYSTEM_H

#include "utils.h"
#include "commandTemplateTree.h"
#include <vector>
#include <string>
#include "cfsBasicComponents.h"

namespace cfs
{
    class CowFileSystem {
    private:
        std::string cfs_pwd_;
        std::string host_pwd_;

        filesystem cfs_basic_filesystem_;
        cfs_journaling_t journaling_;
        cfs_bitmap_block_mirroring_t mirrored_bitmap_;
        cfs_block_attribute_access_t block_attribute_;

    public:
        explicit CowFileSystem(const std::string & path) :
            cfs_basic_filesystem_(path),
            journaling_(&cfs_basic_filesystem_),
            mirrored_bitmap_(&cfs_basic_filesystem_, &journaling_),
            block_attribute_(&cfs_basic_filesystem_, &journaling_)
        { }

    private:
        /// wrapper for ls_pwd
        std::vector <std::string> ls_under_pwd_of_cfs(const std::string & /* type is always cfs */);

        /// wrapper for entry point
        bool command_main_entry_point(const std::vector<std::string> & vec);

    public:
        /// start readline utility mode
        void readline()
        {
            cmdTpTree::read_command(
                [&](const std::vector<std::string> & vec) { return command_main_entry_point(vec); },
                [&](const std::string & type) { return ls_under_pwd_of_cfs(type); },
                "cfs> ");
        }
    };
} // cfs

#endif //CFS_COWFILESYSTEM_H