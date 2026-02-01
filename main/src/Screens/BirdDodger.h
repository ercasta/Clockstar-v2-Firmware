#ifndef CLOCKSTAR_FIRMWARE_BIRDDODGER_H
#define CLOCKSTAR_FIRMWARE_BIRDDODGER_H

#include "../LV_Interface/LVScreen.h"
#include "../Devices/IMU.h"
#include "../Services/ChirpSystem.h"
#include "../Util/Events.h"
#include "../Util/EMA.h"
#include "../Util/Threaded.h"
#include <atomic>

class BirdDodger : public LVScreen {
public:
	BirdDodger();
	~BirdDodger() override;

private:
	void onStart() override;
	void onStop() override;
	void loop() override;

	// UI Objects
	lv_obj_t* bg;
	lv_obj_t* scoreLabel;
	lv_obj_t* plane;
	static constexpr int MAX_BIRDS = 5;
	lv_obj_t* birds[MAX_BIRDS];

	// Game State
	struct Bird {
		float x, y;           // Bird position
		bool active;
	};

	float planeX;             // Plane position (0-1)
	Bird birdState[MAX_BIRDS];
	int score;
	int lastDifficultyScore;  // Track last score when difficulty increased
	bool gameOver;
	float scrollSpeed;

	// Game Constants
	static constexpr int SCREEN_WIDTH = 128;
	static constexpr int SCREEN_HEIGHT = 128;
	static constexpr int PLANE_SIZE = 8;
	static constexpr int BIRD_SIZE = 6;
	static constexpr int PLANE_Y = 100;  // Fixed Y position near bottom
	static constexpr float INITIAL_SPEED = 1.0f;
	static constexpr float SPEED_INCREMENT = 0.1f;
	static constexpr float MAX_SPEED = 3.0f;
	static constexpr float PLANE_SPEED = 0.03f;
	static constexpr int SPAWN_INTERVAL = 60;  // Frames between spawns

	// Game Thread
	ThreadedClosure gameThread;
	std::atomic_bool running{false};
	void gameLoop();

	// IMU
	IMU* imu;
	EMA rollFilter;
	static constexpr float filterStrength = 0.15f;

	// Services
	ChirpSystem* audio;
	EventQueue queue;

	// Game Logic
	void updateGame();
	void checkCollisions();
	void spawnBird();
	void updateUI();
	void playMissSound();

	// Input
	void handleInput();

	// Frame counter
	int frameCounter;
};

#endif // CLOCKSTAR_FIRMWARE_BIRDDODGER_H
