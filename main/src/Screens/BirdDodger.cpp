#include "BirdDodger.h"
#include "Util/Services.h"
#include "Devices/Input.h"
#include "MainMenu/MainMenu.h"
#include "Util/Notes.h"
#include "Services/SleepMan.h"
#include "Util/stdafx.h"
#include <cmath>
#include <algorithm>
#include <esp_random.h>

BirdDodger::BirdDodger() 
	: planeX(0.5f),
	  score(0),
	  lastDifficultyScore(0),
	  gameOver(false),
	  scrollSpeed(INITIAL_SPEED),
	  frameCounter(0),
	  gameThread([this](){ gameLoop(); }, "BirdDodger", 4096, 5, 1),
	  rollFilter(filterStrength),
	  queue(4)
{
	// Get services
	imu = (IMU*) Services.get(Service::IMU);
	audio = (ChirpSystem*) Services.get(Service::Audio);

	// Create background
	bg = lv_obj_create(*this);
	lv_obj_set_size(bg, SCREEN_WIDTH, SCREEN_HEIGHT);
	lv_obj_set_style_bg_color(bg, lv_color_black(), 0);
	lv_obj_set_style_bg_opa(bg, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(bg, 1, 0);
	lv_obj_set_style_border_color(bg, lv_color_white(), 0);

	// Create score label
	scoreLabel = lv_label_create(bg);
	lv_label_set_text(scoreLabel, "Score: 0");
	lv_obj_set_style_text_color(scoreLabel, lv_color_white(), 0);
	lv_obj_set_pos(scoreLabel, 5, 5);

	// Create plane (player)
	plane = lv_obj_create(bg);
	lv_obj_set_size(plane, PLANE_SIZE, PLANE_SIZE);
	lv_obj_set_style_bg_color(plane, lv_color_make(0, 255, 0), 0);  // Green plane
	lv_obj_set_style_bg_opa(plane, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(plane, 0, 0);
	lv_obj_set_style_radius(plane, 2, 0);

	// Create birds
	for(int i = 0; i < MAX_BIRDS; i++) {
		birds[i] = lv_obj_create(bg);
		lv_obj_set_size(birds[i], BIRD_SIZE, BIRD_SIZE);
		lv_obj_set_style_bg_color(birds[i], lv_color_make(255, 0, 0), 0);  // Red birds
		lv_obj_set_style_bg_opa(birds[i], LV_OPA_COVER, 0);
		lv_obj_set_style_border_width(birds[i], 0, 0);
		lv_obj_set_style_radius(birds[i], BIRD_SIZE / 2, 0);
		lv_obj_add_flag(birds[i], LV_OBJ_FLAG_HIDDEN);  // Initially hidden
		
		birdState[i].active = false;
		birdState[i].x = 0;
		birdState[i].y = -BIRD_SIZE;
	}
}

BirdDodger::~BirdDodger() {
	Events::unlisten(&queue);
}

void BirdDodger::onStart() {
	// Disable auto-sleep during game
	auto sleep = (SleepMan*) Services.get(Service::Sleep);
	sleep->enAutoSleep(false);

	// Listen for input events
	Events::listen(Facility::Input, &queue);

	// Initialize IMU filter
	IMU::Sample sample = imu->getSample();
	rollFilter.reset(sample.accelX);

	// Start game thread
	running = true;
	gameThread.start();
}

void BirdDodger::onStop() {
	// Stop game thread
	running = false;
	gameThread.stop(0);

	// Cleanup
	Events::unlisten(&queue);

	// Re-enable auto-sleep
	auto sleep = (SleepMan*) Services.get(Service::Sleep);
	sleep->enAutoSleep(true);
}

void BirdDodger::loop() {
	// Handle button input in main UI thread
	handleInput();
}

void BirdDodger::spawnBird() {
	// Find an inactive bird slot
	for(int i = 0; i < MAX_BIRDS; i++) {
		if(!birdState[i].active) {
			birdState[i].active = true;
			birdState[i].x = (esp_random() % (SCREEN_WIDTH - BIRD_SIZE));
			birdState[i].y = -BIRD_SIZE;
			lv_obj_clear_flag(birds[i], LV_OBJ_FLAG_HIDDEN);
			break;
		}
	}
}

void BirdDodger::updateGame() {
	if(gameOver) return;

	frameCounter++;

	// Spawn birds periodically
	if(frameCounter % SPAWN_INTERVAL == 0) {
		spawnBird();
	}

	// Update bird positions (move down)
	for(int i = 0; i < MAX_BIRDS; i++) {
		if(birdState[i].active) {
			birdState[i].y += scrollSpeed;
			
			// Deactivate birds that go off screen
			if(birdState[i].y > SCREEN_HEIGHT) {
				birdState[i].active = false;
				lv_obj_add_flag(birds[i], LV_OBJ_FLAG_HIDDEN);
				score++;  // Score increases when bird passes
			}
		}
	}

	// Check collisions
	checkCollisions();

	// Update plane from IMU (roll controls left-right)
	IMU::Sample sample = imu->getSample();
	float roll = rollFilter.update(sample.accelX);
	
	// Map roll to plane position (-0.3g to 0.3g → 0 to 1)
	float targetX = std::clamp((roll + 0.3f) / 0.6f, 0.0f, 1.0f);
	
	// Smooth plane movement
	planeX += (targetX - planeX) * PLANE_SPEED;
	planeX = std::clamp(planeX, 0.0f, 1.0f);

	// Gradually increase difficulty every 10 points
	if(score > 0 && score % 10 == 0 && scrollSpeed < MAX_SPEED) {
		if(score != lastDifficultyScore) {
			scrollSpeed = std::min(scrollSpeed + SPEED_INCREMENT, MAX_SPEED);
			lastDifficultyScore = score;
		}
	}
}

void BirdDodger::checkCollisions() {
	if(gameOver) return;

	float planePixelX = planeX * (SCREEN_WIDTH - PLANE_SIZE);
	
	// Check collision with each active bird
	for(int i = 0; i < MAX_BIRDS; i++) {
		if(!birdState[i].active) continue;
		
		// Simple bounding box collision
		bool collisionX = (planePixelX < birdState[i].x + BIRD_SIZE) && 
		                  (planePixelX + PLANE_SIZE > birdState[i].x);
		bool collisionY = (PLANE_Y < birdState[i].y + BIRD_SIZE) && 
		                  (PLANE_Y + PLANE_SIZE > birdState[i].y);
		
		if(collisionX && collisionY) {
			// Collision detected - game over
			gameOver = true;
			playMissSound();
			break;
		}
	}
}

void BirdDodger::updateUI() {
	// Update plane position
	int planePixelX = planeX * (SCREEN_WIDTH - PLANE_SIZE);
	lv_obj_set_pos(plane, planePixelX, PLANE_Y);

	// Update bird positions
	for(int i = 0; i < MAX_BIRDS; i++) {
		if(birdState[i].active) {
			lv_obj_set_pos(birds[i], (int)birdState[i].x, (int)birdState[i].y);
		}
	}

	// Update score
	char buf[32];
	if(gameOver) {
		snprintf(buf, sizeof(buf), "Score: %d\nGame Over!", score);
	} else {
		snprintf(buf, sizeof(buf), "Score: %d", score);
	}
	lv_label_set_text(scoreLabel, buf);
}

void BirdDodger::gameLoop() {
	const TickType_t frameTime = pdMS_TO_TICKS(16);  // ~60 FPS
	
	while(running) {
		TickType_t startTick = xTaskGetTickCount();
		
		// Update game state
		updateGame();
		
		// Update UI
		updateUI();
		
		// Maintain frame rate
		TickType_t elapsed = xTaskGetTickCount() - startTick;
		if(elapsed < frameTime) {
			vTaskDelay(frameTime - elapsed);
		}
	}
}

void BirdDodger::handleInput() {
	Event event;
	if(queue.get(event, 0)) {
		if(event.facility == Facility::Input) {
			auto data = (Input::Data*) event.data;
			
			if(data->action == Input::Data::Press) {
				if(data->btn == Input::Button::Alt) {
					// Exit game
					transition([](){ return std::make_unique<MainMenu>(); });
				} else if(data->btn == Input::Button::Select && gameOver) {
					// Restart game
					score = 0;
					lastDifficultyScore = 0;
					gameOver = false;
					scrollSpeed = INITIAL_SPEED;
					frameCounter = 0;
					
					// Reset all birds
					for(int i = 0; i < MAX_BIRDS; i++) {
						birdState[i].active = false;
						lv_obj_add_flag(birds[i], LV_OBJ_FLAG_HIDDEN);
					}
				}
			}
		}
	}
}

void BirdDodger::playMissSound() {
	audio->play({
		Chirp{ .startFreq = NOTE_C4, .endFreq = NOTE_C3, .duration = 200 },
		Chirp{ .startFreq = 0, .endFreq = 0, .duration = 100 },
		Chirp{ .startFreq = NOTE_C3, .endFreq = NOTE_C2, .duration = 300 }
	});
}
