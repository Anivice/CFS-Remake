#define FUSE_USE_VERSION 317
#include <fuse.h>
#include <atomic>
#include <algorithm>
#include <cstdlib>
#include <sys/ioctl.h>
#include <pthread.h>
#include "routes.h"
#include "logger.h"
#include "args.h"
#include "CowFileSystem.h"
#include "version.h"

#ifdef CFS_COMPILE_FUSE
namespace utils = cfs::utils;

utils::PreDefinedArgumentType::PreDefinedArgument mountMainArgument = {
    { .short_name = 'h', .long_name = "help",       .argument_required = false, .description = "Show help" },
    { .short_name = 'v', .long_name = "version",    .argument_required = false, .description = "Show version" },
    { .short_name = 'p', .long_name = "path",       .argument_required = true,  .description = "Path to CFS archive file" },
    { .short_name = 'f', .long_name = "fuse",       .argument_required = true,  .description = "Fuse arguments" },
    { .short_name = 'e', .long_name = "endpoint",   .argument_required = true,  .description = "Mount endpoint" },
};

extern "C" struct snapshot_ioctl_msg {
    char snapshot_name [255];
    uint64_t action;
};
#define CFS_PUSH_SNAPSHOT _IOW('M', 0x42, struct snapshot_ioctl_msg)

#define CREATE      (0)
#define ROLLBACKTO  (1)
#define DELETE      (2)

inline void set_thread_name(const char * name)
{
    pthread_setname_np(pthread_self(), name);
}

std::unique_ptr<cfs::CowFileSystem> cfs_entity_ptr;

static int fuse_do_getattr(const char *path, struct stat *stbuf, fuse_file_info *) {
    set_thread_name("fuse_do_getattr");
    cfs::stat cfs_stbuf{};
    const auto result = cfs_entity_ptr->do_getattr(path, &cfs_stbuf);
    stbuf->st_dev = cfs_stbuf.st_dev;
    stbuf->st_ino = cfs_stbuf.st_ino;
    stbuf->st_nlink = cfs_stbuf.st_nlink;
    stbuf->st_mode = cfs_stbuf.st_mode;
    stbuf->st_uid = cfs_stbuf.st_uid;
    stbuf->st_gid = cfs_stbuf.st_gid;
    stbuf->st_rdev = cfs_stbuf.st_rdev;
    stbuf->st_size = cfs_stbuf.st_size;
    stbuf->st_blksize = cfs_stbuf.st_blksize;
    stbuf->st_blocks = cfs_stbuf.st_blocks;
    stbuf->st_atim = cfs_stbuf.st_atim;
    stbuf->st_mtim = cfs_stbuf.st_mtim;
    stbuf->st_ctim = cfs_stbuf.st_ctim;
    return result;
}

static int fuse_do_readdir(const char *path,
                           void *buffer,
                           const fuse_fill_dir_t filler,
                           off_t, fuse_file_info *, fuse_readdir_flags) {
    set_thread_name("fuse_do_readdir");
    std::vector<std::string> vector_buffer;
    const int status = cfs_entity_ptr->do_readdir(path, vector_buffer);
    if (status != 0) return status;
    for (const auto &name: vector_buffer) {
        filler(buffer, name.c_str(), nullptr, 0, FUSE_FILL_DIR_DEFAULTS);
    }
    return status;
}

static int fuse_do_mkdir(const char *path, const mode_t mode) {
    set_thread_name("fuse_do_mkdir");
    return cfs_entity_ptr->do_mkdir(path, mode);
}

static int fuse_do_chmod(const char *path, const mode_t mode, fuse_file_info *) {
    set_thread_name("fuse_do_chmod");
    return cfs_entity_ptr->do_chmod(path, mode);
}

static int fuse_do_chown(const char *path, const uid_t uid, const gid_t gid, fuse_file_info *) {
    set_thread_name("fuse_do_chown");
    return cfs_entity_ptr->do_chown(path, uid, gid);
}

static int fuse_do_create(const char *path, const mode_t mode, fuse_file_info *) {
    set_thread_name("fuse_do_create");
    return cfs_entity_ptr->do_create(path, mode);
}

static int fuse_do_flush(const char *, fuse_file_info *) {
    set_thread_name("fuse_do_flush");
    return cfs_entity_ptr->do_flush();
}

static int fuse_do_release(const char *path, fuse_file_info *) {
    set_thread_name("fuse_do_release");
    return cfs_entity_ptr->do_release(path);
}

static int fuse_do_access(const char *path, const int mode) {
    set_thread_name("fuse_do_access");
    return cfs_entity_ptr->do_access(path, mode);
}

