#include "ScoreStats.h"
#include "Score.h"
#include "Constants.h"
#include "IO.h"
#include <algorithm>
#include <cmath>

namespace MikuMikuWorld
{
	ScoreStats::ScoreStats() { reset(); }

	void ScoreStats::reset()
	{
		resetCounts();
		resetCombo();
		nps = 0.0f;
		estimatedLevel = 0.0f;
		durationSeconds = 0.0f;
	}

	void ScoreStats::resetCounts() { hispeeds = 1; taps = flicks = holds = steps = guides = traces = total = 0; }

	void ScoreStats::resetCombo() { combo = 0; }

	float ScoreStats::estimateLevel() const
	{
		// Polynomial difficulty estimation formula
		// Derived from regression analysis of 3249 training charts
		// Formula: level = 0.003278*notes + 3.1043*nps - 0.106609*nps² + 4.636
		// Performance: MAE=1.63, 70.9% within 2 levels, 38.8% within 1 level
		
		if (nps <= 0.0f || durationSeconds < 10.0f || total == 0)
			return 0.0f;  // Not enough data
		
		float notes_term = 0.003278f * (float)total;
		float nps_term = 3.1043f * nps;
		float nps_squared_term = -0.106609f * nps * nps;
		float intercept = 4.636f;
		
		float estimated = notes_term + nps_term + nps_squared_term + intercept;
		
		return std::clamp(estimated, 5.0f, 38.0f);
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
		estimatedLevel = estimateLevel();
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
			estimatedLevel = 0.0f;
			return;
		}

		// Calculate NPS (excluding guide notes, friction/trace notes, and damage notes which are non-scoring)
		int scoringNotes = 0;
		for (const auto& [id, note] : score.notes)
		{
			// Exclude friction/trace notes (decorative)
			if (note.friction)
				continue;
			
			// Exclude damage notes (non-scoring)
			if (note.getType() == NoteType::Damage)
				continue;
			
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
	}

	std::string ScoreStats::getEstimatedDifficulty() const
	{
		if (estimatedLevel <= 0.0f)
			return "N/A";
		
		// Round to nearest integer for cleaner display
		int roundedLevel = (int)std::round(estimatedLevel);
		
		// Simple format: just show level and NPS
		return IO::formatString("Lv. %d (NPS: %.1f)", roundedLevel, nps);
	}
}
