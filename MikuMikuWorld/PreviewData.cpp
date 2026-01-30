#include <queue>
#include <stdexcept>
#include "PreviewData.h"
#include "PreviewEngine.h"
#include "ApplicationConfiguration.h"
#include "Constants.h"
#include "ResourceManager.h"
#include "ScoreContext.h"
#include "Tempo.h"

namespace MikuMikuWorld::Engine
{
	struct DrawingHoldStep
	{
		int tick;
		double time;
		float left;
		float right;
		EaseType ease;
		int layer;
	};

	static void addHoldNote(DrawData& drawData, const HoldNote& holdNote, Score const &score);

	void DrawData::calculateDrawData(Score const &score)
	{
		this->clear();
		try
		{
			this->noteSpeed = config.pvNoteSpeed;
			std::map<int, Range> simBuilder;
			
		// MMWCC: unordered_map doesn't support reverse iteration
		// Collect all notes and sort by ID descending for reverse order
		std::vector<std::pair<id_t, Note>> sortedNotes;
		sortedNotes.reserve(score.notes.size());
		for (const auto& [id, note] : score.notes)
			sortedNotes.emplace_back(id, note);
		std::sort(sortedNotes.begin(), sortedNotes.end(), 
			[](const auto& a, const auto& b) { return a.first > b.first; });
		
		// MMWCC: Iterate all notes from all layers
		for (auto& [id, note] : sortedNotes)
		{
			maxTicks = std::max(note.tick, maxTicks);
			NoteType type = note.getType();
			
			// Skip mid-hold notes and hidden hold starts/ends
			if (type == NoteType::HoldMid)
				continue;
			if (type == NoteType::Hold)
			{
				auto holdIt = score.holdNotes.find(id);
				if (holdIt != score.holdNotes.end() && holdIt->second.startType != HoldNoteType::Normal)
					continue;
			}
			if (type == NoteType::HoldEnd)
			{
				auto holdIt = score.holdNotes.find(note.parentID);
				if (holdIt != score.holdNotes.end() && holdIt->second.endType != HoldNoteType::Normal)
					continue;
			}
					
			auto visual_tm = getNoteVisualTime(note, score, noteSpeed);
			drawingNotes.push_back(DrawingNote{note.ID, note.layer, visual_tm});

			// Find the max and min lane within the same height (visual_tm.max)
			float center = getNoteCenter(note);
			auto&& [it, has_emplaced] = simBuilder.try_emplace(note.tick, Range{center, center});
			auto& x_range = it->second;
			if (has_emplaced)
				continue;
			if (center < x_range.min)
				x_range.min = center;
			if (center > x_range.max)
				x_range.max = center;
		}

		float noteDuration = getNoteDuration(noteSpeed);
		for (const auto& [line_tick, x_range] : simBuilder)
		{
			if (x_range.min != x_range.max)
			{
				// Use layer 0 for simultaneous lines (they span all layers)
				double targetTime = accumulateScaledDuration(line_tick, TICKS_PER_BEAT, score.tempoChanges, score.hiSpeedChanges, 0);
				drawingLines.push_back(DrawingLine{ x_range, Range{ targetTime - getNoteDuration(noteSpeed), targetTime } });
			}
		}

		// MMWCC: unordered_map doesn't support reverse iteration
		// Collect all hold notes and sort by ID descending
		std::vector<std::pair<id_t, HoldNote>> sortedHolds;
		sortedHolds.reserve(score.holdNotes.size());
		for (const auto& [id, hold] : score.holdNotes)
			sortedHolds.emplace_back(id, hold);
		std::sort(sortedHolds.begin(), sortedHolds.end(),
			[](const auto& a, const auto& b) { return a.first > b.first; });

		for (auto& [id, holdNote] : sortedHolds)
		{
			addHoldNote(*this, holdNote, score);
		}
		}
		catch(const std::out_of_range& ex)
		{
			this->clear();
		}
	}

	void DrawData::clear()
	{
		drawingLines.clear();
		drawingNotes.clear();
		drawingHoldTicks.clear();
		drawingHoldSegments.clear();

		maxTicks = 1;
	}

