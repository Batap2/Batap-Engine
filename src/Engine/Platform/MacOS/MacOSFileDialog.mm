// Implémentation macOS de FileDialog.h : NSOpenPanel / NSSavePanel.
//
// Différence avec l'impl Win32 : pas de thread. AppKit exige le main thread
// pour ses panels, donc les variantes Async posent le résultat sur le bus
// immédiatement (modal bloquant) — le pump de l'éditeur le lit à la frame
// suivante, le contrat observable est le même.

#include "FileDialog.h"

#import <Cocoa/Cocoa.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <string>
#include <string_view>
#include <vector>

namespace batap
{

namespace
{
// "*.png;*.jpg" → UTTypes [png, jpg]. Un pattern sans extension exploitable
// (ex. "*.*") désactive le filtrage.
NSArray<UTType*>* contentTypesFromFilters(std::span<const FileDialogFilter> filters)
{
    NSMutableArray<UTType*>* types = [NSMutableArray array];
    for (const FileDialogFilter& f : filters)
    {
        std::string_view patterns = f.patterns;
        while (!patterns.empty())
        {
            const size_t sep = patterns.find(';');
            std::string_view pattern = patterns.substr(0, sep);
            patterns = (sep == std::string_view::npos) ? std::string_view{}
                                                       : patterns.substr(sep + 1);

            const size_t dot = pattern.rfind('.');
            if (dot == std::string_view::npos)
                continue;
            const std::string ext{pattern.substr(dot + 1)};
            if (ext.empty() || ext == "*")
                return nil;  // filtre "tout" : pas de restriction

            UTType* type = [UTType typeWithFilenameExtension:
                                       [NSString stringWithUTF8String:ext.c_str()]];
            if (type)
                [types addObject:type];
        }
    }
    return types.count > 0 ? types : nil;
}

std::vector<std::string> runOpenPanel(std::span<const FileDialogFilter> filters, bool files)
{
    @autoreleasepool
    {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        panel.canChooseFiles = files;
        panel.canChooseDirectories = !files;
        panel.allowsMultipleSelection = files;
        if (files)
            if (NSArray<UTType*>* types = contentTypesFromFilters(filters))
                panel.allowedContentTypes = types;

        std::vector<std::string> paths;
        if ([panel runModal] == NSModalResponseOK)
            for (NSURL* url in panel.URLs)
                paths.emplace_back(url.fileSystemRepresentation);
        return paths;
    }
}
}  // namespace

std::vector<std::string> OpenFilesDialog(std::span<const FileDialogFilter> filters)
{
    return runOpenPanel(filters, true);
}

void OpenFilesDialogAsync(std::span<const FileDialogFilter> filters, FileDialogMsgBus* bus,
                          uint64_t id)
{
    bus->post(FileDialogMsg{id, OpenFilesDialog(filters)});
}

std::string SaveFileDialog(std::span<const FileDialogFilter> filters,
                           std::string_view defaultExtension)
{
    @autoreleasepool
    {
        NSSavePanel* panel = [NSSavePanel savePanel];
        if (NSArray<UTType*>* types = contentTypesFromFilters(filters))
            panel.allowedContentTypes = types;
        if (!defaultExtension.empty())
        {
            const std::string ext{defaultExtension};
            panel.nameFieldStringValue =
                [NSString stringWithUTF8String:("sans-titre." + ext).c_str()];
        }

        if ([panel runModal] != NSModalResponseOK)
            return {};
        return panel.URL.fileSystemRepresentation;
    }
}

std::string OpenFolderDialog()
{
    auto paths = runOpenPanel({}, false);
    return paths.empty() ? std::string{} : paths.front();
}

void OpenFolderDialogAsync(FileDialogMsgBus* bus, uint64_t id)
{
    std::vector<std::string> paths;
    if (std::string folder = OpenFolderDialog(); !folder.empty())
        paths.push_back(std::move(folder));
    bus->post(FileDialogMsg{id, std::move(paths)});
}

}  // namespace batap
