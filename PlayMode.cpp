#include "PlayMode.hpp"

#include "LitColorTextureProgram.hpp"

#include "DrawLines.hpp"
#include "Mesh.hpp"
#include "Load.hpp"
#include "gl_errors.hpp"
#include "data_path.hpp"

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/color_space.hpp>

#include <random>

// colors are written as the sRGB hex you see on screen, the framebuffer does the sRGB encoding
static glm::vec3 srgb(uint32_t hex)
{
	return glm::convertSRGBToLinear(glm::vec3((hex >> 16) & 0xff, (hex >> 8) & 0xff, hex & 0xff) / 255.0f);
}

GLuint cube_sphere_vao = 0;
Load< MeshBuffer > cube_sphere_meshes(LoadTagDefault, []() -> MeshBuffer const * {
	MeshBuffer const *ret = new MeshBuffer(data_path("cube_sphere.pnct"));
	cube_sphere_vao = ret->make_vao_for_program(lit_color_texture_program->program);
	return ret;
});

PlayMode::PlayMode() : scene() {

	{	// clean up
		scene.drawables.clear();
		scene.transforms.clear();
	}

	{	// camera set up
		static constexpr float camera_height = 15.0f;
		camera_transform.name = "Camera";
		float pitch = glm::radians(0.0f);
		camera_transform.position = glm::vec3(0.0f, 0.0f, camera_height);
		camera_transform.rotation = glm::angleAxis(pitch, glm::vec3(1.0f, 0.0f, 0.0f));
	}
	
	{	// create the level with cubes
		Mesh const &cube_mesh = cube_sphere_meshes->lookup("Cube");
		// Mesh const &sphere_mesh = cube_sphere_meshes->lookup("Sphere");

		// add a transform
		scene.transforms.emplace_back();
		Scene::Transform *transform = &scene.transforms.back();
		transform->name = "cube";
		transform->position = glm::vec3(0.0f, 0.0f, 0.0f);
		transform->rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);	// identity (wxyz)
		transform->scale = glm::vec3(1.0f);

		// add a drawable
		scene.drawables.emplace_back(transform);
		Scene::Drawable *drawable = &scene.drawables.back();
		drawable->pipeline = lit_color_texture_program_pipeline;
		drawable->pipeline.vao = cube_sphere_vao;
		drawable->pipeline.type = cube_mesh.type;
		drawable->pipeline.start = cube_mesh.start;
		drawable->pipeline.count = cube_mesh.count;
		glm::vec3 color = srgb(0xff0000);
		drawable->pipeline.set_uniforms = [color]()
		{
			glUniform3fv(lit_color_texture_program->COLOR_vec3, 1, glm::value_ptr(color));
		};
	}
}

PlayMode::~PlayMode() {
}

bool PlayMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size) {

	if (evt.type == SDL_EVENT_KEY_DOWN) {
		if (evt.key.key == SDLK_ESCAPE) {
			SDL_SetWindowRelativeMouseMode(Mode::window, false);
			return true;
		} else if (evt.key.key == SDLK_A) {
			left.downs += 1;
			left.pressed = true;
			return true;
		} else if (evt.key.key == SDLK_D) {
			right.downs += 1;
			right.pressed = true;
			return true;
		} else if (evt.key.key == SDLK_W) {
			up.downs += 1;
			up.pressed = true;
			return true;
		} else if (evt.key.key == SDLK_S) {
			down.downs += 1;
			down.pressed = true;
			return true;
		} else if (evt.key.key == SDLK_SPACE) {
			space.downs += 1;
			space.pressed = true;
			return true;
		}
	} else if (evt.type == SDL_EVENT_KEY_UP) {
		if (evt.key.key == SDLK_A) {
			left.ups += 1;
			left.pressed = false;
			return true;
		} else if (evt.key.key == SDLK_D) {
			right.ups += 1;
			right.pressed = false;
			return true;
		} else if (evt.key.key == SDLK_W) {
			up.ups += 1;
			up.pressed = false;
			return true;
		} else if (evt.key.key == SDLK_S) {
			down.ups += 1;
			down.pressed = false;
			return true;
		} else if (evt.key.key == SDLK_SPACE) {
			space.ups += 1;
			space.pressed = false;
			return true;
		}
	} else if (evt.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
		if (SDL_GetWindowRelativeMouseMode(Mode::window) == false) {
			SDL_SetWindowRelativeMouseMode(Mode::window, true);
			return true;
		}
	} else if (evt.type == SDL_EVENT_MOUSE_MOTION) {
		if (SDL_GetWindowRelativeMouseMode(Mode::window) == true) {
			glm::vec2 motion = glm::vec2(
				evt.motion.xrel / float(window_size.y),
				-evt.motion.yrel / float(window_size.y)
			);
			camera.transform->rotation = glm::normalize(
				camera.transform->rotation
				* glm::angleAxis(-motion.x * camera.fovy, glm::vec3(0.0f, 1.0f, 0.0f))
				* glm::angleAxis(motion.y * camera.fovy, glm::vec3(1.0f, 0.0f, 0.0f))
			);
			return true;
		}
	}

	return false;
}

void PlayMode::update(float elapsed) {

	// gameplay
	switch (game_state)
	{
		case GameState::PrePlay:
			{
				if (space.ups) { game_state = GameState::Playing; }
			}
			break;
			
		case GameState::Playing:
			{
				{	//move camera:
					//combine inputs into a move:
					constexpr float PlayerSpeed = 30.0f;
					glm::vec2 move = glm::vec2(0.0f);
					if (left.pressed && !right.pressed) move.x =-1.0f;
					if (!left.pressed && right.pressed) move.x = 1.0f;
					if (down.pressed && !up.pressed) move.y =-1.0f;
					if (!down.pressed && up.pressed) move.y = 1.0f;

					//make it so that moving diagonally doesn't go faster:
					if (move != glm::vec2(0.0f)) move = glm::normalize(move) * PlayerSpeed * elapsed;

					glm::mat4x3 frame = camera.transform->make_parent_from_local();
					glm::vec3 frame_right = frame[0];
					//glm::vec3 up = frame[1];
					glm::vec3 frame_forward = -frame[2];

					camera.transform->position += move.x * frame_right + move.y * frame_forward;
				}
			}
			break;

		case GameState::PostPlay:
			{

			}
			break;

	}	// end of switch

	{	// reset inputs
		left.downs = 0;		left.ups = 0;
		right.downs = 0; 	right.ups = 0;
		up.downs = 0; 		up.ups = 0;
		down.downs = 0;		down.ups = 0;
		space.downs = 0; 	space.ups = 0;
	}

}	// end of update

void PlayMode::draw(glm::uvec2 const &drawable_size) {

	{	// clear the background to grey
		glClearColor(0.1f, 0.1f, 0.1f, 1.0f);	
		glClearDepth(1.0f); //1.0 is actually the default value to clear the depth buffer to, but FYI you can change it.
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	}

	switch (game_state)
	{
		case GameState::PrePlay:
			{
				
			}

			break;

		case GameState::Playing:
		case GameState::PostPlay:
			{
				//update camera aspect ratio for drawable:
				camera.aspect = float(drawable_size.x) / float(drawable_size.y);

				glEnable(GL_DEPTH_TEST);
				glDepthFunc(GL_LESS); //this is the default depth comparison function, but FYI you can change it.

				GL_ERRORS(); //print any errors produced by this setup code

				scene.draw(camera);
			}
			break;

	}	// end of switch

}	// end of draw
