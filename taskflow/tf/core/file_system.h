#pragma once

#include <type_traits>

#include "core/status.h"
#include "google/protobuf/message.h"

namespace ecm {

struct FileStatistics;
class RandomAccessFile;
class WritableFile;
class ReadOnlyMemoryRegion;
class LockFile;

class FileSystem {
public:
  static FileSystem *Default();

  /// \brief Creates a brand new random access read-only file with the
  /// specified name.
  ///
  /// On success, stores a pointer to the new file in
  /// *result and returns OK.  On failure stores NULL in *result and
  /// returns non-OK.  If the file does not exist, returns a non-OK
  /// status.
  ///
  /// The returned file may be concurrently accessed by multiple threads.
  ///
  /// The ownership of the returned RandomAccessFile is passed to the caller
  /// and the object should be deleted when is not used.
  virtual Status
  NewRandomAccessFile(const std::string &fname,
                      std::unique_ptr<RandomAccessFile> *result) = 0;

  /// \brief Creates an object that writes to a new file with the specified
  /// name.
  ///
  /// Deletes any existing file with the same name and creates a
  /// new file.  On success, stores a pointer to the new file in
  /// *result and returns OK.  On failure stores NULL in *result and
  /// returns non-OK.
  ///
  /// The returned file will only be accessed by one thread at a time.
  ///
  /// The ownership of the returned WritableFile is passed to the caller
  /// and the object should be deleted when is not used.
  virtual Status NewWritableFile(const std::string &fname,
                                 std::unique_ptr<WritableFile> *result) = 0;

  /// \brief Creates an object that either appends to an existing file, or
  /// writes to a new file (if the file does not exist to begin with).
  ///
  /// On success, stores a pointer to the new file in *result and
  /// returns OK.  On failure stores NULL in *result and returns
  /// non-OK.
  ///
  /// The returned file will only be accessed by one thread at a time.
  ///
  /// The ownership of the returned WritableFile is passed to the caller
  /// and the object should be deleted when is not used.
  virtual Status NewAppendableFile(const std::string &fname,
                                   std::unique_ptr<WritableFile> *result) = 0;

  /// \brief Creates a readonly region of memory with the file context.
  ///
  /// On success, it returns a pointer to read-only memory region
  /// from the content of file fname. The ownership of the region is passed to
  /// the caller. On failure stores nullptr in *result and returns non-OK.
  ///
  /// The returned memory region can be accessed from many threads in parallel.
  ///
  /// The ownership of the returned ReadOnlyMemoryRegion is passed to the caller
  /// and the object should be deleted when is not used.
  virtual Status NewReadOnlyMemoryRegionFromFile(
      const std::string &fname,
      std::unique_ptr<ReadOnlyMemoryRegion> *result) = 0;

  /// Returns OK if the named path exists and NOT_FOUND otherwise.
  virtual Status FileExists(const std::string &fname) = 0;

  /// \brief Returns the immediate children in the given directory.
  ///
  /// The returned paths are relative to 'dir'.
  virtual Status GetChildren(const std::string &dir,
                             std::vector<std::string> *result) = 0;

  /// \brief Obtains statistics for the given path.
  virtual Status Stat(const std::string &fname, FileStatistics *stat) = 0;

  /// \brief Deletes the named file.
  virtual Status DeleteFile(const std::string &fname) = 0;

  /// \brief Creates the specified directory.
  /// Typical return codes:
  ///  * OK - successfully created the directory.
  ///  * ALREADY_EXISTS - directory with name dirname already exists.
  ///  * PERMISSION_DENIED - dirname is not writable.
  virtual Status CreateDir(const std::string &dirname) = 0;

  /// \brief Creates the specified directory and all the necessary
  /// subdirectories.
  /// Typical return codes:
  ///  * OK - successfully created the directory and sub directories, even if
  ///         they were already created.
  ///  * PERMISSION_DENIED - dirname or some subdirectory is not writable.
  virtual Status RecursivelyCreateDir(const std::string &dirname);

  /// \brief Deletes the specified directory.
  virtual Status DeleteDir(const std::string &dirname) = 0;

