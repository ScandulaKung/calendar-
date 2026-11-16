#include "log.hpp"
#include "settings.hpp"
#include "theme.hpp"

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <algorithm>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

namespace {

Gdiplus::Image* backgroundImage = nullptr;
Gdiplus::Bitmap* videoFrame = nullptr;
Gdiplus::Bitmap* fittedImage = nullptr;
IMFSourceReader* videoReader = nullptr;
std::wstring loadedPath;
std::wstring shuffleAnchor;
std::wstring shufflePlaying;
ULONGLONG shuffleNext = 0;
bool shuffleWasOn = false;
int frameWidth = 0;
int frameHeight = 0;
int fittedWidth = 0;
int fittedHeight = 0;
int fittedFrame = -1;
int fittedCrop = 1;
std::vector<int> frameDelays;
UINT frameCount = 1;
UINT frameIndex = 0;
ULONGLONG nextTick = 0;
bool motion = false;
bool mediaStarted = false;

void releaseVideo() {
  delete videoFrame;
  videoFrame = nullptr;
  if (videoReader) {
    videoReader->Release();
    videoReader = nullptr;
  }
}

void releaseFitted() {
  delete fittedImage;
  fittedImage = nullptr;
  fittedWidth = 0;
  fittedHeight = 0;
  fittedFrame = -1;
  fittedCrop = 1;
}

float placedX(float frame, float drawn) {
  if (drawn <= frame) return (frame - drawn) / 2.f;
  const int crop = calendarSettings().backgroundCrop;
  if (crop == 0) return 0.f;
  if (crop == 2) return frame - drawn;
  return (frame - drawn) / 2.f;
}

bool imageExtension(const std::wstring& ext) {
  return ext == L"jpg" || ext == L"jpeg" || ext == L"png" || ext == L"bmp" || ext == L"gif";
}

std::wstring extensionOf(const std::wstring& path) {
  const std::wstring::size_type dot = path.find_last_of(L'.');
  if (dot == std::wstring::npos) return L"";
  std::wstring ext = path.substr(dot + 1);
  for (wchar_t& letter : ext) {
    if (letter >= L'A' && letter <= L'Z') letter = static_cast<wchar_t>(letter - L'A' + L'a');
  }
  return ext;
}

bool videoExtension(const std::wstring& ext) {
  return ext == L"mp4" || ext == L"m4v" || ext == L"wmv" || ext == L"avi" || ext == L"mkv" || ext == L"mov" || ext == L"webm";
}

void prepareMedia() {
  if (mediaStarted) return;
  mediaStarted = true;
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
}

bool copyVideoSample(IMFSample* sample) {
  IMFMediaType* type = nullptr;
  if (FAILED(videoReader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &type)) || !type) return false;
  UINT32 width = 0;
  UINT32 height = 0;
  if (FAILED(MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &width, &height)) || width == 0 || height == 0) {
    type->Release();
    return false;
  }
  UINT32 storedStride = 0;
  const bool haveStride = SUCCEEDED(type->GetUINT32(MF_MT_DEFAULT_STRIDE, &storedStride));
  type->Release();
  IMFMediaBuffer* buffer = nullptr;
  if (FAILED(sample->ConvertToContiguousBuffer(&buffer)) || !buffer) return false;
  BYTE* data = nullptr;
  DWORD maxLength = 0;
  DWORD currentLength = 0;
  if (FAILED(buffer->Lock(&data, &maxLength, &currentLength)) || !data) {
    buffer->Release();
    return false;
  }
  const long stride = haveStride ? static_cast<long>(static_cast<LONG>(storedStride)) : static_cast<long>(width * 4);
  const int absStride = std::abs(static_cast<int>(stride));
  if (absStride < static_cast<int>(width * 4) || static_cast<std::size_t>(absStride) * height > currentLength) {
    buffer->Unlock();
    buffer->Release();
    return false;
  }
  const BYTE* top = stride >= 0 ? data : data + static_cast<std::size_t>(height - 1) * static_cast<std::size_t>(absStride);
  const int rowStep = stride >= 0 ? absStride : -absStride;
  auto* bitmap = new Gdiplus::Bitmap(static_cast<INT>(width), static_cast<INT>(height), PixelFormat32bppRGB);
  Gdiplus::BitmapData locked{};
  Gdiplus::Rect rect(0, 0, static_cast<INT>(width), static_cast<INT>(height));
  if (bitmap->LockBits(&rect, Gdiplus::ImageLockModeWrite, PixelFormat32bppRGB, &locked) != Gdiplus::Ok) {
    buffer->Unlock();
    buffer->Release();
    delete bitmap;
    return false;
  }
  for (UINT32 y = 0; y < height; y += 1) {
    const BYTE* source = top + static_cast<std::ptrdiff_t>(y) * rowStep;
    auto* dest = static_cast<BYTE*>(locked.Scan0) + static_cast<std::size_t>(y) * static_cast<std::size_t>(locked.Stride);
    const std::size_t bytes = static_cast<std::size_t>(width) * 4;
    std::copy(source, source + bytes, dest);
  }
  bitmap->UnlockBits(&locked);
  buffer->Unlock();
  buffer->Release();
  delete videoFrame;
  videoFrame = bitmap;
  return true;
}

