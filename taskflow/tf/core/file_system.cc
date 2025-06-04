
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <filesystem>

#include "absl/cleanup/cleanup.h"
#include "butil/logging.h"
#include "core/file_system.h"
#include "google/protobuf/text_format.h"
#include "json2pb/json_to_pb.h"
#include "json2pb/pb_to_json.h"

namespace ecm {

Status FileSystem::IsDirectory(const std::string &name) {
  // Check if path exists.
  // TODO(sami):Forward token to other methods once migration is complete.
  ECM_RETURN_IF_ERROR(FileExists(name));
  FileStatistics stat;
  ECM_RETURN_IF_ERROR(Stat(name, &stat));
  if (stat.is_directory) {
    return OkStatus();
  }
  return Status(StatusCode::kFailedPrecondition, "Not a directory");
}

Status FileSystem::RecursivelyCreateDir(const std::string &) {
  return Status(StatusCode::kUnimplemented, "unimplemented");
}

Status FileSystem::DeleteRecursively(const std::string &) {
  return Status(StatusCode::kUnimplemented, "unimplemented");
}

Status FileSystem::Touch(const std::string &) {
  return Status(StatusCode::kUnimplemented, "unimplemented");
}

Status FileSystem::NewLockFile(const std::string &,
                               std::unique_ptr<LockFile> *) {
  return Status(StatusCode::kUnimplemented, "unimplemented");
}

class PosixFileSystem : public FileSystem {
public:
  PosixFileSystem() {}

  ~PosixFileSystem() override {}

  Status
  NewRandomAccessFile(const std::string &filename,
                      std::unique_ptr<RandomAccessFile> *result) override;
  Status NewWritableFile(const std::string &fname,
                         std::unique_ptr<WritableFile> *result) override;
  Status NewAppendableFile(const std::string &fname,
                           std::unique_ptr<WritableFile> *result) override;
  Status NewReadOnlyMemoryRegionFromFile(
      const std::string &filename,
      std::unique_ptr<ReadOnlyMemoryRegion> *result) override;
  Status FileExists(const std::string &fname) override;
  Status GetChildren(const std::string &dir,
                     std::vector<std::string> *result) override;
  Status Stat(const std::string &fname, FileStatistics *stats) override;
  Status DeleteFile(const std::string &fname) override;
  Status CreateDir(const std::string &name) override;
  Status RecursivelyCreateDir(const std::string &name) override;
  Status DeleteDir(const std::string &name) override;
  Status DeleteRecursively(const std::string &name) override;
  Status GetFileSize(const std::string &fname, uint64_t *size) override;
  Status RenameFile(const std::string &src, const std::string &target) override;
  Status CopyFile(const std::string &src, const std::string &target) override;
  Status Touch(const std::string &fname) override;
  Status NewLockFile(const std::string &fname,
                     std::unique_ptr<LockFile> *result) override;
};

FileSystem *FileSystem::Default() {
  static PosixFileSystem posix_fs;
  return &posix_fs;
}

// 128KB of copy buffer
constexpr size_t kPosixCopyFileBufferSize = 128 * 1024;

// pread() based random-access
class PosixRandomAccessFile : public RandomAccessFile {
private:
  std::string filename_;
  int fd_;

public:
  PosixRandomAccessFile(const std::string &fname, int fd)
      : filename_(fname), fd_(fd) {}
  ~PosixRandomAccessFile() override {
    if (::close(fd_) < 0) {
      LOG(ERROR) << "close() failed: " << strerror(errno);
    }
  }

  Status Name(absl::string_view *result) const override {
    *result = filename_;
    return OkStatus();
  }

