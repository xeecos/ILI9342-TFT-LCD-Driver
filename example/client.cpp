#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <dbt.h>
#include <process.h>
#include <wincodec.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

#ifndef WM_APP_SEND_PROGRESS
#define WM_APP_SEND_PROGRESS (WM_APP + 1)
#define WM_APP_SEND_DONE (WM_APP + 2)
#endif

// {86E0D1E0-8089-11D0-9CE4-08003E301F73} - COM port device interface GUID
static const GUID GUID_DEVINTERFACE_COMPORT = {
    0x86E0D1E0, 0x8089, 0x11D0, {0x9C, 0xE4, 0x08, 0x00, 0x3E, 0x30, 0x1F, 0x73}
};

namespace {
struct ImageData {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgb;
};

struct SendThreadData {
    std::string port;
    uint32_t baud_rate = 115200;
    std::vector<uint8_t> payload;
    int width = 320;
    int height = 240;
    HWND hwnd = nullptr;
};

struct AppState {
    HWND hwnd = nullptr;
    HWND portCombo = nullptr;
    RECT previewRect = { 20, 55, 20 + 320, 55 + 240 };
    HDEVNOTIFY devNotify = nullptr;
    HWND statusLabel = nullptr;
    HWND progressBar = nullptr;
    HWND sendButton = nullptr;
    HBITMAP previewBitmap = nullptr;
    std::string selectedPort;
    std::string imagePath;
    bool sending = false;
    ImageData previewImage;
    std::vector<uint8_t> payload;
};

AppState g_app;

static bool load_bmp(const std::string& path, ImageData& image)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }

    struct BMPFileHeader {
        uint16_t bfType;
        uint32_t bfSize;
        uint16_t bfReserved1;
        uint16_t bfReserved2;
        uint32_t bfOffBits;
    } header;

    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (header.bfType != 0x4D42) {
        return false;
    }

    struct BMPInfoHeader {
        uint32_t biSize;
        int32_t biWidth;
        int32_t biHeight;
        uint16_t biPlanes;
        uint16_t biBitCount;
        uint32_t biCompression;
        uint32_t biSizeImage;
        int32_t biXPelsPerMeter;
        int32_t biYPelsPerMeter;
        uint32_t biClrUsed;
        uint32_t biClrImportant;
    } info;

    file.read(reinterpret_cast<char*>(&info), sizeof(info));
    if (info.biBitCount != 24 && info.biBitCount != 32) {
        return false;
    }

    image.width = info.biWidth;
    image.height = std::abs(info.biHeight);

    const int bytes_per_pixel = info.biBitCount / 8;
    const int row_stride = ((image.width * bytes_per_pixel + 3) / 4) * 4;
    std::vector<uint8_t> raw(row_stride * image.height);
    file.seekg(header.bfOffBits, std::ios::beg);
    file.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(raw.size()));

    image.rgb.resize(static_cast<size_t>(image.width) * image.height * 3);
    size_t out_index = 0;
    for (int y = 0; y < image.height; ++y) {
        const uint8_t* row = raw.data() + static_cast<size_t>(y) * row_stride;
        for (int x = 0; x < image.width; ++x) {
            const uint8_t* pixel = row + static_cast<size_t>(x) * bytes_per_pixel;
            image.rgb[out_index++] = pixel[2];
            image.rgb[out_index++] = pixel[1];
            image.rgb[out_index++] = pixel[0];
        }
    }
    return true;
}