bool pullVideoFrame(bool restarted) {
  if (!videoReader) return false;
  DWORD flags = 0;
  LONGLONG timestamp = 0;
  IMFSample* sample = nullptr;
  const HRESULT result = videoReader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, &flags, &timestamp, &sample);
  if (FAILED(result)) return false;
  if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0 || !sample) {
    if (sample) sample->Release();
    if (restarted) return false;
    PROPVARIANT position{};
    position.vt = VT_I8;
    position.hVal.QuadPart = 0;
    videoReader->SetCurrentPosition(GUID_NULL, position);
    return pullVideoFrame(true);
  }
  const bool copied = copyVideoSample(sample);
  LONGLONG duration = 0;
  int delay = 40;
  if (SUCCEEDED(sample->GetSampleDuration(&duration)) && duration > 0) delay = std::max(15, static_cast<int>(duration / 10000));
  sample->Release();
  nextTick = GetTickCount64() + static_cast<ULONGLONG>(delay);
  return copied;
}

void fitVideoSize(UINT32 sourceWidth, UINT32 sourceHeight, UINT32& outWidth, UINT32& outHeight) {
  outWidth = sourceWidth;
  outHeight = sourceHeight;
  constexpr UINT32 longest = 960;
  if (sourceWidth == 0 || sourceHeight == 0 || std::max(sourceWidth, sourceHeight) <= longest) return;
  if (sourceWidth >= sourceHeight) {
    outWidth = longest;
    outHeight = std::max<UINT32>(2, (sourceHeight * longest / sourceWidth) & ~1u);
  } else {
    outHeight = longest;
    outWidth = std::max<UINT32>(2, (sourceWidth * longest / sourceHeight) & ~1u);
  }
}

bool openVideo(const std::wstring& path) {
  prepareMedia();
  IMFAttributes* attributes = nullptr;
  if (FAILED(MFCreateAttributes(&attributes, 1))) return false;
  attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
  IMFSourceReader* reader = nullptr;
  const HRESULT opened = MFCreateSourceReaderFromURL(path.c_str(), attributes, &reader);
  attributes->Release();
  if (FAILED(opened) || !reader) return false;
  UINT32 sourceWidth = 0;
  UINT32 sourceHeight = 0;
  IMFMediaType* native = nullptr;
  if (SUCCEEDED(reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &native)) && native) {
    MFGetAttributeSize(native, MF_MT_FRAME_SIZE, &sourceWidth, &sourceHeight);
    native->Release();
  }
  UINT32 outWidth = 0;
  UINT32 outHeight = 0;
  fitVideoSize(sourceWidth, sourceHeight, outWidth, outHeight);
  IMFMediaType* type = nullptr;
  if (FAILED(MFCreateMediaType(&type)) || !type) {
    reader->Release();
    return false;
  }
  type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
  type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
  if (outWidth > 0 && outHeight > 0) MFSetAttributeSize(type, MF_MT_FRAME_SIZE, outWidth, outHeight);
  HRESULT selected = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, type);
  if (FAILED(selected)) {
    type->DeleteItem(MF_MT_FRAME_SIZE);
    selected = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, type);
  }
  type->Release();
  if (FAILED(selected)) {
    reader->Release();
    return false;
  }
  videoReader = reader;
  return pullVideoFrame(false);
}

