// SPDX-License-Identifier: GPL-3.0-or-later
#include "game_data.hpp"
#include "test.hpp"

#include "helmsman/bridge/voyage.hpp"
#include "helmsman/data/archive.hpp"
#include "helmsman/engine/savegame.hpp"
#include "helmsman/space/space.hpp"

using namespace helmsman;

HLM_TEST("voyage: saved and restored") {
    bridge::Voyage v;
    v.mission = bridge::kSins;
    v.stage = 1;
    v.clock = 1234;
    v.vars[3] = 7;
    v.system = 0xB;
    v.destination = 0xB;
    v.inOrbit = true;
    v.textFile = "sins";
    v.commendations[2] = 3;
    SaveWriter w;
    v.save(w);
    bridge::Voyage back;
    SaveReader r(w.data);
    back.restore(r);
    HLM_CHECK(back.mission == bridge::kSins);
    HLM_CHECK(back.stage == 1);
    HLM_CHECK(back.clock == 1234);
    HLM_CHECK(back.vars[3] == 7);
    HLM_CHECK(back.system == 0xB);
    HLM_CHECK(back.inOrbit);
    HLM_CHECK(back.textFile == "sins");
    HLM_CHECK(back.commendations[2] == 3);
}

HLM_TEST("space: ships and stills saved and restored") {
    const auto game = test::openGame(GameId::Anniversary);
    if (!game) {
        HLM_SKIP("the 1992 game is not in gamedata/");
    }
    const auto archive = Archive::open(*game, "DATA");
    space::Space a(archive);
    a.setPlayer("ENTERPRI");
    a.player().cls.parts[space::kHull].now = 17;
    a.player().cls.shields[2].best = 99;
    const int k = a.addShip("KLINGON");
    HLM_CHECK(k == space::Space::kFirstShip);
    a.object(k).behaviour = 1;
    a.object(k).pos = space::Vec{10, -20, 3000};
    HLM_CHECK(a.addStill("republic", space::Vec{0, 0, 1600}) >= 0);
    SaveWriter w;
    a.save(w);

    space::Space b(archive);
    SaveReader r(w.data);
    b.restore(r);
    HLM_CHECK(b.player().cls.parts[space::kHull].now == 17);
    HLM_CHECK(b.player().cls.shields[2].best == 99);
    HLM_CHECK(b.object(k).kind == space::Kind::Ship);
    HLM_CHECK(b.object(k).className == "KLINGON");
    HLM_CHECK(b.object(k).behaviour == 1);
    HLM_CHECK(b.object(k).pos.z == 3000);
    HLM_CHECK(b.object(space::Space::kFirstStill).stillName == "republic");
    HLM_CHECK(b.enemiesLeft());
}

HLM_TEST("space: a damaged save is refused") {
    const auto game = test::openGame(GameId::Anniversary);
    if (!game) {
        HLM_SKIP("the 1992 game is not in gamedata/");
    }
    const auto archive = Archive::open(*game, "DATA");
    space::Space a(archive);
    a.setPlayer("ENTERPRI");
    SaveWriter w;
    a.save(w);
    w.data.resize(w.data.size() / 2);
    space::Space b(archive);
    SaveReader r(w.data);
    HLM_CHECK_THROWS(b.restore(r));
    std::vector<std::uint8_t> odd = {9};
    SaveReader r2(odd);
    HLM_CHECK_THROWS(b.restore(r2));
}
