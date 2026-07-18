// SPDX-License-Identifier: MIT
// Copyright (c) 2024 Saxon Nicholls

//
//  encode_decode_mmap.hpp
//  BaseEncodeDecode
//
//  Optional drop-in adapter for memory-mapped file I/O, so multi-GB files can
//  be encoded/decoded with zero copies on BOTH ends: the input file is mapped
//  and handed straight to the parallel codec, and the output file is sized,
//  mapped, and written into directly by the worker threads - no intermediate
//  std::string/Binary the size of the data. Include it explicitly:
//
//      #include "encode_decode_mmap.hpp"
//
//      // Whole-file one-liners (input mapped, output mapped, parallel):
//      EncodeBase64File("firmware.bin", "firmware.b64");
//      DecodeBase64File("firmware.b64", "firmware.out.bin");
//
//      // Or map a file yourself and feed any API a zero-copy view:
//      MappedFile in("firmware.bin");
//      std::string b64 = EncodeBase64Parallel(in);   // in is a ByteSource
//      Binary sha_input(in.begin(), in.end());        // ... or anything else
//
//  Portable over POSIX (mmap) and Windows (CreateFileMapping/MapViewOfFile).
//  Mapping an empty file is well-defined here and yields an empty view.
//
//  Note: base-encoding a file to disk inflates it (Base64 is +33%); this is
//  for when the encoded form must reach a text-only channel. For streaming to
//  a socket/pipe in constant memory without random access, see
//  encode_decode_stream.hpp.
//

#ifndef encode_decode_mmap_hpp
#define encode_decode_mmap_hpp

#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#include "encode_decode_base_whatever.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace snicholls {

    namespace detail {
        [[noreturn]] inline void ThrowSystemError(const char* what) {
#if defined(_WIN32)
            throw std::system_error(static_cast<int>(::GetLastError()), std::system_category(), what);
#else
            throw std::system_error(errno, std::generic_category(), what);
#endif
        }
    } // namespace detail

    // Read-only memory map of an existing file. Models a contiguous range of
    // bytes, so it is itself a ByteSource: pass it to any Encode*/Decode*.
    class MappedFile {
        const uint8_t* ptr_ = nullptr;
        size_t size_ = 0;
#if defined(_WIN32)
        void* file_ = nullptr;    // HANDLE
        void* mapping_ = nullptr; // HANDLE
#else
        int fd_ = -1;
#endif

    public:
        explicit MappedFile(const std::string& path) {
#if defined(_WIN32)
            file_ = ::CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file_ == INVALID_HANDLE_VALUE) {
                file_ = nullptr;
                detail::ThrowSystemError("MappedFile: CreateFileA");
            }
            LARGE_INTEGER fileSize;
            if (!::GetFileSizeEx(file_, &fileSize)) {
                ::CloseHandle(file_);
                detail::ThrowSystemError("MappedFile: GetFileSizeEx");
            }
            size_ = static_cast<size_t>(fileSize.QuadPart);
            if (size_ == 0) {
                return; // empty file: valid, empty view
            }
            mapping_ = ::CreateFileMappingA(file_, nullptr, PAGE_READONLY, 0, 0, nullptr);
            if (mapping_ == nullptr) {
                ::CloseHandle(file_);
                detail::ThrowSystemError("MappedFile: CreateFileMappingA");
            }
            ptr_ = static_cast<const uint8_t*>(::MapViewOfFile(mapping_, FILE_MAP_READ, 0, 0, 0));
            if (ptr_ == nullptr) {
                ::CloseHandle(mapping_);
                ::CloseHandle(file_);
                detail::ThrowSystemError("MappedFile: MapViewOfFile");
            }
#else
            fd_ = ::open(path.c_str(), O_RDONLY);
            if (fd_ < 0) {
                detail::ThrowSystemError("MappedFile: open");
            }
            struct stat st{};
            if (::fstat(fd_, &st) != 0) {
                ::close(fd_);
                detail::ThrowSystemError("MappedFile: fstat");
            }
            size_ = static_cast<size_t>(st.st_size);
            if (size_ == 0) {
                return; // empty file: mmap of length 0 is an error, keep an empty view
            }
            void* p = ::mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd_, 0);
            if (p == MAP_FAILED) {
                ::close(fd_);
                detail::ThrowSystemError("MappedFile: mmap");
            }
            ptr_ = static_cast<const uint8_t*>(p);
