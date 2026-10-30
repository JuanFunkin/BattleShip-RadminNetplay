#include "music_override.h"
#include "../port_log.h"
#include "../app_paths.h"

#include <libultraship/libultraship.h>
#include <zip.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

extern "C" {
int port_mp3_decode(const unsigned char* data, int size, short** out, int* channels, int* hz);
int stb_vorbis_decode_memory(const unsigned char* mem, int len, int* channels, int* sample_rate, short** output);
}

namespace {

constexpr int kPlayers = 4;
constexpr int kOutRate = 32000; // synth output rate (decomp audio.c: "Output rate")

struct Source {
    std::string path;     // loose file, or archive path
    std::string member;   // member name inside the archive ("" = loose file)
    std::string ext;
    std::string name; // display name (file stem without _once)
    bool loop = true;
};

struct Track {
    std::vector<int16_t> pcm; // interleaved stereo @ kOutRate
    bool loop = true;
};

struct Stream {
    std::shared_ptr<Track> track;
    size_t pos = 0; // in frames
    float gain = 1.0f;
    bool active = false;
};

std::mutex gMu;
std::map<unsigned, Source> gIndex;      // numeric file names = default assignment per BGM id
std::vector<Source> gTracks;            // every music file found (free names allowed)
std::map<std::string, std::shared_ptr<Track>> gCache;
bool gScanned = false;
Stream gStreams[kPlayers];
float gLastVol[kPlayers] = {1.0f, 1.0f, 1.0f, 1.0f};

std::string Lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// "<name>.ext" or "<name>_once.ext". A purely numeric name (<= 4 digits) is also a default BGM id.
bool ParseName(const std::string& file, std::string* stem, bool* loop, std::string* ext) {
    std::string f = Lower(file);
    size_t dot = f.rfind('.');
    if (dot == std::string::npos) return false;
    *ext = f.substr(dot + 1);
    if (*ext != "mp3" && *ext != "ogg" && *ext != "wav") return false;
    *stem = file.substr(0, dot); // keep original case for display
    *loop = true;
    if (stem->size() > 5 && Lower(stem->substr(stem->size() - 5)) == "_once") {
        *loop = false;
        stem->resize(stem->size() - 5);
    }
    return !stem->empty();
}

bool NumericId(const std::string& stem, unsigned* id) {
    if (stem.empty() || stem.size() > 4) return false;
    for (char c : stem) if (!std::isdigit((unsigned char)c)) return false;
    *id = (unsigned)std::atoi(stem.c_str());
    return true;
}

void AddTrack(Source s) {
    unsigned id;
    if (NumericId(s.name, &id)) gIndex[id] = s;
    for (const auto& t : gTracks) if (t.name == s.name) return;
    gTracks.push_back(std::move(s));
}

void ScanDir(const std::filesystem::path& mods) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(mods, ec)) return;
    // loose files: mods/music/*
    for (const auto& e : fs::directory_iterator(mods / "music", ec)) {
        if (!e.is_regular_file(ec)) continue;
        std::string stem, ext; bool loop;
        if (ParseName(e.path().filename().string(), &stem, &loop, &ext)) {
            Source s; s.path = e.path().string(); s.ext = ext; s.loop = loop; s.name = stem; AddTrack(s);
        }
    }
    // archives: mods/*.o2r|zip with music/<id>.ext
    ec.clear();
    for (const auto& e : fs::directory_iterator(mods, ec)) {
        if (!e.is_regular_file(ec)) continue;
        std::string ext = Lower(e.path().extension().string());
        if (ext != ".o2r" && ext != ".zip") continue;
        int err = 0;
        zip_t* z = zip_open(e.path().string().c_str(), ZIP_RDONLY, &err);
        if (!z) continue;
        zip_int64_t n = zip_get_num_entries(z, 0);
        for (zip_int64_t i = 0; i < n; i++) {
            const char* nm = zip_get_name(z, (zip_uint64_t)i, 0);
            if (!nm) continue;
            std::string name = nm;
            for (auto& c : name) if (c == '\\') c = '/';
            size_t slash = name.rfind('/');
            std::string dir = slash == std::string::npos ? "" : Lower(name.substr(0, slash));
            if (dir != "music" && (dir.size() < 6 || dir.compare(dir.size() - 6, 6, "/music") != 0)) continue;
            std::string stem, ex; bool loop;
            if (ParseName(name.substr(slash + 1), &stem, &loop, &ex)) {
                Source s; s.path = e.path().string(); s.member = nm; s.ext = ex; s.loop = loop; s.name = stem; AddTrack(s);
            }
        }
        zip_close(z);
    }
}

