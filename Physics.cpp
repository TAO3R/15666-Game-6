#include "Physics.hpp"

#include <cmath>
#include <algorithm>

// half extents of the table interior in world units
static constexpr float half_length = 0.5f * float(table_length);
static constexpr float half_width = 0.5f * float(table_width);

bool in_pocket(int32_t x, int32_t y)
{
	for (glm::ivec2 const &p : pocket_cells)
	{
		if (x >= p.x && x < p.x + pocket_size && y >= p.y && y < p.y + pocket_size) { return true; }
	}
	return false;
}

// inner face of the cushion ring, normal points into the table
struct Segment
{
	glm::vec2 a, b;
	glm::vec2 normal;
};

// the whole table as one static collider: cushion segments that break at the pockets, plus their end points
// built once from the table constants, using the same "cushion is open next to a pocket cell" rule as the scene
static std::vector< Segment > const &cushion_segments()
{
	static std::vector< Segment > const segments = []()
	{
		std::vector< Segment > ret;

		// walk 'count' interior cells along one edge, merging runs of non-pocket cells into one segment
		auto add_edge = [&](glm::ivec2 cell0, glm::ivec2 cell_step, int32_t count, glm::vec2 p0, glm::vec2 p_step, glm::vec2 normal)
		{
			int32_t run_start = -1;
			for (int32_t i = 0; i <= count; i++)
			{
				bool open = (i == count) || in_pocket(cell0.x + cell_step.x * i, cell0.y + cell_step.y * i);
				if (!open && run_start < 0) { run_start = i; }
				if (open && run_start >= 0)
				{
					ret.push_back(Segment{ p0 + p_step * float(run_start), p0 + p_step * float(i), normal });
					run_start = -1;
				}
			}
		};

		add_edge(glm::ivec2(0, 0), glm::ivec2(1, 0), table_length, glm::vec2(-half_length, -half_width), glm::vec2(1.0f, 0.0f), glm::vec2(0.0f, 1.0f));	// bottom
		add_edge(glm::ivec2(0, table_width - 1), glm::ivec2(1, 0), table_length, glm::vec2(-half_length, half_width), glm::vec2(1.0f, 0.0f), glm::vec2(0.0f, -1.0f));	// top
		add_edge(glm::ivec2(0, 0), glm::ivec2(0, 1), table_width, glm::vec2(-half_length, -half_width), glm::vec2(0.0f, 1.0f), glm::vec2(1.0f, 0.0f));	// left
		add_edge(glm::ivec2(table_length - 1, 0), glm::ivec2(0, 1), table_width, glm::vec2(half_length, -half_width), glm::vec2(0.0f, 1.0f), glm::vec2(-1.0f, 0.0f));	// right

		return ret;
	}();
	return segments;
}

// earliest t in [0, max_t] where |p + v t| == radius while p and v close in on each other, or -1 if none
// p / v are relative position / velocity; used for ball vs ball (radius 2r) and ball vs cushion end point (radius r)
static float sweep_circle(glm::vec2 p, glm::vec2 v, float radius, float max_t)
{
	float b = glm::dot(p, v);
	if (b >= 0.0f) { return -1.0f; }	// separating or at rest relative to each other

	float c = glm::dot(p, p) - radius * radius;
	if (c <= 0.0f) { return 0.0f; }	// already touching

	float a = glm::dot(v, v);
	float disc = b * b - a * c;
	if (disc < 0.0f) { return -1.0f; }	// misses

	float t = (-b - std::sqrt(disc)) / a;
	return t <= max_t ? t : -1.0f;
}

// earliest t in [0, max_t] where the ball touches the segment face while moving into it, or -1 if none
// hits beyond the segment ends are left to the end point check
static float sweep_segment(Ball const &ball, Segment const &s, float max_t)
{
	float vn = glm::dot(ball.velocity, s.normal);
	if (vn >= 0.0f) { return -1.0f; }	// moving away from or along the face

	float d0 = glm::dot(ball.position - s.a, s.normal);
	if (d0 < 0.0f) { return -1.0f; }	// center already behind the face (inside a pocket mouth)

	float t = std::max(0.0f, (ball_radius - d0) / vn);
	if (t > max_t) { return -1.0f; }

	glm::vec2 along = s.b - s.a;
	float u = glm::dot(ball.position + ball.velocity * t - s.a, along);
	if (u < 0.0f || u > glm::dot(along, along)) { return -1.0f; }

	return t;
}

// bounce 'velocity' off a static surface with normal 'n'
static void bounce(glm::vec2 &velocity, glm::vec2 n, float restitution)
{
	float vn = glm::dot(velocity, n);
	if (vn < 0.0f) { velocity -= (1.0f + restitution) * vn * n; }
}

