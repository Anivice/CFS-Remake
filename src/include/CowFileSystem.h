#ifndef CFS_COWFILESYSTEM_H
#define CFS_COWFILESYSTEM_H

#include "utils.h"
#include "commandTemplateTree.h"
#include <vector>
#include <string>
#include "cfsBasicComponents.h"
#include <sys/statvfs.h>
#include <filesystem>
#include "inode.h"

namespace cfs
{
    class CowFileSystem {
    private:
        std::string cfs_pwd_ = "/";
        filesystem cfs_basic_filesystem_;
        cfs_journaling_t journaling_;
        cfs_bitmap_block_mirroring_t mirrored_bitmap_;
        cfs_block_attribute_access_t block_attribute_;
        cfs_block_manager_t block_manager_;

    public:
        explicit CowFileSystem(const std::string & path) :
            cfs_basic_filesystem_(path),
            journaling_(&cfs_basic_filesystem_),
            mirrored_bitmap_(&cfs_basic_filesystem_, &journaling_),
            block_attribute_(&cfs_basic_filesystem_, &journaling_),
            block_manager_(&mirrored_bitmap_, &cfs_basic_filesystem_.cfs_header_block, &block_attribute_, &journaling_)
        { }

    private:
        /// wrapper for ls_pwd
        std::vector <std::string> ls_under_pwd_of_cfs(const std::string & /* type is always cfs */);

        static void help();
        static void help_at(const std::vector<std::string> &vec);
        static void version();

        void debug_cat_ditmap();
        void debug_cat_journal();
        void debug_cat_attribute(const std::vector<std::string> &vec);
        void debug_cat_head();
        void debug_check_hash5();
        void ls(const std::vector<std::string> &vec);
        void copy_to_host(const std::vector<std::string> &vec);
        void copy_from_host(const std::vector<std::string> &vec);
        void mkdir(const std::vector<std::string> &vec);
        void rmdir(const std::vector<std::string> &vec);
        void del(const std::vector<std::string> &vec);
        void free();
        void cd(const std::vector<std::string> &vec);
        void pwd();
        void copy(const std::vector<std::string> &vec);
        void move(const std::vector<std::string> &vec);
        void cat(const std::vector<std::string> &vec);

        /// turn path into vector
        static std::vector<std::string> path_to_vector(const std::string & path) noexcept;

        /// calculate relative path, if the provided one is a relative path
        /// and cancel out all the relative jumps like . and ..
        /// return a clean path
        /// @return clean path
        [[nodiscard]] std::string path_calculator(const std::string & path) const noexcept;

        using deferenced_pairs_t = struct {
            std::shared_ptr < inode_t > child;
            std::vector < std::shared_ptr < dentry_t > > parents;
        };

        using vpath_t = std::vector<std::string>;

        dentry_t make_root_inode();

        template < class InodeType >
        requires (std::is_same_v<InodeType, inode_t>
            || std::is_same_v<InodeType, dentry_t>
            || std::is_same_v<InodeType, file_t>)
        InodeType make_child_inode(uint64_t index, inode_t * parent) {
            return InodeType(index, &cfs_basic_filesystem_, &block_manager_, &journaling_, &block_attribute_, parent);
        }

        deferenced_pairs_t deference_inode_from_path(vpath_t);

        [[nodiscard]] std::string auto_path(const std::string & path) const;

        template < typename Type >
        bool is_snapshot_entry(const Type & list)
        {
            return std::ranges::any_of(list, [&](const auto & p)
            {
                if (p == nullptr) return false;
                if (block_attribute_.get<block_status>(p->get_stat().st_ino)
                    == BLOCK_FROZEN_AND_IS_ENTRY_POINT_OF_SNAPSHOTS_0x01)
                {
                    return true;
                }
                return false;
            });
        }

        template < typename Type1, typename Type2 >
        bool check_entry(const Type1 & list, const Type2 & child)
        {
            std::vector< inode_t *> children;
            children.push_back(child.get());
            return is_snapshot_entry(list) || is_snapshot_entry(children); // any_of
        };


    public:
        /// get attributes from an inode by path
        /// @param path Full path
        /// @param stbuf stat buffer
        /// @return 0 means good, negative + errno means error
        int do_getattr(const std::string & path, struct stat *stbuf) noexcept;

        /// read dir
        /// @param path Full path
        /// @param entries path entry, '.' and '..' included
        /// @return 0 means good, negative + errno means error
        int do_readdir(const std::string & path, std::vector < std::string > & entries) noexcept;

        /// make a dir
        /// @param path Full path
        /// @param mode permissions
        /// @return 0 means good, negative + errno means error
        int do_mkdir(const std::string & path, mode_t mode) noexcept;

        /// change ownership info
        /// @param path Full path
        /// @param uid UID
        /// @param gid GID
        /// @return 0 means good, negative + errno means error
        int do_chown(const std::string & path, uid_t uid, gid_t gid) noexcept;

