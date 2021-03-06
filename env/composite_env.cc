// Copyright (c) 2019-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).
//
#include "env/composite_env_wrapper.h"

namespace ROCKSDB_NAMESPACE {
namespace {
// The CompositeEnvWrapper class provides an interface that is compatible
// with the old monolithic Env API, and an implementation that wraps around
// the new Env that provides threading and other OS related functionality, and
// the new FileSystem API that provides storage functionality. By
// providing the old Env interface, it allows the rest of RocksDB code to
// be agnostic of whether the underlying Env implementation is a monolithic
// Env or an Env + FileSystem. In the former case, the user will specify
// Options::env only, whereas in the latter case, the user will specify
// Options::env and Options::file_system.

class CompositeSequentialFileWrapper : public SequentialFile {
 public:
  explicit CompositeSequentialFileWrapper(
      std::unique_ptr<FSSequentialFile>& target)
      : target_(std::move(target)) {}

  Status Read(size_t n, Slice* result, char* scratch) override {
    IOOptions io_opts;
    IODebugContext dbg;
    return target_->Read(n, io_opts, result, scratch, &dbg);
  }
  Status Skip(uint64_t n) override { return target_->Skip(n); }
  bool use_direct_io() const override { return target_->use_direct_io(); }
  size_t GetRequiredBufferAlignment() const override {
    return target_->GetRequiredBufferAlignment();
  }
  Status InvalidateCache(size_t offset, size_t length) override {
    return target_->InvalidateCache(offset, length);
  }
  Status PositionedRead(uint64_t offset, size_t n, Slice* result,
                        char* scratch) override {
    IOOptions io_opts;
    IODebugContext dbg;
    return target_->PositionedRead(offset, n, io_opts, result, scratch, &dbg);
  }

 private:
  std::unique_ptr<FSSequentialFile> target_;
};

class CompositeRandomAccessFileWrapper : public RandomAccessFile {
 public:
  explicit CompositeRandomAccessFileWrapper(
      std::unique_ptr<FSRandomAccessFile>& target)
      : target_(std::move(target)) {}

  Status Read(uint64_t offset, size_t n, Slice* result,
              char* scratch) const override {
    IOOptions io_opts;
    IODebugContext dbg;
    return target_->Read(offset, n, io_opts, result, scratch, &dbg);
  }
  Status MultiRead(ReadRequest* reqs, size_t num_reqs) override {
    IOOptions io_opts;
    IODebugContext dbg;
    std::vector<FSReadRequest> fs_reqs;
    Status status;

    fs_reqs.resize(num_reqs);
    for (size_t i = 0; i < num_reqs; ++i) {
      fs_reqs[i].offset = reqs[i].offset;
      fs_reqs[i].len = reqs[i].len;
      fs_reqs[i].scratch = reqs[i].scratch;
      fs_reqs[i].status = IOStatus::OK();
    }
    status = target_->MultiRead(fs_reqs.data(), num_reqs, io_opts, &dbg);
    for (size_t i = 0; i < num_reqs; ++i) {
      reqs[i].result = fs_reqs[i].result;
      reqs[i].status = fs_reqs[i].status;
    }
    return status;
  }
  Status Prefetch(uint64_t offset, size_t n) override {
    IOOptions io_opts;
    IODebugContext dbg;
    return target_->Prefetch(offset, n, io_opts, &dbg);
  }
  size_t GetUniqueId(char* id, size_t max_size) const override {
    return target_->GetUniqueId(id, max_size);
  }
  void Hint(AccessPattern pattern) override {
    target_->Hint((FSRandomAccessFile::AccessPattern)pattern);
  }
  bool use_direct_io() const override { return target_->use_direct_io(); }
  size_t GetRequiredBufferAlignment() const override {
    return target_->GetRequiredBufferAlignment();
  }
  Status InvalidateCache(size_t offset, size_t length) override {
    return target_->InvalidateCache(offset, length);
  }

 private:
  std::unique_ptr<FSRandomAccessFile> target_;
};

class CompositeWritableFileWrapper : public WritableFile {
 public:
  explicit CompositeWritableFileWrapper(std::unique_ptr<FSWritableFile>& t)
      : target_(std::move(t)) {}

