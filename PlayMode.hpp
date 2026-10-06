#include "Mode.hpp"

#include "Scene.hpp"
#include "Physics.hpp"

#include <glm/glm.hpp>

#include <vector>
#include <deque>

struct PlayMode : Mode {
	PlayMode();
	virtual ~PlayMode();

	//functions called by main loop:
	virtual bool handle_event(SDL_Event const &, glm::uvec2 const &window_size) override;
	virtual void update(float elapsed) override;
	virtual void draw(glm::uvec2 const &drawable_size) override;

	//----- game state -----

	//input tracking:
	struct Button {
		uint8_t downs = 0;
		uint8_t ups = 0;
		uint8_t pressed = 0;
	} left, right, down, up, space, ascend, descend;	// ascend = E, descend = Q, free camera only

	//local copy of the game scene (so code can change it during gameplay):
	Scene scene;
	
	//camera:
	Scene::Transform camera_transform;
	Scene::Camera camera{&camera_transform};

	// Tab toggles: Shot = fixed top-down view with a free cursor for aiming, Free = first person fly camera
	enum class CameraMode : uint8_t { Shot, Free } camera_mode = CameraMode::Shot;

	// free camera, rotation is rebuilt from these two angles every time so roll can never creep in
	float camera_yaw = 0.0f;	// radians around world +z, unbounded (wrapped into [-pi, pi) to keep precision)
	float camera_pitch = 0.0f;	// radians up from the horizontal, clamped to [-90, 90] degrees

	// state enum
	enum class GameState : uint8_t
	{
		PrePlay = 0,
		Playing = 1,
		PostPlay = 2,
		Count = 3
	}	game_state = GameState::PrePlay;

	// level: table constants (size, pockets, cushion height) live in Physics.hpp

	// physics, stepped at a fixed physics_dt; rendering interpolates between the last two steps
	World world;
	World previous_world;
	std::vector< Shot > shots;	// every cue strike so far, replayed by tick
	float accumulator = 0.0f;	// unsimulated time, always < physics_dt after update
	void reset_world();	// back to the break layout, clears shots

	// balls, index matches world.balls (0 is the cue ball)
	std::vector< Scene::Transform * > ball_transforms;

	// slingshot aiming in the shot camera: hold left mouse, drag away from the cue ball, release to shoot
	static constexpr float max_shot_speed = 40.0f;	// cue ball speed at full power
	static constexpr float max_drag = 10.0f;	// drag distance for full power, also the longest aim line
	bool aiming = false;
	glm::vec2 aim_point = glm::vec2(0.0f);	// cursor on the ball-center plane (z = ball_radius)
	glm::vec2 cursor_to_table(glm::vec2 cursor, glm::uvec2 const &window_size) const;
	glm::vec2 aim_impulse() const;	// impulse a release would give the cue ball, zero inside the dead zone (cursor on the ball)

};