        /// change permission info
        /// @param path Full path
        /// @param mode permissions
        /// @return 0 means good, negative + errno means error
        int do_chmod(const std::string &  path, mode_t mode) noexcept;

        /// Create a non-dir file inode
        /// @param path Full path
        /// @param mode permissions
        /// @return 0 means good, negative + errno means error
        int do_create(const std::string & path, mode_t mode) noexcept;

        /// sync the filesystem
        /// @return 0 means good, negative + errno means error
        int do_flush() noexcept;

        /// release a file. actually it only flush the fs
        int do_release(const std::string &) noexcept { return do_flush(); }

        /// Check for permissions, see if it can be read
        /// @param path Full path
        /// @param mode permissions
        /// @return 0 means good, negative + errno means error
        int do_access(const std::string & path, int mode) noexcept;

        /// open a file, but actually it checks for permissions and existences only
        /// @param path Full path
        /// @return 0 means good, negative + errno means error
        int do_open(const std::string & path) noexcept;

        /// read a file
        /// @param path Full path
        /// @param buffer
        /// @param size
        /// @param offset
        /// @return 0 means good, negative + errno means error
        int do_read(const std::string & path, char * buffer, size_t size, off_t offset) noexcept;

        /// write to a file
        /// @param path Full path
        /// @param buffer
        /// @param size
        /// @param offset
        /// @return 0 means good, negative + errno means error
        int do_write(const std::string &  path, const char * buffer, size_t size, off_t offset) noexcept;

        /// change time
        /// @param path Full path
        /// @param tv [0] => atim, [1] => mtim
        /// @return 0 means good, negative + errno means error
        int do_utimens(const std::string & path, const timespec tv[2]) noexcept;

        /// remove a file
        /// @param path Full path
        /// @return 0 means good, negative + errno means error
        int do_unlink(const std::string & path) noexcept;

        /// remove an empty directory
        /// @param path Full path
        /// @return 0 means good, negative + errno means error
        int do_rmdir(const std::string & path) noexcept;

        /// Sync filesystem
        /// @return 0 means good, negative + errno means error
        int do_fsync(const std::string &, int) noexcept { return do_flush(); }

        /// wrapped to sync
        /// @return 0 means good, negative + errno means error
        int do_releasedir(const std::string & ) noexcept { return do_flush(); }

        /// wrapped to sync
        /// @return 0 means good, negative + errno means error
        int do_fsyncdir(const std::string &, int) noexcept { return do_flush(); }

        /// Resize a file
        /// @param path Full path
        /// @param size new size
        /// @return 0 means good, negative + errno means error
        int do_truncate(const std::string & path, off_t size) noexcept;

        /// Create a symlink from path to target
        /// @param path Src full path
        /// @param target target full path
        /// @return 0 means good, negative + errno means error
        int do_symlink(const std::string & path, const std::string & target) noexcept;

        /// Create a snapshot by name
        /// @param name snapshot name
        /// @return 0 means good, negative + errno means error
        int do_snapshot(const std::string & name) noexcept;

        /// Rollback to a snapshot state
        /// @param name snapshot name
        /// @return 0 means good, negative + errno means error
        int do_rollback(const std::string & name) noexcept;

        /// Remove a snapshot
        /// @param name snapshot name
        /// @return 0 means good, negative + errno means error
        int do_cleanup(const std::string & name) noexcept;

        /// Relink an inode
        /// @param path Old path
        /// @param new_path New path
        /// @return 0 means good, negative + errno means error
        int do_rename(const std::string & path, const std::string & new_path, int flags) noexcept;

        /// Create an empty file with defined size
        /// @param path full path
        /// @param mode perm
        /// @param offset offset
        /// @param length len
        /// @return 0 means good, negative + errno means error
        int do_fallocate(const std::string & path, int mode, off_t offset, off_t length) noexcept;

        /// wrapped to do_getattr
        int do_fgetattr(const std::string & path, struct stat * statbuf) noexcept { return do_getattr(path, statbuf); }

        /// wrapped to do_truncate
        int do_ftruncate(const std::string & path, off_t length) noexcept { return do_truncate(path, length); }

        /// read link content
        /// @param path full path
        /// @param buffer buffer
        /// @param size buffer size
        /// @return 0 means good, negative + errno means error
        int do_readlink(const std::string & path, char * buffer, size_t size) noexcept;

        /// Make a special file
        /// @param path full path
        /// @param mode perm
        /// @param device st_dev
        /// @return 0 means good, negative + errno means error
        int do_mknod(const std::string &  path, mode_t mode, dev_t device) noexcept;

        /// get file system state
        struct statvfs do_fstat() noexcept;

        /// wrapper for entry point
        bool command_main_entry_point(const std::vector<std::string> & vec);

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