void EnsureScanned() {
    if (gScanned) return;
    gScanned = true;
    try { ScanDir(Ship::Context::GetPathRelativeToAppDirectory("mods")); } catch (...) {}
#ifndef BATTLESHIP_UWP
    try { ScanDir(std::filesystem::path(ssb64::RealAppBundlePath()) / "mods"); } catch (...) {}
#endif
    port_log("SSB64 Music: %zu track(s) found, %zu default assignment(s)\n", gTracks.size(), gIndex.size());
}

bool ReadAll(const Source& s, std::vector<unsigned char>* out) {
    if (s.member.empty()) {
        std::ifstream f(s.path, std::ios::binary);
        if (!f) return false;
        out->assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        return !out->empty();
    }
    int err = 0;
    zip_t* z = zip_open(s.path.c_str(), ZIP_RDONLY, &err);
    if (!z) return false;
    bool ok = false;
    zip_stat_t st;
    if (zip_stat(z, s.member.c_str(), 0, &st) == 0) {
        zip_file_t* f = zip_fopen(z, s.member.c_str(), 0);
        if (f) {
            out->resize((size_t)st.size);
            ok = zip_fread(f, out->data(), st.size) == (zip_int64_t)st.size;
            zip_fclose(f);
        }
    }
    zip_close(z);
    return ok;
}

bool DecodeWav(const std::vector<unsigned char>& d, std::vector<int16_t>* pcm, int* ch, int* hz) {
    if (d.size() < 44 || std::memcmp(d.data(), "RIFF", 4) != 0 || std::memcmp(d.data() + 8, "WAVE", 4) != 0) return false;
    size_t p = 12; int fmtTag = 0, bits = 0; *ch = 0; *hz = 0;
    auto rd16 = [&](size_t o) { return (int)(d[o] | (d[o + 1] << 8)); };
    auto rd32 = [&](size_t o) { return (uint32_t)(d[o] | (d[o + 1] << 8) | (d[o + 2] << 16) | ((uint32_t)d[o + 3] << 24)); };
    while (p + 8 <= d.size()) {
        uint32_t sz = rd32(p + 4);
        if (std::memcmp(d.data() + p, "fmt ", 4) == 0 && p + 24 <= d.size()) {
            fmtTag = rd16(p + 8); *ch = rd16(p + 10); *hz = (int)rd32(p + 12); bits = rd16(p + 22);
        } else if (std::memcmp(d.data() + p, "data", 4) == 0) {
            size_t n = std::min<size_t>(sz, d.size() - p - 8);
            const unsigned char* b = d.data() + p + 8;
            if (*ch < 1 || *ch > 2) return false;
            if (fmtTag == 1 && bits == 16) {
                pcm->resize(n / 2);
                for (size_t i = 0; i < n / 2; i++) (*pcm)[i] = (int16_t)(b[i * 2] | (b[i * 2 + 1] << 8));
                return true;
            }
            if (fmtTag == 1 && bits == 8) {
                pcm->resize(n);
                for (size_t i = 0; i < n; i++) (*pcm)[i] = (int16_t)((b[i] - 128) << 8);
                return true;
            }
            return false;
        }
        p += 8 + sz + (sz & 1);
    }
    return false;
}

std::shared_ptr<Track> Decode(const Source& s) {
    std::vector<unsigned char> data;
    if (!ReadAll(s, &data)) return nullptr;
    std::vector<int16_t> raw; int ch = 0, hz = 0;
    if (s.ext == "mp3") {
        short* o = nullptr;
        int frames = port_mp3_decode(data.data(), (int)data.size(), &o, &ch, &hz);
        if (frames <= 0) return nullptr;
        raw.assign(o, o + (size_t)frames * ch);
        std::free(o);
    } else if (s.ext == "ogg") {
        short* o = nullptr;
        int frames = stb_vorbis_decode_memory(data.data(), (int)data.size(), &ch, &hz, &o);
        if (frames <= 0 || !o) return nullptr;
        raw.assign(o, o + (size_t)frames * ch);
        std::free(o);
    } else if (!DecodeWav(data, &raw, &ch, &hz)) {
        return nullptr;
    }
    if (ch < 1 || ch > 2 || hz <= 0) return nullptr;
    size_t inFrames = raw.size() / ch;
    size_t outFrames = (size_t)((double)inFrames * kOutRate / hz);
    auto t = std::make_shared<Track>();
    t->loop = s.loop;
    t->pcm.resize(outFrames * 2);
    const double step = (double)hz / kOutRate;
    for (size_t i = 0; i < outFrames; i++) {
        double sp = i * step;
        size_t a = (size_t)sp, b = (std::min)(a + 1, inFrames - 1);
        float f = (float)(sp - a);
        for (int c = 0; c < 2; c++) {
            int cc = ch == 1 ? 0 : c;
            float v = raw[a * ch + cc] * (1.0f - f) + raw[b * ch + cc] * f;
            t->pcm[i * 2 + c] = (int16_t)(std::max)(-32768.0f, (std::min)(32767.0f, v));
        }
    }
    return t;
}


