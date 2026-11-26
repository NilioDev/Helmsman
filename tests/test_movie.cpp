// SPDX-License-Identifier: GPL-3.0-or-later
#include "game_data.hpp"
#include "test.hpp"

#include "helmsman/data/movie.hpp"
#include "helmsman/data/reader.hpp"

#include <cstring>

using namespace helmsman;

namespace {

// Builds a film in memory: one chunk per call to chunk().
struct FilmWriter {
    std::vector<std::uint8_t> bytes;
    std::vector<std::uint8_t> current;

    FilmWriter() {
        const char magic[] = "Interplay MVE File\x1A";
        bytes.assign(magic, magic + 19);
        bytes.resize(26, 0);
    }
    static void put16(std::vector<std::uint8_t>& to, int v) {
        to.push_back(static_cast<std::uint8_t>(v));
        to.push_back(static_cast<std::uint8_t>(v >> 8));
    }
    void op(int type, int version, const std::vector<std::uint8_t>& body) {
        put16(current, static_cast<int>(body.size()));
        current.push_back(static_cast<std::uint8_t>(type));
        current.push_back(static_cast<std::uint8_t>(version));
        current.insert(current.end(), body.begin(), body.end());
    }
    void chunk() {
        op(1, 0, {});
        put16(bytes, static_cast<int>(current.size()));
        put16(bytes, 0);
        bytes.insert(bytes.end(), current.begin(), current.end());
        current.clear();
    }
    static std::vector<std::uint8_t> words(std::initializer_list<int> values) {
        std::vector<std::uint8_t> out;
        for (const int v : values) {
            put16(out, v);
        }
        return out;
    }
    // A picture with every block's code given.
    void picture(std::initializer_list<int> codes, const std::vector<std::uint8_t>& raw) {
        auto body = words({1, 0, 0, 0, 2, 1, 0});
        const auto map = words(codes);
        body.insert(body.end(), map.begin(), map.end());
        body.insert(body.end(), raw.begin(), raw.end());
        op(6, 0, body);
        op(7, 1, words({0, 0, 0}));
    }
};

std::vector<std::uint8_t> block(std::uint8_t colour) { return std::vector<std::uint8_t>(64, colour); }

} // namespace

HLM_TEST("movie: raw blocks, copies and taking turns") {
    FilmWriter film;
    film.op(2, 0, {0xE8, 0x03, 0, 0, 10, 0}); // 1000 us x 10
    film.op(5, 0, FilmWriter::words({2, 1}));  // 16 x 8
    film.op(12, 0, {1, 0, 1, 0, 63, 0, 0});    // colour 1 is red
    film.chunk();
    // Picture 1: the left block raw (colour 5), the right copies it from
    // 8 pixels to the left in the picture being drawn.
    auto raw = block(5);
    film.picture({0, 0x4000 - 8}, raw);
    film.chunk();
    // Picture 2: both blocks raw.
    raw = block(6);
    auto more = block(7);
    raw.insert(raw.end(), more.begin(), more.end());
    film.picture({0, 0}, raw);
    film.chunk();
    // Picture 3: the left block keeps what was there two pictures ago (5),
    // the right copies the left block of the last picture (6).
    film.picture({0x4000, 0xC000 - 8}, {});
    film.chunk();
    film.op(0, 0, {});
    film.chunk();

    Movie movie(film.bytes);
    HLM_CHECK(movie.next());
    HLM_CHECK(movie.width() == 16 && movie.height() == 8);
    HLM_CHECK(movie.frameMicros() == 10000);
    HLM_CHECK(movie.palette()[1].r == 63);
    HLM_CHECK(movie.picture()[0] == 5 && movie.picture()[15] == 5);
    HLM_CHECK(movie.next());
    HLM_CHECK(movie.picture()[0] == 6 && movie.picture()[8] == 7);
    HLM_CHECK(movie.next());
    HLM_CHECK(movie.picture()[0] == 5 && movie.picture()[8] == 6);
    HLM_CHECK(!movie.next());
}

