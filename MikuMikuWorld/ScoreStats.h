#pragma once
#include <string>
#include <vector>
#include <map>

namespace MikuMikuWorld
{
	struct Score;



	class ScoreStats
	{
	  private:
		int hispeeds, taps, flicks, holds, guides, steps, traces, total, combo;
		float nps;  // Notes per second
		float estimatedLevel;
		float durationSeconds;

		void resetCounts();
		void resetCombo();
		float estimateLevel() const;  // Simple polynomial formula

	  public:
		ScoreStats();


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
		float getEstimatedLevel() const { return estimatedLevel; }
		float getDurationSeconds() const { return durationSeconds; }
		std::string getEstimatedDifficulty() const;
	};
}