  Status Read(uint64_t offset, size_t n, absl::string_view *result,
              char *scratch) const override {
    Status s;
    char *dst = scratch;
    while (n > 0 && s.ok()) {
      // Some platforms, notably macs, throw EINVAL if pread is asked to read
      // more than fits in a 32-bit integer.
      size_t requested_read_length;
      if (n > INT32_MAX) {
        requested_read_length = INT32_MAX;
      } else {
        requested_read_length = n;
      }
      ssize_t r =
          ::pread(fd_, dst, requested_read_length, static_cast<off_t>(offset));
      if (r > 0) {
        dst += r;
        n -= r;
        offset += r;
      } else if (r == 0) {
        s = Status(StatusCode::kOutOfRange, "Read less bytes than requested");
      } else if (errno == EINTR || errno == EAGAIN) {
        // Retry
      } else {
        s = IOError(filename_, errno);
      }
    }
    *result = absl::string_view(scratch, dst - scratch);
    return s;
  }

  Status Read(uint64_t offset, size_t n, absl::Cord *cord) const override {
    if (n == 0) {
      return OkStatus();
    }
    if (n < 0) {
      return FmtStatus(StatusCode::kInvalidArgument,
                       "Attempting to read {} bytes. You cannot read a "
                       "negative number of bytes.",
                       n);
    }

    char *scratch = new char[n];
    if (scratch == nullptr) {
      return FmtStatus(StatusCode::kResourceExhausted,
                       "Unable to allocate {} bytes for file reading.", n);
    }

    absl::string_view tmp;
    Status s = Read(offset, n, &tmp, scratch);

    absl::Cord tmp_cord = absl::MakeCordFromExternal(
        absl::string_view(static_cast<char *>(scratch), tmp.size()),
        [scratch](absl::string_view) { delete[] scratch; });
    cord->Append(tmp_cord);
    return s;
  }
};

class PosixWritableFile : public WritableFile {
private:
  std::string filename_;
  FILE *file_;

public:
  PosixWritableFile(const std::string &fname, FILE *f)
      : filename_(fname), file_(f) {}

  ~PosixWritableFile() override {
    if (file_ != nullptr) {
      // Ignoring any potential errors
      ::fclose(file_);
    }
  }

  Status Append(absl::string_view data) override {
    size_t r = ::fwrite(data.data(), 1, data.size(), file_);
    if (r != data.size()) {
      return IOError(filename_, errno);
    }
    return OkStatus();
  }

  // \brief Append 'cord' to the file.
  Status Append(const absl::Cord &cord) override {
    for (const auto &chunk : cord.Chunks()) {
      size_t r = ::fwrite(chunk.data(), 1, chunk.size(), file_);
      if (r != chunk.size()) {
        return IOError(filename_, errno);
      }
    }
    return OkStatus();
  }

  Status Close() override {
    if (file_ == nullptr) {
      return IOError(filename_, EBADF);
    }
    Status result;
    if (::fclose(file_) != 0) {
      result = IOError(filename_, errno);
    }
    file_ = nullptr;
    return result;
  }

  Status Flush() override {
    if (::fflush(file_) != 0) {
      return IOError(filename_, errno);
    }
    return OkStatus();
  }

  Status Name(absl::string_view *result) const override {
    *result = filename_;
    return OkStatus();
  }

  Status Sync() override {
    Status s;
    if (::fflush(file_) != 0) {
      s = IOError(filename_, errno);
    }
    return s;
  }

  Status Tell(int64_t *position) override {
    Status s;
    *position = ::ftell(file_);

    if (*position == -1) {
      s = IOError(filename_, errno);
    }

    return s;
  }