static bool load_ppm(const std::string& path, ImageData& image)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }

    auto skip_ws_and_comments = [&](std::istream& s) {
        char c = 0;
        while (s.get(c)) {
            if (c == '#') {
                while (s.get(c) && c != '\n') {}
                continue;
            }
            if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
                continue;
            }
            s.unget();
            break;
        }
    };

    std::string magic;
    file >> magic;
    if (magic != "P6") {
        return false;
    }

    int width = 0;
    int height = 0;
    int max_value = 0;

    skip_ws_and_comments(file);
    file >> width;
    skip_ws_and_comments(file);
    file >> height;
    skip_ws_and_comments(file);
    file >> max_value;
    file.get();

    if (width <= 0 || height <= 0 || max_value <= 0 || max_value > 255) {
        return false;
    }

    image.width = width;
    image.height = height;
    image.rgb.resize(static_cast<size_t>(width) * height * 3);
    file.read(reinterpret_cast<char*>(image.rgb.data()), static_cast<std::streamsize>(image.rgb.size()));
    return true;
}

static bool load_image_with_wic(const std::string& path, ImageData& image)
{
    IWICImagingFactory* factory = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory1, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        return false;
    }

    wchar_t wide_path[MAX_PATH] = {};
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wide_path, MAX_PATH);

    IWICBitmapDecoder* decoder = nullptr;
    hr = factory->CreateDecoderFromFilename(wide_path, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr)) {
        factory->Release();
        return false;
    }

    IWICBitmapFrameDecode* frame = nullptr;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr)) {
        decoder->Release();
        factory->Release();
        return false;
    }

    UINT width = 0;
    UINT height = 0;
    frame->GetSize(&width, &height);

    IWICFormatConverter* converter = nullptr;
    hr = factory->CreateFormatConverter(&converter);
    if (SUCCEEDED(hr)) {
        hr = converter->Initialize(frame, GUID_WICPixelFormat24bppBGR, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    }

    if (FAILED(hr)) {
        if (converter) converter->Release();
        frame->Release();
        decoder->Release();
        factory->Release();
        return false;
    }

    const UINT stride = ((width * 24 + 31) / 32) * 4;
    const UINT size = stride * height;
    std::vector<uint8_t> pixels(size);
    hr = converter->CopyPixels(nullptr, stride, size, pixels.data());
    if (FAILED(hr)) {
        converter->Release();
        frame->Release();
        decoder->Release();
        factory->Release();
        return false;
    }

    image.width = static_cast<int>(width);
    image.height = static_cast<int>(height);
    image.rgb.resize(static_cast<size_t>(width) * height * 3);

    size_t out_index = 0;
    for (size_t i = 0; i < pixels.size(); i += 3) {
        image.rgb[out_index++] = pixels[i + 2];
        image.rgb[out_index++] = pixels[i + 1];
        image.rgb[out_index++] = pixels[i];
    }

    converter->Release();
    frame->Release();
    decoder->Release();
    factory->Release();
    return true;
}

static bool load_image(const std::string& path, ImageData& image)
{
    const std::string lower = path;
    const std::string ext = lower.substr(lower.find_last_of(".\\/") + 1);
    std::string lower_ext = ext;
    for (char& c : lower_ext) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    if (lower_ext == "bmp") {
        return load_bmp(path, image);
    }
    if (lower_ext == "ppm") {
        return load_ppm(path, image);
    }
    if (lower_ext == "png" || lower_ext == "jpg" || lower_ext == "jpeg") {
        return load_image_with_wic(path, image);
    }
    return false;
}

