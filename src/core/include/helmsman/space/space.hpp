// SPDX-License-Identifier: GPL-3.0-or-later
//
// Space as the viewscreen shows it: the stars streaming past, the ships
// around the player's own, their torpedoes and phaser beams, and the
// explosions. Ships fly and turn the way their description (.DSC) allows;
// the others chase the player, fire when they can and take damage on the
// side they are hit. See docs/rules/space.md and docs/formats/ships.md.
#pragma once

#include "helmsman/data/archive.hpp"
#include "helmsman/gfx/bitmap.hpp"
#include "helmsman/gfx/rect.hpp"
#include "helmsman/gfx/surface.hpp"
#include "helmsman/space/math.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <random>
#include <span>
#include <string>
#include <vector>

namespace helmsman::space {

// A part of a ship (or one shield): whether it works, how strong it is at
// best, and how strong it is now.
struct Part {
    bool on = false;
    int best = 0;
    int now = 0;
};

// The ship's parts, by their place in the list.
enum System : int {
    kHull = 0,
    kBridge = 1,
    kPortTorpedoes = 2,
    kStarboardTorpedoes = 3,
    kPortPhasers = 4,
    kStarboardPhasers = 5,
    kSensors = 6,
    kImpulse = 7,
    kPortWarp = 8,
    kStarboardWarp = 9,
};

// One weapon: when it may fire next, how long its shot lasts, how fast the
// shot flies, how long it takes to be ready again, where on the ship it
// sits and what it fires.
struct Mount {
    std::uint32_t readyAt = 0;
    int life = 0;
    int speed = 0;
    int reload = 0;
    Vec offset;
    int shot = 0;
};

// What a shot is.
enum ShotType : int {
    kTorpedo = 0,
    kPhaser = 1,
    kPlasma = 2,
    kBlast = 3, // the flash where a shot hit
};

// A kind of ship, as its description file gives it.
struct ShipClass {
    int picture = 0; // which set of pictures: 0 ENT, 1 KLING, 2 ORION, 3 ROM
    int size = 0;
    int scale = 3;
    std::array<Part, 6> shields{};
    std::array<Part, 10> parts{};
    std::int32_t maxTurn = 0;   // 16.16, steps a tick
    std::int32_t turnChange = 0;
    std::int32_t maxSpeed = 0;  // 16.16
    std::int32_t speedChange = 0;
    std::int32_t range = 0;     // the distance it likes to fight at
    bool cloaks = false;
    std::array<Mount, 4> mounts{};

    // Throws FormatError when the file is too short.
    static ShipClass fromBytes(std::span<const std::uint8_t> bytes);
};

// What sort of thing an object is.
enum class Kind : int {
    None = 0,
    Still = 1,  // a picture that only turns in the view: a station, a wreck
    Ship = 2,
    Shot = 3,
};

struct Object {
    Kind kind = Kind::None;
    int type = 0;  // the ship's picture set, the shot's type, or the still's picture
    int size = 0;  // how near a shot must come to hit
    int scale = 3; // how large its picture is drawn
    Vec pos;
    Mat axes = Mat::identity();
    Vec velocity;
    std::string stillName; // the picture of a Still

    // A ship's state.
    ShipClass cls;
    std::int32_t wantTurn[3] = {};  // pitch, yaw, roll it is asked for
    std::int32_t turnRate[3] = {};  // and what it does
    std::int32_t turned[3] = {};
    std::int32_t wantSpeed = 0;     // asked for
    std::int32_t allowedSpeed = 0;  // what its power allows
    std::int32_t speed = 0;         // its speed now
    int power = 0;
    int powerBoost = 0;
    bool shieldsUp = false;
    bool shieldsWork = false;
    bool weaponsArmed = false;
    bool weaponsWork = false;
    bool wantsCloak = false;
    bool cloaked = false;
    int cloakLevel = 0;    // 0 seen, rising to 28 as the cloak closes
    int behaviour = 0;     // 1: runs away
    std::int32_t distance = 0;
    std::int32_t closing = 0;
    int manoeuvre = -1;    // the move it is in the middle of, if any
    int manoeuvreStep = 0;
    int timer = 0;         // counts down every tick
    bool destroyed = false;
    int blastFrame = 0;

    // A shot's state.
    int owner = -1;
    int life = 0;
    int damage = 0;
    std::uint8_t colour = 0;

    // As seen this frame.
    Vec viewPos;
    Mat viewAxes;
    int screenX = 0;
    int screenY = 0;
    bool seen = false;
};

class Space {
public:
    // Objects: 0 is the player's ship, 1..4 the other ships, then shots
    // and still pictures.
    static constexpr int kPlayer = 0;
    static constexpr int kFirstShip = 1;
    static constexpr int kShips = 4;
    static constexpr int kFirstShot = kFirstShip + kShips;
    static constexpr int kShots = 32;
    static constexpr int kFirstStill = kFirstShot + kShots;
    static constexpr int kStills = 4;
    static constexpr int kObjects = kFirstStill + kStills;