void readGifDelays(Gdiplus::Image* image, UINT frames) {
  frameDelays.assign(frames, 100);
  const UINT size = image->GetPropertyItemSize(PropertyTagFrameDelay);
  if (size == 0) return;
  std::vector<unsigned char> bytes(size);
  auto* item = reinterpret_cast<Gdiplus::PropertyItem*>(bytes.data());
  if (image->GetPropertyItem(PropertyTagFrameDelay, size, item) != Gdiplus::Ok || !item->value) return;
  const UINT count = std::min(frames, static_cast<UINT>(item->length / sizeof(long)));
  const auto* values = static_cast<const long*>(item->value);
  for (UINT index = 0; index < count; index += 1) {
    const int hundredths = values[index] <= 1 ? 10 : static_cast<int>(values[index]);
    frameDelays[index] = std::max(20, hundredths * 10);
  }
}

void ensureImage(const std::wstring& path) {
  if (path == loadedPath) return;
  delete backgroundImage;
  backgroundImage = nullptr;
  releaseFitted();
  frameDelays.clear();
  frameCount = 1;
  frameIndex = 0;
  releaseVideo();
  motion = false;
  loadedPath = path;
  if (path.empty()) return;
  if (videoExtension(extensionOf(path))) {
    if (backgroundVideoTooLarge(path)) {
      writeErrorLog(L"背景视频", path + L" 超过 20 MB", true);
      return;
    }
    motion = openVideo(path);
    if (!motion) logMissingResource(path);
    return;
  }
  auto* image = new Gdiplus::Image(path.c_str());
  if (image->GetLastStatus() != Gdiplus::Ok) {
    logMissingResource(path);
    delete image;
    return;
  }
  UINT frames = image->GetFrameCount(&Gdiplus::FrameDimensionTime);
  if (frames > 1) {
    frameCount = frames;
    readGifDelays(image, frames);
    image->SelectActiveFrame(&Gdiplus::FrameDimensionTime, 0);
    nextTick = GetTickCount64() + static_cast<ULONGLONG>(frameDelays.empty() ? 100 : frameDelays[0]);
    motion = true;
  }
  backgroundImage = image;
}

Gdiplus::Image* fittedForFrame(Gdiplus::Image* source) {
  if (!source || videoFrame || frameWidth <= 0 || frameHeight <= 0) return source;
  const UINT sourceWidth = source->GetWidth();
  const UINT sourceHeight = source->GetHeight();
  if (sourceWidth == 0 || sourceHeight == 0) return source;
  const int crop = calendarSettings().backgroundCrop;
  if (fittedImage && fittedWidth == frameWidth && fittedHeight == frameHeight && fittedFrame == static_cast<int>(frameIndex) && fittedCrop == crop) return fittedImage;
  const float scale = std::max(frameWidth / static_cast<float>(sourceWidth), frameHeight / static_cast<float>(sourceHeight));
  const float drawnWidth = sourceWidth * scale;
  const float drawnHeight = sourceHeight * scale;
  auto* bitmap = new Gdiplus::Bitmap(frameWidth, frameHeight, PixelFormat32bppPARGB);
  Gdiplus::Graphics canvas(bitmap);
  canvas.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
  canvas.DrawImage(source, placedX(static_cast<float>(frameWidth), drawnWidth), (frameHeight - drawnHeight) / 2.f, drawnWidth, drawnHeight);
  releaseFitted();
  fittedImage = bitmap;
  fittedWidth = frameWidth;
  fittedHeight = frameHeight;
  fittedFrame = static_cast<int>(frameIndex);
  fittedCrop = crop;
  return fittedImage;
}

std::wstring fileNameOf(const std::wstring& path) {
  const std::wstring::size_type slash = path.find_last_of(L"\\/");
  return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

std::vector<std::wstring> folderMedia(const std::wstring& anchor) {
  const std::filesystem::path folder = std::filesystem::path(anchor).parent_path();
  std::vector<std::wstring> files;
  std::error_code error;
  for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(folder, error)) {
    if (error || !entry.is_regular_file(error)) continue;
    const std::wstring path = entry.path().wstring();
    const std::wstring ext = extensionOf(path);
    if (!imageExtension(ext) && !videoExtension(ext)) continue;
    if (videoExtension(ext) && backgroundVideoTooLarge(path)) continue;
    files.push_back(path);
  }
  std::sort(files.begin(), files.end(), [](const std::wstring& left, const std::wstring& right) {
    return _wcsicmp(fileNameOf(left).c_str(), fileNameOf(right).c_str()) < 0;
  });
  return files;
}