  Status Append(const Slice& data) override {
    IOOptions io_opts;
    IODebugContext dbg;
    return target_->Append(data, io_opts, &dbg);
  }
  Status Append(const Slice& data,
                const DataVerificationInfo& verification_info) override {
    IOOptions io_opts;
    IODebugContext dbg;
    return target_->Append(data, io_opts, verification_info, &dbg);
  }
  Status PositionedAppend(const Slice& data, uint64_t offset) override {
    IOOptions io_opts;
    IODebugContext dbg;
    return target_->PositionedAppend(data, offset, io_opts, &dbg);
  }
  Status PositionedAppend(
      const Slice& data, uint64_t offset,
      const DataVerificationInfo& verification_info) override {
    IOOptions io_opts;
    IODebugContext dbg;
    return target_->PositionedAppend(data, offset, io_opts, verification_info,
                                     &dbg);
  }
  Status Truncate(uint64_t size) override {
    IOOptions io_opts;
    IODebugContext dbg;
    return target_->Truncate(size, io_opts, &dbg);
  }
  Status Close() override {
    IOOptions io_opts;
    IODebugContext dbg;
    return target_->Close(io_opts, &dbg);
  }
  Status Flush() override {
    IOOptions io_opts;
    IODebugContext dbg;
    return target_->Flush(io_opts, &dbg);
  }
  Status Sync() override {
    IOOptions io_opts;
    IODebugContext dbg;
    return target_->Sync(io_opts, &dbg);
  }
  Status Fsync() override {
    IOOptions io_opts;
    IODebugContext dbg;
    return target_->Fsync(io_opts, &dbg);
  }
  bool IsSyncThreadSafe() const override { return target_->IsSyncThreadSafe(); }

  bool use_direct_io() const override { return target_->use_direct_io(); }

  size_t GetRequiredBufferAlignment() const override {
    return target_->GetRequiredBufferAlignment();
  }

  void SetWriteLifeTimeHint(Env::WriteLifeTimeHint hint) override {
    target_->SetWriteLifeTimeHint(hint);
  }

  Env::WriteLifeTimeHint GetWriteLifeTimeHint() override {
    return target_->GetWriteLifeTimeHint();
  }

  uint64_t GetFileSize() override {
    IOOptions io_opts;
    IODebugContext dbg;
    return target_->GetFileSize(io_opts, &dbg);
  }

  void SetPreallocationBlockSize(size_t size) override {
    target_->SetPreallocationBlockSize(size);
  }

  void GetPreallocationStatus(size_t* block_size,
                              size_t* last_allocated_block) override {
    target_->GetPreallocationStatus(block_size, last_allocated_block);
  }

  size_t GetUniqueId(char* id, size_t max_size) const override {
    return target_->GetUniqueId(id, max_size);
  }

  Status InvalidateCache(size_t offset, size_t length) override {
    return target_->InvalidateCache(offset, length);
  }

  Status RangeSync(uint64_t offset, uint64_t nbytes) override {
    IOOptions io_opts;
    IODebugContext dbg;
    return target_->RangeSync(offset, nbytes, io_opts, &dbg);
  }

  void PrepareWrite(size_t offset, size_t len) override {
    IOOptions io_opts;
    IODebugContext dbg;
    target_->PrepareWrite(offset, len, io_opts, &dbg);
  }

  Status Allocate(uint64_t offset, uint64_t len) override {
    IOOptions io_opts;
    IODebugContext dbg;
    return target_->Allocate(offset, len, io_opts, &dbg);
  }

  std::unique_ptr<FSWritableFile>* target() { return &target_; }

 private:
  std::unique_ptr<FSWritableFile> target_;
};

class CompositeRandomRWFileWrapper : public RandomRWFile {
 public:
  explicit CompositeRandomRWFileWrapper(std::unique_ptr<FSRandomRWFile>& target)
      : target_(std::move(target)) {}

  bool use_direct_io() const override { return target_->use_direct_io(); }
  size_t GetRequiredBufferAlignment() const override {
    return target_->GetRequiredBufferAlignment();
  }
  Status Write(uint64_t offset, const Slice& data) override {
    IOOptions io_opts;
    IODebugContext dbg;
    return target_->Write(offset, data, io_opts, &dbg);
  }
  Status Read(uint64_t offset, size_t n, Slice* result,
              char* scratch) const override {
    IOOptions io_opts;
    IODebugContext dbg;
    return target_->Read(offset, n, io_opts, result, scratch, &dbg);
  }
  Status Flush() override {
    IOOptions io_opts;
    IODebugContext dbg;
    return target_->Flush(io_opts, &dbg);
  }
  Status Sync() override {
    IOOptions io_opts;
    IODebugContext dbg;
    return target_->Sync(io_opts, &dbg);
  }
  Status Fsync() override {
    IOOptions io_opts;
    IODebugContext dbg;
    return target_->Fsync(io_opts, &dbg);
  }
  Status Close() override {
    IOOptions io_opts;
    IODebugContext dbg;
    return target_->Close(io_opts, &dbg);
  }

