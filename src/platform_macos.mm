#import <Cocoa/Cocoa.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#import <mach-o/dyld.h>

#include "audio_core.hpp"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {

std::vector<forensic::Audio> g_samples;
forensic::RandomForest g_model;
forensic::Evaluation g_evaluation;
forensic::Audio g_current;
forensic::Analysis g_analysis;
bool g_has_analysis = false;
std::filesystem::path g_resource_dir;

NSString* ns_string(const std::string& value) {
    return [NSString stringWithUTF8String:value.c_str()];
}

std::filesystem::path executable_directory() {
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buffer(size);
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
        throw std::runtime_error("Could not locate the application executable.");
    }
    return std::filesystem::canonical(buffer.data()).parent_path();
}

NSButton* make_button(NSString* title, NSRect frame, id target, SEL action) {
    auto* button = [[NSButton alloc] initWithFrame:frame];
    button.title = title;
    button.bezelStyle = NSBezelStyleRounded;
    button.target = target;
    button.action = action;
    return button;
}

NSTextField* make_label(NSString* text, NSRect frame, CGFloat size, NSColor* color) {
    auto* label = [[NSTextField alloc] initWithFrame:frame];
    label.stringValue = text;
    label.font = [NSFont systemFontOfSize:size weight:NSFontWeightMedium];
    label.textColor = color;
    label.bezeled = NO;
    label.drawsBackground = NO;
    label.editable = NO;
    label.selectable = NO;
    return label;
}

NSColor* color_for_db(float db) {
    const double level = std::clamp((static_cast<double>(db) + 75.0) / 75.0, 0.0, 1.0);
    if (level < 0.5) {
        return [NSColor colorWithRed:0.05 + level * 0.1
                               green:0.11 + level * 0.55
                                blue:0.18 + level * 0.55 alpha:1.0];
    }
    return [NSColor colorWithRed:0.1 + (level - 0.5) * 1.2
                           green:0.38 + (level - 0.5) * 1.0
                            blue:0.45 - (level - 0.5) * 0.75 alpha:1.0];
}

}  // namespace

@interface ForensicChartView : NSView
@end

@interface ForensicContentView : NSView
@end

@implementation ForensicContentView

- (BOOL)isFlipped {
    return YES;
}

@end

@implementation ForensicChartView

- (BOOL)isFlipped {
    return YES;
}

- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    const NSRect bounds = self.bounds;
    [[NSColor colorWithRed:0.055 green:0.082 blue:0.12 alpha:1.0] setFill];
    NSRectFill(bounds);
    if (!g_has_analysis) {
        return;
    }
    const CGFloat padding = 24.0;
    const CGFloat chartWidth = bounds.size.width - 2.0 * padding;
    const CGFloat chartHeight = (bounds.size.height - 3.0 * padding) / 2.0;
    const NSRect waveRect = NSMakeRect(padding, padding, chartWidth, chartHeight);
    const NSRect specRect = NSMakeRect(
        padding, 2.0 * padding + chartHeight, chartWidth, chartHeight);
    [[NSColor colorWithRed:0.07 green:0.105 blue:0.15 alpha:1.0] setFill];
    NSRectFill(waveRect);
    NSRectFill(specRect);

    NSDictionary* titleAttrs = @{
        NSFontAttributeName: [NSFont monospacedSystemFontOfSize:12 weight:NSFontWeightMedium],
        NSForegroundColorAttributeName: [NSColor colorWithRed:0.36 green:0.9 blue:0.65 alpha:1.0]
    };
    [@"01 / WAVEFORM" drawAtPoint:NSMakePoint(padding, 8.0) withAttributes:titleAttrs];
    [@"02 / STFT SPECTROGRAM" drawAtPoint:NSMakePoint(
        padding, specRect.origin.y - 18.0) withAttributes:titleAttrs];

    if (g_analysis.waveform.size() > 1) {
        NSBezierPath* path = [NSBezierPath bezierPath];
        path.lineWidth = 1.0;
        const CGFloat middle = waveRect.origin.y + waveRect.size.height / 2.0;
        for (std::size_t i = 0; i < g_analysis.waveform.size(); ++i) {
            const CGFloat x = waveRect.origin.x
                + static_cast<CGFloat>(i) * waveRect.size.width
                    / (g_analysis.waveform.size() - 1);
            const CGFloat y = middle - g_analysis.waveform[i] * waveRect.size.height * 0.42;
            if (i == 0) {
                [path moveToPoint:NSMakePoint(x, y)];
            } else {
                [path lineToPoint:NSMakePoint(x, y)];
            }
        }
        [[NSColor colorWithRed:0.36 green:0.9 blue:0.65 alpha:1.0] setStroke];
        [path stroke];
    }

    if (g_analysis.spectrogram_db.empty()) {
        return;
    }
    const std::size_t frameCount = g_analysis.spectrogram_db.size();
    const std::size_t binCount = g_analysis.spectrogram_db.front().size();
    const CGFloat frameWidth = specRect.size.width / frameCount;
    const CGFloat binHeight = specRect.size.height / binCount;
    for (std::size_t frame = 0; frame < frameCount; ++frame) {
        for (std::size_t bin = 0; bin < binCount; ++bin) {
            [color_for_db(g_analysis.spectrogram_db[frame][bin]) setFill];
            NSRectFill(NSMakeRect(
                specRect.origin.x + frame * frameWidth,
                specRect.origin.y + specRect.size.height - (bin + 1) * binHeight,
                std::max<CGFloat>(1.0, frameWidth + 0.5),
                std::max<CGFloat>(1.0, binHeight + 0.5)));
        }
    }
}