static void resize_bilinear(const ImageData& src, ImageData& dst, int target_width, int target_height)
{
    dst.width = target_width;
    dst.height = target_height;
    dst.rgb.resize(static_cast<size_t>(target_width) * target_height * 3);

    if (src.width <= 0 || src.height <= 0 || target_width <= 0 || target_height <= 0) return;
    if (target_width == src.width && target_height == src.height) {
        dst.rgb = src.rgb;
        return;
    }

    const double ratio_x = static_cast<double>(src.width) / target_width;
    const double ratio_y = static_cast<double>(src.height) / target_height;

    for (int y = 0; y < target_height; ++y) {
        for (int x = 0; x < target_width; ++x) {
            const double src_x = (x + 0.5) * ratio_x - 0.5;
            const double src_y = (y + 0.5) * ratio_y - 0.5;

            const int x0 = std::max(0, static_cast<int>(std::floor(src_x)));
            const int y0 = std::max(0, static_cast<int>(std::floor(src_y)));
            const int x1 = std::min(src.width - 1, x0 + 1);
            const int y1 = std::min(src.height - 1, y0 + 1);

            const double fx = src_x - x0;
            const double fy = src_y - y0;

            for (int c = 0; c < 3; ++c) {
                const double v00 = src.rgb[static_cast<size_t>((y0 * src.width + x0) * 3 + c)];
                const double v10 = src.rgb[static_cast<size_t>((y0 * src.width + x1) * 3 + c)];
                const double v01 = src.rgb[static_cast<size_t>((y1 * src.width + x0) * 3 + c)];
                const double v11 = src.rgb[static_cast<size_t>((y1 * src.width + x1) * 3 + c)];

                const double v = v00 * (1.0 - fx) * (1.0 - fy) + v10 * fx * (1.0 - fy) + v01 * (1.0 - fx) * fy + v11 * fx * fy;
                dst.rgb[static_cast<size_t>((y * target_width + x) * 3 + c)] = static_cast<uint8_t>(std::clamp(static_cast<int>(std::round(v)), 0, 255));
            }
        }
    }
}

static void resize_crop_320x240(const ImageData& src, ImageData& dst)
{
    const int target_w = 320;
    const int target_h = 240;

    dst.width = target_w;
    dst.height = target_h;
    dst.rgb.resize(static_cast<size_t>(target_w) * target_h * 3);

    if (src.width <= 0 || src.height <= 0) return;

    // Scale to cover 320x240 (fill the entire area)
    const double scale = std::max(static_cast<double>(target_w) / src.width,
                                  static_cast<double>(target_h) / src.height);
    const int scaled_w = static_cast<int>(std::round(src.width * scale));
    const int scaled_h = static_cast<int>(std::round(src.height * scale));

    // Bilinear resize to scaled dimensions
    ImageData scaled;
    resize_bilinear(src, scaled, scaled_w, scaled_h);

    // Center-crop to 320x240
    const int crop_x = (scaled_w - target_w) / 2;
    const int crop_y = (scaled_h - target_h) / 2;

    for (int y = 0; y < target_h; ++y) {
        for (int x = 0; x < target_w; ++x) {
            const size_t src_idx = static_cast<size_t>(((crop_y + y) * scaled_w + (crop_x + x)) * 3);
            const size_t dst_idx = static_cast<size_t>((y * target_w + x) * 3);
            dst.rgb[dst_idx + 0] = scaled.rgb[src_idx + 0];
            dst.rgb[dst_idx + 1] = scaled.rgb[src_idx + 1];
            dst.rgb[dst_idx + 2] = scaled.rgb[src_idx + 2];
        }
    }
}

static std::vector<uint8_t> build_rgb565_payload(const ImageData& image)
{
    std::vector<uint8_t> payload;
    payload.reserve(static_cast<size_t>(image.width) * image.height * 2);
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const size_t index = static_cast<size_t>((y * image.width + x) * 3);
            const uint8_t r = image.rgb[index + 0];
            const uint8_t g = image.rgb[index + 1];
            const uint8_t b = image.rgb[index + 2];
            const uint16_t color = static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
            payload.push_back(static_cast<uint8_t>(color & 0xFF));
            payload.push_back(static_cast<uint8_t>((color >> 8) & 0xFF));
        }
    }
    return payload;
}

