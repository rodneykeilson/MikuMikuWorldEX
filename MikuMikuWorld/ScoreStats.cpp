#include "ScoreStats.h"
#include "Score.h"
#include "Constants.h"
#include "IO.h"
#include "File.h"
#include "JsonIO.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <fstream>

namespace MikuMikuWorld
{
	// Static members
	std::map<int, LevelProfile> ScoreStats::difficultyModel;
	bool ScoreStats::modelLoaded = false;

	ScoreStats::ScoreStats() { reset(); }

	void ScoreStats::reset()
	{
		resetCounts();
		resetCombo();
		nps = 0.0f;
		peakNps = 0.0f;
		estimatedLevel = 0.0f;
		durationSeconds = 0.0f;
		jackPatterns = 0;
		crossPatterns = 0;
	}

	void ScoreStats::resetCounts() { hispeeds = 1; taps = flicks = holds = steps = guides = traces = total = 0; }

	void ScoreStats::resetCombo() { combo = 0; }

	void ScoreStats::loadDifficultyModel(const std::string& path)
	{
		if (modelLoaded)
			return;

		try
		{
			std::ifstream file(path);
			if (!file.is_open())
				return;

			nlohmann::json json;
			file >> json;

			if (!json.contains("level_profiles"))
				return;

			for (auto& [levelStr, data] : json["level_profiles"].items())
			{
				int level = std::stoi(levelStr);
				LevelProfile profile;
				
				profile.sampleCount = data.value("sample_count", 0);
				
				if (data.contains("nps"))
				{
					profile.npsMin = data["nps"].value("min", 0.0f);
					profile.npsMax = data["nps"].value("max", 0.0f);
					profile.npsAvg = data["nps"].value("avg", 0.0f);
					profile.npsStd = data["nps"].value("std", 0.0f);
				}
				
				if (data.contains("peak_nps"))
				{
					profile.peakNpsMin = data["peak_nps"].value("min", 0.0f);
					profile.peakNpsMax = data["peak_nps"].value("max", 0.0f);
					profile.peakNpsAvg = data["peak_nps"].value("avg", 0.0f);
				}
				
				if (data.contains("total_notes"))
				{
					profile.totalNotesMin = data["total_notes"].value("min", 0.0f);
					profile.totalNotesMax = data["total_notes"].value("max", 0.0f);
					profile.totalNotesAvg = data["total_notes"].value("avg", 0.0f);
				}
				
				profile.jackPatternsAvg = data.value("jack_patterns_avg", 0.0f);
				profile.crossPatternsAvg = data.value("cross_patterns_avg", 0.0f);
				
				difficultyModel[level] = profile;
			}

			modelLoaded = true;
		}
		catch (...)
		{
			// Failed to load model, will use fallback calculation
		}
	}

	void ScoreStats::calculatePatterns(const Score& score)
	{
		jackPatterns = 0;
		crossPatterns = 0;
		
		if (score.notes.empty())
			return;

		int jackThreshold = TICKS_PER_BEAT / 2;  // 1/8th note threshold for jacks
		int crossThreshold = TICKS_PER_BEAT / 4;  // 1/16th note threshold for cross-hand

		// Group notes by lane for jack detection
		std::unordered_map<int, std::vector<int>> laneNotes;  // lane -> ticks
		std::vector<std::pair<int, const Note*>> sortedNotes;  // tick, note ptr

		for (const auto& [id, note] : score.notes)
		{
			NoteType type = note.getType();
			// Note: Flick notes are Tap notes with isFlick() = true
			if (type == NoteType::Tap || type == NoteType::Hold)
			{
				laneNotes[note.lane].push_back(note.tick);
				sortedNotes.push_back({note.tick, &note});
			}
		}

		// Count jack patterns (same lane repeated quickly)
		for (auto& [lane, ticks] : laneNotes)
		{
			std::sort(ticks.begin(), ticks.end());
			for (size_t i = 1; i < ticks.size(); i++)
			{
				if (ticks[i] - ticks[i-1] <= jackThreshold)
					jackPatterns++;
			}
		}

		// Sort notes by tick for cross-hand detection
		std::sort(sortedNotes.begin(), sortedNotes.end(), 
		          [](const auto& a, const auto& b) { return a.first < b.first; });

		// Count cross-hand patterns
		for (size_t i = 1; i < sortedNotes.size(); i++)
		{
			int tickDiff = sortedNotes[i].first - sortedNotes[i-1].first;
			if (tickDiff > 0 && tickDiff <= crossThreshold)
			{
				const Note* prev = sortedNotes[i-1].second;
				const Note* curr = sortedNotes[i].second;
				
				float prevCenter = prev->lane + prev->width / 2.0f;
				float currCenter = curr->lane + curr->width / 2.0f;
				
				// Check if crossing center (lanes 0-5 vs 6-11)
				if ((prevCenter < 6 && currCenter >= 6) || (prevCenter >= 6 && currCenter < 6))
					crossPatterns++;
			}
		}
	}

