#pragma once

#include "ThreadSafeMsgBus.h"

#include <span>
#include <string_view>
#include <vector>

namespace rayvox
{

struct FileDialogFilter
{
    std::string_view label;     // ex: "Images"
    std::string_view patterns;  // ex: "*.png;*.jpg;*.jpeg"
};

using FileDialogMsg = std::vector<std::string>;
using FileDialogMsgBus = TSMsgBus<FileDialogMsg>;

std::vector<std::string>
OpenFilesDialog(std::span<const FileDialogFilter> filters = {});

void OpenFilesDialogAsync(std::span<const FileDialogFilter> filters, FileDialogMsgBus* bus);

}  // namespace rayvox