static bool open_serial_port(const std::string& port_name, uint32_t baud_rate, HANDLE& handle)
{
    const std::string full_name = port_name.find("\\\\.\\") == std::string::npos ? "\\\\.\\" + port_name : port_name;
    handle = CreateFileA(full_name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }

    DCB dcb = {};
    if (!GetCommState(handle, &dcb)) {
        CloseHandle(handle);
        return false;
    }

    dcb.BaudRate = baud_rate;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;

    if (!SetCommState(handle, &dcb)) {
        CloseHandle(handle);
        return false;
    }

    COMMTIMEOUTS timeouts = {};
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = 50;
    timeouts.ReadTotalTimeoutMultiplier = 10;
    timeouts.WriteTotalTimeoutConstant = 50;
    timeouts.WriteTotalTimeoutMultiplier = 10;
    if (!SetCommTimeouts(handle, &timeouts)) {
        CloseHandle(handle);
        return false;
    }

    return true;
}

static std::vector<uint8_t> build_frame(const std::vector<uint8_t>& payload, int width, int height)
{
    std::vector<uint8_t> frame;
    frame.reserve(11 + payload.size());
    frame.push_back(0xAA);
    frame.push_back(0x55);
    frame.push_back(0x01);
    frame.push_back(static_cast<uint8_t>(width & 0xFF));
    frame.push_back(static_cast<uint8_t>((width >> 8) & 0xFF));
    frame.push_back(static_cast<uint8_t>(height & 0xFF));
    frame.push_back(static_cast<uint8_t>((height >> 8) & 0xFF));
    frame.push_back(static_cast<uint8_t>(payload.size() & 0xFF));
    frame.push_back(static_cast<uint8_t>((payload.size() >> 8) & 0xFF));
    frame.push_back(static_cast<uint8_t>((payload.size() >> 16) & 0xFF));
    frame.push_back(static_cast<uint8_t>((payload.size() >> 24) & 0xFF));
    frame.insert(frame.end(), payload.begin(), payload.end());
    return frame;
}

static std::vector<std::string> enumerate_ports()
{
    std::vector<std::string> ports;
    HKEY hkey = nullptr;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DEVICEMAP\\SERIALCOMM", 0, KEY_READ, &hkey) == ERROR_SUCCESS) {
        char value_name[256] = {};
        char value_data[64] = {};
        DWORD index = 0;
        DWORD name_size = sizeof(value_name);
        DWORD data_size = sizeof(value_data);
        DWORD type = 0;
        while (RegEnumValueA(hkey, index, value_name, &name_size, nullptr, &type,
                             reinterpret_cast<BYTE*>(value_data), &data_size) == ERROR_SUCCESS) {
            if (type == REG_SZ && value_data[0] != '\0') {
                ports.push_back(value_data);
            }
            ++index;
            name_size = sizeof(value_name);
            data_size = sizeof(value_data);
        }
        RegCloseKey(hkey);
    }
    return ports;
}

static void set_status(const char* text_utf8)
{
    // Convert UTF-8 to wide for proper Unicode display (Chinese etc.)
    int len = MultiByteToWideChar(CP_UTF8, 0, text_utf8, -1, nullptr, 0);
    if (len > 0) {
        std::wstring wide(static_cast<size_t>(len), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text_utf8, -1, &wide[0], len);
        SetWindowTextW(g_app.statusLabel, wide.c_str());
    } else {
        SetWindowTextA(g_app.statusLabel, text_utf8);
    }
}

static void update_preview(const ImageData& image)
{
    if (g_app.previewBitmap != nullptr) {
        DeleteObject(g_app.previewBitmap);
        g_app.previewBitmap = nullptr;
    }

    if (image.width <= 0 || image.height <= 0) {
        InvalidateRect(g_app.hwnd, nullptr, TRUE);
        return;
    }

    HDC screen_dc = GetDC(nullptr);
    if (!screen_dc) return;

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = image.width;
    bmi.bmiHeader.biHeight = -image.height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    g_app.previewBitmap = CreateDIBSection(screen_dc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screen_dc);

    if (g_app.previewBitmap == nullptr || bits == nullptr) {
        return;
    }

    uint8_t* dst = static_cast<uint8_t*>(bits);
    for (int i = 0; i < image.width * image.height; ++i) {
        const size_t src_index = static_cast<size_t>(i) * 3;
        dst[i * 4 + 0] = image.rgb[src_index + 2];
        dst[i * 4 + 1] = image.rgb[src_index + 1];
        dst[i * 4 + 2] = image.rgb[src_index + 0];
        dst[i * 4 + 3] = 0xFF;
    }

    // Trigger WM_PAINT on the main window to draw the bitmap
    InvalidateRect(g_app.hwnd, nullptr, TRUE);
    UpdateWindow(g_app.hwnd);
}