static int fuse_do_open(const char *path, fuse_file_info *) {
    set_thread_name("fuse_do_open");
    return cfs_entity_ptr->do_open(path);
}

static int fuse_do_read(const char *path, char *buffer, const size_t size, const off_t offset, fuse_file_info *) {
    set_thread_name("fuse_do_read");
    return cfs_entity_ptr->do_read(path, buffer, size, offset);
}

static int fuse_do_write(const char *path, const char *buffer, const size_t size, const off_t offset,
                         fuse_file_info *) {
    set_thread_name("fuse_do_write");
    return cfs_entity_ptr->do_write(path, buffer, size, offset);
}

static int fuse_do_utimens(const char *path, const timespec tv[2], fuse_file_info *fi) {
    set_thread_name("fuse_do_utimens");
    return cfs_entity_ptr->do_utimens(path, tv);
}

static int fuse_do_unlink(const char *path) {
    set_thread_name("fuse_do_unlink");
    return cfs_entity_ptr->do_unlink(path);
}

static int fuse_do_rmdir(const char *path) {
    set_thread_name("fuse_do_rmdir");
    return cfs_entity_ptr->do_rmdir(path);
}

static int fuse_do_fsync(const char *path, int, fuse_file_info *) {
    set_thread_name("fuse_do_fsync");
    return cfs_entity_ptr->do_fsync(path, 0);
}

static int fuse_do_releasedir(const char *path, fuse_file_info *) {
    set_thread_name("fuse_do_releasedir");
    return cfs_entity_ptr->do_releasedir(path);
}

static int fuse_do_fsyncdir(const char *path, int, fuse_file_info *) {
    set_thread_name("fuse_do_fsyncdir");
    return cfs_entity_ptr->do_fsyncdir(path, 0);
}

static int fuse_do_truncate(const char *path, const off_t size, fuse_file_info *) {
    set_thread_name("fuse_do_truncate");
    return cfs_entity_ptr->do_truncate(path, size);
}

static int fuse_do_symlink(const char *path, const char *target) {
    set_thread_name("fuse_do_symlink");
    return cfs_entity_ptr->do_symlink(path, target);
}

static int fuse_do_ioctl(const char *, const unsigned int cmd, void *,
                         fuse_file_info *, const unsigned int flags, void *data) {
    set_thread_name("fuse_do_ioctl");
    if (!(flags & FUSE_IOCTL_DIR)) {
        return -ENOTTY;
    }

    if (cmd == CFS_PUSH_SNAPSHOT) {
        const auto *msg = static_cast<snapshot_ioctl_msg *>(data);
        if (msg->action == CREATE) {
            return cfs_entity_ptr->do_snapshot(msg->snapshot_name);
        } else if (msg->action == ROLLBACKTO) {
            return cfs_entity_ptr->do_rollback(msg->snapshot_name);
        } else if (msg->action == DELETE) {
            return cfs_entity_ptr->do_cleanup(msg->snapshot_name);
        }
    }

    return -EINVAL;
}

static int fuse_do_rename(const char *path, const char *name, unsigned int flags) {
    set_thread_name("fuse_do_rename");
    if (flags == RENAME_NOREPLACE) {
        return cfs_entity_ptr->do_rename(path, name, 0);
    } else if (flags == RENAME_EXCHANGE) {
        return cfs_entity_ptr->do_rename(path, name, 1);
    } else {
        return cfs_entity_ptr->do_rename(path, name, 1); // default
    }
}

static int fuse_do_fallocate(const char *path, const int mode, const off_t offset, const off_t length,
                             fuse_file_info *) {
    set_thread_name("fuse_do_fallocate");
    return cfs_entity_ptr->do_fallocate(path, mode, offset, length);
}

static int fuse_do_readlink(const char *path, char *buffer, const size_t size) {
    set_thread_name("fuse_do_readlink");
    return cfs_entity_ptr->do_readlink(path, buffer, size);
}

void fuse_do_destroy(void *) {
    set_thread_name("fuse_do_destroy");
}

void *fuse_do_init(fuse_conn_info *conn, fuse_config *) {
    set_thread_name("fuse_do_init");
    fuse_set_feature_flag(conn, FUSE_CAP_IOCTL_DIR);
    conn->max_write = 256 * 1024 * 1024;
    return nullptr;
}

static int fuse_do_mknod(const char *path, const mode_t mode, const dev_t device) {
    set_thread_name("fuse_do_mknod");
    return cfs_entity_ptr->do_mknod(path, mode, device);
}

int fuse_statfs(const char *, struct statvfs *status) {
    set_thread_name("fuse_statfs");
    *status = cfs_entity_ptr->do_fstat();
    return 0;
}