 private:
  std::unique_ptr<FSRandomRWFile> target_;
};

class CompositeDirectoryWrapper : public Directory {
 public:
  explicit CompositeDirectoryWrapper(std::unique_ptr<FSDirectory>& target)
      : target_(std::move(target)) {}

  Status Fsync() override {
    IOOptions io_opts;
    IODebugContext dbg;
    return target_->FsyncWithDirOptions(io_opts, &dbg, DirFsyncOptions());
  }
  size_t GetUniqueId(char* id, size_t max_size) const override {
    return target_->GetUniqueId(id, max_size);
  }

 private:
  std::unique_ptr<FSDirectory> target_;
};
}  // namespace

Status CompositeEnv::NewSequentialFile(const std::string& f,
                                       std::unique_ptr<SequentialFile>* r,
                                       const EnvOptions& options) {
  IODebugContext dbg;
  std::unique_ptr<FSSequentialFile> file;
  Status status;
  status =
      file_system_->NewSequentialFile(f, FileOptions(options), &file, &dbg);
  if (status.ok()) {
    r->reset(new CompositeSequentialFileWrapper(file));
  }
  return status;
}

Status CompositeEnv::NewRandomAccessFile(const std::string& f,
                                         std::unique_ptr<RandomAccessFile>* r,
                                         const EnvOptions& options) {
  IODebugContext dbg;
  std::unique_ptr<FSRandomAccessFile> file;
  Status status;
  status =
      file_system_->NewRandomAccessFile(f, FileOptions(options), &file, &dbg);
  if (status.ok()) {
    r->reset(new CompositeRandomAccessFileWrapper(file));
  }
  return status;
}

Status CompositeEnv::NewWritableFile(const std::string& f,
                                     std::unique_ptr<WritableFile>* r,
                                     const EnvOptions& options) {
  IODebugContext dbg;
  std::unique_ptr<FSWritableFile> file;
  Status status;
  status = file_system_->NewWritableFile(f, FileOptions(options), &file, &dbg);
  if (status.ok()) {
    r->reset(new CompositeWritableFileWrapper(file));
  }
  return status;
}

Status CompositeEnv::ReopenWritableFile(const std::string& fname,
                                        std::unique_ptr<WritableFile>* result,
                                        const EnvOptions& options) {
  IODebugContext dbg;
  Status status;
  std::unique_ptr<FSWritableFile> file;
  status = file_system_->ReopenWritableFile(fname, FileOptions(options), &file,
                                            &dbg);
  if (status.ok()) {
    result->reset(new CompositeWritableFileWrapper(file));
  }
  return status;
}

Status CompositeEnv::ReuseWritableFile(const std::string& fname,
                                       const std::string& old_fname,
                                       std::unique_ptr<WritableFile>* r,
                                       const EnvOptions& options) {
  IODebugContext dbg;
  Status status;
  std::unique_ptr<FSWritableFile> file;
  status = file_system_->ReuseWritableFile(fname, old_fname,
                                           FileOptions(options), &file, &dbg);
  if (status.ok()) {
    r->reset(new CompositeWritableFileWrapper(file));
  }
  return status;
}

Status CompositeEnv::NewRandomRWFile(const std::string& fname,
                                     std::unique_ptr<RandomRWFile>* result,
                                     const EnvOptions& options) {
  IODebugContext dbg;
  std::unique_ptr<FSRandomRWFile> file;
  Status status;
  status =
      file_system_->NewRandomRWFile(fname, FileOptions(options), &file, &dbg);
  if (status.ok()) {
    result->reset(new CompositeRandomRWFileWrapper(file));
  }
  return status;
}

Status CompositeEnv::NewDirectory(const std::string& name,
                                  std::unique_ptr<Directory>* result) {
  IOOptions io_opts;
  IODebugContext dbg;
  std::unique_ptr<FSDirectory> dir;
  Status status;
  status = file_system_->NewDirectory(name, io_opts, &dir, &dbg);
  if (status.ok()) {
    result->reset(new CompositeDirectoryWrapper(dir));
  }
  return status;
}

}  // namespace ROCKSDB_NAMESPACE