static void load_selected_image(const std::string& path)
{
    ImageData image;
    if (!load_image(path, image)) {
        set_status("Unsupported image format. Use BMP, PPM, PNG, or JPEG.");
        return;
    }

    g_app.imagePath = path;

    // Auto-scale and center-crop to 320x240
    resize_crop_320x240(image, g_app.previewImage);
    g_app.payload = build_rgb565_payload(g_app.previewImage);

    std::ostringstream msg;
    const std::string filename = path.substr(path.find_last_of("\\/") + 1);
    msg << filename << " (" << image.width << "x" << image.height << ") -> 320x240";
    set_status(msg.str().c_str());
    update_preview(g_app.previewImage);
}

static void populate_ports()
{
    const auto ports = enumerate_ports();

    // Remember current selection
    char current_port[64] = {};
    const int cur_sel = static_cast<int>(SendMessageA(g_app.portCombo, CB_GETCURSEL, 0, 0));
    if (cur_sel != CB_ERR) {
        SendMessageA(g_app.portCombo, CB_GETLBTEXT, static_cast<WPARAM>(cur_sel), reinterpret_cast<LPARAM>(current_port));
    }

    SendMessageA(g_app.portCombo, CB_RESETCONTENT, 0, 0);
    for (const auto& port : ports) {
        SendMessageA(g_app.portCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(port.c_str()));
    }

    if (!ports.empty()) {
        // Try to restore previous selection
        int select_index = CB_ERR;
        if (current_port[0] != '\0') {
            select_index = static_cast<int>(SendMessageA(g_app.portCombo, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(current_port)));
        }
        if (select_index == CB_ERR) {
            select_index = 0;
        }
        SendMessageA(g_app.portCombo, CB_SETCURSEL, static_cast<WPARAM>(select_index), 0);
        char buf[64] = {};
        SendMessageA(g_app.portCombo, CB_GETLBTEXT, static_cast<WPARAM>(select_index), reinterpret_cast<LPARAM>(buf));
        g_app.selectedPort = buf;
    } else {
        g_app.selectedPort.clear();
    }
}

static DWORD WINAPI send_worker(LPVOID param)
{
    auto* data = static_cast<SendThreadData*>(param);
    std::vector<uint8_t> frame = build_frame(data->payload, data->width, data->height);

    HANDLE serial = nullptr;
    const bool opened = open_serial_port(data->port, data->baud_rate, serial);
    if (!opened) {
        PostMessageA(data->hwnd, WM_APP_SEND_DONE, 0, 0);
        delete data;
        return 0;
    }

    size_t sent = 0;
    while (sent < frame.size()) {
        DWORD wrote = 0;
        const size_t chunk = std::min<size_t>(1024, frame.size() - sent);
        const BOOL ok = WriteFile(serial, frame.data() + sent, static_cast<DWORD>(chunk), &wrote, nullptr);
        if (!ok || wrote == 0) {
            CloseHandle(serial);
            PostMessageA(data->hwnd, WM_APP_SEND_DONE, 0, 0);
            delete data;
            return 0;
        }
        sent += wrote;
        const int percent = static_cast<int>((sent * 100) / frame.size());
        PostMessageA(data->hwnd, WM_APP_SEND_PROGRESS, static_cast<WPARAM>(percent), 0);
    }

    CloseHandle(serial);
    PostMessageA(data->hwnd, WM_APP_SEND_DONE, 1, 0);
    delete data;
    return 0;
}