	float ScoreStats::calculatePeakNPS(const Score& score)
	{
		if (score.notes.empty())
			return 0.0f;

		float baseBpm = score.tempoChanges.empty() ? 120.0f : score.tempoChanges.begin()->bpm;
		if (baseBpm <= 0)
			baseBpm = 120.0f;

		// Collect note times
		std::vector<float> noteTimes;
		for (const auto& [id, note] : score.notes)
		{
			NoteType type = note.getType();
			// Note: Flick notes are Tap notes with isFlick() = true
			if (type == NoteType::Tap || type == NoteType::Hold)
			{
				float beats = (float)note.tick / TICKS_PER_BEAT;
				float seconds = (beats / baseBpm) * 60.0f;
				noteTimes.push_back(seconds);
			}
		}

		if (noteTimes.empty())
			return 0.0f;

		std::sort(noteTimes.begin(), noteTimes.end());

		// Calculate peak NPS using 1-second sliding window
		float maxNps = 0.0f;
		const float windowSize = 1.0f;

		for (size_t i = 0; i < noteTimes.size(); i++)
		{
			float windowEnd = noteTimes[i] + windowSize;
			int count = 0;
			
			for (size_t j = i; j < noteTimes.size() && noteTimes[j] <= windowEnd; j++)
				count++;
			
			float windowNps = (float)count / windowSize;
			maxNps = std::max(maxNps, windowNps);
		}

		return maxNps;
	}