	void addHoldNote(DrawData &drawData, const HoldNote &holdNote, Score const &score)
	{
		// Validate that the hold note references exist
		auto startIt = score.notes.find(holdNote.start.ID);
		auto endIt = score.notes.find(holdNote.end);
		if (startIt == score.notes.end() || endIt == score.notes.end())
		{
			// Skip invalid hold notes
			return;
		}
		
		float noteDuration = getNoteDuration(drawData.noteSpeed);
		const Note& startNote = startIt->second;
		const Note& endNote = endIt->second;
		
		float activeTime = accumulateDuration(startNote.tick, TICKS_PER_BEAT, score.tempoChanges);
		float startTime = activeTime;
		
		DrawingHoldStep head = {
			startNote.tick,
			accumulateScaledDuration(startNote.tick, TICKS_PER_BEAT, score.tempoChanges, score.hiSpeedChanges, startNote.layer),
			Engine::laneToLeft(startNote.lane),
			Engine::laneToLeft(startNote.lane) + startNote.width,
			holdNote.start.ease,
			startNote.layer
		};
		
		for (ptrdiff_t headIdx = -1, tailIdx = 0, stepSz = holdNote.steps.size(); headIdx < stepSz; ++tailIdx)
		{
			if (tailIdx < stepSz && holdNote.steps[tailIdx].type == HoldStepType::Skip)
				continue;
			
			HoldStep tailStep = tailIdx == stepSz ? HoldStep{ holdNote.end, HoldStepType::Hidden } : holdNote.steps[tailIdx];
			
			// Validate tail note exists
			auto tailIt = score.notes.find(tailStep.ID);
			if (tailIt == score.notes.end())
			{
				// Skip this segment if tail note doesn't exist
				++headIdx;
				continue;
			}
			const Note& tailNote = tailIt->second;
			auto easeFunction = getEaseFunction(head.ease);
			
			DrawingHoldStep tail = {
				tailNote.tick,
				accumulateScaledDuration(tailNote.tick, TICKS_PER_BEAT, score.tempoChanges, score.hiSpeedChanges, tailNote.layer),
				Engine::laneToLeft(tailNote.lane),
				Engine::laneToLeft(tailNote.lane) + tailNote.width,
				tailStep.ease,
				tailNote.layer
			};
			
			float endTime = accumulateDuration(tailNote.tick, TICKS_PER_BEAT, score.tempoChanges);
			drawData.drawingHoldSegments.push_back(DrawingHoldSegment {
				holdNote.end,
				startNote.ID,
				head.ease,
				holdNote.isGuide(),
				holdNote.guideColor,
				tailIdx,
				head.time, tail.time,
				head.left, head.right,
				tail.left, tail.right,
				startTime, endTime,
				activeTime,
				head.layer, tail.layer
			});
			startTime = endTime;
			
			while ((headIdx + 1) < tailIdx)
			{
				const HoldStep& skipStep = holdNote.steps[headIdx + 1];
				assert(skipStep.type == HoldStepType::Skip);
				
				// Validate skip note exists
				auto skipIt = score.notes.find(skipStep.ID);
				if (skipIt == score.notes.end())
				{
					++headIdx;
					continue;
				}
				const Note& skipNote = skipIt->second;
				if (skipNote.tick > tail.tick)
					break;
				double tickTime = accumulateScaledDuration(skipNote.tick, TICKS_PER_BEAT, score.tempoChanges, score.hiSpeedChanges, skipNote.layer);
				double tick_t = unlerpD(head.time, tail.time, tickTime);
				float skipLeft = easeFunction(head.left, tail.left, tick_t);
				float skipRight = easeFunction(head.right, tail.right, tick_t);
				drawData.drawingHoldTicks.push_back(DrawingHoldTick{
					skipStep.ID,
					skipNote.layer,
					skipLeft + (skipRight - skipLeft) / 2,
					Range{tickTime - noteDuration, tickTime}
				});
				++headIdx;
			}
			
			if (tailStep.type != HoldStepType::Hidden)
			{
				double tickTime = accumulateScaledDuration(tailNote.tick, TICKS_PER_BEAT, score.tempoChanges, score.hiSpeedChanges, tailNote.layer);
				drawData.drawingHoldTicks.push_back(DrawingHoldTick{
					tailNote.ID,
					tailNote.layer,
					getNoteCenter(tailNote),
					{tickTime - noteDuration, tickTime}
				});
			}
			head = tail;
			++headIdx;
		}
	}
}