std::wstring fileInOrder(const std::wstring& anchor, long long start, int minutes) {
  const std::vector<std::wstring> files = folderMedia(anchor);
  if (files.empty()) return anchor;
  std::size_t startIndex = 0;
  for (std::size_t index = 0; index < files.size(); index += 1) {
    if (_wcsicmp(files[index].c_str(), anchor.c_str()) == 0) {
      startIndex = index;
      break;
    }
  }
  long long elapsed = 0;
  if (start > 0) {
    const long long now = static_cast<long long>(std::time(nullptr));
    if (now > start) elapsed = now - start;
  }
  const long long step = static_cast<long long>(backgroundIntervalMinutes(minutes)) * 60;
  const std::size_t hops = step > 0 ? static_cast<std::size_t>(elapsed / step) : 0;
  return files[(startIndex + hops) % files.size()];
}

std::wstring pickFolderFile(const std::wstring& anchor, const std::wstring& avoid) {
  const std::filesystem::path folder = std::filesystem::path(anchor).parent_path();
  std::vector<std::wstring> files;
  std::error_code error;
  for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(folder, error)) {
    if (error || !entry.is_regular_file(error)) continue;
    const std::wstring path = entry.path().wstring();
    const std::wstring ext = extensionOf(path);
    if (!imageExtension(ext) && !videoExtension(ext)) continue;
    if (videoExtension(ext) && backgroundVideoTooLarge(path)) continue;
    files.push_back(path);
  }
  if (files.empty()) return anchor;
  if (files.size() == 1) return files[0];
  static std::mt19937 generator{std::random_device{}()};
  std::uniform_int_distribution<std::size_t> distribution(0, files.size() - 1);
  for (int attempt = 0; attempt < 6; attempt += 1) {
    const std::wstring choice = files[distribution(generator)];
    if (choice != avoid) return choice;
  }
  return files[distribution(generator)];
}

void drawPlaced(Gdiplus::Graphics& graphics, Gdiplus::Image* image, int width, int height) {
  if (!image || image->GetWidth() == 0 || image->GetHeight() == 0) {
    const Palette palette = currentPalette();
    Gdiplus::SolidBrush base(Gdiplus::Color(255, palette.background[0], palette.background[1], palette.background[2]));
    graphics.FillRectangle(&base, 0, 0, width, height);
    return;
  }
  const float sourceWidth = static_cast<float>(image->GetWidth());
  const float sourceHeight = static_cast<float>(image->GetHeight());
  const float scale = std::max(width / sourceWidth, height / sourceHeight);
  const float drawnWidth = sourceWidth * scale;
  const float drawnHeight = sourceHeight * scale;
  graphics.SetClip(Gdiplus::Rect(0, 0, width, height));
  graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
  graphics.DrawImage(image, placedX(static_cast<float>(width), drawnWidth), (height - drawnHeight) / 2.f, drawnWidth, drawnHeight);
  graphics.ResetClip();
  Gdiplus::SolidBrush veil(Gdiplus::Color(148, 12, 18, 32));
  graphics.FillRectangle(&veil, 0, 0, width, height);
}

}

bool backgroundVideoTooLarge(const std::wstring& path) {
  if (!videoExtension(extensionOf(path))) return false;
  WIN32_FILE_ATTRIBUTE_DATA info{};
  if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info)) return false;
  const unsigned long long bytes = (static_cast<unsigned long long>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
  return bytes > 20ull * 1024ull * 1024ull;
}

void setBackgroundFrame(int width, int height) {
  if (width > 0 && height > 0) {
    frameWidth = width;
    frameHeight = height;
  }
}