@end

@interface ForensicController : NSObject <NSApplicationDelegate, NSComboBoxDataSource, NSSoundDelegate>
@property(nonatomic, strong) NSWindow* window;
@property(nonatomic, strong) NSComboBox* samplePicker;
@property(nonatomic, strong) NSTextField* predictionLabel;
@property(nonatomic, strong) NSTextField* statusLabel;
@property(nonatomic, strong) NSTextView* evaluationText;
@property(nonatomic, strong) NSTextView* detailsText;
@property(nonatomic, strong) ForensicChartView* chartView;
@property(nonatomic, strong) NSScrollView* detailsScroll;
@property(nonatomic, strong) NSSound* currentSound;
@end

@implementation ForensicController

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    (void)notification;
    self.window = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(0, 0, 1160, 900)
                  styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                             | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable)
                    backing:NSBackingStoreBuffered defer:NO];
    self.window.title = @"AUDIO//FORENSICS — Offline DSP Analyzer";
    self.window.minSize = NSMakeSize(960, 720);
    self.window.contentView = [[ForensicContentView alloc]
        initWithFrame:NSMakeRect(0, 0, 1160, 900)];
    [self.window center];
    NSView* content = self.window.contentView;
    content.wantsLayer = YES;
    content.layer.backgroundColor = [NSColor colorWithRed:0.04 green:0.06 blue:0.09 alpha:1].CGColor;

    [content addSubview:make_label(
        @"AUDIO//FORENSICS", NSMakeRect(28, 18, 420, 48), 30,
        [NSColor colorWithRed:0.9 green:0.93 blue:0.97 alpha:1])];
    [content addSubview:make_label(
        @"DISCRETE-TIME SIGNAL ANALYSIS  ·  OFFLINE DEMONSTRATION",
        NSMakeRect(30, 62, 550, 25), 11,
        [NSColor colorWithRed:0.36 green:0.9 blue:0.65 alpha:1])];

    self.samplePicker = [[NSComboBox alloc] initWithFrame:NSMakeRect(28, 102, 325, 30)];
    self.samplePicker.usesDataSource = YES;
    self.samplePicker.dataSource = self;
    self.samplePicker.completes = YES;
    [content addSubview:self.samplePicker];
    [content addSubview:make_button(@"ANALYZE SAMPLE", NSMakeRect(365, 102, 145, 30),
                                    self, @selector(analyzeSelectedSample:))];
    [content addSubview:make_button(@"OPEN AUDIO…", NSMakeRect(520, 102, 120, 30),
                                    self, @selector(openAudio:))];
    [content addSubview:make_button(@"PLAY", NSMakeRect(650, 102, 80, 30),
                                    self, @selector(playAudio:))];

    self.statusLabel = make_label(
        @"Preparing bundled, offline sample data…",
        NSMakeRect(28, 140, 800, 26), 12,
        [NSColor colorWithRed:0.57 green:0.64 blue:0.72 alpha:1]);
    [content addSubview:self.statusLabel];
    self.chartView = [[ForensicChartView alloc] initWithFrame:NSMakeRect(28, 178, 650, 610)];
    self.chartView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [content addSubview:self.chartView];

    [content addSubview:make_label(
        @"SIGNAL CHARACTERISTICS", NSMakeRect(704, 178, 420, 25), 13,
        [NSColor colorWithRed:0.36 green:0.9 blue:0.65 alpha:1])];
    self.detailsText = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, 420, 540)];
    self.detailsText.editable = NO;
    self.detailsText.selectable = YES;
    self.detailsText.font = [NSFont monospacedSystemFontOfSize:12 weight:NSFontWeightRegular];
    self.detailsText.textColor = [NSColor colorWithRed:0.85 green:0.88 blue:0.92 alpha:1];
    self.detailsText.backgroundColor = [NSColor colorWithRed:0.055 green:0.082 blue:0.12 alpha:1];
    self.detailsScroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(704, 210, 420, 445)];
    self.detailsScroll.hasVerticalScroller = YES;
    self.detailsScroll.documentView = self.detailsText;
    self.detailsScroll.autoresizingMask = NSViewHeightSizable;
    [content addSubview:self.detailsScroll];

    [content addSubview:make_label(
        @"SMALL-SAMPLE MODEL · NOT FORENSIC PROOF",
        NSMakeRect(704, 666, 420, 26), 11,
        [NSColor colorWithRed:0.92 green:0.72 blue:0.38 alpha:1])];
    self.predictionLabel = make_label(
        @"", NSMakeRect(704, 690, 420, 46), 25,
        [NSColor colorWithRed:0.36 green:0.9 blue:0.65 alpha:1]);
    [content addSubview:self.predictionLabel];
    self.evaluationText = [[NSTextView alloc] initWithFrame:NSMakeRect(704, 744, 420, 112)];
    self.evaluationText.editable = NO;
    self.evaluationText.selectable = YES;
    self.evaluationText.font = [NSFont monospacedSystemFontOfSize:10 weight:NSFontWeightRegular];
    self.evaluationText.textColor = [NSColor colorWithRed:0.57 green:0.64 blue:0.72 alpha:1];
    self.evaluationText.backgroundColor =
        [NSColor colorWithRed:0.04 green:0.06 blue:0.09 alpha:1];
    self.evaluationText.string = @"Preparing demo evaluation…";
    self.evaluationText.autoresizingMask = NSViewMinYMargin;
    [content addSubview:self.evaluationText];
    NSTextField* attribution = make_label(
        @"Bundled CC BY 4.0 samples · See ATTRIBUTION.md",
        NSMakeRect(704, 866, 420, 22), 10,
        [NSColor colorWithRed:0.57 green:0.64 blue:0.72 alpha:1]);
    [content addSubview:attribution];

    [self.window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
    [self prepareDemo];
}

