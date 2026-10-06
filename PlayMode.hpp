#include "Mode.hpp"

#include "Scene.hpp"

#include <glm/glm.hpp>

#include <vector>
#include <deque>
#include <array>

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
	} left, right, down, up, space;
	// TODO(control): add 'ascend' (E) and 'descend' (Q) buttons for moving along world z

	//local copy of the game scene (so code can change it during gameplay):
	Scene scene;
	
	//camera:
	Scene::Transform camera_transform;
	Scene::Camera camera{&camera_transform};
	// TODO(control): first person camera state, rotation is rebuilt from these two angles every time so roll can never creep in
	//  float camera_yaw = 0.0f;	// radians around world +z, unbounded (wrap into [-pi, pi) to keep precision)
	//  float camera_pitch = 0.0f;	// radians up from the horizontal, clamped to [-90, 90] degrees

	// state enum
	enum class GameState : uint8_t
	{
		PrePlay = 0,
		Playing = 1,
		PostPlay = 2,
		Count = 3
	}	game_state = GameState::PrePlay;

	// level, table centered on the origin with the long side along x, one 1x1 cell per cube
	// single source of truth for both the scene layout and the table collider
	static constexpr int32_t table_length = 40;	// interior cells along x
	static constexpr int32_t table_width = 20;	// interior cells along y
	static constexpr int32_t pocket_size = 4;	// each pocket is a pocket_size x pocket_size hole in the felt
	static constexpr int32_t pocket_depth = 2;	// net layers below the felt top (z = 0)
	static constexpr float cushion_height = 0.8f;	// lower than the ball center (z = 1) so the cue clears it
	std::array< glm::ivec2, 6 > pocket_cells = {	// min corner cell of each pocket hole: 4 corners + 2 middles of the long sides
		glm::ivec2(0, 0),
		glm::ivec2((table_length - pocket_size) / 2, 0),
		glm::ivec2(table_length - pocket_size, 0),
		glm::ivec2(0, table_width - pocket_size),
		glm::ivec2((table_length - pocket_size) / 2, table_width - pocket_size),
		glm::ivec2(table_length - pocket_size, table_width - pocket_size)
	};

	// physics


	// balls

};