    explicit Space(const Archive& archive);

    // The viewscreen: where on the screen, and how far it is zoomed (4..8).
    void setView(ClipRect rect, int zoom);
    const ClipRect& view() const { return view_; }
    int zoom() const { return zoom_; }
    int centreX() const { return centreX_; }
    int centreY() const { return centreY_; }

    // Sets up the player's ship from its description, at the centre.
    void setPlayer(const std::string& cls);
    // Adds a ship near the player; returns its number or -1 when full.
    int addShip(const std::string& cls);
    // Adds a still picture (e.g. "republic") at `where` from the player.
    int addStill(const std::string& picture, Vec where);
    void remove(int index);
    void clear();

    Object& object(int i) { return objects_[static_cast<std::size_t>(i)]; }
    const Object& object(int i) const { return objects_[static_cast<std::size_t>(i)]; }
    Object& player() { return object(kPlayer); }
    bool enemiesLeft() const;

    // The player's controls. Turning is in pixels from the centre of the
    // viewscreen; speed from 0 (stop) to 9 (full).
    void steer(int dx, int dy);
    void setRoll(int direction);
    void setSpeedLevel(int level);
    void fullReverse();
    // Fires at the point of the viewscreen; true when anything fired.
    bool fireTorpedoes(int screenX, int screenY);
    bool firePhasers(int screenX, int screenY);

    // One tick of space: the player's ship, the others, the shots. Calls
    // `say` for what the crew reports.
    void tick();
    // Draws the view into `to` (the viewscreen rectangle only).
    void draw(Surface& to);

    std::uint64_t ticks() const { return ticks_; }
    bool playerDestroyed() const { return playerDead_; }

    // Things the crew reports (as the game's event numbers): 2 shields
    // falling, 3 weapons off line, 4 speed, 7 a part repaired.
    std::function<void(int event, int what, bool player)> report;
    // A sound effect by the game's number.
    std::function<void(int sound)> sound;
    // Music: a song of the bridge music.
    std::function<void(int song)> music;
    // Which part to repair first (a System, or 0x7FFF for the shields).
    int repairFirst = -1;
    // The last other ship the player hit, for the target display; -1 none.
    int lastHit = -1;
    // True while the view shows interference because the sensors are hit.
    bool staticNow() const { return static_; }

    std::mt19937& random() { return rng_; }

private:
    void loadPictures();
    std::shared_ptr<const Bitmap> picture(const std::string& name);
    const ShipClass& shipClass(const std::string& name);

    // The player's ship and the others.
    void power(Object& s, bool isPlayer);
    void fly(Object& s);
    void repair(Object& s, bool isPlayer);
    void think(int index);
    void runManoeuvre(int index, const Vec& local, std::int32_t dist);
    bool fire(int shooter, int mount, const Vec* aim);
    void aimAndFire(int index, const Vec& local, const Vec& toTarget);
    void updateShots();
    void hit(int target, int shot);
    int sideHit(const Vec& local) const;
    bool damagePart(Object& s, int side, int damage);

    // Drawing.
    void placeCamera();
    void lookAt(Object& o);
    void drawStars(Surface& to);
    void drawObject(Surface& to, Object& o);
    void drawPicture(Surface& to, const Bitmap& b, bool mirrored, const double a[4], int x, int y);
    void drawBeam(Surface& to, const Object& o);
    void drawStatic(Surface& to);
    int project(std::int32_t v, std::int32_t z) const;

    const Archive& archive_;
    std::array<Object, kObjects> objects_{};
    std::map<std::string, ShipClass> classes_;
    std::map<std::string, std::shared_ptr<const Bitmap>> pictures_;
    std::vector<Bitmap> starPictures_;

    struct Star {
        bool live = false;
        Vec pos;
    };
    std::array<Star, 16> stars_{};

    ClipRect view_{0x48, 0x1E, 0xF8, 0x67};
    int zoom_ = 4;
    int centreX_ = 0;
    int centreY_ = 0;
    int halfW_ = 0;
    int halfH_ = 0;
    std::int32_t focal_ = 1;
    static constexpr std::int32_t kNear = 50;

    Vec camera_;
    Mat cameraAxes_ = Mat::identity();

    std::uint64_t ticks_ = 0;
    bool playerDead_ = false;
    bool static_ = false;
    std::mt19937 rng_{12345};
};

} // namespace helmsman::space