// Resolve which track (if any) replaces BGM `bgm`: per-id selection from the Audio Editor menu
// (CVar gMusicOverride.Sel.<id>: "" default, "orig" original, "random", or a track name), else the
// numeric default file.
const Source* ResolveTrack(unsigned bgm) {
    char cv[48];
    std::snprintf(cv, sizeof(cv), "gMusicOverride.Sel.%u", bgm);
    std::string sel = CVarGetString(cv, "");
    if (sel == "orig") return nullptr;
    if (sel == "random") {
        if (gTracks.empty()) return nullptr;
        static unsigned sSeed = 12345u;
        sSeed = sSeed * 1664525u + 1013904223u + (unsigned)std::clock();
        return &gTracks[(sSeed >> 8) % gTracks.size()];
    }
    if (!sel.empty()) {
        for (const auto& t : gTracks) if (t.name == sel) return &t;
    }
    auto it = gIndex.find(bgm);
    return it == gIndex.end() ? nullptr : &it->second;
}

} // namespace

extern "C" int portMusicPlay(int player, unsigned int bgm) {
    if (player < 0 || player >= kPlayers) return 0;
    if (CVarGetInteger("gMusicOverride.Enabled", 1) == 0) { portMusicStop(player); return 0; }
    std::lock_guard<std::mutex> lk(gMu);
    EnsureScanned();
    const Source* src = ResolveTrack(bgm);
    if (!src) { gStreams[player].active = false; return 0; }
    const std::string ckey = src->path + "|" + src->member;
    std::shared_ptr<Track> t;
    auto c = gCache.find(ckey);
    if (c != gCache.end()) t = c->second;
    else {
        t = Decode(*src);
        if (!t) port_log("SSB64 Music: could not decode %s (%s)\n", src->name.c_str(), src->path.c_str());
        else {
            if (gCache.size() >= 6) gCache.erase(gCache.begin()); // keep decoded RAM bounded
            gCache[ckey] = t;
        }
    }
    if (!t) { gStreams[player].active = false; return 0; }
    gStreams[player].track = t;
    gStreams[player].pos = 0;
    gStreams[player].gain = gLastVol[player];
    gStreams[player].active = true;
    port_log("SSB64 Music: playing '%s' for bgm %u\n", src->name.c_str(), bgm);
    return 1;
}

extern "C" int portMusicTrackCount(void) {
    std::lock_guard<std::mutex> lk(gMu);
    EnsureScanned();
    return (int)gTracks.size();
}

extern "C" const char* portMusicTrackName(int i) {
    std::lock_guard<std::mutex> lk(gMu);
    return (i >= 0 && i < (int)gTracks.size()) ? gTracks[(size_t)i].name.c_str() : "";
}

extern "C" void portMusicRescan(void) {
    std::lock_guard<std::mutex> lk(gMu);
    gIndex.clear(); gTracks.clear(); gCache.clear(); gScanned = false;
    EnsureScanned();
}

extern "C" void portMusicStop(int player) {
    if (player < 0 || player >= kPlayers) return;
    std::lock_guard<std::mutex> lk(gMu);
    gStreams[player].active = false;
}

extern "C" void portMusicSetVolume(int player, unsigned int vol) {
    if (player < 0 || player >= kPlayers) return;
    float g = (std::min)(1.0f, (float)vol / 30720.0f);
    std::lock_guard<std::mutex> lk(gMu);
    gLastVol[player] = g;
    gStreams[player].gain = g;
}

extern "C" void portMusicMix(short* pcm, int sampleCount) {
    std::lock_guard<std::mutex> lk(gMu);
    for (auto& s : gStreams) {
        if (!s.active || !s.track || s.track->pcm.empty()) continue;
        const size_t total = s.track->pcm.size() / 2;
        for (int i = 0; i < sampleCount; i++) {
            if (s.pos >= total) {
                if (!s.track->loop) { s.active = false; break; }
                s.pos = 0;
            }
            for (int c = 0; c < 2; c++) {
                int v = pcm[i * 2 + c] + (int)(s.track->pcm[s.pos * 2 + c] * s.gain);
                pcm[i * 2 + c] = (short)(std::max)(-32768, (std::min)(32767, v));
            }
            s.pos++;
        }
    }
}