	float ScoreStats::estimateLevelFromModel() const
	{
		if (!modelLoaded || difficultyModel.empty())
		{
			// Fallback: improved linear regression with pattern adjustments
			float baseLevel = 2.5f * nps + 5.0f;
			float peakAdjust = (peakNps - nps * 2.0f) * 0.5f;  // Peak complexity bonus
			float patternAdjust = jackPatterns * 0.01f + crossPatterns * 0.02f;
			return std::clamp(baseLevel + peakAdjust + patternAdjust, 5.0f, 37.0f);
		}

		// Find best matching level using weighted distance
		float bestScore = std::numeric_limits<float>::max();
		int bestLevel = 20;

		for (const auto& [level, profile] : difficultyModel)
		{
			if (profile.sampleCount < 3)
				continue;  // Skip levels with too few samples

			// Calculate normalized distance from this level's profile
			float npsDistance = 0.0f;
			if (profile.npsStd > 0)
				npsDistance = std::abs(nps - profile.npsAvg) / profile.npsStd;
			else
				npsDistance = std::abs(nps - profile.npsAvg) / (profile.npsAvg + 0.1f);

			float peakNpsDistance = 0.0f;
			if (profile.peakNpsAvg > 0)
				peakNpsDistance = std::abs(peakNps - profile.peakNpsAvg) / profile.peakNpsAvg;

			float totalNotesDistance = 0.0f;
			if (profile.totalNotesAvg > 0)
				totalNotesDistance = std::abs((float)total - profile.totalNotesAvg) / profile.totalNotesAvg;

			// Weighted combination (NPS is most important)
			float score = npsDistance * 1.0f + peakNpsDistance * 0.5f + totalNotesDistance * 0.3f;

			if (score < bestScore)
			{
				bestScore = score;
				bestLevel = level;
			}
		}

		// Interpolate between levels for smoother estimation
		float estimatedLevelFloat = (float)bestLevel;

		// Adjust based on how close we are to adjacent levels
		if (difficultyModel.count(bestLevel - 1) && difficultyModel.count(bestLevel + 1))
		{
			const auto& lowerProfile = difficultyModel[bestLevel - 1];
			const auto& upperProfile = difficultyModel[bestLevel + 1];
			const auto& currentProfile = difficultyModel[bestLevel];

			// If NPS is closer to lower level, subtract; if closer to upper, add
			if (nps < currentProfile.npsAvg)
			{
				float ratio = (currentProfile.npsAvg - nps) / (currentProfile.npsAvg - lowerProfile.npsAvg + 0.001f);
				estimatedLevelFloat -= std::clamp(ratio * 0.5f, 0.0f, 0.5f);
			}
			else
			{
				float ratio = (nps - currentProfile.npsAvg) / (upperProfile.npsAvg - currentProfile.npsAvg + 0.001f);
				estimatedLevelFloat += std::clamp(ratio * 0.5f, 0.0f, 0.5f);
			}
		}

		return std::clamp(estimatedLevelFloat, 5.0f, 37.0f);
	}

	void ScoreStats::calculateStats(const Score& score)
	{
		hispeeds = score.hiSpeedChanges.size();

		taps = std::count_if(score.notes.begin(), score.notes.end(),
		                     [](const auto& n)
		                     {
			                     const Note& note = n.second;
			                     return note.getType() == NoteType::Tap && !note.isFlick() &&
			                            !note.friction;
		                     });

		holds = std::count_if(score.notes.begin(), score.notes.end(),
		                      [&](const auto& n) {
			                      if (n.second.getType() != NoteType::Hold) return false;
			                      auto it = score.holdNotes.find(n.first);
			                      return it != score.holdNotes.end() && !it->second.isGuide();
		                      });

		steps =
		    std::count_if(score.notes.begin(), score.notes.end(),
		                  [](const auto& n) { return n.second.getType() == NoteType::HoldMid; });

		guides = std::count_if(score.notes.begin(), score.notes.end(),
		                       [&](const auto& n) {
			                       if (n.second.getType() != NoteType::Hold) return false;
			                       auto it = score.holdNotes.find(n.first);
			                       return it != score.holdNotes.end() && it->second.isGuide();
		                       });

		flicks = std::count_if(score.notes.begin(), score.notes.end(),
		                       [](const auto& n) { return n.second.isFlick(); });

		traces = std::count_if(score.notes.begin(), score.notes.end(),
		                       [](const auto& n) { return n.second.friction; });

		total = score.notes.size();
		calculateCombo(score);
		calculateNPS(score);
	}

	void ScoreStats::calculateCombo(const Score& score)
	{
		resetCombo();
		combo = score.notes.size();

		constexpr int halfBeat = TICKS_PER_BEAT / 2;
		for (const auto& [id, hold] : score.holdNotes)
		{
			if (hold.isGuide())
			{
				// Guide holds are not included
				combo -= 2 + hold.steps.size();
				continue;
			}

			// Hidden hold starts and ends do not count towards combo
			if (hold.startType != HoldNoteType::Normal)
				combo--;

			if (hold.endType != HoldNoteType::Normal)
				combo--;

			combo -= std::count_if(hold.steps.begin(), hold.steps.end(),
			                       [](const HoldStep& step)
			                       { return step.type == HoldStepType::Hidden; });

			auto startIt = score.notes.find(id);
			auto endIt = score.notes.find(hold.end);
			if (startIt == score.notes.end() || endIt == score.notes.end())
				continue;

			int startTick = startIt->second.tick;
			int endTick = endIt->second.tick;
			int eighthTick = startTick;

			eighthTick += halfBeat;
			if (eighthTick % halfBeat)
				eighthTick -= (eighthTick % halfBeat);

			// hold <= 1/8th long
			if (eighthTick == startTick || eighthTick == endTick)
				continue;

			if (endTick % halfBeat)
				endTick += halfBeat - (endTick % halfBeat);

			combo += (endTick - eighthTick) / halfBeat;
		}
	}