HLM_TEST("movie: a skip list leaves blocks alone") {
    FilmWriter film;
    film.op(5, 0, FilmWriter::words({3, 1}));
    film.chunk();
    for (int pass = 0; pass < 2; ++pass) {
        // Bits 1,1,1 then the end mark: all three blocks raw.
        film.op(14, 0, FilmWriter::words({0xF000}));
        film.op(15, 0, FilmWriter::words({0, 0, 0}));
        auto raw = block(static_cast<std::uint8_t>(1 + pass));
        for (int i = 0; i < 2; ++i) {
            const auto b = block(static_cast<std::uint8_t>(1 + pass));
            raw.insert(raw.end(), b.begin(), b.end());
        }
        auto body = FilmWriter::words({1, 0, 0, 0, 3, 1, 0});
        body.insert(body.end(), raw.begin(), raw.end());
        film.op(16, 0, body);
        film.op(7, 1, {});
        film.chunk();
    }
    // Only the middle block changes (0,1,0, end mark), to colour 9.
    film.op(14, 0, FilmWriter::words({0x5000}));
    film.op(15, 0, FilmWriter::words({0}));
    auto body = FilmWriter::words({1, 0, 0, 0, 3, 1, 0});
    const auto b = block(9);
    body.insert(body.end(), b.begin(), b.end());
    film.op(16, 0, body);
    film.op(7, 1, {});
    film.chunk();

    Movie movie(film.bytes);
    HLM_CHECK(movie.next() && movie.next() && movie.next());
    // Skipped blocks keep the picture they are drawn over: two back.
    HLM_CHECK(movie.picture()[0] == 1 && movie.picture()[8] == 9 && movie.picture()[16] == 1);
}

HLM_TEST("movie: sound is collected and damaged films are refused") {
    FilmWriter film;
    film.op(3, 1, {0, 0, 0, 0, 0x11, 0x2B, 0, 0, 0, 0}); // 11025 Hz, 8-bit mono
    film.op(8, 0, {0, 0, 1, 0, 3, 0, 10, 20, 30});
    film.op(9, 0, {1, 0, 1, 0, 2, 0});
    film.op(8, 0, {2, 0, 2, 0, 1, 0, 99}); // another language: left out
    film.chunk();
    Movie movie(film.bytes);
    HLM_CHECK(!movie.next());
    HLM_CHECK(movie.soundRate() == 11025);
    HLM_CHECK(movie.takeSound() == std::vector<std::uint8_t>{10, 20, 30, 0x80, 0x80});

    HLM_CHECK_THROWS(Movie(std::vector<std::uint8_t>(10, 0)));
    FilmWriter huge;
    huge.op(5, 0, FilmWriter::words({1000, 1000}));
    huge.chunk();
    Movie big(huge.bytes);
    HLM_CHECK_THROWS(big.next());
    FilmWriter short_;
    short_.op(5, 0, FilmWriter::words({1, 1}));
    short_.picture({0}, {1, 2, 3}); // a raw block with three of its 64 pixels
    short_.chunk();
    Movie cut(short_.bytes);
    HLM_CHECK_THROWS(cut.next());
}

HLM_TEST("movie: every film of the sequel plays") {
    auto game = test::openGame(GameId::JudgmentRites);
    if (!game) {
        HLM_SKIP("the sequel is not in gamedata/");
    }
    for (const char* name : {"CAPT", "TREKINTS", "VOOM", "VOUCH", "ENDGAME"}) {
        auto data = game->read(std::string("UNCOMP/") + name + ".MVE");
        HLM_CHECK(data.has_value());
        Movie movie(std::move(*data));
        int pictures = 0;
        while (movie.next()) {
            ++pictures;
        }
        HLM_CHECK(pictures > 40);
    }
}
