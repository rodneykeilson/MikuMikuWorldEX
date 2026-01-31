#pragma once
#include <string>
#include <vector>
#include <map>

namespace MikuMikuWorld
{
	struct Score;

	// Difficulty level profile from training data
	struct LevelProfile
	{
		int sampleCount;
		float npsMin, npsMax, npsAvg, npsStd;
		float peakNpsMin, peakNpsMax, peakNpsAvg;
		float totalNotesMin, totalNotesMax, totalNotesAvg;
		float jackPatternsAvg;
		float crossPatternsAvg;
	};

	class ScoreStats
	{
	  private:
		int hispeeds, taps, flicks, holds, guides, steps, traces, total, combo;
		float nps;  // Notes per second
		float peakNps;  // Peak NPS in 1-second windows
		float estimatedLevel;
		float durationSeconds;
		int jackPatterns;  // Same lane repeated quickly
		int crossPatterns;  // Cross-hand patterns
		
		// Loaded difficulty model
		static std::map<int, LevelProfile> difficultyModel;
		static bool modelLoaded;

		void resetCounts();
		void resetCombo();
		void calculatePatterns(const Score& score);
		float calculatePeakNPS(const Score& score);
		float estimateLevelFromModel() const;

	  public:
		ScoreStats();

		static void loadDifficultyModel(const std::string& path);

		void calculateStats(const Score& score);
		void calculateCombo(const Score& score);
		void calculateNPS(const Score& score);
		void reset();

		int getHiSpeeds() const { return hispeeds; }
		int getTaps() const { return taps; }
		int getFlicks() const { return flicks; }
		int getHolds() const { return holds; }
		int getSteps() const { return steps; }
		int getGuides() const { return guides; }
		int getTraces() const { return traces; }
		int getTotal() const { return total; }
		int getCombo() const { return combo; }
		float getNPS() const { return nps; }
		float getPeakNPS() const { return peakNps; }
		float getEstimatedLevel() const { return estimatedLevel; }
		float getDurationSeconds() const { return durationSeconds; }
		int getJackPatterns() const { return jackPatterns; }
		int getCrossPatterns() const { return crossPatterns; }
		std::string getEstimatedDifficulty() const;
	};
}