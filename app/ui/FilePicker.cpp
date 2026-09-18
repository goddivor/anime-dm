#include "ui/FilePicker.h"

#include <objbase.h>
#include <shobjidl.h>

namespace {

// Runs a shell file dialog with one file type and returns the path chosen.
std::wstring Run(HWND owner, REFCLSID kind, const FileKind& type, const std::wstring& suggested) {
    IFileDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(kind, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        return std::wstring();
    }
    std::wstring pattern = std::wstring(L"*.") + type.extension;
    COMDLG_FILTERSPEC filters[] = {{type.label, pattern.c_str()}, {L"*", L"*.*"}};
    dialog->SetFileTypes(2, filters);
    dialog->SetDefaultExtension(type.extension);
    if (!suggested.empty()) {
        dialog->SetFileName(suggested.c_str());
    }
    std::wstring chosen;
    if (SUCCEEDED(dialog->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                chosen = path;
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dialog->Release();
    return chosen;
}

}  // namespace

std::wstring PickSaveFile(HWND owner, const FileKind& kind, const std::wstring& suggested) {
    return Run(owner, CLSID_FileSaveDialog, kind, suggested);
}

std::wstring PickOpenFile(HWND owner, const FileKind& kind) {
    return Run(owner, CLSID_FileOpenDialog, kind, std::wstring());
}
