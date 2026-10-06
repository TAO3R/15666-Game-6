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

	// level: table constants (size, pockets, cushion height) live in Physics.hpp

};
