#pragma once

// 2d billiard physics on the table plane (x-y), plain data only so it can be copied, hashed and tested without GL
// deterministic: fixed dt, integer ticks, every loop runs in index order (same build + same machine => same result)

#include <glm/glm.hpp>

#include <array>
#include <vector>
#include <cstdint>

// table, centered on the origin with the long side along x, one 1x1 cell per cube
// single source of truth for both the scene layout and the table collider
static constexpr int32_t table_length = 40;	// interior cells along x
static constexpr int32_t table_width = 20;	// interior cells along y
static constexpr int32_t pocket_size = 4;	// each pocket is a pocket_size x pocket_size hole in the felt
static constexpr int32_t pocket_depth = 2;	// net layers below the felt top (z = 0)
static constexpr float cushion_height = 0.8f;	// lower than the ball center (z = 1) so the cue clears it
inline std::array< glm::ivec2, 6 > const pocket_cells = {	// min corner cell of each pocket hole: 4 corners + 2 middles of the long sides
	glm::ivec2(0, 0),
	glm::ivec2((table_length - pocket_size) / 2, 0),
	glm::ivec2(table_length - pocket_size, 0),
	glm::ivec2(0, table_width - pocket_size),
	glm::ivec2((table_length - pocket_size) / 2, table_width - pocket_size),
	glm::ivec2(table_length - pocket_size, table_width - pocket_size)
};

// interior cell (x, y) is part of a pocket hole
bool in_pocket(int32_t x, int32_t y);

// tuning knobs
static constexpr float physics_dt = 1.0f / 120.0f;	// seconds per tick
static constexpr float ball_radius = 1.0f;	// matches the sphere mesh at scale 1
static constexpr float friction_decel = 5.0f;	// rolling friction (mu * g), speed lost per second
static constexpr float ball_restitution = 0.95f;	// ball vs ball bounciness
static constexpr float cushion_restitution = 0.8f;	// ball vs cushion bounciness
static constexpr float stop_speed = 0.05f;	// slower than this snaps to rest
static constexpr uint32_t max_events_per_step = 16;	// collision events resolved per tick before giving up and just advancing

struct Ball
{
	glm::vec2 position = glm::vec2(0.0f);
	glm::vec2 velocity = glm::vec2(0.0f);
	float mass = 1.0f;
	bool pocketed = false;	// out of the simulation once true
};

// a cue strike: at the start of 'tick', ball 'ball' gets 'impulse' (velocity += impulse / mass)
struct Shot
{
	uint32_t tick = 0;
	uint32_t ball = 0;
	glm::vec2 impulse = glm::vec2(0.0f);
};

struct World
{
	uint32_t tick = 0;
	std::vector< Ball > balls;
};

// advance one tick (physics_dt): apply this tick's shots, collide, friction, pockets
void step(World &world, std::vector< Shot > const &shots);

// FNV-1a over the simulation state, equal hashes == bitwise equal states
uint64_t hash(World const &world);

// cue ball (index 0) on the left quarter, 10 balls racked as a triangle on the right quarter
World make_break_world();
