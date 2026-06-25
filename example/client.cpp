#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

struct ImageData {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgb;
};

struct Options {
    std::string port = "COM3";
    std::string image_path;
    uint32_t baud_rate = 115200;
    int target_width = 320;
    int target_height = 240;
    bool verbose = false;
};

static void print_usage(const char* app_name)
{
    std::cout << "Usage: " << app_name << " --port COM3 --file image.bmp [--baud 115200] [--size 320x240]\n"
              << "       " << app_name << " --port COM3 --file image.bmp --size 160x120\n"
              << "\n"
              << "Options:\n"
              << "  --port <name>      Serial port name, e.g. COM3 or \\\\.\\COM10\n"
              << "  --file <path>      Input image path (.bmp or .ppm)\n"
              << "  --baud <rate>      Serial baud rate (default: 115200)\n"
              << "  --size <W>x<H>     Resize target size before sending\n"
              << "  --verbose          Print more details\n";
}

static bool parse_size(const std::string& text, int& width, int& height)
{
    std::size_t pos = text.find('x');
    if (pos == std::string::npos) {
        pos = text.find('X');
    }
    if (pos == std::string::npos) {
        return false;
    }

    std::string w = text.substr(0, pos);
    std::string h = text.substr(pos + 1);
    std::istringstream ws(w);
    std::istringstream hs(h);
    ws >> width;
    hs >> height;
    return ws && hs && width > 0 && height > 0;
}

static bool parse_args(int argc, char** argv, Options& options)
{
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            return false;
        }
        if (arg == "--port" && i + 1 < argc) {
            options.port = argv[++i];
        } else if (arg == "--file" && i + 1 < argc) {
            options.image_path = argv[++i];
        } else if (arg == "--baud" && i + 1 < argc) {
            options.baud_rate = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--size" && i + 1 < argc) {
            if (!parse_size(argv[++i], options.target_width, options.target_height)) {
                std::cerr << "Invalid size format. Use W x H, e.g. 320x240.\n";
                return false;
            }
        } else if (arg == "--verbose") {
            options.verbose = true;
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            return false;
        }
    }

    if (options.image_path.empty()) {
        std::cerr << "An image file is required.\n";
        return false;
    }
    return true;
}

static bool load_bmp(const std::string& path, ImageData& image)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open BMP file: " << path << "\n";
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
        std::cerr << "Not a BMP file.\n";
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
        std::cerr << "Only 24-bit and 32-bit BMP files are supported.\n";
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
            // BMP stores BGR(BGRX)
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
        std::cerr << "Failed to open PPM file: " << path << "\n";
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
        std::cerr << "Only binary PPM (P6) is supported.\n";
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
        std::cerr << "Invalid PPM header.\n";
        return false;
    }

    image.width = width;
    image.height = height;
    image.rgb.resize(static_cast<size_t>(width) * height * 3);
    file.read(reinterpret_cast<char*>(image.rgb.data()), static_cast<std::streamsize>(image.rgb.size()));
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

    std::cerr << "Unsupported image format. Use .bmp or .ppm.\n";
    return false;
}

static void resize_nearest(const ImageData& src, ImageData& dst, int target_width, int target_height)
{
    dst.width = target_width;
    dst.height = target_height;
    dst.rgb.resize(static_cast<size_t>(target_width) * target_height * 3);

    for (int y = 0; y < target_height; ++y) {
        const int src_y = (y * src.height) / target_height;
        for (int x = 0; x < target_width; ++x) {
            const int src_x = (x * src.width) / target_width;
            const size_t src_index = static_cast<size_t>((src_y * src.width + src_x) * 3);
            const size_t dst_index = static_cast<size_t>((y * target_width + x) * 3);
            dst.rgb[dst_index + 0] = src.rgb[src_index + 0];
            dst.rgb[dst_index + 1] = src.rgb[src_index + 1];
            dst.rgb[dst_index + 2] = src.rgb[src_index + 2];
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
        std::cerr << "Failed to open serial port: " << full_name << "\n";
        return false;
    }

    DCB dcb = {};
    if (!GetCommState(handle, &dcb)) {
        CloseHandle(handle);
        std::cerr << "GetCommState failed.\n";
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
        std::cerr << "SetCommState failed.\n";
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
        std::cerr << "SetCommTimeouts failed.\n";
        return false;
    }

    return true;
}

static bool send_frame(HANDLE handle, const std::vector<uint8_t>& payload, int width, int height)
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

    DWORD wrote = 0;
    if (!WriteFile(handle, frame.data(), static_cast<DWORD>(frame.size()), &wrote, nullptr)) {
        std::cerr << "WriteFile failed.\n";
        return false;
    }
    return wrote == frame.size();
}

int main(int argc, char** argv)
{
    Options options;
    if (!parse_args(argc, argv, options)) {
        print_usage(argv[0]);
        return 1;
    }

    ImageData src;
    if (!load_image(options.image_path, src)) {
        return 2;
    }

    ImageData resized;
    if (src.width != options.target_width || src.height != options.target_height) {
        resize_nearest(src, resized, options.target_width, options.target_height);
    } else {
        resized = src;
    }

    std::vector<uint8_t> payload = build_rgb565_payload(resized);

    if (options.verbose) {
        std::cout << "Loaded image: " << src.width << "x" << src.height << " -> "
                  << resized.width << "x" << resized.height << "\n";
        std::cout << "Payload size: " << payload.size() << " bytes\n";
    }

    HANDLE serial = nullptr;
    if (!open_serial_port(options.port, options.baud_rate, serial)) {
        return 3;
    }

    std::cout << "Sending to " << options.port << " at " << options.baud_rate << " baud...\n";
    if (!send_frame(serial, payload, resized.width, resized.height)) {
        CloseHandle(serial);
        return 4;
    }

    std::cout << "Sent " << resized.width << "x" << resized.height << " RGB565 frame successfully.\n";
    CloseHandle(serial);
    return 0;
}