	void ScoreStats::calculateNPS(const Score& score)
	{
		if (score.notes.empty() || score.tempoChanges.empty())
		{
			nps = 0.0f;
			peakNps = 0.0f;
			estimatedLevel = 0.0f;
			durationSeconds = 0.0f;
			return;
		}

		// Find first and last note ticks
		int firstTick = INT_MAX;
		int lastTick = 0;
		
		for (const auto& [id, note] : score.notes)
		{
			firstTick = std::min(firstTick, note.tick);
			lastTick = std::max(lastTick, note.tick);
		}

		if (lastTick <= firstTick)
		{
			nps = 0.0f;
			peakNps = 0.0f;
			estimatedLevel = 0.0f;
			durationSeconds = 0.0f;
			return;
		}

		// Calculate duration in seconds using tempo changes
		float baseBpm = score.tempoChanges.empty() ? 120.0f : score.tempoChanges.begin()->bpm;
		
		// Use base BPM for simple calculation
		float tickDuration = (float)(lastTick - firstTick);
		float beatsPerTick = 1.0f / (float)TICKS_PER_BEAT;
		float durationBeats = tickDuration * beatsPerTick;
		float durationMinutes = durationBeats / baseBpm;
		durationSeconds = durationMinutes * 60.0f;

		if (durationSeconds <= 0.0f)
		{
			nps = 0.0f;
			peakNps = 0.0f;
			estimatedLevel = 0.0f;
			return;
		}

		// Calculate NPS (excluding guide notes which are non-scoring)
		int scoringNotes = 0;
		for (const auto& [id, note] : score.notes)
		{
			// Check if this is a guide note (part of a guide hold)
			bool isGuide = false;
			if (note.getType() == NoteType::Hold)
			{
				auto holdIt = score.holdNotes.find(id);
				if (holdIt != score.holdNotes.end() && holdIt->second.isGuide())
					isGuide = true;
			}
			else if (note.getType() == NoteType::HoldEnd)
			{
				// Check parent hold
				if (note.parentID > 0)
				{
					auto holdIt = score.holdNotes.find(note.parentID);
					if (holdIt != score.holdNotes.end() && holdIt->second.isGuide())
						isGuide = true;
				}
			}
			
			if (!isGuide)
				scoringNotes++;
		}

		nps = (float)scoringNotes / durationSeconds;

		// Calculate peak NPS
		peakNps = calculatePeakNPS(score);

		// Calculate pattern complexity
		calculatePatterns(score);

		// Estimate level using the trained model
		estimatedLevel = estimateLevelFromModel();
	}

	std::string ScoreStats::getEstimatedDifficulty() const
	{
		if (estimatedLevel <= 0.0f)
			return "N/A";
		
		// Round to nearest 0.5
		float roundedLevel = std::round(estimatedLevel * 2.0f) / 2.0f;
		
		// Format with peak NPS info
		if (roundedLevel == std::floor(roundedLevel))
			return IO::formatString("Lv. %d (NPS: %.1f, Peak: %.1f)", (int)roundedLevel, nps, peakNps);
		else
			return IO::formatString("Lv. %.1f (NPS: %.1f, Peak: %.1f)", roundedLevel, nps, peakNps);
	}
}