#endif
        }

        ~MappedFile() { Close(); }

        MappedFile(MappedFile&& other) noexcept { MoveFrom(other); }
        MappedFile& operator=(MappedFile&& other) noexcept {
            if (this != &other) {
                Close();
                MoveFrom(other);
            }
            return *this;
        }
        MappedFile(const MappedFile&) = delete;
        MappedFile& operator=(const MappedFile&) = delete;

        const uint8_t* data() const noexcept { return ptr_; }
        size_t size() const noexcept { return size_; }
        bool empty() const noexcept { return size_ == 0; }
        const uint8_t* begin() const noexcept { return ptr_; }
        const uint8_t* end() const noexcept { return ptr_ + size_; }

    private:
        void MoveFrom(MappedFile& other) {
            ptr_ = other.ptr_;
            size_ = other.size_;
#if defined(_WIN32)
            file_ = other.file_;
            mapping_ = other.mapping_;
            other.file_ = other.mapping_ = nullptr;
#else
            fd_ = other.fd_;
            other.fd_ = -1;
#endif
            other.ptr_ = nullptr;
            other.size_ = 0;
        }

        void Close() {
#if defined(_WIN32)
            if (ptr_) { ::UnmapViewOfFile(ptr_); }
            if (mapping_) { ::CloseHandle(mapping_); }
            if (file_) { ::CloseHandle(file_); }
            file_ = mapping_ = nullptr;
#else
            if (ptr_) { ::munmap(const_cast<uint8_t*>(ptr_), size_); }
            if (fd_ >= 0) { ::close(fd_); }
            fd_ = -1;
#endif
            ptr_ = nullptr;
            size_ = 0;
        }
    };

    // Writable memory map of a newly created output file of a fixed size. The
    // file is created/truncated to `size` bytes and mapped; worker threads (or
    // the caller) write into data(). Flushed and unmapped on destruction.
    class MappedOutputFile {
        uint8_t* ptr_ = nullptr;
        size_t size_ = 0;
#if defined(_WIN32)
        void* file_ = nullptr;
        void* mapping_ = nullptr;
#else
        int fd_ = -1;
#endif

    public:
        MappedOutputFile(const std::string& path, size_t size) : size_(size) {
#if defined(_WIN32)
            file_ = ::CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file_ == INVALID_HANDLE_VALUE) {
                file_ = nullptr;
                detail::ThrowSystemError("MappedOutputFile: CreateFileA");
            }
            if (size_ == 0) {
                return; // zero-length output: nothing to map
            }
            LARGE_INTEGER li;
            li.QuadPart = static_cast<LONGLONG>(size_);
            mapping_ = ::CreateFileMappingA(file_, nullptr, PAGE_READWRITE, li.HighPart,
                                            li.LowPart, nullptr);
            if (mapping_ == nullptr) {
                ::CloseHandle(file_);
                detail::ThrowSystemError("MappedOutputFile: CreateFileMappingA");
            }
            ptr_ = static_cast<uint8_t*>(::MapViewOfFile(mapping_, FILE_MAP_WRITE, 0, 0, 0));
            if (ptr_ == nullptr) {
                ::CloseHandle(mapping_);
                ::CloseHandle(file_);
                detail::ThrowSystemError("MappedOutputFile: MapViewOfFile");
            }
#else
            fd_ = ::open(path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
            if (fd_ < 0) {
                detail::ThrowSystemError("MappedOutputFile: open");
            }
            if (size_ == 0) {
                return; // zero-length output: leave the empty file, nothing to map
            }
            if (::ftruncate(fd_, static_cast<off_t>(size_)) != 0) {
                ::close(fd_);
                detail::ThrowSystemError("MappedOutputFile: ftruncate");
            }
            void* p = ::mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
            if (p == MAP_FAILED) {
                ::close(fd_);
                detail::ThrowSystemError("MappedOutputFile: mmap");
            }
            ptr_ = static_cast<uint8_t*>(p);
#endif
        }

        ~MappedOutputFile() { Close(); }

        MappedOutputFile(MappedOutputFile&& other) noexcept { MoveFrom(other); }
        MappedOutputFile& operator=(MappedOutputFile&& other) noexcept {
            if (this != &other) {
                Close();
                MoveFrom(other);
            }
            return *this;
        }
        MappedOutputFile(const MappedOutputFile&) = delete;
        MappedOutputFile& operator=(const MappedOutputFile&) = delete;

        uint8_t* data() noexcept { return ptr_; }
        size_t size() const noexcept { return size_; }

    private:
        void MoveFrom(MappedOutputFile& other) {
            ptr_ = other.ptr_;
            size_ = other.size_;
#if defined(_WIN32)
            file_ = other.file_;
            mapping_ = other.mapping_;
            other.file_ = other.mapping_ = nullptr;
#else
            fd_ = other.fd_;
            other.fd_ = -1;
#endif
            other.ptr_ = nullptr;
            other.size_ = 0;
        }

        void Close() {
#if defined(_WIN32)
            if (ptr_) { ::FlushViewOfFile(ptr_, 0); ::UnmapViewOfFile(ptr_); }
            if (mapping_) { ::CloseHandle(mapping_); }
            if (file_) { ::CloseHandle(file_); }
            file_ = mapping_ = nullptr;
#else
            if (ptr_) { ::msync(ptr_, size_, MS_SYNC); ::munmap(ptr_, size_); }
            if (fd_ >= 0) { ::close(fd_); }
            fd_ = -1;
#endif
            ptr_ = nullptr;
            size_ = 0;
        }
    };

    // Whole-file encode: map input, size + map output, encode straight across
    // in parallel. threadCount 0 = auto.
    template<size_t BitGroupSize, size_t AlphabetSize, const std::array<char, AlphabetSize>& Alphabet, bool PaddingRequired>
    void BaseEncodeFile(const std::string& inputPath, const std::string& outputPath, unsigned threadCount = 0) {
        MappedFile input(inputPath);
        const size_t outSize = EncodedLength<BitGroupSize, PaddingRequired>(input.size());
        MappedOutputFile output(outputPath, outSize);
        if (outSize > 0) {
            BaseEncodeParallelInto<BitGroupSize, AlphabetSize, Alphabet, PaddingRequired>(
                input.data(), input.size(), reinterpret_cast<char*>(output.data()), threadCount);
        }
    }

    // Whole-file decode: map input, size + map output, decode straight across
    // in parallel. Throws std::invalid_argument on malformed input (the
    // partially written output file is left in place; delete it if you care).
    template<size_t BitGroupSize, size_t AlphabetSize, const std::array<char, AlphabetSize>& Alphabet, bool PaddingRequired>
    void BaseDecodeFile(const std::string& inputPath, const std::string& outputPath, unsigned threadCount = 0) {
        MappedFile input(inputPath);
        const size_t outSize = DecodedLength<BitGroupSize, PaddingRequired>(
            reinterpret_cast<const char*>(input.data()), input.size());
        MappedOutputFile output(outputPath, outSize);
        if (outSize > 0) {
            BaseDecodeParallelInto<BitGroupSize, AlphabetSize, Alphabet, PaddingRequired>(
                reinterpret_cast<const char*>(input.data()), input.size(), output.data(), threadCount);
        }
    }

#define SNICHOLLS_DEFINE_MMAP_SCHEME(Name, BitGroupSize, AlphabetSize, Alphabet, Padded) \
    inline void Encode##Name##File(const std::string& inputPath, const std::string& outputPath, unsigned threadCount = 0) { \
        BaseEncodeFile<BitGroupSize, AlphabetSize, Alphabet, Padded>(inputPath, outputPath, threadCount); \
    } \
    inline void Decode##Name##File(const std::string& inputPath, const std::string& outputPath, unsigned threadCount = 0) { \
        BaseDecodeFile<BitGroupSize, AlphabetSize, Alphabet, Padded>(inputPath, outputPath, threadCount); \
    }

    SNICHOLLS_FOR_EACH_SCHEME(SNICHOLLS_DEFINE_MMAP_SCHEME)

#undef SNICHOLLS_DEFINE_MMAP_SCHEME
}

#endif /* encode_decode_mmap_hpp */