bool pollBackgroundShuffle() {
  const CalendarSettings& settings = calendarSettings();
  const bool order = settings.appearance == 2 && settings.backgroundOrder && !settings.backgroundImage.empty();
  const bool active = settings.appearance == 2 && settings.backgroundShuffle && !order && !settings.backgroundImage.empty();
  const ULONGLONG interval = static_cast<ULONGLONG>(backgroundIntervalMinutes(settings.backgroundInterval)) * 60ull * 1000ull;
  if (order) {
    const std::wstring next = fileInOrder(settings.backgroundImage, settings.backgroundOrderStart, settings.backgroundInterval);
    const bool changed = next != shufflePlaying || shuffleWasOn;
    shuffleWasOn = false;
    shuffleAnchor = settings.backgroundImage;
    shufflePlaying = next.empty() ? settings.backgroundImage : next;
    shuffleNext = 0;
    return changed && shufflePlaying != loadedPath;
  }
  if (!active) {
    const bool changed = shuffleWasOn;
    shuffleWasOn = false;
    shuffleAnchor.clear();
    shufflePlaying.clear();
    shuffleNext = 0;
    return changed;
  }
  const long long step = static_cast<long long>(backgroundIntervalMinutes(settings.backgroundInterval)) * 60;
  long long slot = 0;
  if (settings.backgroundOrderStart > 0 && step > 0) {
    const long long nowUnix = static_cast<long long>(std::time(nullptr));
    long long elapsed = nowUnix - settings.backgroundOrderStart;
    if (elapsed < 0) elapsed = 0;
    slot = elapsed / step;
  }
  static long long shuffleSlot = -1;
  if (!shuffleWasOn || shuffleAnchor != settings.backgroundImage || slot != shuffleSlot) {
    const bool switched = shuffleWasOn && shuffleAnchor == settings.backgroundImage && slot != shuffleSlot && settings.backgroundOrderStart > 0;
    shuffleSlot = slot;
    shuffleAnchor = settings.backgroundImage;
    shufflePlaying = switched ? pickFolderFile(shuffleAnchor, shufflePlaying) : shuffleAnchor;
    if (shufflePlaying.empty()) shufflePlaying = shuffleAnchor;
    shuffleNext = GetTickCount64() + interval;
    shuffleWasOn = true;
    return shufflePlaying != loadedPath;
  }
  return false;
}

bool backgroundUsesMotion() { return motion; }

bool advanceBackground() {
  if (!motion) return false;
  const ULONGLONG now = GetTickCount64();
  if (now < nextTick) return false;
  if (videoReader) {
    const bool changed = pullVideoFrame(false);
    if (!changed) nextTick = now + 200;
    return changed;
  }
  if (!backgroundImage || frameCount < 2) return false;
  frameIndex = (frameIndex + 1) % frameCount;
  backgroundImage->SelectActiveFrame(&Gdiplus::FrameDimensionTime, frameIndex);
  const int delay = frameDelays.empty() ? 100 : frameDelays[frameIndex % frameDelays.size()];
  nextTick = now + static_cast<ULONGLONG>(std::max(20, delay));
  return true;
}

Palette currentPalette() {
  const bool day = calendarSettings().appearance == 1;
  if (day) {
    return Palette{
        {246, 248, 251},
        {32, 40, 52},
        {110, 120, 136},
        {255, 255, 255},
        {232, 236, 242},
        {214, 220, 228},
        {210, 64, 72},
        {168, 176, 186},
    };
  }
  return Palette{
      {22, 30, 46},
      {236, 240, 246},
      {154, 168, 188},
      {36, 48, 68},
      {16, 22, 36},
      {42, 54, 74},
      {255, 96, 104},
      {92, 106, 128},
  };
}

void drawThemedBackground(Gdiplus::Graphics& graphics, int width, int height) {
  pollBackgroundShuffle();
  const CalendarSettings& settings = calendarSettings();
  const bool rotating = settings.appearance == 2 && (settings.backgroundShuffle || settings.backgroundOrder) && !shufflePlaying.empty();
  const std::wstring path = rotating ? shufflePlaying : settings.backgroundImage;
  if (settings.appearance == 2) ensureImage(path);
  else ensureImage(L"");
  Gdiplus::Image* image = videoFrame ? static_cast<Gdiplus::Image*>(videoFrame) : backgroundImage;
  image = fittedForFrame(image);
  if (settings.appearance == 2 && image) {
    drawPlaced(graphics, image, width, height);
    return;
  }
  const Palette palette = currentPalette();
  Gdiplus::SolidBrush background(Gdiplus::Color(255, palette.background[0], palette.background[1], palette.background[2]));
  graphics.FillRectangle(&background, 0, 0, width, height);
}