  /// \brief Deletes the specified directory and all subdirectories and files
  /// underneath it. This is accomplished by traversing the directory tree
  /// rooted at dirname and deleting entries as they are encountered.
  ///
  /// If dirname itself is not readable or does not exist, *undeleted_dir_count
  /// is set to 1, *undeleted_file_count is set to 0 and an appropriate status
  /// (e.g. NOT_FOUND) is returned.
  ///
  /// If dirname and all its descendants were successfully deleted, TF_OK is
  /// returned and both error counters are set to zero.
  ///
  /// Otherwise, while traversing the tree, undeleted_file_count and
  /// undeleted_dir_count are updated if an entry of the corresponding type
  /// could not be deleted. The returned error status represents the reason that
  /// any one of these entries could not be deleted.
  ///
  /// REQUIRES: undeleted_files, undeleted_dirs to be not null.
  ///
  /// Typical return codes:
  ///  * OK - dirname exists and we were able to delete everything underneath.
  ///  * NOT_FOUND - dirname doesn't exist
  ///  * PERMISSION_DENIED - dirname or some descendant is not writable
  ///  * UNIMPLEMENTED - Some underlying functions (like Delete) are not
  ///                    implemented
  virtual Status DeleteRecursively(const std::string &dirname);

  /// \brief Stores the size of `fname` in `*file_size`.
  virtual Status GetFileSize(const std::string &fname, uint64_t *file_size) = 0;

  /// \brief Overwrites the target if it exists.
  virtual Status RenameFile(const std::string &src,
                            const std::string &target) = 0;

  /// \brief Copy the src to target.
  virtual Status CopyFile(const std::string &src,
                          const std::string &target) = 0;

  /// \brief Returns whether the given path is a directory or not.
  ///
  /// Typical return codes (not guaranteed exhaustive):
  ///  * OK - The path exists and is a directory.
  ///  * FAILED_PRECONDITION - The path exists and is not a directory.
  ///  * NOT_FOUND - The path entry does not exist.
  ///  * PERMISSION_DENIED - Insufficient permissions.
  ///  * UNIMPLEMENTED - The file factory doesn't support directories.
  virtual Status IsDirectory(const std::string &fname);

  virtual Status Touch(const std::string &fname);

  virtual Status NewLockFile(const std::string &fname,
                             std::unique_ptr<LockFile> *result);

  FileSystem() {}

  virtual ~FileSystem() = default;
};

/// A file abstraction for randomly reading the contents of a file.
class RandomAccessFile {
public:
  RandomAccessFile() {}
  virtual ~RandomAccessFile() = default;

  /// \brief Returns the name of the file.
  ///
  /// This is an optional operation that may not be implemented by every
  /// filesystem.
  virtual Status Name(absl::string_view *result) const {
    return ECM_ERROR(StatusCode::kUnimplemented,
                     "This filesystem does not support Name()");
  }

  /// \brief Reads up to `n` bytes from the file starting at `offset`.
  ///
  /// `scratch[0..n-1]` may be written by this routine.  Sets `*result`
  /// to the data that was read (including if fewer than `n` bytes were
  /// successfully read).  May set `*result` to point at data in
  /// `scratch[0..n-1]`, so `scratch[0..n-1]` must be live when
  /// `*result` is used.
  ///
  /// On OK returned status: `n` bytes have been stored in `*result`.
  /// On non-OK returned status: `[0..n]` bytes have been stored in `*result`.
  ///
  /// Returns `OUT_OF_RANGE` if fewer than n bytes were stored in `*result`
  /// because of EOF.
  ///
  /// Safe for concurrent use by multiple threads.
  virtual Status Read(uint64_t offset, size_t n, absl::string_view *result,
                      char *scratch) const = 0;

  /// \brief Read up to `n` bytes from the file starting at `offset`.
  virtual Status Read(uint64_t offset, size_t n, absl::Cord *cord) const {
    return ECM_ERROR(StatusCode::kUnimplemented,
                     "Read(uint64, size_t, absl::Cord*) is not implemented");
  }

private:
  RandomAccessFile(const RandomAccessFile &) = delete;
  void operator=(const RandomAccessFile &) = delete;
};

/// \brief A file abstraction for sequential writing.
///
/// The implementation must provide buffering since callers may append
/// small fragments at a time to the file.
class WritableFile {
public:
  WritableFile() {}
  virtual ~WritableFile() = default;

