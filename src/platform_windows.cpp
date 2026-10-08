#define UNICODE
#define _UNICODE
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <mmsystem.h>


#include "audio_core.hpp"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr int kPickerId = 1001;
constexpr int kAnalyzeId = 1002;
constexpr int kOpenId = 1003;
constexpr int kPlayId = 1004;
constexpr int kFeaturesId = 1005;

HWND g_window = nullptr;
HWND g_picker = nullptr;
HWND g_status = nullptr;
HWND g_prediction = nullptr;
HWND g_features = nullptr;
std::vector<forensic::Audio> g_samples;
forensic::RandomForest g_model;
forensic::Evaluation g_evaluation;
forensic::Audio g_current;
forensic::Analysis g_analysis;
bool g_has_analysis = false;
std::filesystem::path g_resource_dir;

std::wstring widen(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int length = MultiByteToWideChar(
        CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (length <= 0) {
        return L"";
    }
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, result.data(), length);
    result.resize(static_cast<std::size_t>(length - 1));
    return result;
}

std::filesystem::path executable_directory() {
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) {
        throw std::runtime_error("Could not locate the application executable.");
    }
    path.resize(length);
    return std::filesystem::path(path).parent_path();
}

HWND create_control(DWORD extended_style, const wchar_t* type, const wchar_t* title,
                    DWORD style, int x, int y, int width, int height, int id) {
    return CreateWindowExW(
        extended_style, type, title, style, x, y, width, height,
        g_window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr), nullptr);
}

