#pragma once

#include "core/file_system.h"

namespace ecm {

// An InputBuffer provides a buffer on top of a RandomAccessFile.
// A given instance of an InputBuffer is NOT safe for concurrent use
// by multiple threads
class InputBuffer {
public:
  // Create an InputBuffer for "file" with a buffer size of
  // "buffer_bytes" bytes.  'file' must outlive *this.
  InputBuffer(RandomAccessFile *file, size_t buffer_bytes);
  ~InputBuffer();

  // Read one text line of data into "*result" until end-of-file or a
  // \n is read.  (The \n is not included in the result.)  Overwrites
  // any existing data in *result.
  //
  // If successful, returns OK.  If we are already at the end of the
  // file, we return an OUT_OF_RANGE error.  Otherwise, we return
  // some other non-OK status.
  Status ReadLine(std::string *result);

  // Reads bytes_to_read bytes into *result, overwriting *result.
  //
  // If successful, returns OK.  If we there are not enough bytes to
  // read before the end of the file, we return an OUT_OF_RANGE error.
  // Otherwise, we return some other non-OK status.
  Status ReadNBytes(int64_t bytes_to_read, std::string *result);

  // An overload that writes to char*.  Caller must ensure result[0,
  // bytes_to_read) is valid to be overwritten.  Returns OK iff "*bytes_read ==
  // bytes_to_read".
  Status ReadNBytes(int64_t bytes_to_read, char *result, size_t *bytes_read);

  // Like ReadNBytes() without returning the bytes read.
  Status SkipNBytes(int64_t bytes_to_skip);

  // Seek to this offset within the file.
  //
  // If we seek to somewhere within our pre-buffered data, we will re-use what
  // data we can.  Otherwise, Seek() throws out the current buffer and the next
  // read will trigger a File::Read().
  Status Seek(int64_t position);

  // Provides a hint about future reads, which may improve their performance.
  Status Hint(int64_t bytes_to_read);

  // Returns the position in the file.
  int64_t Tell() const { return file_pos_ - (limit_ - pos_); }

  // Returns the underlying RandomAccessFile.
  RandomAccessFile *file() const { return file_; }

private:
  Status FillBuffer();

  RandomAccessFile *file_; // Not owned
  int64_t file_pos_;       // Next position to read from in "file_"
  size_t size_;            // Size of "buf_"
  char *buf_;              // The buffer itself
  // [pos_,limit_) hold the "limit_ - pos_" bytes just before "file_pos_"
  char *pos_;   // Current position in "buf"
  char *limit_; // Just past end of valid data in "buf"

  InputBuffer(const InputBuffer &) = delete;
  void operator=(const InputBuffer &) = delete;
};

} // namespace ecm