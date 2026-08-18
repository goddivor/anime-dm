#include "ui/PosterDialog.h"

#include <objbase.h>
#include <shobjidl.h>

#include <algorithm>
#include <fstream>

#include "core/Image.h"
#include "core/Text.h"
#include "ui/Paint.h"
#include "ui/Resource.h"
#include "ui/Strings.h"
#include "ui/Theme.h"

namespace {

// What the preview needs to draw and to save.
struct Preview {
    const std::vector<uint8_t>* bytes = nullptr;
    std::string title;
    std::string sourceUrl;
    HBITMAP bitmap = nullptr;
};

// The extension the address suggests, jpg by default.
std::wstring ExtensionOf(const std::string& url) {
    std::string lower = url;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (const char* candidate : {".png", ".webp", ".gif", ".jpeg", ".jpg"}) {
        if (lower.find(candidate) != std::string::npos) {
            return Widen(candidate + 1);
        }
    }
    return L"jpg";
}

// A file name the system will accept.
std::wstring SafeName(const std::string& title) {
    std::wstring name = Widen(title);
    for (wchar_t& letter : name) {
        if (wcschr(L"\\/:*?\"<>|", letter) != nullptr) {
            letter = L' ';
        }
    }
    return name.empty() ? std::wstring(L"cover") : name;
}

// Asks where to save, then writes the bytes there.
bool Save(HWND owner, const Preview& preview) {
    IFileSaveDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dialog)))) {
        return false;
    }

    std::wstring extension = ExtensionOf(preview.sourceUrl);
    std::wstring suggested = SafeName(preview.title) + L"." + extension;
    std::wstring pattern = L"*." + extension;
    COMDLG_FILTERSPEC filter = {L"Image", pattern.c_str()};

    dialog->SetFileTypes(1, &filter);
    dialog->SetDefaultExtension(extension.c_str());
    dialog->SetFileName(suggested.c_str());

    bool written = false;
    if (SUCCEEDED(dialog->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                std::ofstream file(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
                if (file) {
                    file.write(reinterpret_cast<const char*>(preview.bytes->data()),
                               static_cast<std::streamsize>(preview.bytes->size()));
                    written = file.good();
                }
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dialog->Release();
    return written;
}

// Draws the cover as large as the slot allows, keeping its proportions.
void DrawImage(const DRAWITEMSTRUCT& draw, Preview& preview) {
    const ThemeColors& colours = ActiveTheme().Colors();
    HBRUSH background = CreateSolidBrush(colours.window);
    RECT bounds = draw.rcItem;
    FillRect(draw.hDC, &bounds, background);
    DeleteObject(background);

    if (preview.bitmap == nullptr) {
        preview.bitmap = image::Fit(*preview.bytes, bounds.right - bounds.left,
                                    bounds.bottom - bounds.top);
    }
    if (preview.bitmap == nullptr) {
        return;
    }

    BITMAP shape = {};
    GetObjectW(preview.bitmap, sizeof(shape), &shape);
    int x = bounds.left + (bounds.right - bounds.left - shape.bmWidth) / 2;
    int y = bounds.top + (bounds.bottom - bounds.top - shape.bmHeight) / 2;

    HDC memory = CreateCompatibleDC(draw.hDC);
    HBITMAP old = static_cast<HBITMAP>(SelectObject(memory, preview.bitmap));
    BitBlt(draw.hDC, x, y, shape.bmWidth, shape.bmHeight, memory, 0, 0, SRCCOPY);
    SelectObject(memory, old);
    DeleteDC(memory);
}

INT_PTR CALLBACK PosterDialogProc(HWND dialog, UINT msg, WPARAM wParam, LPARAM lParam) {
    INT_PTR colour = 0;
    if (ThemeDialogMessage(msg, wParam, &colour)) {
        return colour;
    }

    auto* preview = reinterpret_cast<Preview*>(GetWindowLongPtrW(dialog, GWLP_USERDATA));

    switch (msg) {
    case WM_INITDIALOG:
        SetWindowLongPtrW(dialog, GWLP_USERDATA, lParam);
        preview = reinterpret_cast<Preview*>(lParam);
        SetWindowTextW(dialog, Widen(preview->title.empty() ? Narrow(Str(STR_POSTER_TITLE))
                                                            : preview->title)
                                   .c_str());
        SetDialogText(dialog, IDC_POSTER_SAVE, STR_POSTER_SAVE);
        SetDialogText(dialog, IDCANCEL, STR_DLG_CLOSE);
        ActiveTheme().ApplyToDialog(dialog);
        return TRUE;

    case WM_DRAWITEM: {
        auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (preview != nullptr && draw->CtlID == IDC_POSTER_IMAGE) {
            DrawImage(*draw, *preview);
            return TRUE;
        }
        return FALSE;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_POSTER_SAVE:
            if (preview != nullptr && Save(dialog, *preview)) {
                MessageBoxW(dialog, Str(STR_POSTER_SAVED), Str(STR_POSTER_TITLE),
                            MB_OK | MB_ICONINFORMATION);
            }
            return TRUE;
        case IDOK:
        case IDCANCEL:
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        default:
            return FALSE;
        }

    case WM_DESTROY:
        if (preview != nullptr && preview->bitmap != nullptr) {
            DeleteObject(preview->bitmap);
            preview->bitmap = nullptr;
        }
        return FALSE;

    case WM_CLOSE:
        EndDialog(dialog, IDCANCEL);
        return TRUE;

    default:
        return FALSE;
    }
}

}  // namespace

// Shows the cover at full size, with a way to save it.
void ShowPosterPreview(HWND owner, HINSTANCE instance, const std::vector<uint8_t>& bytes,
                       const std::string& title, const std::string& sourceUrl) {
    if (bytes.empty()) {
        return;
    }

    Preview preview;
    preview.bytes = &bytes;
    preview.title = title;
    preview.sourceUrl = sourceUrl;

    DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_POSTER), owner, PosterDialogProc,
                    reinterpret_cast<LPARAM>(&preview));
}