  Status Seek(int64_t position) override {
    Status s;
    if (::fseek(file_, position, SEEK_SET) != 0) {
      s = IOError(filename_, errno);
    }
    return s;
  }
};

class PosixReadOnlyMemoryRegion : public ReadOnlyMemoryRegion {
public:
  PosixReadOnlyMemoryRegion(const void *address, uint64_t length)
      : ReadOnlyMemoryRegion(address, length) {}
  ~PosixReadOnlyMemoryRegion() override {
    if (address_) {
      ::munmap(const_cast<void *>(address_), length_);
    }
  }
};

class PosixLockFile : public LockFile {
public:
  PosixLockFile(const std::string &fname, int fd) : fname_(fname), fd_(fd) {}
  ~PosixLockFile() {
    if (fd_ > 0) {
      ::close(fd_);
      ::unlink(fname_.c_str());
    }
  }

private:
  std::string fname_;
  int fd_ = -1;
};

Status PosixFileSystem::NewRandomAccessFile(
    const std::string &fname, std::unique_ptr<RandomAccessFile> *result) {
  Status s;
  int fd = ::open(fname.c_str(), O_RDONLY);
  if (fd < 0) {
    s = IOError(fname, errno);
  } else {
    result->reset(new PosixRandomAccessFile(fname, fd));
  }
  return s;
}

absl::Status
PosixFileSystem::NewWritableFile(const std::string &fname,
                                 std::unique_ptr<WritableFile> *result) {
  Status s;
  FILE *f = ::fopen(fname.c_str(), "w");
  if (f == nullptr) {
    s = IOError(fname, errno);
  } else {
    result->reset(new PosixWritableFile(fname, f));
  }
  return s;
}

absl::Status
PosixFileSystem::NewAppendableFile(const std::string &fname,
                                   std::unique_ptr<WritableFile> *result) {
  Status s;
  FILE *f = ::fopen(fname.c_str(), "a");
  if (f == nullptr) {
    s = IOError(fname, errno);
  } else {
    result->reset(new PosixWritableFile(fname, f));
  }
  return s;
}

absl::Status PosixFileSystem::NewReadOnlyMemoryRegionFromFile(
    const std::string &fname, std::unique_ptr<ReadOnlyMemoryRegion> *result) {
  int fd = ::open(fname.c_str(), O_RDONLY);
  if (fd < 0) {
    return IOError(fname, errno);
  }
  absl::Cleanup fd_closer = [fd] { ::close(fd); };

  struct stat st;
  if (::fstat(fd, &st) < 0) {
    return IOError(fname, errno);
  }

  if (st.st_size == 0) {
    result->reset(new PosixReadOnlyMemoryRegion(nullptr, 0)); // empty file
    return OkStatus();
  }

  const void *address =
      ::mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE | MAP_POPULATE, fd, 0);
  if (address == MAP_FAILED) {
    return IOError(fname, errno);
  }

  result->reset(new PosixReadOnlyMemoryRegion(address, st.st_size));
  return OkStatus();
}

absl::Status PosixFileSystem::FileExists(const std::string &fname) {
  if (::access(fname.c_str(), F_OK) == 0) {
    return OkStatus();
  }
  return FmtStatus(StatusCode::kNotFound, "{} not found", fname);
}

absl::Status PosixFileSystem::GetChildren(const std::string &dir,
                                          std::vector<std::string> *result) {
  result->clear();
  DIR *d = ::opendir(dir.c_str());
  if (d == nullptr) {
    return IOError(dir, errno);
  }
  struct dirent *entry;
  while ((entry = ::readdir(d)) != nullptr) {
    absl::string_view basename = entry->d_name;
    if ((basename != ".") && (basename != "..")) {
      result->push_back(entry->d_name);
    }
  }
  if (::closedir(d) < 0) {
    return IOError(dir, errno);
  }
  return OkStatus();
}

absl::Status PosixFileSystem::DeleteFile(const std::string &fname) {
  Status result;
  if (::unlink(fname.c_str()) != 0) {
    result = IOError(fname, errno);
  }
  return result;
}

absl::Status PosixFileSystem::CreateDir(const std::string &name) {
  if (name.empty()) {
    return Status(StatusCode::kAlreadyExists, name);
  }
  if (::mkdir(name.c_str(), 0755) != 0) {
    return IOError(name, errno);
  }
  return OkStatus();
}

