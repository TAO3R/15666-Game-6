#include "PlayMode.hpp"

#include "LitColorTextureProgram.hpp"

#include "DrawLines.hpp"
#include "Mesh.hpp"
#include "Load.hpp"
#include "gl_errors.hpp"
#include "data_path.hpp"

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/color_space.hpp>
#include <glm/gtc/constants.hpp>

#include <random>
#include <algorithm>

// colors are written as the sRGB hex you see on screen, the framebuffer does the sRGB encoding
static glm::vec3 srgb(uint32_t hex)
{
	return glm::convertSRGBToLinear(glm::vec3((hex >> 16) & 0xff, (hex >> 8) & 0xff, hex & 0xff) / 255.0f);
}
static glm::vec3 const FloorColorA = srgb(0xadd8e6);	// light blue
static glm::vec3 const FloorColorB = srgb(0xd6ecf3);	// lighter blue
static glm::vec3 const WallColor = srgb(0xffffff);	// perimeter and common obstacles

static glm::vec3 const NetColor = srgb(0x404040);	// pocket nets, dark so the holes read as holes
static glm::vec3 const CueBallColor = srgb(0xffffff);
static glm::vec3 const BallColor = srgb(0xe03c3c);

static constexpr float camera_height = 22.0f;	// sees ~25m vertically, fits the 22-cell-tall table with its cushions
// cube_sphere.pnct: cube is 2m on a side and sphere has 1m radius, so a 0.5 scaled cube fills one 1x1 cell
// center of cell (x, y) in world space, cells are 1m and the table interior is centered on the origin
// x / y may go outside the interior (e.g. -1 for the cushion ring)
static glm::vec3 cell_center(int32_t x, int32_t y, float z)
{
	return glm::vec3(
		float(x) - 0.5f * float(table_length) + 0.5f,
		float(y) - 0.5f * float(table_width) + 0.5f,
		z
	);
}

// cube & sphere mesh
static GLuint cube_sphere_vao = 0;
static Load< MeshBuffer > cube_sphere_meshes(LoadTagDefault, []() -> MeshBuffer const * {
	MeshBuffer const *ret = new MeshBuffer(data_path("cube_sphere.pnct"));
	cube_sphere_vao = ret->make_vao_for_program(lit_color_texture_program->program);
	return ret;
});

