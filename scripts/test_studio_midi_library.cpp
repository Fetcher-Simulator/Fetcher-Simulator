// g++ -std=c++20 -I. scripts/test_studio_midi_library.cpp -o test-studio-midi
// test-studio-midi <new-empty-test-directory> [real-library-to-scan]
#include <components/openmw-mp/StudioMidiLibrary.hpp>
#include <cassert>
#include <chrono>
#include <iostream>

int main(int argc, char** argv)
{
    assert(argc >= 2);
    namespace fs = std::filesystem;
    namespace midi = mwmp::studiomidi;
    const fs::path base(argv[1]);
    assert(!fs::exists(base));
    fs::create_directories(base / "library/sub");
    fs::create_directories(base / "outside");
    auto write = [&](const fs::path& path, const std::string& bytes) {
        std::ofstream file(path, std::ios::binary); file.write(bytes.data(),bytes.size());
    };
    const std::string song("MThd\0\0\0\6\0\0\0\1\0\x60MTrk\0\0\0\4\0\xff\x2f\0",26);
    write(base / "library/sub/test.MID",song);
    write(base / "outside/hidden.mid",song);
    assert(midi::read(base / "library","sub/test.MID") == song);
    for (const auto* name : {"", "no-extension", "../outside/hidden.mid", "/tmp/file.mid",
             "sub/../test.mid", "sub//test.mid", "./test.mid", "x\\file.mid", "C:file.mid", "x.txt"})
        assert(!midi::read(base / "library",name));
    fs::create_symlink(base / "outside/hidden.mid",base / "library/escape.mid");
    assert(!midi::read(base / "library","escape.mid"));
    write(base / "library/oversized.mid",std::string(midi::MaxFileSize+1,'a'));
    write(base / "library/not-midi.mid","invalid");
    auto little=[](std::size_t n) {
        std::string out;for(int i=0;i<4;++i) out.push_back(char(n>>(8*i)));return out;
    };
    const std::string rmid="RIFF"+little(12+song.size())+"RMIDdata"+little(song.size())+song;
    write(base / "library/wrapped.mid",rmid);
    assert(midi::read(base / "library","wrapped.mid")==song);
    assert(!midi::unwrap(rmid.substr(0,rmid.size()-1)));
    auto corrupt=rmid;corrupt[16]=char(255);assert(!midi::unwrap(corrupt));
    const auto entries=midi::scan(base / "library");
    assert(entries.size()==2);
    assert(entries[0].name=="sub/test.MID" && entries[1].name=="wrapped.mid");
    assert(entries[0].size==song.size() && entries[0].hash==mwmp::crypto::sha256hex(song));
    assert(entries[0].hash==entries[1].hash);
    assert(midi::scan(base / "missing").empty());
    std::cout << "PASS native MIDI containment, size, deterministic catalogue, hashes and RMID\n";
    if(argc > 2)
    {
        const auto start=std::chrono::steady_clock::now();
        const auto real=midi::scan(fs::path(argv[2]));
        std::size_t bytes=0;
        for(const auto& entry:real)
        {
            const auto data=midi::read(fs::path(argv[2]),entry.name);
            assert(data && data->size()==entry.size && mwmp::crypto::sha256hex(*data)==entry.hash);
            bytes+=entry.size;
        }
        std::cout << "Scanned and re-read " << real.size() << " songs / " << bytes << " bytes in "
                  << std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count() << "s\n";
        assert(real.size()==600);
    }
}