absl::Status PosixFileSystem::RecursivelyCreateDir(const std::string &name) {
  std::error_code ec;
  std::filesystem::create_directories(name, ec);
  if (ec.value() != 0) {
    return ECM_ERROR(StatusCode::kFailedPrecondition,
                     "creative dir {} failed: {}", name, ec.message());
  }
  return OkStatus();
}

absl::Status PosixFileSystem::DeleteDir(const std::string &name) {
  Status result;
  if (::rmdir(name.c_str()) != 0) {
    result = IOError(name, errno);
  }
  return result;
}

absl::Status PosixFileSystem::DeleteRecursively(const std::string &name) {
  std::error_code ec;
  std::filesystem::remove_all(name, ec);
  if (ec.value() != 0) {
    return ECM_ERROR(StatusCode::kFailedPrecondition,
                     "remove dir {} failed: {}", name, ec.message());
  }
  return OkStatus();
}

absl::Status PosixFileSystem::GetFileSize(const std::string &fname,
                                          uint64_t *size) {
  Status s;
  struct stat sbuf;
  if (::stat(fname.c_str(), &sbuf) != 0) {
    *size = 0;
    s = IOError(fname, errno);
  } else {
    *size = sbuf.st_size;
  }
  return s;
}

absl::Status PosixFileSystem::Stat(const std::string &fname,
                                   FileStatistics *stats) {
  Status s;
  struct stat sbuf;
  if (::stat(fname.c_str(), &sbuf) != 0) {
    s = IOError(fname, errno);
  } else {
    stats->length = sbuf.st_size;
    stats->mtime_nsec = sbuf.st_mtime * 1e9;
    stats->is_directory = S_ISDIR(sbuf.st_mode);
  }
  return s;
}

absl::Status PosixFileSystem::RenameFile(const std::string &src,
                                         const std::string &target) {
  Status result;
  if (::rename(src.c_str(), target.c_str()) != 0) {
    result = IOError(src, errno);
  }
  return result;
}

absl::Status PosixFileSystem::CopyFile(const std::string &src,
                                       const std::string &target) {
  struct stat sbuf;
  if (::stat(src.c_str(), &sbuf) != 0) {
    return IOError(src, errno);
  }
  int src_fd = ::open(src.c_str(), O_RDONLY);
  if (src_fd < 0) {
    return IOError(src, errno);
  }
  // O_WRONLY | O_CREAT | O_TRUNC:
  //   Open file for write and if file does not exist, create the file.
  //   If file exists, truncate its size to 0.
  // When creating file, use the same permissions as original
  mode_t mode = sbuf.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO);
  int target_fd = ::open(target.c_str(), O_WRONLY | O_CREAT | O_TRUNC, mode);
  if (target_fd < 0) {
    ::close(src_fd);
    return IOError(target, errno);
  }
  int rc = 0;
  off_t offset = 0;
  std::unique_ptr<char[]> buffer(new char[kPosixCopyFileBufferSize]);
  while (offset < sbuf.st_size) {
    // Use uint64 for safe compare SSIZE_MAX
    uint64_t chunk = sbuf.st_size - offset;
    if (chunk > SSIZE_MAX) {
      chunk = SSIZE_MAX;
    }
#if defined(__linux__) && !defined(__ANDROID__)
    rc = ::sendfile(target_fd, src_fd, &offset, static_cast<size_t>(chunk));
#else
    if (chunk > kPosixCopyFileBufferSize) {
      chunk = kPosixCopyFileBufferSize;
    }
    rc = ::read(src_fd, buffer.get(), static_cast<size_t>(chunk));
    if (rc <= 0) {
      break;
    }
    rc = ::write(target_fd, buffer.get(), static_cast<size_t>(chunk));
    offset += chunk;
#endif
    if (rc <= 0) {
      break;
    }
  }

  Status result = OkStatus();
  if (rc < 0) {
    result = IOError(target, errno);
  }

  // Keep the error code
  rc = ::close(target_fd);
  if (rc < 0 && result == OkStatus()) {
    result = IOError(target, errno);
  }
  rc = ::close(src_fd);
  if (rc < 0 && result == OkStatus()) {
    result = IOError(target, errno);
  }

  return result;
}