PlayMode::PlayMode() : scene() {

	{	// clean up
		scene.drawables.clear();
		scene.transforms.clear();
	}

	{	// camera set up, starts in the shot view: straight above the table, looking down its local -z
		camera_transform.name = "Camera";
		camera_transform.position = glm::vec3(0.0f, 0.0f, camera_height);
		camera_transform.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);	// identity (wxyz)
	}
	
	{	// create the level with meshes

		// copy a 'mesh' to 'position' with a flat color
		auto add_cube = [&](std::string const &name, glm::vec3 const &position, glm::vec3 const &scale, glm::vec3 const &color, Mesh const &mesh) -> Scene::Drawable &
		{
			scene.transforms.emplace_back();
			Scene::Transform *transform = &scene.transforms.back();
			transform->name = name;
			transform->position = position;
			transform->scale = scale;

			scene.drawables.emplace_back(transform);
			Scene::Drawable &drawable = scene.drawables.back();
			drawable.pipeline = lit_color_texture_program_pipeline;
			drawable.pipeline.vao = cube_sphere_vao;
			drawable.pipeline.type = mesh.type;
			drawable.pipeline.start = mesh.start;
			drawable.pipeline.count = mesh.count;
			drawable.pipeline.set_uniforms = [color]()
			{
				glUniform3fv(lit_color_texture_program->COLOR_vec3, 1, glm::value_ptr(color));
			};
			return drawable;
		};

		Mesh const &cube_mesh = cube_sphere_meshes->lookup("Cube");

		glm::vec3 const half_scale(0.5f);	// one 1x1x1 cell

		int32_t const l = table_length;
		int32_t const w = table_width;

		{	// felt, top at z = 0, checkerboard, no cube over the pocket holes
			for (int32_t y = 0; y < w; y++)
			{
				for (int32_t x = 0; x < l; x++)
				{
					if (in_pocket(x, y)) { continue; }
					add_cube("Felt", cell_center(x, y, -0.5f), half_scale, (x + y) % 2 == 0 ? FloorColorA : FloorColorB, cube_mesh);
				}
			}
		}

		{	// cushions, one ring outside the l x w interior, standing on z = 0
			// a ring cell is left open if the interior cell next to it is a pocket, so corner pockets open on both sides
			glm::vec3 const cushion_scale(0.5f, 0.5f, 0.5f * cushion_height);
			for (int32_t y = -1; y <= w; y++)
			{
				for (int32_t x = -1; x <= l; x++)
				{
					bool on_ring = (x == -1 || x == l || y == -1 || y == w);
					if (!on_ring) { continue; }
					if (in_pocket(std::clamp(x, 0, l - 1), std::clamp(y, 0, w - 1))) { continue; }
					add_cube("Cushion", cell_center(x, y, 0.5f * cushion_height), cushion_scale, WallColor, cube_mesh);
				}
			}
		}

		{	// pocket nets, hollow box under each hole: a wall ring per layer plus a floor
			// floor top at z = -pocket_depth, so a ball resting in the net sits just below the felt top
			for (glm::ivec2 const &p : pocket_cells)
			{
				for (int32_t layer = 0; layer < pocket_depth; layer++)
				{
					float z = -0.5f - float(layer);
					for (int32_t y = p.y - 1; y <= p.y + pocket_size; y++)
					{
						for (int32_t x = p.x - 1; x <= p.x + pocket_size; x++)
						{
							bool on_ring = (x == p.x - 1 || x == p.x + pocket_size || y == p.y - 1 || y == p.y + pocket_size);
							if (!on_ring) { continue; }
							bool is_felt = (layer == 0 && x >= 0 && x < l && y >= 0 && y < w);	// top layer inside the table is already felt
							if (is_felt) { continue; }
							add_cube("Net", cell_center(x, y, z), half_scale, NetColor, cube_mesh);
						}
					}
				}

				for (int32_t y = p.y; y < p.y + pocket_size; y++)
				{
					for (int32_t x = p.x; x < p.x + pocket_size; x++)
					{
						add_cube("Net", cell_center(x, y, -0.5f - float(pocket_depth)), half_scale, NetColor, cube_mesh);
					}
				}
			}
		}

		{	// balls, positions are synced from the physics world every frame in update()
			Mesh const &sphere_mesh = cube_sphere_meshes->lookup("Sphere");
			world = make_level_world();
			previous_world = world;
			for (uint32_t i = 0; i < world.balls.size(); i++)
			{
				glm::vec3 position = glm::vec3(world.balls[i].position, ball_radius);
				Scene::Drawable &drawable = add_cube("Ball", position, glm::vec3(ball_radius), i == 0 ? CueBallColor : BallColor, sphere_mesh);
				ball_transforms.push_back(drawable.transform);
			}
		}
	}
}

void PlayMode::reset_world()
{
	world = make_level_world();
	previous_world = world;
	shots.clear();
	accumulator = 0.0f;
	aiming = false;
	shots_left = max_shots;
	paused = false;
	failed = false;
}

glm::vec2 PlayMode::cursor_to_table(glm::vec2 cursor, glm::uvec2 const &window_size) const
{
	// cursor (layout pixels, y down) -> ndc, then un-project two depths into a world space ray
	// ndc z = 1 is at infinity for infinitePerspective, so use -1 (near plane) and 0
	glm::vec2 ndc = glm::vec2(2.0f * cursor.x / float(window_size.x) - 1.0f, 1.0f - 2.0f * cursor.y / float(window_size.y));
	glm::mat4 world_from_clip = glm::inverse(camera.make_projection() * glm::mat4(camera.transform->make_local_from_world()));
	glm::vec4 a = world_from_clip * glm::vec4(ndc, -1.0f, 1.0f);
	glm::vec4 b = world_from_clip * glm::vec4(ndc, 0.0f, 1.0f);
	glm::vec3 from = glm::vec3(a) / a.w;
	glm::vec3 dir = glm::vec3(b) / b.w - from;

	if (dir.z == 0.0f) { return aim_point; }	// ray parallel to the table, keep the last point
	float t = (ball_radius - from.z) / dir.z;
	return glm::vec2(from + t * dir);
}

