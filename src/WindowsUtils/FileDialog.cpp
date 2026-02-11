#include "FileDialog.h"

#include <shobjidl.h>
#include <windows.h>

#include <cstddef>
#include <string>
#include <thread>

namespace batap
{

static std::wstring ToW(std::string_view s)
{
    if (s.empty())
        return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), len);
    return out;
}

std::vector<std::string> OpenFilesDialog(std::span<const FileDialogFilter> filters)
{
    std::vector<std::string> result;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr))
        return result;

    IFileOpenDialog* dialog = nullptr;
    hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&dialog));
    if (FAILED(hr))
    {
        CoUninitialize();
        return result;
    }

    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_ALLOWMULTISELECT | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST);

    // ---- Filters optionnels (default = All files) ----
    FileDialogFilter defaultFilter{"All files", "*.*"};
    if (filters.empty())
        filters = std::span<const FileDialogFilter>(&defaultFilter, 1);

    std::vector<std::wstring> labelsW, patternsW;
    std::vector<COMDLG_FILTERSPEC> specs;
    labelsW.reserve(filters.size());
    patternsW.reserve(filters.size());
    specs.reserve(filters.size());

    for (const auto& f : filters)
    {
        labelsW.push_back(ToW(f.label));
        patternsW.push_back(ToW(f.patterns));
        specs.push_back(COMDLG_FILTERSPEC{labelsW.back().c_str(), patternsW.back().c_str()});
    }

    dialog->SetFileTypes(static_cast<UINT>(specs.size()), specs.data());
    dialog->SetFileTypeIndex(1);  // 1-based

    // ---- Show + collect ----
    if (SUCCEEDED(dialog->Show(nullptr)))
    {
        IShellItemArray* items = nullptr;
        if (SUCCEEDED(dialog->GetResults(&items)))
        {
            DWORD count = 0;
            items->GetCount(&count);

            for (DWORD i = 0; i < count; ++i)
            {
                IShellItem* item = nullptr;
                if (SUCCEEDED(items->GetItemAt(i, &item)))
                {
                    PWSTR path = nullptr;
                    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)))
                    {
                        int bufferSize =
                            WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);

                        std::string pathStr(static_cast<size_t>(bufferSize - 1), '\0');

                        WideCharToMultiByte(CP_UTF8, 0, path, -1, &pathStr[0], bufferSize, nullptr,
                                            nullptr);

                        result.emplace_back(pathStr);
                        CoTaskMemFree(path);
                    }
                    item->Release();
                }
            }
            items->Release();
        }
    }

    dialog->Release();
    CoUninitialize();
    return result;
}

void OpenFilesDialogAsync(std::span<const FileDialogFilter> filters, FileDialogMsgBus* bus)
{
    std::thread(
        [filters, bus]() mutable
        {
            FileDialogMsg result = OpenFilesDialog(filters);
            bus->post(result);
        })
        .detach();
}

}  // namespace batap
