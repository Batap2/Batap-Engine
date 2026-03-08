#pragma once

#include "ThreadSafeMsgBus.h"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace batap
{

struct FileDialogFilter
{
    std::string_view label;     // ex: "Images"
    std::string_view patterns;  // ex: "*.png;*.jpg;*.jpeg"
};

struct FileDialogMsg
{
    uint64_t id_;
    std::vector<std::string> paths_;
};

using FileDialogMsgBus = TSMsgBus<FileDialogMsg>;

std::vector<std::string> OpenFilesDialog(std::span<const FileDialogFilter> filters = {});

void OpenFilesDialogAsync(std::span<const FileDialogFilter> filters, FileDialogMsgBus* bus,
                          uint64_t id = 0);

}  // namespace batap