glm::vec2 PlayMode::aim_impulse() const
{
	Ball const &cue = world.balls[0];
	glm::vec2 drag = cue.position - aim_point;	// slingshot: the ball flies away from the cursor
	float dist = glm::length(drag);
	if (dist <= ball_radius) { return glm::vec2(0.0f); }	// releasing on the ball cancels the shot
	float power = std::min(dist / max_drag, 1.0f);
	return drag / dist * power * max_shot_speed * cue.mass;
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
		} else if (evt.key.key == SDLK_E) {
			ascend.downs += 1;
			ascend.pressed = true;
			return true;
		} else if (evt.key.key == SDLK_Q) {
			descend.downs += 1;
			descend.pressed = true;
			return true;
		} else if (evt.key.key == SDLK_R) {
			if (failed) { return true; }	// after a fail only space restarts
			reset_world();
			if (game_state == GameState::PostPlay) { game_state = GameState::Playing; }
			return true;
		} else if (evt.key.key == SDLK_TAB) {
			if (camera_mode == CameraMode::Shot)
			{	// free camera picks up from the shot view: yaw 0 / pitch -90 rebuilds to the same identity rotation
				camera_mode = CameraMode::Free;
				camera_yaw = 0.0f;
				camera_pitch = glm::radians(-90.0f);
				aiming = false;
				SDL_SetWindowRelativeMouseMode(Mode::window, true);
			}
			else
			{	// back to the fixed top-down view with a visible cursor
				camera_mode = CameraMode::Shot;
				camera_transform.position = glm::vec3(0.0f, 0.0f, camera_height);
				camera_transform.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
				SDL_SetWindowRelativeMouseMode(Mode::window, false);
			}
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
		} else if (evt.key.key == SDLK_E) {
			ascend.ups += 1;
			ascend.pressed = false;
			return true;
		} else if (evt.key.key == SDLK_Q) {
			descend.ups += 1;
			descend.pressed = false;
			return true;
		}
	} else if (evt.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
		if (camera_mode == CameraMode::Free)
		{
			if (SDL_GetWindowRelativeMouseMode(Mode::window) == false) {
				SDL_SetWindowRelativeMouseMode(Mode::window, true);
				return true;
			}
		}
		else if (evt.button.button == SDL_BUTTON_LEFT && can_shoot())
		{	// start a slingshot drag (allowed while paused)
			aiming = true;
			aim_point = cursor_to_table(glm::vec2(evt.button.x, evt.button.y), window_size);
			return true;
		}
	} else if (evt.type == SDL_EVENT_MOUSE_BUTTON_UP) {
		if (camera_mode == CameraMode::Shot && evt.button.button == SDL_BUTTON_LEFT && aiming)
		{	// release: shoot on the next physics tick, unless released on the ball
			aim_point = cursor_to_table(glm::vec2(evt.button.x, evt.button.y), window_size);
			glm::vec2 impulse = aim_impulse();
			if (impulse != glm::vec2(0.0f) && can_shoot())
			{
				shots.push_back(Shot{ world.tick, 0, impulse });
				shots_left--;
				paused = false;	// striking during time stop resumes time
			}
			aiming = false;
			return true;
		}
	} else if (evt.type == SDL_EVENT_MOUSE_MOTION) {
		if (camera_mode == CameraMode::Shot)
		{
			aim_point = cursor_to_table(glm::vec2(evt.motion.x, evt.motion.y), window_size);
			return true;
		}
		if (SDL_GetWindowRelativeMouseMode(Mode::window) == true) {
			glm::vec2 motion = glm::vec2(
				evt.motion.xrel / float(window_size.y),
				-evt.motion.yrel / float(window_size.y)
			);
			// first person look: unbounded yaw, clamped pitch, no roll term at all
			camera_yaw -= motion.x * camera.fovy;
			camera_yaw -= glm::two_pi< float >() * std::floor((camera_yaw + glm::pi< float >()) / glm::two_pi< float >());	// wrap into [-pi, pi)
			camera_pitch = std::clamp(camera_pitch + motion.y * camera.fovy, -glm::half_pi< float >(), glm::half_pi< float >());
			// +90 deg because the camera looks down its local -z, so an unrotated camera faces straight down
			camera.transform->rotation = glm::angleAxis(camera_yaw, glm::vec3(0.0f, 0.0f, 1.0f))
				* glm::angleAxis(camera_pitch + glm::half_pi< float >(), glm::vec3(1.0f, 0.0f, 0.0f));
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
				if (camera_mode == CameraMode::Free)
				{	//move camera:
					//combine inputs into a move:
					constexpr float PlayerSpeed = 30.0f;
					glm::vec3 move = glm::vec3(0.0f);
					if (left.pressed && !right.pressed) move.x =-1.0f;
					if (!left.pressed && right.pressed) move.x = 1.0f;
					if (down.pressed && !up.pressed) move.y =-1.0f;
					if (!down.pressed && up.pressed) move.y = 1.0f;
					if (descend.pressed && !ascend.pressed) move.z =-1.0f;
					if (!descend.pressed && ascend.pressed) move.z = 1.0f;

					//make it so that moving diagonally doesn't go faster:
					if (move != glm::vec3(0.0f)) move = glm::normalize(move) * PlayerSpeed * elapsed;

					// WASD on the x-y plane from the yaw alone (pitch doesn't tilt the move), Q / E along world z
					// yaw = 0 faces +y with local x on world +x, matching the rotation built in handle_event
					glm::vec3 frame_forward = glm::vec3(-std::sin(camera_yaw), std::cos(camera_yaw), 0.0f);
					glm::vec3 frame_right = glm::vec3(std::cos(camera_yaw), std::sin(camera_yaw), 0.0f);

					camera.transform->position += move.x * frame_right + move.y * frame_forward + glm::vec3(0.0f, 0.0f, move.z);
				}

				if (failed)
				{	// table frozen on "Press Space to Try Again", space is the only way back to the level layout
					if (space.downs) { reset_world(); }
				}
				else if (space.downs)
				{	// time stop
					paused = !paused;
				}
				else if (!paused)
				{	// physics at a fixed step, as many ticks as the elapsed time covers
					accumulator += elapsed;
					while (accumulator >= physics_dt)
					{
						previous_world = world;
						step(world, shots);
						accumulator -= physics_dt;

						// win: every red ball is down (checked first, so a last-moment pocket still counts)
						bool cleared = std::all_of(world.balls.begin() + 1, world.balls.end(), [](Ball const &ball) { return ball.pocketed; });
						if (cleared)
						{
							game_state = GameState::PostPlay;
							accumulator = 0.0f;
							break;
						}

						// lose: once the cue ball has been struck, it coming to rest or falling in ends the attempt
						Ball const &cue = world.balls[0];
						bool struck = shots_left < max_shots;
						if (struck && (cue.pocketed || cue.velocity == glm::vec2(0.0f)))
						{
							failed = true;
							aiming = false;
							accumulator = 0.0f;
							break;
						}
					}
				}

				{	// sync balls, interpolated between the last two ticks so motion stays smooth at any frame rate
					float alpha = accumulator / physics_dt;
					for (uint32_t i = 0; i < ball_transforms.size(); i++)
					{
						Ball const &ball = world.balls[i];
						glm::vec2 position = glm::mix(previous_world.balls[i].position, ball.position, alpha);
						ball_transforms[i]->position = glm::vec3(position, ball_radius);
						ball_transforms[i]->scale = glm::vec3(ball.pocketed ? 0.0f : ball_radius);	// pocketed balls just vanish
					}
					if (world.balls[0].pocketed) { aiming = false; }	// cue ball gone: nothing to aim until R
				}
			}
			break;

		case GameState::PostPlay:
			{	// table stays frozen on the winning frame until space
				if (space.downs)
				{
					reset_world();
					game_state = GameState::Playing;
				}
			}
			break;

	}	// end of switch

	{	// reset inputs
		left.downs = 0;		left.ups = 0;
		right.downs = 0; 	right.ups = 0;
		up.downs = 0; 		up.ups = 0;
		down.downs = 0;		down.ups = 0;
		space.downs = 0; 	space.ups = 0;
		ascend.downs = 0;	ascend.ups = 0;
		descend.downs = 0;	descend.ups = 0;
	}

}	// end of update