static fuse_operations fuse_operation_vector_table =
{
    .getattr = fuse_do_getattr,
    .readlink = fuse_do_readlink,
    .mknod = fuse_do_mknod,
    .mkdir = fuse_do_mkdir,
    .unlink = fuse_do_unlink,
    .rmdir = fuse_do_rmdir,
    .symlink = fuse_do_symlink,
    .rename = fuse_do_rename,
    .chmod = fuse_do_chmod,
    .chown = fuse_do_chown,
    .truncate = fuse_do_truncate,
    .open = fuse_do_open,
    .read = fuse_do_read,
    .write = fuse_do_write,
    .statfs = fuse_statfs,
    .flush = fuse_do_flush,
    .release = fuse_do_release,
    .fsync = fuse_do_fsync,
    .opendir = fuse_do_open,
    .readdir = fuse_do_readdir,
    .releasedir = fuse_do_releasedir,
    .fsyncdir = fuse_do_fsyncdir,
    .init = fuse_do_init,
    .destroy = fuse_do_destroy,
    .access = fuse_do_access,
    .create = fuse_do_create,
    .utimens = fuse_do_utimens,
    .ioctl = fuse_do_ioctl,
    .fallocate = fuse_do_fallocate,
};

int fuse_redirect(const int argc, char ** argv)
{
    fuse_args args = FUSE_ARGS_INIT(argc, argv);
    if (fuse_opt_parse(&args, nullptr, nullptr, nullptr) == -1)
    {
        elog("FUSE initialization failed, errno: ", strerror(errno), " (", errno, ")\n");
        return EXIT_FAILURE;
    }

    const int ret = fuse_main(args.argc, args.argv, &fuse_operation_vector_table, nullptr);
    fuse_opt_free_args(&args);
    return ret;
}

static std::vector<std::string> splitString(const std::string& s, const char delim = ' ')
{
    std::vector<std::string> parts;
    std::string token;
    std::stringstream ss(s);

    while (std::getline(ss, token, delim)) {
        parts.push_back(token);
    }

    return parts;
}

int mount_main(int argc, char **argv)
{
    try
    {
        const utils::PreDefinedArgumentType PreDefinedArguments(mountMainArgument);
        utils::ArgumentParser ArgumentParser(argc, argv, PreDefinedArguments);
        const auto parsed = ArgumentParser.parse();
        if (parsed.contains("help")) {
            std::cout << *argv << " <MOUNT> [Arguments [OPTIONS...]...]" << std::endl;
            std::cout << PreDefinedArguments.print_help();
            return EXIT_SUCCESS;
        }

        if (parsed.contains("version")) {
            std::cout << *argv << " [MOUNT]" << std::endl;
            std::cout.write(reinterpret_cast<const char *>(version_text), version_text_len);
            std::cout << std::endl;
            return EXIT_SUCCESS;
        }

        std::string arg_val;
        if (parsed.contains("fuse")) {
            arg_val = parsed.at("fuse");
        }

        std::unique_ptr<char*[]> fuse_argv;
        std::vector<std::string> fuse_args = splitString(arg_val, ' ');
        if constexpr (DEBUG) {
            // fuse_args.emplace_back("-s");
            // fuse_args.emplace_back("-d");
            fuse_args.emplace_back("-f");
        }

        if (!parsed.contains("endpoint")) {
            elog("No endpoint specified!\n");
        }

        fuse_args.push_back(parsed.at("endpoint"));
        fuse_args.emplace_back("-o");
        fuse_args.emplace_back("subtype=cfs");
        fuse_args.emplace_back("-o");
        fuse_args.emplace_back("fsname=" + parsed.at("path"));

        fuse_argv = std::make_unique<char*[]>(fuse_args.size() + 1);
        fuse_argv[0] = argv[0]; // redirect
        for (int i = 0; i < static_cast<int>(fuse_args.size()); ++i) {
            fuse_argv[i + 1] = const_cast<char *>(fuse_args[i].c_str());
        }

        ilog("Mounting filesystem ", parsed.at("path"), " to ", parsed.at("endpoint"), "\n");
        ilog("Arguments passed down to fuse from command line are: ", fuse_args, "\n");

        const int d_fuse_argc = static_cast<int>(fuse_args.size()) + 1;
        char ** d_fuse_argv = fuse_argv.get();
        return fuse_redirect(d_fuse_argc, d_fuse_argv);
    }
    catch (const std::exception & e)
    {
        elog(e.what(), "\n");
        return EXIT_FAILURE;
    }
}

#else

int mount_main(int argc, char** argv)
{
    elog("CFS is not compiled with FUSE\n");
    return EXIT_FAILURE;
}

#endif