Status PosixFileSystem::Touch(const std::string &fname) {
  auto rc = ::utimensat(AT_FDCWD, fname.c_str(), 0, 0);
  if (rc < 0) {
    return IOError(fname, errno);
  }
  return OkStatus();
}

absl::Status PosixFileSystem::NewLockFile(const std::string &fname,
                                          std::unique_ptr<LockFile> *result) {
  int fd = ::open(fname.c_str(), O_CLOEXEC | O_RDWR | O_CREAT);
  if (fd < 0) {
    return IOError(fname, errno);
  }

  // 非阻塞尝试获取排它锁
  struct flock fl {};
  fl.l_type = F_WRLCK;
  fl.l_whence = SEEK_SET;
  fl.l_start = 0;
  fl.l_len = 0;

  if (::fcntl(fd, F_SETLK, &fl) < 0) {
    ::close(fd);
    return IOError(fname, errno);
  }

  result->reset(new PosixLockFile(fname, fd));
  return OkStatus();
}

Status ReadFileToString(FileSystem *fs, const std::string &fname,
                        std::string *data) {
  uint64_t file_size;
  Status s = fs->GetFileSize(fname, &file_size);
  if (!s.ok()) {
    return s;
  }
  std::unique_ptr<RandomAccessFile> file;
  s = fs->NewRandomAccessFile(fname, &file);
  if (!s.ok()) {
    return s;
  }
  data->resize(file_size);
  char *p = &*data->begin();
  absl::string_view result;
  s = file->Read(0, file_size, &result, p);
  if (!s.ok()) {
    data->clear();
  } else if (result.size() != file_size) {
    s = FmtStatus(StatusCode::kAborted,
                  "File {} changed while reading: {} vs {}", fname, file_size,
                  result.size());
    data->clear();
  } else if (result.data() == p) {
    // Data is already in the correct location
  } else {
    memmove(p, result.data(), result.size());
  }
  return s;
}

Status WriteStringToFile(FileSystem *fs, const std::string &fname,
                         const absl::string_view &data) {
  std::unique_ptr<WritableFile> file;
  Status s = fs->NewWritableFile(fname, &file);
  if (!s.ok()) {
    return s;
  }
  s = file->Append(data);
  if (s.ok()) {
    s = file->Close();
  }
  return s;
}

Status WriteBinaryProto(FileSystem *fs, const std::string &fname,
                        const google::protobuf::MessageLite &proto) {
  std::string serialized;
  proto.AppendToString(&serialized);
  return WriteStringToFile(fs, fname, serialized);
}

Status ReadBinaryProto(FileSystem *fs, const std::string &fname,
                       google::protobuf::MessageLite *proto) {
  std::unique_ptr<ReadOnlyMemoryRegion> mem;
  ECM_RETURN_IF_ERROR(fs->NewReadOnlyMemoryRegionFromFile(fname, &mem));
  std::unique_ptr<ZeroCopyFileStream> stream(new ZeroCopyFileStream(mem.get()));
  google::protobuf::io::CodedInputStream coded_stream(stream.get());

  if (!proto->ParseFromCodedStream(&coded_stream) ||
      !coded_stream.ConsumedEntireMessage()) {
    ECM_RETURN_IF_ERROR(stream->status());
    return FmtStatus(StatusCode::kDataLoss, "Can't parse {} as binary proto",
                     fname);
  }
  return OkStatus();
}

Status WriteTextProto(FileSystem *fs, const std::string &fname,
                      const google::protobuf::Message &proto) {
  std::string serialized;
  if (!google::protobuf::TextFormat::PrintToString(proto, &serialized)) {
    return Status(StatusCode::kFailedPrecondition,
                  "Unable to convert proto to text.");
  }
  return WriteStringToFile(fs, fname, serialized);
}

