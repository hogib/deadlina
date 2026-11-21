// SPDX-License-Identifier: GPL-3.0-or-later
#include "support/faulty_file_system.hpp"

#include <new>
#include <utility>

namespace lina::format::testing {

namespace {

class FaultyInputFile final : public InputFile {
public:
  FaultyInputFile(FaultyFileSystem &owner, std::filesystem::path path,
                  std::unique_ptr<InputFile> real)
      : owner_(&owner), path_(std::move(path)), real_(std::move(real)) {}

  [[nodiscard]] std::uint64_t size() const override { return real_->size(); }

  std::expected<std::size_t, Error> read(std::span<char> buffer) override {
    if (auto fault = owner_->enter("read", path_)) {
      return std::unexpected(std::move(*fault));
    }
    auto count = real_->read(buffer);
    owner_->leave();
    return count;
  }

private:
  FaultyFileSystem *owner_;
  std::filesystem::path path_;
  std::unique_ptr<InputFile> real_;
};

class FaultyOutputFile final : public OutputFile {
public:
  FaultyOutputFile(FaultyFileSystem &owner, std::filesystem::path path,
                   std::unique_ptr<OutputFile> real)
      : owner_(&owner), path_(std::move(path)), real_(std::move(real)) {}

  std::expected<void, Error> write(std::string_view bytes) override {
    const bool firstFault = !owner_->faultHit();
    if (auto fault = owner_->enter("write", path_)) {
      if (firstFault) {
        // What a write that fails half way leaves behind.
        const auto torn = real_->write(bytes.substr(0, bytes.size() / 2));
        static_cast<void>(torn);
      }
      return std::unexpected(std::move(*fault));
    }
    auto written = real_->write(bytes);
    owner_->leave();
    return written;
  }

  std::expected<void, Error> sync() override {
    if (auto fault = owner_->enter("fsync", path_)) {
      return std::unexpected(std::move(*fault));
    }
    auto flushed = real_->sync();
    owner_->leave();
    return flushed;
  }

private:
  FaultyFileSystem *owner_;
  std::filesystem::path path_;
  std::unique_ptr<OutputFile> real_;
};

} // namespace

void FaultyFileSystem::failAt(std::size_t operation, FaultMode mode) {
  faultAt_ = operation;
  mode_ = mode;
  hit_ = false;
  log_.clear();
}

void FaultyFileSystem::heal() {
  faultAt_.reset();
  hit_ = false;
  log_.clear();
}

void FaultyFileSystem::leave() {
  if (afterEach_) {
    afterEach_(log_.back());
  }
}

std::optional<Error>
FaultyFileSystem::enter(std::string_view verb,
                        const std::filesystem::path &path) {
  const std::size_t number = log_.size();
  const std::filesystem::path shown = path.lexically_relative(root_);
  log_.push_back(std::string{verb} + " " + shown.string());
  const bool fails = faultAt_.has_value() &&
                     (number == *faultAt_ ||
                      (mode_ == FaultMode::Crash && number > *faultAt_));
  if (!fails) {
    return std::nullopt;
  }
  hit_ = true;
  if (mode_ == FaultMode::Throw) {
    throw std::bad_alloc{};
  }
  return Error{
      .code = ErrorCode::Io,
      .where = path.string(),
      .message = "injected failure",
  };
}

std::expected<std::unique_ptr<InputFile>, Error>
FaultyFileSystem::openForReading(const std::filesystem::path &file,
                                 Links links) {
  if (auto fault = enter("open", file)) {
    return std::unexpected(std::move(*fault));
  }
  auto opened = real_.openForReading(file, links);
  leave();
  if (!opened.has_value()) {
    return std::unexpected(std::move(opened.error()));
  }
  return std::make_unique<FaultyInputFile>(*this, file, std::move(*opened));
}

std::expected<std::unique_ptr<OutputFile>, Error>
FaultyFileSystem::createFile(const std::filesystem::path &file,
                             std::optional<std::uint32_t> permissions) {
  if (auto fault = enter("create", file)) {
    return std::unexpected(std::move(*fault));
  }
  auto created = real_.createFile(file, permissions);
  leave();
  if (!created.has_value()) {
    return std::unexpected(std::move(created.error()));
  }
  return std::make_unique<FaultyOutputFile>(*this, file, std::move(*created));
}

std::expected<void, Error>
FaultyFileSystem::rename(const std::filesystem::path &from,
                         const std::filesystem::path &to) {
  if (auto fault = enter("rename", from)) {
    return std::unexpected(std::move(*fault));
  }
  return done(real_.rename(from, to));
}

std::expected<void, Error>
FaultyFileSystem::removeFile(const std::filesystem::path &file) {
  if (auto fault = enter("remove", file)) {
    return std::unexpected(std::move(*fault));
  }
  return done(real_.removeFile(file));
}

std::expected<void, Error>
FaultyFileSystem::removeDirectory(const std::filesystem::path &folder) {
  if (auto fault = enter("rmdir", folder)) {
    return std::unexpected(std::move(*fault));
  }
  return done(real_.removeDirectory(folder));
}

std::expected<void, Error>
FaultyFileSystem::createDirectory(const std::filesystem::path &folder) {
  if (auto fault = enter("mkdir", folder)) {
    return std::unexpected(std::move(*fault));
  }
  return done(real_.createDirectory(folder));
}

std::expected<void, Error>
FaultyFileSystem::syncDirectory(const std::filesystem::path &folder) {
  if (auto fault = enter("sync", folder)) {
    return std::unexpected(std::move(*fault));
  }
  return done(real_.syncDirectory(folder));
}

std::expected<FileInfo, Error>
FaultyFileSystem::info(const std::filesystem::path &file) {
  if (auto fault = enter("info", file)) {
    return std::unexpected(std::move(*fault));
  }
  return done(real_.info(file));
}

std::expected<std::vector<std::string>, Error>
FaultyFileSystem::list(const std::filesystem::path &folder) {
  if (auto fault = enter("list", folder)) {
    return std::unexpected(std::move(*fault));
  }
  return done(real_.list(folder));
}

std::expected<LockAttempt, Error>
FaultyFileSystem::lock(const std::filesystem::path &file,
                       std::string_view note) {
  if (auto fault = enter("lock", file)) {
    return std::unexpected(std::move(*fault));
  }
  return done(real_.lock(file, note));
}

} // namespace lina::format::testing