void step(World &world, std::vector< Shot > const &shots)
{
	std::vector< Ball > &balls = world.balls;
	std::vector< Segment > const &segments = cushion_segments();

	{	// shots scheduled for this tick
		for (Shot const &shot : shots)
		{
			if (shot.tick != world.tick || shot.ball >= balls.size()) { continue; }
			Ball &ball = balls[shot.ball];
			if (ball.pocketed) { continue; }
			ball.velocity += shot.impulse / ball.mass;
		}
	}

	{	// move + collide, earliest event first
		// all balls move together to the earliest contact, that contact is resolved, repeat for the rest of the tick
		// ties keep the first found (lowest indices), so the order is deterministic
		enum class EventKind : uint8_t { None, BallBall, BallSegment, BallPoint };

		auto advance = [&](float t)
		{
			for (Ball &ball : balls)
			{
				if (!ball.pocketed) { ball.position += ball.velocity * t; }
			}
		};

		float remaining = physics_dt;
		for (uint32_t iteration = 0; iteration < max_events_per_step; iteration++)
		{
			EventKind kind = EventKind::None;
			float event_t = remaining;
			uint32_t event_i = 0, event_j = 0;	// ball index, then other ball / segment / point index

			for (uint32_t i = 0; i < balls.size(); i++)
			{
				Ball const &a = balls[i];
				if (a.pocketed) { continue; }

				for (uint32_t j = i + 1; j < balls.size(); j++)
				{
					Ball const &b = balls[j];
					if (b.pocketed) { continue; }
					float t = sweep_circle(b.position - a.position, b.velocity - a.velocity, 2.0f * ball_radius, event_t);
					if (t >= 0.0f && (t < event_t || kind == EventKind::None)) { kind = EventKind::BallBall; event_t = t; event_i = i; event_j = j; }
				}

				for (uint32_t s = 0; s < segments.size(); s++)
				{
					float t = sweep_segment(a, segments[s], event_t);
					if (t >= 0.0f && (t < event_t || kind == EventKind::None)) { kind = EventKind::BallSegment; event_t = t; event_i = i; event_j = s; }
				}

				for (uint32_t s = 0; s < segments.size(); s++)
				{
					for (uint32_t e = 0; e < 2; e++)
					{
						glm::vec2 point = e == 0 ? segments[s].a : segments[s].b;
						float t = sweep_circle(a.position - point, a.velocity, ball_radius, event_t);
						if (t >= 0.0f && (t < event_t || kind == EventKind::None)) { kind = EventKind::BallPoint; event_t = t; event_i = i; event_j = s * 2 + e; }
					}
				}
			}

			if (kind == EventKind::None) { break; }

			advance(event_t);
			remaining -= event_t;

			Ball &a = balls[event_i];
			if (kind == EventKind::BallBall)
			{
				Ball &b = balls[event_j];
				glm::vec2 d = b.position - a.position;
				float len = glm::length(d);
				if (len > 0.0f)
				{
					glm::vec2 n = d / len;
					float vn = glm::dot(b.velocity - a.velocity, n);
					if (vn < 0.0f)
					{
						float j = -(1.0f + ball_restitution) * vn / (1.0f / a.mass + 1.0f / b.mass);
						a.velocity -= (j / a.mass) * n;
						b.velocity += (j / b.mass) * n;
					}
				}
			}
			else if (kind == EventKind::BallSegment)
			{
				bounce(a.velocity, segments[event_j].normal, cushion_restitution);
			}
			else
			{
				Segment const &s = segments[event_j / 2];
				glm::vec2 d = a.position - (event_j % 2 == 0 ? s.a : s.b);
				float len = glm::length(d);
				if (len > 0.0f) { bounce(a.velocity, d / len, cushion_restitution); }
			}
		}

		// whatever time is left (no more events, or out of iterations) is plain motion
		advance(remaining);
	}

	{	// rolling friction, constant deceleration until rest
		for (Ball &ball : balls)
		{
			if (ball.pocketed) { continue; }
			float speed = glm::length(ball.velocity);
			float new_speed = speed - friction_decel * physics_dt;
			if (new_speed < stop_speed) { ball.velocity = glm::vec2(0.0f); }
			else { ball.velocity *= new_speed / speed; }
		}
	}

	{	// pockets, center inside a hole => out of play
		for (Ball &ball : balls)
		{
			if (ball.pocketed) { continue; }
			for (glm::ivec2 const &p : pocket_cells)
			{
				glm::vec2 min = glm::vec2(float(p.x) - half_length, float(p.y) - half_width);
				glm::vec2 max = min + glm::vec2(float(pocket_size));
				if (ball.position.x >= min.x && ball.position.x <= max.x && ball.position.y >= min.y && ball.position.y <= max.y)
				{
					ball.pocketed = true;
					ball.velocity = glm::vec2(0.0f);
					break;
				}
			}
		}
	}

	world.tick++;
}

uint64_t hash(World const &world)
{
	uint64_t h = 14695981039346656037ull;
	auto mix = [&](void const *data, size_t size)
	{
		unsigned char const *bytes = static_cast< unsigned char const * >(data);
		for (size_t i = 0; i < size; i++)
		{
			h ^= bytes[i];
			h *= 1099511628211ull;
		}
	};

	mix(&world.tick, sizeof(world.tick));
	for (Ball const &ball : world.balls)
	{
		mix(&ball.position, sizeof(ball.position));
		mix(&ball.velocity, sizeof(ball.velocity));
		mix(&ball.mass, sizeof(ball.mass));
		uint8_t pocketed = ball.pocketed ? 1 : 0;
		mix(&pocketed, sizeof(pocketed));
	}
	return h;
}

World make_break_world()
{
	World world;

	{	// cue ball
		Ball cue;
		cue.position = glm::vec2(-0.25f * float(table_length), 0.0f);
		world.balls.push_back(cue);
	}

	{	// rack, apex toward the cue ball, a small gap so no two balls start in contact
		float const spacing = 2.0f * ball_radius + 0.01f;
		glm::vec2 const apex = glm::vec2(0.25f * float(table_length), 0.0f);
		for (int32_t row = 0; row < 4; row++)
		{
			for (int32_t k = 0; k <= row; k++)
			{
				Ball ball;
				ball.position = apex + glm::vec2(float(row) * spacing * 0.8660254f, (float(k) - 0.5f * float(row)) * spacing);
				world.balls.push_back(ball);
			}
		}
	}

	return world;
}
