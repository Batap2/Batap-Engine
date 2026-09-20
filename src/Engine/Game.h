#pragma once

namespace batap
{
struct World;

// The game as the engine drives it. The standalone main and the editor's Play
// button call the same functions: init() when play begins (the scene is
// already loaded — spawn, grab references), then per frame: fixedUpdate() 0..n
// times at World::Time::fixedDt_ (physics, deterministic logic), update()
// before the transform flush, lateUpdate() after it — world matrices read in
// lateUpdate are this frame's (camera follow, look-at). dt is already scaled
// by pause/timescale (0 when paused).
// Scene loading itself stays outside: the standalone main loads the game's
// scene, the editor plays whatever scene is open.
// editorUpdate() is the editor's: every frame while NOT playing, before the
// engine systems, never from the standalone exe. Inspector actions live there
// (a bool the game consumes and clears), so their result lands in the scene
// that gets saved instead of being undone by Stop. No input, no physics.
struct Game
{
    virtual ~Game() = default;
    virtual void init(World&) {}
    virtual void fixedUpdate(World&, float dt) {}
    virtual void update(World&, float dt) {}
    virtual void lateUpdate(World&, float dt) {}
    virtual void editorUpdate(World&) {}
};
}  // namespace batap