- (void)prepareDemo {
    try {
        g_resource_dir = executable_directory();
        const auto datasetRoot = g_resource_dir / "data" / "demo_dataset";
        g_samples = forensic::load_dataset(datasetRoot);
        auto demo = forensic::train_demo(g_samples);
        g_model = std::move(demo.model);
        g_evaluation = demo.evaluation;
        self.evaluationText.string = ns_string(forensic::format_evaluation(g_evaluation));
        [self.samplePicker reloadData];
        [self.samplePicker selectItemAtIndex:0];
        self.statusLabel.stringValue = [NSString stringWithFormat:
            @"Offline-ready · %lu bundled samples · no network access",
            static_cast<unsigned long>(g_samples.size())];
        [self analyzeSelectedSample:nil];
    } catch (const std::exception& exception) {
        self.statusLabel.stringValue = ns_string(exception.what());
    }
}

- (NSInteger)numberOfItemsInComboBox:(NSComboBox*)comboBox {
    (void)comboBox;
    return static_cast<NSInteger>(g_samples.size());
}

- (id)comboBox:(NSComboBox*)comboBox objectValueForItemAtIndex:(NSInteger)index {
    (void)comboBox;
    if (index < 0 || static_cast<std::size_t>(index) >= g_samples.size()) {
        return @"";
    }
    const auto& sample = g_samples[static_cast<std::size_t>(index)];
    const auto rowName = sample.path.stem().string();
    return [NSString stringWithFormat:@"%@  ·  %s  ·  %s",
        ns_string(rowName), forensic::label_text(sample.label).c_str(),
        sample.label == forensic::Label::Real ? "public real sample" : "public synthetic sample"];
}