void PlayMode::draw(glm::uvec2 const &drawable_size) {

	{	// clear the background to grey
		glClearColor(0.1f, 0.1f, 0.1f, 1.0f);	
		glClearDepth(1.0f); //1.0 is actually the default value to clear the depth buffer to, but FYI you can change it.
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	}

	// horizontally center one line on its measured width
	auto centered = [&drawable_size](TextRenderer &font, std::string const &text, float baseline_y, glm::u8vec4 const &color)
	{
		float x = 0.5f * (float(drawable_size.x) - font.measure(text));
		font.draw(text, drawable_size, glm::vec2(x, baseline_y), color);
	};
	float const title_baseline = 0.5f * float(drawable_size.y) - 0.5f * (title.ascender() - title.descender());	// roughly center the caps on the screen
	glm::u8vec4 const light_text = glm::u8vec4(0xff, 0xff, 0xff, 0xff);	// on the dark background
	glm::u8vec4 const dark_text = glm::u8vec4(0x00, 0x00, 0x00, 0xff);	// on the light felt; vertex color is linear, anything above 0 gets brightened by the sRGB framebuffer

	switch (game_state)
	{
		case GameState::PrePlay:
			{
				centered(title, "\"One Shot\" Clearance", title_baseline, light_text);
				centered(hud, "Press Space to start", title_baseline - title.descender() - hud.ascender(), light_text);
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

				if (camera_mode == CameraMode::Shot && aiming)
				{	// aim line in world space: from the cue ball along the shot, longer and redder with power
					glm::vec2 impulse = aim_impulse();
					if (impulse != glm::vec2(0.0f))
					{
						glm::vec2 cue = world.balls[0].position;
						float power = glm::length(impulse) / (max_shot_speed * world.balls[0].mass);
						glm::vec2 end = cue + impulse / glm::length(impulse) * power * max_drag;
						uint8_t fade = uint8_t(255.0f * (1.0f - power));
						DrawLines lines(camera.make_projection() * glm::mat4(camera.transform->make_local_from_world()));
						lines.draw(glm::vec3(cue, ball_radius), glm::vec3(end, ball_radius), glm::u8vec4(0xff, fade, fade, 0xff));
					}
				}

				{	// hud, drawn over the scene
					glDisable(GL_DEPTH_TEST);

					float const margin = 0.5f * hud.line_height();
					std::string status = "Shots: " + std::to_string(shots_left);
					if (paused) { status += "  PAUSED"; }
					hud.draw(status, drawable_size, glm::vec2(margin, margin + hud.descender()), light_text);

					if (failed) { centered(title, "Press Space to Try Again", title_baseline, dark_text); }

					if (game_state == GameState::PostPlay)
					{
						centered(title, "Clear!", title_baseline, dark_text);
						centered(hud, "Space to play again", title_baseline - title.descender() - hud.ascender(), dark_text);
					}
				}
			}
			break;

	}	// end of switch

}	// end of draw