static void start_send()
{
    if (g_app.sending || g_app.payload.empty() || g_app.selectedPort.empty()) {
        return;
    }

    int selected_index = static_cast<int>(SendMessageA(g_app.portCombo, CB_GETCURSEL, 0, 0));
    if (selected_index == CB_ERR) {
        set_status("Please select a serial port.");
        return;
    }

    char buffer[64] = {};
    SendMessageA(g_app.portCombo, CB_GETLBTEXT, selected_index, reinterpret_cast<LPARAM>(buffer));
    g_app.selectedPort = buffer;

    if (g_app.imagePath.empty()) {
        set_status("Please choose an image first.");
        return;
    }

    g_app.sending = true;
    EnableWindow(g_app.sendButton, FALSE);
    SendMessageA(g_app.progressBar, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
    SendMessageA(g_app.progressBar, PBM_SETPOS, 0, 0);
    set_status("Sending frame...");

    auto* data = new SendThreadData();
    data->port = g_app.selectedPort;
    data->baud_rate = 115200;
    data->payload = g_app.payload;
    data->width = 320;
    data->height = 240;
    data->hwnd = g_app.hwnd;
    CreateThread(nullptr, 0, send_worker, data, 0, nullptr);
}

static void browse_for_image()
{
    OPENFILENAMEW ofn = {};
    wchar_t filename[1024] = {};
    ofn.lStructSize = sizeof(OPENFILENAMEW);
    ofn.hwndOwner = g_app.hwnd;
    ofn.lpstrFile = filename;
    ofn.nMaxFile = sizeof(filename) / sizeof(wchar_t);
    ofn.lpstrFilter = L"Image Files\0*.bmp;*.ppm;*.png;*.jpg;*.jpeg\0All Files\0*.*\0";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&ofn)) {
        // Convert wide path to UTF-8
        char utf8_path[MAX_PATH * 3] = {};
        WideCharToMultiByte(CP_UTF8, 0, filename, -1, utf8_path, sizeof(utf8_path), nullptr, nullptr);
        load_selected_image(utf8_path);
    }
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE: {
        g_app.hwnd = hwnd;
        InitCommonControls();

        const int margin = 10;
        const int ctrl_y = 15;
        const int preview_y = 45;
        const int preview_w = 320;
        const int preview_h = 240;

        CreateWindowExA(0, "STATIC", "Port:", WS_CHILD | WS_VISIBLE | SS_LEFT, margin, ctrl_y, 35, 20, hwnd, nullptr, nullptr, nullptr);
        g_app.portCombo = CreateWindowExA(0, "COMBOBOX", "", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST, margin + 35, ctrl_y - 2, 100, 140, hwnd, reinterpret_cast<HMENU>(1001), nullptr, nullptr);

        CreateWindowExA(0, "BUTTON", "Browse...", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, margin + 140, ctrl_y - 2, 70, 24, hwnd, reinterpret_cast<HMENU>(1002), nullptr, nullptr);

        g_app.sendButton = CreateWindowExA(0, "BUTTON", "Send", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, margin + 220, ctrl_y - 2, 60, 24, hwnd, reinterpret_cast<HMENU>(1003), nullptr, nullptr);

        g_app.previewRect = { margin, preview_y, margin + preview_w, preview_y + preview_h };

        g_app.statusLabel = CreateWindowExA(0, "STATIC", "Drop or browse an image (auto-cropped to 320x240).", WS_CHILD | WS_VISIBLE | SS_LEFT, margin, preview_y + preview_h + 8, preview_w, 18, hwnd, nullptr, nullptr, nullptr);
        g_app.progressBar = CreateWindowExA(0, "msctls_progress32", "", WS_CHILD | WS_VISIBLE | PBS_SMOOTH, margin, preview_y + preview_h + 30, preview_w, 18, hwnd, nullptr, nullptr, nullptr);

        populate_ports();

        // Register for COM port device arrival/removal notifications
        DEV_BROADCAST_DEVICEINTERFACE_W notify_filter = {};
        notify_filter.dbcc_size = sizeof(notify_filter);
        notify_filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
        notify_filter.dbcc_classguid = GUID_DEVINTERFACE_COMPORT;
        g_app.devNotify = RegisterDeviceNotificationW(hwnd, &notify_filter, DEVICE_NOTIFY_WINDOW_HANDLE);

        DragAcceptFiles(hwnd, TRUE);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        const RECT r = g_app.previewRect;
        const int w = r.right - r.left;
        const int h = r.bottom - r.top;
        if (g_app.previewBitmap) {
            HDC mem_dc = CreateCompatibleDC(hdc);
            if (mem_dc) {
                SelectObject(mem_dc, g_app.previewBitmap);
                BitBlt(hdc, r.left, r.top, w, h, mem_dc, 0, 0, SRCCOPY);
                DeleteDC(mem_dc);
            }
        } else {
            // Draw a border/frame around the preview area
            SelectObject(hdc, GetStockObject(DC_PEN));
            SetDCPenColor(hdc, RGB(200, 200, 200));
            SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Rectangle(hdc, r.left, r.top, r.right, r.bottom);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DEVICECHANGE:
        if (wParam == DBT_DEVICEARRIVAL || wParam == DBT_DEVICEREMOVECOMPLETE) {
            populate_ports();
        }
        return 0;
    case WM_DESTROY:
        if (g_app.devNotify) {
            UnregisterDeviceNotification(g_app.devNotify);
            g_app.devNotify = nullptr;
        }
        if (g_app.previewBitmap != nullptr) {
            DeleteObject(g_app.previewBitmap);
            g_app.previewBitmap = nullptr;
        }
        PostQuitMessage(0);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == 1002) {
            browse_for_image();
            return 0;
        }
        if (LOWORD(wParam) == 1003) {
            start_send();
            return 0;
        }
        break;
    case WM_DROPFILES: {
        const HDROP drop = reinterpret_cast<HDROP>(wParam);
        wchar_t wpath[MAX_PATH] = {};
        if (DragQueryFileW(drop, 0, wpath, MAX_PATH) > 0) {
            // Convert wide path to UTF-8
            char utf8_path[MAX_PATH * 3] = {};
            WideCharToMultiByte(CP_UTF8, 0, wpath, -1, utf8_path, sizeof(utf8_path), nullptr, nullptr);
            load_selected_image(utf8_path);
        }
        DragFinish(drop);
        return 0;
    }
    case WM_APP_SEND_PROGRESS: {
        const int progress = static_cast<int>(wParam);
        SendMessageA(g_app.progressBar, PBM_SETPOS, static_cast<WPARAM>(progress), 0);
        return 0;
    }
    case WM_APP_SEND_DONE: {
        g_app.sending = false;
        EnableWindow(g_app.sendButton, TRUE);
        if (wParam != 0) {
            set_status("Frame sent successfully.");
        } else {
            set_status("Send failed. Please check the port settings.");
        }
        return 0;
    }
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}
}  // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, PSTR, int)
{
    // Initialize COM for WIC image loading (JPEG, PNG support)
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    WNDCLASSA wc = {};
    wc.lpfnWndProc = window_proc;
    wc.hInstance = instance;
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = "SerialImageGuiClass";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassA(&wc);

    // Calculate window size for fixed client area: 340 x 345 (compact)
    RECT client = { 0, 0, 340, 345 };
    const DWORD win_style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRect(&client, win_style, FALSE);

    const HWND hwnd = CreateWindowExA(0, "SerialImageGuiClass", "ILI9342 Serial Image Sender", win_style,
                                      CW_USEDEFAULT, CW_USEDEFAULT, client.right - client.left, client.bottom - client.top, nullptr, nullptr, instance, nullptr);
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    MSG msg = {};
    while (GetMessageA(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    CoUninitialize();
    return static_cast<int>(msg.wParam);
}