- (void)analyzeAudio:(forensic::Audio)audio {
    try {
        g_current = std::move(audio);
        g_analysis = forensic::analyze(g_current);
        g_has_analysis = true;
        const auto prediction = g_model.predict(g_analysis.features);
        self.predictionLabel.stringValue = [NSString stringWithFormat:@"%@  ·  demo score %.1f%%",
            ns_string(forensic::label_text(prediction.label)),
            prediction.score * 100.0];
        self.predictionLabel.textColor = prediction.label == forensic::Label::Real
            ? [NSColor colorWithRed:0.36 green:0.9 blue:0.65 alpha:1]
            : [NSColor colorWithRed:1.0 green:0.37 blue:0.48 alpha:1];
        const auto meta = [NSString stringWithFormat:
            @"%@  ·  %.2f s  ·  source %d Hz → %d Hz  ·  %d channel(s)\n\n",
            ns_string(g_current.path.filename().string()), g_analysis.duration_seconds,
            g_current.source_sample_rate, g_current.sample_rate, g_current.channels];
        self.detailsText.string = [meta stringByAppendingString:
            ns_string(forensic::format_features(g_analysis))];
        [self.chartView setNeedsDisplay:YES];
    } catch (const std::exception& exception) {
        self.statusLabel.stringValue = ns_string(exception.what());
    }
}

- (void)analyzeSelectedSample:(id)sender {
    (void)sender;
    const NSInteger index = self.samplePicker.indexOfSelectedItem;
    if (index < 0 || static_cast<std::size_t>(index) >= g_samples.size()) {
        self.statusLabel.stringValue = @"Choose a bundled sample or open a WAV or MP3 file.";
        return;
    }
    try {
        auto sample = forensic::load_audio(g_samples[static_cast<std::size_t>(index)].path);
        sample.label = g_samples[static_cast<std::size_t>(index)].label;
        [self analyzeAudio:std::move(sample)];
    } catch (const std::exception& exception) {
        self.statusLabel.stringValue = ns_string(exception.what());
    }
}

- (void)openAudio:(id)sender {
    (void)sender;
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.allowedContentTypes = @[[UTType typeWithFilenameExtension:@"wav"],
        [UTType typeWithFilenameExtension:@"mp3"]];
    panel.allowsMultipleSelection = NO;
    if ([panel runModal] != NSModalResponseOK || panel.URL == nil) {
        return;
    }
    try {
        auto audio = forensic::load_audio(panel.URL.fileSystemRepresentation);
        [self analyzeAudio:std::move(audio)];
    } catch (const std::exception& exception) {
        self.statusLabel.stringValue = ns_string(exception.what());
    }
}

- (void)playAudio:(id)sender {
    (void)sender;
    const NSInteger selectedIndex = self.samplePicker.indexOfSelectedItem;
    if (selectedIndex >= 0 && static_cast<std::size_t>(selectedIndex) < g_samples.size()
        && g_current.path != g_samples[static_cast<std::size_t>(selectedIndex)].path) {
        [self analyzeSelectedSample:nil];
    }
    if (g_current.path.empty()) {
        self.statusLabel.stringValue = @"Select or open an audio file before playback.";
        return;
    }
    [self.currentSound stop];
    self.currentSound = [[NSSound alloc] initWithContentsOfFile:
        ns_string(g_current.path.string()) byReference:NO];
    self.currentSound.delegate = self;
    if (self.currentSound == nil || ![self.currentSound play]) {
        self.statusLabel.stringValue = @"Playback could not start. Check the macOS output device/volume.";
        self.currentSound = nil;
        return;
    }
    self.statusLabel.stringValue = @"Playing selected recording…";
}

- (void)sound:(NSSound*)sound didFinishPlaying:(BOOL)successfully {
    if (sound != self.currentSound) {
        return;
    }
    self.statusLabel.stringValue = successfully
        ? @"Playback finished."
        : @"Playback stopped unexpectedly. Check the macOS output device.";
    self.currentSound = nil;
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
    (void)sender;
    return YES;
}

@end

int main() {
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        ForensicController* controller = [[ForensicController alloc] init];
        NSApp.delegate = controller;
        [NSApp run];
        (void)g_evaluation;
    }
    return 0;
}
