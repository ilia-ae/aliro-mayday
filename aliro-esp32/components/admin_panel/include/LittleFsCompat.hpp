#pragma once
//
// LittleFsCompat — minimal Arduino-ESP32 LittleFS API surface backed by
// esp_littlefs (ESP-IDF) so the vendored rednblkx WebServerManager.cpp
// builds without dragging Arduino-ESP32 in.
//
// We only implement the methods actually used in WebServerManager.cpp:
//   LittleFS.begin()
//   LittleFS.end()
//   LittleFS.exists(path)
//   LittleFS.usedBytes() / LittleFS.totalBytes()
//   LittleFS.open(path, "r") -> File
//   File.read(buf, n)
//   File.close()
//   File.operator bool()
//
// All paths are POSIX paths under the LittleFS mount point ("/spiffs"
// matching the partition label used by littlefs_create_partition_image).

#include <esp_littlefs.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string>

class FileCompat {
public:
    FileCompat() = default;
    explicit FileCompat(FILE * fp) : fp_(fp) {}
    FileCompat(const FileCompat &) = delete;
    FileCompat & operator=(const FileCompat &) = delete;
    FileCompat(FileCompat && other) noexcept : fp_(other.fp_) { other.fp_ = nullptr; }
    FileCompat & operator=(FileCompat && other) noexcept {
        if (this != &other) {
            if (fp_) fclose(fp_);
            fp_ = other.fp_;
            other.fp_ = nullptr;
        }
        return *this;
    }
    ~FileCompat() { if (fp_) fclose(fp_); }

    explicit operator bool() const { return fp_ != nullptr; }
    int  read(uint8_t * buf, size_t n) { return fp_ ? (int)fread(buf, 1, n, fp_) : -1; }
    int  read(char * buf, size_t n)    { return fp_ ? (int)fread(buf, 1, n, fp_) : -1; }
    void close() { if (fp_) { fclose(fp_); fp_ = nullptr; } }
    size_t size() {
        if (!fp_) return 0;
        long pos = ftell(fp_);
        fseek(fp_, 0, SEEK_END);
        long s = ftell(fp_);
        fseek(fp_, pos, SEEK_SET);
        return (s > 0) ? (size_t)s : 0;
    }
private:
    FILE * fp_ = nullptr;
};

using File = FileCompat;

class LittleFsCompat {
public:
    static constexpr const char * kMountPoint   = "/spiffs";
    static constexpr const char * kPartitionLabel = "spiffs";

    bool begin() {
        if (mounted_) return true;
        esp_vfs_littlefs_conf_t conf = {
            .base_path = kMountPoint,
            .partition_label = kPartitionLabel,
            .partition = nullptr,
            .format_if_mount_failed = false,
            .read_only = false,
            .dont_mount = false,
            .grow_on_mount = false,
        };
        esp_err_t err = esp_vfs_littlefs_register(&conf);
        if (err != ESP_OK) {
            return false;
        }
        mounted_ = true;
        return true;
    }
    void end() {
        if (mounted_) {
            esp_vfs_littlefs_unregister(kPartitionLabel);
            mounted_ = false;
        }
    }
    bool exists(const char * path) const {
        std::string full = std::string(kMountPoint) + path;
        struct stat st;
        return stat(full.c_str(), &st) == 0;
    }
    size_t usedBytes() const {
        size_t total = 0, used = 0;
        esp_littlefs_info(kPartitionLabel, &total, &used);
        return used;
    }
    size_t totalBytes() const {
        size_t total = 0, used = 0;
        esp_littlefs_info(kPartitionLabel, &total, &used);
        return total;
    }
    File open(const char * path, const char * mode) {
        std::string full = std::string(kMountPoint) + path;
        return File(fopen(full.c_str(), mode));
    }
private:
    bool mounted_ = false;
};

inline LittleFsCompat LittleFS;
