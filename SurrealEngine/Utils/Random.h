// Random numbers of the game logic. Scripts and the engine code for actors use one generator, as the original engine does.
void SeedRandom(unsigned int seed); // The engine calls this one time at the start
int RandInt(int Min, int Max); // A random integer value between [Min, Max]
int RandInt(int Max); // A random integer value between [0, Max]
float FRand(); // A random float value between [0, 1]
float FRandRange(float Min, float Max);

// Random numbers for effects that only change the picture, for example fire textures. They come from a second
// generator. How many of these effects the player sees then does not change the random numbers of the game logic.
int VisualRandInt(int Max); // A random integer value between [0, Max]
float VisualFRand(); // A random float value between [0, 1]