void set_font(HWND control) {
    SendMessageW(control, WM_SETFONT,
        reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
}

void set_status(const std::string& text) {
    if (g_status != nullptr) {
        SetWindowTextW(g_status, widen(text).c_str());
    }
}

void update_analysis(forensic::Audio audio) {
    try {
        g_current = std::move(audio);
        g_analysis = forensic::analyze(g_current);
        g_has_analysis = true;
        const auto prediction = g_model.predict(g_analysis.features);
        std::ostringstream output;
        output.setf(std::ios::fixed);
        output.precision(1);
        output << forensic::label_text(prediction.label)
               << "  ·  demo score " << prediction.score * 100.0 << "%";
        SetWindowTextW(g_prediction, widen(output.str()).c_str());

        std::wostringstream metadata;
        metadata.precision(2);
        metadata.setf(std::ios::fixed);
        metadata << g_current.path.filename().wstring() << L"  ·  "
                 << g_analysis.duration_seconds << L" s  ·  source "
                 << g_current.source_sample_rate << L" Hz -> "
                 << g_current.sample_rate << L" Hz  ·  "
                 << g_current.channels << L" channel(s)\r\n\r\n"
                 << widen(forensic::format_features(g_analysis));
        SetWindowTextW(g_features, metadata.str().c_str());
        InvalidateRect(g_window, nullptr, TRUE);
    } catch (const std::exception& error) {
        set_status(error.what());
    }
}

void analyze_selected() {
    const int index = static_cast<int>(SendMessageW(g_picker, CB_GETCURSEL, 0, 0));
    if (index < 0 || static_cast<std::size_t>(index) >= g_samples.size()) {
        set_status("Choose a bundled sample or open a WAV or MP3 file.");
        return;
    }
    try {
        auto sample = forensic::load_audio(g_samples[static_cast<std::size_t>(index)].path);
        sample.label = g_samples[static_cast<std::size_t>(index)].label;
        update_analysis(std::move(sample));
    } catch (const std::exception& error) {
        set_status(error.what());
    }
}

void open_audio_file() {
    wchar_t file[MAX_PATH] = {};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = g_window;
    dialog.lpstrFilter = L"Audio files (*.wav;*.mp3)\0*.wav;*.mp3\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = file;
    dialog.nMaxFile = MAX_PATH;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    dialog.lpstrTitle = L"Open audio for local DSP analysis";
    if (!GetOpenFileNameW(&dialog)) {
        return;
    }
    try {
        update_analysis(forensic::load_audio(file));
    } catch (const std::exception& error) {
        set_status(error.what());
    }
}

void draw_text(HDC dc, int x, int y, const std::wstring& text, COLORREF color,
               int size, const wchar_t* face = L"Segoe UI") {
    HFONT font = CreateFontW(-size, 0, 0, 0, FW_MEDIUM, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, face);
    const auto previous = SelectObject(dc, font);
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    TextOutW(dc, x, y, text.c_str(), static_cast<int>(text.size()));
    SelectObject(dc, previous);
    DeleteObject(font);
}

void paint_charts(HDC dc) {
    RECT client{};
    GetClientRect(g_window, &client);
    HBRUSH background = CreateSolidBrush(RGB(12, 17, 26));
    FillRect(dc, &client, background);
    DeleteObject(background);

    draw_text(dc, 24, 15, L"AUDIO//FORENSICS", RGB(231, 237, 245), 28);
    draw_text(dc, 25, 53, L"DISCRETE-TIME SIGNAL ANALYSIS  ·  OFFLINE DEMONSTRATION",
              RGB(86, 230, 165), 11, L"Consolas");
    draw_text(dc, 28, 128, L"01 / WAVEFORM", RGB(86, 230, 165), 12, L"Consolas");
    draw_text(dc, 28, 394, L"02 / STFT SPECTROGRAM", RGB(86, 230, 165), 12, L"Consolas");
    if (!g_has_analysis) {
        return;
    }
    RECT wave{24, 153, 666, 370};
    RECT spectrum{24, 419, 666, 650};
    HBRUSH panel = CreateSolidBrush(RGB(15, 25, 38));
    FillRect(dc, &wave, panel);
    FillRect(dc, &spectrum, panel);
    DeleteObject(panel);
    if (g_analysis.waveform.size() > 1) {
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(86, 230, 165));
        const auto previous = SelectObject(dc, pen);
        const int middle = (wave.top + wave.bottom) / 2;
        for (std::size_t i = 0; i < g_analysis.waveform.size(); ++i) {
            const int x = wave.left + static_cast<int>(
                i * (wave.right - wave.left) / (g_analysis.waveform.size() - 1));
            const int y = middle - static_cast<int>(
                g_analysis.waveform[i] * (wave.bottom - wave.top) * 0.42F);
            if (i == 0) {
                MoveToEx(dc, x, y, nullptr);
            } else {
                LineTo(dc, x, y);
            }
        }
        SelectObject(dc, previous);
        DeleteObject(pen);
    }

    const std::size_t frames = g_analysis.spectrogram_db.size();
    const std::size_t bins = frames == 0 ? 0 : g_analysis.spectrogram_db.front().size();
    for (std::size_t frame = 0; frame < frames; ++frame) {
        for (std::size_t bin = 0; bin < bins; ++bin) {
            const double level = std::clamp(
                (static_cast<double>(g_analysis.spectrogram_db[frame][bin]) + 75.0) / 75.0,
                0.0, 1.0);
            const auto x0 = spectrum.left + static_cast<int>(
                frame * (spectrum.right - spectrum.left) / frames);
            const auto x1 = spectrum.left + static_cast<int>(
                (frame + 1) * (spectrum.right - spectrum.left) / frames) + 1;
            const auto y0 = spectrum.bottom - static_cast<int>(
                (bin + 1) * (spectrum.bottom - spectrum.top) / bins);
            const auto y1 = spectrum.bottom - static_cast<int>(
                bin * (spectrum.bottom - spectrum.top) / bins) + 1;
            const int red = static_cast<int>(18 + level * 95);
            const int green = static_cast<int>(28 + level * 190);
            const int blue = static_cast<int>(47 + level * 150);
            HBRUSH brush = CreateSolidBrush(RGB(red, green, blue));
            RECT cell{x0, y0, std::max(x0 + 1, x1), std::max(y0 + 1, y1)};
            FillRect(dc, &cell, brush);
            DeleteObject(brush);
        }
    }
    draw_text(dc, 700, 130, L"SIGNAL CHARACTERISTICS", RGB(86, 230, 165), 13, L"Consolas");
    draw_text(dc, 700, 660, L"SMALL-SAMPLE MODEL · NOT FORENSIC PROOF",
              RGB(235, 184, 97), 11, L"Consolas");
    const auto evaluation = forensic::format_evaluation(g_evaluation);
    std::istringstream lines(evaluation);
    std::string line;
    int y = 690;
    while (std::getline(lines, line)) {
        draw_text(dc, 700, y, widen(line), RGB(177, 190, 205), 11, L"Consolas");
        y += 18;
    }
}

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_CREATE: {
        g_window = window;
        g_picker = create_control(WS_EX_CLIENTEDGE, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST | WS_TABSTOP,
            24, 89, 415, 300, kPickerId);
        HWND analyze = create_control(0, L"BUTTON", L"ANALYZE SAMPLE",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
            452, 89, 140, 30, kAnalyzeId);
        HWND open = create_control(0, L"BUTTON", L"OPEN AUDIO...",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
            602, 89, 115, 30, kOpenId);
        HWND play = create_control(0, L"BUTTON", L"PLAY",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
            727, 89, 75, 30, kPlayId);
        g_status = create_control(0, L"STATIC", L"Loading bundled offline dataset...",
            WS_CHILD | WS_VISIBLE, 24, 122, 760, 22, 0);
        g_prediction = create_control(0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE, 700, 690, 470, 34, 0);
        g_features = create_control(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY
                | ES_AUTOVSCROLL | ES_NOHIDESEL,
            700, 153, 470, 490, kFeaturesId);
        for (HWND control : {g_picker, analyze, open, play, g_status, g_prediction, g_features}) {
            set_font(control);
        }
        try {
            g_resource_dir = executable_directory();
            g_samples = forensic::load_dataset(g_resource_dir / "data" / "demo_dataset");
            auto demo = forensic::train_demo(g_samples);
            g_model = std::move(demo.model);
            g_evaluation = demo.evaluation;
            for (const auto& sample : g_samples) {
                const auto caption = sample.path.stem().wstring() + L"  ·  "
                    + (sample.label == forensic::Label::Real
                        ? L"REAL" : L"AI-GENERATED");
                SendMessageW(g_picker, CB_ADDSTRING, 0,
                    reinterpret_cast<LPARAM>(caption.c_str()));
            }
            SendMessageW(g_picker, CB_SETCURSEL, 0, 0);
            set_status("Offline-ready · 24 bundled samples · no network access");
            analyze_selected();
        } catch (const std::exception& error) {
            set_status(error.what());
        }
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case kAnalyzeId:
            analyze_selected();
            return 0;
        case kOpenId:
            open_audio_file();
            return 0;
        case kPlayId:
            if (!g_current.path.empty()) {
                const auto extension = g_current.path.extension().wstring();
                if (extension == L".mp3" || extension == L".MP3") {
                    mciSendStringW(L"close audio_forensics_mp3", nullptr, 0, g_window);
                    const std::wstring open_command = L"open \""
                        + g_current.path.wstring() + L"\" type mpegvideo alias audio_forensics_mp3";
                    const bool opened = mciSendStringW(open_command.c_str(), nullptr, 0, g_window) == 0;
                    const bool playing = opened && mciSendStringW(
                        L"play audio_forensics_mp3", nullptr, 0, g_window) == 0;
                    if (playing) {
                        set_status("Playing selected recording...");
                    } else {
                        mciSendStringW(L"close audio_forensics_mp3", nullptr, 0, g_window);
                        set_status("Playback could not start. Check the Windows output device/volume.");
                    }
                } else if (PlaySoundW(g_current.path.c_str(), nullptr, SND_FILENAME | SND_ASYNC)) {
                    set_status("Playing selected recording...");
                } else {
                    set_status("Playback could not start. Check the Windows output device/volume.");
                }
            } else {
                set_status("Select or open an audio file before playback.");
            }
            return 0;
        default:
            break;
        }
        break;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        paint_charts(dc);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_DESTROY:
        PlaySoundW(nullptr, nullptr, 0);
        mciSendStringW(L"close audio_forensics_mp3", nullptr, 0, g_window);
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
    try {
        const wchar_t class_name[] = L"AudioForensicsNativeWindow";
        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.style = CS_HREDRAW | CS_VREDRAW;
        window_class.lpfnWndProc = window_proc;
        window_class.hInstance = instance;
        window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        window_class.lpszClassName = class_name;
        if (!RegisterClassExW(&window_class)) {
            return 1;
        }
        g_window = CreateWindowExW(
            0, class_name, L"AUDIO//FORENSICS — Offline C++ DSP Analyzer",
            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1210, 880,
            nullptr, nullptr, instance, nullptr);
        if (g_window == nullptr) {
            return 1;
        }
        ShowWindow(g_window, show_command);
        UpdateWindow(g_window);
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        return static_cast<int>(message.wParam);
    } catch (...) {
        MessageBoxW(nullptr, L"Audio Forensics could not start.",
            L"Audio Forensics", MB_ICONERROR | MB_OK);
        return 1;
    }
}