  /// \brief Append 'data' to the file.
  virtual Status Append(absl::string_view data) = 0;

  // \brief Append 'data' to the file.
  virtual Status Append(const absl::Cord &cord) {
    for (absl::string_view chunk : cord.Chunks()) {
      ECM_RETURN_IF_ERROR(Append(chunk));
    }
    return OkStatus();
  }

  // \brief Append POD as binary bytes to the file.
  template <class T>
  auto Append(const T &v) ->
      typename std::enable_if<std::is_pod<T>::value, Status>::type {
    return Append(
        absl::string_view(reinterpret_cast<const char *>(&v), sizeof(T)));
  }

  /// \brief Close the file.
  ///
  /// Flush() and de-allocate resources associated with this file
  ///
  /// Typical return codes (not guaranteed to be exhaustive):
  ///  * OK
  ///  * Other codes, as returned from Flush()
  virtual Status Close() = 0;

  /// \brief Flushes the file and optionally syncs contents to filesystem.
  ///
  /// This should flush any local buffers whose contents have not been
  /// delivered to the filesystem.
  ///
  /// If the process terminates after a successful flush, the contents
  /// may still be persisted, since the underlying filesystem may
  /// eventually flush the contents.  If the OS or machine crashes
  /// after a successful flush, the contents may or may not be
  /// persisted, depending on the implementation.
  virtual Status Flush() = 0;

  // \brief Returns the name of the file.
  ///
  /// This is an optional operation that may not be implemented by every
  /// filesystem.
  virtual Status Name(absl::string_view *result) const {
    return ECM_ERROR(StatusCode::kUnimplemented,
                     "This filesystem does not support Name()");
  }

  /// \brief Syncs contents of file to filesystem.
  ///
  /// This waits for confirmation from the filesystem that the contents
  /// of the file have been persisted to the filesystem; if the OS
  /// or machine crashes after a successful Sync, the contents should
  /// be properly saved.
  virtual Status Sync() = 0;

  /// \brief Retrieves the current write position in the file, or -1 on
  /// error.
  ///
  /// This is an optional operation, subclasses may choose to return
  /// errors::Unimplemented.
  virtual Status Tell(int64_t *position) {
    *position = -1;
    return ECM_ERROR(StatusCode::kUnimplemented,
                     "This filesystem does not support Tell()");
  }

  virtual Status Seek(int64_t position) {
    return ECM_ERROR(StatusCode::kUnimplemented,
                     "This filesystem does not support Seek()");
  }

private:
  WritableFile(const WritableFile &) = delete;
  void operator=(const WritableFile &) = delete;
};

/// \brief A readonly memmapped file abstraction.
///
/// The implementation must guarantee that all memory is accessible when the
/// object exists, independently from the Env that created it.
class ReadOnlyMemoryRegion {
public:
  ReadOnlyMemoryRegion() = default;
  ReadOnlyMemoryRegion(const void *address, uint64_t length)
      : address_(address), length_(length) {}
  virtual ~ReadOnlyMemoryRegion() = default;

  /// \brief Returns a pointer to the memory region.
  const void *data() const { return address_; }

  /// \brief Returns the length of the memory region in bytes.
  uint64_t length() const { return length_; }

  // bytes offset
  template <class T = void> const T *offset(size_t offset) const {
    return reinterpret_cast<const T *>(cast_data<char>() + offset);
  }

  bool aligned_at(uint64_t offset, uint64_t alignment) const {
    return ((reinterpret_cast<uintptr_t>(data()) + offset) & (alignment - 1)) ==
           0;
  }

  // typed data accessor
  template <class T> const T *cast_data() const {
    return reinterpret_cast<const T *>(data());
  }
  template <class T> uint64_t typed_size() const {
    return length() / sizeof(T);
  }

  // vector like accessor
  template <class T> bool check(uint64_t i) const {
    return i < typed_size<T>();
  }
  template <class T> T get(uint64_t i) const { return cast_data<T>()[i]; }

  // raw accessor at bytes offset
  template <class T> bool check_at(uint64_t offset) const {
    return offset + sizeof(T) <= length();
  }
  template <class T> T get_at(uint64_t offset) const {
    return *reinterpret_cast<const T *>(cast_data<uint8_t>() + offset);
  }