Status ReadTextProto(FileSystem *fs, const std::string &fname,
                     google::protobuf::Message *proto) {
  std::unique_ptr<ReadOnlyMemoryRegion> mem;
  ECM_RETURN_IF_ERROR(fs->NewReadOnlyMemoryRegionFromFile(fname, &mem));
  std::unique_ptr<ZeroCopyFileStream> stream(new ZeroCopyFileStream(mem.get()));

  if (!google::protobuf::TextFormat::Parse(stream.get(), proto)) {
    ECM_RETURN_IF_ERROR(stream->status());
    return FmtStatus(StatusCode::kDataLoss, "Can't parse {} as text proto",
                     fname);
  }
  return OkStatus();
}

Status ReadJsonToProto(FileSystem *fs, const std::string &fname,
                       google::protobuf::Message *proto) {
  std::string json_data;
  ECM_RETURN_IF_ERROR(ReadFileToString(fs, fname, &json_data));

  std::string error;
  if (!json2pb::JsonToProtoMessage(json_data, proto, &error)) {
    return ECM_ERROR(StatusCode::kInternal, "parse {} json `{}` failed: {}",
                     fname, json_data, error);
  }

  return OkStatus();
}

Status WriteProtoToJson(FileSystem *fs, const std::string &fname,
                        const google::protobuf::Message *proto) {

  std::string json_data, error;
  if (!json2pb::ProtoMessageToJson(*proto, &json_data, &error)) {
    return ECM_ERROR(StatusCode::kInternal, "serialize pb to json failed: {}",
                     error);
  }

  return WriteStringToFile(fs, fname, json_data);
}

Status ReadTextOrBinaryProto(FileSystem *fs, const std::string &fname,
                             google::protobuf::Message *proto) {
  if (ReadTextProto(fs, fname, proto).ok()) {
    return OkStatus();
  }
  return ReadBinaryProto(fs, fname, proto);
}

Status CopyFile(FileSystem *src_fs, const std::string &src_path,
                FileSystem *target_fs, const std::string &target_path) {
  FileStatistics stat;
  ECM_RETURN_IF_ERROR(src_fs->Stat(src_path, &stat));

  std::unique_ptr<RandomAccessFile> src_file;
  ECM_RETURN_IF_ERROR(src_fs->NewRandomAccessFile(src_path, &src_file));

  std::unique_ptr<WritableFile> target_file;
  ECM_RETURN_IF_ERROR(target_fs->NewWritableFile(target_path, &target_file));

  // 32MB
  size_t buf_size = 32 << 20;
  std::string buf(buf_size, 0);

  for (int64_t off = 0; off < stat.length; off += buf_size) {
    auto n = std::min(buf_size, (size_t)(stat.length - off));
    absl::string_view read_res;
    ECM_RETURN_IF_ERROR(src_file->Read(off, n, &read_res, buf.data()));
    ECM_RETURN_IF_ERROR(target_file->Append(read_res));
  }

  return OkStatus();
}

Status CopyR(FileSystem *src_fs, const std::string &src_path,
             FileSystem *target_fs, const std::string &target_path) {
  FileStatistics stat;
  ECM_RETURN_IF_ERROR(src_fs->Stat(src_path, &stat));
  if (stat.is_directory) {
    auto s = target_fs->CreateDir(target_path);
    if (!s.ok() && s.code() != StatusCode::kAlreadyExists) {
      return s;
    }
    std::vector<std::string> children;
    ECM_RETURN_IF_ERROR(src_fs->GetChildren(src_path, &children));
    for (auto &child : children) {
      ECM_RETURN_IF_ERROR(CopyR(src_fs, format("{}/{}", src_path, child),
                                target_fs,
                                format("{}/{}", target_path, child)));
    }
  } else {
    ECM_RETURN_IF_ERROR(CopyFile(src_fs, src_path, target_fs, target_path));
  }

  return OkStatus();
}

} // namespace ecm