  // span accessor
  template <class T> absl::Span<const T> as_span() const {
    return absl::MakeConstSpan(cast_data<T>(), typed_size<T>());
  }
  // offset: bytes offset
  // len: number of elements
  template <class T> bool check_span_at(uint64_t offset, uint64_t len) const {
    return offset + len * sizeof(T) <= length();
  }
  template <class T>
  absl::Span<const T> as_span_at(uint64_t offset, uint64_t len) const {
    return absl::MakeConstSpan(
        reinterpret_cast<const T *>(cast_data<uint8_t>() + offset), len);
  }

protected:
  const void *const address_ = nullptr;
  const uint64_t length_ = 0;
};

class LockFile {
public:
  LockFile() {}
  virtual ~LockFile() = default;
};

struct FileStatistics {
  // The length of the file or -1 if finding file length is not supported.
  int64_t length = -1;
  // The last modified time in nanoseconds.
  int64_t mtime_nsec = 0;
  // True if the file is a directory, otherwise false.
  bool is_directory = false;

  FileStatistics() {}
  FileStatistics(int64_t length, int64_t mtime_nsec, bool is_directory)
      : length(length), mtime_nsec(mtime_nsec), is_directory(is_directory) {}
  ~FileStatistics() {}
};

class ZeroCopyFileStream : public google::protobuf::io::ZeroCopyInputStream {
public:
  explicit ZeroCopyFileStream(ReadOnlyMemoryRegion *mem) : mem_(mem), pos_(0) {}
  ZeroCopyFileStream(ReadOnlyMemoryRegion *mem, int64_t pos)
      : mem_(mem), pos_(pos) {}

  void BackUp(int count) override { pos_ -= count; }
  bool Skip(int count) override {
    pos_ += count;
    return true;
  }
  int64_t ByteCount() const override { return pos_; }
  Status status() const { return status_; }

  bool Next(const void **data, int *size) override {
    if ((size_t)pos_ >= mem_->length()) {
      status_ = Status(StatusCode::kOutOfRange, "out of range");
      return false;
    }
    *data = (char *)mem_->data() + pos_;
    *size = mem_->length() - pos_;
    pos_ += *size;
    return true;
  }

private:
  ReadOnlyMemoryRegion *mem_;
  int64_t pos_;
  Status status_;
};

/// A utility routine: reads contents of named file into `*data`
Status ReadFileToString(FileSystem *fs, const std::string &fname,
                        std::string *data);

/// A utility routine: write contents of `data` to file named `fname`
/// (overwriting existing contents, if any).
Status WriteStringToFile(FileSystem *fs, const std::string &fname,
                         const absl::string_view &data);

/// Write binary representation of "proto" to the named file.
Status WriteBinaryProto(FileSystem *fs, const std::string &fname,
                        const google::protobuf::MessageLite &proto);

/// Reads contents of named file and parse as binary encoded proto data
/// and store into `*proto`.
Status ReadBinaryProto(FileSystem *fs, const std::string &fname,
                       google::protobuf::MessageLite *proto);

Status WriteTextProto(FileSystem *fs, const std::string &fname,
                      const google::protobuf::Message &proto);

/// Read contents of named file and parse as text encoded proto data
Status ReadTextProto(FileSystem *fs, const std::string &fname,
                     google::protobuf::Message *proto);

Status ReadJsonToProto(FileSystem *fs, const std::string &fname,
                       google::protobuf::Message *proto);
Status WriteProtoToJson(FileSystem *fs, const std::string &fname,
                        const google::protobuf::Message *proto);

/// Read contents of named file and parse as either text or binary encoded proto
/// data and store into `*proto`.
Status ReadTextOrBinaryProto(FileSystem *fs, const std::string &fname,
                             google::protobuf::Message *proto);

// cross fs copy file
Status CopyFile(FileSystem *src_fs, const std::string &src_path,
                FileSystem *target_fs, const std::string &target_path);

// copy path recursively
Status CopyR(FileSystem *src_fs, const std::string &src_path,
             FileSystem *target_fs, const std::string &target_path);

} // namespace ecm
